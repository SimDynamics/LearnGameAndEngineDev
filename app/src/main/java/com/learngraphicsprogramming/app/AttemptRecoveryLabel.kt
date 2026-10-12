package com.learngraphicsprogramming.app

import kotlin.math.ceil

/** Remaining time to refill the ENTIRE reserve, not just the next life. */
internal object AttemptRecoveryLabel {
    fun format(maximum: Int, reserveUnits: Float, unlimited: Boolean,
               recoveryMillis: Long = 3_600_000L): String {
        if (unlimited || maximum <= 0 || !reserveUnits.isFinite() || recoveryMillis <= 0) return ""
        val missing = (maximum.toDouble() - reserveUnits.toDouble()).coerceIn(0.0, maximum.toDouble())
        if (missing <= 0.0) return ""
        // Float reserve precision can straddle an exact minute by a few milliseconds.
        val minutes = ceil(missing * recoveryMillis / 60_000.0 - 0.0001).toLong().coerceAtLeast(1L)
        val hours = minutes / 60L
        val rest = minutes % 60L
        return when {
            hours == 0L -> "${minutes}m"
            rest == 0L -> "${hours}h"
            else -> "${hours}h ${rest}m"
        }
    }
}
