package com.learngraphicsprogramming.app.math

import android.content.Context
import android.graphics.Rect
import android.view.MotionEvent
import android.view.View
import android.view.ViewConfiguration
import android.view.ViewGroup
import android.widget.Button
import android.widget.CompoundButton
import android.widget.EditText
import android.widget.HorizontalScrollView
import android.widget.ImageButton
import android.widget.LinearLayout
import android.widget.Spinner
import kotlin.math.abs

/** Makes a rich answer's whole surface respond to taps and card drags, including formula Views. */
internal class MathTouchCard(context: Context) : LinearLayout(context) {
    private enum class Surface { CARD, SCROLL, CHILD }
    private var surface = Surface.CARD
    private var downX = 0f
    private var downY = 0f
    private var clicked = false
    private val slop = ViewConfiguration.get(context).scaledTouchSlop

    init {
        orientation = VERTICAL
        layoutParams = LayoutParams(-1, -2)
        minimumHeight = MathWidgetUi.dp(context, 48)
    }

    override fun dispatchTouchEvent(event: MotionEvent): Boolean {
        if (event.actionMasked == MotionEvent.ACTION_DOWN) {
            clicked = false
            downX = event.x
            downY = event.y
            surface = classify(this, event.rawX, event.rawY)
        }
        val handled = super.dispatchTouchEvent(event)
        // A fitting formula is intercepted by the card. A scrollable formula keeps scrolling,
        // but its stationary taps still select the surrounding option rather than disappearing.
        if (event.actionMasked == MotionEvent.ACTION_UP && surface == Surface.SCROLL &&
            isEnabled && !clicked && abs(event.x - downX) <= slop && abs(event.y - downY) <= slop) {
            performClick()
            return true
        }
        return handled
    }

    override fun onInterceptTouchEvent(event: MotionEvent): Boolean =
        isEnabled && surface == Surface.CARD

    override fun performClick(): Boolean {
        clicked = true
        return super.performClick()
    }

    private fun classify(view: View, rawX: Float, rawY: Float): Surface {
        val bounds = Rect()
        if (view.visibility != VISIBLE || !view.getGlobalVisibleRect(bounds) ||
            !bounds.contains(rawX.toInt(), rawY.toInt())) return Surface.CARD
        if (view !== this && view.isEnabled) {
            if (view is MathTouchCard || view is ImageButton ||
                (view is Button && view !is CompoundButton) || view is EditText || view is Spinner) return Surface.CHILD
        }
        if (view is ViewGroup) {
            for (index in view.childCount - 1 downTo 0) {
                val child = view.getChildAt(index)
                if (child.visibility == VISIBLE && child.getGlobalVisibleRect(bounds) &&
                    bounds.contains(rawX.toInt(), rawY.toInt())) {
                    val result = classify(child, rawX, rawY)
                    if (result != Surface.CARD) return result
                    break
                }
            }
        }
        if (view is HorizontalScrollView && view.isEnabled &&
            (view.canScrollHorizontally(-1) || view.canScrollHorizontally(1))) return Surface.SCROLL
        return Surface.CARD
    }
}
