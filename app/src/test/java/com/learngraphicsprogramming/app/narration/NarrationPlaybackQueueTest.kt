package com.learngraphicsprogramming.app.narration

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class NarrationPlaybackQueueTest {

    @Test
    fun delayedCompletionOfPreviousPageCannotAdvanceCurrentPage() {
        val queue = NarrationPlaybackQueue()
        queue.start(listOf("Página anterior", "Su segundo segmento"))
        val previous = requireNotNull(queue.current())

        queue.stop()
        queue.start(listOf("Nueva página", "Siguiente explicación"))
        val current = requireNotNull(queue.current())

        assertFalse(queue.complete(previous.id))
        assertFalse(queue.isCurrent(previous))
        assertTrue(queue.isCurrent(current))
        assertEquals("Nueva página", queue.current()?.text)
    }

    @Test
    fun duplicateCompletionCannotSkipTheNextSegment() {
        val queue = NarrationPlaybackQueue()
        queue.start(listOf("Primero", "Segundo", "Tercero"))
        val first = requireNotNull(queue.current())

        assertTrue(queue.complete(first.id))
        assertFalse(queue.complete(first.id))
        assertEquals("Segundo", queue.current()?.text)
    }

    @Test
    fun stopInvalidatesQueuedSynthesisAndItsCallbacks() {
        val queue = NarrationPlaybackQueue()
        queue.start(listOf("Explicación"))
        val utterance = requireNotNull(queue.current())

        queue.stop()

        assertFalse(queue.isPlaying)
        assertFalse(queue.isCurrent(utterance))
        assertFalse(queue.complete(utterance.id))
        assertNull(queue.current())
    }

    @Test
    fun completedPageCanBeListenedToAgainWithNewIdentifiers() {
        val queue = NarrationPlaybackQueue()
        queue.start(listOf("Una página"))
        val first = requireNotNull(queue.current())

        assertTrue(queue.complete(first.id))
        assertFalse(queue.isPlaying)
        queue.start(listOf("Una página"))

        assertTrue(queue.isPlaying)
        assertNotEquals(first.id, queue.current()?.id)
        assertFalse(queue.complete(first.id))
    }

    @Test
    fun emptyPagesDoNotStartPlayback() {
        val queue = NarrationPlaybackQueue()
        queue.start(listOf(" ", "\n"))
        assertFalse(queue.isPlaying)
        assertNull(queue.current())
    }
}
