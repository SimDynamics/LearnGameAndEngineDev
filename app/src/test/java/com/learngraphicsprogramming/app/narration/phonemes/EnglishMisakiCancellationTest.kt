package com.learngraphicsprogramming.app.narration.phonemes

import java.io.ByteArrayInputStream
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class EnglishMisakiCancellationTest {
    @Test fun obsoleteSentenceIsRejectedBeforeOpeningAnyResource() {
        var opened = 0
        val frontend = EnglishMisakiG2P({ opened++; ByteArrayInputStream(byteArrayOf()) })
        assertThrows(InterruptedException::class.java) { frontend.phonemize("Hello", isCurrent = { false }) }
        assertEquals(0, opened)
        frontend.close()
    }

    @Test fun cancellationFromAnotherThreadDoesNotWaitForThePhonemizerMonitor() {
        val entered = CountDownLatch(1)
        val release = CountDownLatch(1)
        val failure = AtomicReference<Throwable?>()
        val frontend = EnglishMisakiG2P({
            entered.countDown()
            check(release.await(5, TimeUnit.SECONDS))
            ByteArrayInputStream(byteArrayOf())
        })
        val worker = Thread {
            try { frontend.phonemize("Hello") }
            catch (error: Throwable) { failure.set(error) }
        }
        worker.start()
        try {
            assertTrue(entered.await(5, TimeUnit.SECONDS))
            // Cancellation itself runs on a separate caller while resource loading owns
            // the sentence monitor. It must return immediately without that monitor.
            val cancelled = CountDownLatch(1)
            val canceller = Thread { frontend.cancelCurrentRun(); cancelled.countDown() }
            canceller.start()
            assertTrue("Cancellation waited for the worker", cancelled.await(2, TimeUnit.SECONDS))
            release.countDown()
            worker.join(5_000)
            assertFalse(worker.isAlive)
            assertTrue(failure.get() is InterruptedException)
        } finally { release.countDown(); worker.join(5_000); frontend.close() }
    }

    @Test fun cancellationAfterFallbackPreventsReturningItsStalePhonemes() {
        var cancelled = false
        var calls = 0
        val lexicon = MisakiLexicon(JSONObject(), JSONObject(), false)
        assertThrows(InterruptedException::class.java) {
            MisakiEnglishPipeline.speakCancellable(listOf(MisakiWord("outofdictionary", tag = "NN")), lexicon,
                { calls++; cancelled = true; "ɡɹˈæfi" }) {
                if (cancelled) throw InterruptedException("Old page")
            }
        }
        assertEquals(1, calls)
        assertEquals("ɡɹˈæfi", MisakiEnglishPipeline.speakCancellable(
            listOf(MisakiWord("outofdictionary", tag = "NN")), lexicon, { "ɡɹˈæfi" }) {})
    }

    @Test fun greedyDecodingStopsAtTheFirstCancelledStepWithoutContinuingInference() {
        var steps = 0
        assertThrows(InterruptedException::class.java) {
            MisakiBartTokens.generate {
                steps++
                if (steps == 3) throw InterruptedException("Old page")
                FloatArray(63).also { it[4] = 1f }
            }
        }
        assertEquals(3, steps)
        assertArrayEquals(longArrayOf(1, 2), MisakiBartTokens.generate { FloatArray(63).also { it[2] = 1f } })
    }
}
