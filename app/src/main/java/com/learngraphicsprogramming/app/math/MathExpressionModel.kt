package com.learngraphicsprogramming.app.math

import org.json.JSONArray
import org.json.JSONObject

data class MathSelection(val path: List<MathCursorStep>, val start: Int, val end: Int)

/** All edits replace AST nodes. LaTeX is used only at import/export boundaries. */
class MathExpressionModel(initialExpression: MathExpression.Sequence = MathExpression.Sequence()) {
    var expression: MathExpression.Sequence = initialExpression
        private set
    private var navigationBoundary: NavigationBoundary? = null
    private var navigationInsertion: NavigationInsertion? = null
    private var navigationEdge: NavigationEdge? = null
    var cursor: MathCursor = MathCursor(offset = initialExpression.children.size)
        set(value) {
            navigationBoundary = null
            navigationInsertion = null
            navigationEdge = null
            field = safeCursor(value)
        }
    var selectionRange: MathSelection? = null
        private set

    /** Navigation controls remain present for the whole lifetime of a structured expression. */
    val hasNavigableSlots: Boolean
        get() = expression.children.any { it.childSlots().isNotEmpty() }

    /** Availability queries never clear a selection or move the active cursor. */
    val canNavigatePrevious: Boolean
        get() = progressiveNavigationTarget(forward = false) != null

    val canNavigateNext: Boolean
        get() = progressiveNavigationTarget(forward = true) != null

    fun setExpression(value: MathExpression.Sequence, position: MathCursor = MathCursor(offset = value.children.size)) {
        expression = value
        cursor = position
        selectionRange = null
    }

    fun isComplete(): Boolean = expression.isComplete() && MathLatexParser.parse(MathLatexSerializer.serialize(expression)) is MathLatexParser.Result.Success
    fun clear() = setExpression(MathExpression.Sequence())

    fun selectAll() {
        selectionRange = if (expression.children.isEmpty()) null else MathSelection(emptyList(), 0, expression.children.size)
        cursor = MathCursor(offset = expression.children.size)
    }

    fun select(path: List<MathCursorStep>, start: Int, end: Int) {
        val sequence = sequenceAt(path) ?: return
        val left = minOf(start, end).coerceIn(0, sequence.children.size)
        val right = maxOf(start, end).coerceIn(left, sequence.children.size)
        selectionRange = if (left == right) null else MathSelection(path.toList(), left, right)
        cursor = if (cursor.path == path) cursor.copy(offset = right) else MathCursor(path, right)
    }

    fun clearSelection() { selectionRange = null }

    fun takeSelection(): MathExpression.Sequence {
        val selection = selectionRange ?: return MathExpression.Sequence()
        val sequence = sequenceAt(selection.path) ?: return MathExpression.Sequence().also { selectionRange = null }
        val selected = MathExpression.Sequence(sequence.children.subList(selection.start, selection.end).toList())
        replaceSequence(selection.path, sequence.copy(children = sequence.children.take(selection.start) + sequence.children.drop(selection.end)))
        selectionRange = null
        cursor = cursor.copy(path = selection.path, offset = selection.start)
        return selected
    }

    fun takePrevious(): MathExpression.Sequence {
        if (selectionRange != null) return takeSelection()
        val current = sequenceAt(cursor.path) ?: return MathExpression.Sequence()
        if (cursor.offset == 0 || (navigationLevel == MathCursorLevel.INTERIOR &&
                cursor.offset <= contentScopeBounds().first)) return MathExpression.Sequence()
        val end = cursor.offset
        var start = end - 1
        val previous = current.children[start]
        if (previous is MathExpression.Number && navigationLevel == MathCursorLevel.INTERIOR) {
            val first = contentScopeBounds().first
            while (start > first && current.children[start - 1] is MathExpression.Number) start--
        }
        val selected = current.children.subList(start, end)
        val result = if (previous is MathExpression.Number) listOf(MathExpression.Number(selected.joinToString("") { (it as MathExpression.Number).value })) else selected.toList()
        replaceSequence(cursor.path, current.copy(children = current.children.take(start) + current.children.drop(end)))
        cursor = cursor.copy(offset = start)
        return MathExpression.Sequence(result)
    }

    fun insert(node: MathExpression, enterSlot: String? = null): Boolean {
        val selection = selectionRange
        val position = selection?.let { if (cursor.path == it.path) cursor.copy(offset = it.start) else MathCursor(it.path, it.start) } ?: cursor
        val current = sequenceAt(position.path) ?: return false
        val end = selection?.end ?: position.offset
        val removed = current.children.subList(position.offset, end)
        val added = if (node is MathExpression.Sequence) node.children else listOf(node)
        val numericScope = (position.scope as? MathContentScope.Atom)?.takeIf {
            position.level == MathCursorLevel.INTERIOR && it.start < it.end &&
                position.offset >= it.start && end <= it.end &&
                current.children.subList(it.start, it.end).all { child -> child is MathExpression.Number } &&
                added.all { child -> child is MathExpression.Number }
        }
        val editingSlot = position.path.isNotEmpty() && position.level == MathCursorLevel.INTERIOR && position.scope !is MathContentScope.Atom
        if (position.path.size + structuralDepth(node) > 60 ||
            nodeCount(expression) - removed.sumOf(::nodeCount) + added.sumOf(::nodeCount) > 4_096 ||
            MathLatexSerializer.serialize(expression).length - removed.sumOf { MathLatexSerializer.serialize(it).length } +
            added.sumOf { MathLatexSerializer.serialize(it).length } > 32_768) return false
        replaceSequence(position.path, current.copy(children = current.children.take(position.offset) + added + current.children.drop(end)))
        selectionRange = null
        val editable = added.singleOrNull()?.navigationChildren().orEmpty()
        val slot = enterSlot?.takeIf { name -> editable.any { it.id == name } }
            ?: if (node !is MathExpression.Sequence) editable.firstOrNull()?.id else null
        val retainedScope = cursor.scope.takeIf { numericScope != null }
        cursor = if (slot != null) enterSlotTarget(position.path + MathCursorStep(position.offset, slot), forward = true)!!.cursor
            else if (added.isNotEmpty() && added.all { it.navigationChildren().isEmpty() }) MathCursor(position.path, position.offset + added.size, MathCursorLevel.INTERIOR,
                if (editingSlot) MathContentScope.Slot else retainedScope ?:
                    MathContentScope.Atom(position.offset + added.size - 1, position.offset + added.size))
            else MathCursor(position.path, position.offset + added.size, MathCursorLevel.NEUTRAL, null)
        return true
    }

    /** Only the directly edited base or preceding group can own the newly created script. */
    fun addScript(upper: Boolean): Boolean {
        val originalExpression = expression
        val originalCursor = cursor
        val originalSelection = selectionRange
        val originalBoundary = navigationBoundary
        val originalInsertion = navigationInsertion
        val originalEdge = navigationEdge
        val slot = if (upper) "upper" else "lower"
        if (selectionRange == null) {
            for (depth in cursor.path.indices.reversed().take(1)) {
                val step = cursor.path[depth]
                if (step.slot != "base") break
                val parentPath = cursor.path.take(depth)
                val scripts = sequenceAt(parentPath)?.children?.getOrNull(step.nodeIndex) as? MathExpression.Scripts ?: continue
                val existing = if (upper) scripts.upper else scripts.lower
                if (existing != null) {
                    cursor = MathCursor(parentPath + step.copy(slot = slot), existing.children.size)
                    return true
                }
                val updated = if (upper) scripts.copy(upper = MathExpression.Sequence()) else scripts.copy(lower = MathExpression.Sequence())
                select(parentPath, step.nodeIndex, step.nodeIndex + 1)
                if (insert(updated, slot)) return true
                expression = originalExpression
                cursor = originalCursor
                selectionRange = originalSelection
                navigationBoundary = originalBoundary
                navigationInsertion = originalInsertion
                navigationEdge = originalEdge
                return false
            }
        }
        val base = takePrevious()
        val previous = base.children.singleOrNull() as? MathExpression.Scripts
        val scripts = previous?.takeIf { (if (upper) it.upper else it.lower) == null } ?: MathExpression.Scripts(base)
        val existing = if (upper) scripts.upper else scripts.lower
        val updated = if (upper) scripts.copy(upper = existing ?: MathExpression.Sequence())
            else scripts.copy(lower = existing ?: MathExpression.Sequence())
        val target = slot
        if (insert(updated, target)) {
            if (existing != null && target == slot) cursor = cursor.copy(offset = existing.children.size)
            return true
        }
        expression = originalExpression
        cursor = originalCursor
        selectionRange = originalSelection
        navigationBoundary = originalBoundary
        navigationInsertion = originalInsertion
        navigationEdge = originalEdge
        return false
    }

    /** IME text becomes glyph nodes or imported supported LaTeX, never a mutable LaTeX string. */
    fun insertText(text: String): Boolean {
        if (text.isEmpty()) return true
        if (text.length > 32_768 || MathLatexSerializer.serialize(expression).length + text.length > 32_768) return false
        if (text.length > 1 || text.contains('\\')) {
            val parsed = MathLatexParser.parse(text)
            if (parsed is MathLatexParser.Result.Success) {
                return insert(parsed.expression)
            }
            if (text.contains('\\')) return false
        }
        // Check the complete incoming batch before editing so unsupported paste is atomic.
        if (text.any { !(it.isLetterOrDigit() || it.isWhitespace() || it.toString() in MathLatexSerializer.symbols || it in ".,+-−*/×÷·=<>≤≥≠≈→π∞∂()[]|^_!:%;") }) return false
        if (text.count { !it.isWhitespace() } + nodeCount(expression) > 4_096) return false
        val originalExpression = expression
        val originalCursor = cursor
        val originalSelection = selectionRange
        val originalBoundary = navigationBoundary
        val originalInsertion = navigationInsertion
        val originalEdge = navigationEdge
        for (char in text) {
            val inserted = when {
                char.isWhitespace() -> true
                char.isDigit() || char == '.' || char == ',' -> insertNumericCharacter(if (char == ',') "." else char.toString())
                (char.toString() in MathLatexSerializer.symbols && char !in "≤≥≠≈→") || char == 'e' -> insert(MathExpression.Symbol(char.toString()))
                char.isLetter() -> insert(MathExpression.Variable(char.toString()))
                char in "+-−*/×÷·" -> insert(MathExpression.Operator(char.toString()))
                char in "=<>≤≥≠≈→" -> insert(MathExpression.Relation(char.toString()))
                char == '(' || char == '[' || char == '|' -> {
                    val delimiter = if (char == '(') MathDelimiter.PARENTHESES else if (char == '[') MathDelimiter.BRACKETS else MathDelimiter.ABSOLUTE
                    insert(MathExpression.Group(takeSelection(), delimiter), "content")
                }
                char == ')' || char == ']' -> { exitCurrentSlot(); true }
                char == '^' || char == '_' -> addScript(upper = char == '^')
                else -> insert(MathExpression.Symbol(char.toString()))
            }
            if (!inserted) {
                expression = originalExpression
                cursor = originalCursor
                selectionRange = originalSelection
                navigationBoundary = originalBoundary
                navigationInsertion = originalInsertion
                navigationEdge = originalEdge
                return false
            }
        }
        return true
    }

    /**
     * Inserta un carácter numérico.
     *
     * Dentro de SUBSTRUCTURE cada carácter
     * crea su propio PARENT:
     *
     * 1
     * 2
     * 3
     *
     * son tres nodos consecutivos.
     *
     * Fuera de SUBSTRUCTURE conservamos
     * el comportamiento normal de agrupar
     * cifras en Number.
     */
    private fun insertNumericCharacter(
        value: String
    ): Boolean {

        takeSelection()

        val currentPart =
            navigationPart(
                cursor.path
            )

        /*
         * Numerador, denominador, celda, etc.
         *
         * Cada carácter crea su PARENT propio.
         */
        if (
            currentPart?.role ==
            MathNavigationRole.SUBSTRUCTURE
        ) {

            return insert(
                MathExpression.Number(
                    value
                )
            )
        }

        val sequence =
            sequenceAt(
                cursor.path
            ) ?: return false

        val bounds =
            contentScopeBounds()

        val previous =
            if (
                navigationLevel ==
                MathCursorLevel.INTERIOR &&
                cursor.offset - 1 in
                bounds.first until bounds.second
            ) {

                sequence.children
                    .getOrNull(
                        cursor.offset - 1
                    ) as? MathExpression.Number

            } else {

                null
            }

        if (
            previous == null
        ) {

            return insert(
                MathExpression.Number(
                    value
                )
            )
        }

        val children =
            sequence.children
                .toMutableList()

        children[
            cursor.offset - 1
        ] =
            previous.copy(
                value =
                    previous.value +
                            value
            )

        replaceSequence(
            cursor.path,
            sequence.copy(
                children = children
            )
        )

        return true
    }

    fun insertLatex(latex: String): MathLatexParser.Result {
        val parsed = MathLatexParser.parse(latex)
        return if (parsed is MathLatexParser.Result.Success && !insert(parsed.expression))
            MathLatexParser.Result.Failure("Expression exceeds the editable resource limits", 0, latex.length)
        else parsed
    }

    /** Fine directional movement; fast Tab slot traversal remains a separate API. */
    fun moveRight(): Boolean = stepRight()
    fun moveLeft(): Boolean = stepLeft()

    /** LEFT opens declared children; RIGHT advances through the next declared child. */
    fun navigateNext(): Boolean = navigateProgressively(forward = true)
    fun navigatePrevious(): Boolean = navigateProgressively(forward = false)

    private data class NavigationBoundary(
        val slotPath: List<MathCursorStep>, val after: Boolean,
        val expression: MathExpression.Sequence, val cursor: MathCursor
    ) { init { require(after) } }
    private data class NavigationTarget(
        val cursor: MathCursor, val selection: MathSelection? = null, val boundary: NavigationBoundary? = null,
        val insertion: Boolean = false
    )
    private data class NavigationInsertion(val expression: MathExpression.Sequence, val cursor: MathCursor)
    private data class NavigationEdge(val expression: MathExpression.Sequence, val forward: Boolean,
        val from: NavigationTarget, val to: NavigationTarget)

    /** Exterior navigation is anchored to a real parent AST cursor, never an IME selection. */
    val isAtNavigationBoundary: Boolean get() = activeNavigationBoundary() != null
    val isAtSequenceInsertion: Boolean get() = activeNavigationInsertion() != null
    val navigationLevel: MathCursorLevel get() = if (isAtNavigationBoundary) MathCursorLevel.CHILD_BOUNDARY
        else cursor.level.takeUnless { it == MathCursorLevel.CHILD_BOUNDARY } ?: MathCursorLevel.NEUTRAL
    val editingScope: MathContentScope? get() = cursor.scope
    val cursorPresentation: MathCursorPresentation
        get() =
            activeNavigationBoundary()?.let {

                MathCursorPresentation.ChildBoundary(
                    it.slotPath,
                    it.after
                )

            } ?: if (
                isAtSequenceInsertion
            ) {

                MathCursorPresentation.ContinuationBoundary

            } else if (
                cursor.level ==
                MathCursorLevel.PARENT_BOUNDARY
            ) {

                MathCursorPresentation.ParentBoundary(
                    cursor.path.toList(),
                    cursor.offset - 1
                )

            } else if (
                navigationLevel ==
                MathCursorLevel.INTERIOR
            ) {

                MathCursorPresentation.Interior(
                    scope =
                        cursor.scope,

                    leafTextOffset =
                        cursor.leafTextOffset
                )

            } else {

                MathCursorPresentation.Neutral
            }

    fun contentScopeBounds(): Pair<Int, Int> {
        val end = sequenceAt(cursor.path)?.children?.size ?: 0
        if (navigationLevel != MathCursorLevel.INTERIOR) return cursor.offset to cursor.offset
        return when (val scope = cursor.scope) {
            is MathContentScope.Atom -> scope.start.coerceIn(0, end) to scope.end.coerceIn(scope.start.coerceIn(0, end), end)
            else -> 0 to end
        }
    }

    private fun activeNavigationBoundary(): NavigationBoundary? = navigationBoundary?.takeIf {
        it.expression === expression && it.cursor == cursor && sequenceAt(it.slotPath) != null
    }

    private fun activeNavigationInsertion(): NavigationInsertion? = navigationInsertion?.takeIf {
        it.expression === expression && it.cursor == cursor && selectionRange == null &&
            sequenceAt(cursor.path)?.let { sequence ->
                cursor.offset == sequence.children.size && sequence.hasContinuationInsertion()
            } == true
    }

    private fun navigationState() = NavigationTarget(cursor, selectionRange, activeNavigationBoundary(), isAtSequenceInsertion)

    private fun navigateProgressively(forward: Boolean): Boolean {
        val target = progressiveNavigationTarget(forward) ?: return false
        val previous = navigationState()
        cursor = target.cursor
        selectionRange = target.selection
        navigationBoundary = target.boundary
        navigationInsertion = if (target.insertion) NavigationInsertion(expression, cursor) else null
        navigationEdge = NavigationEdge(expression, forward, previous, navigationState())
        return true
    }

    /**
     * Navegación estructural detallada.
     *
     * LEFT recorre el árbol en orden semántico.
     * RIGHT recorre exactamente los mismos estados
     * en sentido inverso.
     *
     * MathSelection NO representa navegación.
     *
     * Solo existen bordes derechos.
     */
    private fun progressiveNavigationTarget(
        forward: Boolean
    ): NavigationTarget? {

        /*
         * BORDE DERECHO DEL PARENT VIRTUAL
         *
         * LEFT:
         *
         * □ ] -> □ |
         *
         * RIGHT:
         *
         * □ ] -> FIN |
         */
        if (
            isAtSequenceInsertion
        ) {

            val sequence =
                sequenceAt(
                    cursor.path
                ) ?: return null

            return if (forward) {

                if (
                    cursor.path.isEmpty()
                ) {

                    /*
                     * FIN derecho de la expresión completa.
                     *
                     * Tiene exactamente el mismo offset físico,
                     * pero ya NO conserva navigationInsertion.
                     */
                    NavigationTarget(
                        MathCursor(
                            path = cursor.path,
                            offset = sequence.children.size,
                            level = MathCursorLevel.NEUTRAL,
                            scope = null
                        )
                    )

                } else {

                    /*
                     * Dentro de una región estructural,
                     * regresamos a su extremo derecho editable.
                     */
                    slotEditingTarget(
                        cursor.path,
                        atRight = true
                    )
                }

            } else {

                continuationEditingTarget(
                    cursor.path
                )
            }
        }

        /*
         * SELECCIÓN MATEMÁTICA REAL.
         *
         * LEFT:
         *
         * x| -> {x} -> |x
         *
         * RIGHT hace lo mismo en reversa.
         */
        selectionRange?.let { selection ->

            val scope =
                cursor.scope
                    ?: if (
                        selection.path.isNotEmpty()
                    ) {

                        MathContentScope.Slot

                    } else {

                        MathContentScope.Atom(
                            selection.start,
                            selection.end
                        )
                    }

            return NavigationTarget(
                cursor.copy(
                    path = selection.path,
                    offset =
                        if (forward) {
                            selection.end
                        } else {
                            selection.start
                        },
                    level =
                        MathCursorLevel.INTERIOR,
                    scope = scope
                )
            )
        }

        /*
         * BORDE DE PARENT / CHILD DECLARADO.
         */
        activeNavigationBoundary()?.let { boundary ->

            return if (forward) {

                previousSlotEditingTarget(
                    boundary.slotPath
                )

            } else {

                enterSlotTarget(
                    boundary.slotPath,
                    forward = false
                )
            }
        }

        /*
         * EDICIÓN.
         */
        if (
            navigationLevel ==
            MathCursorLevel.INTERIOR
        ) {

            val sequence =
                sequenceAt(
                    cursor.path
                ) ?: return null

            when (
                val scope =
                    cursor.scope
            ) {

                is MathContentScope.Atom -> {

                    /*
                     * PARENT virtual vacío situado
                     * después de LargeOperator.
                     *
                     * □ |
                     */
                    if (
                        scope.start ==
                        scope.end &&
                        cursor.offset ==
                        sequence.children.size &&
                        sequence.hasContinuationInsertion()
                    ) {

                        return if (forward) {

                            /*
                             * RIGHT:
                             *
                             * □ | -> □ ]
                             */
                            continuationTarget(
                                cursor.path
                            )

                        } else {

                            /*
                             * LEFT:
                             *
                             * □ | -> Integral ]
                             */
                            val lastIndex =
                                sequence.children.lastIndex

                            if (
                                lastIndex >= 0
                            ) {

                                nodeBoundaryTarget(
                                    cursor.path,
                                    lastIndex
                                )

                            } else if (
                                cursor.path.isNotEmpty()
                            ) {

                                afterSlotEditingTarget(
                                    cursor.path
                                )

                            } else {

                                NavigationTarget(
                                    MathCursor(
                                        path =
                                            emptyList(),
                                        offset = 0,
                                        level =
                                            MathCursorLevel.NEUTRAL,
                                        scope = null
                                    )
                                )
                            }
                        }
                    }

                    val start =
                        scope.start.coerceIn(
                            0,
                            sequence.children.size
                        )

                    val end =
                        scope.end.coerceIn(
                            start,
                            sequence.children.size
                        )

                    if (
                        end <= start
                    ) {
                        return null
                    }

                    val nodeIndex =
                        start.coerceAtMost(
                            sequence.children.lastIndex
                        )

                    return if (forward) {

                        if (
                            cursor.offset <= start
                        ) {

                            NavigationTarget(
                                cursor.copy(
                                    offset = end,
                                    leafTextOffset = null
                                )
                            )

                        } else {

                            /*
                             * x| -> x]
                             */
                            nodeBoundaryTarget(
                                cursor.path,
                                nodeIndex
                            )
                        }

                    } else {

                        if (
                            cursor.offset >= end
                        ) {

                            NavigationTarget(
                                cursor.copy(
                                    offset = start,
                                    leafTextOffset = null
                                )
                            )

                        } else {

                            /*
                             * |x -> siguiente elemento
                             * situado hacia la izquierda.
                             */
                            nextSemanticAfterNode(
                                cursor.path,
                                nodeIndex
                            )
                        }
                    }
                }

                else -> {

                    /*
                     * Región editable completa:
                     *
                     * numerator|
                     * {numerator}
                     * |numerator
                     *
                     * También se usa para CHILD editables
                     * como upper/lower.
                     */
                    val start =
                        0

                    val end =
                        sequence.children.size

                    val part =
                        navigationPart(
                            cursor.path
                        )

                    /*
 * Una región formada únicamente por
 * texto se recorre carácter por carácter.
 *
 * Ya NO existe:
 *
 * {x+1}
 *
 * ni ninguna selección automática.
 */
                    val isPlainText =
                        sequence.children
                            .subList(
                                start,
                                end
                            )
                            .all {
                                leafText(it) != null
                            }

                    if (isPlainText) {

                        return textNavigationTarget(
                            sequence =
                                sequence,

                            start =
                                start,

                            end =
                                end,

                            forward =
                                forward,

                            exitLeft = {

                                afterSlotEditingTarget(
                                    cursor.path
                                )
                            },

                            exitRight = {

                                slotExitRightTarget(
                                    cursor.path
                                )
                            }
                        )
                    }

                    return if (forward) {

                        when {

                            cursor.offset <=
                                    start &&
                                    end > start -> {

                                NavigationTarget(
                                    cursor.copy(
                                        offset = end,
                                        scope =
                                            MathContentScope.Slot
                                    ),
                                    MathSelection(
                                        cursor.path.toList(),
                                        start,
                                        end
                                    )
                                )
                            }

                            cursor.offset >=
                                    end -> {

                                slotExitRightTarget(
                                    cursor.path
                                )
                            }

                            else -> {

                                NavigationTarget(
                                    cursor.copy(
                                        offset = end,
                                        scope =
                                            MathContentScope.Slot
                                    )
                                )
                            }
                        }

                    } else {

                        when {

                            cursor.offset >=
                                    end &&
                                    end > start -> {

                                NavigationTarget(
                                    cursor.copy(
                                        offset = end,
                                        scope =
                                            MathContentScope.Slot
                                    ),
                                    MathSelection(
                                        cursor.path.toList(),
                                        start,
                                        end
                                    )
                                )
                            }

                            cursor.offset <=
                                    start -> {

                                if (
                                    requiresStructuralTraversal(
                                        cursor.path,
                                        part
                                    ) &&
                                    sequence.children
                                        .isNotEmpty()
                                ) {

                                    firstSemanticTargetInSequence(
                                        cursor.path
                                    )

                                } else {

                                    afterSlotEditingTarget(
                                        cursor.path
                                    )
                                }
                            }

                            else -> {

                                NavigationTarget(
                                    cursor.copy(
                                        offset = start,
                                        scope =
                                            MathContentScope.Slot
                                    )
                                )
                            }
                        }
                    }
                }
            }
        }

        /*
         * BORDE DERECHO DE MathExpression REAL.
         */
        if (
            cursor.level ==
            MathCursorLevel.PARENT_BOUNDARY
        ) {

            val sequence =
                sequenceAt(
                    cursor.path
                ) ?: return null

            val nodeIndex =
                cursor.offset - 1

            val node =
                sequence.children
                    .getOrNull(
                        nodeIndex
                    ) ?: return null

            if (forward) {

                return previousSemanticBeforeNodeBoundary(
                    cursor.path,
                    nodeIndex
                )
            }

            val parts =
                node.navigationTraversalParts()

            return when {

                parts.isNotEmpty() -> {

                    declaredNavigationTarget(
                        cursor.path,
                        nodeIndex,
                        parts.first()
                    )
                }

                node.interactionCapabilities
                    .editable -> {

                    nodeEditTarget(
                        cursor.path,
                        nodeIndex,
                        atRight = true
                    )
                }

                else -> {

                    nextSemanticAfterNode(
                        cursor.path,
                        nodeIndex
                    )
                }
            }
        }

        /*
         * NEUTRAL.
         *
         * Aquí viven únicamente los FIN reales
         * y fronteras libres.
         */
        val sequence =
            sequenceAt(
                cursor.path
            ) ?: return null

        return if (forward) {

            if (
                cursor.offset >=
                sequence.children.size
            ) {

                null

            } else {

                lastSemanticInsideNode(
                    cursor.path,
                    cursor.offset
                )
            }

        } else {

            /*
             * Caso crítico de LargeOperator.
             *
             * FIN derecho
             *
             * LEFT
             *
             * □ ]
             */
            if (
                cursor.offset ==
                sequence.children.size &&
                sequence.hasContinuationInsertion()
            ) {

                continuationTarget(
                    cursor.path
                )

            } else if (
                cursor.offset <= 0
            ) {

                null

            } else {

                nodeBoundaryTarget(
                    cursor.path,
                    cursor.offset - 1
                )
            }
        }
    }

    private fun navigationPart(
        path: List<MathCursorStep>
    ): MathNavigationPart? {

        val step =
            path.lastOrNull()
                ?: return null

        return sequenceAt(
            path.dropLast(1)
        )
            ?.children
            ?.getOrNull(
                step.nodeIndex
            )
            ?.navigationParts()
            ?.firstOrNull {
                it.id == step.slot
            }
    }

    private fun parentAfterTarget(
        path: List<MathCursorStep>
    ): NavigationTarget? {

        val step =
            path.lastOrNull()
                ?: return null

        return NavigationTarget(
            MathCursor(
                path.dropLast(1),
                step.nodeIndex + 1,
                MathCursorLevel.NEUTRAL,
                null
            )
        )
    }

    /**
     * Estado de edición canónico de una región completa.
     *
     * Se utiliza para PARENT / CHILD editables que no necesitan
     * recorrer estructuralmente cada nodo de su Sequence.
     */
    private fun slotEditingTarget(
        path: List<MathCursorStep>,
        atRight: Boolean = true
    ): NavigationTarget? {

        val sequence =
            sequenceAt(path)
                ?: return null

        return NavigationTarget(
            MathCursor(
                path = path,
                offset =
                    if (atRight) {
                        sequence.children.size
                    } else {
                        0
                    },
                level = MathCursorLevel.INTERIOR,
                scope = MathContentScope.Slot
            )
        )
    }

    /**
     * Borde derecho de un MathExpression contenido
     * directamente dentro de una Sequence.
     *
     * Ejemplos:
     *
     * x ]
     * + ]
     * Fraction ]
     *
     * No existe borde izquierdo.
     */
    private fun nodeBoundaryTarget(
        path: List<MathCursorStep>,
        nodeIndex: Int
    ): NavigationTarget? {

        val sequence =
            sequenceAt(path)
                ?: return null

        val node =
            sequence.children
                .getOrNull(nodeIndex)
                ?: return null

        if (
            !node.interactionCapabilities.selectable
        ) {

            val parts =
                node.navigationTraversalParts()

            if (
                parts.isNotEmpty()
            ) {

                return declaredNavigationTarget(
                    path,
                    nodeIndex,
                    parts.first()
                )
            }

            if (
                node.interactionCapabilities.editable
            ) {

                return nodeEditTarget(
                    path,
                    nodeIndex,
                    atRight = true
                )
            }

            return nextSemanticAfterNode(
                path,
                nodeIndex
            )
        }

        return NavigationTarget(
            MathCursor(
                path = path,
                offset = nodeIndex + 1,
                level = MathCursorLevel.PARENT_BOUNDARY,
                scope = null
            )
        )
    }

    /**
     * Estado de edición de un PARENT atómico.
     *
     * El borde derecho y la edición son estados distintos:
     *
     * x ]
     * ↓ LEFT
     * editar x
     */
    private fun nodeEditTarget(
        path: List<MathCursorStep>,
        nodeIndex: Int,
        atRight: Boolean = true
    ): NavigationTarget? {

        val node =
            sequenceAt(path)
                ?.children
                ?.getOrNull(nodeIndex)
                ?: return null

        if (
            !node.interactionCapabilities.editable
        ) {
            return null
        }

        return NavigationTarget(
            MathCursor(
                path = path,
                offset =
                    if (atRight) {
                        nodeIndex + 1
                    } else {
                        nodeIndex
                    },
                level = MathCursorLevel.INTERIOR,
                scope =
                    MathContentScope.Atom(
                        nodeIndex,
                        nodeIndex + 1
                    )
            )
        )
    }

    /**
     * Indica si una región debe recorrerse
     * como árbol estructural.
     *
     * SUBSTRUCTURE siempre se recorre nodo por nodo.
     *
     * Un PARENT / CHILD normal se edita como región completa,
     * salvo que contenga una STRUCTURE interna o la
     * continuación especial de LargeOperator.
     */
    private fun requiresStructuralTraversal(
        path: List<MathCursorStep>,
        part: MathNavigationPart?
    ): Boolean {

        val sequence =
            sequenceAt(path)
                ?: return false

        return part?.role ==
                MathNavigationRole.SUBSTRUCTURE ||
                sequence.hasContinuationInsertion() ||
                sequence.children.any {
                    it.navigationParts()
                        .isNotEmpty()
                }
    }

    /**
     * Primer estado semántico al continuar LEFT
     * dentro de una Sequence.
     *
     * LEFT recorre los siblings de derecha -> izquierda.
     *
     * Regla importante:
     *
     * Si estamos dentro de un PARENT / CHILD editable
     * que contiene UNA única STRUCTURE ocupando todo
     * su contenido, el borde del PARENT ya representó
     * visualmente a esa estructura.
     *
     * Ejemplo:
     *
     * body = x²
     *
     * Ya recorrimos:
     *
     * x²]
     * x²|
     * {x²}
     * |x²
     *
     * Por tanto NO repetimos:
     *
     * x²]
     *
     * Entramos directamente en la primera parte
     * interna de Scripts:
     *
     * ²]
     *
     * SUBSTRUCTURE queda excluida de esta regla,
     * porque dentro de numeradores, denominadores,
     * matrices, etc. sí queremos descubrir las
     * STRUCTURE que contiene.
     */
    private fun firstSemanticTargetInSequence(
        path: List<MathCursorStep>
    ): NavigationTarget? {

        val sequence =
            sequenceAt(path)
                ?: return null

        if (
            sequence.hasContinuationInsertion()
        ) {

            return continuationTarget(
                path
            )
        }

        if (
            sequence.children.isEmpty()
        ) {

            return slotEditingTarget(
                path,
                atRight = false
            )
        }

        val part =
            navigationPart(
                path
            )

        /*
         * PARENT / CHILD compuesto por una única STRUCTURE.
         *
         * Su borde exterior ya fue visitado.
         *
         * Saltamos el borde duplicado de la STRUCTURE
         * y entramos directamente en su jerarquía.
         */
        if (
            part != null &&
            part.role !=
            MathNavigationRole.SUBSTRUCTURE &&
            sequence.children.size == 1
        ) {

            val node =
                sequence.children[0]

            val parts =
                node.navigationTraversalParts()

            if (
                parts.isNotEmpty()
            ) {

                return declaredNavigationTarget(
                    structurePath = path,
                    nodeIndex = 0,
                    part = parts.first()
                )
            }
        }

        /*
         * Caso normal.
         *
         * LEFT empieza por el elemento situado
         * más a la derecha de esta Sequence.
         */
        return nodeBoundaryTarget(
            path,
            sequence.children.lastIndex
        )
    }

    /**
     * Último estado semántico contenido por un nodo.
     *
     * Se utiliza por RIGHT para recorrer exactamente
     * el árbol en sentido inverso.
     */
    private fun lastSemanticInsideNode(
        path: List<MathCursorStep>,
        nodeIndex: Int
    ): NavigationTarget? {

        val node =
            sequenceAt(path)
                ?.children
                ?.getOrNull(nodeIndex)
                ?: return null

        val parts =
            node.navigationTraversalParts()

        if (
            parts.isNotEmpty()
        ) {

            val lastPart =
                parts.last()

            return enterSlotTarget(
                path +
                        MathCursorStep(
                            nodeIndex,
                            lastPart.id
                        ),
                forward = true
            )
        }

        val interaction =
            node.interactionCapabilities

        return when {

            interaction.editable -> {

                /*
                 * Último estado LEFT:
                 *
                 * |x
                 */
                nodeEditTarget(
                    path,
                    nodeIndex,
                    atRight = false
                )
            }

            interaction.selectable -> {

                nodeBoundaryTarget(
                    path,
                    nodeIndex
                )
            }

            else ->
                null
        }
    }

    /**
     * Último estado semántico de una Sequence.
     *
     * La continuación vacía de LargeOperator es un estado
     * editable real y permanece sibling del operador.
     */
    private fun lastSemanticTargetInSequence(
        path: List<MathCursorStep>
    ): NavigationTarget? {

        val sequence =
            sequenceAt(path)
                ?: return null

        if (
            sequence.children.isEmpty()
        ) {

            return slotEditingTarget(
                path,
                atRight = false
            )
        }

        /*
         * RIGHT entra desde la izquierda.
         */
        return lastSemanticInsideNode(
            path,
            0
        )
    }

    /**
     * Continúa el recorrido semántico LEFT después
     * de terminar completamente un nodo.
     *
     * Si existe otro sibling:
     *
     * vamos a SU borde derecho.
     *
     * Si no existe:
     *
     * terminamos la región actual y subimos
     * recursivamente al árbol exterior.
     */
    private fun nextSemanticAfterNode(
        path: List<MathCursorStep>,
        nodeIndex: Int
    ): NavigationTarget? {

        val sequence =
            sequenceAt(path)
                ?: return null

        val previousIndex =
            nodeIndex - 1

        /*
         * LEFT:
         * derecha -> izquierda.
         */
        if (
            previousIndex >= 0
        ) {

            return nodeBoundaryTarget(
                path,
                previousIndex
            )
        }

        if (
            path.isNotEmpty()
        ) {

            return afterSlotEditingTarget(
                path
            )
        }

        /*
         * FIN izquierdo real.
         */
        return NavigationTarget(
            MathCursor(
                path = emptyList(),
                offset = 0,
                level = MathCursorLevel.NEUTRAL,
                scope = null
            )
        )
    }

    /**
     * Estado inmediatamente anterior a un borde
     * de nodo cuando RIGHT recorre el árbol
     * en sentido inverso.
     */
    private fun previousSemanticBeforeNodeBoundary(
        path: List<MathCursorStep>,
        nodeIndex: Int
    ): NavigationTarget? {

        val sequence =
            sequenceAt(path)
                ?: return null

        val nextIndex =
            nodeIndex + 1

        /*
         * RIGHT:
         * izquierda -> derecha.
         */
        if (
            nextIndex <
            sequence.children.size
        ) {

            return lastSemanticInsideNode(
                path,
                nextIndex
            )
        }

        if (
            sequence.hasContinuationInsertion()
        ) {

            return continuationEditingTarget(
                path
            )
        }

        /*
         * Terminamos los PARENT internos.
         *
         * En reversa regresamos a:
         *
         * |numerator
         */
        if (
            path.isNotEmpty()
        ) {

            return slotEditingTarget(
                path,
                atRight = false
            )
        }

        /*
         * FIN derecho real.
         */
        return NavigationTarget(
            MathCursor(
                path = emptyList(),
                offset = sequence.children.size,
                level = MathCursorLevel.NEUTRAL,
                scope = null
            )
        )
    }

    /**
     * Al terminar una región por la derecha decidimos
     * qué ocurre usando sus capacidades, no su tipo.
     *
     * selectable = true:
     * aparece su único checkpoint estructural,
     * el borde derecho.
     *
     * selectable = false:
     * se omite ese checkpoint y continuamos
     * con la navegación anterior.
     *
     * No existe borde izquierdo.
     */
    private fun slotExitRightTarget(
        path: List<MathCursorStep>
    ): NavigationTarget? {

        val part =
            navigationPart(path)
                ?: return null

        /*
         * SUBSTRUCTURE:
         *
         * numerator|
         * {numerator}
         * |numerator
         *
         * NO:
         *
         * numerator]
         */
        if (
            part.role ==
            MathNavigationRole.SUBSTRUCTURE
        ) {

            return previousSlotEditingTarget(
                path
            )
        }

        return if (
            part.interaction.selectable
        ) {

            boundaryTarget(
                path
            )

        } else {

            previousSlotEditingTarget(
                path
            )
        }
    }


    /**
     * RIGHT recorre las partes de una STRUCTURE
     * exactamente al revés que LEFT.
     *
     * Si existe una parte anterior,
     * entramos en SU último estado semántico.
     *
     * Si no existe:
     * regresamos al borde derecho de la
     * STRUCTURE completa.
     */
    private fun previousSlotEditingTarget(
        path: List<MathCursorStep>
    ): NavigationTarget? {

        val step =
            path.lastOrNull()
                ?: return null

        val structurePath =
            path.dropLast(1)

        val structure =
            sequenceAt(
                structurePath
            )
                ?.children
                ?.getOrNull(
                    step.nodeIndex
                )
                ?: return null

        val parts =
            structure.navigationTraversalParts()

        val currentIndex =
            parts.indexOfFirst {
                it.id == step.slot
            }

        if (
            currentIndex < 0
        ) {
            return null
        }

        val previous =
            parts.getOrNull(
                currentIndex - 1
            )

        if (
            previous != null
        ) {

            return enterSlotTarget(
                structurePath +
                        MathCursorStep(
                            step.nodeIndex,
                            previous.id
                        ),
                forward = true
            )
        }

        /*
         * Era la primera parte.
         *
         * Volvemos al checkpoint de la
         * STRUCTURE completa.
         */
        return nodeBoundaryTarget(
            structurePath,
            step.nodeIndex
        )
    }

    /**
     * Estado editable del PARENT virtual situado
     * a la derecha de LargeOperator.
     *
     * No añade ningún nodo al AST.
     *
     * Visualmente:
     *
     * □ |
     */
    private fun continuationEditingTarget(
        path: List<MathCursorStep>
    ): NavigationTarget? {

        val sequence =
            sequenceAt(path)
                ?: return null

        if (
            !sequence.hasContinuationInsertion()
        ) {
            return null
        }

        val end =
            sequence.children.size

        return NavigationTarget(
            MathCursor(
                path = path,
                offset = end,
                level = MathCursorLevel.INTERIOR,
                scope =
                    MathContentScope.Atom(
                        end,
                        end
                    )
            )
        )
    }

    /**
     * Borde derecho del PARENT virtual
     * de continuación.
     *
     * Visualmente:
     *
     * □ ]
     *
     * El estado se distingue de FIN mediante
     * navigationInsertion.
     */
    private fun continuationTarget(
        path: List<MathCursorStep>
    ): NavigationTarget? {

        val sequence =
            sequenceAt(path)
                ?: return null

        if (
            !sequence.hasContinuationInsertion()
        ) {
            return null
        }

        return NavigationTarget(
            cursor =
                MathCursor(
                    path = path,
                    offset = sequence.children.size,
                    level = MathCursorLevel.NEUTRAL,
                    scope = null
                ),
            insertion = true
        )
    }

    /**
     * Borde derecho de un CHILD.
     */
    private fun boundaryTarget(
        path: List<MathCursorStep>
    ): NavigationTarget? {

        if (
            path.isEmpty() ||
            sequenceAt(path) == null
        ) {
            return null
        }

        val step =
            path.last()

        val outside =
            MathCursor(
                path.dropLast(1),
                step.nodeIndex + 1,
                MathCursorLevel.CHILD_BOUNDARY,
                null
            )

        return NavigationTarget(
            outside,
            boundary =
                NavigationBoundary(
                    path.toList(),
                    true,
                    expression,
                    outside
                )
        )
    }

    /**
     * Decide cómo comienza el recorrido de una parte
     * declarada dentro de una STRUCTURE.
     *
     * selectable:
     * primero encontramos su borde derecho.
     *
     * editable:
     * después podremos entrar en su contenido.
     *
     * Una parte puede ser:
     *
     * SUBSTRUCTURE
     * PARENT
     * CHILD
     *
     * El role no decide por sí mismo
     * cómo se interactúa con ella.
     */
    private fun declaredNavigationTarget(
        structurePath: List<MathCursorStep>,
        nodeIndex: Int,
        part: MathNavigationPart
    ): NavigationTarget? {

        val path =
            structurePath +
                    MathCursorStep(
                        nodeIndex,
                        part.id
                    )

        /*
         * SUBSTRUCTURE no posee ].
         *
         * Fraction] -> numerator|
         */
        if (
            part.role ==
            MathNavigationRole.SUBSTRUCTURE
        ) {

            return if (
                part.interaction.editable
            ) {

                enterSlotTarget(
                    path,
                    forward = false
                )

            } else {

                afterSlotEditingTarget(
                    path
                )
            }
        }

        return when {

            part.interaction.selectable -> {

                boundaryTarget(
                    path
                )
            }

            part.interaction.editable -> {

                enterSlotTarget(
                    path,
                    forward = false
                )
            }

            else -> {

                afterSlotEditingTarget(
                    path
                )
            }
        }
    }

    /**
     * LEFT terminó completamente una parte.
     *
     * Continúa con la siguiente parte declarada.
     *
     * Cuando termina la última parte:
     * termina también la STRUCTURE actual
     * y continúa recursivamente con el
     * siguiente sibling exterior.
     */
    private fun afterSlotEditingTarget(
        path: List<MathCursorStep>
    ): NavigationTarget? {

        if (
            path.isEmpty() ||
            sequenceAt(path) == null
        ) {
            return null
        }

        val step =
            path.last()

        val structurePath =
            path.dropLast(1)

        val structure =
            sequenceAt(
                structurePath
            )
                ?.children
                ?.getOrNull(
                    step.nodeIndex
                )
                ?: return null

        val parts =
            structure.navigationTraversalParts()

        val currentIndex =
            parts.indexOfFirst {
                it.id == step.slot
            }

        if (
            currentIndex < 0
        ) {
            return null
        }

        val next =
            parts.getOrNull(
                currentIndex + 1
            )

        if (
            next != null
        ) {

            return declaredNavigationTarget(
                structurePath,
                step.nodeIndex,
                next
            )
        }

        /*
         * Terminamos completamente la STRUCTURE.
         *
         * No volvemos a mostrar su borde porque
         * ese borde fue el PRIMER checkpoint
         * cuando entramos en ella.
         *
         * Continuamos con el siguiente sibling
         * de la Sequence exterior.
         */
        return nextSemanticAfterNode(
            structurePath,
            step.nodeIndex
        )
    }

    /**
     * Entra en una región declarada por una STRUCTURE.
     *
     * LEFT entra por el INICIO semántico.
     * RIGHT entra por el FINAL semántico.
     *
     * Esto permite reciprocidad exacta.
     */
    private fun enterSlotTarget(
        path: List<MathCursorStep>,
        forward: Boolean
    ): NavigationTarget? {

        val part =
            navigationPart(
                path
            )

        if (
            part != null &&
            !part.interaction.editable
        ) {

            return if (forward) {

                if (
                    part.role !=
                    MathNavigationRole.SUBSTRUCTURE &&
                    part.interaction.selectable
                ) {

                    boundaryTarget(
                        path
                    )

                } else {

                    previousSlotEditingTarget(
                        path
                    )
                }

            } else {

                afterSlotEditingTarget(
                    path
                )
            }
        }

        val sequence =
            sequenceAt(path)
                ?: return null

        val structural =
            requiresStructuralTraversal(
                path,
                part
            )

        return if (forward) {

            /*
             * RIGHT reproduce LEFT al revés.
             *
             * Si hay PARENT internos, comienza
             * por el extremo izquierdo interno.
             */
            if (
                structural &&
                sequence.children.isNotEmpty()
            ) {

                lastSemanticTargetInSequence(
                    path
                )

            } else {

                slotEditingTarget(
                    path,
                    atRight = false
                )
            }

        } else {

            /*
             * LEFT entra primero por:
             *
             * numerator|
             */
            slotEditingTarget(
                path,
                atRight = true
            )
        }
    }

    fun stepRight(): Boolean = navigateNext()
    fun stepLeft(): Boolean = navigatePrevious()
    fun selectCurrentSlot() {
        val (start, end) = if (navigationLevel == MathCursorLevel.INTERIOR) contentScopeBounds() else 0 to (sequenceAt(cursor.path)?.children?.size ?: 0)
        select(cursor.path, start, end)
    }

    /** Tab/IME navigation visits nested slots before leaving the current response field. */
    fun nextSlot(): Boolean = moveToSlot(forward = true)
    fun previousSlot(): Boolean = moveToSlot(forward = false)

    private fun moveToSlot(forward: Boolean): Boolean {
        val target = slotNavigationTarget(forward) ?: return false
        selectionRange = null
        cursor = target
        return true
    }

    private fun slotNavigationTarget(forward: Boolean): MathCursor? {
        val sequence = sequenceAt(cursor.path) ?: return null
        val indices = if (forward) cursor.offset until sequence.children.size else (cursor.offset - 1 downTo 0)
        val index = indices.firstOrNull { sequence.children[it].childSlots().isNotEmpty() }
        return if (index != null) {
            val slots = sequence.children[index].childSlots()
            val target = if (forward) slots.first() else slots.last()
            MathCursor(cursor.path + MathCursorStep(index, target.first), if (forward) 0 else target.second.children.size)
        } else if (cursor.path.isNotEmpty()) {
            adjacentSlotCursor(forward)
        } else {
            null
        }
    }

    private fun moveBetweenSlots(forward: Boolean) {
        adjacentSlotCursor(forward)?.let { cursor = it }
    }

    private fun adjacentSlotCursor(forward: Boolean): MathCursor? {
        if (cursor.path.isEmpty()) return null
        val step = cursor.path.last()
        val parentPath = cursor.path.dropLast(1)
        val parent = sequenceAt(parentPath) ?: return null
        val slots = parent.children.getOrNull(step.nodeIndex)?.childSlots() ?: return null
        val current = slots.indexOfFirst { it.first == step.slot }
        val target = current + if (forward) 1 else -1
        return if (target in slots.indices) MathCursor(parentPath + step.copy(slot = slots[target].first), if (forward) 0 else slots[target].second.children.size)
            else MathCursor(parentPath, step.nodeIndex + if (forward) 1 else 0)
    }

    fun exitCurrentSlot() {
        selectionRange = null
        if (cursor.path.isNotEmpty()) cursor = MathCursor(cursor.path.dropLast(1), cursor.path.last().nodeIndex + 1)
    }

    /**
     * Android text deletion is bounded by the active slot; an empty slot remains editable.
     * Genuine AST selections are deleted once, without also deleting surrounding siblings.
     */
    fun deleteSurroundingTextInCurrentSlot(beforeLength: Int, afterLength: Int, inCodePoints: Boolean = false): Boolean {
        if (beforeLength < 0 || afterLength < 0) return false
        if (beforeLength == 0 && afterLength == 0) return true
        if (selectionRange != null) {
            takeSelection()
            return true
        }
        val sequence = sequenceAt(cursor.path) ?: return false
        val texts = sequence.children.map { leafText(it) ?: "\uFFFC" }
        val text = texts.joinToString("")
        val caret = texts.take(cursor.offset).sumOf { it.length }
        val scope = if (navigationLevel == MathCursorLevel.INTERIOR) contentScopeBounds() else 0 to sequence.children.size
        val scopeStart = texts.take(scope.first).sumOf { it.length }
        val scopeEnd = texts.take(scope.second).sumOf { it.length }
        val start = if (inCodePoints) Character.offsetByCodePoints(text, caret,
            -minOf(beforeLength, Character.codePointCount(text, scopeStart, caret)))
            else caret - minOf(beforeLength, caret - scopeStart)
        val end = if (inCodePoints) Character.offsetByCodePoints(text, caret,
            minOf(afterLength, Character.codePointCount(text, caret, scopeEnd)))
            else caret + minOf(afterLength, scopeEnd - caret)
        if (inCodePoints && (!wellFormedUtf16(text, start, caret) || !wellFormedUtf16(text, caret, end))) return false
        return deleteTextRangeInCurrentSlot(start, end)
    }

    /** Offsets refer to the active slot's UTF-16 leaf-text/object projection, never LaTeX. */
    fun deleteTextRangeInCurrentSlot(startUtf16: Int, endUtf16: Int): Boolean =
        deleteTextRangesInCurrentSlot(listOf(startUtf16 to endUtf16))

    /** Deletes disjoint native surrounding ranges in one AST update, retaining text between them. */
    fun deleteTextRangesInCurrentSlot(ranges: List<Pair<Int, Int>>): Boolean {
        if (ranges.any { (start, end) -> start < 0 || end < start }) return false
        if (ranges.isEmpty()) return true
        if (selectionRange != null) { takeSelection(); return true }
        val sequence = sequenceAt(cursor.path) ?: return false
        val texts = sequence.children.map { leafText(it) ?: "\uFFFC" }
        val text = texts.joinToString("")
        val intervals = mutableListOf<Pair<Int, Int>>()
        ranges.mapNotNull { (rawStart, rawEnd) ->
            var start = rawStart.coerceAtMost(text.length)
            var end = rawEnd.coerceAtMost(text.length)
            if (start == end) return@mapNotNull null
            if (start > 0 && start < text.length && Character.isHighSurrogate(text[start - 1]) && Character.isLowSurrogate(text[start])) start--
            if (end > 0 && end < text.length && Character.isHighSurrogate(text[end - 1]) && Character.isLowSurrogate(text[end])) end++
            start to end
        }.sortedBy { it.first }.forEach { interval ->
            val previous = intervals.lastOrNull()
            if (previous != null && interval.first <= previous.second) {
                intervals[intervals.lastIndex] = previous.first to maxOf(previous.second, interval.second)
            } else intervals.add(interval)
        }
        if (intervals.isEmpty()) return true
        val caret = texts.take(cursor.offset).sumOf { it.length }
        val children = mutableListOf<MathExpression>()
        var position = 0
        var cursorOffset = 0
        var removedText = false
        fun retain(node: MathExpression, start: Int, end: Int, value: String? = null) {
            if (start == end) return
            if (value == null) children.add(node) else children.add(withLeafText(node, value))
            if (end <= caret) cursorOffset++
        }
        sequence.children.forEachIndexed { index, node ->
            val value = texts[index]
            val nodeEnd = position + value.length
            val overlapping = intervals.filter { it.first < nodeEnd && it.second > position }
            if (leafText(node) == null || overlapping.isEmpty()) {
                retain(node, position, nodeEnd)
            } else {
                var retainedFrom = position
                overlapping.forEach { (start, end) ->
                    val cutStart = start.coerceAtLeast(position)
                    val cutEnd = end.coerceAtMost(nodeEnd)
                    if (cutStart > retainedFrom) retain(node, retainedFrom, cutStart,
                        value.substring(retainedFrom - position, cutStart - position))
                    if (cutEnd > cutStart) removedText = true
                    retainedFrom = cutEnd
                }
                if (retainedFrom < nodeEnd) retain(node, retainedFrom, nodeEnd, value.substring(retainedFrom - position))
            }
            position = nodeEnd
        }
        if (!removedText) return true
        val updated = expressionReplacingSequence(cursor.path, sequence.copy(children = children))
        if (nodeCount(updated) > 4_096 || MathLatexSerializer.serialize(updated).length > 32_768) return false
        expression = updated
        val scope = (cursor.scope as? MathContentScope.Atom)?.let {
            it.copy(end = maxOf(it.start, it.end + children.size - sequence.children.size))
        } ?: cursor.scope
        cursor = cursor.copy(offset = cursorOffset, scope = scope)
        selectionRange = null
        return true
    }

    private fun wellFormedUtf16(value: String, start: Int, end: Int): Boolean {
        var index = start
        while (index < end) {
            val char = value[index]
            if (Character.isHighSurrogate(char)) {
                if (index + 1 >= end || !Character.isLowSurrogate(value[index + 1])) return false
                index += 2
            } else {
                if (Character.isLowSurrogate(char)) return false
                index++
            }
        }
        return true
    }

    /** Native collapsed selection becomes an exact AST boundary inside the active slot. */
    fun setTextCursorInCurrentSlot(offsetUtf16: Int): Boolean {
        if (offsetUtf16 < 0) return false
        val sequence = sequenceAt(cursor.path) ?: return false
        val texts = sequence.children.map { leafText(it) ?: "\uFFFC" }
        val text = texts.joinToString("")
        var target = offsetUtf16.coerceAtMost(text.length)
        if (target > 0 && target < text.length && Character.isHighSurrogate(text[target - 1]) && Character.isLowSurrogate(text[target])) target++
        if (texts.take(cursor.offset).sumOf { it.length } == target && selectionRange == null) return true
        var offset = 0
        sequence.children.forEachIndexed { index, node ->
            val value = texts[index]
            val end = offset + value.length
            val position = when {
                target == offset -> index
                target == end -> index + 1
                target in (offset + 1) until end -> {
                    if (leafText(node) == null || nodeCount(expression) >= 4_096) return false
                    val split = target - offset
                    replaceSequence(cursor.path, sequence.copy(children = sequence.children.take(index) +
                        listOf(withLeafText(node, value.take(split)), withLeafText(node, value.drop(split))) + sequence.children.drop(index + 1)))
                    index + 1
                }
                else -> null
            }
            if (position != null) {
                // A native selection update may add only private IME metadata at the same
                // AST caret. Preserve its exterior slot intent until the cursor really moves.
                val targetCursor = cursor.copy(offset = position)
                if (targetCursor != cursor) cursor = targetCursor
                selectionRange = null
                return true
            }
            offset = end
        }
        val targetCursor = cursor.copy(offset = sequence.children.size)
        if (targetCursor != cursor) cursor = targetCursor
        selectionRange = null
        return true
    }

    /**
     * Touch elige edición explícitamente.
     *
     * El cuadro virtual después de LargeOperator
     * funciona como un PARENT editable obligatorio.
     */
    fun placeTextCursor(
        position: MathCursor,
        leafTextOffset: Int? = null,
        sequenceInsertion: Boolean = false
    ): Boolean {

        val sequence =
            sequenceAt(
                position.path
            ) ?: return false

        /*
         * Touch dentro del cuadro de continuación.
         *
         * Entramos DIRECTAMENTE en edición:
         *
         * □ |
         *
         * No en:
         *
         * □ ]
         */
        if (
            sequenceInsertion &&
            position.offset ==
            sequence.children.size &&
            sequence.hasContinuationInsertion()
        ) {

            val end =
                sequence.children.size

            cursor =
                position.copy(
                    offset = end,
                    level =
                        MathCursorLevel.INTERIOR,
                    scope =
                        MathContentScope.Atom(
                            end,
                            end
                        )
                )

            selectionRange =
                null

            navigationInsertion =
                null

            return true
        }

        if (
            leafTextOffset == null &&
            (
                    position.path.isEmpty() ||
                            sequence.children.any {
                                it.navigationChildren()
                                    .isNotEmpty()
                            }
                    )
        ) {

            cursor =
                position.copy(
                    level =
                        MathCursorLevel.NEUTRAL,
                    scope = null
                )

            selectionRange =
                null

            return true
        }

        val index =
            if (
                leafTextOffset != null
            ) {

                position.offset

            } else {

                (
                        position.offset - 1
                        )
                    .coerceAtLeast(
                        0
                    )
            }

        cursor =
            position.copy(
                level =
                    MathCursorLevel.INTERIOR,

                scope =
                    if (
                        position.path.isEmpty()
                    ) {

                        MathContentScope.Atom(
                            index.coerceAtMost(
                                sequence.children.size
                            ),
                            (
                                    index + 1
                                    ).coerceAtMost(
                                    sequence.children.size
                                )
                        )

                    } else {

                        MathContentScope.Slot
                    }
            )

        selectionRange =
            null

        if (
            leafTextOffset == null
        ) {
            return true
        }

        val offset =
            sequence.children
                .take(
                    position.offset
                )
                .sumOf {
                    (
                            leafText(it)
                                ?: "\uFFFC"
                            ).length
                } +
                    leafTextOffset

        return setTextCursorInCurrentSlot(
            offset
        )
    }

    /**
     * Replaces text atomically without touching another slot or implicitly removing a structure.
     * A partial numeric edit creates AST boundaries around the edit, so the cursor stays exact.
     * replaceStructures is reserved for a live composition that owns the structural markers.
     */
    fun replaceTextRangeInCurrentSlot(startUtf16: Int, endUtf16: Int, replacement: String, replaceStructures: Boolean = false): Boolean {
        if (startUtf16 < 0 || endUtf16 < startUtf16) return false
        val originalExpression = expression
        val originalCursor = cursor
        val originalSelection = selectionRange
        val originalBoundary = navigationBoundary
        val originalInsertion = navigationInsertion
        val originalEdge = navigationEdge
        fun rollback(): Boolean {
            expression = originalExpression
            cursor = originalCursor
            selectionRange = originalSelection
            navigationBoundary = originalBoundary
            navigationInsertion = originalInsertion
            navigationEdge = originalEdge
            return false
        }
        if (selectionRange != null) {
            takeSelection()
            return if (insertText(replacement)) true else rollback()
        }
        val sequence = sequenceAt(cursor.path) ?: return false
        val text = sequence.children.joinToString("") { leafText(it) ?: "\uFFFC" }
        var start = startUtf16.coerceAtMost(text.length)
        var end = endUtf16.coerceAtMost(text.length)
        if (start == end && replacement.isEmpty()) return true
        // Android's UTF-16 protocol can request half a surrogate. Never create invalid text.
        val startInsidePair = start > 0 && start < text.length && Character.isHighSurrogate(text[start - 1]) && Character.isLowSurrogate(text[start])
        if (start == end && startInsidePair) {
            start++
            end++
        } else {
            if (startInsidePair) start--
            if (end > 0 && end < text.length && Character.isHighSurrogate(text[end - 1]) && Character.isLowSurrogate(text[end])) end++
        }
        val before = mutableListOf<MathExpression>()
        val after = mutableListOf<MathExpression>()
        var removedText = false
        var offset = 0
        sequence.children.forEach { node ->
            val leaf = leafText(node)
            val value = leaf ?: "\uFFFC"
            val nodeEnd = offset + value.length
            when {
                nodeEnd <= start -> before.add(node)
                offset >= end -> after.add(node)
                leaf == null -> if (replaceStructures) removedText = true else after.add(node)
                else -> {
                    val prefix = value.take((start - offset).coerceIn(0, value.length))
                    val suffix = value.drop((end - offset).coerceIn(0, value.length))
                    if (prefix.length + suffix.length < value.length) removedText = true
                    if (prefix.isNotEmpty()) before.add(withLeafText(node, prefix))
                    if (suffix.isNotEmpty()) after.add(withLeafText(node, suffix))
                }
            }
            offset = nodeEnd
        }
        if (!removedText && replacement.isEmpty()) return true
        val numericScope = (originalCursor.scope as? MathContentScope.Atom)?.takeIf {
            originalCursor.level == MathCursorLevel.INTERIOR && it.start < it.end &&
                sequence.children.subList(it.start, it.end).all { child -> child is MathExpression.Number } &&
                start >= sequence.children.take(it.start).sumOf { child -> (leafText(child) ?: "\uFFFC").length } &&
                end <= sequence.children.take(it.end).sumOf { child -> (leafText(child) ?: "\uFFFC").length }
        }
        val editingSlot = originalCursor.path.isNotEmpty() && originalCursor.level == MathCursorLevel.INTERIOR && originalCursor.scope !is MathContentScope.Atom
        val retained = before + after
        // A collapsed append has no deletion or split: publish only the insertion's new AST.
        if (retained != sequence.children) replaceSequence(cursor.path, sequence.copy(children = retained))
        cursor = if (editingSlot || numericScope != null) cursor.copy(offset = before.size)
            else MathCursor(originalCursor.path, before.size, MathCursorLevel.NEUTRAL, null)
        if (nodeCount(expression) > 4_096 || !insertText(replacement)) return rollback()
        return true
    }

    private fun nextTextBoundary(
        value: String,
        offset: Int
    ): Int {

        val position =
            offset.coerceIn(
                0,
                value.length
            )

        if (
            position >=
            value.length
        ) {
            return value.length
        }

        return if (
            Character.isHighSurrogate(
                value[position]
            ) &&
            position + 1 <
            value.length &&
            Character.isLowSurrogate(
                value[position + 1]
            )
        ) {
            position + 2
        } else {
            position + 1
        }
    }

    private fun previousTextBoundary(
        value: String,
        offset: Int
    ): Int {

        val position =
            offset.coerceIn(
                0,
                value.length
            )

        if (
            position <= 0
        ) {
            return 0
        }

        return if (
            position >= 2 &&
            Character.isHighSurrogate(
                value[position - 2]
            ) &&
            Character.isLowSurrogate(
                value[position - 1]
            )
        ) {
            position - 2
        } else {
            position - 1
        }
    }

    /*
     * Navega carácter por carácter dentro
     * de una Sequence formada solamente
     * por hojas de texto.
     *
     * Ejemplos:
     *
     * x+1|
     * x+|1
     * x|+1
     * |x+1
     *
     * y:
     *
     * 22|
     * 2|2
     * |22
     *
     * No crea MathSelection.
     * No modifica el AST.
     */
    private fun textNavigationTarget(
        sequence: MathExpression.Sequence,
        start: Int,
        end: Int,
        forward: Boolean,
        exitLeft: () -> NavigationTarget?,
        exitRight: () -> NavigationTarget?
    ): NavigationTarget? {

        if (
            start > end ||
            start < 0 ||
            end > sequence.children.size
        ) {
            return null
        }

        /*
         * Este helper se utiliza solamente
         * para regiones puramente textuales.
         */
        if (
            sequence.children
                .subList(
                    start,
                    end
                )
                .any {
                    leafText(it) == null
                }
        ) {
            return null
        }

        val internalOffset =
            cursor.leafTextOffset

        /*
         * Estamos DENTRO de una hoja.
         *
         * Ejemplo:
         *
         * 2|2
         */
        if (
            internalOffset != null
        ) {

            val node =
                sequence.children
                    .getOrNull(
                        cursor.offset
                    )
                    ?: return null

            val text =
                leafText(node)
                    ?: return null

            if (forward) {

                val next =
                    nextTextBoundary(
                        text,
                        internalOffset
                    )

                /*
                 * Seguimos dentro de la
                 * misma hoja.
                 */
                if (
                    next <
                    text.length
                ) {

                    return NavigationTarget(
                        cursor.copy(
                            leafTextOffset =
                                next
                        )
                    )
                }

                /*
                 * Salimos por la derecha
                 * de la hoja.
                 *
                 * 2|2 -> 22|
                 */
                return NavigationTarget(
                    cursor.copy(
                        offset =
                            cursor.offset + 1,

                        leafTextOffset =
                            null
                    )
                )

            } else {

                val previous =
                    previousTextBoundary(
                        text,
                        internalOffset
                    )

                /*
                 * Seguimos dentro de la
                 * misma hoja.
                 */
                if (
                    previous > 0
                ) {

                    return NavigationTarget(
                        cursor.copy(
                            leafTextOffset =
                                previous
                        )
                    )
                }

                /*
                 * Salimos por la izquierda.
                 *
                 * 2|2 -> |22
                 */
                return NavigationTarget(
                    cursor.copy(
                        leafTextOffset =
                            null
                    )
                )
            }
        }

        /*
         * Estamos en una frontera normal
         * entre nodos.
         */
        if (forward) {

            if (
                cursor.offset >= end
            ) {
                return exitRight()
            }

            val nodeIndex =
                cursor.offset.coerceAtLeast(
                    start
                )

            val text =
                leafText(
                    sequence.children[
                        nodeIndex
                    ]
                ) ?: return null

            val next =
                nextTextBoundary(
                    text,
                    0
                )

            /*
             * Hoja con varios caracteres.
             *
             * |22 -> 2|2
             */
            if (
                next <
                text.length
            ) {

                return NavigationTarget(
                    cursor.copy(
                        offset =
                            nodeIndex,

                        leafTextOffset =
                            next
                    )
                )
            }

            /*
             * Hoja de un carácter.
             *
             * |x -> x|
             */
            return NavigationTarget(
                cursor.copy(
                    offset =
                        nodeIndex + 1,

                    leafTextOffset =
                        null
                )
            )

        } else {

            if (
                cursor.offset <= start
            ) {
                return exitLeft()
            }

            val nodeIndex =
                cursor.offset - 1

            val text =
                leafText(
                    sequence.children[
                        nodeIndex
                    ]
                ) ?: return null

            val previous =
                previousTextBoundary(
                    text,
                    text.length
                )

            /*
             * Hoja con varios caracteres.
             *
             * 22| -> 2|2
             */
            if (
                previous > 0
            ) {

                return NavigationTarget(
                    cursor.copy(
                        offset =
                            nodeIndex,

                        leafTextOffset =
                            previous
                    )
                )
            }

            /*
             * Hoja de un carácter.
             *
             * x+1| -> x+|1
             */
            return NavigationTarget(
                cursor.copy(
                    offset =
                        nodeIndex,

                    leafTextOffset =
                        null
                )
            )
        }
    }

    private fun leafText(node: MathExpression): String? = when (node) {
        is MathExpression.Number -> node.value
        is MathExpression.Variable -> node.name
        is MathExpression.Symbol -> node.value
        is MathExpression.Operator -> node.value
        is MathExpression.Relation -> node.value
        else -> null
    }

    private fun withLeafText(node: MathExpression, value: String): MathExpression = when (node) {
        is MathExpression.Number -> node.copy(value = value)
        is MathExpression.Variable -> node.copy(name = value)
        is MathExpression.Symbol -> node.copy(value = value)
        is MathExpression.Operator -> node.copy(value = value)
        is MathExpression.Relation -> node.copy(value = value)
        else -> node
    }

    /**
     * El borde derecho representa una parte estructural.
     *
     * deletable decide si esa parte puede desaparecer.
     *
     * Ya NO usamos REQUIRED / OPTIONAL como regla
     * principal de eliminación.
     *
     * Ejemplos:
     *
     * numerator ]
     * deletable = false
     * -> permanece
     *
     * radicand ]
     * deletable = false
     * -> permanece
     *
     * CHILD opcional ]
     * deletable = true
     * -> puede desaparecer
     */
    fun deleteBackwardAtNavigationBoundary(): Boolean {

        val boundary =
            activeNavigationBoundary()
                ?: return false

        if (
            selectionRange != null
        ) {
            return false
        }

        val step =
            boundary.slotPath.last()

        val parentPath =
            boundary.slotPath.dropLast(1)

        val node =
            sequenceAt(parentPath)
                ?.children
                ?.getOrNull(
                    step.nodeIndex
                )
                ?: return false

        val part =
            node.navigationParts()
                .firstOrNull {
                    it.id == step.slot
                }
                ?: return false

        /*
         * Existe el borde, pero esta región
         * es obligatoria y no puede desaparecer.
         *
         * Consumimos Backspace sin modificar el AST.
         */
        if (
            !part.interaction.deletable
        ) {
            return true
        }

        replaceNodeAt(
            parentPath,
            step.nodeIndex,
            part.remove()
        )

        /*
         * Si alguna futura región REQUIRED se declara
         * explícitamente deletable pero remove() la deja
         * vacía, conservamos su frontera correctamente.
         */
        if (
            part.requirement ==
            MathSlotRequirement.REQUIRED
        ) {

            val target =
                boundaryTarget(
                    boundary.slotPath
                ) ?: return true

            cursor =
                target.cursor

            navigationBoundary =
                target.boundary
        }

        return true
    }

    /**
     * Elimina según el nivel actual.
     *
     * - contenido seleccionado: elimina contenido
     * - borde de parte: usa interaction.deletable
     * - interior editable: elimina texto/contenido
     * - borde derecho de MathExpression:
     *   usa las capacidades del nodo completo
     */
    fun deleteBackwardAtCurrentLevel(): Boolean {

        if (
            selectionRange != null
        ) {

            takeSelection()

            return true
        }

        if (
            deleteBackwardAtNavigationBoundary()
        ) {
            return true
        }

        if (
            navigationLevel ==
            MathCursorLevel.INTERIOR
        ) {

            return deleteSurroundingTextInCurrentSlot(
                1,
                0
            )
        }

        val sequence =
            sequenceAt(
                cursor.path
            ) ?: return false

        if (
            cursor.offset == 0
        ) {
            return true
        }

        val nodeIndex =
            cursor.offset - 1

        val node =
            sequence.children
                .getOrNull(
                    nodeIndex
                )
                ?: return true

        /*
         * Estamos en el borde derecho del
         * MathExpression completo.
         *
         * Ejemplo:
         *
         * Fraction ]
         *
         * STRUCTURE Fraction:
         * deletable = true
         *
         * Por tanto desaparece completa.
         */
        if (
            !node.interactionCapabilities.deletable
        ) {
            return true
        }

        replaceNodeAt(
            cursor.path,
            nodeIndex,
            emptyList()
        )

        return true
    }

    fun deleteBackward() { deleteBackwardAtCurrentLevel() }

    private fun replaceNodeAt(path: List<MathCursorStep>, index: Int, replacement: List<MathExpression>) {
        val parent = sequenceAt(path) ?: return
        replaceSequence(path, parent.copy(children = parent.children.take(index) + replacement + parent.children.drop(index + 1)))
        cursor = MathCursor(path, index + replacement.size, MathCursorLevel.NEUTRAL, null)
    }

    private fun nodeCount(node: MathExpression): Int = when (node) {
        is MathExpression.Sequence -> 1 + node.children.sumOf { nodeCount(it) }
        else -> 1 + node.childSlots().sumOf { nodeCount(it.second) }
    }

    private fun structuralDepth(node: MathExpression): Int = when (node) {
        is MathExpression.Sequence -> node.children.maxOfOrNull { structuralDepth(it) } ?: 0
        else -> 1 + (node.childSlots().maxOfOrNull { structuralDepth(it.second) } ?: 0)
    }

    fun saveDraft(): String = JSONObject()
        .put("version", 1)
        .put("expression", encode(expression))
        .put("cursor", encodeCursor(cursor))
        .apply { selectionRange?.let { put("selection", JSONObject().put("path", encodePath(it.path)).put("start", it.start).put("end", it.end)) } }
        .apply { activeNavigationBoundary()?.let {
            put("navigationBoundary", JSONObject().put("path", encodePath(it.slotPath)).put("after", it.after))
        } }
        .apply { if (isAtSequenceInsertion) put("sequenceInsertion", true) }
        .toString()

    /** Invalid or unsupported drafts leave the current tree unchanged. */
    fun restoreDraft(
        draft: String
    ): Boolean {

        if (
            draft.length > 262_144 ||
            !boundedJsonNesting(draft)
        ) {
            return false
        }

        return try {

            val json =
                JSONObject(
                    draft
                )

            require(
                json.getInt(
                    "version"
                ) == 1
            )

            val tree =
                decode(
                    json.getJSONObject(
                        "expression"
                    ),
                    0
                ) as? MathExpression.Sequence
                    ?: return false

            require(
                nodeCount(tree) <= 4_096 &&
                        structuralDepth(tree) <= 60 &&
                        MathLatexSerializer
                            .serialize(tree)
                            .length <= 32_768
            )

            val restoredCursor =
                decodeCursor(
                    json.getJSONObject(
                        "cursor"
                    )
                )

            val selection =
                json.optJSONObject(
                    "selection"
                )?.let {

                    MathSelection(
                        decodePath(
                            it.getJSONArray(
                                "path"
                            )
                        ),
                        it.getInt(
                            "start"
                        ),
                        it.getInt(
                            "end"
                        )
                    )
                }

            val boundary =
                json.optJSONObject(
                    "navigationBoundary"
                )?.let {

                    if (
                        it.getBoolean(
                            "after"
                        )
                    ) {

                        decodePath(
                            it.getJSONArray(
                                "path"
                            )
                        )

                    } else {

                        null
                    }
                }

            /*
             * Old symmetric navigation trails are
             * intentionally ignored.
             */
            setExpression(
                tree,
                restoredCursor
            )

            if (
                selection != null
            ) {

                select(
                    selection.path,
                    selection.start,
                    selection.end
                )
            }

            /*
             * Cualquier parte seleccionable puede poseer
             * ahora un borde derecho persistente:
             *
             * SUBSTRUCTURE
             * PARENT
             * CHILD
             */
            val restoredBoundaryPart =
                boundary?.let {
                    navigationPart(it)
                }

            if (
                selectionRange == null &&
                boundary != null &&
                restoredBoundaryPart
                    ?.interaction
                    ?.selectable == true
            ) {

                val step =
                    boundary.last()

                val outside =
                    MathCursor(
                        boundary.dropLast(1),
                        step.nodeIndex + 1,
                        MathCursorLevel.CHILD_BOUNDARY,
                        null
                    )

                if (
                    cursor.path ==
                    outside.path &&
                    cursor.offset ==
                    outside.offset
                ) {

                    cursor =
                        outside

                    navigationBoundary =
                        NavigationBoundary(
                            boundary,
                            true,
                            expression,
                            outside
                        )
                }
            }

            /*
             * CHILD_BOUNDARY es todavía el nombre técnico
             * legado del estado de frontera.
             *
             * Después lo renombraremos a un término genérico,
             * porque ahora también sirve a PARENT y SUBSTRUCTURE.
             */
            if (
                navigationBoundary == null &&
                cursor.level ==
                MathCursorLevel.CHILD_BOUNDARY
            ) {

                cursor =
                    cursor.copy(
                        level =
                            MathCursorLevel.NEUTRAL,
                        scope = null
                    )
            }

            if (
                selectionRange == null &&
                json.optBoolean(
                    "sequenceInsertion"
                ) &&
                cursor.level ==
                MathCursorLevel.NEUTRAL &&
                cursor.scope == null &&
                sequenceAt(
                    cursor.path
                )?.let {
                    cursor.offset ==
                            it.children.size &&
                            it.hasContinuationInsertion()
                } == true
            ) {

                navigationInsertion =
                    NavigationInsertion(
                        expression,
                        cursor
                    )
            }

            val scope =
                cursor.scope as?
                        MathContentScope.Atom

            if (
                selectionRange == null &&
                cursor.level ==
                MathCursorLevel.INTERIOR &&
                scope != null &&
                scope.end - scope.start > 1 &&
                sequenceAt(
                    cursor.path
                )
                    ?.children
                    ?.subList(
                        scope.start,
                        scope.end
                    )
                    ?.all {
                        it is MathExpression.Variable &&
                                it.name.length == 1
                    } == true
            ) {

                val index =
                    (cursor.offset - 1)
                        .coerceIn(
                            scope.start,
                            scope.end - 1
                        )

                cursor =
                    cursor.copy(
                        scope =
                            MathContentScope.Atom(
                                index,
                                index + 1
                            )
                    )
            }

            true

        } catch (_: Exception) {

            false
        }
    }

    private fun boundedJsonNesting(value: String): Boolean {
        var depth = 0
        var quoted = false
        var escaped = false
        for (char in value) {
            if (quoted) {
                if (escaped) escaped = false else if (char == '\\') escaped = true else if (char == '"') quoted = false
            } else when (char) {
                '"' -> quoted = true
                '{', '[' -> if (++depth > 256) return false
                '}', ']' -> if (--depth < 0) return false
            }
        }
        return depth == 0 && !quoted
    }

    /** Immutable slot contents; the cursor remains the sole active-slot state. */
    internal fun sequenceAt(path: List<MathCursorStep>): MathExpression.Sequence? {
        var result = expression
        for (step in path) result = result.children.getOrNull(step.nodeIndex)?.childSlots()?.firstOrNull { it.first == step.slot }?.second ?: return null
        return result
    }

    private fun safeCursor(value: MathCursor): MathCursor {
        if (expression.children.isEmpty() && value.path.isEmpty()) return MathCursor()
        var sequence = expression
        val validPath = mutableListOf<MathCursorStep>()
        for (step in value.path) {
            val node = sequence.children.getOrNull(step.nodeIndex)
            val child = node?.childSlots()?.firstOrNull { it.first == step.slot }?.second
                ?: return MathCursor(validPath.toList(), (step.nodeIndex + if (node != null) 1 else 0).coerceIn(0, sequence.children.size))
            validPath += step
            sequence = child
        }
        val scope = (value.scope as? MathContentScope.Atom)?.let {
            val start = it.start.coerceIn(0, sequence.children.size)
            MathContentScope.Atom(start, it.end.coerceIn(start, sequence.children.size))
        } ?: value.scope
        val offset = if (value.level == MathCursorLevel.INTERIOR && scope is MathContentScope.Atom)
            value.offset.coerceIn(scope.start, scope.end) else value.offset.coerceIn(0, sequence.children.size)
        return value.copy(path = validPath.toList(), offset = offset, scope = scope)
    }

    private fun replaceSequence(path: List<MathCursorStep>, value: MathExpression.Sequence) {
        val old = sequenceAt(path)
        val scope = cursor.scope as? MathContentScope.Atom
        expression = expressionReplacingSequence(path, value)
        navigationBoundary = null
        navigationInsertion = null
        navigationEdge = null
        if (old != null && scope != null && cursor.path == path) {
            cursor = cursor.copy(scope = MathContentScope.Atom(scope.start, maxOf(scope.start, scope.end + value.children.size - old.children.size)))
        }
    }

    private fun expressionReplacingSequence(path: List<MathCursorStep>, value: MathExpression.Sequence): MathExpression.Sequence {
        fun replace(current: MathExpression.Sequence, depth: Int): MathExpression.Sequence {
            if (depth == path.size) return value
            val step = path[depth]
            val node = current.children.getOrNull(step.nodeIndex) ?: return current
            val child = node.childSlots().firstOrNull { it.first == step.slot }?.second ?: return current
            val children = current.children.toMutableList()
            children[step.nodeIndex] = node.withChildSlot(step.slot, replace(child, depth + 1))
            return current.copy(children = children)
        }
        return replace(expression, 0)
    }

    private fun encodeCursor(value: MathCursor) = JSONObject().put("path", encodePath(value.path)).put("offset", value.offset)
        .put("level", value.level.name).apply {
            when (val scope = value.scope) {
                is MathContentScope.Atom -> put("scope", JSONObject().put("kind", "atom").put("start", scope.start).put("end", scope.end))
                MathContentScope.Slot -> put("scope", JSONObject().put("kind", "slot"))
                null -> Unit
            }
        }
    private fun encodePath(path: List<MathCursorStep>) = JSONArray().apply { path.forEach { put(JSONObject().put("node", it.nodeIndex).put("slot", it.slot)) } }
    private fun decodeCursor(value: JSONObject): MathCursor {
        val path = decodePath(value.getJSONArray("path"))
        val level = if (value.has("level")) MathCursorLevel.valueOf(value.getString("level")) else if (path.isEmpty()) MathCursorLevel.NEUTRAL else MathCursorLevel.INTERIOR
        val scope = value.optJSONObject("scope")?.let { if (it.getString("kind") == "atom") MathContentScope.Atom(it.getInt("start"), it.getInt("end")) else MathContentScope.Slot }
            ?: if (level == MathCursorLevel.INTERIOR && path.isNotEmpty()) MathContentScope.Slot else null
        return MathCursor(path, value.getInt("offset"), level, scope)
    }
    private fun decodePath(value: JSONArray): List<MathCursorStep> {
        require(value.length() <= 64)
        return (0 until value.length()).map { value.getJSONObject(it).let { step -> MathCursorStep(step.getInt("node"), step.getString("slot")) } }
    }

    private fun encode(node: MathExpression): JSONObject {
        val json = JSONObject()
        when (node) {
            is MathExpression.Sequence -> json.put("type", "sequence").put("children", JSONArray().apply { node.children.forEach { put(encode(it)) } })
            is MathExpression.Number -> json.put("type", "number").put("value", node.value)
            is MathExpression.Variable -> json.put("type", "variable").put("value", node.name)
            is MathExpression.Symbol -> json.put("type", "symbol").put("value", node.value)
            is MathExpression.Operator -> json.put("type", "operator").put("value", node.value)
            is MathExpression.Relation -> json.put("type", "relation").put("value", node.value)
            is MathExpression.Fraction -> json.put("type", "fraction")
            is MathExpression.Scripts -> json.put("type", "scripts")
            is MathExpression.Root -> json.put("type", "root")
            is MathExpression.Group -> json.put("type", "group").put("delimiter", node.delimiter.name)
            is MathExpression.Function -> json.put("type", "function").put("name", node.name)
            is MathExpression.LargeOperator -> json.put("type", "largeOperator").put("kind", node.kind.name)
            is MathExpression.Limit -> json.put("type", "limit")
            is MathExpression.Derivative -> json.put("type", "derivative")
            is MathExpression.Matrix -> json.put("type", "matrix").put("rows", node.rows.size).put("columns", node.rows.first().size)
            is MathExpression.Cases -> json.put("type", "cases").put("rows", node.rows.size)
        }
        node.childSlots().forEach { (name, child) -> json.put(name, encode(child)) }
        return json
    }

    private fun decode(json: JSONObject, depth: Int): MathExpression {
        require(depth < 128)
        fun child(name: String) = decode(json.getJSONObject(name), depth + 1) as MathExpression.Sequence
        fun optional(name: String) = json.optJSONObject(name)?.let { decode(it, depth + 1) as MathExpression.Sequence }
        return when (json.getString("type")) {
            "sequence" -> {

                val array =
                    json.getJSONArray(
                        "children"
                    )

                require(
                    array.length() <= 4_096
                )

                val decoded =
                    (0 until array.length())
                        .map {
                            decode(
                                array.getJSONObject(it),
                                depth + 1
                            )
                        }

                /*
                 * La migración de un LargeOperator antiguo
                 * puede producir:
                 *
                 * Sequence(
                 *     LargeOperator,
                 *     body...
                 * )
                 *
                 * Al restaurarlo dentro de su Sequence padre
                 * lo aplanamos para convertir body en hermanos
                 * reales del operador.
                 */
                MathExpression.Sequence(
                    decoded.flatMap { node ->

                        if (
                            node is MathExpression.Sequence
                        ) {
                            node.children
                        } else {
                            listOf(node)
                        }
                    }
                )
            }
            "number" -> MathExpression.Number(json.getString("value"))
            "variable" -> MathExpression.Variable(json.getString("value"))
            "symbol" -> MathExpression.Symbol(json.getString("value"))
            "operator" -> MathExpression.Operator(json.getString("value"))
            "relation" -> MathExpression.Relation(json.getString("value"))
            "fraction" -> MathExpression.Fraction(child("numerator"), child("denominator"))
            "scripts" -> MathExpression.Scripts(child("base"), optional("upper"), optional("lower"))
            "root" -> MathExpression.Root(child("radicand"), optional("index"))
            "group" -> MathExpression.Group(child("content"), MathDelimiter.valueOf(json.getString("delimiter")))
            "function" -> MathExpression.Function(json.getString("name"), child("argument"), optional("base"))
            "largeOperator" -> {

                val operator =
                    MathExpression.LargeOperator(
                        kind =
                            MathLargeOperator.valueOf(
                                json.getString(
                                    "kind"
                                )
                            ),

                        lower =
                            optional(
                                "lower"
                            ),

                        upper =
                            optional(
                                "upper"
                            )
                    )

                /*
                 * Compatibilidad con drafts antiguos.
                 *
                 * Antes:
                 *
                 * LargeOperator(body = x)
                 *
                 * Ahora:
                 *
                 * Sequence(
                 *     LargeOperator(),
                 *     x
                 * )
                 */
                val legacyBody =
                    json.optJSONObject(
                        "body"
                    )?.let {

                        decode(
                            it,
                            depth + 1
                        ) as MathExpression.Sequence
                    }

                if (legacyBody != null) {

                    MathExpression.Sequence(
                        listOf(operator) +
                                legacyBody.children
                    )

                } else {

                    operator
                }
            }
            "limit" -> MathExpression.Limit(child("body"), child("condition"))
            "derivative" -> MathExpression.Derivative(child("body"), child("variable"), optional("order"))
            "matrix" -> {
                val rows = json.getInt("rows"); val columns = json.getInt("columns")
                require(rows in 1..32 && columns in 1..32)
                MathExpression.Matrix((0 until rows).map { r -> (0 until columns).map { c -> child("r${r}c$c") } })
            }
            "cases" -> {
                val rows = json.getInt("rows"); require(rows in 1..32)
                MathExpression.Cases((0 until rows).map { MathExpression.Case(child("expression$it"), child("condition$it")) })
            }
            else -> throw IllegalArgumentException("Unsupported draft node")
        }
    }
}
