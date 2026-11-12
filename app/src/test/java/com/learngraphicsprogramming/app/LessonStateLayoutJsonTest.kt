package com.learngraphicsprogramming.app

import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class LessonStateLayoutJsonTest {
    private val paragraph = JSONObject().put("type", "paragraph").put("text", "Original explanation.")
    private val editor = JSONObject().put("type", "cpp_compiler").put("initialCode", "int main() { return 0; }")
    private val prediction = JSONObject().put("type", "output_prediction").put("id", "original_prediction")
        .put("question", "Predict the output.").put("answers", JSONArray().put("Original"))
    private val extraEditor = JSONObject().put("type", "cpp_compiler").put("initialCode", "int main() { return 1; }")

    private fun manifest(pages: List<List<Pair<String, JSONObject>>>): JSONArray = JSONArray().apply {
        pages.forEachIndexed { index, blocks -> put(JSONObject().put("id", "p$index").put("blocks",
            JSONArray().apply { blocks.forEach { (id, block) -> put(JSONObject().put("id", id)
                .put("sha256", LessonStateLayoutJson.stateFingerprint(block))) } })) }
    }

    private fun fixture(): JSONObject {
        val original = manifest(listOf(listOf("prose" to paragraph, "editor" to editor),
            listOf("prediction" to prediction)))
        val translated = manifest(listOf(listOf("prose" to paragraph, "editor" to editor),
            listOf("translated_note" to JSONObject().put("type", "note").put("text", "A note, not a puzzle."))))
        val changed = JSONObject(paragraph.toString()).put("text", "A corrected explanation.")
        val family = JSONObject().put("es", JSONObject().put("sha256", "a".repeat(64)).put("origin", original))
            .put("en", JSONObject().put("sha256", "b".repeat(64)).put("origin", translated))
            .put("fr", JSONObject().put("sha256", "c".repeat(64)).put("origin", translated))
        val current = JSONArray().put(JSONObject().put("id", "p1").put("blocks",
            JSONArray().put("prediction").put("prose").put("new_original_run")))
            .put(JSONObject().put("id", "p0").put("blocks", JSONArray().put("editor")))
        val additions = JSONArray().put(JSONObject().put("id", "new_original_run")
            .put("sha256", LessonStateLayoutJson.stateFingerprint(extraEditor))
            .put("storage", JSONObject().put("page", 2).put("block", 0)).put("progressPage", "p1"))
        val editorial = JSONArray().put(JSONObject().put("id", "prose")
            .put("beforeSha256", LessonStateLayoutJson.fingerprint(paragraph))
            .put("afterSha256", LessonStateLayoutJson.fingerprint(changed)))
        return JSONObject().put("id", "fixture").put("pages", JSONArray()
            .put(JSONObject().put("blocks", JSONArray().put(prediction).put(changed).put(extraEditor)))
            .put(JSONObject().put("blocks", JSONArray().put(editor))))
            .put("stateLayout", JSONObject().put("version", 2).put("locale", "es")
                .put("origin", original).put("current", current).put("family", family)
                .put("additions", additions).put("editorial", editorial))
    }

    @Test fun versionOneKeepsItsExactOriginalContractAndAddresses() {
        val original = manifest(listOf(listOf("prose" to paragraph, "editor" to editor),
            listOf("prediction" to prediction)))
        val document = JSONObject().put("pages", JSONArray()
            .put(JSONObject().put("blocks", JSONArray().put(paragraph).put(editor)))
            .put(JSONObject().put("blocks", JSONArray().put(prediction))))
            .put("stateLayout", JSONObject().put("version", 1).put("origin", original)
                .put("current", JSONArray().put(JSONObject().put("id", "p0")
                    .put("blocks", JSONArray().put("prose").put("editor")))
                    .put(JSONObject().put("id", "p1").put("blocks", JSONArray().put("prediction")))))
        val layout = LessonStateLayoutJson.read(document)!!
        assertEquals(1, layout.version)
        assertEquals(layout.legacyLineage, layout.lineage)
        assertNull(layout.locale)
        assertNull(layout.additionNamespace(0, 1))
        assertEquals(LessonStatePosition(0, 1), layout.storagePosition(0, 1))
        document.getJSONObject("stateLayout").put("family", JSONObject())
        assertTrue(runCatching { LessonStateLayoutJson.read(document) }.exceptionOrNull()
            is LessonStateLayoutException)
    }

    @Test fun editorialCorrectionReorderingAndNewActivityShareOneSafeTransition() {
        val layout = LessonStateLayoutJson.read(fixture())!!
        assertEquals(2, layout.version)
        assertEquals("es", layout.locale)
        assertEquals(LessonStatePosition(1, 0), layout.storagePosition(0, 0))
        assertEquals(LessonStatePosition(0, 0), layout.storagePosition(0, 1))
        assertEquals(LessonStatePosition(2, 0), layout.storagePosition(0, 2))
        assertEquals(LessonStatePosition(0, 1), layout.storagePosition(1, 0))
        assertNull(layout.additionNamespace(1, 0))
        assertTrue(layout.additionNamespace(0, 2)!!.matches(Regex("[0-9a-f]{64}")))
        assertEquals(2, layout.progressGroups.size)
        assertEquals(listOf(LessonStatePosition(0, 0), LessonStatePosition(0, 2)), layout.progressGroups[1])
        val read = layout.legacyReadBlocks(true, 1)
        assertFalse("new_original_run" in read)
        assertFalse(layout.progressPageRead(1, layout.legacyReadPages(true, 1), read))
        assertFalse(layout.pageRead(0, layout.legacyReadPages(true, 1), read))
        val (pages, blocks) = layout.visited(0, emptySet(), read)
        assertTrue(layout.pageRead(0, pages, blocks))
        assertEquals("translated_note", layout.family.getValue("en").origin[1].blocks.single())
    }

    @Test fun newActivitiesKeepTheirNamespaceAcrossFurtherAdditionsAndPermutations() {
        val first = LessonStateLayoutJson.read(fixture())!!
        val document = fixture()
        val metadata = document.getJSONObject("stateLayout")
        metadata.getJSONArray("additions").put(JSONObject().put("id", "second_new_editor")
            .put("sha256", LessonStateLayoutJson.stateFingerprint(extraEditor))
            .put("storage", JSONObject().put("page", 2).put("block", 1)).put("progressPage", "p0"))
        metadata.getJSONArray("current").getJSONObject(1).getJSONArray("blocks").put("second_new_editor")
        document.getJSONArray("pages").getJSONObject(1).getJSONArray("blocks").put(extraEditor)
        val second = LessonStateLayoutJson.read(document)!!
        assertEquals(first.lineage, second.lineage)
        assertEquals(first.additionNamespace(0, 2), second.additionNamespace(0, 2))
        assertNotEquals(second.additionNamespace(0, 2), second.additionNamespace(1, 1))
        assertNotEquals(first.lineage, first.legacyLineage)
        assertEquals(StableLessonStateLayout(first.origin, first.origin).lineage, first.legacyLineage)
    }

    @Test fun familyAndLocaleArePartOfTheLineageButDisplayOrderIsNot() {
        val first = LessonStateLayoutJson.read(fixture())!!
        val metadata = fixture().getJSONObject("stateLayout")
        val family = first.family.toMutableMap().apply {
            this["fr"] = getValue("fr").copy(sha256 = "d".repeat(64))
        }
        val changed = StableLessonStateLayout(first.origin, first.current, 2, "es", family, first.additions)
        assertNotEquals(first.lineage, changed.lineage)
        val reordered = StableLessonStateLayout(first.origin, first.current.reversed(), 2, "es",
            first.family, first.additions)
        assertEquals(first.lineage, reordered.lineage)
        assertEquals(2, metadata.getInt("version"))
    }

    @Test fun oldSlotsDuplicateNewSlotsForeignFamiliesAndTamperedAnswersAreRejected() {
        val changes: List<(JSONObject) -> Unit> = listOf(
            { it.getJSONObject("stateLayout").getJSONArray("additions").getJSONObject(0)
                .getJSONObject("storage").put("page", 1) },
            { it.getJSONObject("stateLayout").put("locale", "de") },
            { it.getJSONObject("stateLayout").getJSONObject("family").getJSONObject("es").put("sha256", "bad") },
            { it.getJSONObject("stateLayout").getJSONObject("family").getJSONObject("es")
                .getJSONArray("origin").getJSONObject(0).put("id", "foreign_page") },
            { it.getJSONArray("pages").getJSONObject(0).getJSONArray("blocks").getJSONObject(0)
                .put("answers", JSONArray().put("An altered answer")) },
            { it.getJSONArray("pages").getJSONObject(1).getJSONArray("blocks").getJSONObject(0)
                .put("initialCode", "int main() { return 9; }") },
            { it.getJSONObject("stateLayout").getJSONArray("current").getJSONObject(0)
                .getJSONArray("blocks").put(2, "editor") },
            { it.getJSONObject("stateLayout").getJSONArray("editorial").getJSONObject(0)
                .put("afterSha256", "f".repeat(64)) }
        )
        changes.forEach { change ->
            val document = fixture()
            change(document)
            assertTrue(runCatching { LessonStateLayoutJson.read(document) }.exceptionOrNull()
                is LessonStateLayoutException)
        }
    }

    @Test fun editorialHistoryCanAccumulateButCannotLoopOrSkipItsPreviousCorrection() {
        val document = fixture()
        val current = document.getJSONArray("pages").getJSONObject(0).getJSONArray("blocks").getJSONObject(1)
        val previous = LessonStateLayoutJson.fingerprint(current)
        current.put("text", "A second corrected explanation.")
        val edits = document.getJSONObject("stateLayout").getJSONArray("editorial")
        edits.put(JSONObject().put("id", "prose").put("beforeSha256", previous)
            .put("afterSha256", LessonStateLayoutJson.fingerprint(current)))
        assertNotNull(LessonStateLayoutJson.read(document))
        edits.getJSONObject(1).put("beforeSha256", "e".repeat(64))
        assertTrue(runCatching { LessonStateLayoutJson.read(document) }.exceptionOrNull()
            is LessonStateLayoutException)
    }

    private fun formattedEditor(): JSONObject {
        val document = fixture()
        val editorBlocks = document.getJSONArray("pages").getJSONObject(1).getJSONArray("blocks")
        val previous = JSONObject(editorBlocks.getJSONObject(0).toString())
        val revised = JSONObject(previous.toString())
            .put("initialCode", "int main() {\n\n    return 0;\n}\n")
        editorBlocks.put(0, revised)
        val layout = document.getJSONObject("stateLayout")
        layout.getJSONArray("editorial").put(JSONObject().put("id", "editor")
            .put("beforeSha256", LessonStateLayoutJson.fingerprint(previous))
            .put("afterSha256", LessonStateLayoutJson.fingerprint(revised)))
        layout.put("sourceFormatRevisions", JSONArray().put(JSONObject()
            .put("id", "editor").put("block", previous)))
        return document
    }

    @Test fun formattingPreservesTheOldAndroidStateLineageAndStorageKeys() {
        val before = LessonStateLayoutJson.read(fixture())!!
        val document = formattedEditor()
        val after = LessonStateLayoutJson.read(document)!!
        assertEquals(before.legacyLineage, after.legacyLineage)
        assertEquals(before.lineage, after.lineage)
        assertEquals(before.storagePosition(1, 0), after.storagePosition(1, 0))
        assertEquals(before.additionNamespace(0, 2), after.additionNamespace(0, 2))
        assertEquals("int main() {\n\n    return 0;\n}\n",
            document.getJSONArray("pages").getJSONObject(1).getJSONArray("blocks")
                .getJSONObject(0).getString("initialCode"))
    }

    @Test fun formattingRejectsStaleSnapshotsAlteredResultsAndMissingProvenance() {
        val changes: List<(JSONObject) -> Unit> = listOf(
            { it.getJSONObject("stateLayout").getJSONArray("sourceFormatRevisions")
                .getJSONObject(0).getJSONObject("block")
                .put("initialCode", "int main() { return 2; }") },
            { it.getJSONArray("pages").getJSONObject(1).getJSONArray("blocks")
                .getJSONObject(0).put("initialCode", "int main() { return 9; }") },
            { it.getJSONObject("stateLayout").getJSONArray("editorial").remove(1) },
            { it.getJSONObject("stateLayout").getJSONArray("sourceFormatRevisions")
                .getJSONObject(0).put("id", "prediction") },
            { it.getJSONObject("stateLayout").getJSONArray("sourceFormatRevisions")
                .getJSONObject(0).put("another", true) }
        )
        changes.forEach { change ->
            val document = formattedEditor()
            change(document)
            assertTrue(runCatching { LessonStateLayoutJson.read(document) }.exceptionOrNull()
                is LessonStateLayoutException)
        }
    }

    @Test fun cppCmakeLiteralAndMacroSourceEditsRemainProtected() {
        val before = JSONObject().put("type", "code")
            .put("code", "int main(){int x=2;return x;}")
        val after = JSONObject(before.toString())
            .put("code", "int main() {\n  int x = 2;\n\n  return x;\n}")
        assertEquals(before.getString("code"),
            CurricularSourceFormatCompat.project(after, before).getString("code"))
        for ((original, changed) in listOf(
            "int x=2;" to "int x=3;",
            "int line=__LINE__;" to "\nint line=__LINE__;",
            "auto value=\"abc\"_tag;" to "auto value=\"abc\" _tag;",
            "auto value=R\"x(a\nb)x\";" to "auto value=R\"x(a\n\nb)x\";",
            "#define X(v) \\\n ((v)+1)\nint result=X(2);" to
                "#define X(v) \\\n ((v)+1)\n\nint result=X(2);"
        )) {
            val a = JSONObject().put("type", "code").put("code", original)
            val b = JSONObject(a.toString()).put("code", changed)
            assertTrue(runCatching { CurricularSourceFormatCompat.project(b, a) }.isFailure)
        }
        val cmake = JSONObject().put("type", "code").put("language", "cmake")
            .put("code", "add_executable(game main.cpp)\n")
        val spaced = JSONObject(cmake.toString())
            .put("code", "add_executable(\n  game main.cpp\n)\n")
        assertEquals(cmake.getString("code"),
            CurricularSourceFormatCompat.project(spaced, cmake).getString("code"))
        val line = JSONObject().put("type", "code").put("language", "cmake")
            .put("code", "message(\"\${CMAKE_CURRENT_LIST_LINE}\")")
        val moved = JSONObject(line.toString()).put("code", "\n" + line.getString("code"))
        assertTrue(runCatching { CurricularSourceFormatCompat.project(moved, line) }.isFailure)
    }

    @Test fun imageDescriptionsCanChangeButImageResourcesRemainProtected() {
        val original = JSONObject().put("type", "image")
            .put("imageId", "example_image")
            .put("src", "images/programming1/example.png")
            .put("alt", "Original alternative text")
            .put("caption", "Original caption")
        val corrected = JSONObject(original.toString())
            .put("alt", "Corrected accessible description")
            .put("caption", "Corrected caption")
        assertNotEquals(LessonStateLayoutJson.fingerprint(original),
            LessonStateLayoutJson.fingerprint(corrected))
        assertEquals(LessonStateLayoutJson.stateFingerprint(original),
            LessonStateLayoutJson.stateFingerprint(corrected))
        val document = JSONObject()
            .put("pages", JSONArray().put(JSONObject()
                .put("blocks", JSONArray().put(corrected))))
            .put("stateLayout", JSONObject().put("version", 1)
                .put("origin", manifest(listOf(listOf("visual" to original))))
                .put("current", JSONArray().put(JSONObject()
                    .put("id", "p0").put("blocks", JSONArray().put("visual")))))
        val layout = requireNotNull(LessonStateLayoutJson.read(document))
        assertEquals(LessonStatePosition(0, 0), layout.storagePosition(0, 0))
        for ((field, replacement) in listOf(
            "src" to "images/programming1/forged.png",
            "imageId" to "other_image"
        )) {
            val altered = JSONObject(document.toString())
            altered.getJSONArray("pages").getJSONObject(0)
                .getJSONArray("blocks").getJSONObject(0).put(field, replacement)
            assertTrue("Changed image resource was accepted: $field",
                runCatching { LessonStateLayoutJson.read(altered) }.exceptionOrNull()
                    is LessonStateLayoutException)
        }
    }

    @Test fun unknownVersionsAndFieldsNeverDowngradeToLegacyBehavior() {
        assertNull(LessonStateLayoutJson.read(JSONObject().put("pages", JSONArray())))
        for (value in listOf<Any>(0, 3, "2", true, JSONObject.NULL)) {
            val document = fixture()
            document.getJSONObject("stateLayout").put("version", value)
            assertTrue(runCatching { LessonStateLayoutJson.read(document) }.exceptionOrNull()
                is LessonStateLayoutException)
        }
        val document = fixture()
        document.getJSONObject("stateLayout").put("unexpected", true)
        assertTrue(runCatching { LessonStateLayoutJson.read(document) }.exceptionOrNull()
            is LessonStateLayoutException)
    }
}
