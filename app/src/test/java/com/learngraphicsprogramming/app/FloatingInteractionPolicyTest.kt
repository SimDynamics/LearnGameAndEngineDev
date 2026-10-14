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

    @Test fun quickTouchSwipeLikeLdplayerMouseWheelCannotDragOrLaterArm() {
        val gate = FloatingReaderDragGate()
        gate.begin(1_000L,mouse = false,primaryPressed = false)
        assertFalse(gate.mayDrag(1_025L,true,mouse = false,primaryPressed = false))
        assertTrue(gate.isRejected)
        assertFalse(gate.canClick)
        assertFalse("Later moves of the same wheel-like stream must stay rejected",
            gate.mayDrag(1_500L,true,mouse = false,primaryPressed = false))
        gate.end()
        assertFalse(gate.canClick)
    }

    @Test fun heldTouchDragsAndOrdinaryTapsStayClickable() {
        val gate = FloatingReaderDragGate()
        gate.begin(1_000L,mouse = false,primaryPressed = false)
        assertTrue(gate.canClick)
        assertFalse(gate.mayDrag(1_020L,false,mouse = false,primaryPressed = false))
        assertTrue(gate.mayDrag(1_200L,true,mouse = false,primaryPressed = false))
        gate.end()
        gate.begin(2_000L,mouse = false,primaryPressed = false)
        assertTrue(gate.canClick)
        gate.end()
    }

    @Test fun primaryMouseDragHasNoHoldDelayAndButtonsAreRequired() {
        val gate = FloatingReaderDragGate()
        gate.begin(100L,mouse = true,primaryPressed = true)
        assertTrue(gate.mayDrag(110L,true,mouse = true,primaryPressed = true))
        assertFalse(gate.mayDrag(120L,true,mouse = true,primaryPressed = false))
        assertTrue(gate.isRejected)
        gate.end()
        gate.begin(200L,mouse = true,primaryPressed = false)
        assertFalse(gate.mayDrag(600L,true,mouse = true,primaryPressed = true))
        gate.end()
    }

    @Test fun mixedPointerSourceOrSecondFingerCannotStartReaderDrag() {
        val gate = FloatingReaderDragGate()
        gate.begin(100L,mouse = false,primaryPressed = false)
        assertFalse(gate.mayDrag(300L,true,mouse = true,primaryPressed = true))
        gate.end()
        gate.begin(100L,mouse = false,primaryPressed = false)
        gate.reject()
        assertFalse(gate.mayDrag(400L,true,mouse = false,primaryPressed = false))
    }

}
