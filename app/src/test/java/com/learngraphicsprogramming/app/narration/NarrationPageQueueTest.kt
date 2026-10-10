package com.learngraphicsprogramming.app.narration

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class NarrationPageQueueTest {
    private fun sentence(text: String, voice: String = "ef_dora") =
        NarrationCurriculum.Sentence(text, NarrationLanguage.SPANISH, voice, "current-page/1")

    @Test fun persistentPageResumesEveryMissingSentenceWithTheOriginalVoice() {
        var persisted = ""
        val queue = NarrationPageQueue { persisted = it.encode() }
        val page = queue.submit(listOf(sentence("Primera."), sentence("Segunda."), sentence("Tercera.")))
        queue.markCompleted(page.revision, NarrationPageQueue.hash(page.sentences.first()))
        val restarted = requireNotNull(NarrationPageQueue(persisted).snapshot())
        assertEquals(3, restarted.total)
        assertEquals(1, restarted.completedCount)
        assertEquals(2, restarted.pending)
        assertTrue(restarted.sentences.all { it.voice == "ef_dora" })
        assertEquals(page.revision, restarted.revision)
    }

    @Test fun revisitingIdenticalPageDoesNotInterruptWorkOrDuplicateSentences() {
        val queue = NarrationPageQueue()
        val first = queue.submit(listOf(sentence("Repetida."), sentence("Repetida."), sentence("Final.")))
        val second = queue.submit(listOf(sentence("Repetida."), sentence("Final.")))
        assertEquals(first.revision, second.revision)
        assertEquals(2, second.total)
    }

    @Test fun newPagePreservesCommonCompletedAudioAndRejectsOldCallbacks() {
        val queue = NarrationPageQueue()
        val old = queue.submit(listOf(sentence("Común."), sentence("Anterior.")))
        val common = NarrationPageQueue.hash(old.sentences.first())
        queue.markCompleted(old.revision, common)
        val next = queue.submit(listOf(sentence("Común."), sentence("Nueva.")))
        assertNotEquals(old.revision, next.revision)
        assertEquals(setOf(common), next.completed)
        assertNull(queue.markCompleted(old.revision, NarrationPageQueue.hash(old.sentences.last())))
        assertEquals(1, requireNotNull(queue.snapshot()).pending)
    }

    @Test fun changedVoiceIsSeparateWorkAndMissingCacheCanBeMarkedPendingAgain() {
        val queue = NarrationPageQueue()
        val first = queue.submit(listOf(sentence("Hola.")))
        val hash = NarrationPageQueue.hash(first.sentences.single())
        queue.markCompleted(first.revision, hash)
        assertEquals(0, requireNotNull(queue.snapshot()).pending)
        queue.markPending(first.revision, hash)
        assertEquals(1, requireNotNull(queue.snapshot()).pending)
        queue.markCompleted(first.revision, hash)
        val changed = queue.submit(listOf(sentence("Hola.", "em_alex")))
        assertTrue(changed.completed.isEmpty())
        assertNotEquals(hash, NarrationPageQueue.hash(changed.sentences.single()))
    }

    @Test fun failedPersistenceCannotPublishAFalseCompletedPage() {
        var fail = false
        val queue = NarrationPageQueue { if (fail) throw java.io.IOException("Storage unavailable") }
        val page = queue.submit(listOf(sentence("Pendiente.")))
        fail = true
        try {
            queue.markCompleted(page.revision, NarrationPageQueue.hash(page.sentences.single()))
            throw AssertionError("Expected storage failure")
        } catch (_: java.io.IOException) { }
        assertFalse(requireNotNull(queue.snapshot()).completed.isNotEmpty())
        assertEquals(1, requireNotNull(queue.snapshot()).pending)
    }
}
