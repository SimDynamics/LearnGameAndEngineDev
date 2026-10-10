package com.learngraphicsprogramming.app.narration

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class NarrationWorkPriorityTest {
    @Test fun compilationOrGraphicsPreemptsBackgroundUntilLeaseCloses() {
        var interruptions = 0
        val listener = NarrationWorkPriority.onPreempt { interruptions++ }
        val compiler = NarrationWorkPriority.beginInteractive()
        val graphics = NarrationWorkPriority.beginInteractive()
        try {
            assertTrue(NarrationWorkPriority.isBusy)
            assertEquals(2, interruptions)
            compiler.close()
            assertTrue(NarrationWorkPriority.isBusy)
        } finally {
            graphics.close()
            listener.close()
        }
        assertFalse(NarrationWorkPriority.isBusy)
    }

    @Test fun aWorkerRegisteredDuringPlaybackIsInterruptedImmediately() {
        val audio = NarrationWorkPriority.beginInteractive()
        var interrupted = false
        val listener = NarrationWorkPriority.onPreempt { interrupted = true }
        try {
            assertTrue(interrupted)
        } finally {
            listener.close()
            audio.close()
        }
        assertFalse(NarrationWorkPriority.isBusy)
    }
}
