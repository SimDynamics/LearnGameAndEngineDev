package com.learngraphicsprogramming.app.math

import android.content.Context
import android.app.Activity
import android.graphics.Color
import android.content.res.ColorStateList
import org.json.JSONArray
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
        tag = MathInputFocusOrder.GESTURE_OWNER_TAG
        layoutParams = LinearLayout.LayoutParams(-1, -2)
        visibility = View.GONE
        addView(keys)
        contentDescription = "Atajos del teclado matemático"
    }

    fun bind(editText: EditText, mode: String, keyboardSymbols: JSONArray? = null) {
        input = editText
        visibility = View.VISIBLE
        keys.removeAllViews()
        val selected = runCatching { Mode.valueOf(mode.uppercase()) }.getOrDefault(Mode.NUMERIC)
        val defaults = when (selected) {
            Mode.NUMERIC -> listOf("−", ".", ",")
            Mode.FRACTION -> listOf("a/b", "−")
            Mode.ALGEBRA -> listOf("x", "y", "+", "−", "×", "÷", "^", "=", "()")
            Mode.INEQUALITY -> listOf("x", "=", "<", ">", "≠", "≤", "≥", "−", "()")
            Mode.TRIG -> listOf("x", "π", "+", "−", "×", "÷", "^", "()")
            Mode.CALCULUS -> listOf("x", "y", "+", "−", "×", "÷", "^", "()")
        }
        val allowed = setOf(".", ",", "+", "−", "-", "×", "÷", "π", "%", "a/b", "/", "(", ")", "()", "=", "<", ">", "≤", "≥", "≠", "x", "y", "a", "b", "^", "√", "∞")
        val symbols = if (keyboardSymbols == null) defaults else
            (0 until keyboardSymbols.length()).map { keyboardSymbols.optString(it) }
                .filter { it in allowed }.distinct()
        for (symbol in symbols + "⌫") {
            val key = MathWidgetUi.button(context, symbol, accentColor)
            key.setTextSize(TypedValue.COMPLEX_UNIT_PX, editText.textSize * 14f / 18f)
            key.isFocusable = false
            key.minWidth = 0
            key.isSingleLine = true
            key.setPadding(0, 0, 0, 0)
            key.layoutParams = LinearLayout.LayoutParams(MathWidgetUi.dp(context, 48), MathWidgetUi.dp(context, 48)).apply {
                marginEnd = MathWidgetUi.dp(context, 2)
            }
            key.tag = MathInputFocusOrder.GESTURE_OWNER_TAG
            key.contentDescription = if (symbol == "a/b") "Insertar barra de fracción" else symbol
            key.setOnClickListener { insert(symbol) }
            keys.addView(key)
        }
        (context as? Activity)?.let { MathKeyboardHost.current(it)?.show(this, editText) }
    }

    fun hide() {
        (context as? Activity)?.let { MathKeyboardHost.current(it)?.hide(this) }
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
