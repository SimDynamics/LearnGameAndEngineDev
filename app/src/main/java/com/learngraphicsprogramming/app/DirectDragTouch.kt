package com.learngraphicsprogramming.app

import android.view.InputDevice
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
        // Reuse the reader's pointer/source gate with a shorter puzzle-only hold.
        // A responsive card drag takes priority over filtering slow emulator wheel
        // translations; native wheel events still never start a drag.
        val dragGate = FloatingReaderDragGate(touchHoldMs = 20L)
        val density = view.resources.displayMetrics.density
        // Same responsive threshold as Programming's existing ordering/code palette.
        val threshold = maxOf((2 * density).toInt(), ViewConfiguration.get(view.context).scaledTouchSlop / 2)
        fun fromMouse(event: MotionEvent) = event.isFromSource(InputDevice.SOURCE_MOUSE) ||
            (event.pointerCount > 0 && event.getToolType(0) == MotionEvent.TOOL_TYPE_MOUSE)
        fun primaryPressed(event: MotionEvent) = (event.buttonState and MotionEvent.BUTTON_PRIMARY) != 0
        fun moved(event: MotionEvent) = abs(event.x - downX) >= threshold || abs(event.y - downY) >= threshold
        view.setOnGenericMotionListener { target, event ->
            if (event.actionMasked == MotionEvent.ACTION_SCROLL) {
                dragGate.reject()
                if (dragging) target.cancelDragAndDrop()
                dragging = false
                target.alpha = 1f
                target.parent?.requestDisallowInterceptTouchEvent(false)
            }
            // Native wheel events remain available to the surrounding lesson scroll.
            false
        }
        view.setOnTouchListener { target, event ->
            if (!target.isEnabled || !canInteract()) return@setOnTouchListener false
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN -> {
                    onGestureClaim()
                    dragging = false
                    downX = event.x
                    downY = event.y
                    dragGate.begin(event.eventTime, fromMouse(event), primaryPressed(event))
                    target.parent?.requestDisallowInterceptTouchEvent(!dragGate.isRejected)
                    true
                }
                MotionEvent.ACTION_MOVE -> {
                    if (event.pointerCount != 1) dragGate.reject()
                    val mayDrag = dragGate.mayDrag(event.eventTime, moved(event), fromMouse(event), primaryPressed(event))
                    if (dragGate.isRejected) {
                        target.parent?.requestDisallowInterceptTouchEvent(false)
                        return@setOnTouchListener true
                    }
                    if (!dragGate.canClick) return@setOnTouchListener false
                    onGestureClaim()
                    target.parent?.requestDisallowInterceptTouchEvent(true)
                    if (!dragging && mayDrag) {
                        target.alpha = .55f
                        dragging = startDrag(target)
                        if (!dragging) target.alpha = 1f
                    }
                    true
                }
                MotionEvent.ACTION_UP -> {
                    target.parent?.requestDisallowInterceptTouchEvent(false)
                    if (!dragging && dragGate.canClick && !moved(event)) target.performClick()
                    dragGate.end()
                    true
                }
                MotionEvent.ACTION_CANCEL -> {
                    target.parent?.requestDisallowInterceptTouchEvent(false)
                    if (!dragging) target.alpha = 1f
                    // Android sends CANCEL when a native drag takes ownership; its
                    // normal DRAG_ENDED callback still owns cleanup of a real drag.
                    dragGate.end()
                    true
                }
                MotionEvent.ACTION_POINTER_DOWN, MotionEvent.ACTION_POINTER_UP -> {
                    dragGate.reject()
                    target.parent?.requestDisallowInterceptTouchEvent(false)
                    true
                }
                else -> false
            }
        }
    }

    fun finish(view: View) {
        view.alpha = 1f
        view.parent?.requestDisallowInterceptTouchEvent(false)
    }
}
