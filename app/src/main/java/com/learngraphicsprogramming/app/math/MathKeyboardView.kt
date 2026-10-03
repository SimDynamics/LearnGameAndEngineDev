package com.learngraphicsprogramming.app.math

import android.app.Activity
import android.content.Context
import android.util.TypedValue
import android.view.View
import android.widget.EditText
import android.widget.HorizontalScrollView
import android.widget.LinearLayout
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
        input = editor
        editor.keyboardShortcuts = shortcuts
        visibility = View.VISIBLE
        keys.removeAllViews()
        renderActions(shortcuts.map { MathKeyboardAction.fromShortcut(when (it) { "^" -> "POWER"; "√" -> "ROOT"; else -> it }) } + listOf(
            MathKeyboardAction.MoveLeft, MathKeyboardAction.MoveRight, MathKeyboardAction.Backspace), editor.textSizePx)
    }

    private fun renderActions(actions: List<MathKeyboardAction>, textSize: Float) {

        actions.forEach { action ->

            val key =
                MathWidgetUi.button(
                    context,
                    action.label,
                    accentColor
                )

            key.setTextSize(
                TypedValue.COMPLEX_UNIT_PX,
                textSize * 14f / 18f
            )

            key.isFocusable = false
            key.minWidth = 0
            key.isSingleLine = action !is MathKeyboardAction.Fraction

            if (action is MathKeyboardAction.Fraction) {
                key.gravity = android.view.Gravity.CENTER
                key.setTextSize(
                    TypedValue.COMPLEX_UNIT_SP,
                    11f
                )
                key.setLineSpacing(
                    -MathWidgetUi.dp(context, 3).toFloat(),
                    0.8f
                )
            }

            key.setPadding(
                0,
                0,
                0,
                0
            )

            key.layoutParams =
                LinearLayout.LayoutParams(
                    MathWidgetUi.dp(context, 48),
                    MathWidgetUi.dp(context, 48)
                ).apply {
                    marginEnd =
                        MathWidgetUi.dp(context, 2)
                }

            key.tag =
                MathInputFocusOrder.GESTURE_OWNER_TAG

            key.contentDescription =
                action.contentDescription

            key.setOnClickListener {
                execute(action)
            }

            keys.addView(key)
        }

        (context as? Activity)?.let {
            MathKeyboardHost
                .current(it)
                ?.show(
                    this,
                    input!!
                )
        }
    }

    fun hide() {
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
