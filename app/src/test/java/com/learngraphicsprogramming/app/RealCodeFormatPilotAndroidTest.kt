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

    private fun realForm(original: JSONObject): JSONObject {
        val formatted = JSONObject(original.toString())
        val state = formatted.getJSONObject("stateLayout")
        require(state.getInt("version") == 2 && state.getString("locale") == "es")
        val editions = state.getJSONArray("editorial")
        val archives = JSONArray()
        for (page in listOf(3, 5)) {
            val index = 2
            val block = formatted.getJSONArray("pages").getJSONObject(page)
                .getJSONArray("blocks").getJSONObject(index)
            val historical = JSONObject(block.toString())
            require(block.getString("type") == "code")
            val identity = state.getJSONArray("current").getJSONObject(page)
                .getJSONArray("blocks").getString(index)
            require(identity == if (page == 3) "block_0003_0002" else "block_0005_0002")

            var source = block.getString("code")
            for ((before, after) in listOf(
                "    const bool acepta_sobrantes = true;\n    if (" to
                    "    const bool acepta_sobrantes = true;\n\n    if (",
                "        const int sobrantes = fichas % capacidad;\n        if (" to
                    "        const int sobrantes = fichas % capacidad;\n\n        if (",
                "    }\n    return 0;\n}\n" to "    }\n\n    return 0;\n}\n"
            )) {
                require(source.split(before).size == 2) {
                    "The actual published lesson has changed its reviewed spacing anchor"
                }
                source = source.replace(before, after)
            }
            block.put("code", source)
            archives.put(JSONObject().put("id", identity).put("block", historical))
            editions.put(JSONObject().put("id", identity)
                .put("beforeSha256", LessonStateLayoutJson.fingerprint(historical))
                .put("afterSha256", LessonStateLayoutJson.fingerprint(block)))
        }
        state.put("sourceFormatRevisions", archives)
        return formatted
    }

    @Test fun bothPublishedCodeBlocksRetainAndroidProgressLineageAndAnswers() {
        val authentic = originalLesson()
        val unformatted = requireNotNull(LessonStateLayoutJson.read(authentic))
        val rewritten = realForm(authentic)
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

    @Test fun realLessonForgedAnswerCodeOrHistoricalSnapshotIsRejected() {
        val expected = realForm(originalLesson())
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
            requireNotNull(LessonStateLayoutJson.read(originalLesson())).lineage,
            requireNotNull(LessonStateLayoutJson.read(expected)).lineage)
    }
}
