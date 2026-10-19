package com.learngraphicsprogramming.app.math

import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

class MathValidatorsTest {
    @Test fun numericAcceptsFractionsDecimalCommaSignsAndPercent() {
        val spec = validator("numeric", "-0.75")
        assertCorrect(spec, "-3/4")
        assertCorrect(spec, "3/-4")
        assertCorrect(spec, "-0,75")
        assertCorrect(spec, "-75%")
        assertIncorrect(spec, "3/4")
        assertIncorrect(spec, "3/0")
        assertIncorrect(spec, "NaN")
        assertIncorrect(spec, "Infinity")
    }

    @Test fun toleranceIsAbsoluteAndRejectsNonFiniteSpecifications() {
        val spec = validator("tolerance", "5").put("tolerance", 0.01)
        assertCorrect(spec, "5.009")
        assertIncorrect(spec, "5.02")
        assertIncorrect(spec, "1e999")
        assertFalse(MathValidators.validate(spec.put("tolerance", -1), MathAnswer.Text("5")).correct)
        assertSchemaError(validator("numeric", "NaN"))
        assertSchemaError(validator("numeric", "2").put("tolerance", "Infinity"))
    }

    @Test fun numericExactnessDoesNotCollapseLargeIntegersOrTinyDecimalsToDouble() {
        val large = validator("numeric", "10000000000000001")
        assertCorrect(large, "10000000000000001")
        assertIncorrect(large, "10000000000000000")
        assertIncorrect(validator("numeric", "0.3"), "0.30000000000000004")
        assertCorrect(validator("numeric", "0.3"), "3/10")
        assertIncorrect(validator("numeric", "1e-1000"), "0")
        assertCorrect(validator("numeric", "1e-1000"), "1e-1000")
    }

    @Test fun toleranceBoundariesAreComparedBeforeDoubleRounding() {
        val decimal = validator("tolerance", "0.3").put("tolerance", 0.1)
        assertCorrect(decimal, "0.4")
        assertIncorrect(decimal, "0.400000000000000001")
        val large = validator("tolerance", "10000000000000001").put("tolerance", 0.5)
        assertIncorrect(large, "10000000000000000")
        assertCorrect(large.put("tolerance", 1), "10000000000000000")
        assertCorrect(validator("numeric", "0.3").put("tolerance", 1e-6), "0.30000000000000004")
    }

    @Test fun fractionsAreComparedExactlyAfterReducing() {
        val spec = validator("exact_fraction", "3/4")
        assertCorrect(spec, "6/8")
        assertCorrect(spec, "-6/-8")
        assertCorrect(spec, "0.75")
        assertIncorrect(spec, "0.7500000000000001")
        assertIncorrect(spec, "6/0")
        assertCorrect(spec.put("tolerance", 0.001), "0.7505")
    }

    @Test fun expressionsUseExactPolynomialEquivalence() {
        assertCorrect(validator("equivalent_expression", "x/2"), "0.5x")
        assertCorrect(validator("equivalent_expression", "(x+1)^2"), "x^2+2x+1")
        assertCorrect(validator("equivalent_expression", "-x^2+3"), "3-(x*x)")
        assertCorrect(validator("equivalent_expression", "4π"), "2*pi+2π")
        assertCorrect(validator("equivalent_expression", "1/3"), "2/6")
        assertIncorrect(validator("equivalent_expression", "1/3"), "0.3333333333333333")
        assertIncorrect(validator("equivalent_expression", "x+1"), "x-1")
        assertIncorrect(validator("equivalent_expression", "π"), "3.141592653589793")
        assertCorrect(validator("equivalent_expression", "x/2"), "x/(x-x+2)")
        assertCorrect(validator("equivalent_expression", "0"), "x-x")
    }

    @Test fun equationsAllowReversedSidesAndRationalScaling() {
        val spec = validator("equivalent_expression", "y=3x+2")
        assertCorrect(spec, "3x+2=y")
        assertCorrect(spec, "2y=6x+4")
        assertIncorrect(spec, "y=3x-2")
        assertCorrect(validator("equivalent_expression", "x+2=5"), "x=3")
        assertIncorrect(validator("equivalent_expression", "x+2=5"), "x=4")
        assertCorrect(validator("equivalent_expression", "x^2=1"), "(x-1)(x+1)=0")
        assertIncorrect(spec, "3x+2")
    }

    @Test fun unsupportedExpressionsAreReportedInsteadOfSampledOrComparedLiterally() {
        val spec = validator("equivalent_expression", "x")
        listOf("sin(x)", "x/x", "x^-2", "x^9", "x+y", "1/(x-1)").forEach {
            val result = MathValidators.validate(spec, MathAnswer.Text(it))
            assertFalse(it, result.correct)
            assertTrue(it, result.message.contains("no soportada"))
        }
    }

    @Test fun compositeFieldsRequireEveryEditableFieldAndValidateEachType() {
        val spec = JSONObject().put("type", "fields").put("fields", JSONObject()
            .put("radius", validator("numeric", 3))
            .put("direction", validator("text", "left")))
        assertTrue(MathValidators.validate(spec, MathAnswer.Fields(mapOf("radius" to "6/2", "direction" to "left"))).correct)
        assertFalse(MathValidators.validate(spec, MathAnswer.Fields(mapOf("radius" to "3"))).correct)
        val wrong = MathValidators.validate(spec, MathAnswer.Fields(mapOf("radius" to "3", "direction" to "right")))
        assertFalse(wrong.correct)
        assertTrue(wrong.message.startsWith("direction:"))
    }

    @Test fun choicesOrdersAndMatchingCompareStableIds() {
        val choice = validator("choice", JSONArray(listOf("a", "c")))
        assertTrue(MathValidators.validate(choice, MathAnswer.Choice(setOf("c", "a"))).correct)
        assertFalse(MathValidators.validate(choice, MathAnswer.Choice(setOf("a"))).correct)
        val order = validator("order", JSONArray(listOf("first", "second")))
        assertTrue(MathValidators.validate(order, MathAnswer.Order(listOf("first", "second"))).correct)
        assertFalse(MathValidators.validate(order, MathAnswer.Order(listOf("second", "first"))).correct)
        val matching = validator("matching", JSONObject().put("a", "one").put("b", "two"))
        assertTrue(MathValidators.validate(matching, MathAnswer.Matching(mapOf("b" to "two", "a" to "one"))).correct)
        assertFalse(MathValidators.validate(matching, MathAnswer.Matching(mapOf("a" to "two", "b" to "one"))).correct)
    }

    @Test fun numberLineValidatesPositionBoundaryAndDirection() {
        val spec = JSONObject().put("type", "number_line").put("value", -2).put("closed", false).put("direction", "right")
        assertTrue(MathValidators.validate(spec, MathAnswer.NumberLine(-2.0, false, "right")).correct)
        assertFalse(MathValidators.validate(spec, MathAnswer.NumberLine(-2.0, true, "right")).correct)
        assertFalse(MathValidators.validate(spec, MathAnswer.NumberLine(-2.0, false, "left")).correct)
        assertFalse(MathValidators.validate(spec, MathAnswer.NumberLine(Double.NaN, false, "right")).correct)
    }

    @Test fun sliderValidatorUsesPerParameterTolerance() {
        val spec = JSONObject().put("type", "sliders").put("values", JSONObject()
            .put("scale", validator("numeric", 2).put("tolerance", 0.1)))
        assertTrue(MathValidators.validate(spec, MathAnswer.Sliders(mapOf("scale" to 2.05))).correct)
        assertFalse(MathValidators.validate(spec, MathAnswer.Sliders(mapOf("scale" to 2.2))).correct)
        assertFalse(MathValidators.validate(spec, MathAnswer.Sliders(mapOf("scale" to Double.POSITIVE_INFINITY))).correct)
        val roundoff = JSONObject().put("type", "sliders").put("values", JSONObject().put("scale", validator("numeric", "0.3")))
        assertTrue(MathValidators.validate(roundoff, MathAnswer.Sliders(mapOf("scale" to (0.1 + 0.2)))).correct)
        roundoff.getJSONObject("values").getJSONObject("scale").put("tolerance", 0)
        assertFalse(MathValidators.validate(roundoff, MathAnswer.Sliders(mapOf("scale" to (0.1 + 0.2)))).correct)
    }

    @Test fun lineValidationAcceptsDifferentPointsOnTheSameLine() {
        val spec = coordinates("line", listOf(point(0, 1), point(1, 3)))
        assertTrue(MathValidators.validate(spec, graph(point(-2, -3), point(3, 7))).correct)
        assertFalse(MathValidators.validate(spec, graph(point(2, 5), point(2, 5))).correct)
        assertFalse(MathValidators.validate(spec, graph(point(0, 1), point(1, 4))).correct)
        val vertical = coordinates("line", listOf(point(2, 0), point(2, 1)))
        assertTrue(MathValidators.validate(vertical, graph(point(2, -3), point(2, 4))).correct)
    }

    @Test fun pointScatterAndPolygonHaveDifferentOrderSemantics() {
        val one = coordinates("point", listOf(point(1, -2)))
        assertTrue(MathValidators.validate(one, graph(point(1, -2))).correct)
        assertFalse(MathValidators.validate(one, graph(point(1, -2), point(1, -2))).correct)
        val scatter = coordinates("scatter", listOf(point(0, 0), point(2, 1)))
        assertTrue(MathValidators.validate(scatter, graph(point(2, 1), point(0, 0))).correct)
        val square = listOf(point(0, 0), point(3, 0), point(3, 3), point(0, 3))
        val polygon = coordinates("polygon", square)
        assertTrue(MathValidators.validate(polygon, MathAnswer.Graph(square.reversed())).correct)
        assertTrue(MathValidators.validate(polygon, MathAnswer.Graph(square.drop(2) + square.take(2))).correct)
        assertFalse(MathValidators.validate(polygon, graph(square[0], square[2], square[1], square[3])).correct)
    }

    @Test fun parabolaValidationUsesVertexAndCurvature() {
        val spec = coordinates("parabola", listOf(point(1, -2), point(2, 0)))
        assertTrue(MathValidators.validate(spec, graph(point(1, -2), point(3, 6))).correct)
        assertFalse(MathValidators.validate(spec, graph(point(1, -2), point(3, 4))).correct)
        assertFalse(MathValidators.validate(spec, graph(point(1, -2), point(1, 4))).correct)
        assertFalse(MathValidators.validate(spec, graph(point(0, -2), point(2, 6))).correct)
    }

    @Test fun geometryRejectsCrossingsDegenerateEdgesAndWrongShape() {
        val rectangle = JSONObject().put("type", "geometry").put("expected", 12).put("shape", "rectangle")
        assertTrue(MathValidators.validate(rectangle, graph(point(0, 0), point(4, 0), point(4, 3), point(0, 3))).correct)
        assertFalse(MathValidators.validate(rectangle, graph(point(0, 0), point(4, 3), point(4, 0), point(0, 3))).correct)
        assertFalse(MathValidators.validate(rectangle, graph(point(0, 0), point(4, 0), point(4, 0), point(0, 3))).correct)
        assertFalse(MathValidators.validate(rectangle, graph(point(0, 0), point(4, 0), point(5, 3), point(1, 3))).correct)
        assertFalse(MathValidators.validate(rectangle, graph(point(0, 0), point(1, 0), point(2, 0), point(3, 0))).correct)
        assertFalse(MathValidators.validate(rectangle, graph(MathPoint(Double.NaN, 0.0), point(4, 0), point(4, 3), point(0, 3))).correct)
        val rotated = graph(point(0, 0), point(1, 1), point(0, 2), point(-1, 1))
        assertTrue(MathValidators.validate(rectangle.put("expected", 2), rotated).correct)
    }

    @Test fun geometryRemainsStableUnderLargeTranslationAndRejectsOverlappingEdges() {
        val expected = JSONObject().put("type", "geometry").put("expected", 12).put("shape", "rectangle")
        val translated = MathAnswer.Graph(listOf(
            MathPoint(1e9, 1e9), MathPoint(1e9 + 4, 1e9),
            MathPoint(1e9 + 4, 1e9 + 3), MathPoint(1e9, 1e9 + 3)))
        assertEquals(12.0, MathGeometry.area(translated.points), 0.0)
        assertTrue(MathValidators.validate(expected, translated).correct)
        val overlap = graph(point(0, 0), point(3, 0), point(1, 0), point(3, 3), point(0, 3))
        assertFalse(MathGeometry.isSimplePolygon(overlap.points))
        val crossingWithNonzeroArea = graph(point(0, 0), point(4, 4), point(0, 4), point(3, 0))
        assertTrue(MathGeometry.area(crossingWithNonzeroArea.points) > 0)
        assertFalse(MathGeometry.isSimplePolygon(crossingWithNonzeroArea.points))
        val touching = graph(point(0, 0), point(4, 0), point(4, 4), point(0, 0), point(0, 4))
        assertFalse(MathGeometry.isSimplePolygon(touching.points))
        assertSchemaError(validator("geometry", 0))
    }

    @Test fun compositeParserNormalizesSingletonAndPreservesMultipleResponses() {
        val singleton = JSONObject().put("type", "math_numeric_input").put("question", "Cuánto vale?")
            .put("validator", validator("numeric", 4))
        val parsed = MathPuzzleParser.parse(singleton)
        assertEquals("Cuánto vale?", parsed.prompt)
        assertEquals("response", parsed.responses.single().id)
        val composite = JSONObject().put("type", "math_puzzle").put("version", 1).put("prompt", "Compuesto")
            .put("representations", JSONArray().put(JSONObject().put("type", "math_formula").put("text", "2+2")))
            .put("responses", JSONArray().put(response("value", "math_numeric_input", validator("numeric", 4)))
                .put(response("expression", "math_expression_input", validator("equivalent_expression", "2x"))))
            .put("feedback", JSONObject().put("correct", "Bien").put("incorrect", "Revisa"))
        val parsedComposite = MathPuzzleParser.parse(composite)
        assertEquals(2, parsedComposite.responses.size)
        assertEquals("Bien", parsedComposite.correctFeedback)
        assertEquals("math_formula", parsedComposite.representations.single().getString("type"))
    }

    @Test fun parserRejectsMalformedContentAndDuplicateResponseIds() {
        val base = JSONObject().put("type", "math_puzzle").put("prompt", "Prueba")
        val response = response("same", "math_numeric_input", validator("numeric", 4))
        assertParserError(JSONObject(base.toString()).put("responses", JSONArray()))
        assertParserError(JSONObject(base.toString()).put("responses", JSONArray().put(response).put(response)))
        assertParserError(JSONObject(base.toString()).put("responses", JSONArray().put(JSONObject(response.toString()).put("type", "unknown"))))
        assertParserError(JSONObject(base.toString()).put("responses", JSONArray().put(JSONObject(response.toString()).removeAndReturn("validator"))))
        assertParserError(JSONObject(base.toString()).put("responses", JSONArray().put(response)).put("version", 2))
        assertParserError(JSONObject(base.toString()).put("responses", JSONArray().put(response)).put("schemaVersion", 2))
        assertParserError(JSONObject(base.toString()).put("responses", JSONArray().put(response)).put("schemaVersion", 1.5))
        assertParserError(JSONObject(base.toString()).put("responses", JSONArray().put(response)).put("tools", "wrong"))
        assertSchemaError(JSONObject().put("type", "not_registered"))
    }

    @Test fun parserRejectsSingleChoiceCardinalityUnknownOptionsAndValidatorMismatch() {
        val options = JSONArray().put(JSONObject().put("id", "a")).put(JSONObject().put("id", "b"))
        val choice = response("choice", "math_multiple_choice_single", validator("choice", JSONArray(listOf("a"))))
            .put("prompt", "Elige").put("options", options)
        assertEquals(1, MathPuzzleParser.parse(choice).responses.size)
        assertParserError(JSONObject(choice.toString()).put("validator", validator("choice", JSONArray(listOf("a", "b")))))
        assertParserError(JSONObject(choice.toString()).put("validator", validator("choice", JSONArray(listOf("unknown")))))
        assertParserError(JSONObject(choice.toString()).put("validator", validator("numeric", 2)))
        assertParserError(JSONObject(choice.toString()).put("options", JSONArray().put(JSONObject().put("id", "a")).put(JSONObject().put("id", "a"))))
        val numeric = response("numeric", "math_numeric_input", validator("choice", JSONArray(listOf("a")))).put("prompt", "Número")
        assertParserError(numeric)
    }

    @Test fun parserRejectsFieldsThatCannotBeValidatedOrDropdownAnswersThatCannotBeSelected() {
        val definitions = JSONArray().put(JSONObject().put("id", "direction").put("type", "dropdown")
            .put("options", JSONArray().put(JSONObject().put("id", "left")).put(JSONObject().put("id", "right"))))
        val fields = response("fields", "math_multi_input", JSONObject().put("type", "fields")
            .put("fields", JSONObject().put("direction", validator("text", "left"))))
            .put("prompt", "Dirección").put("fields", definitions)
        assertEquals(1, MathPuzzleParser.parse(fields).responses.size)
        assertParserError(JSONObject(fields.toString()).put("validator", JSONObject().put("type", "fields")
            .put("fields", JSONObject().put("missing", validator("text", "left")))))
        assertParserError(JSONObject(fields.toString()).put("validator", JSONObject().put("type", "fields")
            .put("fields", JSONObject().put("direction", validator("text", "unknown")))))
        assertSchemaError(JSONObject().put("type", "fields").put("fields", JSONObject().put("direction", validator("choice", JSONArray(listOf("a"))))))
    }

    @Test fun validatorExtensionsCanBeRegisteredWithoutChangingThePuzzleParser() {
        val type = "test_positive_integer"
        MathValidators.register(type, object : MathAnswerValidator {
            override fun validate(spec: JSONObject, answer: MathAnswer): MathValidationResult {
                val number = (answer as? MathAnswer.Text)?.value?.toIntOrNull()
                return MathValidationResult(number != null && number > 0, "Usa un entero positivo.")
            }
        })
        val config = JSONObject().put("type", type)
        val puzzle = response("custom", "math_numeric_input", config).put("prompt", "Un entero positivo")
        assertEquals(type, MathPuzzleParser.parse(puzzle).responses.single().validator.getString("type"))
        assertTrue(MathValidators.validate(config, MathAnswer.Text("4")).correct)
        assertFalse(MathValidators.validate(config, MathAnswer.Text("-4")).correct)
    }

    @Test fun originalSandboxUsesValidReusablePuzzleSpecifications() {
        val relative = "src/main/assets/content/es/math_physics/math_course_01/arithmetic_fundamentals/lesson_math_puzzle_sandbox.json"
        val file = sequenceOf(File(relative), File("app/$relative")).firstOrNull { it.isFile }
        assertTrue("El sandbox debe estar disponible desde app o la raíz del proyecto", file != null)
        val lesson = JSONObject(file!!.readText(Charsets.UTF_8))
        val pages = lesson.getJSONArray("pages")
        val seen = mutableSetOf<String>()
        for (i in 0 until pages.length()) {
            val blocks = pages.getJSONObject(i).getJSONArray("blocks")
            for (j in 0 until blocks.length()) {
                val block = blocks.getJSONObject(j)
                if (MathPuzzleTypes.isExercise(block.optString("type"))) {
                    MathPuzzleParser.parse(block).responses.forEach { seen.add(it.type) }
                }
            }
        }
        // The original sandbox is immutable curriculum; the optional rigid-copy contract
        // is exercised by RigidAngleCopyTest, not by adding an unreviewed curricular puzzle.
        assertEquals(MathPuzzleTypes.responseTypes - "math_rigid_angle_copy", seen)
    }

    @Test fun answerCodecRoundTripsEveryMechanism() {
        val answers = listOf<MathAnswer>(
            MathAnswer.Text("-3/4"), MathAnswer.Choice(setOf("a", "c")),
            MathAnswer.Fields(mapOf("a" to "4", "direction" to "left")),
            MathAnswer.Order(listOf("b", "a")), MathAnswer.Matching(mapOf("a" to "c")),
            MathAnswer.NumberLine(-2.0, false, "right"), MathAnswer.Sliders(mapOf("scale" to 1.5)),
            graph(point(1, 2), point(3, 4))
        )
        answers.forEach { assertEquals(it, MathAnswerJson.decode(JSONObject(MathAnswerJson.encode(it).toString()))) }
        assertNull(MathAnswerJson.decode(JSONObject().put("type", "unknown")))
        assertNull(MathAnswerJson.decode(JSONObject().put("type", "graph").put("points", JSONArray().put(JSONObject().put("x", "NaN").put("y", 1)))))
        assertNull(MathAnswerJson.decode(JSONObject().put("type", "fields").put("values", JSONObject().put("a", 3))))
    }

    private fun assertCorrect(spec: JSONObject, text: String) {
        val result = MathValidators.validate(spec, MathAnswer.Text(text))
        assertTrue("$text: ${result.message}", result.correct)
    }
    private fun assertIncorrect(spec: JSONObject, text: String) = assertFalse(text, MathValidators.validate(spec, MathAnswer.Text(text)).correct)
    private fun assertSchemaError(spec: JSONObject) = expectArgumentError { MathValidators.validateSpec(spec) }
    private fun assertParserError(spec: JSONObject) = expectArgumentError { MathPuzzleParser.parse(spec) }
    private fun expectArgumentError(action: () -> Unit) {
        var error: IllegalArgumentException? = null
        try { action() } catch (exception: IllegalArgumentException) { error = exception }
        assertTrue("Debe rechazar contenido inválido", error != null)
        assertTrue("Debe explicar el error", !error?.message.isNullOrBlank())
    }
    private fun validator(type: String, expected: Any) = JSONObject().put("type", type).put("expected", expected)
    private fun response(id: String, type: String, validator: JSONObject) = JSONObject().put("id", id).put("type", type).put("validator", validator)
    private fun point(x: Int, y: Int) = MathPoint(x.toDouble(), y.toDouble())
    private fun graph(vararg points: MathPoint) = MathAnswer.Graph(points.toList())
    private fun coordinates(mode: String, points: List<MathPoint>) = JSONObject().put("type", "coordinates").put("mode", mode)
        .put("expected", JSONArray().apply { points.forEach { put(JSONObject().put("x", it.x).put("y", it.y)) } })
    private fun JSONObject.removeAndReturn(key: String): JSONObject { remove(key); return this }
}
