package com.learngraphicsprogramming.app

import java.io.File
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class CppExerciseAnswersTest {
    private val resources = File(System.getProperty("translation.resources") ?: "src/main/res")
    private val content = resources.absoluteFile.parentFile!!.resolve("assets/content/es/programming/cpp_fundamentals")
    private fun block(path: String, page: Int, index: Int): JSONObject =
        JSONObject(content.resolve(path).readText()).getJSONArray("pages")
            .getJSONObject(page - 1).getJSONArray("blocks").getJSONObject(index - 1)

    @Test fun allFivePublishedExactOutputsRejectAdditionalBoundaryCharacters() {
        val locations = listOf(
            Triple("programas_y_ejecucion/lesson_primer_programa_y_salida.json", 2, 4),
            Triple("repeticion_y_terminacion/lesson_recorridos_anidados.json", 10, 3),
            Triple("repeticion_y_terminacion/lesson_recorridos_anidados.json", 12, 6),
            Triple("datos_propios_y_estado_valido/lesson_transferir_invariantes_a_un_deposito.json", 12, 3),
            Triple("datos_propios_y_estado_valido/lesson_transferir_invariantes_a_un_deposito.json", 15, 3),
        )
        for ((path, page, index) in locations) {
            val b = block(path, page, index)
            assertEquals("exact", b.getString("outputComparison"))
            for (answer in answers(b)) {
                assertTrue(path, CppExerciseAnswers.outputMatches(b, answer))
                for (extra in listOf(" ", "\t", "\n", "\r\n")) {
                    assertFalse(path, CppExerciseAnswers.outputMatches(b, answer + extra))
                    assertFalse(path, CppExerciseAnswers.outputMatches(b, extra + answer))
                }
            }
        }
    }

    @Test fun legacyOutputTrimsBoundariesAndKeepsInternalNewlinesAndSpaces() {
        for ((right, wrong) in listOf("Entrada\nSalida" to "Entrada Salida", "[   8]|[9]" to "[ 8]|[9]")) {
            val b = JSONObject().put("answers", JSONArray(listOf(right)))
            assertTrue(CppExerciseAnswers.outputMatches(b, "\t $right\n "))
            assertFalse(CppExerciseAnswers.outputMatches(b, wrong))
            b.put("outputComparison", "trimmed")
            assertTrue(CppExerciseAnswers.outputMatches(b, " $right "))
        }
    }

    @Test fun allCurricularInlineAlternativesWorkIndependentlyAndKeepTwoBlanks() {
        for ((path, page, index) in inlineLocations) {
            val b = block(path, page, index)
            assertEquals(2, b.getString("template").windowed(2).count { it == "[]" })
            assertEquals(2, answers(b).size)
            val groups = b.getJSONArray("answerAlternatives")
            val first = groups.getJSONArray(0)
            val second = groups.getJSONArray(1)
            for (i in 0 until first.length()) for (j in 0 until second.length()) {
                assertTrue(path, CppExerciseAnswers.inlineMatches(b, listOf(" ${first.getString(i)}\t", second.getString(j))))
            }
            for (wrong in listOf(answers(b).reversed(), listOf("clear", answers(b)[1]), listOf(answers(b)[0], "incorrect"))) {
                assertFalse(path, CppExerciseAnswers.inlineMatches(b, wrong))
            }
        }
        val comparison = block(inlineLocations[1].first, 3, 6)
        assertFalse(CppExerciseAnswers.inlineMatches(comparison, listOf("==", "==")))
        val vector = block(inlineLocations[2].first, 4, 5)
        for (operation in listOf("pop_back", "resize", "assign", "insert", "clear")) {
            assertFalse(CppExerciseAnswers.inlineMatches(vector, listOf(operation, "9")))
        }
        assertFalse(CppExerciseAnswers.inlineMatches(vector, listOf("push_back", "8")))
        val selection = block(inlineLocations[3].first, 5, 4)
        assertFalse(CppExerciseAnswers.inlineMatches(selection, listOf("emplace_back", "valor+1")))
    }

    @Test fun legacyInlineAnswersAndFillEquivalentTypesStayCompatible() {
        val b = JSONObject().put("template", "[] []").put("answers", JSONArray(listOf("unsigned char", "!=")))
        assertTrue(CppExerciseAnswers.inlineMatches(b, listOf("\t unsigned   char\n", " != ")))
        assertFalse(CppExerciseAnswers.inlineMatches(b, listOf("char unsigned", "not_eq")))
        val types = block("colecciones_y_recorridos/lesson_un_vector_para_una_coleccion.json", 2, 3)
        for (answer in answers(types)) assertTrue(CppExerciseAnswers.fillMatches(types, "\t $answer \n"))
        val operator = block("expresiones_y_decisiones/lesson_combinar_condiciones.json", 1, 5)
        for (answer in answers(operator)) assertTrue(CppExerciseAnswers.fillMatches(operator, " $answer "))
    }

    @Test fun literalFillsKeepAllInternalCharactersAndTolerateOutsideWhitespace() {
        for ((path, page, index) in literalLocations) {
            val b = block(path, page, index)
            val published = answers(b)
            val inline = b.getString("type") == "inline_fill_code"
            val literalIndex = if (inline) 1 else 0
            fun matches(literal: String): Boolean = if (inline) {
                CppExerciseAnswers.inlineMatches(b, published.toMutableList().apply { this[literalIndex] = literal })
            } else CppExerciseAnswers.fillMatches(b, literal)
            val literal = published[literalIndex]
            assertTrue(matches(literal))
            assertTrue(matches(" \t$literal\n "))
            val firstSpace = literal.indexOf(' ')
            assertTrue(firstSpace > 0)
            for (replacement in listOf("  ", "\t", "\n", "\r\n")) {
                assertFalse(path, matches(literal.replaceRange(firstSpace, firstSpace + 1, replacement)))
            }
        }
    }

    @Test fun everyExistingProgrammingAnswerRemainsAcceptedAndInlineLegacyIsUnchanged() {
        var checked = 0
        content.walkTopDown().filter { it.isFile && it.name.startsWith("lesson_") && it.extension == "json" }.forEach { file ->
            val pages = JSONObject(file.readText()).getJSONArray("pages")
            for (p in 0 until pages.length()) {
                val blocks = pages.getJSONObject(p).getJSONArray("blocks")
                for (i in 0 until blocks.length()) {
                    val b = blocks.getJSONObject(i)
                    when (b.getString("type")) {
                        "fill_code" -> answers(b).forEach { assertTrue(file.name, CppExerciseAnswers.fillMatches(b, it)); checked++ }
                        "inline_fill_code" -> { assertTrue(file.name, CppExerciseAnswers.inlineMatches(b, answers(b))); checked++ }
                        "output_prediction" -> answers(b).forEach { assertTrue(file.name, CppExerciseAnswers.outputMatches(b, it)); checked++ }
                    }
                }
            }
        }
        assertTrue("All curricular text puzzles were covered", checked > 500)
    }

    @Test fun answersInsideTemplateLiteralsKeepTheirBoundaryCharacters() {
        val inline = JSONObject().put("template", "Token t{nombre, [], \"[]\"};")
            .put("answers", JSONArray(listOf("0", "Mesa9")))
        assertTrue(CppExerciseAnswers.inlineMatches(inline, listOf(" 0 ", "Mesa9")))
        for (wrong in listOf(" Mesa9", "Mesa9 ", "Mesa9\t", "\nMesa9")) {
            assertFalse(CppExerciseAnswers.inlineMatches(inline, listOf("0", wrong)))
        }
        for (code in listOf("std::setfill('___')", "std::format(\"{:04___}\", 8)")) {
            val fill = JSONObject().put("code", code).put("answers", JSONArray(listOf("0")))
            assertTrue(CppExerciseAnswers.fillMatches(fill, "0"))
            assertFalse(CppExerciseAnswers.fillMatches(fill, " 0 "))
            assertFalse(CppExerciseAnswers.fillMatches(fill, "0\t"))
        }
    }

    @Test fun malformedExtensionsAreTechnicalFailuresRatherThanWrongStudentAnswers() {
        fun invalid(action: () -> Boolean) {
            try { action(); fail("Invalid configuration must fail") } catch (_: RuntimeException) { }
        }
        invalid { CppExerciseAnswers.outputMatches(JSONObject("""{"answers":["ok"],"outputComparison":"typo"}"""), "ok") }
        invalid { CppExerciseAnswers.outputMatches(JSONObject("""{"answers":["ok"],"outputComparison":null}"""), "ok") }
        invalid { CppExerciseAnswers.outputMatches(JSONObject("""{"answers":["ok"],"outputComparison":7}"""), "ok") }
        for (json in listOf(
            """{"answers":["&&","||"],"answerAlternatives":[["&&","and"]]}""",
            """{"answers":["&&"],"answerAlternatives":[[]]}""",
            """{"answers":["&&"],"answerAlternatives":[["and","&&"]]}""",
            """{"answers":["&&"],"answerAlternatives":[["&&",8]]}""",
        )) invalid { CppExerciseAnswers.inlineMatches(JSONObject(json), listOf("&&")) }
        invalid { CppExerciseAnswers.inlineMatches(JSONObject("""{"answers":["&&","||"]}"""), listOf("&&")) }
    }

    private fun answers(b: JSONObject) = b.getJSONArray("answers").let { array -> List(array.length()) { array.getString(it) } }

    private val inlineLocations = listOf(
        Triple("expresiones_y_decisiones/lesson_combinar_condiciones.json", 2, 4),
        Triple("expresiones_y_decisiones/lesson_comparaciones_y_condiciones.json", 3, 6),
        Triple("colecciones_y_recorridos/lesson_un_vector_para_una_coleccion.json", 4, 5),
        Triple("colecciones_y_recorridos/lesson_acumular_contar_y_seleccionar.json", 5, 4),
    )
    private val literalLocations = listOf(
        Triple("programas_y_ejecucion/lesson_primer_programa_y_salida.json", 2, 5),
        Triple("programas_y_ejecucion/lesson_resolver_probar_y_recuperar.json", 1, 4),
        Triple("programas_y_ejecucion/lesson_texto_secuencia_y_formato.json", 5, 2),
    )
}
