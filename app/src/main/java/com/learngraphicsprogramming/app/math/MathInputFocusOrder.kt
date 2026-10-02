package com.learngraphicsprogramming.app.math

import android.content.Context
import android.view.View
import android.view.ViewGroup
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputMethodManager
import android.widget.EditText

/** Follows the composed View order, including row-major tables and multiple responses. */
object MathInputFocusOrder {
    const val GESTURE_OWNER_TAG = "math_gesture_owner"

    private class FocusListener(
        val original: View.OnFocusChangeListener?,
        val update: () -> Unit
    ) : View.OnFocusChangeListener {
        override fun onFocusChange(view: View, focused: Boolean) {
            original?.onFocusChange(view, focused)
            if (focused) {
                update()
                (view as? EditText)?.let { input ->
                    input.selectAll()
                    input.post { if (input.hasFocus()) input.selectAll() }
                }
            }
        }
    }

    fun configure(root: View) {
        val inputs = mutableListOf<EditText>()
        fun collect(view: View) {
            if (view.visibility != View.VISIBLE) return
            if (view is EditText) inputs.add(view)
            if (view is ViewGroup) for (index in 0 until view.childCount) collect(view.getChildAt(index))
        }
        collect(root)
        if (inputs.isNotEmpty()) root.isFocusableInTouchMode = true
        inputs.forEach { input ->
            if (input.id == View.NO_ID) input.id = View.generateViewId()
            input.tag = GESTURE_OWNER_TAG
            input.setSelectAllOnFocus(true)
        }
        inputs.forEachIndexed { index, input ->
            fun nextInput() = inputs.drop(index + 1).firstOrNull { it.isEnabled && it.visibility == View.VISIBLE }
            fun updateIme() {
                val next = nextInput()
                input.imeOptions = (input.imeOptions and (EditorInfo.IME_MASK_ACTION or EditorInfo.IME_FLAG_NO_ENTER_ACTION).inv()) or
                    (if (next == null) EditorInfo.IME_ACTION_DONE else EditorInfo.IME_ACTION_NEXT) or
                    EditorInfo.IME_FLAG_NO_EXTRACT_UI or EditorInfo.IME_FLAG_NO_FULLSCREEN
                input.nextFocusForwardId = next?.id ?: View.NO_ID
            }
            updateIme()
            val previous = input.onFocusChangeListener
            val original = if (previous is FocusListener) previous.original else previous
            input.onFocusChangeListener = FocusListener(original, ::updateIme)
            fun finishEditing() {
                root.requestFocus()
                (input.context.getSystemService(Context.INPUT_METHOD_SERVICE) as? InputMethodManager)
                    ?.hideSoftInputFromWindow(input.windowToken, 0)
            }
            input.setOnEditorActionListener { _, action, _ ->
                when (action) {
                    EditorInfo.IME_ACTION_NEXT -> {
                        val next = nextInput()
                        if (next != null) { next.requestFocus(); next.selectAll() } else finishEditing()
                        true
                    }
                    EditorInfo.IME_ACTION_DONE -> {
                        finishEditing()
                        true
                    }
                    else -> false
                }
            }
        }
    }
}
