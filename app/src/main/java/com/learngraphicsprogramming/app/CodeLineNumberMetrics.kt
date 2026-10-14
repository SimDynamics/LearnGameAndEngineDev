package com.learngraphicsprogramming.app

import kotlin.math.ceil

/** Width of complete labels, including blank continuation rows, at the current text size. */
internal object CodeLineNumberMetrics {
    fun requiredWidth(
        labels: String,
        measure: (String) -> Float,
        horizontalPadding: Int,
        minimumWidth: Int
    ): Int {
        require(horizontalPadding >= 0 && minimumWidth >= 0)
        var widest = 0f
        for (label in labels.split('\n')) {
            val measured = measure(label)
            require(measured.isFinite() && measured >= 0f)
            widest = maxOf(widest, measured)
        }
        // Round upward: truncating a fractional glyph extent can make TextView wrap a digit.
        return maxOf(minimumWidth, ceil(widest.toDouble()).toInt() + horizontalPadding + 1)
    }
}
