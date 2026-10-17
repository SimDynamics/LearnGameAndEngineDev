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

    @Test fun cacheMaintenanceRefusesToStartDuringReaderPlayback() {
        val reader = NarrationWorkPriority.beginInteractive()
        try {
            org.junit.Assert.assertNull(NarrationWorkPriority.beginMaintenanceIfIdle())
        } finally {
            reader.close()
        }
        assertFalse(NarrationWorkPriority.isBusy)
    }

    @Test fun explicitMaintenancePreemptsBackgroundAndReleasesExactlyOneLease() {
        var interrupted = 0
        val callback = NarrationWorkPriority.onPreempt { interrupted++ }
        val maintenance = NarrationWorkPriority.beginMaintenanceIfIdle()
        try {
            org.junit.Assert.assertNotNull(maintenance)
            assertTrue(NarrationWorkPriority.isBusy)
            assertTrue(NarrationWorkPriority.isMaintenanceExclusive)
            assertEquals(1, interrupted)
            org.junit.Assert.assertNull(NarrationWorkPriority.beginMaintenanceIfIdle())
            // A reader starting at this point makes further deletion ineligible.
            val reader = NarrationWorkPriority.beginInteractive()
            try {
                assertFalse(NarrationWorkPriority.isMaintenanceExclusive)
            } finally {
                reader.close()
            }
            assertTrue(NarrationWorkPriority.isMaintenanceExclusive)
        } finally {
            maintenance?.close()
            maintenance?.close()
            callback.close()
        }
        assertFalse(NarrationWorkPriority.isBusy)
    }

    @Test fun closingOneLeaseTwiceDoesNotReleaseAnotherInteractiveOperation() {
        val audio = NarrationWorkPriority.beginInteractive()
        val compiler = NarrationWorkPriority.beginInteractive()
        try {
            audio.close()
            audio.close()
            assertTrue(NarrationWorkPriority.isBusy)
        } finally {
            compiler.close()
            audio.close()
        }
        assertFalse(NarrationWorkPriority.isBusy)
    }
}
