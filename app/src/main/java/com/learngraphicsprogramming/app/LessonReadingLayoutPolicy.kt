package com.learngraphicsprogramming.app

import kotlin.math.roundToInt

/** Geometry shared by the lesson views and their JVM regression tests. */
internal object LessonReadingLayoutPolicy {
    // 0.01 percent, not 1 percent. Two dp make this visible at ordinary widths.
    const val HORIZONTAL_FRACTION = 0.0001
    const val MIN_HORIZONTAL_DP = 2.0

    fun horizontalInset(widthPx: Int, density: Float): Int {
        require(density.isFinite() && density > 0f)
        if (widthPx <= 0) return 0
        return maxOf(widthPx * HORIZONTAL_FRACTION, MIN_HORIZONTAL_DP * density)
            .roundToInt().coerceAtLeast(1).coerceAtMost(widthPx / 2)
    }

    fun inlineHeader(widthPx: Int, labelRoomPx: Int, density: Float, fontScale: Float): Boolean {
        require(density.isFinite() && density > 0f)
        require(fontScale.isFinite() && fontScale > 0f)
        return widthPx >= 600f * density &&
            labelRoomPx >= 160f * density * maxOf(1f, fontScale)
    }

    fun centeredTop(availableHeight: Int, childHeight: Int): Int =
        ((availableHeight - childHeight) / 2).coerceAtLeast(0)
}
