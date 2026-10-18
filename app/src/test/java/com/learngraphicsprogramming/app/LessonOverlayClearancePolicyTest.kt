package com.learngraphicsprogramming.app

import org.junit.Assert.assertEquals
import org.junit.Test

class LessonOverlayClearancePolicyTest {
    private val viewport = LessonOverlayBounds(0, 300, 1080, 2070)

    @Test fun observedFeedbackCanScrollAboveTheControlsWithoutMovingThem() {
        val controls = LessonOverlayBounds(27, 1877, 1060, 2046)
        val extra = LessonOverlayClearancePolicy.bottomClearance(viewport, controls, 24)
        assertEquals(217, extra)
        // The original final line was at 1938; the additional legal scroll makes
        // its bottom strictly higher than the unchanged top of the control strip.
        assertEquals(1721, 1938 - extra)
    }

    @Test fun absentOrEntirelyOutsideControlsNeedNoReservation() {
        for (controls in listOf(null,
            LessonOverlayBounds(1080, 1800, 1200, 1900),
            LessonOverlayBounds(-120, 1800, 0, 1900),
            LessonOverlayBounds(0, 2070, 1000, 2126),
            LessonOverlayBounds(0, 100, 1000, 300))) {
            assertEquals(0, LessonOverlayClearancePolicy.bottomClearance(viewport, controls, 24))
        }
    }

    @Test fun partiallyClippedControlsReserveOnlyTheirIntersectionDepth() {
        assertEquals(217, LessonOverlayClearancePolicy.bottomClearance(viewport,
            LessonOverlayBounds(-200, 1877, 500, 2500), 24))
        assertEquals(1794, LessonOverlayClearancePolicy.bottomClearance(viewport,
            LessonOverlayBounds(0, -100, 500, 356), 24))
    }

    @Test fun movingControlsOrResizingTheViewportUsesTheirCurrentBounds() {
        assertEquals(70, LessonOverlayClearancePolicy.bottomClearance(
            LessonOverlayBounds(0, 120, 1920, 1000),
            LessonOverlayBounds(500, 938, 1500, 994), 8))
        assertEquals(370, LessonOverlayClearancePolicy.bottomClearance(
            LessonOverlayBounds(0, 120, 1920, 1000),
            LessonOverlayBounds(500, 638, 1500, 694), 8))
    }

    @Test fun unmeasuredOrEmptyRectsNeverAddSpace() {
        assertEquals(0, LessonOverlayClearancePolicy.bottomClearance(
            LessonOverlayBounds(0, 300, 1080, 300), LessonOverlayBounds(0, 280, 1000, 350), 24))
        assertEquals(0, LessonOverlayClearancePolicy.bottomClearance(
            viewport, LessonOverlayBounds(0, 1800, 0, 1856), 24))
    }

    @Test fun clearanceDoesNotCountTheSameOverlayMoreThanOnce() {
        val controls = LessonOverlayBounds(27, 1877, 1060, 2046)
        repeat(50) {
            assertEquals(217, LessonOverlayClearancePolicy.bottomClearance(viewport, controls, 24))
        }
    }
}
