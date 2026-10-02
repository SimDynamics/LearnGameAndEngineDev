package com.learngraphicsprogramming.app.math

import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import java.io.File

/** Independently worked solutions exercise the authored lesson, not values copied from validators. */
class MathSandboxSolutionsTest {
    private fun text(value: String): MathAnswer = MathAnswer.Text(value)
    private fun fields(vararg values: Pair<String, String>): MathAnswer = MathAnswer.Fields(mapOf(*values))
    private fun graph(vararg coordinates: Pair<Int, Int>): MathAnswer = MathAnswer.Graph(coordinates.map { MathPoint(it.first.toDouble(), it.second.toDouble()) })

    @Test fun allOriginalExamplesAcceptTheirWorkedSolutionsAndRequireEveryResponse() {
        val solutions = mapOf(
            "numbers_and_fractions" to mapOf("difference" to text("7,5"), "fraction" to text("6/8"), "approximation" to text("0.33")),
            "single_rich_option" to mapOf("choice" to MathAnswer.Choice(setOf("linear"))),
            "multi_rich_option" to mapOf("choices" to MathAnswer.Choice(setOf("fraction", "decimal", "ratio"))),
            "editable_table" to mapOf("table" to fields("a" to "-1", "b" to "1", "c" to "5")),
            "equivalent_expression" to mapOf("equation" to text("2*y = 4 + 6*x")),
            "pi_expression" to mapOf("product" to text("2π + 2π")),
            "structured_comparison" to mapOf("comparison" to fields("operator" to "lt", "right" to "1")),
            "equation_blanks" to mapOf("blanks" to fields("added" to "4", "total" to "9")),
            "ordering_and_median" to mapOf("order" to MathAnswer.Order(listOf("n3", "n0", "n2", "n5", "n8")), "median" to text("2")),
            "matching_expressions" to mapOf("pairs" to MathAnswer.Matching(mapOf("sum" to "five", "product" to "twelve", "difference" to "seven"))),
            "inequality_builder" to mapOf("inequality" to MathAnswer.NumberLine(2.0, true, "left")),
            "number_line_absolute" to mapOf("distance" to text("3")),
            "point_and_coordinate" to mapOf("point" to graph(2 to 3), "x" to text("2")),
            "line_builder" to mapOf("line" to graph(-1 to -1, 1 to 3)),
            "scatter_builder" to mapOf("scatter" to graph(2 to 2, -2 to 1, 0 to 3)),
            "polygon_builder" to mapOf("triangle" to graph(3 to 0, 0 to 2, 0 to 0)),
            "parabola_builder" to mapOf("parabola" to graph(1 to -1, 0 to 1)),
            "rectangle_area" to mapOf("rectangle" to graph(0 to 0, 4 to 0, 4 to 2, 0 to 2)),
            "rectangle_sliders" to mapOf("dimensions" to MathAnswer.Sliders(mapOf("width" to 4.0, "height" to 3.0))),
            "venn_and_table" to mapOf("totals" to fields("a" to "6", "b" to "5", "union" to "9")),
            "static_geometry_volume" to mapOf("volume" to text("24"))
        )
        val relative = "src/main/assets/content/es/math_physics/math_course_01/arithmetic_fundamentals/lesson_math_puzzle_sandbox.json"
        val lesson = JSONObject(sequenceOf(File(relative), File("app/$relative")).first { it.isFile }.readText(Charsets.UTF_8))
        val pages = lesson.getJSONArray("pages")
        val found = mutableSetOf<String>()
        for (page in 0 until pages.length()) {
            val blocks = pages.getJSONObject(page).getJSONArray("blocks")
            for (index in 0 until blocks.length()) {
                val block = blocks.getJSONObject(index)
                if (!MathPuzzleTypes.isExercise(block.optString("type"))) continue
                val id = block.getString("id")
                val spec = MathPuzzleParser.parse(block)
                val answers = solutions.getValue(id)
                assertEquals(id, spec.responses.map { it.id }.toSet(), answers.keys)
                val savedAnswers = JSONObject()
                spec.responses.forEach { response ->
                    val answer = answers.getValue(response.id)
                    val validation = MathValidators.validate(response.validator, answer)
                    assertTrue("$id/${response.id}: ${validation.message}", validation.correct)
                    savedAnswers.put(response.id, MathAnswerJson.encode(answer))
                }
                val state = JSONObject().put("correct", true).put("contentFingerprint", MathPuzzleState.fingerprint(block)).put("responses", savedAnswers)
                assertTrue(id, MathPuzzleState.isCurrentCorrect(block, state))
                val incomplete = JSONObject(state.toString())
                incomplete.getJSONObject("responses").remove(spec.responses.first().id)
                assertFalse("$id no puede completar con una respuesta ausente", MathPuzzleState.isCurrentCorrect(block, incomplete))
                val revised = JSONObject(block.toString()).put("prompt", "Enunciado revisado")
                assertFalse("$id no puede usar progreso de otro contenido", MathPuzzleState.isCurrentCorrect(revised, state))
                found.add(id)
            }
        }
        assertEquals(solutions.keys, found)
    }
}
