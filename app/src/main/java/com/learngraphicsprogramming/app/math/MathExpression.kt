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
    data class LargeOperator(
        val kind: MathLargeOperator,
        val lower: Sequence? = null,
        val upper: Sequence? = null
    ) : MathExpression
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
enum class MathCursorLevel { NEUTRAL, CHILD_BOUNDARY, INTERIOR }
sealed interface MathContentScope {
    data object Slot : MathContentScope
    data class Atom(val start: Int, val end: Int) : MathContentScope
}
data class MathCursor(
    val path: List<MathCursorStep> = emptyList(), val offset: Int = 0,
    val level: MathCursorLevel = if (path.isEmpty()) MathCursorLevel.NEUTRAL else MathCursorLevel.INTERIOR,
    val scope: MathContentScope? = if (level == MathCursorLevel.INTERIOR && path.isNotEmpty()) MathContentScope.Slot else null
)
sealed interface MathCursorPresentation {
    data class Interior(val scope: MathContentScope? = null) : MathCursorPresentation
    data object Neutral : MathCursorPresentation
    data class ParentBoundary(val path: List<MathCursorStep>, val nodeIndex: Int) : MathCursorPresentation
    /** An exterior child cursor always means immediately after that child. */
    data class ChildBoundary(val slotPath: List<MathCursorStep>, val after: Boolean = true) : MathCursorPresentation {
        init { require(after) { "A child boundary exists only to its right" } }
    }
}
enum class MathSlotRequirement { REQUIRED, OPTIONAL }
enum class MathNavigationRole { PARENT_CONTENT, CHILD }
data class MathNavigationChild(
    val id: String, val content: MathExpression.Sequence, val requirement: MathSlotRequirement,
    val update: (MathExpression.Sequence) -> MathExpression,
    val remove: () -> List<MathExpression>, val role: MathNavigationRole = MathNavigationRole.CHILD
)

/** Shared structural slots. Children declare upper left-to-right, then lower left-to-right order. */
fun MathExpression.navigationChildren(): List<MathNavigationChild> {

    fun required(
        id: String,
        content: MathExpression.Sequence,
        role: MathNavigationRole = MathNavigationRole.CHILD,
        update: (MathExpression.Sequence) -> MathExpression
    ) =
        MathNavigationChild(
            id,
            content,
            MathSlotRequirement.REQUIRED,
            update,
            {
                listOf(
                    update(
                        MathExpression.Sequence()
                    )
                )
            },
            role
        )

    fun optional(
        id: String,
        content: MathExpression.Sequence?,
        update: (MathExpression.Sequence) -> MathExpression,
        remove: () -> List<MathExpression>
    ) =
        content?.let {
            MathNavigationChild(
                id,
                it,
                MathSlotRequirement.OPTIONAL,
                update,
                remove
            )
        }

    fun scriptReplacement(
        value: MathExpression.Scripts
    ): List<MathExpression> =
        if (
            value.lower == null &&
            value.upper == null
        ) {
            value.base.children
        } else {
            listOf(value)
        }

    /*
     * ORDEN CANÓNICO:
     *
     * 1. Arriba -> abajo.
     * 2. A la misma altura:
     *    izquierda -> derecha.
     *
     * Este orden es lógico y declarativo.
     * El renderer NO decide la navegación.
     */
    return when (this) {

        is MathExpression.Fraction ->
            listOf(
                required(
                    "numerator",
                    numerator
                ) {
                    copy(
                        numerator = it
                    )
                },

                required(
                    "denominator",
                    denominator
                ) {
                    copy(
                        denominator = it
                    )
                }
            )

        is MathExpression.Scripts ->
            listOfNotNull(

                /*
                 *      upper
                 * base
                 *      lower
                 */
                optional(
                    "upper",
                    upper,
                    {
                        copy(
                            upper = it
                        )
                    }
                ) {
                    scriptReplacement(
                        copy(
                            upper = null
                        )
                    )
                },

                required(
                    "base",
                    base,
                    MathNavigationRole.PARENT_CONTENT
                ) {
                    copy(
                        base = it
                    )
                },

                optional(
                    "lower",
                    lower,
                    {
                        copy(
                            lower = it
                        )
                    }
                ) {
                    scriptReplacement(
                        copy(
                            lower = null
                        )
                    )
                }
            )

        is MathExpression.Root ->
            listOfNotNull(

                /*
                 * index está arriba a la izquierda
                 * del radicando.
                 */
                optional(
                    "index",
                    index,
                    {
                        copy(
                            index = it
                        )
                    }
                ) {
                    listOf(
                        copy(
                            index = null
                        )
                    )
                },

                required(
                    "radicand",
                    radicand,
                    MathNavigationRole.PARENT_CONTENT
                ) {
                    copy(
                        radicand = it
                    )
                }
            )

        is MathExpression.Group ->
            listOf(
                required(
                    "content",
                    content,
                    MathNavigationRole.PARENT_CONTENT
                ) {
                    copy(
                        content = it
                    )
                }
            )

        is MathExpression.Function ->
            listOfNotNull(

                /*
                 * El argumento está en el nivel principal.
                 * La base, cuando existe, queda debajo.
                 */
                required(
                    "argument",
                    argument,
                    MathNavigationRole.PARENT_CONTENT
                ) {
                    copy(
                        argument = it
                    )
                },

                optional(
                    "base",
                    base,
                    {
                        copy(
                            base = it
                        )
                    }
                ) {
                    listOf(
                        copy(
                            base = null
                        )
                    )
                }
            )

        is MathExpression.LargeOperator ->
            listOfNotNull(
                optional(
                    "upper",
                    upper,
                    {
                        copy(
                            upper = it
                        )
                    }
                ) {
                    listOf(
                        copy(
                            upper = null
                        )
                    )
                },

                optional(
                    "lower",
                    lower,
                    {
                        copy(
                            lower = it
                        )
                    }
                ) {
                    listOf(
                        copy(
                            lower = null
                        )
                    )
                }
            )

        is MathExpression.Limit ->
            listOf(

                /*
                 * body está al nivel principal.
                 * condition aparece debajo de lim.
                 */
                required(
                    "body",
                    body,
                    MathNavigationRole.PARENT_CONTENT
                ) {
                    copy(
                        body = it
                    )
                },

                required(
                    "condition",
                    condition
                ) {
                    copy(
                        condition = it
                    )
                }
            )

        is MathExpression.Derivative ->
            listOfNotNull(

                optional(
                    "order",
                    order,
                    {
                        copy(
                            order = it
                        )
                    }
                ) {
                    listOf(
                        copy(
                            order = null
                        )
                    )
                },

                required(
                    "body",
                    body,
                    MathNavigationRole.PARENT_CONTENT
                ) {
                    copy(
                        body = it
                    )
                },

                required(
                    "variable",
                    variable
                ) {
                    copy(
                        variable = it
                    )
                }
            )

        is MathExpression.Matrix ->
            rows.flatMapIndexed { r, row ->

                row.mapIndexed { c, cell ->

                    required(
                        "r${r}c$c",
                        cell
                    ) { value ->

                        copy(
                            rows =
                                rows.mapIndexed { rr, cells ->

                                    cells.mapIndexed { cc, old ->

                                        if (
                                            rr == r &&
                                            cc == c
                                        ) {
                                            value
                                        } else {
                                            old
                                        }
                                    }
                                }
                        )
                    }
                }
            }

        is MathExpression.Cases ->
            rows.flatMapIndexed { index, row ->

                listOf(

                    required(
                        "expression$index",
                        row.expression
                    ) { value ->

                        copy(
                            rows =
                                rows.mapIndexed { i, old ->

                                    if (i == index) {
                                        old.copy(
                                            expression = value
                                        )
                                    } else {
                                        old
                                    }
                                }
                        )
                    },

                    required(
                        "condition$index",
                        row.condition
                    ) { value ->

                        copy(
                            rows =
                                rows.mapIndexed { i, old ->

                                    if (i == index) {
                                        old.copy(
                                            condition = value
                                        )
                                    } else {
                                        old
                                    }
                                }
                        )
                    }
                )
            }

        else ->
            emptyList()
    }
}

fun MathExpression.childSlots(): List<Pair<String, MathExpression.Sequence>> = navigationChildren().map { it.id to it.content }
fun MathExpression.withChildSlot(slot: String, value: MathExpression.Sequence): MathExpression =
    navigationChildren().firstOrNull { it.id == slot }?.update?.invoke(value) ?: this

fun MathExpression.isComplete(): Boolean = when (this) {
    is MathExpression.Sequence -> children.isNotEmpty() && children.all { it.isComplete() }
    is MathExpression.Number -> value.isNotBlank()
    is MathExpression.Variable -> name.isNotBlank()
    is MathExpression.Symbol -> value.isNotBlank()
    is MathExpression.Operator -> value.isNotBlank()
    is MathExpression.Relation -> value.isNotBlank()
    else -> childSlots().all { it.second.isComplete() }
}
