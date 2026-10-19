package com.learngraphicsprogramming.app.math

import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** Classification used by the attempt host, through the real validator contracts. */
class MathAttemptValidationTest {
    @Test fun everyExistingMathResponseCanProduceOneCompleteIncorrectEvaluation() {
        val fixtures = listOf(
            fixture("math_multiple_choice_single", validator("choice", JSONArray(listOf("a"))), MathAnswer.Choice(setOf("b")))
                .configure { put("options", ids("a", "b")) },
            fixture("math_multiple_choice_multi", validator("choice", JSONArray(listOf("a", "c"))), MathAnswer.Choice(setOf("a")))
                .configure { put("options", ids("a", "b", "c")) },
            fixture("math_numeric_input", validator("numeric", 4), MathAnswer.Text("5")),
            fixture("math_expression_input", validator("equivalent_expression", "x+1"), MathAnswer.Text("x+2")),
            fixture("math_multi_input", fields(), MathAnswer.Fields(mapOf("x" to "9", "y" to "2"))).withFields(),
            fixture("math_structured_answer", fields(), MathAnswer.Fields(mapOf("x" to "9", "y" to "2"))).withFields(),
            fixture("math_equation_fill_blanks", fields(), MathAnswer.Fields(mapOf("x" to "9", "y" to "2"))).withFields(),
            fixture("math_ordering_drag", validator("order", JSONArray(listOf("a", "b"))), MathAnswer.Order(listOf("b", "a")))
                .configure { put("items", ids("a", "b")) },
            fixture("math_matching_drag", validator("matching", JSONObject().put("a", "one").put("b", "two")),
                MathAnswer.Matching(mapOf("a" to "two", "b" to "one")))
                .configure { put("sources", ids("a", "b")); put("targets", ids("one", "two")) },
            fixture("math_number_line", validator("number_line", 1), MathAnswer.NumberLine(2.0, true, "none")),
            fixture("math_slider_interaction", JSONObject().put("type", "sliders").put("values", JSONObject().put("scale", validator("numeric", 1))),
                MathAnswer.Sliders(mapOf("scale" to 2.0))).configure { put("parameters", ids("scale")) },
            fixture("math_coordinate_graph_builder", coordinates("point", point(1, 1)), graph(point(2, 2))),
            fixture("math_rigid_angle_copy", RigidAngleCopyTest.spec(), MathAnswer.RigidAngle(RigidAnglePlacement(-3.0, 8.0, .5))),
            fixture("math_geometry_builder", validator("geometry", 4).put("shape", "rectangle"),
                graph(point(0, 0), point(3, 0), point(3, 2), point(0, 2)))
        )
        assertEquals(MathPuzzleTypes.responseTypes, fixtures.map { it.block.getString("type") }.toSet())
        fixtures.forEach { fixture ->
            val spec = MathPuzzleParser.parse(fixture.block).responses.single()
            val result = MathValidators.validate(spec.validator, fixture.answer)
            assertFalse(spec.type, result.correct)
            assertTrue("A complete wrong ${spec.type} is evaluable", result.evaluable)
        }
    }

    @Test fun supportedTextValidatorFamiliesDistinguishRealMistakesFromBlankOrParserErrors() {
        listOf(
            validator("numeric", 4) to "5",
            validator("tolerance", 4) to "5",
            validator("exact_fraction", "1/2") to "2/3",
            validator("equivalent_expression", "x+1") to "x+2",
            validator("symbolic", "x+1") to "x+2",
            validator("text", "left") to "right",
            validator("exact_text", "left") to "right",
            validator("latex", "\\frac{1}{2}") to "\\frac{1}{3}"
        ).forEach { (spec, wrong) ->
            val checked = MathValidators.validate(spec, MathAnswer.Text(wrong))
            assertFalse(checked.correct)
            assertTrue(spec.getString("type"), checked.evaluable)
            assertNotEvaluated(spec, MathAnswer.Text("  "))
        }
        assertNotEvaluated(validator("numeric", 1), MathAnswer.Text("1+"))
        assertNotEvaluated(validator("numeric", 1), MathAnswer.Text("NaN"))
        assertNotEvaluated(validator("exact_fraction", 1), MathAnswer.Text("1/0"))
        assertNotEvaluated(validator("equivalent_expression", "x"), MathAnswer.Text("sin(x)"))
        assertNotEvaluated(validator("equivalent_expression", "x"), MathAnswer.Text("x+"))
        assertNotEvaluated(validator("latex", "\\frac{1}{2}"), MathAnswer.Text("\\frac{1}{}"))
        assertNotEvaluated(validator("latex", "x"), MathAnswer.Text("\\frac{"))
        assertNotEvaluated(validator("latex", "x"), MathAnswer.Text("x+"))
        assertNotEvaluated(validator("latex", "x"), MathAnswer.Text("x\\cdot{}"))
        assertNotEvaluated(validator("latex", "x"), MathAnswer.Text("nCr2"))
        assertNotEvaluated(validator("latex", "x"), MathAnswer.Text("\\frac{x+}{2}"))
        for (complete in listOf("x!", "50%", "5!^2", "x+2", "-x", "x+-2")) {
            val result = MathValidators.validate(validator("latex", "y"), MathAnswer.Text(complete))
            assertFalse(complete, result.correct)
            assertTrue(complete, result.evaluable)
        }
        val literalOperator = MathValidators.validate(validator("latex", "+"), MathAnswer.Text("-"))
        assertFalse(literalOperator.correct)
        assertTrue("An operator-only question still grades an operator-only answer", literalOperator.evaluable)
        val literalRelation = MathValidators.validate(validator("latex", "="), MathAnswer.Text("<"))
        assertFalse(literalRelation.correct)
        assertTrue(literalRelation.evaluable)
    }

    @Test fun unfinishedSelectionsFieldsMatchesOrdersAndGraphsAreNotIncorrectEvaluations() {
        assertNotEvaluated(validator("choice", JSONArray(listOf("a"))), MathAnswer.Choice(emptySet()))
        assertNotEvaluated(fields(), MathAnswer.Fields(mapOf("x" to "1")))
        assertNotEvaluated(fields(), MathAnswer.Fields(mapOf("x" to "9", "y" to "")))
        assertNotEvaluated(validator("order", JSONArray(listOf("a", "b"))), MathAnswer.Order(listOf("a")))
        assertNotEvaluated(validator("matching", JSONObject().put("a", "one").put("b", "two")), MathAnswer.Matching(mapOf("a" to "two")))
        assertNotEvaluated(validator("number_line", 1), MathAnswer.NumberLine(Double.NaN, true, "none"))
        assertNotEvaluated(JSONObject().put("type", "sliders").put("values", JSONObject().put("scale", validator("numeric", 1))), MathAnswer.Sliders(emptyMap()))
        assertNotEvaluated(coordinates("line", point(0, 0), point(1, 1)), graph(point(0, 0)))
        assertNotEvaluated(coordinates("point", point(0, 0)), graph(MathPoint(Double.NaN, 0.0)))
        assertNotEvaluated(validator("geometry", 4), graph(point(0, 0), point(1, 1)))
        assertNotEvaluated(validator("geometry", 4), graph(MathPoint(Double.NaN, 0.0), point(1, 0), point(0, 1)))
    }

    @Test fun aWrongFieldCannotHideAnotherFieldsParserOrInternalError() {
        val result = MathValidators.validate(fields(), MathAnswer.Fields(mapOf("x" to "9", "y" to "invalid")))
        assertFalse(result.correct)
        assertFalse(result.evaluable)
        assertTrue(result.message.startsWith("y:"))

        val type = "attempt_test_broken_child"
        var calls = 0
        MathValidators.register(type, object : MathAnswerValidator {
            override fun validate(spec: JSONObject, answer: MathAnswer): MathValidationResult {
                calls++
                throw IllegalStateException("Internal evaluator failure")
            }
        })
        val spec = JSONObject().put("type", "sliders").put("values", JSONObject()
            .put("wrong", validator("numeric", 1)).put("broken", JSONObject().put("type", type)))
        assertNotEvaluated(spec, MathAnswer.Sliders(mapOf("wrong" to 9.0, "broken" to 2.0)))
        assertEquals(1, calls)
    }

    @Test fun completeInvalidGeometricConstructionsRemainDeliberateWrongAnswers() {
        val rectangle = validator("geometry", 4).put("shape", "rectangle")
        listOf(
            graph(point(0, 0), point(2, 2), point(2, 0), point(0, 2)),
            graph(point(0, 0), point(2, 0), point(2, 0), point(0, 2)),
            graph(point(0, 0), point(2, 0), point(3, 2), point(1, 2))
        ).forEach { answer ->
            val result = MathValidators.validate(rectangle, answer)
            assertFalse(result.correct)
            assertTrue(result.evaluable)
        }
        val degenerateLine = MathValidators.validate(coordinates("line", point(0, 0), point(1, 1)), graph(point(2, 2), point(2, 2)))
        assertFalse(degenerateLine.correct)
        assertTrue(degenerateLine.evaluable)
    }

    @Test fun configurationExceptionsAndWrongAnswerContractsAreNotStudentMistakes() {
        assertNotEvaluated(JSONObject().put("type", "not_registered"), MathAnswer.Text("1"))
        assertNotEvaluated(validator("numeric", "bad_expected"), MathAnswer.Text("1"))
        assertNotEvaluated(validator("numeric", 1).put("tolerance", -1), MathAnswer.Text("2"))
        assertNotEvaluated(validator("text", "left"), MathAnswer.Choice(setOf("left")))
        assertNotEvaluated(validator("choice", JSONArray(listOf("a"))), MathAnswer.Text("a"))
        val type = "attempt_test_argument_exception"
        MathValidators.register(type, object : MathAnswerValidator {
            override fun validate(spec: JSONObject, answer: MathAnswer): MathValidationResult = throw IllegalArgumentException("Unsupported input")
        })
        assertNotEvaluated(JSONObject().put("type", type), MathAnswer.Text("complete"))
    }

    @Test fun finiteInputCoordinatesThatOverflowTheEvaluatorAreNotPenalizableResults() {
        assertNotEvaluated(coordinates("line", point(0, 0), point(1, 1)),
            graph(MathPoint(-1e308, 0.0), MathPoint(1e308, 0.0)))
        assertNotEvaluated(coordinates("parabola", point(0, 0), point(1, 1)),
            graph(point(0, 0), MathPoint(1e308, 1.0)))
        assertNotEvaluated(validator("geometry", 4),
            graph(point(0, 0), MathPoint(1e308, 0.0), MathPoint(1e308, 1e308), MathPoint(0.0, 1e308)))
    }

    @Test fun completeCorrectAnswersRemainEvaluableForTheHostWithoutAnIncorrectPenalty() {
        listOf(
            validator("numeric", 4) to MathAnswer.Text("4"),
            validator("latex", "\\frac{1}{2}") to MathAnswer.Text("\\frac{1}{2}"),
            fields() to MathAnswer.Fields(mapOf("x" to "1", "y" to "2")),
            validator("choice", JSONArray(listOf("a"))) to MathAnswer.Choice(setOf("a")),
            coordinates("point", point(1, 1)) to graph(point(1, 1))
        ).forEach { (spec, answer) ->
            val result = MathValidators.validate(spec, answer)
            assertTrue(result.correct)
            assertTrue(result.evaluable)
        }
    }

    private inner class Fixture(val block: JSONObject, val answer: MathAnswer) {
        fun configure(action: JSONObject.() -> Unit): Fixture = apply { block.action() }
        fun withFields() = configure { put("fields", ids("x", "y")) }
    }
    private fun fixture(type: String, validator: JSONObject, answer: MathAnswer) =
        Fixture(JSONObject().put("type", type).put("prompt", "Prueba").put("validator", validator), answer)
    private fun assertNotEvaluated(spec: JSONObject, answer: MathAnswer) {
        val result = MathValidators.validate(spec, answer)
        assertFalse("$answer: ${result.message}", result.correct)
        assertFalse("$answer must not consume an attempt: ${result.message}", result.evaluable)
    }
    private fun fields() = JSONObject().put("type", "fields").put("fields", JSONObject()
        .put("x", validator("numeric", 1)).put("y", validator("numeric", 2)))
    private fun validator(type: String, expected: Any) = JSONObject().put("type", type).put("expected", expected)
    private fun ids(vararg ids: String) = JSONArray().apply { ids.forEach { put(JSONObject().put("id", it)) } }
    private fun point(x: Int, y: Int) = MathPoint(x.toDouble(), y.toDouble())
    private fun graph(vararg points: MathPoint) = MathAnswer.Graph(points.toList())
    private fun coordinates(mode: String, vararg points: MathPoint) = JSONObject().put("type", "coordinates").put("mode", mode)
        .put("expected", JSONArray().apply { points.forEach { put(JSONObject().put("x", it.x).put("y", it.y)) } })
}
