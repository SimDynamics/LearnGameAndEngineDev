package com.learngraphicsprogramming.app

import android.view.MotionEvent
import android.view.View
import android.view.ViewConfiguration
import kotlin.math.abs

/** Shared direct-drag/tap gesture used by lesson ordering and matching cards. */
object DirectDragTouch {
    fun bind(
        view: View,
        canInteract: () -> Boolean,
        startDrag: (View) -> Boolean,
        onGestureClaim: () -> Unit = {}
    ) {
        var downX = 0f
        var downY = 0f
        var dragging = false
        val density = view.resources.displayMetrics.density
        // Same responsive threshold as Programming's existing ordering/code palette.
        val threshold = maxOf((2 * density).toInt(), ViewConfiguration.get(view.context).scaledTouchSlop / 2)
        view.setOnTouchListener { target, event ->
            if (!target.isEnabled || !canInteract()) return@setOnTouchListener false
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN -> {
                    onGestureClaim()
                    dragging = false
                    downX = event.x
                    downY = event.y
                    target.parent?.requestDisallowInterceptTouchEvent(true)
                    true
                }
                MotionEvent.ACTION_MOVE -> {
                    onGestureClaim()
                    target.parent?.requestDisallowInterceptTouchEvent(true)
                    if (!dragging && (abs(event.x - downX) >= threshold || abs(event.y - downY) >= threshold)) {
                        target.alpha = .55f
                        dragging = startDrag(target)
                        if (!dragging) target.alpha = 1f
                    }
                    true
                }
                MotionEvent.ACTION_UP -> {
                    target.parent?.requestDisallowInterceptTouchEvent(false)
                    if (!dragging) target.performClick()
                    true
                }
                MotionEvent.ACTION_CANCEL -> {
                    target.parent?.requestDisallowInterceptTouchEvent(false)
                    if (!dragging) target.alpha = 1f
                    true
                }
                else -> true
            }
        }
    }

    fun finish(view: View) {
        view.alpha = 1f
        view.parent?.requestDisallowInterceptTouchEvent(false)
    }
}
