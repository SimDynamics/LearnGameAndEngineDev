package com.learngraphicsprogramming.app.math

import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class MathStructuredIntegrationTest {
    private fun validator(type: String, expected: String) = JSONObject().put("type", type).put("expected", expected)

    @Test fun structuredPolynomialsUseExistingExactEquivalence() {
        val spec = validator("equivalent_expression", "x/2+1")
        assertTrue(MathValidators.validate(spec, MathAnswer.Text("\\frac{x}{2}+1")).correct)
        assertTrue(MathValidators.validate(spec, MathAnswer.Text("\\frac{x+2}{2}")).correct)
        assertFalse(MathValidators.validate(spec, MathAnswer.Text("\\frac{x+3}{2}")).correct)
        MathValidators.validateSpec(validator("symbolic", "\\frac{x}{2}+1"))
    }

    @Test fun structuredPowersEquationsAndPiIgnoreLatexFormatting() {
        assertTrue(MathValidators.validate(validator("symbolic", "x^2+2x+1"), MathAnswer.Text("\\left(x+1\\right)^{2}")).correct)
        assertTrue(MathValidators.validate(validator("symbolic", "y=3x+2"), MathAnswer.Text("y=\\frac{6x+4}{2}")).correct)
        assertTrue(MathValidators.validate(validator("symbolic", "4π"), MathAnswer.Text("4\\pi{}")).correct)
    }

    @Test fun unsupportedSemanticDomainsAreControlledRatherThanAccepted() {
        val spec = validator("symbolic", "x")
        for (source in listOf("\\sqrt{x}", "\\sin{x}", "x_{1}", "\\int{x}", "\\frac{x}{x}")) {
            assertFalse(source, MathValidators.validate(spec, MathAnswer.Text(source)).correct)
        }
        assertFalse(MathValidators.validate(spec, MathAnswer.Text("\\frac{x}{")).correct)
    }

    @Test fun structuralLatexValidatorNormalizesSurfaceSyntaxWithoutClaimingCalculus() {
        val spec = validator("latex", "\\sqrt[3]{x}+\\sin(x)+x_1^2")
        assertTrue(MathValidators.validate(spec, MathAnswer.Text("\\sqrt[3]{ x } + \\sin{x}+{x}_{1}^{2}")).correct)
        assertFalse(MathValidators.validate(spec, MathAnswer.Text("\\sqrt[2]{x}+\\sin{x}+x_1^2")).correct)
        assertFalse(MathValidators.validate(spec, MathAnswer.Text("\\sqrt[3]{}+\\sin{x}+x_1^2")).correct)
    }

    @Test fun shortcutsAreInferredFromAstStructuresAndNestedOperators() {
        assertEquals(setOf("FRACTION", "+"), MathShortcutResolver.resolveStructured(validator("latex", "\\frac{x+1}{2}"), "ALGEBRA").toSet())
        assertEquals(setOf("ROOT", "+"), MathShortcutResolver.resolveStructured(validator("latex", "\\sqrt{x+4}"), "ALGEBRA").toSet())
        assertEquals(setOf("INTEGRAL", "POWER"), MathShortcutResolver.resolveStructured(validator("latex", "\\int_0^1 x^2\\,dx"), "ALGEBRA").toSet())
        val nested = MathShortcutResolver.resolveStructured(validator("latex", "\\sqrt[3]{\\frac{x_1^2+\\pi}{2}}"), "ALGEBRA")
        assertEquals(setOf("NTH_ROOT", "FRACTION", "SUBSCRIPT", "POWER", "+", "π"), nested.toSet())
        assertEquals(setOf("π"), MathShortcutResolver.resolveStructured(validator("symbolic", "4pix"), "ALGEBRA").toSet())
        assertEquals(setOf("θ", "+", "α"), MathShortcutResolver.resolveStructured(validator("latex", "\\theta+\\alpha"), "ALGEBRA").toSet())
        assertEquals(setOf("BRACKETS", "+"), MathShortcutResolver.resolveStructured(validator("latex", "\\left[x+1\\right]"), "ALGEBRA").toSet())
        assertEquals(setOf("FUNCTION:log"), MathShortcutResolver.resolveStructured(validator("latex", "\\log{x}"), "ALGEBRA").toSet())
        assertEquals(setOf("."), MathShortcutResolver.resolveStructured(validator("latex", "1.5"), "ALGEBRA").toSet())
    }

    @Test fun structuralOverridesAndInputModesAreValidatedAtContentBoundary() {
        val spec = validator("latex", "\\frac{x+1}{2}")
        MathShortcutResolver.validateConfiguration(spec, "ALGEBRA", JSONArray().put("FRACTION").put("+"), "structured")
        assertTrue(runCatching { MathShortcutResolver.validateConfiguration(spec, "ALGEBRA", JSONArray().put("ROOT"), "structured") }.isFailure)
        assertTrue(runCatching { MathInputMode.parse("structured", "numeric") }.isFailure)
        assertTrue(runCatching { MathInputMode.parse("other", "expression") }.isFailure)
        assertEquals(MathInputMode.SIMPLE, MathInputMode.parse(null, "expression"))
    }
}
