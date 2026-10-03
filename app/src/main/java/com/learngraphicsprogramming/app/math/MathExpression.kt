package com.learngraphicsprogramming.app.math

/** UI-independent mathematical tree. An empty sequence is an editable structural slot. */
sealed interface MathExpression {
    data class Sequence(val children: List<MathExpression> = emptyList()) : MathExpression
    data class Number(val value: String) : MathExpression
    data class Variable(val name: String) : MathExpression
    data class Symbol(val value: String) : MathExpression
    data class Operator(val value: String) : MathExpression
    data class Relation(val value: String) : MathExpression
    data class Fraction(val numerator: Sequence, val denominator: Sequence) : MathExpression
    data class Scripts(val base: Sequence, val upper: Sequence? = null, val lower: Sequence? = null) : MathExpression
    data class Root(val radicand: Sequence, val index: Sequence? = null) : MathExpression
    data class Group(val content: Sequence, val delimiter: MathDelimiter = MathDelimiter.PARENTHESES) : MathExpression
    data class Function(val name: String, val argument: Sequence, val base: Sequence? = null) : MathExpression
    data class LargeOperator(val kind: MathLargeOperator, val body: Sequence, val lower: Sequence? = null, val upper: Sequence? = null) : MathExpression
    data class Limit(val body: Sequence, val condition: Sequence) : MathExpression
    data class Derivative(val body: Sequence, val variable: Sequence, val order: Sequence? = null) : MathExpression
    data class Matrix(val rows: List<List<Sequence>>) : MathExpression {
        init { require(rows.isNotEmpty() && rows.first().isNotEmpty() && rows.all { it.size == rows.first().size }) { "A matrix must have a rectangular non-empty shape" } }
    }
    data class Case(val expression: Sequence, val condition: Sequence)
    data class Cases(val rows: List<Case>) : MathExpression {
        init { require(rows.isNotEmpty()) { "Cases must contain at least one row" } }
    }
}

enum class MathDelimiter { PARENTHESES, BRACKETS, BRACES, ABSOLUTE }
enum class MathLargeOperator { INTEGRAL, SUM, PRODUCT }
data class MathCursorStep(val nodeIndex: Int, val slot: String)
data class MathCursor(val path: List<MathCursorStep> = emptyList(), val offset: Int = 0)

/** Shared structural slot contract for editing, rendering, hit testing and future node types. */
fun MathExpression.childSlots(): List<Pair<String, MathExpression.Sequence>> = when (this) {
    is MathExpression.Fraction -> listOf("numerator" to numerator, "denominator" to denominator)
    is MathExpression.Scripts -> listOfNotNull("base" to base, upper?.let { "upper" to it }, lower?.let { "lower" to it })
    is MathExpression.Root -> listOfNotNull(index?.let { "index" to it }, "radicand" to radicand)
    is MathExpression.Group -> listOf("content" to content)
    is MathExpression.Function -> listOfNotNull(base?.let { "base" to it }, "argument" to argument)
    is MathExpression.LargeOperator -> listOfNotNull(lower?.let { "lower" to it }, upper?.let { "upper" to it }, "body" to body)
    is MathExpression.Limit -> listOf("condition" to condition, "body" to body)
    is MathExpression.Derivative -> listOfNotNull(order?.let { "order" to it }, "body" to body, "variable" to variable)
    is MathExpression.Matrix -> rows.flatMapIndexed { r, row -> row.mapIndexed { c, cell -> "r${r}c$c" to cell } }
    is MathExpression.Cases -> rows.flatMapIndexed { index, row -> listOf("expression$index" to row.expression, "condition$index" to row.condition) }
    else -> emptyList()
}

fun MathExpression.withChildSlot(slot: String, value: MathExpression.Sequence): MathExpression = when (this) {
    is MathExpression.Fraction -> when (slot) { "numerator" -> copy(numerator = value); "denominator" -> copy(denominator = value); else -> this }
    is MathExpression.Scripts -> when (slot) { "base" -> copy(base = value); "upper" -> copy(upper = value); "lower" -> copy(lower = value); else -> this }
    is MathExpression.Root -> when (slot) { "radicand" -> copy(radicand = value); "index" -> copy(index = value); else -> this }
    is MathExpression.Group -> if (slot == "content") copy(content = value) else this
    is MathExpression.Function -> when (slot) { "argument" -> copy(argument = value); "base" -> copy(base = value); else -> this }
    is MathExpression.LargeOperator -> when (slot) { "body" -> copy(body = value); "lower" -> copy(lower = value); "upper" -> copy(upper = value); else -> this }
    is MathExpression.Limit -> when (slot) { "body" -> copy(body = value); "condition" -> copy(condition = value); else -> this }
    is MathExpression.Derivative -> when (slot) { "body" -> copy(body = value); "variable" -> copy(variable = value); "order" -> copy(order = value); else -> this }
    is MathExpression.Matrix -> copy(rows = rows.mapIndexed { r, row -> row.mapIndexed { c, cell -> if (slot == "r${r}c$c") value else cell } })
    is MathExpression.Cases -> copy(rows = rows.mapIndexed { index, row -> when (slot) { "expression$index" -> row.copy(expression = value); "condition$index" -> row.copy(condition = value); else -> row } })
    else -> this
}

fun MathExpression.isComplete(): Boolean = when (this) {
    is MathExpression.Sequence -> children.isNotEmpty() && children.all { it.isComplete() }
    is MathExpression.Number -> value.isNotBlank()
    is MathExpression.Variable -> name.isNotBlank()
    is MathExpression.Symbol -> value.isNotBlank()
    is MathExpression.Operator -> value.isNotBlank()
    is MathExpression.Relation -> value.isNotBlank()
    else -> childSlots().all { it.second.isComplete() }
}
