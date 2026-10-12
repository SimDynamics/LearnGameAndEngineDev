package com.learngraphicsprogramming.app

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class LessonReadingLayoutPolicyTest {
    @Test fun requestedFractionIsOneHundredthOfOnePercent() {
        assertEquals(0.0001, LessonReadingLayoutPolicy.HORIZONTAL_FRACTION, 0.00000001)
    }

    @Test fun marginRemainsVisibleOnPhonesAndFourKWindows() {
        assertEquals(2, LessonReadingLayoutPolicy.horizontalInset(360, 1f))
        assertEquals(2, LessonReadingLayoutPolicy.horizontalInset(3840, 1f))
        assertEquals(4, LessonReadingLayoutPolicy.horizontalInset(3840, 2f))
        assertEquals(6, LessonReadingLayoutPolicy.horizontalInset(1080, 3f))
    }

    @Test fun fractionCanGrowWithoutCompoundingOnRepeatedLayout() {
        assertEquals(5, LessonReadingLayoutPolicy.horizontalInset(50000, 1f))
        repeat(50) { assertEquals(2, LessonReadingLayoutPolicy.horizontalInset(3840, 1f)) }
    }

    @Test fun tinyOrUnmeasuredContainersAreNotOverPadded() {
        assertEquals(0, LessonReadingLayoutPolicy.horizontalInset(0, 1f))
        assertEquals(0, LessonReadingLayoutPolicy.horizontalInset(1, 1f))
        assertEquals(1, LessonReadingLayoutPolicy.horizontalInset(2, 3f))
    }

    @Test fun compactWindowsRetainSeparateRows() {
        assertFalse(LessonReadingLayoutPolicy.inlineHeader(320, 70, 1f, 1f))
        assertFalse(LessonReadingLayoutPolicy.inlineHeader(1080, 500, 3f, 1f))
    }

    @Test fun wideWindowsUseOneCommonVerticalCenter() {
        assertTrue(LessonReadingLayoutPolicy.inlineHeader(1600, 1200, 1f, 1f))
        assertTrue(LessonReadingLayoutPolicy.inlineHeader(3840, 2800, 2f, 1f))
        for (height in listOf(72, 96, 144, 240)) {
            val actionsCenter = LessonReadingLayoutPolicy.centeredTop(height, 48) + 24
            val labelsCenter = LessonReadingLayoutPolicy.centeredTop(height, 24) + 12
            assertEquals(height / 2, actionsCenter)
            assertEquals(actionsCenter, labelsCenter)
        }
    }

    @Test fun largeFontsOrInsufficientLabelSpaceKeepSeparateRows() {
        assertFalse(LessonReadingLayoutPolicy.inlineHeader(700, 120, 1f, 1f))
        assertFalse(LessonReadingLayoutPolicy.inlineHeader(700, 250, 1f, 2f))
        assertTrue(LessonReadingLayoutPolicy.inlineHeader(700, 400, 1f, 2f))
    }
}
