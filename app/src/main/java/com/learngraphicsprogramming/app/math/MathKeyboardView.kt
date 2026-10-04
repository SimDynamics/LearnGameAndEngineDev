package com.learngraphicsprogramming.app.math

import android.app.Activity
import android.content.Context
import android.content.res.ColorStateList
import android.graphics.Color
import android.util.TypedValue
import android.view.View
import android.widget.Button
import android.widget.EditText
import android.widget.HorizontalScrollView
import android.widget.ImageButton
import android.widget.ImageView
import android.widget.LinearLayout
import com.learngraphicsprogramming.app.R
import org.json.JSONArray

/**
 * Barra local de acciones matemáticas.
 *
 * MathShortcutResolver decide QUÉ necesita el ejercicio.
 * MathKeyboardAction decide QUÉ significa cada botón.
 * MathKeyboardView solamente lo presenta.
 */
class MathKeyboardView(
    context: Context,
    private val accentColor: Int
) : HorizontalScrollView(context) {

    enum class Mode {
        NUMERIC,
        FRACTION,
        ALGEBRA,
        INEQUALITY,
        TRIG,
        CALCULUS
    }

    private val keys =
        MathWidgetUi.row(context)

    private var input: View? = null

    private var minusInsertion = "-"
    private val keyboardStateListener: () -> Unit = { updateStructuredState() }
    private var previousSlotKey: ImageButton? = null
    private var nextSlotKey: ImageButton? = null
    private var renderedTemplate: MathExpression.Sequence? = null
    private val previews = mutableListOf<Pair<MathKeyboardAction, MathKeyboardActionPreviewView>>()
    private val symbolKeys = mutableListOf<Button>()

    init {
        isFillViewport = false
        isHorizontalScrollBarEnabled = false

        tag =
            MathInputFocusOrder.GESTURE_OWNER_TAG

        layoutParams =
            LinearLayout.LayoutParams(
                -1,
                -2
            )

        visibility = View.GONE

        addView(keys)

        contentDescription =
            "Atajos del teclado matemático"
    }

    fun bind(
        editText: EditText,
        mode: String,
        keyboardSymbols: JSONArray? = null
    ) {
        unbindEditor()
        input = editText
        visibility = View.VISIBLE

        keys.removeAllViews()

        val fieldContext =
            MathKeyboardContext.get(editText)

        minusInsertion =
            fieldContext?.minus ?: "-"

        val shortcuts =
            if (keyboardSymbols != null) {
                MathShortcutResolver.resolve(
                    null,
                    mode,
                    keyboardSymbols
                )
            } else {
                fieldContext?.symbols
                    ?: MathShortcutResolver.resolve(
                        null,
                        mode
                    )
            }

        val actions =
            shortcuts
                .map(MathKeyboardAction::fromShortcut)
                .plus(MathKeyboardAction.Backspace)

        renderActions(actions, editText.textSize)
    }

    fun bind(editor: MathExpressionEditor, shortcuts: List<String>) {
        unbindEditor()
        input = editor
        editor.keyboardShortcuts = shortcuts
        visibility = View.VISIBLE
        keys.removeAllViews()
        renderedTemplate = editor.previewTemplate
        val actions = shortcuts.map {
            MathKeyboardAction.fromShortcut(when (it) { "^" -> "POWER"; "√" -> "ROOT"; else -> it })
        }.filterNot {
            it == MathKeyboardAction.MoveLeft || it == MathKeyboardAction.MoveRight ||
                it == MathKeyboardAction.PreviousSlot || it == MathKeyboardAction.NextSlot || it == MathKeyboardAction.Backspace
        }
        actions.forEach { action ->
            val preview = action.previewExpression(editor.previewTemplate)
            val key = if (preview != null) {
                MathKeyboardActionPreviewView(context, action, preview, accentColor, editor.textSizePx).also {
                    previews.add(action to it)
                    it.layoutParams = LinearLayout.LayoutParams(-2, -2)
                }
            } else {
                textKey(action, editor.textSizePx).also { symbolKeys.add(it) }
            }
            addKey(key, action)
        }
        previousSlotKey = slotKey(MathKeyboardAction.PreviousSlot, R.drawable.ic_math_previous_slot)
        nextSlotKey = slotKey(MathKeyboardAction.NextSlot, R.drawable.ic_math_next_slot)
        addKey(textKey(MathKeyboardAction.Backspace, editor.textSizePx).also { symbolKeys.add(it) }, MathKeyboardAction.Backspace)
        editor.addKeyboardStateListener(keyboardStateListener)
        updateStructuredState()
        showHost()
    }

    private fun renderActions(actions: List<MathKeyboardAction>, textSize: Float) {
        actions.forEach { action ->
            addKey(textKey(action, textSize), action)
        }
        showHost()
    }

    private fun textKey(action: MathKeyboardAction, textSize: Float): Button =
        MathWidgetUi.button(context, action.label, accentColor).apply {
            setTextSize(TypedValue.COMPLEX_UNIT_PX, textSize * 14f / 18f)
            minWidth = 0
            isSingleLine = action !is MathKeyboardAction.Fraction
            if (action is MathKeyboardAction.Fraction) {
                gravity = android.view.Gravity.CENTER
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
                setLineSpacing(-MathWidgetUi.dp(context, 3).toFloat(), 0.8f)
            }
            setPadding(0, 0, 0, 0)
            layoutParams = LinearLayout.LayoutParams(MathWidgetUi.dp(context, 48), MathWidgetUi.dp(context, 48))
        }

    private fun slotKey(action: MathKeyboardAction, drawable: Int): ImageButton = ImageButton(context).apply {
        val buttonStyle = MathWidgetUi.button(context, "", accentColor)
        background = buttonStyle.background
        backgroundTintList = ColorStateList(
            arrayOf(intArrayOf(android.R.attr.state_enabled), intArrayOf()),
            intArrayOf(accentColor, Color.argb(85, Color.red(accentColor), Color.green(accentColor), Color.blue(accentColor)))
        )
        imageTintList = ColorStateList(
            arrayOf(intArrayOf(android.R.attr.state_enabled), intArrayOf()),
            intArrayOf(buttonStyle.currentTextColor, MathWidgetUi.muted)
        )
        setImageResource(drawable)
        scaleType = ImageView.ScaleType.FIT_CENTER
        val padding = MathWidgetUi.dp(context, 8)
        setPadding(padding, padding, padding, padding)
        layoutParams = LinearLayout.LayoutParams(MathWidgetUi.dp(context, 48), MathWidgetUi.dp(context, 48))
        addKey(this, action)
    }

    private fun addKey(key: View, action: MathKeyboardAction) {
        key.isFocusable = false
        (key.layoutParams as LinearLayout.LayoutParams).marginEnd = MathWidgetUi.dp(context, 2)
        key.tag = MathInputFocusOrder.GESTURE_OWNER_TAG
        if (key !is MathKeyboardActionPreviewView) key.contentDescription = action.contentDescription
        key.setOnClickListener { execute(action) }
        keys.addView(key)
    }

    private fun updateStructuredState() {
        val editor = input as? MathExpressionEditor ?: return
        val slotVisibility = if (editor.model.hasNavigableSlots) View.VISIBLE else View.GONE
        previousSlotKey?.apply {
            visibility = slotVisibility
            isEnabled = editor.isEnabled && editor.model.canNavigatePrevious
        }
        nextSlotKey?.apply {
            visibility = slotVisibility
            isEnabled = editor.isEnabled && editor.model.canNavigateNext
        }
        val templateChanged = renderedTemplate !== editor.previewTemplate
        previews.forEach { (action, preview) ->
            if (templateChanged) action.previewExpression(editor.previewTemplate)?.let { preview.previewExpression = it }
            preview.previewTextSizePx = editor.textSizePx
        }
        renderedTemplate = editor.previewTemplate
        symbolKeys.forEach { it.setTextSize(TypedValue.COMPLEX_UNIT_PX, editor.textSizePx * 14f / 18f) }
    }

    private fun unbindEditor() {
        (input as? MathExpressionEditor)?.removeKeyboardStateListener(keyboardStateListener)
        previews.clear()
        symbolKeys.clear()
        previousSlotKey = null
        nextSlotKey = null
        renderedTemplate = null
    }

    private fun showHost() {
        (context as? Activity)?.let {
            MathKeyboardHost
                .current(it)
                ?.show(
                    this,
                    input ?: return
                )
        }
    }

    fun hide() {
        unbindEditor()
        (context as? Activity)?.let {
            MathKeyboardHost
                .current(it)
                ?.hide(this)
        }

        input = null
        visibility = View.GONE
    }

    private fun execute(
        action: MathKeyboardAction
    ) {
        val target =
            input ?: return

        if (!target.isEnabled) {
            return
        }

        when (target) {
            is EditText -> action.apply(target, minusInsertion)
            is MathExpressionEditor -> target.perform(action)
        }
    }
}
