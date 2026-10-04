package com.learngraphicsprogramming.app.math

import org.junit.Assert.*
import org.junit.Test

class MathKeyboardActionPreviewsTest {
    private fun parse(source: String) = (MathLatexParser.parse(source) as MathLatexParser.Result.Success).expression
    private fun node(action: MathKeyboardAction, source: String? = null) = action.previewExpression(source?.let(::parse))!!.children.single()
    private fun assertEmptySlots(expression: MathExpression) {
        val slots = expression.childSlots()
        assertTrue("A structural preview must offer editable slots", slots.isNotEmpty())
        slots.forEach { (name, slot) -> assertTrue("$name never contains expected answer values", slot.children.isEmpty()) }
    }

    @Test fun everyStructuralActionUsesEmptyAstSlots() {
        val actions = listOf(MathKeyboardAction.Fraction, MathKeyboardAction.Parentheses, MathKeyboardAction.Power,
            MathKeyboardAction.Subscript, MathKeyboardAction.Root, MathKeyboardAction.NthRoot,
            MathKeyboardAction.AbsoluteValue, MathKeyboardAction.Brackets, MathKeyboardAction.Braces,
            MathKeyboardAction.Integral, MathKeyboardAction.Sum, MathKeyboardAction.Product,
            MathKeyboardAction.Limit, MathKeyboardAction.Matrix, MathKeyboardAction.Cases,
            MathKeyboardAction.Derivative, MathKeyboardAction.Function("sin"), MathKeyboardAction.Function("log"))
        actions.forEach { action ->
            val preview = action.previewExpression()!!
            assertEmptySlots(preview.children.single())
            assertFalse("No decorative placeholder becomes answer content", MathLatexSerializer.serialize(preview).contains('□'))
        }
        assertTrue(node(MathKeyboardAction.Fraction) is MathExpression.Fraction)
        assertEquals(MathDelimiter.PARENTHESES, (node(MathKeyboardAction.Parentheses) as MathExpression.Group).delimiter)
        assertNotNull((node(MathKeyboardAction.Power) as MathExpression.Scripts).upper)
        assertNull((node(MathKeyboardAction.Power) as MathExpression.Scripts).lower)
        assertNull((node(MathKeyboardAction.Subscript) as MathExpression.Scripts).upper)
        assertNotNull((node(MathKeyboardAction.Subscript) as MathExpression.Scripts).lower)
    }

    @Test fun operatorBoundsMatchTheirContextWithoutCopyingValues() {
        val indefinite = node(MathKeyboardAction.Integral, "\\int{x}") as MathExpression.LargeOperator
        assertNull(indefinite.lower)
        assertNull(indefinite.upper)
        val bounded = node(MathKeyboardAction.Integral, "\\int_{123}^{456}{x+7}") as MathExpression.LargeOperator
        assertEmptySlots(bounded)
        assertNotNull(bounded.lower)
        assertNotNull(bounded.upper)
        val lowerOnly = node(MathKeyboardAction.Sum, "\\sum_{i=1}{i}") as MathExpression.LargeOperator
        assertNotNull(lowerOnly.lower)
        assertNull(lowerOnly.upper)
        val unrelated = node(MathKeyboardAction.Product, "\\int{x}") as MathExpression.LargeOperator
        assertNotNull(unrelated.lower)
        assertNotNull(unrelated.upper)
    }

    @Test fun functionBaseAndDerivativeOrderFollowTheRelevantNestedTemplate() {
        val naturalLog = node(MathKeyboardAction.Function("log"), "\\sin{x}+\\log{x}") as MathExpression.Function
        assertNull(naturalLog.base)
        val basedLog = node(MathKeyboardAction.Function("log"), "\\frac{1}{\\log_{42}{x+8}}") as MathExpression.Function
        assertEmptySlots(basedLog)
        assertNotNull(basedLog.base)
        assertNotNull((node(MathKeyboardAction.Function("log")) as MathExpression.Function).base)
        assertNull((node(MathKeyboardAction.Function("sin"), "\\log_{2}{x}") as MathExpression.Function).base)
        val derivative = node(MathKeyboardAction.Derivative, "\\frac{\\mathrm{d}^{3}x}{\\mathrm{d}x^{3}}") as MathExpression.Derivative
        assertEmptySlots(derivative)
        assertNotNull(derivative.order)
        assertNull((node(MathKeyboardAction.Derivative) as MathExpression.Derivative).order)
    }

    @Test fun matricesAndCasesCopyOnlyDimensionsFromNestedContext() {
        val matrix = node(MathKeyboardAction.Matrix, "\\sqrt{\\begin{matrix}1&2&3\\\\4&5&6\\end{matrix}}") as MathExpression.Matrix
        assertEquals(2, matrix.rows.size)
        assertTrue(matrix.rows.all { it.size == 3 })
        assertEmptySlots(matrix)
        val cases = node(MathKeyboardAction.Cases, "\\begin{cases}x&x<0\\\\1&x=0\\\\2x&x>0\\end{cases}") as MathExpression.Cases
        assertEquals(3, cases.rows.size)
        assertEmptySlots(cases)
        val defaultMatrix = node(MathKeyboardAction.Matrix) as MathExpression.Matrix
        assertEquals(2, defaultMatrix.rows.size)
        assertEquals(2, defaultMatrix.rows.first().size)
    }

    @Test fun utilityAndSymbolKeysDoNotBecomeMathematicalTemplates() {
        for (action in listOf(MathKeyboardAction.Symbol("+"), MathKeyboardAction.Backspace,
            MathKeyboardAction.MoveLeft, MathKeyboardAction.MoveRight, MathKeyboardAction.PreviousSlot, MathKeyboardAction.NextSlot)) {
            assertNull(action.previewExpression(parse("\\frac{x}{2}")))
        }
    }
}
