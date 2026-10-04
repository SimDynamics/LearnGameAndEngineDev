package com.learngraphicsprogramming.app.math

/** A keyboard template describes shape only: expected answer values never become icon content. */
fun MathKeyboardAction.previewExpression(
    expectedTemplate: MathExpression.Sequence? = null
): MathExpression.Sequence? {
    val empty = MathExpression.Sequence()
    fun expected(predicate: (MathExpression) -> Boolean): MathExpression? {
        fun find(node: MathExpression): MathExpression? {
            if (predicate(node)) return node
            val children = if (node is MathExpression.Sequence) node.children else node.childSlots().map { it.second }
            return children.firstNotNullOfOrNull(::find)
        }
        return expectedTemplate?.let(::find)
    }
    fun operator(kind: MathLargeOperator): MathExpression.LargeOperator {
        val template = expected { it is MathExpression.LargeOperator && it.kind == kind } as? MathExpression.LargeOperator
        return MathExpression.LargeOperator(kind, empty,
            lower = if (template == null || template.lower != null) empty else null,
            upper = if (template == null || template.upper != null) empty else null)
    }
    val node = when (this) {
        MathKeyboardAction.Fraction -> MathExpression.Fraction(empty, empty)
        MathKeyboardAction.Parentheses -> MathExpression.Group(empty)
        MathKeyboardAction.Power -> MathExpression.Scripts(empty, upper = empty)
        MathKeyboardAction.Subscript -> MathExpression.Scripts(empty, lower = empty)
        MathKeyboardAction.Root -> MathExpression.Root(empty)
        MathKeyboardAction.NthRoot -> MathExpression.Root(empty, index = empty)
        MathKeyboardAction.AbsoluteValue -> MathExpression.Group(empty, MathDelimiter.ABSOLUTE)
        MathKeyboardAction.Brackets -> MathExpression.Group(empty, MathDelimiter.BRACKETS)
        MathKeyboardAction.Braces -> MathExpression.Group(empty, MathDelimiter.BRACES)
        MathKeyboardAction.Integral -> operator(MathLargeOperator.INTEGRAL)
        MathKeyboardAction.Sum -> operator(MathLargeOperator.SUM)
        MathKeyboardAction.Product -> operator(MathLargeOperator.PRODUCT)
        MathKeyboardAction.Limit -> MathExpression.Limit(empty, empty)
        MathKeyboardAction.Matrix -> {
            val template = expected { it is MathExpression.Matrix } as? MathExpression.Matrix
            MathExpression.Matrix(List(template?.rows?.size ?: 2) { List(template?.rows?.first()?.size ?: 2) { empty } })
        }
        MathKeyboardAction.Cases -> {
            val template = expected { it is MathExpression.Cases } as? MathExpression.Cases
            MathExpression.Cases(List(template?.rows?.size ?: 2) { MathExpression.Case(empty, empty) })
        }
        MathKeyboardAction.Derivative -> {
            val template = expected { it is MathExpression.Derivative } as? MathExpression.Derivative
            MathExpression.Derivative(empty, empty, order = if (template?.order != null) empty else null)
        }
        is MathKeyboardAction.Function -> {
            val template = expected { it is MathExpression.Function && it.name == name } as? MathExpression.Function
            val base = if (template?.base != null || template == null && name == "log") empty else null
            MathExpression.Function(name, empty, base)
        }
        else -> return null
    }
    return MathExpression.Sequence(listOf(node))
}
