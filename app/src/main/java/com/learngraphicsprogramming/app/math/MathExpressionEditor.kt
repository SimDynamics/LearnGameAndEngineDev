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

    var imeOptions = EditorInfo.IME_ACTION_DONE or EditorInfo.IME_FLAG_NO_EXTRACT_UI or EditorInfo.IME_FLAG_NO_FULLSCREEN
    private data class Composition(val draft: String, val path: List<MathCursorStep>,
        val start: Int, var end: Int, var cursor: MathCursor)
    private var composition: Composition? = null
    private var connectionGeneration = 0
    internal var isSelectingSlot = false
        private set
    private var longPressed = false

    private var pendingTouchCursor:
            MathCursor? = null

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
    fun configureExpected(source: String) { expectedTemplate = (MathLatexParser.parse(source) as? MathLatexParser.Result.Success)?.expression }
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
        model.setExpression(imported.expression, imported.cursor)
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
        finishImeCompositionForExternalAction()

        val previousCursorPath =
            model.cursor.path.toList()
        val previousDraft = model.saveDraft()
        var accepted = true
        val empty = MathExpression.Sequence()
        fun structure(node: MathExpression, slot: String) { accepted = model.insert(node, slot) }
        fun largeOperator(kind: MathLargeOperator) {
            val expected = template { it is MathExpression.LargeOperator && it.kind == kind } as? MathExpression.LargeOperator
            val lower = if (expected == null || expected.lower != null) empty else null
            val upper = if (expected == null || expected.upper != null) empty else null
            structure(MathExpression.LargeOperator(kind, empty, lower, upper), when { lower != null -> "lower"; upper != null -> "upper"; else -> "body" })
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
            MathKeyboardAction.MoveLeft -> model.moveLeft()
            MathKeyboardAction.MoveRight -> if (!model.moveRight() && imeOptions and EditorInfo.IME_MASK_ACTION == EditorInfo.IME_ACTION_NEXT)
                onImeAction?.invoke(EditorInfo.IME_ACTION_NEXT)
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
            model.cursor.path
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

        if (notify) {
            onExpressionChanged?.invoke()
        }
    }

    override fun onFocusChanged(gainFocus: Boolean, direction: Int, previouslyFocusedRect: android.graphics.Rect?) {
        super.onFocusChanged(gainFocus, direction, previouslyFocusedRect)
        cursor = if (gainFocus) model.cursor else null
        if (!gainFocus) { composition = null; connectionGeneration++ }
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
                    expressionLayout.hitTest(
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

                    if (target != null) {
                        selectCursor(target)
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

    private fun selectCursor(target: MathCursor) {
        composition = null
        model.clearSelection()
        model.cursor = target
        refresh()
        restartIme()
    }

    private data class ImeProjection(val text: String, val boundaries: List<Int>, val start: Int, val end: Int) {
        fun nodeOffset(offset: Int): Int = boundaries.indices.minByOrNull { kotlin.math.abs(boundaries[it] - offset) } ?: 0
    }

    /** A read-only plain-text projection of the active slot. The AST and its cursor remain authoritative. */
    private fun imeProjection(): ImeProjection {
        val sequence = model.sequenceAt(model.cursor.path) ?: MathExpression.Sequence()
        val offsets = mutableListOf(0)
        val text = buildString {
            sequence.children.forEach { node ->
                append(when (node) {
                    is MathExpression.Number -> node.value
                    is MathExpression.Variable -> node.name
                    is MathExpression.Symbol -> node.value
                    is MathExpression.Operator -> node.value
                    is MathExpression.Relation -> node.value
                    else -> "\uFFFC"
                })
                offsets.add(length)
            }
        }
        val selected = model.selectionRange?.takeIf { it.path == model.cursor.path }
        return ImeProjection(text, offsets, offsets[selected?.start ?: model.cursor.offset], offsets[selected?.end ?: model.cursor.offset])
    }

    private fun updateImeSelection() {
        if (!hasFocus() || !isAttachedToWindow) return
        val projection = imeProjection()
        val composing = composition?.takeIf { it.path == model.cursor.path && it.cursor == model.cursor }
        (context.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager).updateSelection(this,
            projection.start, projection.end, composing?.start ?: -1, composing?.end ?: -1)
    }

    private fun restartIme() {
        val ime = context.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager
        if (hasFocus() && isAttachedToWindow && ime.isActive(this)) {
            connectionGeneration++
            ime.restartInput(this)
        }
    }

    private fun advanceWithinExpression(): Boolean {
        composition = null
        if (!model.nextSlot()) return false
        refresh()
        restartIme()
        return true
    }

    override fun onCheckIsTextEditor() = true
    override fun onCreateInputConnection(outAttrs: EditorInfo): InputConnection {
        outAttrs.inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
        outAttrs.imeOptions = imeOptions
        composition = null
        val initial = imeProjection()
        outAttrs.initialSelStart = initial.start
        outAttrs.initialSelEnd = initial.end
        val generation = ++connectionGeneration
        return object : BaseInputConnection(this, true) {
            private fun current() = isEnabled && generation == connectionGeneration
            override fun getEditable(): Editable = SpannableStringBuilder(imeProjection().text).also {
                val projection = imeProjection()
                Selection.setSelection(it, projection.start, projection.end)
            }
            override fun commitText(text: CharSequence?, newCursorPosition: Int): Boolean {
                if (!current()) return false
                val beforePath = model.cursor.path
                composition?.takeIf { it.path == model.cursor.path && it.cursor == model.cursor }?.let { model.restoreDraft(it.draft) }
                composition = null
                val inserted = model.insertText(text?.toString().orEmpty())
                refresh()
                if (beforePath != model.cursor.path) restartIme()
                return inserted
            }
            override fun setComposingText(text: CharSequence?, newCursorPosition: Int): Boolean {
                if (!current()) return false
                val previous = composition?.takeIf { it.path == model.cursor.path && it.cursor == model.cursor }
                val original = previous ?: Composition(model.saveDraft(), model.cursor.path.toList(), imeProjection().start, imeProjection().start, model.cursor)
                model.restoreDraft(original.draft)
                val inserted = model.insertText(text?.toString().orEmpty())
                original.cursor = model.cursor
                original.end = imeProjection().end
                composition = original.takeIf { inserted && it.path == model.cursor.path }
                refresh()
                if (original.path != model.cursor.path) restartIme()
                return inserted
            }
            override fun finishComposingText(): Boolean {

                if (!current()) {
                    return false
                }

                val wasComposing =
                    composition != null

                composition = null

                if (wasComposing) {
                    model.clearSelection()
                    refresh(false)
                } else {
                    updateImeSelection()
                }

                return true
            }
            override fun setComposingRegion(
                start: Int,
                end: Int
            ): Boolean {

                if (!current()) {
                    return false
                }

                /*
                 * El teclado Android utiliza regiones de composición
                 * para sugerencias, predicción, correcciones, etc.
                 *
                 * Nuestro editor matemático NO debe convertir esa
                 * región temporal en una selección real del AST.
                 *
                 * De hacerlo:
                 *
                 * x|
                 *
                 * puede convertirse internamente en:
                 *
                 * [x]
                 *
                 * y después pulsar "+" reemplazaría la x.
                 */
                composition =
                    null

                model.clearSelection()

                updateImeSelection()

                return true
            }
            override fun setSelection(
                start: Int,
                end: Int
            ): Boolean {

                if (!current()) {
                    return false
                }

                val projection =
                    imeProjection()

                /*
                 * Si Android solamente confirma exactamente
                 * la posición que ya tenemos, no hacemos nada.
                 */
                if (
                    start == projection.start &&
                    end == projection.end
                ) {

                    updateImeSelection()

                    return true
                }

                /*
                 * IMPORTANTE:
                 *
                 * setSelection() pertenece al protocolo del IME.
                 *
                 * Una selección temporal del teclado NO equivale
                 * a una selección matemática realizada por el usuario.
                 *
                 * Por eso siempre colapsamos esa selección en un
                 * único cursor y jamás llamamos aquí a model.select().
                 */
                composition =
                    null

                model.clearSelection()

                val safeEnd =
                    end.coerceIn(
                        0,
                        projection.text.length
                    )

                model.cursor =
                    MathCursor(
                        model.cursor.path,
                        projection.nodeOffset(
                            safeEnd
                        )
                    )

                refresh(false)

                return true
            }
            override fun deleteSurroundingText(beforeLength: Int, afterLength: Int): Boolean {
                if (!current()) return false
                composition = null
                val previousPath = model.cursor.path
                repeat(beforeLength.coerceIn(0, 1000)) { model.deleteBackward() }
                repeat(afterLength.coerceIn(0, 1000)) {
                    val previous = model.cursor
                    model.moveRight()
                    if (model.cursor != previous) model.deleteBackward()
                }
                refresh()
                if (previousPath != model.cursor.path) restartIme()
                return true
            }
            override fun sendKeyEvent(event: KeyEvent): Boolean {
                if (!current()) return false
                return when (event.action) {
                    KeyEvent.ACTION_DOWN -> onKeyDown(event.keyCode, event)
                    KeyEvent.ACTION_UP -> true
                    KeyEvent.ACTION_MULTIPLE -> event.characters?.let { commitText(it, 1) } ?: false
                    else -> false
                }
            }
            override fun performEditorAction(actionCode: Int): Boolean {
                if (!current()) return false
                if (actionCode in setOf(EditorInfo.IME_ACTION_NEXT, EditorInfo.IME_ACTION_DONE) && advanceWithinExpression()) return true
                return onImeAction?.invoke(actionCode) ?: false
            }
            override fun getTextBeforeCursor(n: Int, flags: Int): CharSequence = if (!current()) "" else imeProjection().let { it.text.substring(0, it.start).takeLast(n.coerceAtLeast(0)) }
            override fun getTextAfterCursor(n: Int, flags: Int): CharSequence = if (!current()) "" else imeProjection().let { it.text.substring(it.end).take(n.coerceAtLeast(0)) }
            override fun getSelectedText(flags: Int): CharSequence? = if (!current()) null else imeProjection().let { if (it.start == it.end) null else it.text.substring(it.start, it.end) }
            override fun getExtractedText(request: ExtractedTextRequest?, flags: Int): ExtractedText? = if (!current()) null else imeProjection().let { projection ->
                ExtractedText().apply { text = projection.text; startOffset = 0; selectionStart = projection.start; selectionEnd = projection.end }
            }
            override fun performContextMenuAction(id: Int): Boolean = current() && editCommand(id)
            override fun closeConnection() {
                if (generation == connectionGeneration) { composition = null; connectionGeneration++ }
                super.closeConnection()
            }
        }
    }

    override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean {
        if (!isEnabled || event.action != KeyEvent.ACTION_DOWN) return super.onKeyDown(keyCode, event)
        when (keyCode) {
            KeyEvent.KEYCODE_DEL -> perform(MathKeyboardAction.Backspace)
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

        if (
            composition == null
        ) {
            return
        }

        composition =
            null

        /*
         * La selección creada por una composición
         * temporal no debe sobrevivir.
         */
        model.clearSelection()
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
