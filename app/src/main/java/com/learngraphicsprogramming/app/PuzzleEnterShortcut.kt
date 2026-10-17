package com.learngraphicsprogramming.app

import android.graphics.Rect
import android.view.KeyEvent
import android.view.View
import android.widget.EditText
import com.learngraphicsprogramming.app.math.MathExpressionEditor

/**
 * One current-page shortcut registry, never an alternative answer evaluator.
 * Each entry invokes the exact existing Comprobar button with its normal state,
 * attempt limits, feedback and persistence. All entries are discarded on page render.
 */
internal class PuzzleEnterShortcut {
    private data class Entry(val check: View, val region: View)
    private val entries = mutableListOf<Entry>()
    private var touchedCheck: View? = null

    fun register(check: View, region: View) {
        entries += Entry(check, region)
    }

    fun clear() {
        entries.clear()
        touchedCheck = null
    }

    fun trackTouch(rawX: Float, rawY: Float) {
        fun contains(view: View): Boolean {
            val rect = Rect()
            return view.isShown && view.getGlobalVisibleRect(rect) &&
                rect.contains(rawX.toInt(), rawY.toInt())
        }
        // The action often sits below its puzzle body rather than inside it.
        touchedCheck = entries.lastOrNull { contains(it.region) || contains(it.check) }?.check
    }

    private fun belongsTo(view: View?, ancestor: View): Boolean {
        var current = view
        while (current != null) {
            if (current === ancestor) return true
            current = current.parent as? View
        }
        return false
    }

    fun dispatch(event: KeyEvent, focused: View?, compilerFullscreen: Boolean): Boolean {
        if (event.keyCode == KeyEvent.KEYCODE_TAB && event.action == KeyEvent.ACTION_DOWN) {
            touchedCheck = null
        }
        if (event.action != KeyEvent.ACTION_DOWN && event.action != KeyEvent.ACTION_UP) return false
        if (event.keyCode != KeyEvent.KEYCODE_ENTER &&
            event.keyCode != KeyEvent.KEYCODE_NUMPAD_ENTER) return false
        if (!event.hasNoModifiers() || compilerFullscreen) return false
        // Structured mathematics owns its cursor/IME protocol. Compiler and multiline
        // editors need literal line breaks, never a click on an unrelated puzzle.
        if (focused is MathExpressionEditor ||
            focused is EditText && !PuzzleAnswerSubmit.isSingleLine(focused)) return false

        val available = entries.filter { it.check.isAttachedToWindow && it.check.isShown }
        val focusedPuzzle = available.lastOrNull { belongsTo(focused, it.region) || belongsTo(focused, it.check) }
        // A separately focused navigation or settings button keeps its own Enter behavior.
        if (focusedPuzzle == null && touchedCheck == null && focused?.isClickable == true) return false
        val chosen = focusedPuzzle
            ?: available.lastOrNull { it.check === touchedCheck }
            ?: available.singleOrNull()
            ?: return false
        // Consume down and up to prevent a second IME/focused-view submission.
        if (event.action == KeyEvent.ACTION_UP && !event.isCanceled &&
            chosen.check.isEnabled && chosen.check.isClickable) chosen.check.performClick()
        return true
    }
}
