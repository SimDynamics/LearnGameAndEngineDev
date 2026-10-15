package com.learngraphicsprogramming.app.narration

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class LessonReadingColorTest {
    @Test fun darkLessonPendingInkIsDarkerThanBeforeWithoutLosingTextContrast() {
        for (surface in listOf(0xff101114.toInt(), 0xff1c1f24.toInt())) {
            val pending = LessonReadingColor.unread(surface)
            assertTrue("Pending ink is visibly darker than the previous channel 145", pending and 255 < 145)
            assertTrue(LessonReadingColor.contrast(pending, surface) >= LessonReadingColor.MIN_CONTRAST)
            assertTrue("Read white remains distinctly brighter", 233 - (pending and 255) >= 90)
        }
        assertEquals("The existing readable light-theme gray is retained", 80,
            LessonReadingColor.unread(0xffffffff.toInt()) and 255)
    }

    @Test fun unreadGrayRemainsOpaqueAndReadableOnActualLessonAndLightSurfaces() {
        val surfaces = listOf(0xff101114.toInt(), 0xff1c1f24.toInt(), 0xff262330.toInt(),
            0xff313842.toInt(), 0xffffffff.toInt(), 0xfffafafa.toInt(), 0xffeeeeee.toInt())
        surfaces.forEach { surface ->
            val gray = LessonReadingColor.unread(surface)
            assertEquals(255, gray ushr 24)
            assertEquals(gray ushr 16 and 255, gray ushr 8 and 255)
            assertEquals(gray ushr 8 and 255, gray and 255)
            assertTrue(LessonReadingColor.contrast(gray, surface) >= 4.5)
            assertTrue("Ordinary surfaces use a muted gray", gray and 255 in 60..200)
        }
    }

    @Test fun everyIntermediateNeutralSurfaceHasAnAccessibleUnreadGray() {
        for (channel in 0..255) {
            val surface = 0xff000000.toInt() or (channel shl 16) or (channel shl 8) or channel
            assertTrue("Surface $channel", LessonReadingColor.contrast(LessonReadingColor.unread(surface), surface) >= 4.5)
        }
    }

    @Test fun contrastingHueSurfacesRemainReadableWithoutDependingOnThemeMode() {
        for (red in listOf(0, 51, 102, 153, 204, 255)) {
            for (green in listOf(0, 51, 102, 153, 204, 255)) {
                for (blue in listOf(0, 51, 102, 153, 204, 255)) {
                    val surface = 0xff000000.toInt() or (red shl 16) or (green shl 8) or blue
                    assertTrue(LessonReadingColor.contrast(LessonReadingColor.unread(surface), surface) >= 4.5)
                }
            }
        }
    }

    @Test fun backgroundCompositionKeepsOpaqueAndTransparentCasesExact() {
        val foreground = 0xff123456.toInt()
        val background = 0xff654321.toInt()
        assertEquals(foreground, LessonReadingColor.composite(foreground, background))
        assertEquals(background, LessonReadingColor.composite(0, background))
        assertEquals(0, LessonReadingColor.composite(0, 0))
        assertEquals(0xff808080.toInt(), LessonReadingColor.composite(0x80ffffff.toInt(), 0xff000000.toInt()))
        assertEquals(21.0, LessonReadingColor.contrast(0xff000000.toInt(), 0xffffffff.toInt()), .0001)
    }
}
