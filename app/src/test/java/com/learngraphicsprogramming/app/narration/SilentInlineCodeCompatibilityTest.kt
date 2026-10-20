package com.learngraphicsprogramming.app.narration

import java.security.MessageDigest
import org.junit.Assert.*
import org.junit.Test

class SilentInlineCodeCompatibilityTest {
    private val oldInlineSpeech = "${ManualEnglishSpeech.PREPARED}El operador código, doble menor que, fin de código introduce la salida."
    private val unchangedSpeech = "${ManualEnglishSpeech.PREPARED}El programa conserva sus datos."
    private val point = NarrationResumePoint(0, 2_345L)

    private fun inlineSpeech() = SpanishTechnicalSpeech.prepareForKokoro(
        "El operador `<<` introduce la salida.").text

    private fun sentence(text: String, location: String) =
        NarrationCurriculum.Sentence(text, NarrationLanguage.SPANISH, "ef_dora", location)

    @Test fun changedInlineSpeechCannotReuseThePreviouslyAnnouncedAudio() {
        val current = inlineSpeech()
        assertEquals("El operador doble menor que introduce la salida.", ManualEnglishSpeech.strip(current))
        assertNotEquals(KokoroNarrationCache.key("es", "ef_dora", oldInlineSpeech),
            KokoroNarrationCache.key("es", "ef_dora", current))
        assertEquals(KokoroNarrationCache.key("es", "ef_dora", current),
            KokoroNarrationCache.key("es", "ef_dora", inlineSpeech()))
    }

    @Test fun ordinaryAndFencedSpeechRetainTheirPreviouslyUsableAudioKeys() {
        val sourceAndLegacySpeech = listOf(
            "El programa conserva sus datos." to "El programa conserva sus datos.",
            "```<<```" to "código, doble menor que, fin de código")
        for ((source, legacy) in sourceAndLegacySpeech) {
            val current = SpanishTechnicalSpeech.prepareForKokoro(source).text
            assertEquals(legacy, ManualEnglishSpeech.strip(current))
            assertEquals(legacyCacheKey(legacy), KokoroNarrationCache.key("es", "ef_dora", current))
        }
    }

    @Test fun changedInlineAudioStartsAFreshBookmarkWithoutDiscardingTheExistingOne() {
        for (engine in listOf("KOKORO", "AUTO", "ANDROID")) {
            val voice = if (engine == "ANDROID") "android-voice" else "ef_dora"
            fun key(text: String) = NarrationResumePolicy.key("lesson/page", "es", engine, voice, listOf(text))
            val legacy = key(oldInlineSpeech)
            val current = key(inlineSpeech())
            val history = NarrationResumePolicy.begin(NarrationResumeHistory(), legacy, "old-owner", point)
            assertNotEquals(engine, legacy, current)
            assertNull(NarrationResumePolicy.restore(history, current, 1))
            assertEquals(point, NarrationResumePolicy.restore(history, legacy, 1))
            val resumed = NarrationResumePolicy.begin(history, current, "new-owner", NarrationResumePoint(0, 0L))
            assertEquals(point, NarrationResumePolicy.restore(resumed, legacy, 1))
            assertEquals(NarrationResumePoint(0, 0L), NarrationResumePolicy.restore(resumed, current, 1))
        }
    }

    @Test fun unchangedOrdinaryAndFencedBookmarksKeepTheirOriginalIdentity() {
        val legacy = listOf("El programa conserva sus datos.", "código, doble menor que, fin de código")
        val current = listOf("El programa conserva sus datos.", "```<<```")
            .map { SpanishTechnicalSpeech.prepareForKokoro(it).text }
        for (engine in listOf("KOKORO", "AUTO", "ANDROID")) {
            val voice = if (engine == "ANDROID") "android-voice" else "ef_dora"
            val oldKey = NarrationResumePolicy.key("lesson/page", "es", engine, voice, legacy)
            val newKey = NarrationResumePolicy.key("lesson/page", "es", engine, voice, current)
            assertEquals(engine, oldKey, newKey)
            val history = NarrationResumePolicy.begin(NarrationResumeHistory(), oldKey, "owner", point)
            assertEquals(point, NarrationResumePolicy.restore(history, newKey, 2))
        }
    }

    @Test fun resubmittingADurablePageRetainsOnlyCompatibleCompletionsAndRejectsStaleCallbacks() {
        var persisted = ""
        val queue = NarrationPageQueue { persisted = it.encode() }
        val old = queue.submit(listOf(sentence(unchangedSpeech, "current-page/1"),
            sentence(oldInlineSpeech, "current-page/2")))
        old.sentences.forEach { queue.markCompleted(old.revision, NarrationPageQueue.hash(it)) }
        val oldEncoded = persisted
        val restarted = NarrationPageQueue(oldEncoded) { persisted = it.encode() }
        assertEquals(2, requireNotNull(restarted.snapshot()).completedCount)
        val current = restarted.submit(listOf(sentence(unchangedSpeech, "current-page/1"),
            sentence(inlineSpeech(), "current-page/2")))
        val common = NarrationPageQueue.hash(old.sentences.first())
        val changed = NarrationPageQueue.hash(current.sentences.last())
        assertNotEquals(old.revision, current.revision)
        assertEquals(setOf(common), current.completed)
        assertEquals(1, current.pending)
        assertFalse(current.completed.contains(changed))
        assertNull(restarted.markCompleted(old.revision, NarrationPageQueue.hash(old.sentences.last())))
        assertEquals(current, NarrationPageQueue(persisted).snapshot())
        assertEquals("Old serialized work is retained as evidence without mutating it", oldEncoded,
            requireNotNull(NarrationPageQueue(oldEncoded).snapshot()).encode())
        val complete = requireNotNull(restarted.markCompleted(current.revision, changed))
        assertEquals(0, complete.pending)
        assertEquals(complete, NarrationPageQueue(persisted).snapshot())
        assertEquals(complete, restarted.submit(current.sentences))
    }

    private fun legacyCacheKey(text: String): String = MessageDigest.getInstance("SHA-256")
        .digest(listOf("kokoro-wav-v1", "1939ad2a8e416c0acfeecc08a694d14ef25f2231", "es", "ef_dora", text)
            .joinToString("\u0000").toByteArray(Charsets.UTF_8))
        .joinToString("") { "%02x".format(it.toInt() and 0xff) }
}
