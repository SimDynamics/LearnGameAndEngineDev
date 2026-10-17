package com.learngraphicsprogramming.app

import android.graphics.Rect
import android.view.KeyEvent
import android.view.View
import android.widget.EditText
import com.learngraphicsprogramming.app.math.MathExpressionEditor

/** The exact native Check button is registered for each cached page. */
internal class PuzzleEnterShortcut {
    private data class Entry(val check: View, val region: View)
    private val pageEntries = mutableMapOf<Int, MutableList<Entry>>()
    private var lesson: String? = null
    private var page = -1
    private var touchedCheck: View? = null
    private val entries: List<Entry> get() = pageEntries[page].orEmpty()

    fun activatePage(lessonKey: String, pageIndex: Int, reused: Boolean) {
        if (lesson != lessonKey) {
            pageEntries.clear()
            lesson = lessonKey
        }
        page = pageIndex
        if (!reused) pageEntries.remove(page)
        touchedCheck = null
    }

    fun register(check: View, region: View) {
        require(page >= 0) { "Activate a lesson page before adding puzzle shortcuts" }
        pageEntries.getOrPut(page) { mutableListOf() }.add(Entry(check, region))
    }

    fun clear() {
        pageEntries.clear()
        lesson = null
        page = -1
        touchedCheck = null
    }

    fun trackTouch(rawX: Float, rawY: Float) {
        fun contains(view: View): Boolean {
            val rect = Rect()
            return view.isShown && view.getGlobalVisibleRect(rect) &&
                rect.contains(rawX.toInt(), rawY.toInt())
        }
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
        if (event.keyCode == KeyEvent.KEYCODE_TAB && event.action == KeyEvent.ACTION_DOWN)
            touchedCheck = null
        if (event.action != KeyEvent.ACTION_DOWN && event.action != KeyEvent.ACTION_UP) return false
        if (event.keyCode != KeyEvent.KEYCODE_ENTER &&
            event.keyCode != KeyEvent.KEYCODE_NUMPAD_ENTER) return false
        if (!event.hasNoModifiers() || compilerFullscreen) return false
        if (focused is MathExpressionEditor ||
            focused is EditText && !PuzzleAnswerSubmit.isSingleLine(focused)) return false

        val eligible = entries.filter {
            it.check.isAttachedToWindow && it.check.isShown &&
                it.check.isEnabled && it.check.isClickable
        }
        if (eligible.isEmpty()) return entries.isNotEmpty()
        val chosen = eligible.lastOrNull {
            belongsTo(focused, it.region) || belongsTo(focused, it.check)
        } ?: eligible.lastOrNull { it.check === touchedCheck }
          ?: eligible.singleOrNull()
        // Only a selected/active or unambiguous puzzle is submitted. We do not
        // guess among multiple prepared answers and consume students' attempts.
        if (chosen != null && event.action == KeyEvent.ACTION_DOWN &&
            event.repeatCount == 0 && !event.isCanceled) chosen.check.performClick()
        return true
    }
}
