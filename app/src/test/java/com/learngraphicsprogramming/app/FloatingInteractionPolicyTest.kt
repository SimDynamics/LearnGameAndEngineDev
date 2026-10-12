package com.learngraphicsprogramming.app

import org.junit.Assert.*
import org.junit.Test

class FloatingInteractionPolicyTest {
    @Test fun readerOwnsEntireGestureUntilUpEvenOutsideScreen() {
        val gesture = FloatingGestureOwnership()
        gesture.begin(true,false)
        repeat(1000) { assertTrue(gesture.readerOwnsGesture) }
        gesture.end()
        assertFalse(gesture.readerOwnsGesture)
        gesture.begin(false,false)
        assertFalse(gesture.readerOwnsGesture)
    }
    @Test fun secondHeartTapIsConsumedAndCannotReopenPopup() {
        val gesture = FloatingGestureOwnership()
        gesture.begin(false,true)
        assertTrue(gesture.consumeAnchorGesture)
        // This stays true even after the popup has been dismissed on DOWN.
        assertTrue(gesture.consumeAnchorGesture)
        gesture.end()
        assertFalse(gesture.consumeAnchorGesture)
    }
    @Test fun timeoutIsExactlyFiveSeconds() {
        val popup = PopupLifetime()
        val token = popup.opened(100)
        assertFalse(popup.expired(token,5099))
        assertTrue(popup.expired(token,5100))
    }
    @Test fun oldTimeoutCannotCloseANewlyOpenedPopup() {
        val popup = PopupLifetime()
        val old = popup.opened(0)
        popup.dismissed()
        val current = popup.opened(1000)
        assertFalse(popup.expired(old,5000))
        assertFalse(popup.expired(current,5999))
        assertTrue(popup.expired(current,6000))
    }
    @Test fun offscreenAndNonfinitePositionsCanBeRecovered() {
        for(point in listOf(FloatingPoint(-999f,-99f),FloatingPoint(99999f,99999f),
            FloatingPoint(Float.NaN,Float.POSITIVE_INFINITY))) {
            assertTrue(FloatingReaderPlacement.outside(point,4f,24f,300f,600f))
            val safe = FloatingReaderPlacement.visible(point,4f,24f,300f,600f)
            assertFalse(FloatingReaderPlacement.outside(safe,4f,24f,300f,600f))
        }
    }
}
