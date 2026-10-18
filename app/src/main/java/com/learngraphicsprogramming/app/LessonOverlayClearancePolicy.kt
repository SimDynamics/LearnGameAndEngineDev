package com.learngraphicsprogramming.app

/** Window-coordinate bounds, independent of Android for geometry regression tests. */
internal data class LessonOverlayBounds(val left: Int, val top: Int, val right: Int, val bottom: Int) {
    val hasArea: Boolean get() = right > left && bottom > top
}

internal object LessonOverlayClearancePolicy {
    /** Extra scrollable tail lets the last content line reach above a visible overlay. */
    fun bottomClearance(viewport: LessonOverlayBounds, overlay: LessonOverlayBounds?, gapPx: Int): Int {
        require(gapPx >= 0)
        if (!viewport.hasArea || overlay == null || !overlay.hasArea ||
            overlay.right <= viewport.left || overlay.left >= viewport.right ||
            overlay.bottom <= viewport.top || overlay.top >= viewport.bottom) return 0

        return viewport.bottom - maxOf(viewport.top, overlay.top) + gapPx
    }
}
