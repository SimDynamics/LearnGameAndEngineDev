package com.learngraphicsprogramming.app.narration

import org.junit.Assert.*
import org.junit.Test

class KokoroCourseAudioCoverageTest {
    @Test fun eachCourseCountsUniqueSentencesAndRealStoredFiles() {
        val cache = mapOf(
            "shared" to KokoroCachedAudio(files = 2, bytes = 600L),
            "local" to KokoroCachedAudio(files = 1, bytes = 200L),
            "different-course" to KokoroCachedAudio(files = 1, bytes = 900L)
        )
        val coverage = KokoroCourseAudioCoverage.calculate(
            listOf("shared", "shared", "local", "missing"), cache)
        assertEquals(3, coverage.expectedSentences)
        assertEquals(2, coverage.generatedSentences)
        assertEquals(3, coverage.files) // Both WAV and Ogg physically exist for "shared".
        assertEquals(800L, coverage.bytes)
        assertEquals(67, coverage.percent)
        // Another course can refer to the same immutable file without owning it.
        val other = KokoroCourseAudioCoverage.calculate(listOf("shared", "different-course"), cache)
        assertEquals(2, other.generatedSentences)
        assertEquals(1500L, other.bytes)
        assertEquals(100, other.percent)
        assertEquals(4, cache.values.sumOf { it.files }) // Physical cache is not additive by course.
    }

    @Test fun noGeneratedAudioIsZeroPercentWithoutInventingAFile() {
        val result = KokoroCourseAudioCoverage.calculate(listOf("a", "b"), emptyMap())
        assertEquals(2, result.expectedSentences)
        assertEquals(0, result.generatedSentences)
        assertEquals(0, result.files)
        assertEquals(0L, result.bytes)
        assertEquals(0, result.percent)
    }

    @Test fun noPublishedAudioDemandDoesNotFalselyReportCompleteCourse() {
        val result = KokoroCourseAudioCoverage.calculate(emptyList(), emptyMap())
        assertEquals(0, result.expectedSentences)
        assertNull(result.percent)
    }

    @Test fun invalidOrIncompleteInventoryEntryNeverCountsAsGenerated() {
        val cache = mapOf(
            "staged" to KokoroCachedAudio(0, 400L),
            "invalid" to KokoroCachedAudio(1, 0L),
            "ready" to KokoroCachedAudio(1, 280L)
        )
        val result = KokoroCourseAudioCoverage.calculate(listOf("staged", "invalid", "ready"), cache)
        assertEquals(1, result.generatedSentences)
        assertEquals(1, result.files)
        assertEquals(280L, result.bytes)
        assertEquals(33, result.percent)
    }
}
