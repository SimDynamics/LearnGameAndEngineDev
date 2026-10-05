package com.learngraphicsprogramming.app.calculator

import com.learngraphicsprogramming.app.math.MathExpression
import com.learngraphicsprogramming.app.math.MathLatexParser

sealed interface CalculatorResult {
    data class Success(val expression: MathExpression.Sequence, val latex: String) : CalculatorResult
    data class Failure(val message: String) : CalculatorResult
}

/** Editor input is immutable. Failed calculations never replace or restore the user's draft. */
class MathCalculator(private val service: MathToolService = MathToolService()) {
    fun evaluate(input: MathExpression.Sequence, angleMode: CalculatorAngleMode = CalculatorAngleMode.RAD): CalculatorResult {
        return try {
            val expression = CalculatorExpressionCompiler(angleMode).compile(input)
            val latex = service.evaluate(expression)
            when (val parsed = MathLatexParser.parse(latex)) {
                is MathLatexParser.Result.Success -> CalculatorResult.Success(parsed.expression, latex)
                is MathLatexParser.Result.Failure -> CalculatorResult.Failure("El resultado no se puede representar todavía.")
            }
        } catch (failure: IllegalArgumentException) {
            CalculatorResult.Failure(failure.message ?: "Revisa la expresión.")
        } catch (failure: IllegalStateException) {
            CalculatorResult.Failure(failure.message ?: "No se pudo calcular la expresión.")
        } catch (_: Exception) {
            CalculatorResult.Failure("No se pudo calcular la expresión. Revisa sus valores y operaciones.")
        }
    }
}
