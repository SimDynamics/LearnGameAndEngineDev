package com.learngraphicsprogramming.app.calculator

import org.matheclipse.core.basic.Config
import org.matheclipse.core.eval.EvalEngine
import org.matheclipse.core.eval.ExprEvaluator
import org.matheclipse.core.expression.F
import org.matheclipse.core.expression.S
import org.matheclipse.core.interfaces.IExpr
import java.math.BigDecimal
import java.math.MathContext
import java.math.RoundingMode

/** Only the controlled arithmetic language reaches Symja. Each request has its own engine state. */
class MathToolService {
    internal fun evaluate(expression: ScalarExpression): String = withResult(expression, ::numericLatex)

    /** Numeric consumers can bypass result LaTeX and the display parser altogether. */
    internal fun evaluateNumeric(expression: ScalarExpression): Double = withResult(expression) { it }

    private fun <T> withResult(expression: ScalarExpression, consume: (Double) -> T): T {
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
            val input = toSymja(expression, 0)
            val result = evaluator.eval(F.TimeConstrained(F.N(input), F.ZZ(3)))
            val numeric = runCatching { result.evalDouble() }.getOrNull()
            require(numeric != null && numeric.isFinite()) {
                "No hay un resultado real finito. Revisa divisiones, raíces y dominios de las funciones."
            }
            return consume(numeric)
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

    private fun toSymja(expression: ScalarExpression, depth: Int): IExpr {
        require(depth <= 128) { "La expresión es demasiado grande para calcularla." }
        fun nested(value: ScalarExpression) = toSymja(value, depth + 1)
        return when (expression) {
            is ScalarExpression.Number -> if (expression.value % 1.0 == 0.0 && kotlin.math.abs(expression.value) <= 9_007_199_254_740_991.0) {
                F.ZZ(expression.value.toLong())
            } else F.num(expression.value)
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
                    ScalarExpression.Operation.POWER -> F.Power(left, right)
                }
            }
            is ScalarExpression.Function -> nested(expression.argument).let {
                when (expression.function) {
                    ScalarExpression.FunctionKind.SIN -> F.Sin(it)
                    ScalarExpression.FunctionKind.COS -> F.Cos(it)
                    ScalarExpression.FunctionKind.TAN -> F.Tan(it)
                    ScalarExpression.FunctionKind.LOG -> F.Log(it)
                    ScalarExpression.FunctionKind.ABS -> F.Abs(it)
                }
            }
        }
    }
}
