package com.learngraphicsprogramming.app

import org.junit.Assert.assertEquals
import org.junit.Test

class AttemptCountdownPlacementTest {
    /** 43dp icon scaled to 80%, centred inside a 68.8dp lives slot. */
    private fun pos(textWidth: Float, iconTop: Float = 12f, upperBottom: Float = 10f) =
        AttemptCountdownPlacement.offsets(
            iconLeft = 17.2f, iconTop = iconTop,
            iconRight = 51.6f, iconBottom = 46.4f,
            upperGlyphBottom = upperBottom,
            labelLeft = 0f, labelTop = 48f,
            labelWidth = 68.8f, labelHeight = 16f,
            textWidth = textWidth, glyphTopInsideLabel = 3f,
            slotBottom = 64f, minimumGap = 1f
        )

    @Test fun oneMinuteAndSevenHoursRemainExactlyCenteredOnConsole() {
        val short = pos(12f)
        val long = pos(43f)
        assertEquals(short.x, long.x, 0.001f)
        assertEquals(0f, short.x, 0.001f)
        assertEquals(0f, long.x, 0.001f)
        assertEquals(short.y, long.y, 0.001f)
    }

    @Test fun bottomGapMatchesMeasuredTopGap() {
        val actual = pos(43f)
        // Top gap: icon begins at 12px, top glyph ends at 10px = 2px.
        // The lower glyph starts 2px below icon bottom (46.4 + 2).
        assertEquals(48.4f, 48f + actual.y + 3f, 0.001f)
    }

    @Test fun gapHasOneDpMinimumWhenTopLabelsOverlapIcon() {
        val actual = pos(12f, upperBottom = 15f)
        assertEquals(47.4f, 48f + actual.y + 3f, 0.001f)
    }

    @Test fun doesNotLeaveTheSmallLivesSlotWhenGlyphsNeedMoreRoom() {
        val actual = pos(43f, upperBottom = 6f)
        assertEquals(48f, 48f + actual.y, 0.001f) // 64 - 16 max top.
    }

    @Test fun shiftedIconRetainsOwnAxisRegardlessOfSlotWidth() {
        val shifted = AttemptCountdownPlacement.offsets(
            iconLeft = 20f, iconTop = 12f, iconRight = 54.4f, iconBottom = 46.4f,
            upperGlyphBottom = 10f,
            labelLeft = 0f, labelTop = 48f, labelWidth = 68.8f, labelHeight = 16f,
            textWidth = 40f, glyphTopInsideLabel = 3f,
            slotBottom = 64f, minimumGap = 1f
        )
        assertEquals(2.8f, shifted.x, 0.001f)
    }
}
