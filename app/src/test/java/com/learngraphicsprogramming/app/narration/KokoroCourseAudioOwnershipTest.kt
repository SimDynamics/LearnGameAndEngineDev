package com.learngraphicsprogramming.app.narration

import org.junit.Assert.*
import org.junit.Test

class KokoroCourseAudioOwnershipTest {
    private val programming = KokoroCourseRef("programming", "course-1")
    private val programming2 = KokoroCourseRef("programming", "course-2")
    private val math = KokoroCourseRef("math", "course-1")

    @Test fun sharedAudioIsProtectedAcrossBothRoutesAndNeverMarkedForDeletion() {
        val refs = mapOf(
            programming to setOf("unique", "shared", "again"),
            programming2 to setOf("shared", "elsewhere"),
            math to setOf("again")
        )
        val cache = mapOf(
            "unique" to KokoroCachedAudio(1, 800L),
            "shared" to KokoroCachedAudio(2, 1600L),
            "again" to KokoroCachedAudio(1, 700L),
            "elsewhere" to KokoroCachedAudio(1, 300L)
        )
        val plan = KokoroCourseAudioOwnership.plan(programming, refs, cache)
        assertEquals(setOf("unique"), plan.removableKeys)
        assertEquals(1, plan.removableFiles)
        assertEquals(800L, plan.removableBytes)
        assertEquals(setOf("shared", "again"), plan.sharedKeys)
        assertEquals(3, plan.sharedFiles)
        assertEquals(2300L, plan.sharedBytes)
        assertFalse("Another course's audio is never owned by this one",
            "elsewhere" in plan.removableKeys)
    }

    @Test fun courseIdentityIncludesRouteAndDoesNotConfuseSameCourseIds() {
        val sameNameProgramming = KokoroCourseRef("programming", "fundamentals")
        val sameNameMath = KokoroCourseRef("math", "fundamentals")
        val refs = mapOf(
            sameNameProgramming to setOf("identical", "only-programming"),
            sameNameMath to setOf("identical")
        )
        val cache = mapOf(
            "identical" to KokoroCachedAudio(1, 250L),
            "only-programming" to KokoroCachedAudio(1, 170L)
        )
        val plan = KokoroCourseAudioOwnership.plan(sameNameProgramming, refs, cache)
        assertEquals(setOf("only-programming"), plan.removableKeys)
        assertEquals(setOf("identical"), plan.sharedKeys)
    }

    @Test fun missingAndUnverifiedFilesNeverBecomeDeletionCandidates() {
        val refs = mapOf(programming to setOf("missing", "invalid", "unfinished", "valid"))
        val cache = mapOf(
            "invalid" to KokoroCachedAudio(1, 0L),
            "unfinished" to KokoroCachedAudio(0, 300L),
            "valid" to KokoroCachedAudio(1, 700L)
        )
        val plan = KokoroCourseAudioOwnership.plan(programming, refs, cache)
        assertEquals(setOf("valid"), plan.removableKeys)
        assertEquals(1, plan.removableFiles)
        assertEquals(700L, plan.removableBytes)
    }

    @Test fun multipleFileFormatsCountSeparatelyButDoNotDoubleCountSentenceOwnership() {
        val refs = mapOf(programming to setOf("one", "one"))
        val cache = mapOf("one" to KokoroCachedAudio(2, 1450L))
        val plan = KokoroCourseAudioOwnership.plan(programming, refs, cache)
        assertEquals(1, plan.removableKeys.size)
        assertEquals(2, plan.removableFiles)
        assertEquals(1450L, plan.removableBytes)
    }

    @Test fun unknownCourseOrUnrelatedAudioFailsClosedWithoutAnyDeletionKeys() {
        val refs = mapOf(programming2 to setOf("stored"))
        val cache = mapOf("stored" to KokoroCachedAudio(1, 150L))
        val plan = KokoroCourseAudioOwnership.plan(programming, refs, cache)
        assertTrue(plan.removableKeys.isEmpty())
        assertEquals(0, plan.removableFiles)
        assertEquals(0L, plan.removableBytes)
    }
}
