package com.learngraphicsprogramming.app.math

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.graphics.Canvas
import android.os.Parcel
import android.os.Parcelable
import android.text.InputType
import android.text.Editable
import android.text.Selection
import android.text.SpannableStringBuilder
import android.view.inputmethod.TextAttribute
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.GestureDetector
import android.view.View
import android.view.accessibility.AccessibilityNodeInfo
import android.view.inputmethod.BaseInputConnection
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputConnection
import android.view.inputmethod.InputMethodManager
import android.view.inputmethod.ExtractedText
import android.view.inputmethod.ExtractedTextRequest
import android.widget.PopupMenu
import org.json.JSONObject

/** One native mathematical surface. IME events edit the AST, never a hidden LaTeX EditText. */
class MathExpressionEditor(context: Context) : MathExpressionView(context) {
    val model = MathExpressionModel()
    var onExpressionChanged: (() -> Unit)? = null
    var onImeAction: ((Int) -> Boolean)? = null
    var keyboardShortcuts: List<String> = emptyList()
    private var expectedTemplate: MathExpression.Sequence? = null
    internal val previewTemplate: MathExpression.Sequence? get() = expectedTemplate
    private val keyboardStateListeners = mutableSetOf<() -> Unit>()

    internal fun addKeyboardStateListener(listener: () -> Unit) { keyboardStateListeners.add(listener) }
    internal fun removeKeyboardStateListener(listener: () -> Unit) { keyboardStateListeners.remove(listener) }
    private fun notifyKeyboardState() { keyboardStateListeners.toList().forEach { it() } }

    override fun scaleMathText(factor: Float) {
        super.scaleMathText(factor)
        notifyKeyboardState()
    }

    var imeOptions = EditorInfo.IME_ACTION_DONE or EditorInfo.IME_FLAG_NO_EXTRACT_UI or EditorInfo.IME_FLAG_NO_FULLSCREEN
    /** IME ranges are protocol metadata, never a mathematical selection or a saved answer. */
    private data class Composition(val path: List<MathCursorStep>, var modelStart: Int, var modelEnd: Int,
        var expression: MathExpression.Sequence, val ownsStructures: Boolean = false)
    private data class ImeSelection(val modelStart: Int, val modelEnd: Int, val cursor: MathCursor,
        val expression: MathExpression.Sequence)
    private var composition: Composition? = null
    private var imeSelection: ImeSelection? = null
    private var imeBatchDepth = 0
    private var connectionGeneration = 0
    internal var isSelectingSlot = false
        private set
    private var longPressed = false

    private var pendingTouchCursor:
            MathExpressionTouchTarget? = null

    private var touchDownX =
        0f

    private var touchDownY =
        0f

    private var touchDragged =
        false

    private val touchSlop =
        android.view.ViewConfiguration
            .get(context)
            .scaledTouchSlop
            .toFloat()

    private val gestures = GestureDetector(context, object : GestureDetector.SimpleOnGestureListener() {
        override fun onDown(event: MotionEvent) = true
        override fun onLongPress(event: MotionEvent) { longPressed = true; performLongClick() }
    })
    var lastError: String? = null
        private set

    init {
        isFocusable = true
        isFocusableInTouchMode = true
        isSaveEnabled = true
        minimumWidth = MathWidgetUi.dp(context, 100)
        minimumHeight = MathWidgetUi.dp(context, 56)
        setPadding(MathWidgetUi.dp(context, 12), MathWidgetUi.dp(context, 10), MathWidgetUi.dp(context, 12), MathWidgetUi.dp(context, 10))
        tag = MathInputFocusOrder.GESTURE_OWNER_TAG
        showPlaceholders = true
        onCursorRequested = { target -> selectCursor(target) }
        setOnLongClickListener { showEditMenu(); true }
        refresh(false)
    }

    fun getLatex(): String = MathLatexSerializer.serialize(model.expression)
    fun isComplete(): Boolean = model.isComplete()
    fun configureExpected(source: String) {
        expectedTemplate = (MathLatexParser.parse(source) as? MathLatexParser.Result.Success)?.expression
        notifyKeyboardState()
    }
    private fun template(predicate: (MathExpression) -> Boolean): MathExpression? {
        fun find(node: MathExpression): MathExpression? {
            if (predicate(node)) return node
            val children = if (node is MathExpression.Sequence) node.children else node.childSlots().map { it.second }
            return children.firstNotNullOfOrNull(::find)
        }
        return expectedTemplate?.let(::find)
    }

    fun setLatex(latex: String): Boolean {
        if (latex.isBlank()) { composition = null; model.clear(); refresh(); restartIme(); return true }
        val result = MathLatexParser.parse(latex)
        if (result !is MathLatexParser.Result.Success) {
            lastError = (result as MathLatexParser.Result.Failure).message
            announceForAccessibility(lastError)
            return false
        }
        val imported = MathExpressionModel()
        val insertion = imported.insertLatex(latex)
        if (insertion is MathLatexParser.Result.Failure) { lastError = insertion.message; announceForAccessibility(lastError); return false }
        model.setExpression(imported.expression, MathCursor(offset = imported.expression.children.size))
        composition = null
        refresh()
        restartIme()
        return true
    }

    fun saveDraft(): JSONObject = JSONObject().put("structured", model.saveDraft())
    fun restoreDraft(draft: JSONObject) {
        if (model.restoreDraft(draft.optString("structured"))) { composition = null; refresh(); restartIme() }
    }

    fun perform(action: MathKeyboardAction) {
        if (!isEnabled) return
        if (action == MathKeyboardAction.PreviousSlot || action == MathKeyboardAction.NextSlot ||
            action == MathKeyboardAction.MoveLeft || action == MathKeyboardAction.MoveRight) {
            val forward = action == MathKeyboardAction.NextSlot || action == MathKeyboardAction.MoveRight
            val moved = if (forward) model.stepRight() else model.stepLeft()
            if (moved) {
                finishImeCompositionForExternalAction()
                refresh()
                // Explicit navigation also changes same-slot selection/caret states. Retire the
                // old predictive context while preserving the model's genuine MathSelection.
                restartIme()
            } else if (action == MathKeyboardAction.MoveRight &&
                imeOptions and EditorInfo.IME_MASK_ACTION == EditorInfo.IME_ACTION_NEXT) {
                onImeAction?.invoke(EditorInfo.IME_ACTION_NEXT)
            }
            return
        }
        finishImeCompositionForExternalAction()

        val previousCursorPath =
            model.cursor.path.toList()
        val previousLevel = model.navigationLevel
        val previousDraft = model.saveDraft()
        var accepted = true
        val empty = MathExpression.Sequence()
        fun structure(
            node: MathExpression,
            slot: String? = null
        ) {
            accepted =
                model.insert(
                    node,
                    slot
                )
        }
        fun largeOperator(
            kind: MathLargeOperator
        ) {

            val expected =
                template {
                    it is MathExpression.LargeOperator &&
                            it.kind == kind
                } as? MathExpression.LargeOperator

            val lower =
                if (
                    expected == null ||
                    expected.lower != null
                ) {
                    empty
                } else {
                    null
                }

            val upper =
                if (
                    expected == null ||
                    expected.upper != null
                ) {
                    empty
                } else {
                    null
                }

            structure(
                MathExpression.LargeOperator(
                    kind = kind,
                    lower = lower,
                    upper = upper
                )
            )
        }

        when (action) {
            MathKeyboardAction.Fraction -> {
                val selected = model.takeSelection()
                structure(MathExpression.Fraction(selected, empty), if (selected.children.isEmpty()) "numerator" else "denominator")
            }
            MathKeyboardAction.Power -> accepted = model.addScript(upper = true)
            MathKeyboardAction.Subscript -> accepted = model.addScript(upper = false)
            MathKeyboardAction.Root -> structure(MathExpression.Root(model.takeSelection()), "radicand")
            MathKeyboardAction.NthRoot -> structure(MathExpression.Root(model.takeSelection(), empty), "index")
            MathKeyboardAction.Parentheses -> structure(MathExpression.Group(model.takeSelection()), "content")
            MathKeyboardAction.AbsoluteValue -> structure(MathExpression.Group(model.takeSelection(), MathDelimiter.ABSOLUTE), "content")
            MathKeyboardAction.Brackets -> structure(MathExpression.Group(model.takeSelection(), MathDelimiter.BRACKETS), "content")
            MathKeyboardAction.Braces -> structure(MathExpression.Group(model.takeSelection(), MathDelimiter.BRACES), "content")
            MathKeyboardAction.Integral -> largeOperator(MathLargeOperator.INTEGRAL)
            MathKeyboardAction.Sum -> largeOperator(MathLargeOperator.SUM)
            MathKeyboardAction.Product -> largeOperator(MathLargeOperator.PRODUCT)
            MathKeyboardAction.Limit -> structure(MathExpression.Limit(empty, empty), "condition")
            MathKeyboardAction.Matrix -> {
                val expected = template { it is MathExpression.Matrix } as? MathExpression.Matrix
                structure(MathExpression.Matrix(List(expected?.rows?.size ?: 2) { List(expected?.rows?.first()?.size ?: 2) { empty } }), "r0c0")
            }
            MathKeyboardAction.Cases -> {
                val expected = template { it is MathExpression.Cases } as? MathExpression.Cases
                structure(MathExpression.Cases(List(expected?.rows?.size ?: 2) { MathExpression.Case(empty, empty) }), "expression0")
            }
            MathKeyboardAction.Derivative -> {
                val expected = template { it is MathExpression.Derivative } as? MathExpression.Derivative
                structure(MathExpression.Derivative(empty, empty, if (expected?.order != null) empty else null), if (expected?.order != null) "order" else "body")
            }
            is MathKeyboardAction.Function -> {
                val expected = template { it is MathExpression.Function && it.name == action.name } as? MathExpression.Function
                val base = if (expected?.base != null || expected == null && action.name == "log") empty else null
                structure(MathExpression.Function(action.name, empty, base), if (base != null) "base" else "argument")
            }
            MathKeyboardAction.Backspace -> model.deleteBackward()
            MathKeyboardAction.PreviousSlot, MathKeyboardAction.NextSlot,
            MathKeyboardAction.MoveLeft, MathKeyboardAction.MoveRight -> Unit // handled above
            is MathKeyboardAction.Symbol -> accepted = if (action.label in MathLatexSerializer.symbols && action.label !in setOf("→", "≤", "≥", "≠", "≈"))
                model.insert(MathExpression.Symbol(action.label)) else model.insertText(action.label)
        }
        if (!accepted) model.restoreDraft(previousDraft)
        refresh()

        /*
         * Solo reconstruimos InputConnection cuando
         * realmente cambiamos de slot estructural.
         *
         * Escribir:
         *
         * x
         * +
         * 1
         *
         * dentro del mismo slot NO necesita reiniciar
         * todo el teclado Android tres veces.
         */
        if (
            previousCursorPath !=
            model.cursor.path || previousLevel != model.navigationLevel
        ) {
            restartIme()
        }
        if (!accepted) { lastError = "La expresión supera el tamaño o la profundidad admitidos."; announceForAccessibility(lastError) }
    }

    private fun refresh(
        notify: Boolean = true
    ) {

        expression =
            model.expression

        cursor =
            if (hasFocus()) {
                model.cursor
            } else {
                null
            }
        cursorPresentation = model.cursorPresentation

        lastError =
            null

        contentDescription =
            "Editor matemático: " +
                    getLatex()
                        .ifBlank {
                            "vacío"
                        }

        invalidate()

        /*
         * Actualizamos la posición lógica del IME,
         * pero NO pedimos al ScrollView que persiga
         * continuamente al cursor.
         *
         * El desplazamiento inicial lo controla
         * MathKeyboardHost una sola vez al enfocar.
         */
        updateImeSelection()
        notifyKeyboardState()

        if (notify) {
            onExpressionChanged?.invoke()
        }
    }

    override fun onFocusChanged(gainFocus: Boolean, direction: Int, previouslyFocusedRect: android.graphics.Rect?) {
        super.onFocusChanged(gainFocus, direction, previouslyFocusedRect)
        cursor = if (gainFocus) model.cursor else null
        cursorPresentation = model.cursorPresentation
        if (!gainFocus) {
            composition = null
            imeSelection = null
            imeBatchDepth = 0
            connectionGeneration++
        }
        invalidate()
    }

    override fun onDraw(canvas: Canvas) {
        model.selectionRange?.let { selection ->
            val start = expressionLayout.caretBounds(MathCursor(selection.path, selection.start))
            val end = expressionLayout.caretBounds(MathCursor(selection.path, selection.end))
            if (start != null && end != null) {
                val paint = android.graphics.Paint().apply { color = 0x554799dd }
                val top = if (selection.path.isEmpty()) expressionLayout.ascent else minOf(start.top, end.top)
                val bottom = if (selection.path.isEmpty()) expressionLayout.descent else maxOf(start.bottom, end.bottom)
                canvas.drawRect(expressionOriginX + start.left, expressionBaseline + top,
                    expressionOriginX + end.left, expressionBaseline + bottom, paint)
            }
        }
        super.onDraw(canvas)
    }

    override fun onTouchEvent(
        event: MotionEvent
    ): Boolean {

        if (!isEnabled) {
            return false
        }

        when (event.actionMasked) {

            MotionEvent.ACTION_DOWN -> {

                longPressed = false
                touchDragged = false

                touchDownX =
                    event.x

                touchDownY =
                    event.y

                isSelectingSlot =
                    true

                /*
                 * Guardamos el destino AHORA,
                 * antes de que cualquier scroll cambie
                 * la geometría.
                 *
                 * Pero todavía NO movemos el cursor.
                 */
                pendingTouchCursor =
                    expressionLayout.hitTestForEditing(
                        event.x -
                                expressionOriginX,
                        event.y -
                                expressionBaseline
                    )
            }

            MotionEvent.ACTION_MOVE -> {

                val dx =
                    kotlin.math.abs(
                        event.x -
                                touchDownX
                    )

                val dy =
                    kotlin.math.abs(
                        event.y -
                                touchDownY
                    )

                if (
                    dx > touchSlop ||
                    dy > touchSlop
                ) {
                    touchDragged =
                        true
                }
            }

            MotionEvent.ACTION_UP -> {

                isSelectingSlot =
                    false

                if (
                    !longPressed &&
                    !touchDragged
                ) {

                    val target =
                        pendingTouchCursor

                    requestFocus()

                    if (
                        target != null
                    ) {

                        selectCursor(
                            target =
                                target.cursor,

                            leafTextOffset =
                                target.leafTextOffset,

                            sequenceInsertion =
                                target.sequenceInsertion,

                            prefixOffset =
                                target.prefixOffset
                        )
                    }

                    performClick()

                    (
                            context.getSystemService(
                                Context.INPUT_METHOD_SERVICE
                            ) as InputMethodManager
                            ).showSoftInput(
                            this,
                            InputMethodManager.SHOW_IMPLICIT
                        )
                }

                pendingTouchCursor =
                    null

                (context as? android.app.Activity)
                    ?.let {
                        MathKeyboardHost
                            .current(it)
                            ?.refreshPosition()
                    }

                refresh(false)
            }

            MotionEvent.ACTION_CANCEL -> {

                isSelectingSlot =
                    false

                touchDragged =
                    false

                pendingTouchCursor =
                    null

                (context as? android.app.Activity)
                    ?.let {
                        MathKeyboardHost
                            .current(it)
                            ?.refreshPosition()
                    }
            }
        }

        gestures.onTouchEvent(event)

        return true
    }

    private fun selectCursor(
        target: MathCursor,
        leafTextOffset: Int? = null,
        sequenceInsertion: Boolean = false,
        prefixOffset: Int? = null
    ) {

        composition =
            null

        imeSelection =
            null

        model.clearSelection()

        model.placeTextCursor(
            position =
                target,

            leafTextOffset =
                leafTextOffset,

            sequenceInsertion =
                sequenceInsertion,

            prefixOffset =
                prefixOffset
        )

        refresh()

        restartIme()
    }

    private data class ImeProjection(val text: String, val boundaries: List<Int>,
        val modelBoundaries: List<Int>, val editableStart: Int, val editableEnd: Int,
        val start: Int, val end: Int) {
        fun nodeOffset(offset: Int): Int = boundaries.indices.minByOrNull { kotlin.math.abs(boundaries[it] - offset) } ?: 0
        fun modelOffset(offset: Int): Int {
            val index = boundaries.indexOfLast { it <= offset }.coerceAtLeast(0)
            return modelBoundaries[index] + (offset - boundaries[index])
        }
        fun nativeOffset(offset: Int): Int {
            val index = modelBoundaries.indexOfLast { it <= offset }.coerceAtLeast(0)
            val nativeWidth = boundaries.getOrNull(index + 1)?.minus(boundaries[index]) ?: 0
            return boundaries[index] + minOf(offset - modelBoundaries[index], nativeWidth)
        }
    }

    /** A read-only plain-text projection of the active slot. The AST and its cursor remain authoritative. */
    private fun imeProjection(ownedText: Pair<Int, Int>? = null): ImeProjection {
        val sequence = model.sequenceAt(model.cursor.path) ?: MathExpression.Sequence()
        val offsets = mutableListOf(0)
        val modelOffsets = mutableListOf(0)
        val selected = model.selectionRange?.takeIf { it.path == model.cursor.path }
        val scope = model.contentScopeBounds()
        val composingText = ownedText ?: activeComposition()?.let { it.modelStart to it.modelEnd }
        val text = buildString {
            sequence.children.forEachIndexed { index, node ->
                // Neutral groups and child boundaries are objects in the native protocol.
                // Predictive word replacement must never turn structural Backspace into
                // text editing inside the previous sibling, including a numeric sibling.
                val value = when (node) {
                    is MathExpression.Number -> node.value
                    is MathExpression.Variable -> node.name
                    is MathExpression.Symbol -> node.value
                    is MathExpression.Operator -> node.value
                    is MathExpression.Relation -> node.value
                    else -> "\uFFFC"
                }
                val modelStart = modelOffsets.last()
                // A live IME word can span several independent atoms. This private
                // projection never expands the model's navigation or deletion scope.
                val editable = selected != null || model.navigationLevel == MathCursorLevel.INTERIOR &&
                    index in scope.first until scope.second || composingText?.let {
                        modelStart < it.second && modelStart + value.length > it.first
                    } == true
                append(if (editable) value else "\uFFFC")
                offsets.add(length)
                modelOffsets.add(modelOffsets.last() + value.length)
            }
        }
        val native = imeSelection?.takeIf { it.expression === model.expression && it.cursor == model.cursor }
        val projection = ImeProjection(text, offsets, modelOffsets,
            offsets[if (selected != null) 0 else scope.first],
            offsets[if (selected != null) sequence.children.size else scope.second],
            selected?.let { offsets[it.start] } ?: offsets[model.cursor.offset],
            selected?.let { offsets[it.end] } ?: offsets[model.cursor.offset])
        return if (selected != null || native == null) projection else projection.copy(
            start = projection.nativeOffset(native.modelStart), end = projection.nativeOffset(native.modelEnd))
    }

    private fun activeComposition() = composition?.takeIf {
        it.path == model.cursor.path && it.expression === model.expression
    }

    /** Convert native protocol offsets immediately; word visibility may later change without an AST edit. */
    private fun rememberImeSelection(nativeStart: Int, nativeEnd: Int = nativeStart) {
        val projection = imeProjection()
        imeSelection = ImeSelection(projection.modelOffset(nativeStart), projection.modelOffset(nativeEnd), model.cursor, model.expression)
    }

    private fun updateImeSelection() {
        if (imeBatchDepth > 0 || !hasFocus() || !isAttachedToWindow) return
        val projection = imeProjection()
        val composing = activeComposition()
        (context.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager).updateSelection(this,
            projection.start, projection.end, composing?.let { projection.nativeOffset(it.modelStart) } ?: -1,
            composing?.let { projection.nativeOffset(it.modelEnd) } ?: -1)
    }

    private fun restartIme() {
        // Invalidate even while Android has not yet marked this editor as active.
        composition = null
        imeSelection = null
        connectionGeneration++
        imeBatchDepth = 0
        val ime = context.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager
        if (hasFocus() && isAttachedToWindow && ime.isActive(this)) {
            ime.restartInput(this)
        }
    }

    private fun advanceWithinExpression(): Boolean {
        finishImeCompositionForExternalAction()
        if (!model.nextSlot()) return false
        refresh()
        restartIme()
        return true
    }

    override fun onCheckIsTextEditor() = true
    /** All Android deletion transports call this slot-bounded AST operation directly. */
    private fun deleteImeText(beforeLength: Int, afterLength: Int, codePoints: Boolean = false,
        deleteSelection: Boolean = false): Boolean {
        if (beforeLength < 0 || afterLength < 0) return false
        if (beforeLength == 0 && afterLength == 0) return true
        val structuralBefore = model.expression
        if (beforeLength == 1 && afterLength == 0 && model.selectionRange == null &&
            model.navigationLevel != MathCursorLevel.INTERIOR && model.deleteBackwardAtCurrentLevel()) {
            finishImeCompositionForExternalAction()
            refresh(model.expression != structuralBefore)
            if (model.expression !== structuralBefore) restartIme()
            return true
        }
        val previous = model.expression
        val projection = imeProjection()
        val composing = activeComposition()
        val realSelection = model.selectionRange != null
        val left = minOf(projection.start, projection.end)
        val right = maxOf(projection.start, projection.end)
        val ranges = if (deleteSelection && left < right || realSelection) listOf(left to right) else {
            val start = if (codePoints) Character.offsetByCodePoints(projection.text, left,
                -minOf(beforeLength, Character.codePointCount(projection.text, 0, left)))
                else left - minOf(beforeLength, left)
            val end = if (codePoints) Character.offsetByCodePoints(projection.text, right,
                minOf(afterLength, Character.codePointCount(projection.text, right, projection.text.length)))
                else right + minOf(afterLength, projection.text.length - right)
            // deleteSurroundingText excludes the native protocol selection. KEY_DEL and a
            // genuine mathematical selection delete selected content instead.
            listOf(start to left, right to end)
        }
        if (codePoints && !realSelection && ranges.any { (start, end) ->
            var index = start
            var valid = true
            while (index < end && valid) {
                val character = projection.text[index++]
                if (Character.isHighSurrogate(character)) {
                    valid = index < end && Character.isLowSurrogate(projection.text[index])
                    if (valid) index++
                } else if (Character.isLowSurrogate(character)) valid = false
            }
            !valid
        }) return false
        // Normalize UTF-16 boundaries exactly as the model does; keep protocol offsets in sync.
        val normalized = ranges.map { (start, end) ->
            maxOf(start, projection.editableStart) to minOf(end, projection.editableEnd)
        }.filter { it.first < it.second }.map { (start, end) ->
            val from = if (start > 0 && start < projection.text.length && Character.isHighSurrogate(projection.text[start - 1]) && Character.isLowSurrogate(projection.text[start])) start - 1 else start
            val to = if (end > 0 && end < projection.text.length && Character.isHighSurrogate(projection.text[end - 1]) && Character.isLowSurrogate(projection.text[end])) end + 1 else end
            from to to
        }.sortedBy { it.first }.fold(mutableListOf<Pair<Int, Int>>()) { merged, range ->
            val last = merged.lastOrNull()
            if (last != null && range.first <= last.second) merged[merged.lastIndex] = last.first to maxOf(last.second, range.second)
            else merged.add(range)
            merged
        }
        val deleted = if (realSelection) model.deleteSurroundingTextInCurrentSlot(beforeLength, afterLength, codePoints)
            else model.deleteTextRangesInCurrentSlot(normalized.map { (start, end) ->
                projection.modelOffset(start) to projection.modelOffset(end)
            })
        if (!deleted) return false
        imeSelection = null
        fun translateModel(offset: Int) = offset - normalized.sumOf { (start, end) ->
            val until = offset.coerceIn(projection.modelOffset(start), projection.modelOffset(end))
            projection.text.substring(start, projection.nativeOffset(until)).count { it != '\uFFFC' }
        }
        if (!realSelection && !deleteSelection && left < right) imeSelection = ImeSelection(
            translateModel(projection.modelOffset(left)), translateModel(projection.modelOffset(right)), model.cursor, model.expression)
        if (realSelection) composition = null
        else if (composing != null) {
            // Keep the remaining composing range anchored to the edited AST, never restore a draft.
            composing.modelStart = translateModel(composing.modelStart)
            composing.modelEnd = translateModel(composing.modelEnd)
            composing.expression = model.expression
            composition = composing.takeIf { it.modelEnd > it.modelStart }
        } else composition = null
        refresh(model.expression != previous)
        return true
    }

    override fun onCreateInputConnection(outAttrs: EditorInfo): InputConnection {
        outAttrs.inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
        outAttrs.imeOptions = imeOptions
        val initial = imeProjection()
        outAttrs.initialSelStart = initial.start
        outAttrs.initialSelEnd = initial.end
        // Android may query a connection and discard it while retaining the served one.
        // Only explicit mathematical context changes retire that context's connections.
        val generation = connectionGeneration
        val connectionSlot = model.cursor.path.toList()
        return object : BaseInputConnection(this, true) {
            private var closed = false
            private var localBatchDepth = 0
            private fun current() = !closed && isEnabled && generation == connectionGeneration &&
                connectionSlot == model.cursor.path
            override fun getEditable(): Editable {
                if (!current()) return SpannableStringBuilder()
                val projection = imeProjection()
                val snapshot = SpannableStringBuilder(projection.text)
                activeComposition()?.let { composing ->
                    val start = projection.nativeOffset(composing.modelStart)
                    val end = projection.nativeOffset(composing.modelEnd)
                    val span = SpannableStringBuilder(snapshot.subSequence(start, end))
                    BaseInputConnection.setComposingSpans(span)
                    snapshot.replace(start, end, span)
                }
                Selection.setSelection(snapshot, projection.start, projection.end)
                return snapshot
            }
            override fun beginBatchEdit(): Boolean {
                if (!current()) return false
                localBatchDepth++
                imeBatchDepth++
                return true
            }
            override fun endBatchEdit(): Boolean {
                if (!current() || localBatchDepth == 0) return false
                localBatchDepth--
                imeBatchDepth = (imeBatchDepth - 1).coerceAtLeast(0)
                if (imeBatchDepth == 0) updateImeSelection()
                return localBatchDepth > 0
            }
            private fun replaceImeText(text: CharSequence?, newCursorPosition: Int,
                composing: Boolean, explicitStart: Int? = null, explicitEnd: Int? = null): Boolean {
                if (!current()) return false
                val before = model.expression
                val path = model.cursor.path
                val projection = imeProjection()
                val range = if (model.selectionRange == null && explicitStart == null) activeComposition() else null
                var start = (explicitStart ?: range?.let { projection.nativeOffset(it.modelStart) } ?: minOf(projection.start, projection.end)).coerceIn(0, projection.text.length)
                var end = (explicitEnd ?: range?.let { projection.nativeOffset(it.modelEnd) } ?: maxOf(projection.start, projection.end)).coerceIn(start, projection.text.length)
                val replacement = text?.toString().orEmpty()
                val structuralInsertion = model.navigationLevel != MathCursorLevel.INTERIOR &&
                    model.selectionRange == null && range == null
                if (structuralInsertion && explicitStart != null && start != end) return false
                if (structuralInsertion && explicitStart != null) {
                    val position = MathCursor(path, projection.nodeOffset(start), MathCursorLevel.NEUTRAL)
                    if (position != model.cursor) model.cursor = position
                }
                val insertionPosition = model.cursor
                // A standalone mathematical separator ends the IME's predictive word.
                // Its temporary word range must not consume the preceding operand in x + 1.
                // Explicit AST selections and replaceText ranges still replace their contents.
                if (!composing && model.selectionRange == null && explicitStart == null &&
                    replacement in setOf("+", "-", "−", "×", "÷", "·", "*", "/", "=", "<", ">", "≤", "≥", "≠", "≈", "→", "^", "_") &&
                    projection.text.substring(start, end).any { it.isLetterOrDigit() || it == '∞' || it == '\uFFFC' }) {
                    start = projection.end
                    end = start
                }
                if (!structuralInsertion && model.selectionRange == null && range == null) {
                    start = start.coerceIn(projection.editableStart, projection.editableEnd)
                    end = end.coerceIn(start, projection.editableEnd)
                }
                // Keep composition and newCursorPosition in the same normalized UTF-16
                // range that the model edits. A half-surrogate replacement consumes the
                // complete code point; a collapsed insertion moves beyond the pair.
                val startInsidePair = start > 0 && start < projection.text.length &&
                    Character.isHighSurrogate(projection.text[start - 1]) && Character.isLowSurrogate(projection.text[start])
                if (start == end && startInsidePair) {
                    start++
                    end++
                } else {
                    if (startInsidePair) start--
                    if (end > 0 && end < projection.text.length && Character.isHighSurrogate(projection.text[end - 1]) &&
                        Character.isLowSurrogate(projection.text[end])) end++
                }
                // A private IME range cannot acquire ownership of existing AST objects.
                // Only a genuine mathematical selection or the live composition that
                // inserted an object may replace it. Retained objects are not inserted text.
                if (!structuralInsertion && model.selectionRange == null && range?.ownsStructures != true &&
                    '\uFFFC' in projection.text.substring(start, end)) return false
                val modelStart = if (structuralInsertion) projection.modelBoundaries[insertionPosition.offset]
                    else projection.modelOffset(start)
                val modelEnd = if (structuralInsertion) modelStart else projection.modelOffset(end)
                val replaced = if (structuralInsertion) model.insertText(replacement)
                    else model.replaceTextRangeInCurrentSlot(modelStart, modelEnd, replacement,
                        replaceStructures = range?.ownsStructures == true)
                if (!replaced) return false
                composition = null
                imeSelection = null
                if (path == model.cursor.path) {
                    val newModelLength = imeProjection().modelBoundaries.last()
                    val insertedLength = (newModelLength - projection.modelBoundaries.last() + modelEnd - modelStart).coerceAtLeast(0)
                    val insertedModelStart = modelStart.coerceAtMost(newModelLength)
                    val insertedModelEnd = (modelStart + insertedLength).coerceAtMost(newModelLength)
                    val after = imeProjection(insertedModelStart to insertedModelEnd)
                    val insertionStart = after.nativeOffset(insertedModelStart)
                    val insertionEnd = if (structuralInsertion) after.boundaries[model.cursor.offset]
                        else after.nativeOffset((modelStart + insertedLength).coerceAtMost(after.modelBoundaries.last()))
                    val desired = (if (newCursorPosition > 0) insertionEnd.toLong() + newCursorPosition - 1
                        else insertionStart.toLong() + newCursorPosition).coerceIn(0, after.text.length.toLong()).toInt()
                    if (model.navigationLevel == MathCursorLevel.INTERIOR) {
                        model.setTextCursorInCurrentSlot(after.modelOffset(desired))
                    } else {
                        val position = model.cursor.copy(offset = after.nodeOffset(desired))
                        if (position != model.cursor) model.cursor = position
                    }
                    if (composing && insertionEnd > insertionStart) composition = Composition(path.toList(), insertedModelStart,
                        insertedModelEnd, model.expression, ownsStructures = '\uFFFC' in after.text.substring(insertionStart, insertionEnd))
                    rememberImeSelection(imeProjection().start)
                }
                refresh(model.expression != before)
                if (path != model.cursor.path) restartIme()
                return true
            }
            override fun commitText(text: CharSequence?, newCursorPosition: Int): Boolean =
                replaceImeText(text, newCursorPosition, composing = false)
            override fun setComposingText(text: CharSequence?, newCursorPosition: Int): Boolean =
                replaceImeText(text, newCursorPosition, composing = true)
            override fun replaceText(start: Int, end: Int, text: CharSequence, newCursorPosition: Int,
                textAttribute: TextAttribute?): Boolean {
                if (start < 0 || end < 0) return false
                return replaceImeText(text, newCursorPosition, composing = false,
                    explicitStart = minOf(start, end), explicitEnd = maxOf(start, end))
            }
            override fun finishComposingText(): Boolean {
                if (!current()) return false
                composition = null
                imeSelection = null
                updateImeSelection()
                return true
            }
            override fun setComposingRegion(start: Int, end: Int): Boolean {
                if (!current()) return false
                val projection = imeProjection()
                val left = minOf(start, end).coerceIn(0, projection.text.length)
                val right = maxOf(start, end).coerceIn(left, projection.text.length)
                // Object markers belong to AST structures, not an IME word to replace.
                if ('\uFFFC' in projection.text.substring(left, right)) return false
                composition = if (left < right) Composition(model.cursor.path.toList(), projection.modelOffset(left),
                    projection.modelOffset(right), model.expression) else null
                return true
            }
            override fun setSelection(start: Int, end: Int): Boolean {
                if (!current()) return false
                val projection = imeProjection()
                if (start == projection.start && end == projection.end) return true
                // A native protocol selection never replaces an explicit mathematical selection.
                if (model.selectionRange != null) { updateImeSelection(); return true }
                val left = minOf(start, end).coerceIn(0, projection.text.length)
                val right = maxOf(start, end).coerceIn(left, projection.text.length)
                val composing = activeComposition()
                if (model.navigationLevel != MathCursorLevel.INTERIOR) {
                    val offset = projection.nodeOffset(right)
                    if (offset != model.cursor.offset) model.cursor = MathCursor(model.cursor.path, offset, MathCursorLevel.NEUTRAL)
                } else if (!model.setTextCursorInCurrentSlot(projection.modelOffset(
                    right.coerceIn(projection.editableStart, projection.editableEnd)))) return false
                composing?.expression = model.expression
                rememberImeSelection(left, right)
                refresh(false)
                return true
            }
            override fun deleteSurroundingText(beforeLength: Int, afterLength: Int): Boolean =
                current() && deleteImeText(beforeLength, afterLength)
            override fun deleteSurroundingTextInCodePoints(beforeLength: Int, afterLength: Int): Boolean =
                current() && deleteImeText(beforeLength, afterLength, codePoints = true)
            override fun sendKeyEvent(event: KeyEvent): Boolean {
                if (!current()) return false
                return when (event.action) {
                    KeyEvent.ACTION_DOWN -> when (event.keyCode) {
                        KeyEvent.KEYCODE_DEL -> deleteImeText(1, 0, deleteSelection = true)
                        KeyEvent.KEYCODE_FORWARD_DEL -> deleteImeText(0, 1, deleteSelection = true)
                        else -> onKeyDown(event.keyCode, event)
                    }
                    KeyEvent.ACTION_UP -> true // A key release never mutates the AST.
                    KeyEvent.ACTION_MULTIPLE -> if (event.keyCode == KeyEvent.KEYCODE_DEL || event.keyCode == KeyEvent.KEYCODE_FORWARD_DEL) false
                        else event.characters?.let { commitText(it, 1) } ?: false
                    else -> false
                }
            }
            override fun performEditorAction(actionCode: Int): Boolean {
                if (!current()) return false
                if (actionCode in setOf(EditorInfo.IME_ACTION_NEXT, EditorInfo.IME_ACTION_DONE) && advanceWithinExpression()) return true
                return onImeAction?.invoke(actionCode) ?: false
            }
            override fun getTextBeforeCursor(n: Int, flags: Int): CharSequence = if (!current()) "" else imeProjection().let { it.text.substring(0, minOf(it.start, it.end)).takeLast(n.coerceAtLeast(0)) }
            override fun getTextAfterCursor(n: Int, flags: Int): CharSequence = if (!current()) "" else imeProjection().let { it.text.substring(maxOf(it.start, it.end)).take(n.coerceAtLeast(0)) }
            override fun getSelectedText(flags: Int): CharSequence? = if (!current()) null else imeProjection().let { if (it.start == it.end) null else it.text.substring(minOf(it.start, it.end), maxOf(it.start, it.end)) }
            override fun getExtractedText(request: ExtractedTextRequest?, flags: Int): ExtractedText? = if (!current()) null else imeProjection().let { projection ->
                ExtractedText().apply {
                    text = projection.text; startOffset = 0; partialStartOffset = -1; partialEndOffset = -1
                    selectionStart = projection.start; selectionEnd = projection.end
                }
            }
            override fun performContextMenuAction(id: Int): Boolean = current() && editCommand(id)
            override fun closeConnection() {
                if (closed) return
                // BaseInputConnection.closeConnection calls finishComposingText. Close this
                // transport first so that call cannot erase another transport's live word.
                closed = true
                val sameContext = generation == connectionGeneration
                if (sameContext) imeBatchDepth = (imeBatchDepth - localBatchDepth).coerceAtLeast(0)
                localBatchDepth = 0
                super.closeConnection()
                if (sameContext && imeBatchDepth == 0) updateImeSelection()
            }
        }
    }
    override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean {
        if (!isEnabled || event.action != KeyEvent.ACTION_DOWN) return super.onKeyDown(keyCode, event)
        when (keyCode) {
            KeyEvent.KEYCODE_DEL -> return deleteImeText(1, 0, deleteSelection = true)
            KeyEvent.KEYCODE_FORWARD_DEL -> return deleteImeText(0, 1, deleteSelection = true)
            KeyEvent.KEYCODE_DPAD_LEFT -> perform(MathKeyboardAction.MoveLeft)
            KeyEvent.KEYCODE_DPAD_RIGHT -> perform(MathKeyboardAction.MoveRight)
            KeyEvent.KEYCODE_DPAD_UP -> { composition = null; model.previousSlot(); refresh(); restartIme() }
            KeyEvent.KEYCODE_DPAD_DOWN, KeyEvent.KEYCODE_TAB -> if (!advanceWithinExpression() && imeOptions and EditorInfo.IME_MASK_ACTION == EditorInfo.IME_ACTION_NEXT) onImeAction?.invoke(EditorInfo.IME_ACTION_NEXT)
            KeyEvent.KEYCODE_A -> if (event.isCtrlPressed) { composition = null; model.selectAll(); refresh(); restartIme() } else return insertHardware(event)
            KeyEvent.KEYCODE_V -> if (event.isCtrlPressed) editCommand(android.R.id.paste) else return insertHardware(event)
            KeyEvent.KEYCODE_C -> if (event.isCtrlPressed) editCommand(android.R.id.copy) else return insertHardware(event)
            else -> return insertHardware(event)
        }
        return true
    }

    private fun insertHardware(event: KeyEvent): Boolean {
        val unicode = event.unicodeChar
        if (unicode == 0 || Character.isISOControl(unicode)) return super.onKeyDown(event.keyCode, event)
        composition = null
        val previousPath = model.cursor.path
        val inserted = model.insertText(String(Character.toChars(unicode)))
        refresh()
        if (previousPath != model.cursor.path) restartIme()
        return inserted
    }

    private fun showEditMenu() {
        PopupMenu(context, this).apply {
            menu.add(0, android.R.id.selectAll, 0, "Seleccionar todo")
            menu.add(0, android.R.id.copy, 1, "Copiar LaTeX")
            if (isEnabled) { menu.add(0, android.R.id.cut, 2, "Cortar"); menu.add(0, android.R.id.paste, 3, "Pegar") }
            setOnMenuItemClickListener { editCommand(it.itemId) }
            show()
        }
    }

    private fun editCommand(id: Int): Boolean {
        val clipboard = context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
        composition = null
        when (id) {
            android.R.id.selectAll -> model.selectAll()
            android.R.id.copy, android.R.id.cut -> {
                val snapshot = model.saveDraft()
                val selected = model.takeSelection()
                val text = if (selected.children.isEmpty()) getLatex() else MathLatexSerializer.serialize(selected)
                clipboard.setPrimaryClip(ClipData.newPlainText("Expresión matemática", text))
                if (id == android.R.id.copy || !isEnabled) model.restoreDraft(snapshot)
                else if (selected.children.isEmpty()) model.clear()
            }
            android.R.id.paste -> {
                if (!isEnabled) return false
                val text = clipboard.primaryClip?.getItemAt(0)?.coerceToText(context)?.toString().orEmpty()
                if ('\\' in text) {
                    val parsed = model.insertLatex(text)
                    if (parsed is MathLatexParser.Result.Failure) { lastError = parsed.message; announceForAccessibility(lastError); return false }
                } else if (!model.insertText(text)) return false
            }
            else -> return false
        }
        refresh()
        restartIme()
        return true
    }

    private fun finishImeCompositionForExternalAction() {
        composition = null
        imeSelection = null
    }

    override fun onInitializeAccessibilityNodeInfo(info: AccessibilityNodeInfo) {
        super.onInitializeAccessibilityNodeInfo(info)
        info.className = "android.widget.EditText"
        info.isEditable = isEnabled
        info.text = getLatex()
        info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SET_TEXT)
        info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_COPY)
        info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_PASTE)
    }

    override fun performAccessibilityAction(action: Int, arguments: android.os.Bundle?): Boolean = when (action) {
        AccessibilityNodeInfo.ACTION_SET_TEXT -> isEnabled && setLatex(arguments?.getCharSequence(AccessibilityNodeInfo.ACTION_ARGUMENT_SET_TEXT_CHARSEQUENCE)?.toString().orEmpty())
        AccessibilityNodeInfo.ACTION_COPY -> editCommand(android.R.id.copy)
        AccessibilityNodeInfo.ACTION_PASTE -> editCommand(android.R.id.paste)
        else -> super.performAccessibilityAction(action, arguments)
    }

    override fun onSaveInstanceState(): Parcelable = SavedState(super.onSaveInstanceState()).apply { draft = model.saveDraft() }
    override fun onRestoreInstanceState(state: Parcelable?) {
        if (state !is SavedState) { super.onRestoreInstanceState(state); return }
        super.onRestoreInstanceState(state.superState)
        model.restoreDraft(state.draft)
        refresh(false)
    }
    private class SavedState : View.BaseSavedState {
        var draft = ""
        constructor(state: Parcelable?) : super(state)
        constructor(parcel: Parcel) : super(parcel) { draft = parcel.readString().orEmpty() }
        override fun writeToParcel(out: Parcel, flags: Int) { super.writeToParcel(out, flags); out.writeString(draft) }
        companion object {
            @JvmField val CREATOR = object : Parcelable.Creator<SavedState> {
                override fun createFromParcel(source: Parcel) = SavedState(source)
                override fun newArray(size: Int): Array<SavedState?> = arrayOfNulls(size)
            }
        }
    }
}
