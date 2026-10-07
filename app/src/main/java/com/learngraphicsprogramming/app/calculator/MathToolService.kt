package com.learngraphicsprogramming.app.calculator

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
            require(numeric != null && numeric.isFinite()) {
                "No hay un resultado real finito. Revisa divisiones, raíces y dominios de las funciones."
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
            require(result != S.`$Aborted`) { "El cálculo excedió el tiempo permitido." }
            return result
        }
        private fun checkBudget() {
            require(!Thread.currentThread().isInterrupted && System.currentTimeMillis() < deadline) {
                "El cálculo excedió el tiempo permitido."
            }
        }
    }

    private fun toSymja(expression: ScalarExpression, depth: Int, request: EngineRequest): IExpr {
        require(depth <= 128) { "La expresión es demasiado grande para calcularla." }
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
                        require(r <= n) { "nCr y nPr requieren 0 ≤ r ≤ n." }
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
                    ScalarExpression.FunctionKind.FACTORIAL -> F.Factorial(boundedInteger(it, request, "El factorial"))
                }
            }
            is ScalarExpression.Root -> {
                val index = boundedInteger(nested(expression.index), request, "El índice de la raíz")
                require(index > 0) { "El índice de la raíz debe ser un entero positivo." }
                val radicand = request.evaluate(nested(expression.radicand))
                val numeric = runCatching { radicand.evalDouble() }.getOrNull()
                require(numeric != null && numeric.isFinite()) { "La raíz requiere un radicando real finito." }
                if (radicand.isNegative || numeric < 0) {
                    require(index % 2 == 1) { "Una raíz de índice par no admite un radicando negativo." }
                    F.Times(F.CN1, F.Power(F.Abs(radicand), F.QQ(1, index.toLong())))
                } else F.Power(radicand, F.QQ(1, index.toLong()))
            }
        }
    }

    private fun boundedInteger(value: IExpr, request: EngineRequest, label: String): Int {
        val exact = request.evaluate(value)
        require(exact is IInteger) { "$label debe ser un entero no negativo; no se redondea automáticamente." }
        val integer = exact.toBigNumerator()
        require(integer.signum() >= 0 && integer <= BigInteger.valueOf(1000)) {
            "$label debe estar entre 0 y 1000."
        }
        return integer.toInt()
    }

    private fun boundedPower(base: IExpr, exponent: IExpr, request: EngineRequest): IExpr {
        val exactExponent = request.evaluate(exponent)
        val numericExponent = runCatching { exactExponent.evalDouble() }.getOrNull()
        require(numericExponent != null && numericExponent.isFinite()) { "La potencia requiere un exponente real finito." }
        require(kotlin.math.abs(numericExponent) <= 10000) { "El exponente supera el rango de cálculo permitido (−10000 a 10000)." }
        val exactBase = request.evaluate(base)
        // Limit intermediate integer allocation before Symja raises an exact integer to a huge power.
        if (exactBase is IRational) {
            val numerator = exactBase.toBigNumerator().abs()
            val denominator = exactBase.toBigDenominator()
            if (numerator > BigInteger.ONE || denominator > BigInteger.ONE) {
                val bits = maxOf(numerator.bitLength(), denominator.bitLength())
                val estimatedBits = bits.toLong() * kotlin.math.ceil(kotlin.math.abs(numericExponent)).toLong()
                require(estimatedBits <= 16384) { "La potencia supera el tamaño de cálculo permitido." }
            }
        }
        return F.Power(exactBase, exactExponent)
    }

    private fun inverseArgument(argument: IExpr, request: EngineRequest): IExpr {
        val exact = request.evaluate(argument)
        val numeric = runCatching { exact.evalDouble() }.getOrNull()
        val inDomain = if (exact is IRational) exact.toBigNumerator().abs() <= exact.toBigDenominator()
        else numeric != null && numeric.isFinite() && numeric in -1.0..1.0
        require(inDomain) { "arcsin y arccos requieren un argumento entre −1 y 1." }
        return exact
    }
}
