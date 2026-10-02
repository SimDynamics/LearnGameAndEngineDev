package com.learngraphicsprogramming.app.math

import android.content.Context
import android.view.View
import android.util.TypedValue
import android.widget.EditText
import android.widget.HorizontalScrollView
import android.widget.LinearLayout

/** Local shortcut keyboard; adding a context never changes expression validation. */
class MathKeyboardView(context: Context, private val accentColor: Int) : HorizontalScrollView(context) {
    enum class Mode { NUMERIC, FRACTION, ALGEBRA, INEQUALITY, TRIG, CALCULUS }
    private val keys = MathWidgetUi.row(context)
    private var input: EditText? = null

    init {
        isFillViewport = false
        isHorizontalScrollBarEnabled = false
        layoutParams = LinearLayout.LayoutParams(-1, -2)
        visibility = View.GONE
        addView(keys)
        contentDescription = "Atajos del teclado matemático"
    }

    fun bind(editText: EditText, mode: String) {
        input = editText
        visibility = View.VISIBLE
        keys.removeAllViews()
        val selected = runCatching { Mode.valueOf(mode.uppercase()) }.getOrDefault(Mode.NUMERIC)
        val symbols = when (selected) {
            Mode.NUMERIC -> listOf("0", "1", "2", "3", "4", "5", "6", "7", "8", "9", ".", ",", "+", "−", "%", "a/b", "()")
            Mode.FRACTION -> listOf("0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "a/b", "−", ".", "()")
            Mode.ALGEBRA -> listOf("x", "y", "+", "−", "*", "/", "^", "=", "π", "a/b", "()")
            Mode.INEQUALITY -> listOf("x", "=", "<", ">", "!=", "<=", ">=", "−", "a/b", "()")
            // Reserved contexts currently offer algebra shortcuts, not a CAS.
            Mode.TRIG, Mode.CALCULUS -> listOf("x", "y", "+", "−", "*", "/", "^", "π", "()")
        }
        for (symbol in symbols + "⌫") {
            val key = MathWidgetUi.button(context, symbol, accentColor)
            key.setTextSize(TypedValue.COMPLEX_UNIT_PX, editText.textSize * 14f / 18f)
            key.isFocusable = false
            key.contentDescription = if (symbol == "a/b") "Insertar barra de fracción" else symbol
            key.setOnClickListener { insert(symbol) }
            keys.addView(key)
        }
    }

    fun hide() {
        input = null
        visibility = View.GONE
    }

    private fun insert(symbol: String) {
        val target = input ?: return
        if (!target.isEnabled) return
        val start = target.selectionStart.coerceAtLeast(0)
        val end = target.selectionEnd.coerceAtLeast(start)
        val value = target.text
        when (symbol) {
            "⌫" -> if (start != end) value.delete(start, end) else if (start > 0) value.delete(start - 1, start)
            "()" -> {
                value.replace(start, end, "(${value.substring(start, end)})")
                target.setSelection(start + 1)
            }
            else -> value.replace(start, end, when (symbol) { "a/b" -> "/"; "−" -> "-"; else -> symbol })
        }
    }
}
