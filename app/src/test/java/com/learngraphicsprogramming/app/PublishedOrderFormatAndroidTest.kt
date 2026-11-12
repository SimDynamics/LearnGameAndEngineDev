package com.learngraphicsprogramming.app

import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import java.io.File

/** The real published decision lesson contains both prior grouped-order
 * history and five format-only code/prediction revisions in stateLayout v2.
 * Verify them together, without editing assets or a student's preferences.
 */
class PublishedOrderFormatAndroidTest {
    private fun published(): JSONObject {
        val fileName = "src/main/assets/content/es/programming/cpp_fundamentals/" +
            "expresiones_y_decisiones/lesson_alternativas_y_fronteras.json"
        val file = listOf(File(fileName), File("app/$fileName")).firstOrNull(File::isFile)
            ?: error("Published alternatives lesson is unavailable")
        return JSONObject(file.readText(Charsets.UTF_8))
    }

    @Test fun previouslyEarnedOrderAndFiveSourceRevisionsShareTheirHistoricalLineage() {
        val document = published()
        val state = document.getJSONObject("stateLayout")
        assertEquals(2, state.getInt("version"))
        assertEquals(1, state.getJSONArray("orderRefinements").length())
        assertEquals(5, state.getJSONArray("sourceFormatRevisions").length())
        val layout = requireNotNull(LessonStateLayoutJson.read(document))
        val original = state.getJSONArray("origin").getJSONObject(4)
            .getJSONArray("blocks").getJSONObject(1)
        val stableId = "block_0004_0001"
        assertEquals(stableId, original.getString("id"))
        assertEquals(LessonStatePosition(4, 1), layout.storagePosition(4, 1))

        val originalOrder = listOf(3, 2, 0, 1)
        val restored = layout.savedOrderForDisplay(4, 1, originalOrder)
        val visible = document.getJSONArray("pages").getJSONObject(4)
            .getJSONArray("blocks").getJSONObject(1)
        val visibleOrder = visible.getJSONArray("correctOrder")
        assertEquals(10, visibleOrder.length())
        assertEquals((0 until visibleOrder.length()).map(visibleOrder::getInt), restored)
        assertEquals(listOf(3, 2, 0, 1), originalOrder)

        // Previously failed partial orders are presented without inventing a
        // new correct verdict or silently saving a different response.
        val draft = listOf(0, 1, 2, 3)
        val displayedDraft = layout.savedOrderForDisplay(4, 1, draft)
        assertEquals((0..9).toList(), displayedDraft)
        assertNotEquals(restored, displayedDraft)
        assertEquals(draft, listOf(0, 1, 2, 3))
        assertEquals(restored, layout.savedOrderForDisplay(4, 1, restored))

        val ids = (0 until state.getJSONArray("sourceFormatRevisions").length()).map {
            state.getJSONArray("sourceFormatRevisions").getJSONObject(it).getString("id")
        }
        assertEquals(listOf("block_0000_0002", "block_0001_0002", "block_0002_0002",
            "block_0005_0002", "block_0006_0002"), ids)
        for (identity in ids) {
            val found = layout.current.flatMapIndexed { page, item ->
                item.blocks.mapIndexed { block, id -> Triple(page, block, id) }
            }.single { it.third == identity }
            val archival = state.getJSONArray("sourceFormatRevisions")
            val historic = (0 until archival.length()).map(archival::getJSONObject)
                .single { it.getString("id") == identity }.getJSONObject("block")
            val current = document.getJSONArray("pages").getJSONObject(found.first)
                .getJSONArray("blocks").getJSONObject(found.second)
            assertNotEquals(historic.getString("code"), current.getString("code"))
            assertEquals(LessonStateLayoutJson.fingerprint(historic),
                LessonStateLayoutJson.fingerprint(CurricularSourceFormatCompat.project(current, historic)))
        }
    }

    @Test fun orderRefinementForgeriesAndAlteredPredictionsStillFailClosed() {
        val attacks: List<(JSONObject) -> Unit> = listOf(
            { it.getJSONObject("stateLayout").getJSONArray("orderRefinements")
                .getJSONObject(0).getJSONObject("block")
                .getJSONArray("fragments").put(0, "return 123;") },
            { it.getJSONArray("pages").getJSONObject(4).getJSONArray("blocks")
                .getJSONObject(1).getJSONArray("correctOrder").put(0, 0) },
            { it.getJSONObject("stateLayout").getJSONArray("orderRefinements")
                .getJSONObject(0).put("id", "block_0001_0002") },
            { it.getJSONObject("stateLayout").remove("orderRefinements") },
            { it.getJSONArray("pages").getJSONObject(1).getJSONArray("blocks")
                .getJSONObject(2).getJSONArray("answers").put(0, "alterada") },
            { it.getJSONObject("stateLayout").getJSONArray("sourceFormatRevisions")
                .getJSONObject(1).getJSONObject("block")
                .put("code", "int unrelated = 42;") }
        )
        for (forgery in attacks) {
            val modified = JSONObject(published().toString())
            forgery(modified)
            assertTrue(runCatching { LessonStateLayoutJson.read(modified) }.exceptionOrNull()
                is LessonStateLayoutException)
        }
    }
}
