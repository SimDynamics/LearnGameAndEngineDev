package com.learngraphicsprogramming.app

import android.app.Activity
import android.graphics.Rect
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.view.MotionEvent
import android.view.View
import android.view.Window
import android.widget.PopupWindow

/** Capture overlays BEFORE the lesson's swipe observer sees the gesture. */
internal class LessonFloatingInteractions(private val activity: Activity) {
    private val handler = Handler(Looper.getMainLooper())
    private val ownership = FloatingGestureOwnership()
    private val lifetime = PopupLifetime()
    private var popup: PopupWindow? = null
    private var timeout: Runnable? = null
    private var wrapper: Window.Callback? = null
    private var previous: Window.Callback? = null
    private var readerTarget: View? = null
    private var closed = false

    fun install() {
        if (closed || wrapper != null) return
        val original = activity.window.callback ?: return
        previous = original
        val callback = object : Window.Callback by original {
            override fun dispatchTouchEvent(event: MotionEvent): Boolean {
                if (event.actionMasked == MotionEvent.ACTION_DOWN) {
                    val reader = activity.findViewById<View>(R.id.lessonNarrationControls)
                    val hitReader = inside(reader,event)
                    val open = popup?.isShowing == true
                    val anchor = activity.findViewById<View>(R.id.attemptIndicatorSlot)
                    val hitAnchor = open && inside(anchor,event)
                    ownership.begin(hitReader,hitAnchor)
                    readerTarget = reader.takeIf { hitReader }
                    // Outside dismissal is handled here, not by PopupWindow on DOWN.
                    // Otherwise the anchor's UP sees a closed popup and reopens it.
                    if (open && !inside(popup?.contentView,event)) dismissPopup()
                }
                val terminal = event.actionMasked == MotionEvent.ACTION_UP || event.actionMasked == MotionEvent.ACTION_CANCEL
                try {
                    if (ownership.consumeAnchorGesture) return true
                    if (ownership.readerOwnsGesture) {
                        readerTarget?.takeIf { it.isAttachedToWindow }?.let { target ->
                            val location = IntArray(2).also(target::getLocationOnScreen)
                            val local = MotionEvent.obtain(event)
                            try {
                                local.offsetLocation(event.rawX-event.x-location[0], event.rawY-event.y-location[1])
                                target.dispatchTouchEvent(local)
                            } finally { local.recycle() }
                        }
                        // Never pass an orphan UP to the page, even if the overlay disappeared.
                        return true
                    }
                    return original.dispatchTouchEvent(event)
                } finally {
                    if (terminal) { ownership.end(); readerTarget = null }
                }
            }
            override fun dispatchGenericMotionEvent(event: MotionEvent): Boolean {
                if (event.actionMasked == MotionEvent.ACTION_SCROLL) dismissPopup()
                return original.dispatchGenericMotionEvent(event)
            }
        }
        wrapper = callback
        activity.window.callback = callback
    }

    private fun inside(view: View?, event: MotionEvent): Boolean {
        if (view == null || !view.isShown) return false
        val area = Rect()
        val location = IntArray(2).also(view::getLocationOnScreen)
        area.set(location[0],location[1],location[0]+view.width,location[1]+view.height)
        return area.contains(event.rawX.toInt(),event.rawY.toInt())
    }

    fun shown(value: PopupWindow) {
        install()
        timeout?.let(handler::removeCallbacks)
        popup = value
        value.setTouchInterceptor { _, event ->
            if (event.actionMasked == MotionEvent.ACTION_DOWN || event.actionMasked == MotionEvent.ACTION_SCROLL) {
                ownership.begin(false,true)
                value.dismiss()
                true
            } else false
        }
        value.contentView.setOnGenericMotionListener { _, event ->
            if (event.actionMasked == MotionEvent.ACTION_SCROLL) { value.dismiss(); true } else false
        }
        val token = lifetime.opened(SystemClock.uptimeMillis())
        val task = Runnable {
            if (popup === value && lifetime.expired(token,SystemClock.uptimeMillis())) value.dismiss()
        }
        timeout = task
        handler.postDelayed(task,PopupLifetime.TIMEOUT_MS)
    }
    fun dismissed(value: PopupWindow) {
        if (popup !== value) return
        timeout?.let(handler::removeCallbacks)
        timeout = null
        popup = null
        lifetime.dismissed()
    }
    fun dismissPopup() { popup?.dismiss() }
    fun close() {
        dismissPopup()
        timeout?.let(handler::removeCallbacks)
        timeout = null
        ownership.end(); readerTarget = null
        if (activity.window.callback === wrapper) previous?.let { activity.window.callback = it }
        wrapper = null; previous = null; closed = true
    }
}
