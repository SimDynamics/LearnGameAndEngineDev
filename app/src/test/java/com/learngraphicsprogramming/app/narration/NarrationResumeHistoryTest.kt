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

    @Test fun finishingAudioFreesItsBookmarkButNotOtherPages() {
        val h = begin(begin(NarrationResumeHistory(), "a"), "b")
        val done = NarrationResumePolicy.update(h, "b", "owner-b", point.copy(finished = true))
        assertEquals(listOf("a"), done.entries.map { it.key })
        assertEquals("", done.activeOwner)
        assertEquals(done, NarrationResumePolicy.update(done, "b", "owner-b", point))
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
