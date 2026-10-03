package com.learngraphicsprogramming.app.math

import android.content.Context
import android.view.GestureDetector
import android.view.MotionEvent
import android.view.ScaleGestureDetector
import android.widget.FrameLayout

/** A passive surface can opt into double tap. Editable diagrams keep their original tap gestures. */
internal open class MathDoubleTapFrame(context: Context) : FrameLayout(context) {
    var onDoubleTap: (() -> Unit)? = null
    private var doubleTap = false
    private val gestures = GestureDetector(context, object : GestureDetector.SimpleOnGestureListener() {
        override fun onDoubleTap(event: MotionEvent): Boolean {
            doubleTap = true
            return true
        }

        override fun onDoubleTapEvent(event: MotionEvent): Boolean {
            if (event.actionMasked == MotionEvent.ACTION_UP && doubleTap) {
                doubleTap = false
                onDoubleTap?.invoke()
            }
            return true
        }
    })

    override fun dispatchTouchEvent(event: MotionEvent): Boolean {
        if (event.actionMasked == MotionEvent.ACTION_CANCEL || event.pointerCount > 1) doubleTap = false
        val recognized = onDoubleTap != null && gestures.onTouchEvent(event)
        if (recognized && doubleTap && event.actionMasked == MotionEvent.ACTION_DOWN) {
            val cancel = MotionEvent.obtain(event).apply { action = MotionEvent.ACTION_CANCEL }
            super.dispatchTouchEvent(cancel)
            cancel.recycle()
        }
        if (recognized) return true
        return super.dispatchTouchEvent(event) || onDoubleTap != null
    }
}

/** Zoom the same native visual; Android maps unclaimed taps through its transformed child matrix.
 * Two-finger gestures belong to the viewport. At 1x, all one-finger diagram gestures are unchanged.
 * When enlarged, dragging empty space pans; dragging an editable graph point still edits that point.
 */
internal open class MathZoomViewport(context: Context) : MathDoubleTapFrame(context) {
    private val transform = MathViewportTransform()
    val zoomScale: Float get() = transform.scale
    var onZoomChanged: ((Boolean) -> Unit)? = null
    private var viewportOwnsGesture = false
    private var childOwnsGesture = false
    private var lastX = 0f
    private var lastY = 0f
    private var scaleFocusX = 0f
    private var scaleFocusY = 0f
    private val scaleGestures = ScaleGestureDetector(context, object : ScaleGestureDetector.SimpleOnScaleGestureListener() {
        override fun onScaleBegin(detector: ScaleGestureDetector): Boolean {
            scaleFocusX = detector.focusX
            scaleFocusY = detector.focusY
            return true
        }

        override fun onScale(detector: ScaleGestureDetector): Boolean {
            transform.panBy(detector.focusX - scaleFocusX, detector.focusY - scaleFocusY)
            transform.zoomBy(detector.scaleFactor, detector.focusX, detector.focusY)
            scaleFocusX = detector.focusX
            scaleFocusY = detector.focusY
            applyTransform()
            return true
        }
    }).apply {
        isQuickScaleEnabled = false
        isStylusScaleEnabled = false
    }

    fun resetZoom() {
        transform.reset()
        applyTransform()
    }

    override fun onLayout(changed: Boolean, left: Int, top: Int, right: Int, bottom: Int) {
        super.onLayout(changed, left, top, right, bottom)
        val visual = getChildAt(0) ?: return
        transform.resize(width.toFloat(), height.toFloat(), visual.width.toFloat(), visual.height.toFloat())
        applyTransform()
    }

    private fun applyTransform() {
        getChildAt(0)?.let { visual ->
            visual.scaleX = transform.scale
            visual.scaleY = transform.scale
            visual.translationX = transform.panX
            visual.translationY = transform.panY
        }
        onZoomChanged?.invoke(transform.scale > 1f)
    }

    override fun dispatchTouchEvent(event: MotionEvent): Boolean {
        if (event.actionMasked == MotionEvent.ACTION_DOWN) {
            viewportOwnsGesture = false
            childOwnsGesture = false
            descendantWantsGesture = false
            lastX = event.x
            lastY = event.y
        }
        if (event.actionMasked == MotionEvent.ACTION_POINTER_DOWN && event.pointerCount >= 2) {
            viewportOwnsGesture = true
            val cancel = MotionEvent.obtain(event).apply { action = MotionEvent.ACTION_CANCEL }
            super.dispatchTouchEvent(cancel)
            cancel.recycle()
            parent?.requestDisallowInterceptTouchEvent(true)
        }
        scaleGestures.onTouchEvent(event)
        if (viewportOwnsGesture) {
            if (event.actionMasked == MotionEvent.ACTION_UP || event.actionMasked == MotionEvent.ACTION_CANCEL) {
                parent?.requestDisallowInterceptTouchEvent(false)
            }
            return true
        }
        if (event.actionMasked == MotionEvent.ACTION_MOVE && zoomScale > 1f && !childOwnsGesture) {
            super.dispatchTouchEvent(event)
            transform.panBy(event.x - lastX, event.y - lastY)
            applyTransform()
            lastX = event.x
            lastY = event.y
            return true
        }
        val handled = super.dispatchTouchEvent(event)
        if (event.actionMasked == MotionEvent.ACTION_DOWN) {
            // The superclass can keep passive taps alive for double tap. Only a real diagram
            // requesting interception owns a drag; otherwise enlarged empty space may pan.
            childOwnsGesture = descendantWantsGesture
        }
        // Keep the initial empty-space down alive so a second pointer can claim a pinch.
        return handled || zoomScale > 1f || event.actionMasked == MotionEvent.ACTION_DOWN
    }

    private var descendantWantsGesture = false
    override fun requestDisallowInterceptTouchEvent(disallowIntercept: Boolean) {
        descendantWantsGesture = disallowIntercept
        super.requestDisallowInterceptTouchEvent(disallowIntercept)
    }
}
