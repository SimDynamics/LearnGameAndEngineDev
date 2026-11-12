package com.learngraphicsprogramming.app

import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import java.io.File

/** Cross-check exact published math-label exceptions with unrelated grading intact. */
class PublishedMathLabelRevisionAndroidTest {
    private data class Example(
        val unit: String, val name: String, val id: String,
        val owner: String, val sourceIndex: Int, val before: String, val after: String
    )
    private val examples = listOf(
        Example("datos_propios_y_estado_valido", "construir_y_comprobar_un_modelo_de_pedidos",
            "cpp_fundamentals__datos_propios_y_estado_valido__construir_y_comprobar_un_modelo_de_pedidos",
            "block_0007_0001", 0,
            "Construcción que termina normalmente",
            "Construcción desde código y unidades que termina normalmente"),
        Example("texto_interpretacion_y_flujos", "construir_un_interprete_con_historial_y_pruebas",
            "cpp_fundamentals__texto_interpretacion_y_flujos__construir_un_interprete_con_historial_y_pruebas",
            "block_0008_0002", 5, "100+1;", "99+2;")
    )
    private fun lesson(info: Example): JSONObject {
        val path = "src/main/assets/content/es/programming/cpp_fundamentals/" +
            info.unit + "/lesson_" + info.name + ".json"
        val file = listOf(File(path), File("app/$path")).firstOrNull(File::isFile)
            ?: error("Missing reviewed mathematical lesson: " + info.name)
        return JSONObject(file.readText(Charsets.UTF_8))
    }
    private fun block(doc: JSONObject, id: String): JSONObject {
        val records = doc.getJSONObject("stateLayout").getJSONArray("current")
        for (p in 0 until records.length()) {
            val ids = records.getJSONObject(p).getJSONArray("blocks")
            for (b in 0 until ids.length()) if (ids.getString(b) == id)
                return doc.getJSONArray("pages").getJSONObject(p).getJSONArray("blocks").getJSONObject(b)
        }
        error("Missing exact archived mathematical owner: $id")
    }

    @Test fun exactReviewedLabelsPreserveAllHistoricalMathematicalState() {
        for (info in examples) {
            val doc = lesson(info)
            assertEquals(info.id, doc.getString("id"))
            val layout = requireNotNull(LessonStateLayoutJson.read(doc))
            assertTrue(layout.origin.isNotEmpty())
            val current = block(doc, info.owner)
            val old = CurricularMathLabelRevision.project(info.id, info.owner, current)
            val source = current.getJSONArray("responses").getJSONObject(0)
                .getJSONArray("sources").getJSONObject(info.sourceIndex)
            val historical = old.getJSONArray("responses").getJSONObject(0)
                .getJSONArray("sources").getJSONObject(info.sourceIndex)
            assertEquals(info.after, source.getString("label"))
            assertEquals(info.before, historical.getString("label"))
            assertNotEquals(LessonStateLayoutJson.fingerprint(current),
                LessonStateLayoutJson.fingerprint(old))
        }
    }

    @Test fun mathematicalSourceAndResponseTamperingCannotBorrowTheOldIdentity() {
        for (info in examples) {
            val doc = lesson(info)
            val copy = JSONObject(doc.toString())
            val sources = block(copy, info.owner)
                .getJSONArray("responses").getJSONObject(0).getJSONArray("sources")
            sources.getJSONObject(info.sourceIndex).put("label", "not a reviewed source label")
            assertTrue(runCatching { LessonStateLayoutJson.read(copy) }.exceptionOrNull()
                is LessonStateLayoutException)
            val corrupted = JSONObject(doc.toString())
            block(corrupted, info.owner).getJSONArray("responses")
                .getJSONObject(0).put("id", "forged_response")
            assertTrue(runCatching { LessonStateLayoutJson.read(corrupted) }.exceptionOrNull()
                is LessonStateLayoutException)
        }
    }
}
