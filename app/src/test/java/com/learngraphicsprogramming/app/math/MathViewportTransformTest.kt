package com.learngraphicsprogramming.app.math

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class MathViewportTransformTest {
    @Test fun zoomPersistsThroughPanAndResizeUntilExplicitReset() {
        val geometry = MathViewportTransform()
        geometry.resize(400f, 300f, 400f, 300f)
        geometry.zoomBy(2f, 200f, 150f)
        geometry.panBy(40f, -25f)
        assertEquals(2f, geometry.scale, 0f)
        assertEquals(40f, geometry.panX, 0f)
        assertEquals(-25f, geometry.panY, 0f)
        geometry.resize(300f, 400f, 300f, 225f)
        assertEquals(2f, geometry.scale, 0f)
        assertTrue(geometry.panY >= -25f)
        geometry.reset()
        assertEquals(1f, geometry.scale, 0f)
        assertEquals(0f, geometry.panX, 0f)
        assertEquals(0f, geometry.panY, 0f)
    }

    @Test fun limitsPreventOverscalingAndPanningBeyondTheContent() {
        val geometry = MathViewportTransform()
        geometry.resize(400f, 300f, 400f, 300f)
        geometry.zoomBy(100f, 200f, 150f)
        assertEquals(5f, geometry.scale, 0f)
        geometry.panBy(100000f, -100000f)
        assertEquals(800f, geometry.panX, 0f)
        assertEquals(-600f, geometry.panY, 0f)
        geometry.zoomBy(.001f, 200f, 150f)
        assertEquals(1f, geometry.scale, 0f)
        assertEquals(0f, geometry.panX, 0f)
        assertEquals(0f, geometry.panY, 0f)
    }

    @Test fun focalPointStaysAnchoredAndInvalidInputCannotCorruptTheTransform() {
        val geometry = MathViewportTransform()
        geometry.resize(400f, 300f, 400f, 300f)
        geometry.zoomBy(2f, 300f, 200f)
        assertEquals(-100f, geometry.panX, .001f)
        assertEquals(-50f, geometry.panY, .001f)
        geometry.zoomBy(Float.NaN, 200f, 150f)
        geometry.panBy(Float.POSITIVE_INFINITY, 2f)
        assertEquals(2f, geometry.scale, 0f)
        assertEquals(-100f, geometry.panX, .001f)
        assertEquals(-50f, geometry.panY, .001f)
    }
}
