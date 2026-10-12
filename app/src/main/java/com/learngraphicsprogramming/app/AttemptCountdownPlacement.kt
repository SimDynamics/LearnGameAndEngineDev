package com.learngraphicsprogramming.app

/** Screen coordinates are measured AFTER Android's 80% scale on the lives icon. */
internal object AttemptCountdownPlacement {
    data class Offset(val x: Float, val y: Float)

    /**
     * The text is centred over the actual console graphic, not its 86dp unscaled parent.
     * The spacing BELOW the graphic matches the visible glyph-to-graphic spacing ABOVE.
     * If the upper labels overlap the top of the icon, retain at least a 1dp air gap.
     * Only the final label moves; count, percentage, fill and icon stay untouched.
     */
    fun offsets(
        iconLeft: Float, iconTop: Float, iconRight: Float, iconBottom: Float,
        upperGlyphBottom: Float,
        labelLeft: Float, labelTop: Float, labelWidth: Float, labelHeight: Float,
        textWidth: Float, glyphTopInsideLabel: Float,
        slotBottom: Float, minimumGap: Float
    ): Offset {
        require(iconRight >= iconLeft && iconBottom >= iconTop)
        require(labelWidth >= 0f && labelHeight >= 0f)
        require(textWidth >= 0f && minimumGap >= 0f)
        // Android gravity=CENTER centres the actual glyph run in the TextView.
        // Use the measured text width so both "1m" and "7h 55m" share the icon axis.
        val baseTextLeft = labelLeft + (labelWidth - textWidth) / 2f
        val desiredTextLeft = (iconLeft + iconRight - textWidth) / 2f
        val topGap = (iconTop - upperGlyphBottom).coerceAtLeast(minimumGap)
        val desiredLabelTop = (iconBottom + topGap - glyphTopInsideLabel)
            .coerceAtMost(slotBottom - labelHeight)
        return Offset(desiredTextLeft - baseTextLeft, desiredLabelTop - labelTop)
    }
}
