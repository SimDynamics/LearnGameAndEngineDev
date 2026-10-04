package com.learngraphicsprogramming.app.math

/** Canonical supported LaTeX; editable placeholders are empty braces, never answer characters. */
object MathLatexSerializer {
    fun serialize(expression: MathExpression): String = when (expression) {
        is MathExpression.Sequence -> expression.children.joinToString("") { serialize(it) }
        is MathExpression.Number -> expression.value
        is MathExpression.Variable -> expression.name
        is MathExpression.Symbol -> symbols[expression.value]?.let { "$it{}" } ?: "\\mathrm{${escape(expression.value)}}"
        is MathExpression.Operator -> when (expression.value) { "×", "·" -> "\\cdot{}"; "÷" -> "\\div{}"; "−" -> "-"; else -> expression.value }
        is MathExpression.Relation -> symbols[expression.value]?.let { "$it{}" } ?: expression.value
        is MathExpression.Fraction -> "\\frac${braces(expression.numerator)}${braces(expression.denominator)}"
        is MathExpression.Scripts -> "${braces(expression.base)}${expression.lower?.let { "_${braces(it)}" }.orEmpty()}${expression.upper?.let { "^${braces(it)}" }.orEmpty()}"
        is MathExpression.Root -> "\\sqrt${expression.index?.let { "[${serialize(it)}]" }.orEmpty()}${braces(expression.radicand)}"
        is MathExpression.Group -> {
            val delimiters = when (expression.delimiter) {
                MathDelimiter.PARENTHESES -> "(" to ")"
                MathDelimiter.BRACKETS -> "[" to "]"
                MathDelimiter.BRACES -> "\\{" to "\\}"
                MathDelimiter.ABSOLUTE -> "|" to "|"
            }
            "\\left${delimiters.first}${serialize(expression.content)}\\right${delimiters.second}"
        }
        is MathExpression.Function -> "${if (expression.name in functions) "\\${expression.name}" else "\\operatorname{${escape(expression.name)}}"}${expression.base?.let { "_${braces(it)}" }.orEmpty()}${braces(expression.argument)}"
        is MathExpression.LargeOperator ->
            "\\${expression.kind.name.lowercase().let {
                if (it == "integral") {
                    "int"
                } else if (it == "product") {
                    "prod"
                } else {
                    it
                }
            }}${expression.lower?.let {
                "_${braces(it)}"
            }.orEmpty()}${expression.upper?.let {
                "^${braces(it)}"
            }.orEmpty()}"
        is MathExpression.Limit -> "\\lim_${braces(expression.condition)}${braces(expression.body)}"
        is MathExpression.Derivative -> {
            val order = expression.order?.let { "^${braces(it)}" }.orEmpty()
            "\\frac{\\mathrm{d}$order${serialize(expression.body)}}{\\mathrm{d}${braces(expression.variable)}$order}"
        }
        is MathExpression.Matrix -> "\\begin{matrix}${expression.rows.joinToString("\\\\") { row -> row.joinToString("&") { serialize(it) } }}\\end{matrix}"
        is MathExpression.Cases -> "\\begin{cases}${expression.rows.joinToString("\\\\") { "${serialize(it.expression)}&${serialize(it.condition)}" }}\\end{cases}"
    }

    private fun braces(expression: MathExpression) = "{${serialize(expression)}}"
    private fun escape(value: String) = value.replace("\\", "\\backslash{}").replace("{", "\\{").replace("}", "\\}")
    internal val functions = setOf("sin", "cos", "tan", "sec", "csc", "cot", "arcsin", "arccos", "arctan", "sinh", "cosh", "tanh", "log", "ln", "exp")
    internal val symbols = mapOf("π" to "\\pi", "∞" to "\\infty", "→" to "\\to", "≤" to "\\le", "≥" to "\\ge", "≠" to "\\ne", "≈" to "\\approx", "∂" to "\\partial", "α" to "\\alpha", "β" to "\\beta", "γ" to "\\gamma", "θ" to "\\theta", "λ" to "\\lambda", "μ" to "\\mu", "σ" to "\\sigma", "Δ" to "\\Delta", "∪" to "\\cup", "∩" to "\\cap", "∈" to "\\in")
}
