package com.learngraphicsprogramming.app

import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

/**
 * Real published Programación 1 lesson, inspected without writing any content
 * or touching device preferences. Work retains exclusive curricular ownership.
 */
class RealCodeFormatPilotAndroidTest {
    private val lessonRelative =
        "programming/cpp_fundamentals/expresiones_y_decisiones/" +
            "lesson_construir_decisiones_desde_un_problema.json"

    private fun originalLesson(): JSONObject {
        val relative = "src/main/assets/content/es/$lessonRelative"
        val source = listOf(File(relative), File("app/$relative"))
            .firstOrNull(File::isFile) ?: error("Missing authentic U4 curriculum fixture")
        return JSONObject(source.readText(Charsets.UTF_8))
    }

    private val pages = listOf(3, 5)

    private fun addPhysicalSpacing(original: String): String {
        var source = original
        for ((before, after) in listOf(
            "    const bool acepta_sobrantes = true;\n    if (" to
                "    const bool acepta_sobrantes = true;\n\n    if (",
            "        const int sobrantes = fichas % capacidad;\n        if (" to
                "        const int sobrantes = fichas % capacidad;\n\n        if (",
            "    }\n    return 0;\n}\n" to "    }\n\n    return 0;\n}\n"
        )) {
            require(source.split(before).size == 2) {
                "The published lesson lost an exactly reviewed source-layout anchor"
            }
            source = source.replace(before, after)
        }
        return source
    }

    private fun realForm(original: JSONObject): JSONObject {
        val formatted = JSONObject(original.toString())
        val state = formatted.getJSONObject("stateLayout")
        require(state.getInt("version") == 2 && state.getString("locale") == "es")
        require(state.optJSONArray("sourceFormatRevisions") == null)
        val editions = state.getJSONArray("editorial")
        val archives = JSONArray()
        for (page in pages) {
            val index = 2
            val block = formatted.getJSONArray("pages").getJSONObject(page)
                .getJSONArray("blocks").getJSONObject(index)
            val historical = JSONObject(block.toString())
            require(block.getString("type") == "code")
            val identity = state.getJSONArray("current").getJSONObject(page)
                .getJSONArray("blocks").getString(index)
            require(identity == if (page == 3) "block_0003_0002" else "block_0005_0002")
            block.put("code", addPhysicalSpacing(block.getString("code")))
            archives.put(JSONObject().put("id", identity).put("block", historical))
            editions.put(JSONObject().put("id", identity)
                .put("beforeSha256", LessonStateLayoutJson.fingerprint(historical))
                .put("afterSha256", LessonStateLayoutJson.fingerprint(block)))
        }
        state.put("sourceFormatRevisions", archives)
        return formatted
    }

    private fun pilotDocuments(published: JSONObject = originalLesson()): Pair<JSONObject, JSONObject> {
        val state = published.getJSONObject("stateLayout")
        val revisions = state.optJSONArray("sourceFormatRevisions")
        if (revisions == null || revisions.length() == 0)
            return published to realForm(published)

        // Work may have already published these two exact format-only revisions
        // on main. Restore their genuine archived source in memory, never
        // rewrite the shipped file or regenerate its immutable origin.
        require(revisions.length() == pages.size)
        val restored = JSONObject(published.toString())
        val oldState = restored.getJSONObject("stateLayout")
        val edits = oldState.getJSONArray("editorial")
        for ((ordinal, page) in pages.withIndex()) {
            val original = revisions.getJSONObject(ordinal)
            val id = state.getJSONArray("current").getJSONObject(page)
                .getJSONArray("blocks").getString(2)
            require(id == original.getString("id") &&
                id == if (page == 3) "block_0003_0002" else "block_0005_0002")
            val archived = original.getJSONObject("block")
            val current = published.getJSONArray("pages").getJSONObject(page)
                .getJSONArray("blocks").getJSONObject(2)
            require(current.getString("code") == addPhysicalSpacing(archived.getString("code")))
            restored.getJSONArray("pages").getJSONObject(page)
                .getJSONArray("blocks").put(2, JSONObject(archived.toString()))
        }
        for (ordinal in pages.indices.reversed()) {
            val page = pages[ordinal]
            val oldBlock = revisions.getJSONObject(ordinal).getJSONObject("block")
            val newBlock = published.getJSONArray("pages").getJSONObject(page)
                .getJSONArray("blocks").getJSONObject(2)
            val edge = edits.getJSONObject(edits.length() - 1)
            require(edge.getString("id") == revisions.getJSONObject(ordinal).getString("id") &&
                edge.getString("beforeSha256") == LessonStateLayoutJson.fingerprint(oldBlock) &&
                edge.getString("afterSha256") == LessonStateLayoutJson.fingerprint(newBlock))
            edits.remove(edits.length() - 1)
        }
        oldState.remove("sourceFormatRevisions")
        requireNotNull(LessonStateLayoutJson.read(restored))
        requireNotNull(LessonStateLayoutJson.read(published))
        return restored to published
    }

    @Test fun bothPublishedCodeBlocksRetainAndroidProgressLineageAndAnswers() {
        val (authentic, rewritten) = pilotDocuments()
        val unformatted = requireNotNull(LessonStateLayoutJson.read(authentic))
        val formatted = requireNotNull(LessonStateLayoutJson.read(rewritten))

        assertEquals(authentic.getString("id"), rewritten.getString("id"))
        assertEquals(unformatted.lineage, formatted.lineage)
        assertEquals(unformatted.legacyLineage, formatted.legacyLineage)
        assertEquals(unformatted.origin, formatted.origin)
        assertEquals(unformatted.current, formatted.current)
        assertEquals(unformatted.additions, formatted.additions)
        for (page in listOf(3, 5)) {
            assertEquals(unformatted.storagePosition(page, 2), formatted.storagePosition(page, 2))
            val prior = authentic.getJSONArray("pages").getJSONObject(page)
                .getJSONArray("blocks").getJSONObject(2).getString("code")
            val corrected = rewritten.getJSONArray("pages").getJSONObject(page)
                .getJSONArray("blocks").getJSONObject(2).getString("code")
            assertNotEquals(prior, corrected)
            assertTrue(corrected.contains("\n\n    if ("))
            assertTrue(corrected.contains("\n\n    return 0;"))
        }
        val originalPrediction = authentic.getJSONArray("pages").getJSONObject(3)
            .getJSONArray("blocks").getJSONObject(3)
        val revisedPrediction = rewritten.getJSONArray("pages").getJSONObject(3)
            .getJSONArray("blocks").getJSONObject(3)
        assertEquals(originalPrediction.toString(), revisedPrediction.toString())
        val initialCompiler = authentic.getJSONArray("pages").getJSONObject(3)
            .getJSONArray("blocks").getJSONObject(6)
        val revisedCompiler = rewritten.getJSONArray("pages").getJSONObject(3)
            .getJSONArray("blocks").getJSONObject(6)
        assertEquals(initialCompiler.toString(), revisedCompiler.toString())
    }

    @Test fun alreadyPublishedPhysicalFormattingRecoversOnlyAuthenticOriginals() {
        val (before, after) = pilotDocuments()
        val (recovered, published) = pilotDocuments(realForm(before))
        assertEquals(
            requireNotNull(LessonStateLayoutJson.read(before)).lineage,
            requireNotNull(LessonStateLayoutJson.read(recovered)).lineage)
        assertEquals(
            requireNotNull(LessonStateLayoutJson.read(after)).lineage,
            requireNotNull(LessonStateLayoutJson.read(published)).lineage)
        for (page in pages) {
            assertEquals(
                before.getJSONArray("pages").getJSONObject(page).getJSONArray("blocks")
                    .getJSONObject(2).getString("code"),
                recovered.getJSONArray("pages").getJSONObject(page).getJSONArray("blocks")
                    .getJSONObject(2).getString("code"))
            assertEquals(
                after.getJSONArray("pages").getJSONObject(page).getJSONArray("blocks")
                    .getJSONObject(2).getString("code"),
                published.getJSONArray("pages").getJSONObject(page).getJSONArray("blocks")
                    .getJSONObject(2).getString("code"))
        }
    }

    @Test fun realLessonForgedAnswerCodeOrHistoricalSnapshotIsRejected() {
        val (original, expected) = pilotDocuments()
        val attacks: List<(JSONObject) -> Unit> = listOf(
            { it.getJSONArray("pages").getJSONObject(3).getJSONArray("blocks")
                .getJSONObject(3).put("answers", JSONArray().put("forged")) },
            { it.getJSONArray("pages").getJSONObject(3).getJSONArray("blocks")
                .getJSONObject(2).put("code", it.getJSONArray("pages").getJSONObject(3)
                    .getJSONArray("blocks").getJSONObject(2).getString("code")
                    .replace("const int fichas = 18;", "const int fichas = 19;")) },
            { it.getJSONObject("stateLayout").getJSONArray("sourceFormatRevisions")
                .getJSONObject(0).getJSONObject("block")
                .put("code", "int unrelated = 7;") },
            { it.getJSONObject("stateLayout").getJSONArray("sourceFormatRevisions")
                .getJSONObject(0).put("id", "block_0003_0003") }
        )
        attacks.forEach { forge ->
            val changed = JSONObject(expected.toString())
            forge(changed)
            assertTrue(runCatching { LessonStateLayoutJson.read(changed) }.exceptionOrNull()
                is LessonStateLayoutException)
        }
        assertEquals(
            requireNotNull(LessonStateLayoutJson.read(original)).lineage,
            requireNotNull(LessonStateLayoutJson.read(expected)).lineage)
    }
}
