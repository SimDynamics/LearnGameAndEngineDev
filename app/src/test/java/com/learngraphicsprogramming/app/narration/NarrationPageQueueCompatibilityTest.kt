package com.learngraphicsprogramming.app.narration

import java.security.MessageDigest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class NarrationPageQueueCompatibilityTest {
    private val sentences = listOf(
        sentence("Primera frase.", "current-page/1"),
        sentence("std::vector<int>", "current-page/2"),
        sentence("Calcula 3/4 y x².", "current-page/3")
    )

    @Test fun restartingAnOldPagePreservesOnlyCompatibleCompletionAndOriginalSentenceOrder() {
        val legacyCompleted = sentences.map(::legacyKey).toSet()
        val stored = NarrationPageQueue.Snapshot(7L, sentences, legacyCompleted).encode()
        val queue = NarrationPageQueue(stored)
        val restored = requireNotNull(queue.snapshot())
        val technical = sentences[1]
        val correctedKey = NarrationPageQueue.hash(technical)

        assertEquals(7L, restored.revision)
        assertEquals(sentences, restored.sentences)
        assertEquals(setOf(legacyKey(sentences[0]), legacyKey(sentences[2])), restored.completed)
        assertEquals(2, restored.completedCount)
        assertEquals(1, restored.pending)
        assertNotEquals(legacyKey(technical), correctedKey)
        assertNull(queue.markCompleted(restored.revision, legacyKey(technical)))
        assertEquals(1, requireNotNull(queue.snapshot()).pending)

        val completed = requireNotNull(queue.markCompleted(restored.revision, correctedKey))
        assertEquals(0, completed.pending)
        assertEquals(sentences.map { NarrationPageQueue.hash(it) }.toSet(), completed.completed)
        assertEquals(completed, NarrationPageQueue(completed.encode()).snapshot())
    }

    @Test fun resubmittingThePreparedPageRetainsCompatibleAudioAndItsVisibleOrder() {
        val stored = NarrationPageQueue.Snapshot(7L, sentences, sentences.map(::legacyKey).toSet()).encode()
        val queue = NarrationPageQueue(stored)
        val previous = requireNotNull(queue.snapshot())
        assertEquals(previous, queue.submit(sentences))

        val prepared = SpanishTechnicalSpeech.prepare(sentences[1].text)
        assertTrue(prepared.changed)
        val currentTexts = sentences.toMutableList().apply {
            this[1] = this[1].copy(text = prepared.text)
        }
        val currentPage = queue.submit(currentTexts)

        assertEquals(8L, currentPage.revision)
        assertEquals(currentTexts, currentPage.sentences)
        assertEquals(previous.completed, currentPage.completed)
        assertEquals(1, currentPage.pending)
        assertNull(queue.markCompleted(previous.revision, NarrationPageQueue.hash(sentences[1])))
        assertEquals(1, requireNotNull(queue.snapshot()).pending)
    }

    private fun sentence(text: String, location: String) =
        NarrationCurriculum.Sentence(text, NarrationLanguage.SPANISH, "ef_dora", location)

    private fun legacyKey(sentence: NarrationCurriculum.Sentence): String {
        val parts = listOf("kokoro-wav-v1", "1939ad2a8e416c0acfeecc08a694d14ef25f2231",
            sentence.language.tag, sentence.voice, sentence.text)
        return MessageDigest.getInstance("SHA-256")
            .digest(parts.joinToString("\u0000").toByteArray(Charsets.UTF_8))
            .joinToString("") { "%02x".format(it.toInt() and 0xff) }
    }
}
