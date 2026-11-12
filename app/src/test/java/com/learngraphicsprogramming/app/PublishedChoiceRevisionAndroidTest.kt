package com.learngraphicsprogramming.app

import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import java.io.File

/** Real published option revisions, never invented answer aliases. */
class PublishedChoiceRevisionAndroidTest {
    private val names = listOf(
        "estados_del_flujo_y_datos_validos",
        "validar_una_operacion_y_recuperar_la_siguiente"
    )
    private fun document(name: String): JSONObject {
        val relative = "src/main/assets/content/es/programming/cpp_fundamentals/" +
            "texto_interpretacion_y_flujos/lesson_$name.json"
        val file = listOf(File(relative), File("app/$relative")).firstOrNull(File::isFile)
            ?: error("Missing actual choice archive: $name")
        return JSONObject(file.readText(Charsets.UTF_8))
    }
    private fun position(lesson: JSONObject, id: String): Pair<Int, Int> {
        val pages = lesson.getJSONObject("stateLayout").getJSONArray("current")
        for (p in 0 until pages.length()) {
            val ids = pages.getJSONObject(p).getJSONArray("blocks")
            for (b in 0 until ids.length()) if (ids.getString(b) == id) return p to b
        }
        error("Missing stable identity: $id")
    }
    private fun block(lesson: JSONObject, id: String): JSONObject {
        val (p, b) = position(lesson, id)
        return lesson.getJSONArray("pages").getJSONObject(p)
            .getJSONArray("blocks").getJSONObject(b)
    }

    @Test fun reviewedOptionChangesPreserveSavedResponsePositionsAndCorrectIndices() {
        for (name in names) {
            val lesson = document(name)
            val entries = lesson.getJSONObject("stateLayout").getJSONArray("choiceRevisions")
            assertEquals(1, entries.length())
            val revision = entries.getJSONObject(0)
            val id = revision.getString("id")
            val historical = revision.getJSONObject("block")
            val active = block(lesson, id)
            assertEquals("multiple_choice", active.getString("type"))
            val layout = requireNotNull(LessonStateLayoutJson.read(lesson))
            val (p, b) = position(lesson, id)
            assertEquals(LessonStatePosition(p, b), layout.storagePosition(p, b))
            assertEquals(historical.getInt("correctIndex"), active.getInt("correctIndex"))
            assertEquals(LessonStateLayoutJson.fingerprint(historical),
                LessonStateLayoutJson.fingerprint(CurricularChoiceRevision.project(active, historical)))
            val originalOptions = historical.getJSONArray("options")
            val changedOptions = active.getJSONArray("options")
            assertEquals(1, (0 until originalOptions.length()).count {
                originalOptions.getString(it) != changedOptions.getString(it)
            })
        }
    }

    @Test fun alteredChoiceAnswerOrForgedOriginCannotBorrowOldProgress() {
        for (name in names) {
            val original = document(name)
            val id = original.getJSONObject("stateLayout")
                .getJSONArray("choiceRevisions").getJSONObject(0).getString("id")
            val mutations: List<(JSONObject) -> Unit> = listOf(
                { block(it, id).put("correctIndex", 999) },
                { block(it, id).getJSONArray("options").put(1, "unreviewed replacement") },
                { it.getJSONObject("stateLayout").getJSONArray("choiceRevisions")
                    .getJSONObject(0).getJSONObject("block").put("correctIndex", 2) },
                { it.getJSONObject("stateLayout").remove("choiceRevisions") },
                { it.getJSONObject("stateLayout").getJSONArray("choiceRevisions")
                    .getJSONObject(0).put("id", "block_0000_0000") }
            )
            for (mutation in mutations) {
                val forged = JSONObject(original.toString())
                mutation(forged)
                assertTrue("Forged choice was accepted in $name",
                    runCatching { LessonStateLayoutJson.read(forged) }.exceptionOrNull()
                        is LessonStateLayoutException)
            }
        }
    }

    @Test fun optionalSourceFormattingComposesWithTheExistingChoiceRevision() {
        val published = document(names.first())
        val before = requireNotNull(LessonStateLayoutJson.read(published))
        val modified = JSONObject(published.toString())
        val meta = modified.getJSONObject("stateLayout")
        val formatId = "block_0000_0002"
        val snapshot = JSONObject(block(modified, formatId).toString())
        val formatted = block(modified, formatId)
        formatted.put("code", snapshot.getString("code") + "\n")
        meta.put("sourceFormatRevisions", JSONArray().put(
            JSONObject().put("id", formatId).put("block", snapshot)))
        meta.getJSONArray("editorial").put(JSONObject()
            .put("id", formatId)
            .put("beforeSha256", LessonStateLayoutJson.fingerprint(snapshot))
            .put("afterSha256", LessonStateLayoutJson.fingerprint(formatted)))
        val after = requireNotNull(LessonStateLayoutJson.read(modified))
        assertEquals(before.lineage, after.lineage)
        assertEquals(before.legacyLineage, after.legacyLineage)
        val choiceId = meta.getJSONArray("choiceRevisions").getJSONObject(0).getString("id")
        val (p, b) = position(published, choiceId)
        assertEquals(before.storagePosition(p, b), after.storagePosition(p, b))
        assertEquals(snapshot.getString("code"),
            CurricularSourceFormatCompat.project(formatted, snapshot).getString("code"))
        assertNotEquals(snapshot.getString("code"), formatted.getString("code"))
    }
}
