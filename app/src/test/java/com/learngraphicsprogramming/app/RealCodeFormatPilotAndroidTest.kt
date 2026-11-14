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

    private fun archivedSource(published: JSONObject, page: Int): JSONObject {
        val metadata = published.getJSONObject("stateLayout")
        val id = metadata.getJSONArray("current").getJSONObject(page)
            .getJSONArray("blocks").getString(2)
        require(id == if (page == 3) "block_0003_0002" else "block_0005_0002")
        val archives = metadata.getJSONArray("sourceFormatRevisions")
        val original = (0 until archives.length())
            .map { archives.getJSONObject(it) }
            .single { it.getString("id") == id }
            .getJSONObject("block")
        val current = published.getJSONArray("pages").getJSONObject(page)
            .getJSONArray("blocks").getJSONObject(2)
        require(original.getString("type") == "code")
        require(current.getString("code") == addPhysicalSpacing(original.getString("code")))
        require(CurricularSourceFormatCompat.project(current, original).getString("code") ==
            original.getString("code"))
        return original
    }

    @Test fun bothPublishedCodeBlocksRetainAndroidProgressLineageAndAnswers() {
        val published = originalLesson()
        val layout = requireNotNull(LessonStateLayoutJson.read(published))
        val reopened = JSONObject(published.toString())
        val roundtrip = requireNotNull(LessonStateLayoutJson.read(reopened))
        assertEquals(layout.lineage, roundtrip.lineage)
        assertEquals(layout.legacyLineage, roundtrip.legacyLineage)
        assertEquals(layout.origin, roundtrip.origin)
        assertEquals(layout.current, roundtrip.current)
        assertEquals(layout.additions, roundtrip.additions)
        for (page in pages) {
            assertEquals(layout.storagePosition(page, 2), roundtrip.storagePosition(page, 2))
            val prior = archivedSource(published, page).getString("code")
            val corrected = published.getJSONArray("pages").getJSONObject(page)
                .getJSONArray("blocks").getJSONObject(2).getString("code")
            assertNotEquals(prior, corrected)
            assertTrue(corrected.contains("\n\n    if ("))
            assertTrue(corrected.contains("\n\n    return 0;"))
        }
        // The later published paragraph corrections are real history, not
        // something to roll back merely to fabricate an earlier test fixture.
        val originalPrediction = published.getJSONArray("pages").getJSONObject(3)
            .getJSONArray("blocks").getJSONObject(3)
        val reopenedPrediction = reopened.getJSONArray("pages").getJSONObject(3)
            .getJSONArray("blocks").getJSONObject(3)
        assertEquals(originalPrediction.toString(), reopenedPrediction.toString())
        val compiler = published.getJSONArray("pages").getJSONObject(3)
            .getJSONArray("blocks").getJSONObject(6)
        val reopenedCompiler = reopened.getJSONArray("pages").getJSONObject(3)
            .getJSONArray("blocks").getJSONObject(6)
        assertEquals(compiler.toString(), reopenedCompiler.toString())
    }

    @Test fun alreadyPublishedPhysicalFormattingSupportsFurtherAuthenticatedSpacing() {
        val published = originalLesson()
        val before = requireNotNull(LessonStateLayoutJson.read(published))
        val modified = JSONObject(published.toString())
        val state = modified.getJSONObject("stateLayout")
        val originalArchives = state.getJSONArray("sourceFormatRevisions").toString()
        for (page in pages) {
            val historical = archivedSource(published, page)
            val id = state.getJSONArray("current").getJSONObject(page)
                .getJSONArray("blocks").getString(2)
            val active = modified.getJSONArray("pages").getJSONObject(page)
                .getJSONArray("blocks").getJSONObject(2)
            val former = JSONObject(active.toString())
            active.put("code", former.getString("code") + "\n")
            state.getJSONArray("editorial").put(JSONObject()
                .put("id", id)
                .put("beforeSha256", LessonStateLayoutJson.fingerprint(former))
                .put("afterSha256", LessonStateLayoutJson.fingerprint(active)))
            assertEquals(historical.getString("code"),
                CurricularSourceFormatCompat.project(active, historical).getString("code"))
        }
        // The earlier archive and its original state addresses are unchanged
        // even when subsequent editorial commits add more blank lines.
        assertEquals(originalArchives, state.getJSONArray("sourceFormatRevisions").toString())
        val after = requireNotNull(LessonStateLayoutJson.read(modified))
        assertEquals(before.lineage, after.lineage)
        assertEquals(before.legacyLineage, after.legacyLineage)
        assertEquals(before.origin, after.origin)
        assertEquals(before.current, after.current)
        for (page in pages)
            assertEquals(before.storagePosition(page, 2), after.storagePosition(page, 2))
    }

    @Test fun realLessonForgedAnswerCodeOrHistoricalSnapshotIsRejected() {
        val published = originalLesson()
        val layout = requireNotNull(LessonStateLayoutJson.read(published))
        for (page in pages) archivedSource(published, page)
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
                .getJSONObject(0).put("id", "block_0003_0003") },
            { it.getJSONObject("stateLayout").getJSONArray("editorial")
                .getJSONObject(0).put("afterSha256", "0".repeat(64)) }
        )
        attacks.forEach { forge ->
            val changed = JSONObject(published.toString())
            forge(changed)
            assertTrue(runCatching { LessonStateLayoutJson.read(changed) }.exceptionOrNull()
                is LessonStateLayoutException)
        }
        assertEquals(layout.lineage,
            requireNotNull(LessonStateLayoutJson.read(originalLesson())).lineage)
    }
}
