package com.learngraphicsprogramming.app.math

import android.content.Context
import android.view.View
import android.view.ViewGroup
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputMethodManager
import android.widget.EditText
import com.learngraphicsprogramming.app.PuzzleAnswerSubmit
import com.learngraphicsprogramming.app.R

/** Follows the composed View order, including row-major tables and multiple responses. */
object MathInputFocusOrder {
    const val GESTURE_OWNER_TAG = "math_gesture_owner"

    private class AnswerSubmission(val submit: () -> Unit)

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

    fun configure(root: View, answerRoots: Collection<View> = emptyList(), onSubmit: (() -> Unit)? = null) {
        val inputs = mutableListOf<View>()
        fun collect(view: View) {
            if (view.visibility != View.VISIBLE) return
            if (view is EditText || view is MathExpressionEditor) inputs.add(view)
            if (view is ViewGroup) for (index in 0 until view.childCount) collect(view.getChildAt(index))
        }
        collect(root)
        if (inputs.isNotEmpty()) root.isFocusableInTouchMode = true
        inputs.forEach { input ->
            if (input.id == View.NO_ID) input.id = View.generateViewId()
            input.tag = GESTURE_OWNER_TAG
            (input as? EditText)?.setSelectAllOnFocus(true)
        }
        inputs.forEachIndexed { index, input ->
            fun isAnswerInput(): Boolean {
                var current: View? = input
                while (current != null) {
                    if (current in answerRoots) return true
                    current = current.parent as? View
                }
                return false
            }
            // A lesson/fullscreen pass recomposes focus order around the same controls.
            // Keep each graded field's original action on that View; unrelated auxiliary
            // inputs have no marker and retain normal NEXT/DONE navigation.
            val submitAnswer = if (onSubmit == null) {
                (input.getTag(R.id.puzzle_answer_submit_action) as? AnswerSubmission)?.submit
            } else {
                onSubmit.takeIf { isAnswerInput() &&
                    (input !is EditText || PuzzleAnswerSubmit.isSingleLine(input)) }.also { action ->
                    input.setTag(R.id.puzzle_answer_submit_action, action?.let(::AnswerSubmission))
                }
            }
            fun nextInput() = inputs.drop(index + 1).firstOrNull { it.isEnabled && it.visibility == View.VISIBLE }
            fun updateIme() {
                val next = nextInput()
                val options = if (input is EditText) input.imeOptions else (input as MathExpressionEditor).imeOptions
                val updated = (options and (EditorInfo.IME_MASK_ACTION or EditorInfo.IME_FLAG_NO_ENTER_ACTION).inv()) or
                    (if (submitAnswer != null || next == null) EditorInfo.IME_ACTION_DONE else EditorInfo.IME_ACTION_NEXT) or
                    EditorInfo.IME_FLAG_NO_EXTRACT_UI or EditorInfo.IME_FLAG_NO_FULLSCREEN
                if (input is EditText) input.imeOptions = updated else (input as MathExpressionEditor).imeOptions = updated
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
            val editorAction: (Int) -> Boolean = { action ->
                when (action) {
                    EditorInfo.IME_ACTION_NEXT -> {
                        val next = nextInput()
                        if (next != null) { next.requestFocus(); (next as? EditText)?.selectAll() } else finishEditing()
                        true
                    }
                    EditorInfo.IME_ACTION_DONE -> {
                        finishEditing()
                        true
                    }
                    else -> false
                }
            }
            if (input is EditText) {
                if (submitAnswer != null) PuzzleAnswerSubmit.install(input, submitAnswer)
                else input.setOnEditorActionListener { _, action, _ -> editorAction(action) }
            } else (input as MathExpressionEditor).apply {
                onImeAction = editorAction
                onSubmitAnswer = submitAnswer
            }
        }
    }
}
