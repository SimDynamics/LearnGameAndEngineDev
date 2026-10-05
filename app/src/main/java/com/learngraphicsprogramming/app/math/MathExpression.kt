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
    PARENT_BOUNDARY,
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
/**
 * Papel estructural de una parte matemática.
 *
 * PARENT:
 * región matemática principal de una estructura.
 *
 * CHILD:
 * región matemática subordinada:
 * índice, exponente, subíndice,
 * límite, base auxiliar, etc.
 *
 * PARENT y CHILD solamente dicen QUÉ ES
 * una parte dentro de una STRUCTURE.
 *
 * No indican si puede editarse,
 * seleccionarse o eliminarse.
 */
/**
 * Papel de una región navegable dentro de una STRUCTURE.
 *
 * SUBSTRUCTURE:
 * región obligatoria que posee su propio recorrido interno.
 *
 * Ejemplos:
 * - numerador
 * - denominador
 *
 * PARENT:
 * unidad matemática normal contenida dentro de una región.
 *
 * Ejemplos:
 * - x
 * - 2
 * - +
 *
 * CHILD:
 * modificador subordinado.
 *
 * Ejemplos:
 * - índice de raíz
 * - exponente
 * - subíndice
 * - límites
 *
 * STRUCTURE y DECORATION no aparecen aquí:
 *
 * STRUCTURE es el MathExpression completo.
 * DECORATION no participa en navegación.
 */
enum class MathNavigationRole {
    SUBSTRUCTURE,
    PARENT,
    CHILD
}

/**
 * Capacidades de interacción de un elemento matemático.
 *
 * Son completamente independientes
 * de su papel estructural.
 *
 * editable:
 * el usuario puede entrar dentro del elemento
 * y modificar su contenido.
 *
 * selectable:
 * el elemento posee un checkpoint navegable
 * en su borde derecho.
 *
 * No existe borde izquierdo.
 *
 * deletable:
 * el elemento puede eliminarse desde
 * su estado estructural correspondiente.
 */
data class MathInteractionCapabilities(
    val editable: Boolean,
    val selectable: Boolean,
    val deletable: Boolean
)

/**
 * Papel estructural de un MathExpression cuando vive
 * directamente dentro de una Sequence.
 *
 * Los átomos matemáticos son PARENT.
 *
 * Las STRUCTURE no reciben aquí un papel PARENT/CHILD,
 * porque administran sus propias partes mediante
 * navigationParts().
 */
val MathExpression.navigationRole: MathNavigationRole?
    get() =
        when (this) {

            is MathExpression.Number,
            is MathExpression.Variable,
            is MathExpression.Symbol,
            is MathExpression.Operator,
            is MathExpression.Relation ->
                MathNavigationRole.PARENT

            is MathExpression.Sequence,
            is MathExpression.Fraction,
            is MathExpression.Scripts,
            is MathExpression.Root,
            is MathExpression.Group,
            is MathExpression.Function,
            is MathExpression.LargeOperator,
            is MathExpression.Limit,
            is MathExpression.Derivative,
            is MathExpression.Matrix,
            is MathExpression.Cases ->
                null
        }

/**
 * Parte navegable inmediata de una STRUCTURE.
 *
 * role describe:
 *
 * PARENT / CHILD
 *
 * interaction describe:
 *
 * editable / selectable / deletable
 *
 * Ambos conceptos son independientes.
 */
data class MathNavigationPart(
    val id: String,
    val content: MathExpression.Sequence,
    val requirement: MathSlotRequirement,
    val update: (MathExpression.Sequence) -> MathExpression,
    val remove: () -> List<MathExpression>,
    val role: MathNavigationRole,
    val interaction: MathInteractionCapabilities =
        MathInteractionCapabilities(
            editable = true,
            selectable = true,
            deletable =
                requirement == MathSlotRequirement.OPTIONAL
        )
)

/**
 * Alias temporal para no romper código antiguo que todavía
 * use el nombre MathNavigationChild.
 *
 * El concepto correcto es MathNavigationPart.
 */
typealias MathNavigationChild = MathNavigationPart

/**
 * Capacidades de un MathExpression completo.
 *
 * Estas capacidades pertenecen al bloque matemático
 * completo y NO a sus PARENT o CHILD internos.
 *
 * Ejemplo:
 *
 * log₂(x)
 *
 * STRUCTURE Function:
 *
 * editable   = false
 * selectable = true
 * deletable  = true
 */
val MathExpression.interactionCapabilities: MathInteractionCapabilities
    get() =
        when (this) {

            /*
             * Sequence organiza elementos.
             *
             * No representa por sí misma
             * una unidad matemática seleccionable.
             */
            is MathExpression.Sequence ->
                MathInteractionCapabilities(
                    editable = false,
                    selectable = false,
                    deletable = false
                )

            /*
             * Operadores y relaciones.
             *
             * Son unidades matemáticas atómicas.
             *
             * No se entra dentro de ellas.
             *
             * Sí poseen borde derecho navegable
             * y sí pueden eliminarse.
             *
             * Ejemplo:
             *
             * x + ] y
             */
            is MathExpression.Operator,
            is MathExpression.Relation ->
                MathInteractionCapabilities(
                    editable = false,
                    selectable = true,
                    deletable = true
                )

            /*
             * Hojas matemáticas normales.
             *
             * Conservamos de momento
             * su comportamiento editable.
             */
            is MathExpression.Number,
            is MathExpression.Variable,
            is MathExpression.Symbol ->
                MathInteractionCapabilities(
                    editable = true,
                    selectable = true,
                    deletable = true
                )

            /*
             * Toda STRUCTURE matemática completa
             * funciona como un bloque.
             *
             * La estructura no se edita directamente.
             * Sus PARENT y CHILD administran
             * la navegación interna.
             */
            is MathExpression.Fraction,
            is MathExpression.Scripts,
            is MathExpression.Root,
            is MathExpression.Group,
            is MathExpression.Function,
            is MathExpression.LargeOperator,
            is MathExpression.Limit,
            is MathExpression.Derivative,
            is MathExpression.Matrix,
            is MathExpression.Cases ->
                MathInteractionCapabilities(
                    editable = false,
                    selectable = true,
                    deletable = true
                )
        }

/**
 * Partes navegables inmediatas de una estructura.
 *
 * Una estructura administra únicamente sus partes inmediatas.
 * Si una parte contiene otra estructura, esa estructura
 * administra su propio universo cuando la navegación entra en ella.
 *
 * ORDEN CANÓNICO:
 *
 * Primero todos los PARENT.
 * Después todos los CHILD.
 *
 * Dentro de cada grupo se conserva
 * el orden declarado.
 */
fun MathExpression.navigationParts(): List<MathNavigationPart> {

    fun required(
        id: String,
        content: MathExpression.Sequence,
        role: MathNavigationRole,
        interaction: MathInteractionCapabilities =
            MathInteractionCapabilities(
                editable = true,
                selectable = true,
                deletable = false
            ),
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
            role = role,
            interaction = interaction
        )

    fun optional(
        id: String,
        content: MathExpression.Sequence?,
        role: MathNavigationRole,
        interaction: MathInteractionCapabilities =
            MathInteractionCapabilities(
                editable = true,
                selectable = true,
                deletable = true
            ),
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
                role = role,
                interaction = interaction
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
         * STRUCTURE Fraction
         *
         * numerator y denominator son SUBSTRUCTURE
         * obligatorias.
         *
         * La raya es DECORATION:
         * no aparece aquí porque no participa
         * en navegación.
         */
        is MathExpression.Fraction ->
            listOf(
                required(
                    id = "numerator",
                    content = numerator,
                    role = MathNavigationRole.SUBSTRUCTURE
                ) {
                    copy(
                        numerator = it
                    )
                },

                required(
                    id = "denominator",
                    content = denominator,
                    role = MathNavigationRole.SUBSTRUCTURE
                ) {
                    copy(
                        denominator = it
                    )
                }
            )

        /*
         * STRUCTURE Scripts
         *
         * base es la región matemática principal.
         * upper y lower son CHILD.
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
         * STRUCTURE Root
         *
         * La casita/√ es DECORATION.
         *
         * radicand es PARENT obligatorio:
         * seleccionable
         * editable
         * no eliminable.
         *
         * index es CHILD opcional.
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
         * Los delimitadores son DECORATION.
         * content es PARENT obligatorio.
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
         * Function todavía conserva la definición
         * actual hasta que implementemos su head "log".
         *
         * argument es PARENT obligatorio.
         * base es CHILD opcional.
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
         * LargeOperator sigue SIN body.
         *
         * upper y lower son CHILD.
         *
         * Su expresión de la derecha continúa siendo
         * sibling dentro de la Sequence contenedora.
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

        is MathExpression.Matrix ->
            rows.flatMapIndexed { r, row ->

                row.mapIndexed { c, cell ->

                    required(
                        id = "r${r}c$c",
                        content = cell,
                        role = MathNavigationRole.SUBSTRUCTURE
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
                        id = "expression$index",
                        content = row.expression,
                        role = MathNavigationRole.SUBSTRUCTURE
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
                        role = MathNavigationRole.SUBSTRUCTURE
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
/**
 * Orden semántico inmediato dentro de una STRUCTURE.
 *
 * IMPORTANTE:
 *
 * Esto solamente organiza las partes inmediatas.
 * La recursión profunda la controla MathExpressionModel.
 *
 * Orden:
 *
 * 1. SUBSTRUCTURE
 * 2. PARENT
 * 3. CHILD
 *
 * Dentro de cada grupo se conserva el orden visual
 * declarado por navigationParts():
 *
 * arriba -> abajo
 * izquierda -> derecha
 *
 * Cuando una región contiene otra STRUCTURE,
 * esa STRUCTURE administra completamente
 * su propio recorrido antes de continuar.
 */
fun MathExpression.navigationTraversalParts(): List<MathNavigationPart> {

    val parts =
        navigationParts()

    /*
     * Orden de LEFT:
     *
     * 1. arriba -> abajo
     * 2. a la misma altura: derecha -> izquierda
     *
     * Cada parte conserva después su propia jerarquía interna.
     */
    return when (this) {

        is MathExpression.Fraction,
        is MathExpression.Scripts,
        is MathExpression.Root,
        is MathExpression.Group,
        is MathExpression.Function,
        is MathExpression.LargeOperator,
        is MathExpression.Limit,
        is MathExpression.Derivative ->
            parts

        is MathExpression.Matrix ->
            rows.indices.flatMap { row ->

                rows[row].indices
                    .reversed()
                    .mapNotNull { column ->

                        parts.firstOrNull {
                            it.id == "r${row}c$column"
                        }
                    }
            }

        is MathExpression.Cases ->
            rows.indices.flatMap { row ->

                listOfNotNull(
                    parts.firstOrNull {
                        it.id == "condition$row"
                    },
                    parts.firstOrNull {
                        it.id == "expression$row"
                    }
                )
            }

        else ->
            parts
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