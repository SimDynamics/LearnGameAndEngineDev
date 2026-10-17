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

    /** A deliberate selection overrides a stale field from another puzzle
     * or the independent compiler. Its own editing field remains protected.
     */
    fun selectedPuzzleOverridesFocusedEditor(focused: View?): Boolean {
        if (focused !is EditText) return false
        val selected = entries.lastOrNull {
            it.check === touchedCheck && it.check.isEnabled &&
                it.check.isAttachedToWindow && it.check.isShown
        } ?: return false
        if (belongsTo(focused, selected.region) || belongsTo(focused, selected.check)) return false
        return focused.tag == "cpp_code_input" || entries.any { entry ->
            entry !== selected &&
                (belongsTo(focused, entry.region) || belongsTo(focused, entry.check))
        }
    }

    /** The user's actual answer selection, not Android's incidental keyboard focus,
     * chooses which puzzle receives Enter. Narration controls never own this choice. */
    fun select(check: View) {
        if (entries.any { it.check === check }) touchedCheck = check
    }

    fun trackTouch(rawX: Float, rawY: Float): Boolean {
        fun contains(view: View): Boolean {
            val rect = Rect()
            return view.isShown && view.getGlobalVisibleRect(rect) &&
                rect.contains(rawX.toInt(), rawY.toInt())
        }
        // A one-touch LDPlayer mapping for Pause/Resume touches outside all puzzles.
        // It must NOT discard the student's previous deliberate puzzle selection.
        val target = entries.lastOrNull { contains(it.region) || contains(it.check) }
        target?.check?.let(::select)
        return target != null
    }

    private fun belongsTo(view: View?, ancestor: View): Boolean {
        var current = view
        while (current != null) {
            if (current === ancestor) return true
            current = current.parent as? View
        }
        return false
    }

    fun dispatch(
        event: KeyEvent,
        focused: View?,
        compilerFullscreen: Boolean,
        acceptRecoveredUp: Boolean = false
    ): Boolean {
        if (event.keyCode == KeyEvent.KEYCODE_TAB && event.action == KeyEvent.ACTION_DOWN)
            touchedCheck = null
        if (event.action != KeyEvent.ACTION_DOWN && event.action != KeyEvent.ACTION_UP) return false
        if (event.keyCode != KeyEvent.KEYCODE_ENTER &&
            event.keyCode != KeyEvent.KEYCODE_NUMPAD_ENTER) return false
        if (!event.hasNoModifiers() || compilerFullscreen) return false
        if (focused is MathExpressionEditor ||
            focused is EditText && !PuzzleAnswerSubmit.isSingleLine(focused) &&
                !selectedPuzzleOverridesFocusedEditor(focused)) return false

        val eligible = entries.filter {
            it.check.isAttachedToWindow && it.check.isShown &&
                it.check.isEnabled && it.check.isClickable
        }
        if (eligible.isEmpty()) return entries.isNotEmpty()
        val chosen = eligible.lastOrNull { it.check === touchedCheck }
            ?: eligible.lastOrNull {
                belongsTo(focused, it.region) || belongsTo(focused, it.check)
            }
            ?: eligible.singleOrNull()
        // Only a selected/active or unambiguous puzzle is submitted. We do not
        // guess among multiple prepared answers and consume students' attempts.
        if ((event.action == KeyEvent.ACTION_DOWN || acceptRecoveredUp) && BuildConfig.DEBUG) {
            android.util.Log.d("LgpEnterTrace",
                "puzzle eligible=${eligible.size} chosen=${chosen != null}" +
                    " selected=${touchedCheck != null} focused=${focused?.javaClass?.simpleName ?: "none"}")
        }
        // Ordinary UP never submits. The Activity gate alone can approve
        // recovery when the real physical DOWN did not reach the Activity.
        val submit = event.action == KeyEvent.ACTION_DOWN ||
            (acceptRecoveredUp && event.action == KeyEvent.ACTION_UP)
        if (chosen != null && submit && event.repeatCount == 0 && !event.isCanceled) {
            chosen.check.performClick()
        }
        return true
    }
}
