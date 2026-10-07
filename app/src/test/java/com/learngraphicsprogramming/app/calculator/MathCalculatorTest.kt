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
import java.math.BigInteger
import java.util.concurrent.Callable
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit

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

    @Test fun percentagesAreAlwaysPostfixDivisionByOneHundred() {
        listOf(
            "50\\%" to 0.5,
            "12.5\\%" to 0.125,
            "200*10\\%" to 20.0,
            "50\\%+25\\%" to 0.75,
            "200+10\\%" to 200.1,
            "-50\\%" to -0.5,
            "(20+30)\\%" to 0.5,
            "50\\%\\%" to 0.005,
            "2(50\\%)" to 1.0
        ).forEach { (input, expected) -> assertResult(input, expected) }
        assertFailure(parse("\\%50"))
        assertResult("5\\%^{2}", 0.0025)
        val legacyPercent = MathExpression.Sequence(listOf(MathExpression.Number("50"), MathExpression.Symbol("%")))
        assertEquals("0.5", success(calculator.evaluate(legacyPercent)).latex)
    }

    @Test fun inverseFunctionsConvertTheReturnedAngleInDegrees() {
        listOf(
            "\\arcsin{0.5}" to 30.0,
            "\\arccos{0.5}" to 60.0,
            "\\arctan{1}" to 45.0,
            "\\arctan{-1}" to -45.0,
            "\\arcsin{\\sin{30}}" to 30.0
        ).forEach { (input, expected) -> assertResult(input, expected, CalculatorAngleMode.DEG) }
        assertResult("\\arcsin{0.5}", PI / 6)
        assertResult("\\arccos{0.5}", PI / 3)
        assertResult("\\arctan{1}", PI / 4)
        assertFailure(parse("\\arcsin{1.01}"))
        assertFailure(parse("\\arccos{-1.01}"))
        assertFailure(parse("\\arcsin_2{0.5}"))
    }

    @Test fun nthRootsUseRealOddRootsAndAbsoluteValueUsesItsStructuralGroup() {
        assertResult("\\sqrt[3]{-8}", -2.0)
        assertResult("\\sqrt[5]{-32}", -2.0)
        assertResult("\\sqrt[1]{-7}", -7.0)
        assertResult("\\sqrt[4]{16}", 2.0)
        assertResult("\\sqrt[1+2]{-27}", -3.0)
        assertResult("|-3|", 3.0)
        assertResult("|2-5|+|0|", 3.0)
        listOf("\\sqrt[0]{8}", "\\sqrt[-3]{-8}", "\\sqrt[1.5]{8}",
            "\\sqrt[2]{-8}", "\\sqrt[1001]{8}", "\\sqrt[]{8}").forEach { assertFailure(parse(it)) }
    }

    @Test fun factorialCombinationAndPermutationOperateOnExactIntegers() {
        listOf(
            "0!" to 1.0,
            "5!" to 120.0,
            "(2+3)!" to 120.0,
            "-3!" to -6.0,
            "5nCr2" to 10.0,
            "5nPr2" to 20.0,
            "5nCr0" to 1.0,
            "5nPr0" to 1.0,
            "5nCr5" to 1.0,
            "5nPr5" to 120.0,
            "(2+3)nCr(1+1)" to 10.0,
            "2*5nCr2+1" to 21.0,
            "2(5nCr2)" to 20.0,
            "2^{3!}" to 64.0,
            "5!^{2}" to 14400.0,
            "5!\\%" to 1.2
        ).forEach { (input, expected) -> assertResult(input, expected) }
        assertEquals("2432902008176640000", success(calculator.evaluate(parse("20!"))).latex)
        val legacyFactorial = MathExpression.Sequence(listOf(MathExpression.Number("5"), MathExpression.Symbol("!")))
        assertEquals("120", success(calculator.evaluate(legacyFactorial)).latex)
        assertEquals("118264581564861424", success(calculator.evaluate(parse("60nCr30"))).latex)
        assertEquals("1860480", success(calculator.evaluate(parse("20nPr5"))).latex)
        assertEquals("9007199254740993", success(calculator.evaluate(parse("9007199254740993"))).latex)
        assertEquals("1", success(calculator.evaluate(parse("9007199254740993-9007199254740992"))).latex)
        val factorial170 = (1..170).fold(BigInteger.ONE) { value, n -> value * BigInteger.valueOf(n.toLong()) }
        assertEquals(factorial170.toString(), success(calculator.evaluate(parse("170!"))).latex)
    }

    @Test fun discreteDomainsRejectFractionsNegativesInvalidSelectionsAndUnboundedWork() {
        listOf("1.5!", "(-1)!", "(3/2)!", "1001!", "171!", "5.1nCr2", "5nCr2.1",
            "5nCr6", "5nPr6", "(-5)nCr2", "5nPr(-1)", "1001nCr2", "5nCr", "!5")
            .forEach { assertFailure(parse(it)) }
        // Decimal syntax may denote an exact integer, but fractional values are never rounded.
        assertResult("5.0!", 120.0)
        assertResult("(10/2)nCr2", 10.0)
        assertFailure(parse("9007199254740993.1!"))
    }

    @Test fun compilationAndExactPowersKeepSizeAndDepthBounds() {
        assertFailure(MathExpression.Sequence(List(513) { MathExpression.Number("1") }))
        assertFailure(MathExpression.Sequence(listOf(MathExpression.Number("1".repeat(65)))))
        var nested = parse("1")
        repeat(40) { nested = MathExpression.Sequence(listOf(MathExpression.Group(nested))) }
        assertFailure(nested)
        assertFailure(parse("2^{1000000000000000000}"))
        assertFailure(parse("1.1^{1000000000000000000}"))
        assertFailure(parse("(1/2)^{1000000000000000000}"))
        assertFailure(parse("(11/10)^{20000.5}"))
        assertResult("2^{-2}", 0.25)
        assertResult("2^{3^2}", 512.0)
    }

    @Test fun independentRequestsDoNotCarryFailuresOrAngleState() {
        assertFailure(parse("5nCr6"))
        assertResult("\\arcsin{0.5}", 30.0, CalculatorAngleMode.DEG)
        assertResult("\\arcsin{0.5}", PI / 6)
        assertFailure(parse("\\sqrt[2]{-8}"))
        assertResult("20!", 2432902008176640000.0)
        assertResult("50\\%", 0.5)
    }

    @Test fun simultaneousRequestsKeepIndependentExactAndAngleResults() {
        val workers = Executors.newFixedThreadPool(2)
        try {
            val factorial = workers.submit(Callable { success(calculator.evaluate(parse("20!"))).latex })
            val angle = workers.submit(Callable {
                scalar(success(calculator.evaluate(parse("\\arcsin{0.5}"), CalculatorAngleMode.DEG)).expression)
            })
            assertEquals("2432902008176640000", factorial.get(10, TimeUnit.SECONDS))
            assertEquals(30.0, angle.get(10, TimeUnit.SECONDS), 1e-10)
        } finally {
            workers.shutdownNow()
        }
    }

    @Test fun anInterruptedRequestReturnsAnErrorAndDoesNotPoisonTheNextRequest() {
        Thread.currentThread().interrupt()
        try {
            assertFailure(parse("20!"))
        } finally {
            Thread.interrupted()
        }
        assertResult("5nCr2", 10.0)
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
        for ((source, shouldSucceed) in listOf("\\frac{12}{4}" to true, "\\frac{12}{0}" to false,
            "\\frac{20!}{2}" to true, "\\frac{1.5!}{2}" to false)) {
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
