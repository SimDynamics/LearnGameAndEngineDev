package com.learngraphicsprogramming.app.math

import com.learngraphicsprogramming.app.R
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import java.io.File

class MathParametricValidatorsTest {
    @Test fun declaredExactExpressionsAcceptGeneralAndTransferredConstructions() {
        val cases = listOf(
            "a*d-b*c" to listOf("da-cb", "D", "(2ad-2bc)/2"),
            "d*u-b*v" to listOf("ud-vb", "d*(u+v)-v*(b+d)"),
            "a*v-c*u" to listOf("va-uc", "(2av-2cu)/2"),
            "(d*u-b*v)/D" to listOf("(du-bv)/(ad-bc)", "(bv-du)/(bc-ad)", "du/D-bv/D", "((du-bv)*D)/(D*D)", "\\frac{d u-b v}{D}"),
            "(a*v-c*u)/D" to listOf("(va-uc)/(da-cb)", "(cu-av)/(bc-ad)", "av/D-cu/D"),
            "(d*(u+v)-b*(u-v))/D" to listOf("(du+dv-bu+bv)/(ad-bc)", "(u*(d-b)+v*(d+b))/D"),
            "(a*(u-v)-c*(u+v))/D" to listOf("(au-av-cu-cv)/D", "(u*(a-c)-v*(a+c))/(ad-bc)")
        )
        cases.forEach { (expected, alternatives) ->
            val spec = contract(expected)
            MathValidators.validateSpec(spec)
            (listOf(expected) + alternatives).forEach { assertCorrect(spec, it) }
        }
    }

    @Test fun valueAndDomainMistakesHaveDistinctEvaluableFeedback() {
        val spec = contract("(du-bv)/D")
        for (wrong in listOf("(du+bv)/D", "(av-cu)/D", "(du-bv)/(ad+bc)", "(du-bv)/D+0.000000000000000001")) {
            val result = MathValidators.validate(spec, MathAnswer.Text(wrong))
            assertFalse(wrong, result.correct)
            assertTrue(wrong, result.evaluable)
            assertEquals(R.string.math_ui_parametric_value_mismatch, result.uiMessage?.resourceId)
        }
        for (wrong in listOf("d*(du-bv)/(d*D)", "b*(du-bv)/(b*D)", "(du-bv)/D+0/d")) {
            val result = MathValidators.validate(spec, MathAnswer.Text(wrong))
            assertFalse(wrong, result.correct)
            assertTrue(wrong, result.evaluable)
            assertEquals(R.string.math_ui_parametric_domain_mismatch, result.uiMessage?.resourceId)
        }
    }

    @Test fun parserLimitsUnknownDomainProofsAndIncompleteSyntaxAreRetries() {
        val spec = contract("(du-bv)/D")
        for (answer in listOf("(du-bv)/(", "sin(a)", "(a+b)^1000", "x", "a".repeat(4097),
            "((du-bv)/D)*(D^2+1)/(D^2+1)")) {
            val checked = MathValidators.validate(spec, MathAnswer.Text(answer))
            assertFalse(answer.take(40), checked.correct)
            assertFalse(answer.take(40), checked.evaluable)
        }
        assertCorrect(spec, "(du-bv)/D")
    }

    @Test fun malformedExpectedDefinitionsAndAssumptionsNeverBecomeWrongStudentAnswers() {
        val malformed = listOf<(JSONObject) -> Unit>(
            { it.remove("parameters") }, { it.put("parameters", "a,b") },
            { it.put("parameters", JSONArray(listOf("a", "a"))) },
            { it.put("definitions", JSONObject().put("D", "D+1")) },
            { it.put("definitions", JSONObject().put("D", "q")) },
            { it.put("definitions", JSONObject().put("D", 7)) },
            { it.put("nonzero", JSONArray(listOf("0"))) },
            { it.put("nonzero", "D") }, { it.put("expected", "(du-bv)/d") },
            { it.put("expected", "sin(a)") }, { it.put("expected", 7) },
            { it.put("tolerance", 0.01) }, { it.put("parameterDomain", "integer") }
        )
        malformed.forEachIndexed { index, mutate ->
            val spec = contract("(du-bv)/D").also(mutate)
            try {
                MathValidators.validateSpec(spec)
                fail("Malformed content $index was accepted")
            } catch (_: IllegalArgumentException) { }
            val result = MathValidators.validate(spec, MathAnswer.Text("(du-bv)/D"))
            assertFalse("configuration $index", result.correct)
            assertFalse("configuration $index", result.evaluable)
        }
    }

    @Test fun oldExpressionValidatorsKeepTheirBoundedContract() {
        val old = JSONObject().put("type", "equivalent_expression").put("expected", "x/2")
        MathValidators.validateSpec(old)
        assertCorrect(old, "0.5x")
        val unsupported = MathValidators.validate(old, MathAnswer.Text("x/d"))
        assertFalse(unsupported.correct)
        assertFalse(unsupported.evaluable)
        try {
            MathValidators.validateSpec(JSONObject().put("type", "equivalent_expression").put("expected", "ad-bc"))
            fail("The old multi-variable contract silently changed")
        } catch (_: IllegalArgumentException) { }
    }

    @Test fun aTechnicalFieldTakesPriorityOverAnotherCompleteMathematicalMistake() {
        val spec = JSONObject().put("type", "fields").put("fields", JSONObject()
            .put("D", contract("ad-bc")).put("x", contract("(du-bv)/D")))
        MathValidators.validateSpec(spec)
        val result = MathValidators.validate(spec, MathAnswer.Fields(mapOf("D" to "ad+bc", "x" to "sin(a)")))
        assertFalse(result.correct)
        assertFalse(result.evaluable)
        assertEquals("x", result.fieldId)
    }

    @Test fun actualCurricularPuzzlesValidateAllSevenFreeExpressionsAndNumericPredecessors() {
        val lesson = JSONObject(File("src/main/assets/$ASSET").readText())
        val expectedIds = mapOf("construir_el_criterio_con_datos" to 5,
            "trasladar_datos_sin_perder_la_condicion_no_nula" to 2)
        var numericPredecessors = 0
        val found = mutableSetOf<String>()
        val pages = lesson.getJSONArray("pages")
        for (page in 0 until pages.length()) {
            val blocks = pages.getJSONObject(page).getJSONArray("blocks")
            for (block in 0 until blocks.length()) {
                val raw = blocks.getJSONObject(block)
                if (raw.optString("type") != "math_puzzle") continue
                val parsed = MathPuzzleParser.parse(raw)
                val id = raw.getString("id")
                if (id !in expectedIds) { numericPredecessors++; continue }
                found += id
                assertEquals(expectedIds.getValue(id), parsed.responses.size)
                parsed.responses.forEach { response ->
                    assertEquals("math_expression_input", response.type)
                    assertEquals("simple", response.config.getString("inputMode"))
                    assertEquals("ALGEBRA", response.config.getString("keyboard"))
                    assertCorrect(response.validator, response.validator.getString("expected"))
                }
            }
        }
        assertEquals(expectedIds.keys, found)
        assertEquals(9, numericPredecessors)
    }

    private fun assertCorrect(spec: JSONObject, answer: String) {
        val result = MathValidators.validate(spec, MathAnswer.Text(answer))
        assertTrue("$answer: ${result.message}", result.evaluable && result.correct)
    }

    private fun contract(expected: String) = JSONObject().put("type", "parametric_rational_expression")
        .put("parameters", JSONArray(listOf("a", "b", "c", "d", "u", "v")))
        .put("definitions", JSONObject().put("D", "a*d-b*c"))
        .put("nonzero", JSONArray(listOf("D"))).put("expected", expected)

    companion object {
        const val ASSET = "content/es/math_physics/math_algebra_elemental/sistemas_lineales/lesson_formulas_con_datos_y_una_condicion_no_nula.json"
    }
}
