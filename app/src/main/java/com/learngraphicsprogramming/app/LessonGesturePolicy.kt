package com.learngraphicsprogramming.app

import android.graphics.Rect
import android.view.View
import android.view.ViewGroup
import android.widget.EditText
import android.widget.HorizontalScrollView
import android.widget.SeekBar
import android.widget.TextView

/** Decide ownership at ACTION_DOWN, before a child consumes the gesture. */
internal object LessonGesturePolicy {
    const val INTERACTIVE_TAG = "math_gesture_owner"

    private fun hitPath(view: View, rawX: Float, rawY: Float): List<View> {
        val bounds = Rect()
        val path = mutableListOf<View>()
        fun hit(candidate: View): Boolean {
            if (candidate.visibility != View.VISIBLE || !candidate.getGlobalVisibleRect(bounds) ||
                !bounds.contains(rawX.toInt(), rawY.toInt())) return false
            path += candidate
            if (candidate is ViewGroup) {
                for (index in candidate.childCount - 1 downTo 0) {
                    if (hit(candidate.getChildAt(index))) break
                }
            }
            return true
        }
        return if (hit(view) && path.none { !it.isEnabled }) path else emptyList()
    }

    fun startsInEmptyCppEditor(view: View, rawX: Float, rawY: Float): Boolean =
        hitPath(view, rawX, rawY).filterIsInstance<FreeScrollEditText>().any {
            it.cppEditorViewport?.startsInEmptySpace(rawX, rawY) == true
        }

    fun isOwnedByControl(view: View, rawX: Float, rawY: Float): Boolean {
        // Tap targets do not require horizontal ownership; short formulas can be swiped.
        return hitPath(view, rawX, rawY).any { target ->
            when (target) {
                is HorizontalScrollView -> target.canScrollHorizontally(-1) || target.canScrollHorizontally(1)
                is FreeScrollEditText -> target.cppEditorViewport?.ownsHorizontalGestureAt(rawX, rawY)
                    ?: (target.hasFocus() && target.isFocusable)
                is EditText -> target.hasFocus() && target.isFocusable
                is SeekBar -> true
                else -> target.tag == INTERACTIVE_TAG ||
                    (target is TextView && target.isTextSelectable && target.hasFocus())
            }
        }
    }
}
