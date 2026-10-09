package com.learngraphicsprogramming.app.math

import com.learngraphicsprogramming.app.R
/** Bridges notation to the existing bounded polynomial validator, without expanding its domain. */
object MathExpressionSemantics {
    fun polynomialSource(source: String): String {
        if ('\\' !in source && '_' !in source && '{' !in source) return source
        val parsed = MathLatexParser.parse(source)
        requireMath(parsed is MathLatexParser.Result.Success) { MathUiMessage(R.string.math_ui_invalid_latex) }
        requireMath(parsed.expression.isComplete()) { MathUiMessage(R.string.math_ui_complete_math_expression) }
        return polynomial(parsed.expression)
            ?: throw MathUiException(MathUiMessage(R.string.math_ui_specific_validator))
    }

    private fun polynomial(expression: MathExpression): String? = when (expression) {
        is MathExpression.Sequence -> {
            val children = expression.children.map { polynomial(it) }
            if (children.any { it == null }) null else children.joinToString("")
        }
        is MathExpression.Number -> expression.value
        is MathExpression.Variable -> expression.name.takeIf { it.matches(Regex("[a-zA-Z]+")) }
        is MathExpression.Symbol -> if (expression.value == "π") "π" else null
        is MathExpression.Operator -> when (expression.value) { "×", "·" -> "*"; "÷" -> "/"; "−" -> "-"; else -> expression.value }
        is MathExpression.Relation -> expression.value.takeIf { it == "=" }
        is MathExpression.Fraction -> {
            val numerator = polynomial(expression.numerator)
            val denominator = polynomial(expression.denominator)
            if (numerator == null || denominator == null) null else "($numerator)/($denominator)"
        }
        is MathExpression.Scripts -> {
            val base = polynomial(expression.base)
            val power = expression.upper?.let(::polynomial)
            if (expression.lower != null || base == null || power == null || !power.matches(Regex("[0-8]"))) null
            else "($base)^$power"
        }
        is MathExpression.Group -> if (expression.delimiter == MathDelimiter.ABSOLUTE) null else polynomial(expression.content)?.let { "($it)" }
        else -> null
    }

    /** Canonical AST notation comparison for QA/structural answers; not symbolic calculus. */
    fun canonical(source: String): String {
        val parsed = MathLatexParser.parse(source)
        requireMath(parsed is MathLatexParser.Result.Success) { MathUiMessage(R.string.math_ui_invalid_latex) }
        requireMath(parsed.expression.isComplete()) { MathUiMessage(R.string.math_ui_complete_math_expression) }
        return MathLatexSerializer.serialize(parsed.expression)
    }
}
