package com.learngraphicsprogramming.app.narration

import org.junit.Assert.*
import org.junit.Test

class NarrationResumeHistoryTest {
    private val point = NarrationResumePoint(2, 12_345L)
    private fun begin(h: NarrationResumeHistory, key: String) =
        NarrationResumePolicy.begin(h, key, "owner-$key", point)

    @Test fun aThousandPausedPagesNeverRetainMoreThanThreeBookmarks() {
        var h = NarrationResumeHistory()
        repeat(1_000) { index ->
            h = begin(h, "page-$index")
            assertTrue(h.entries.size <= 3)
        }
        assertEquals(listOf("page-999", "page-998", "page-997"), h.entries.map { it.key })
        assertNull(NarrationResumePolicy.restore(h, "page-0", 10))
        assertEquals(point, NarrationResumePolicy.restore(h, "page-998", 10))
    }

    @Test fun visitingOrPreparingOtherPagesDoesNotPromoteOrEvictAnything() {
        val h = begin(begin(begin(NarrationResumeHistory(), "a"), "b"), "c")
        repeat(1_000) { assertNull(NarrationResumePolicy.restore(h, "unplayed-$it", 10)) }
        assertEquals(listOf("c", "b", "a"), h.entries.map { it.key })
        assertEquals(point, NarrationResumePolicy.restore(h, "a", 10))
    }

    @Test fun resumingAnOlderPagePromotesItWithoutCreatingADuplicate() {
        val h = begin(begin(begin(NarrationResumeHistory(), "a"), "b"), "c")
        val resumed = begin(h, "a")
        assertEquals(listOf("a", "c", "b"), resumed.entries.map { it.key })
        val next = begin(resumed, "d")
        assertEquals(listOf("d", "a", "c"), next.entries.map { it.key })
    }

    @Test fun oldControllersCannotOverwriteOrResurrectBookmarkedPages() {
        val a = begin(NarrationResumeHistory(), "a")
        val b = begin(a, "b")
        assertEquals(b, NarrationResumePolicy.update(b, "a", "owner-a", point.copy(positionMs = 999)))
        val aAgain = NarrationResumePolicy.begin(b, "a", "new-owner-a", point)
        assertEquals(aAgain, NarrationResumePolicy.update(aAgain, "a", "owner-a", point.copy(positionMs = 1)))
    }

    @Test fun finishingAudioPreservesEndWithoutResurrectingOldOwner() {
        val h = begin(begin(NarrationResumeHistory(), "a"), "b")
        val last = point.copy(segment = 4, positionMs = 19_700L, finished = true)
        val done = NarrationResumePolicy.update(h, "b", "owner-b", last)
        assertEquals(listOf("b", "a"), done.entries.map { it.key })
        assertEquals(last, NarrationResumePolicy.restore(done, "b", 5))
        assertEquals("", done.activeOwner)
        assertEquals("", done.activeKey)
        assertEquals(done, NarrationResumePolicy.update(done, "b", "owner-b", point))
        val rewind = NarrationResumePolicy.begin(done, "b", "new-owner", last.copy(positionMs = 16_700L))
        assertEquals(last.copy(positionMs = 16_700L, finished = false),
            NarrationResumePolicy.restore(rewind, "b", 5))
        assertNull("A finished bookmark belongs to the last segment",
            NarrationResumePolicy.restore(done, "b", 6))
    }

    @Test fun completingManyPagesStillRespectsThreeBookmarkLimit() {
        var history = NarrationResumeHistory()
        repeat(200) { index ->
            history = NarrationResumePolicy.begin(history, "page-$index", "owner-$index",
                NarrationResumePoint(0, 0))
            history = NarrationResumePolicy.update(history, "page-$index", "owner-$index",
                NarrationResumePoint(0, 8_000, finished = true))
        }
        assertEquals(3, history.entries.size)
        assertEquals(listOf("page-199", "page-198", "page-197"), history.entries.map { it.key })
        assertTrue(history.entries.all { it.point.finished })
    }

    @Test fun pauseStoresAnExactSegmentAndMillisecondPosition() {
        val h = begin(NarrationResumeHistory(), "a")
        val p = NarrationResumePoint(3, 28_719L)
        val updated = NarrationResumePolicy.update(h, "a", "owner-a", p)
        assertEquals(p, NarrationResumePolicy.restore(updated, "a", 10))
        assertNull(NarrationResumePolicy.restore(updated, "a", 3))
    }

    @Test fun equalTextsOnDifferentPagesDoNotShareAResumeKey() {
        fun key(page: String, language: String = "es", voice: String = "ef_dora",
                parts: List<String> = listOf("Uno.", "Dos.")) =
            NarrationResumePolicy.key(page, language, "KOKORO", voice, parts)
        assertEquals(key("a/1"), key("a/1"))
        assertNotEquals(key("a/1"), key("b/1"))
        assertNotEquals(key("a/1"), key("a/1", language = "fr"))
        assertNotEquals(key("a/1"), key("a/1", voice = "otra"))
        assertNotEquals(key("a/1", parts = listOf("ab", "c")), key("a/1", parts = listOf("a", "bc")))
    }

    @Test fun existingOrdinarySpanishBookmarksSurviveInternalPronunciationMetadata() {
        val plain = listOf("El vector representa una dirección.", "El programa imprime el resultado.", "Otra explicación.")
        val prepared = plain.map { SpanishTechnicalSpeech.prepareForKokoro(it).text }
        assertTrue(prepared.all { ManualEnglishSpeech.spans(it).isEmpty() })
        for (engine in listOf("KOKORO", "AUTO")) {
            val legacy = NarrationResumePolicy.key("lesson/page", "es", engine, "ef_dora", plain)
            val history = begin(NarrationResumeHistory(), legacy)
            val current = NarrationResumePolicy.key("lesson/page", "es", engine, "ef_dora", prepared)
            assertEquals(legacy, current)
            assertEquals(point, NarrationResumePolicy.restore(history, current, prepared.size))
        }
    }

    @Test fun changedEnglishAudioStartsAFreshBookmarkWithoutRemovingTheOldOne() {
        val plain = listOf("Usa include para incluir iostream.", "El programa imprime el resultado.", "Otra explicación.")
        val prepared = plain.map { SpanishTechnicalSpeech.prepareForKokoro(it).text }
        assertTrue(prepared.any { ManualEnglishSpeech.spans(it).isNotEmpty() })
        val legacy = NarrationResumePolicy.key("lesson/page", "es", "KOKORO", "ef_dora", plain)
        val history = begin(NarrationResumeHistory(), legacy)
        val current = NarrationResumePolicy.key("lesson/page", "es", "KOKORO", "ef_dora", prepared)
        assertNotEquals(legacy, current)
        assertNull(NarrationResumePolicy.restore(history, current, prepared.size))
        assertEquals(point, NarrationResumePolicy.restore(history, legacy, plain.size))
        val currentHistory = begin(history, current)
        assertEquals(point, NarrationResumePolicy.restore(currentHistory, current, prepared.size))
        assertEquals(point, NarrationResumePolicy.restore(currentHistory, legacy, plain.size))
    }

    @Test fun androidBookmarksUseOnlyTheRestoredSpokenText() {
        val plain = listOf("Usa include para incluir iostream.", "El programa imprime el resultado.", "Otra explicación.")
        val prepared = plain.map { SpanishTechnicalSpeech.prepareForKokoro(it).text }
        val legacy = NarrationResumePolicy.key("lesson/page", "es", "ANDROID", "android-voice", plain)
        val current = NarrationResumePolicy.key("lesson/page", "es", "ANDROID", "android-voice", prepared)
        assertEquals(legacy, current)
    }

    @Test fun metadataVariantsWithIdenticalEnglishAudioShareTheSameBookmark() {
        val entry = ManualEnglishTerms.entries.single { it.spelling == "include" }
        val code = listOf("${ManualEnglishSpeech.PREPARED}Usa ${ManualEnglishSpeech.encode(entry)}.")
        val prose = listOf("${ManualEnglishSpeech.PREPARED}Usa ${ManualEnglishSpeech.encode(entry, code = false)}.")
        val codeKey = NarrationResumePolicy.key("lesson/page", "es", "KOKORO", "ef_dora", code)
        val proseKey = NarrationResumePolicy.key("lesson/page", "es", "KOKORO", "ef_dora", prose)
        assertEquals(codeKey, proseKey)
        val history = NarrationResumePolicy.begin(NarrationResumeHistory(), codeKey, "same-audio", NarrationResumePoint(0, 456L))
        assertEquals(NarrationResumePoint(0, 456L), NarrationResumePolicy.restore(history, proseKey, 1))
    }

    @Test fun nextPlaybackReleasesPreviousResourcesBeforeAcquiringNewOnes() {
        var openPlayers = 0
        var previous: Any? = null
        repeat(1_000) {
            val next = Any()
            NarrationPlaybackOwner.acquire(next) { openPlayers-- }
            openPlayers++
            assertEquals(1, openPlayers)
            // Cleanup from an old Activity cannot release the current owner.
            previous?.let(NarrationPlaybackOwner::release)
            previous = next
        }
        openPlayers--
        previous?.let(NarrationPlaybackOwner::release)
        assertEquals(0, openPlayers)
    }
}
