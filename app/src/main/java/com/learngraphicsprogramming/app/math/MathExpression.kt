package com.learngraphicsprogramming.app.math

/** UI-independent mathematical tree. An empty sequence is an editable structural slot. */
sealed interface MathExpression {

    data class Sequence(
        val children: List<MathExpression> = emptyList()
    ) : MathExpression

    data class Number(
        val value: String
    ) : MathExpression

    data class Variable(
        val name: String
    ) : MathExpression

    data class Symbol(
        val value: String
    ) : MathExpression

    data class Operator(
        val value: String
    ) : MathExpression

    data class Relation(
        val value: String
    ) : MathExpression

    data class Fraction(
        val numerator: Sequence,
        val denominator: Sequence
    ) : MathExpression

    data class Scripts(
        val base: Sequence,
        val upper: Sequence? = null,
        val lower: Sequence? = null
    ) : MathExpression

    data class Root(
        val radicand: Sequence,
        val index: Sequence? = null
    ) : MathExpression

    data class Group(
        val content: Sequence,
        val delimiter: MathDelimiter = MathDelimiter.PARENTHESES
    ) : MathExpression

    data class Function(
        val name: String,
        val argument: Sequence,
        val base: Sequence? = null
    ) : MathExpression

    data class LargeOperator(
        val kind: MathLargeOperator,
        val lower: Sequence? = null,
        val upper: Sequence? = null
    ) : MathExpression

    data class Limit(
        val body: Sequence,
        val condition: Sequence
    ) : MathExpression

    data class Derivative(
        val body: Sequence,
        val variable: Sequence,
        val order: Sequence? = null
    ) : MathExpression

    data class Matrix(
        val rows: List<List<Sequence>>
    ) : MathExpression {

        init {
            require(
                rows.isNotEmpty() &&
                        rows.first().isNotEmpty() &&
                        rows.all {
                            it.size == rows.first().size
                        }
            ) {
                "A matrix must have a rectangular non-empty shape"
            }
        }
    }

    data class Case(
        val expression: Sequence,
        val condition: Sequence
    )

    data class Cases(
        val rows: List<Case>
    ) : MathExpression {

        init {
            require(
                rows.isNotEmpty()
            ) {
                "Cases must contain at least one row"
            }
        }
    }
}

enum class MathDelimiter {
    PARENTHESES,
    BRACKETS,
    BRACES,
    ABSOLUTE
}

enum class MathLargeOperator {
    INTEGRAL,
    SUM,
    PRODUCT
}

/**
 * A standalone large operator keeps its operand
 * as the next sibling in the containing Sequence.
 *
 * The operand is never added as a body child
 * of LargeOperator.
 */
internal fun MathExpression.Sequence.hasContinuationInsertion(): Boolean =
    children.lastOrNull() is MathExpression.LargeOperator

data class MathCursorStep(
    val nodeIndex: Int,
    val slot: String
)

enum class MathCursorLevel {
    NEUTRAL,
    CHILD_BOUNDARY,
    INTERIOR
}

sealed interface MathContentScope {

    data object Slot : MathContentScope

    data class Atom(
        val start: Int,
        val end: Int
    ) : MathContentScope
}

data class MathCursor(
    val path: List<MathCursorStep> = emptyList(),
    val offset: Int = 0,
    val level: MathCursorLevel =
        if (path.isEmpty()) {
            MathCursorLevel.NEUTRAL
        } else {
            MathCursorLevel.INTERIOR
        },
    val scope: MathContentScope? =
        if (
            level == MathCursorLevel.INTERIOR &&
            path.isNotEmpty()
        ) {
            MathContentScope.Slot
        } else {
            null
        }
)

sealed interface MathCursorPresentation {

    data class Interior(
        val scope: MathContentScope? = null
    ) : MathCursorPresentation

    data object Neutral : MathCursorPresentation

    /**
     * Borde derecho de una estructura matemática.
     */
    data class ParentBoundary(
        val path: List<MathCursorStep>,
        val nodeIndex: Int
    ) : MathCursorPresentation

    /**
     * Borde derecho de un hijo.
     *
     * No existe un borde izquierdo equivalente.
     */
    data class ChildBoundary(
        val slotPath: List<MathCursorStep>,
        val after: Boolean = true
    ) : MathCursorPresentation {

        init {
            require(after) {
                "A child boundary exists only to its right"
            }
        }
    }
}

enum class MathSlotRequirement {
    REQUIRED,
    OPTIONAL
}

/**
 * La estructura es el MathExpression que contiene estas partes.
 *
 * Una parte interna puede ser:
 *
 * PARENT:
 * región matemática principal editable.
 *
 * CHILD:
 * región matemática subordinada editable:
 * índice, exponente, subíndice, límite, base auxiliar, etc.
 *
 * La estructura en sí nunca se edita directamente.
 */
enum class MathNavigationRole {
    PARENT,
    CHILD
}

data class MathNavigationPart(
    val id: String,
    val content: MathExpression.Sequence,
    val requirement: MathSlotRequirement,
    val update: (MathExpression.Sequence) -> MathExpression,
    val remove: () -> List<MathExpression>,
    val role: MathNavigationRole
)

/**
 * Alias temporal para no romper código antiguo que todavía
 * use el nombre MathNavigationChild.
 *
 * El concepto nuevo correcto es MathNavigationPart.
 */
typealias MathNavigationChild = MathNavigationPart

/**
 * Partes navegables inmediatas de una estructura.
 *
 * Una estructura administra únicamente sus partes inmediatas.
 * Si una parte contiene otra estructura, esa estructura
 * administra su propio universo cuando la navegación entra en ella.
 *
 * ORDEN CANÓNICO:
 *
 * 1. Arriba -> abajo.
 * 2. A la misma altura: izquierda -> derecha.
 *
 * LEFT recorre este orden.
 * RIGHT lo recorre exactamente al revés.
 */
fun MathExpression.navigationParts(): List<MathNavigationPart> {

    fun required(
        id: String,
        content: MathExpression.Sequence,
        role: MathNavigationRole,
        update: (MathExpression.Sequence) -> MathExpression
    ): MathNavigationPart =
        MathNavigationPart(
            id = id,
            content = content,
            requirement = MathSlotRequirement.REQUIRED,
            update = update,
            remove = {
                listOf(
                    update(
                        MathExpression.Sequence()
                    )
                )
            },
            role = role
        )

    fun optional(
        id: String,
        content: MathExpression.Sequence?,
        role: MathNavigationRole,
        update: (MathExpression.Sequence) -> MathExpression,
        remove: () -> List<MathExpression>
    ): MathNavigationPart? =
        content?.let {
            MathNavigationPart(
                id = id,
                content = it,
                requirement = MathSlotRequirement.OPTIONAL,
                update = update,
                remove = remove,
                role = role
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

    return when (this) {

        /*
         *      numerator
         *     -----------
         *     denominator
         *
         * La fracción es la estructura.
         * El guion no se edita.
         *
         * Numerador y denominador son dos
         * expresiones principales.
         */
        is MathExpression.Fraction ->
            listOf(
                required(
                    id = "numerator",
                    content = numerator,
                    role = MathNavigationRole.PARENT
                ) {
                    copy(
                        numerator = it
                    )
                },

                required(
                    id = "denominator",
                    content = denominator,
                    role = MathNavigationRole.PARENT
                ) {
                    copy(
                        denominator = it
                    )
                }
            )

        /*
         *        upper
         * base
         *        lower
         *
         * base es la expresión principal.
         * upper y lower modifican a la base.
         */
        is MathExpression.Scripts ->
            listOfNotNull(
                optional(
                    id = "upper",
                    content = upper,
                    role = MathNavigationRole.CHILD,
                    update = {
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
                    id = "base",
                    content = base,
                    role = MathNavigationRole.PARENT
                ) {
                    copy(
                        base = it
                    )
                },

                optional(
                    id = "lower",
                    content = lower,
                    role = MathNavigationRole.CHILD,
                    update = {
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

        /*
         * index √ radicand
         *
         * radicand es la expresión principal.
         * index modifica a la raíz.
         */
        is MathExpression.Root ->
            listOfNotNull(
                optional(
                    id = "index",
                    content = index,
                    role = MathNavigationRole.CHILD,
                    update = {
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
                    id = "radicand",
                    content = radicand,
                    role = MathNavigationRole.PARENT
                ) {
                    copy(
                        radicand = it
                    )
                }
            )

        /*
         * Los delimitadores forman la estructura.
         * El contenido es la expresión principal.
         */
        is MathExpression.Group ->
            listOf(
                required(
                    id = "content",
                    content = content,
                    role = MathNavigationRole.PARENT
                ) {
                    copy(
                        content = it
                    )
                }
            )

        /*
         * argument es la expresión principal.
         * base, cuando existe, es auxiliar.
         *
         * Ejemplo:
         *
         * log₂(x + 1)
         *
         * PARENT = x + 1
         * CHILD  = 2
         */
        is MathExpression.Function ->
            listOfNotNull(
                required(
                    id = "argument",
                    content = argument,
                    role = MathNavigationRole.PARENT
                ) {
                    copy(
                        argument = it
                    )
                },

                optional(
                    id = "base",
                    content = base,
                    role = MathNavigationRole.CHILD,
                    update = {
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

        /*
         * Integral, sumatoria y producto.
         *
         * upper y lower son hijos.
         *
         * El operando principal NO pertenece
         * al LargeOperator en el AST.
         * Permanece como siguiente sibling
         * dentro de Sequence.
         */
        is MathExpression.LargeOperator ->
            listOfNotNull(
                optional(
                    id = "upper",
                    content = upper,
                    role = MathNavigationRole.CHILD,
                    update = {
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
                    id = "lower",
                    content = lower,
                    role = MathNavigationRole.CHILD,
                    update = {
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

        /*
         * lim condition body
         *
         * body es la expresión principal.
         * condition funciona como anotación inferior.
         */
        is MathExpression.Limit ->
            listOf(
                required(
                    id = "body",
                    content = body,
                    role = MathNavigationRole.PARENT
                ) {
                    copy(
                        body = it
                    )
                },

                required(
                    id = "condition",
                    content = condition,
                    role = MathNavigationRole.CHILD
                ) {
                    copy(
                        condition = it
                    )
                }
            )

        /*
         * La expresión derivada es principal.
         *
         * order y variable describen/modifican
         * la operación derivada.
         */
        is MathExpression.Derivative ->
            listOfNotNull(
                optional(
                    id = "order",
                    content = order,
                    role = MathNavigationRole.CHILD,
                    update = {
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
                    id = "body",
                    content = body,
                    role = MathNavigationRole.PARENT
                ) {
                    copy(
                        body = it
                    )
                },

                required(
                    id = "variable",
                    content = variable,
                    role = MathNavigationRole.CHILD
                ) {
                    copy(
                        variable = it
                    )
                }
            )

        /*
         * Cada celda es una región matemática
         * principal independiente.
         *
         * No es un índice ni un modificador
         * de otra celda.
         */
        is MathExpression.Matrix ->
            rows.flatMapIndexed { r, row ->

                row.mapIndexed { c, cell ->

                    required(
                        id = "r${r}c$c",
                        content = cell,
                        role = MathNavigationRole.PARENT
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

        /*
         * Cada fila de Cases posee dos regiones
         * matemáticas completas:
         *
         * expresión
         * condición
         *
         * Ninguna es un índice, exponente
         * o modificador tipográfico de la otra.
         */
        is MathExpression.Cases ->
            rows.flatMapIndexed { index, row ->

                listOf(
                    required(
                        id = "expression$index",
                        content = row.expression,
                        role = MathNavigationRole.PARENT
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
                        id = "condition$index",
                        content = row.condition,
                        role = MathNavigationRole.PARENT
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

/**
 * Compatibilidad temporal con el nombre antiguo.
 *
 * La lista ya no significa "solo hijos".
 * Puede contener PARENT y CHILD.
 */
fun MathExpression.navigationChildren(): List<MathNavigationPart> =
    navigationParts()

/**
 * Orden semántico de recorrido dentro de una STRUCTURE.
 *
 * La posición visual NO decide por dónde entra la navegación.
 *
 * Regla:
 *
 * 1. Primero todos los PARENT.
 * 2. Después todos los CHILD.
 *
 * Dentro de cada grupo se conserva el orden declarado
 * por navigationParts().
 *
 * Ejemplos:
 *
 * Root:
 *     PARENT radicand
 *     CHILD  index
 *
 * Scripts:
 *     PARENT base
 *     CHILD  upper
 *     CHILD  lower
 *
 * Fraction:
 *     PARENT numerator
 *     PARENT denominator
 *
 * LargeOperator:
 *     no tiene PARENT propios
 *     CHILD upper
 *     CHILD lower
 */
fun MathExpression.navigationTraversalParts(): List<MathNavigationPart> {

    val parts =
        navigationParts()

    return parts.filter {
        it.role == MathNavigationRole.PARENT
    } + parts.filter {
        it.role == MathNavigationRole.CHILD
    }
}

fun MathExpression.childSlots(): List<Pair<String, MathExpression.Sequence>> =
    navigationParts().map {
        it.id to it.content
    }

fun MathExpression.withChildSlot(
    slot: String,
    value: MathExpression.Sequence
): MathExpression =
    navigationParts()
        .firstOrNull {
            it.id == slot
        }
        ?.update
        ?.invoke(value)
        ?: this

fun MathExpression.isComplete(): Boolean =
    when (this) {

        is MathExpression.Sequence ->
            children.isNotEmpty() &&
                    children.all {
                        it.isComplete()
                    }

        is MathExpression.Number ->
            value.isNotBlank()

        is MathExpression.Variable ->
            name.isNotBlank()

        is MathExpression.Symbol ->
            value.isNotBlank()

        is MathExpression.Operator ->
            value.isNotBlank()

        is MathExpression.Relation ->
            value.isNotBlank()

        else ->
            childSlots().all {
                it.second.isComplete()
            }
    }