package com.learngraphicsprogramming.app.calculator

import com.learngraphicsprogramming.app.R
import com.learngraphicsprogramming.app.math.MathUiMessage
import com.learngraphicsprogramming.app.math.MathUiException
import com.learngraphicsprogramming.app.math.MathExpression
import com.learngraphicsprogramming.app.math.MathLatexParser

sealed interface CalculatorResult {
    data class Success(val expression: MathExpression.Sequence, val latex: String) : CalculatorResult
    data class Failure(val message: String, val uiMessage: MathUiMessage? = null) : CalculatorResult {
        constructor(message: MathUiMessage) : this(message.diagnostic, message)
        fun resolveMessage(context: android.content.Context): String = uiMessage?.resolve(context) ?: message
    }
}

/** Editor input is immutable. Failed calculations never replace or restore the user's draft. */
class MathCalculator(private val service: MathToolService = MathToolService()) {
    fun evaluate(input: MathExpression.Sequence, angleMode: CalculatorAngleMode = CalculatorAngleMode.RAD): CalculatorResult {
        return try {
            val expression = CalculatorExpressionCompiler(angleMode).compile(input)
            val latex = service.evaluate(expression)
            when (val parsed = MathLatexParser.parse(latex)) {
                is MathLatexParser.Result.Success -> CalculatorResult.Success(parsed.expression, latex)
                is MathLatexParser.Result.Failure -> CalculatorResult.Failure(MathUiMessage(R.string.math_ui_unrepresentable_result))
            }
        } catch (failure: IllegalArgumentException) {
            CalculatorResult.Failure((failure as? MathUiException)?.uiMessage ?: MathUiMessage(R.string.math_ui_check_expression))
        } catch (failure: IllegalStateException) {
            CalculatorResult.Failure((failure as? MathUiException)?.uiMessage ?: MathUiMessage(R.string.math_ui_calculation_error))
        } catch (_: Exception) {
            CalculatorResult.Failure(MathUiMessage(R.string.math_ui_calculation_check))
        }
    }
}
