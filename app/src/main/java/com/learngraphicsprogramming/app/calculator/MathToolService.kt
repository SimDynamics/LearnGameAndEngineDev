package com.learngraphicsprogramming.app.calculator

import com.learngraphicsprogramming.app.R
import com.learngraphicsprogramming.app.math.MathUiMessage
import com.learngraphicsprogramming.app.math.requireMath
import org.matheclipse.core.basic.Config
import org.matheclipse.core.eval.EvalEngine
import org.matheclipse.core.eval.ExprEvaluator
import org.matheclipse.core.expression.F
import org.matheclipse.core.expression.S
import org.matheclipse.core.interfaces.IExpr
import org.matheclipse.core.interfaces.IInteger
import org.matheclipse.core.interfaces.IRational
import java.math.BigDecimal
import java.math.BigInteger
import java.math.MathContext
import java.math.RoundingMode

/** Only the controlled arithmetic language reaches Symja. Each request has its own engine state. */
class MathToolService {
    internal fun evaluate(expression: ScalarExpression): String = withResult(expression) { exact, numeric ->
        // Do not round an integer through Double, particularly factorials and combinations.
        if (exact is IInteger) exact.toBigNumerator().toString() else numericLatex(numeric)
    }

    /** Numeric consumers can bypass result LaTeX and the display parser altogether. */
    internal fun evaluateNumeric(expression: ScalarExpression): Double = withResult(expression) { _, numeric -> numeric }

    private fun <T> withResult(expression: ScalarExpression, consume: (IExpr, Double) -> T): T {
        // The calculator has no desktop, generated-code, filesystem or command input surface.
        Config.JAVA_AWT_DESKTOP_AVAILABLE = false
        Config.JAVA_UNSAFE = false
        val evaluator = ExprEvaluator(true, 0.toShort())
        val engine = evaluator.evalEngine
        engine.setFileSystemEnabled(false)
        engine.setIterationLimit(4000)
        engine.setRecursionLimit(256)
        engine.setOutputSizeLimit(4096)
        try {
            val request = EngineRequest(evaluator)
            val input = toSymja(expression, 0, request)
            val exact = request.evaluate(input)
            val result = request.evaluate(F.N(exact))
            val numeric = runCatching { result.evalDouble() }.getOrNull()
            requireMath(numeric != null && numeric.isFinite()) {
                MathUiMessage(R.string.math_ui_finite_result)
            }
            return consume(exact, numeric)
        } finally {
            engine.setTimeConstrainedMillis(-1)
            EvalEngine.remove()
        }
    }

    private fun numericLatex(value: Double): String {
        // Symja 3.2's machine-number TeX formatter can emit 1.0E20, which the editor
        // correctly treats as a variable E. Encode a finite numeric value explicitly.
        val decimal = BigDecimal.valueOf(value).round(MathContext(15, RoundingMode.HALF_UP)).stripTrailingZeros()
        if (decimal.signum() == 0) return "0"
        val exponent = decimal.precision() - decimal.scale() - 1
        return if (exponent >= 12 || exponent <= -8) {
            "${decimal.movePointLeft(exponent).toPlainString()}\\cdot{}10^{$exponent}"
        } else decimal.toPlainString()
    }

    /** Domain checks and the final calculation share one three-second request budget. */
    private class EngineRequest(private val evaluator: ExprEvaluator) {
        private val deadline = System.currentTimeMillis() + 3000L
        fun evaluate(expression: IExpr): IExpr {
            checkBudget()
            // A nested TimeConstrained call must not extend the deadline of earlier checks.
            // Symja leaves its seconds flag set after a call; clear it so the next call
            // still installs a timeout, then pass the request's remaining seconds.
            evaluator.evalEngine.setSeconds(0)
            evaluator.evalEngine.setTimeConstrainedMillis(deadline)
            val remainingSeconds = (deadline - System.currentTimeMillis()) / 1000.0
            val result = evaluator.eval(F.TimeConstrained(expression, F.num(remainingSeconds)))
            checkBudget()
            requireMath(result != S.`$Aborted`) { MathUiMessage(R.string.math_ui_calculation_timeout) }
            return result
        }
        private fun checkBudget() {
            requireMath(!Thread.currentThread().isInterrupted && System.currentTimeMillis() < deadline) {
                MathUiMessage(R.string.math_ui_calculation_timeout)
            }
        }
    }

    private fun toSymja(expression: ScalarExpression, depth: Int, request: EngineRequest): IExpr {
        requireMath(depth <= 128) { MathUiMessage(R.string.math_ui_calculation_size) }
        fun nested(value: ScalarExpression) = toSymja(value, depth + 1, request)
        return when (expression) {
            is ScalarExpression.Number -> expression.value.stripTrailingZeros().let { value ->
                if (value.scale() <= 0) F.ZZ(value.toBigIntegerExact())
                else F.QQ(value.unscaledValue(), BigInteger.TEN.pow(value.scale()))
            }
            ScalarExpression.Constant.PI -> S.Pi
            ScalarExpression.Constant.E -> S.E
            is ScalarExpression.Binary -> {
                val left = nested(expression.left)
                val right = nested(expression.right)
                when (expression.operation) {
                    ScalarExpression.Operation.ADD -> F.Plus(left, right)
                    ScalarExpression.Operation.SUBTRACT -> F.Subtract(left, right)
                    ScalarExpression.Operation.MULTIPLY -> F.Times(left, right)
                    ScalarExpression.Operation.DIVIDE -> F.Divide(left, right)
                    ScalarExpression.Operation.POWER -> boundedPower(left, right, request)
                    ScalarExpression.Operation.COMBINATION, ScalarExpression.Operation.PERMUTATION -> {
                        val n = boundedInteger(left, request, "n")
                        val r = boundedInteger(right, request, "r")
                        requireMath(r <= n) { MathUiMessage(R.string.math_ui_combinatorial_domain) }
                        if (expression.operation == ScalarExpression.Operation.COMBINATION) F.Binomial(n, r)
                        else F.Divide(F.Factorial(n), F.Factorial(n - r))
                    }
                }
            }
            is ScalarExpression.Function -> nested(expression.argument).let {
                when (expression.function) {
                    ScalarExpression.FunctionKind.SIN -> F.Sin(it)
                    ScalarExpression.FunctionKind.COS -> F.Cos(it)
                    ScalarExpression.FunctionKind.TAN -> F.Tan(it)
                    ScalarExpression.FunctionKind.ARCSIN -> F.ArcSin(inverseArgument(it, request))
                    ScalarExpression.FunctionKind.ARCCOS -> F.ArcCos(inverseArgument(it, request))
                    ScalarExpression.FunctionKind.ARCTAN -> F.ArcTan(it)
                    ScalarExpression.FunctionKind.LOG -> F.Log(it)
                    ScalarExpression.FunctionKind.ABS -> F.Abs(it)
                    ScalarExpression.FunctionKind.FACTORIAL -> F.Factorial(boundedInteger(it, request, MathUiMessage(R.string.math_ui_factorial)))
                }
            }
            is ScalarExpression.Root -> {
                val index = boundedInteger(nested(expression.index), request, MathUiMessage(R.string.math_ui_root_index))
                requireMath(index > 0) { MathUiMessage(R.string.math_ui_root_positive_index) }
                val radicand = request.evaluate(nested(expression.radicand))
                val numeric = runCatching { radicand.evalDouble() }.getOrNull()
                requireMath(numeric != null && numeric.isFinite()) { MathUiMessage(R.string.math_ui_root_finite) }
                if (radicand.isNegative || numeric < 0) {
                    requireMath(index % 2 == 1) { MathUiMessage(R.string.math_ui_root_even_negative) }
                    F.Times(F.CN1, F.Power(F.Abs(radicand), F.QQ(1, index.toLong())))
                } else F.Power(radicand, F.QQ(1, index.toLong()))
            }
        }
    }

    private fun boundedInteger(value: IExpr, request: EngineRequest, label: Any): Int {
        val exact = request.evaluate(value)
        requireMath(exact is IInteger) { MathUiMessage(R.string.math_ui_nonnegative_integer, listOf(label)) }
        val integer = exact.toBigNumerator()
        requireMath(integer.signum() >= 0 && integer <= BigInteger.valueOf(1000)) {
            MathUiMessage(R.string.math_ui_bounded_integer, listOf(label))
        }
        return integer.toInt()
    }

    private fun boundedPower(base: IExpr, exponent: IExpr, request: EngineRequest): IExpr {
        val exactExponent = request.evaluate(exponent)
        val numericExponent = runCatching { exactExponent.evalDouble() }.getOrNull()
        requireMath(numericExponent != null && numericExponent.isFinite()) { MathUiMessage(R.string.math_ui_power_finite) }
        requireMath(kotlin.math.abs(numericExponent) <= 10000) { MathUiMessage(R.string.math_ui_power_range) }
        val exactBase = request.evaluate(base)
        // Limit intermediate integer allocation before Symja raises an exact integer to a huge power.
        if (exactBase is IRational) {
            val numerator = exactBase.toBigNumerator().abs()
            val denominator = exactBase.toBigDenominator()
            if (numerator > BigInteger.ONE || denominator > BigInteger.ONE) {
                val bits = maxOf(numerator.bitLength(), denominator.bitLength())
                val estimatedBits = bits.toLong() * kotlin.math.ceil(kotlin.math.abs(numericExponent)).toLong()
                requireMath(estimatedBits <= 16384) { MathUiMessage(R.string.math_ui_power_size) }
            }
        }
        return F.Power(exactBase, exactExponent)
    }

    private fun inverseArgument(argument: IExpr, request: EngineRequest): IExpr {
        val exact = request.evaluate(argument)
        val numeric = runCatching { exact.evalDouble() }.getOrNull()
        val inDomain = if (exact is IRational) exact.toBigNumerator().abs() <= exact.toBigDenominator()
        else numeric != null && numeric.isFinite() && numeric in -1.0..1.0
        requireMath(inDomain) { MathUiMessage(R.string.math_ui_inverse_domain) }
        return exact
    }
}
