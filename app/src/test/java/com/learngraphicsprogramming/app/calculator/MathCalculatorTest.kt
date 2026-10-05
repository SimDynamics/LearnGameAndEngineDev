package com.learngraphicsprogramming.app.calculator

import com.learngraphicsprogramming.app.math.MathCursor
import com.learngraphicsprogramming.app.math.MathCursorStep
import com.learngraphicsprogramming.app.math.MathExpression
import com.learngraphicsprogramming.app.math.MathExpressionModel
import com.learngraphicsprogramming.app.math.MathLargeOperator
import com.learngraphicsprogramming.app.math.MathLatexParser
import com.learngraphicsprogramming.app.math.MathLatexSerializer
import org.junit.Assert.assertEquals
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.E
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.pow

/** End-to-end tests use the production compiler and Symja adapter, with no fake engine. */
class MathCalculatorTest {
    private val calculator = MathCalculator()

    @Test fun arithmeticRetainsPrecedenceAssociativityAndUnaryPowerSemantics() {
        listOf(
            "2+3*4" to 14.0,
            "(2+3)*4" to 20.0,
            "-2^2" to -4.0,
            "(-2)^2" to 4.0,
            "2-3-4" to -5.0,
            "8/4/2" to 1.0,
            "0.5+1.25" to 1.75,
            "2(3+1)" to 8.0
        ).forEach { (input, expected) ->
            assertResult(input, expected)
        }
        val splitNumber = MathExpression.Sequence(listOf(MathExpression.Number("1"), MathExpression.Number("2"),
            MathExpression.Operator("+"), MathExpression.Number("3")))
        assertEquals("Selections can split a numeric leaf without turning 12 into 1×2", 15.0,
            scalar(success(calculator.evaluate(splitNumber)).expression), 1e-10)
    }

    @Test fun nestedFractionPowerRootAndFunctionEvaluateAsTheirAstStructure() {
        assertResult("\\frac{2^3+\\sqrt{16}}{\\sin{\\frac{\\pi}{2}}+1}", 6.0)
        assertResult("\\sqrt[3]{27}+2^{-2}", 3.25)
        assertResult("10^{20}", 1e20)
        assertResult("10^{-12}", 1e-12)
    }

    @Test fun constantsAndLogarithmsKeepTheirMathematicalMeaning() {
        listOf(
            "\\pi+\\pi" to 2 * PI,
            "e" to E,
            "e/e" to 1.0,
            "\\sin{\\pi}" to 0.0,
            "\\ln{e}" to 1.0,
            "\\log{100}" to 2.0,
            "\\log_2{8}" to 3.0
        ).forEach { (input, expected) ->
            assertResult(input, expected)
        }
    }

    @Test fun degreesAreConvertedSemanticallyAndDefaultAngleModeRemainsRadians() {
        listOf(
            "\\sin{30}" to 0.5,
            "\\cos{60}" to 0.5,
            "\\tan{45}" to 1.0,
            "\\sin{30+60}" to 1.0,
            "\\tan{89.999}" to kotlin.math.tan(89.999 * PI / 180.0)
        ).forEach { (input, expected) ->
            assertResult(input, expected, CalculatorAngleMode.DEG)
        }
        assertResult("\\sin{\\frac{\\pi}{2}}", 1.0, CalculatorAngleMode.RAD)
        val input = parse("\\sin{30}")
        val radians = success(calculator.evaluate(input, CalculatorAngleMode.RAD))
        assertEquals(radians, calculator.evaluate(input))
        val degrees = success(calculator.evaluate(input, CalculatorAngleMode.DEG))
        assertTrue("RAD and DEG cannot evaluate sin(30) identically", radians.expression != degrees.expression)
    }

    @Test fun incompleteExpressionsAndNonRealDomainsReturnControlledErrors() {
        val incomplete = listOf(
            MathExpression.Sequence(),
            parse("1+"),
            parse("\\frac{1}{}"),
            parse("\\sqrt{}"),
            parse("2^{}")
        )
        (incomplete + listOf(parse("1/0"), parse("\\sqrt{-1}"), parse("\\ln{0}"))).forEach {
            assertFailure(it)
        }
        assertTrue(calculator.evaluate(parse("\\tan{90}"), CalculatorAngleMode.DEG) is CalculatorResult.Failure)
        assertTrue(calculator.evaluate(parse("\\tan{\\frac{\\pi}{2}}")) is CalculatorResult.Failure)
    }

    @Test fun unsupportedAstAndEngineSyntaxInLeafFieldsCannotReachTheEngineAsCommands() {
        val one = parse("1")
        val unsupported = listOf(
            MathExpression.Number("1);Quit()"),
            MathExpression.Variable("Run(1)"),
            MathExpression.Symbol("Evaluate[1]"),
            MathExpression.Operator(";"),
            MathExpression.Function("Run", one),
            MathExpression.Relation("="),
            MathExpression.Scripts(one, lower = one),
            MathExpression.LargeOperator(MathLargeOperator.INTEGRAL),
            MathExpression.Matrix(listOf(listOf(one)))
        )
        unsupported.forEach { node ->
            assertFailure(MathExpression.Sequence(listOf(node)))
        }
    }

    @Test fun evaluationLeavesInputAstCursorSelectionAndSavedDraftIntactOnSuccessAndError() {
        for ((source, shouldSucceed) in listOf("\\frac{12}{4}" to true, "\\frac{12}{0}" to false)) {
            val model = MathExpressionModel(parse(source))
            val numerator = listOf(MathCursorStep(0, "numerator"))
            model.cursor = MathCursor(numerator, 1)
            model.select(numerator, 0, 1)
            val expression = model.expression
            val cursor = model.cursor
            val selection = model.selectionRange
            val draft = model.saveDraft()
            val result = calculator.evaluate(expression)
            assertEquals(source, shouldSucceed, result is CalculatorResult.Success)
            assertSame("Calculation must retain the exact input tree", expression, model.expression)
            assertEquals(cursor, model.cursor)
            assertEquals(selection, model.selectionRange)
            assertEquals(draft, model.saveDraft())
            assertEquals(parse(source), model.expression)
        }
    }

    private fun assertResult(
        input: String,
        expected: Double,
        angleMode: CalculatorAngleMode = CalculatorAngleMode.RAD
    ) {
        val result = success(calculator.evaluate(parse(input), angleMode))
        assertEquals("Calculation of $input in $angleMode", expected, scalar(result.expression), max(1e-14, abs(expected) * 1e-12))
        assertEquals("The result LaTeX must describe the rendered result AST", result.expression, parse(result.latex))
        assertEquals(result.expression, parse(MathLatexSerializer.serialize(result.expression)))
    }

    private fun assertFailure(input: MathExpression.Sequence) {
        val result = calculator.evaluate(input)
        assertTrue("Expected a controlled error for $input, received $result", result is CalculatorResult.Failure)
        assertTrue("An error must provide a usable message", (result as CalculatorResult.Failure).message.isNotBlank())
    }

    private fun success(result: CalculatorResult): CalculatorResult.Success {
        assertTrue("Expected an evaluated result, received $result", result is CalculatorResult.Success)
        return result as CalculatorResult.Success
    }

    private fun parse(source: String): MathExpression.Sequence {
        val result = MathLatexParser.parse(source)
        assertTrue("Invalid test fixture $source: $result", result is MathLatexParser.Result.Success)
        return (result as MathLatexParser.Result.Success).expression
    }

    /** Interpret only the scalar display shapes Symja's number formatter can produce. */
    private fun scalar(expression: MathExpression): Double = when (expression) {
        is MathExpression.Number -> expression.value.toDouble()
        is MathExpression.Symbol -> when (expression.value) {
            "π" -> PI
            "e" -> E
            else -> error("Unexpected result symbol: $expression")
        }
        is MathExpression.Fraction -> scalar(expression.numerator) / scalar(expression.denominator)
        is MathExpression.Scripts -> scalar(expression.base).pow(scalar(requireNotNull(expression.upper)))
        is MathExpression.Group -> scalar(expression.content)
        is MathExpression.Sequence -> {
            var value = 1.0
            expression.children.forEachIndexed { index, child ->
                if (child is MathExpression.Operator) {
                    when (child.value) {
                        "-", "−" -> { check(index == 0); value = -value }
                        "×", "·", "*" -> Unit
                        else -> error("Unexpected numeric display operator: $child")
                    }
                } else value *= scalar(child)
            }
            value
        }
        else -> error("Unexpected scalar display shape: $expression")
    }
}
