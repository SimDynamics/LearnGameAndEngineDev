package com.learngraphicsprogramming.app

import com.learngraphicsprogramming.app.math.MathPuzzleParser
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import java.io.File

/** The seven actual C++ activities use the existing shared mathematical parser unchanged. */
class CurrentSharedProgrammingMathContractTest {
    private val resources = File(System.getProperty("translation.resources") ?: "src/main/res")
    private val assets = resources.absoluteFile.parentFile!!.resolve("assets/content/es/programming/cpp_fundamentals")
    private val locations = listOf(
        Triple("archivos_y_programa_completo/lesson_componer_informes_sin_cambiar_los_datos.json", 2, 5),
        Triple("datos_propios_y_estado_valido/lesson_construir_y_comprobar_un_modelo_de_pedidos.json", 2, 5),
        Triple("repeticion_y_terminacion/lesson_recursion_finita_y_comparacion.json", 10, 7),
        Triple("repeticion_y_terminacion/lesson_despacho_y_recuperacion_de_recorridos.json", 5, 7),
        Triple("colecciones_y_recorridos/lesson_contar_frecuencias_y_conservar_empates.json", 8, 8),
        Triple("errores_y_construccion_gradual/lesson_construir_un_proceso_de_recarga.json", 4, 6),
        Triple("repeticion_y_terminacion/lesson_despacho_y_recuperacion_de_recorridos.json", 6, 5))

    @Test fun allSevenActualCompositionsRetainEveryPromptFieldValidatorAndFeedback() {
        val families = mutableSetOf<String>()
        for ((path, page, number) in locations) {
            val block = block(path, page, number)
            val before = block.toString()
            assertEquals(path, "math_puzzle", block.getString("type"))
            val parsed = MathPuzzleParser.parse(block)
            assertEquals(block.optString("prompt", block.optString("question")), parsed.prompt)
            val responses = block.getJSONArray("responses")
            assertEquals(responses.length(), parsed.responses.size)
            parsed.responses.forEachIndexed { index, response ->
                val original = responses.getJSONObject(index)
                families += response.type
                assertEquals(original.getString("id"), response.id)
                assertEquals(original.getJSONObject("validator").toString(), response.validator.toString())
                assertEquals(original.toString(), response.config.toString())
            }
            assertEquals(block.getJSONObject("feedback").getString("correct"), parsed.correctFeedback)
            assertEquals(block.getJSONObject("feedback").getString("incorrect"), parsed.incorrectFeedback)
            assertEquals("The real parser does not rewrite authored content", before, block.toString())
        }
        assertEquals(setOf("math_multi_input", "math_multiple_choice_single", "math_matching_drag"), families)
    }

    @Test fun newContainerRecognitionDoesNotRelaxResponseAndValidatorFamilies() {
        for ((path, page, number) in locations) {
            val altered = block(path, page, number)
            val response = altered.getJSONArray("responses").getJSONObject(0)
            response.put("validator", JSONObject().put("type", "choice").put("expected", org.json.JSONArray(listOf("nonexistent"))))
            assertRejected(altered)
            val unknown = block(path, page, number)
            unknown.getJSONArray("responses").getJSONObject(0).put("type", "math_invented")
            assertRejected(unknown)
        }
    }

    @Test fun duplicatedAnswerIdentitiesAndUnknownSchemaRemainInvalid() {
        val (path, page, number) = locations.first()
        val duplicate = block(path, page, number)
        duplicate.getJSONArray("responses").put(JSONObject(duplicate.getJSONArray("responses").getJSONObject(0).toString()))
        assertRejected(duplicate)
        assertRejected(block(path, page, number).put("schemaVersion", 2))
    }

    private fun block(path: String, page: Int, index: Int): JSONObject {
        // Locate by immutable lesson filename if its unit has since been reorganized.
        val file = assets.resolve(path).takeIf(File::isFile) ?: assets.walkTopDown().single { it.name == File(path).name }
        val lesson = JSONObject(file.readText())
        val historical = LessonStatePosition(page - 1, index - 1)
        val layout = LessonStateLayoutJson.read(lesson)
        val position = layout?.let { checked -> checked.current.flatMapIndexed { p, current -> current.blocks.indices.map { b ->
            LessonStatePosition(p, b)
        } }.single { checked.storagePosition(it.page, it.block) == historical } } ?: historical
        return lesson.getJSONArray("pages").getJSONObject(position.page).getJSONArray("blocks").getJSONObject(position.block)
    }
    private fun assertRejected(block: JSONObject) {
        try { MathPuzzleParser.parse(block); fail("Invalid shared composition must be rejected") }
        catch (_: IllegalArgumentException) { }
    }
}
