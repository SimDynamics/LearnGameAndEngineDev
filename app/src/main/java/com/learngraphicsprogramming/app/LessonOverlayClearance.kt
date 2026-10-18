package com.learngraphicsprogramming.app

import android.graphics.Rect
import android.view.View
import android.view.ViewTreeObserver
import android.widget.ScrollView
import kotlin.math.roundToInt

/**
 * Reserves a measured scrollable tail for floating narration controls.
 *
 * The keyboard owns ScrollView padding. This helper instead pads its content wrapper,
 * after the page and Continue button, leaving page/fullscreen padding and audio alone.
 */
internal class LessonOverlayClearance(
    private val scroll: ScrollView,
    private val overlay: () -> View?
) : AutoCloseable {
    private val content = requireNotNull(scroll.getChildAt(0))
    private val originalBottom = content.paddingBottom
    private val gapPx = (8f * scroll.resources.displayMetrics.density).roundToInt()
    private val viewportBounds = Rect()
    private val overlayBounds = Rect()
    private var closed = false
    private val observer = scroll.viewTreeObserver
    private val preDrawListener = ViewTreeObserver.OnPreDrawListener {
        // Translations from dragging do not trigger OnLayoutChangeListener. Inspect
        // the actual drawn bounds, including clipping, before each scheduled draw.
        update()
        // setPadding requests the next layout. Returning false would cancel the
        // entire window's drawing on every drag frame and freeze the controls.
        true
    }

    init {
        observer.addOnPreDrawListener(preDrawListener)
    }

    private fun update(): Boolean {
        if (closed) return false
        val controls = overlay()
        val visible = scroll.isShown && scroll.getGlobalVisibleRect(viewportBounds) &&
            controls?.isShown == true && controls.alpha > 0f &&
            controls.getGlobalVisibleRect(overlayBounds)
        val clearance = if (visible) {
            // Exclude any space already occupied by the keyboard's scroll padding.
            LessonOverlayClearancePolicy.bottomClearance(
                LessonOverlayBounds(
                    viewportBounds.left + scroll.paddingLeft,
                    viewportBounds.top + scroll.paddingTop,
                    viewportBounds.right - scroll.paddingRight,
                    viewportBounds.bottom - scroll.paddingBottom
                ),
                LessonOverlayBounds(overlayBounds.left, overlayBounds.top,
                    overlayBounds.right, overlayBounds.bottom),
                gapPx
            )
        } else 0
        val bottom = originalBottom + clearance
        if (content.paddingBottom == bottom) return false
        content.setPadding(content.paddingLeft, content.paddingTop, content.paddingRight, bottom)
        return true
    }

    override fun close() {
        if (closed) return
        closed = true
        if (observer.isAlive) observer.removeOnPreDrawListener(preDrawListener)
        else if (scroll.viewTreeObserver.isAlive) scroll.viewTreeObserver.removeOnPreDrawListener(preDrawListener)
        content.setPadding(content.paddingLeft, content.paddingTop, content.paddingRight, originalBottom)
    }
}
