package com.learngraphicsprogramming.app

import android.text.InputType
import android.view.KeyEvent
import android.view.View
import android.view.inputmethod.EditorInfo
import android.widget.EditText

/** The keyboard submits the existing puzzle action; it never evaluates a second answer. */
internal object PuzzleAnswerSubmit {
    fun isSingleLine(input: EditText): Boolean = input.maxLines == 1 &&
        input.inputType and InputType.TYPE_TEXT_FLAG_MULTI_LINE == 0

    fun isSubmitAction(action: Int): Boolean = action == EditorInfo.IME_ACTION_DONE ||
        action == EditorInfo.IME_ACTION_GO || action == EditorInfo.IME_ACTION_SEND ||
        action == EditorInfo.IME_ACTION_NEXT

    fun install(input: EditText, button: View): Boolean = install(input) {
        if (button.isEnabled && button.visibility == View.VISIBLE) button.performClick()
    }

    fun install(input: EditText, submit: () -> Unit): Boolean {
        if (!isSingleLine(input)) return false
        input.imeOptions = (input.imeOptions and
            (EditorInfo.IME_MASK_ACTION or EditorInfo.IME_FLAG_NO_ENTER_ACTION).inv()) or
            EditorInfo.IME_ACTION_DONE or EditorInfo.IME_FLAG_NO_EXTRACT_UI or
            EditorInfo.IME_FLAG_NO_FULLSCREEN
        input.setOnEditorActionListener { _, action, event ->
            if (event == null) {
                if (!isSubmitAction(action)) false else {
                    if (input.isEnabled && input.keyListener != null) submit()
                    true
                }
            } else if ((event.keyCode == KeyEvent.KEYCODE_ENTER ||
                    event.keyCode == KeyEvent.KEYCODE_NUMPAD_ENTER) && event.hasNoModifiers() &&
                (event.action == KeyEvent.ACTION_DOWN || event.action == KeyEvent.ACTION_UP)) {
                // TextView delivers the physical key twice. Consume both halves so it
                // cannot navigate focus, and submit once on release (not autorepeat).
                if (event.action == KeyEvent.ACTION_UP && !event.isCanceled &&
                    input.isEnabled && input.keyListener != null) submit()
                true
            } else false
        }
        return true
    }
}
