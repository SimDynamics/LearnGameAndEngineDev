package com.learngraphicsprogramming.app.narration

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class LessonWordReadingProgressTest {
    private val identity = "ordinary-lesson/page/0"
    private val ranges = listOf(ReadingTextRange(0, 3), ReadingTextRange(4, 7),
        ReadingTextRange(8, 12), ReadingTextRange(13, 19))
    private val mapping = mapOf(0 to NarrationWordMapping(0, ranges, ranges))
    private val alignment = NarrationWordAlignment("exact-audio-bytes", 32_400, listOf(
        NarrationWordTiming(0, 3, 2_400, 7_200),       // 100..300 ms
        NarrationWordTiming(4, 7, 9_600, 14_400),    // 400..600 ms
        NarrationWordTiming(8, 12, 16_800, 21_600),  // 700..900 ms
        NarrationWordTiming(13, 19, 24_000, 28_800)  // 1000..1200 ms
    ))

    private fun model(aligned: Boolean = true) = LessonWordReadingProgress().apply {
        bind(identity, mapping, replace = false)
        if (aligned) update(event(NarrationPlaybackPhase.ALIGNMENT_AVAILABLE, alignment = alignment))
    }

    private fun event(phase: NarrationPlaybackPhase, position: Long = 0, clock: Long = 1_000,
                      segment: Int = 0, count: Int = 1, rate: Float = 1f,
                      alignment: NarrationWordAlignment? = null) = NarrationPlaybackProgress(
        identity, 1, segment, count, phase, "actual-recording-key", position, clock, rate, alignment)

    private fun LessonWordReadingProgress.pending() = unread(identity)[0].orEmpty()

    @Test fun aSingleParagraphContainsNormalCurrentAndHeardWordsAlongsideGrayFutureWords() {
        val progress = model()
        progress.update(event(NarrationPlaybackPhase.PLAYING))
        assertEquals(ranges, progress.pending())
        progress.update(event(NarrationPlaybackPhase.ADVANCED, 200, 1_200))
        assertEquals(ranges.drop(1), progress.pending()) // Current first word is normal.
        progress.update(event(NarrationPlaybackPhase.ADVANCED, 500, 1_500))
        assertEquals(ranges.drop(2), progress.pending()) // First heard; second current; last two gray.
        progress.update(event(NarrationPlaybackPhase.ADVANCED, 650, 1_650))
        assertEquals(ranges.drop(2), progress.pending()) // A silence does not undo heard words.
    }

    @Test fun aPositionChangeWithoutConfirmedForwardControlDoesNotCreditSkippedWords() {
        val progress = model()
        progress.update(event(NarrationPlaybackPhase.PLAYING))
        progress.update(event(NarrationPlaybackPhase.POSITION_CHANGED, 1_000, 1_010))
        progress.update(event(NarrationPlaybackPhase.PLAYING, 1_000, 1_020))
        progress.update(event(NarrationPlaybackPhase.ADVANCED, 1_250, 1_270))
        assertEquals(ranges.take(3), progress.pending())
        progress.update(event(NarrationPlaybackPhase.SEGMENT_COMPLETED, 1_350, 1_370))
        assertEquals(ranges.take(3), progress.pending())
    }

    @Test fun aConfirmedManualForwardControlMarksItsSkippedWordsOnlyForVisualPresentation() {
        val progress = model()
        progress.update(event(NarrationPlaybackPhase.PLAYING))
        progress.update(event(NarrationPlaybackPhase.ADVANCED, 200, 1_200))
        progress.update(event(NarrationPlaybackPhase.POSITION_CHANGED, 1_000, 1_210)
            .copy(manualForwardFrom = NarrationGatePosition(0, 200)))
        assertEquals(listOf(ranges.last()), progress.pending())
        progress.update(event(NarrationPlaybackPhase.PLAYING, 1_000, 1_220))
        assertTrue(progress.pending().isEmpty()) // Target word is current; no hearing milestone was fabricated.
        progress.update(event(NarrationPlaybackPhase.ADVANCED, 1_250, 1_470))
        progress.update(event(NarrationPlaybackPhase.POSITION_CHANGED, 100, 1_480))
        assertEquals(ranges, progress.pending()) // The skipped future becomes pending again.
        progress.update(event(NarrationPlaybackPhase.RESTARTED))
        assertEquals(ranges, progress.pending())
    }

    @Test fun confirmedForwardControlCrossesSegmentsWhileUnrelatedPrefixesRemainPending() {
        val progress = LessonWordReadingProgress()
        val mappings = (0..3).associateWith { NarrationWordMapping(it, ranges, ranges) }
        progress.bind(identity, mappings, replace = true)
        for (segment in 0..3) progress.update(event(NarrationPlaybackPhase.ALIGNMENT_AVAILABLE,
            segment = segment, count = 4, alignment = alignment))
        progress.update(event(NarrationPlaybackPhase.POSITION_CHANGED, 650, segment = 3, count = 4)
            .copy(manualForwardFrom = NarrationGatePosition(1, 400)))
        assertEquals(ranges, progress.unread(identity)[0])
        assertEquals(listOf(ranges.first()), progress.unread(identity)[1])
        assertTrue(progress.unread(identity)[2].orEmpty().isEmpty())
        assertEquals(ranges.drop(2), progress.unread(identity)[3])
        progress.update(event(NarrationPlaybackPhase.RESTARTED, count = 4))
        assertTrue(progress.unread(identity).values.all { it == ranges })
    }

    @Test fun lateTimingAppliesConfirmedManualCoverageWithoutClaimingAnUnexplainedJump() {
        val progress = model(aligned = false)
        progress.update(event(NarrationPlaybackPhase.POSITION_CHANGED, 650)
            .copy(manualForwardFrom = NarrationGatePosition(0, 0)))
        assertEquals(ranges, progress.pending())
        progress.update(event(NarrationPlaybackPhase.ALIGNMENT_AVAILABLE, alignment = alignment))
        assertEquals(ranges.drop(2), progress.pending())
    }

    @Test fun explicitSkipAndObservedTailUseWholeSegmentCompletionWithoutInventedWordTiming() {
        val progress = model(aligned = false)
        progress.update(event(NarrationPlaybackPhase.POSITION_CHANGED, 650)
            .copy(manualForwardFrom = NarrationGatePosition(0, 0)))
        progress.update(event(NarrationPlaybackPhase.PLAYING, 650))
        assertEquals(ranges, progress.pending()) // Partial coverage has no pretend word precision.
        progress.update(event(NarrationPlaybackPhase.SEGMENT_COMPLETED, 1_350, 1_700))
        assertTrue(progress.pending().isEmpty())
        progress.update(event(NarrationPlaybackPhase.RESTARTED))
        assertEquals(ranges, progress.pending())
    }

    @Test fun wholeSegmentCoverageForFallbackViewsUnionsListeningAndManualSkipWithoutAnotherHistory() {
        val progress = LessonWordReadingProgress().apply { bind(identity, emptyMap(), replace = true) }
        progress.update(event(NarrationPlaybackPhase.PLAYING))
        progress.update(event(NarrationPlaybackPhase.ADVANCED, 200, 1_200))
        progress.update(event(NarrationPlaybackPhase.POSITION_CHANGED, 650, 1_210)
            .copy(manualForwardFrom = NarrationGatePosition(0, 200)))
        progress.update(event(NarrationPlaybackPhase.PLAYING, 650, 1_220))
        assertTrue(progress.visuallyCompletedSegments(identity).isEmpty())
        progress.update(event(NarrationPlaybackPhase.SEGMENT_COMPLETED, 1_350, 1_920))
        assertEquals(setOf(0), progress.visuallyCompletedSegments(identity))
        progress.update(event(NarrationPlaybackPhase.RESTARTED))
        assertTrue(progress.visuallyCompletedSegments(identity).isEmpty())
        // A completion without evidence for the omitted prefix cannot complete presentation.
        progress.update(event(NarrationPlaybackPhase.PLAYING, 400))
        progress.update(event(NarrationPlaybackPhase.SEGMENT_COMPLETED, 1_350, 1_950))
        assertTrue(progress.visuallyCompletedSegments(identity).isEmpty())
    }

    @Test fun implausibleClockJumpIsTreatedAsASeekInsteadOfListeningEvidence() {
        val progress = model()
        progress.update(event(NarrationPlaybackPhase.PLAYING))
        progress.update(event(NarrationPlaybackPhase.ADVANCED, 1_000, 1_050))
        progress.update(event(NarrationPlaybackPhase.ADVANCED, 1_250, 1_300))
        assertEquals(ranges.take(3), progress.pending())
    }

    @Test fun anExplicitSkipToTheMeasuredRecordingEndCompletesVisualFallbackWithoutWordTiming() {
        val progress = model(aligned = false)
        progress.update(event(NarrationPlaybackPhase.PLAYING))
        progress.update(event(NarrationPlaybackPhase.PAUSED, 200, 1_200))
        progress.update(event(NarrationPlaybackPhase.POSITION_CHANGED, 1_350, 1_210).copy(
            manualForwardFrom = NarrationGatePosition(0, 200), manualForwardToEnd = true))
        assertTrue(progress.pending().isEmpty())
        assertEquals(setOf(0), progress.visuallyCompletedSegments(identity))
        progress.update(event(NarrationPlaybackPhase.FINISHED, 1_350, 1_220))
        assertTrue(progress.pending().isEmpty())
        progress.update(event(NarrationPlaybackPhase.RESTARTED))
        assertEquals(ranges, progress.pending())
    }

    @Test fun pauseAndExactResumeMergeOnlyTheIntervalsActuallyPlayed() {
        val progress = model()
        progress.update(event(NarrationPlaybackPhase.PLAYING))
        progress.update(event(NarrationPlaybackPhase.PAUSED, 500, 1_500))
        assertEquals(ranges.drop(2), progress.pending())
        progress.update(event(NarrationPlaybackPhase.PLAYING, 500, 8_000))
        progress.update(event(NarrationPlaybackPhase.ADVANCED, 650, 8_150))
        assertEquals(ranges.drop(2), progress.pending())
        progress.update(event(NarrationPlaybackPhase.PAUSED, 650, 8_150))
        assertEquals(ranges.drop(2), progress.pending())
    }

    @Test fun lateAlignmentReconstructsOnlyObservedPlaybackIntervals() {
        val progress = model(aligned = false)
        progress.update(event(NarrationPlaybackPhase.PLAYING, 400))
        progress.update(event(NarrationPlaybackPhase.PAUSED, 650, 1_250))
        assertEquals(ranges, progress.pending()) // No timing means no claimed word precision.
        progress.update(event(NarrationPlaybackPhase.ALIGNMENT_AVAILABLE, alignment = alignment))
        assertEquals(listOf(ranges[0], ranges[2], ranges[3]), progress.pending())
    }

    @Test fun naturalCompletionRecordsThePlayedTailButNotAnUnheardPrefix() {
        val progress = model()
        progress.update(event(NarrationPlaybackPhase.PLAYING, 400))
        progress.update(event(NarrationPlaybackPhase.SEGMENT_COMPLETED, 1_350, 1_950))
        assertEquals(listOf(ranges.first()), progress.pending())
        progress.update(event(NarrationPlaybackPhase.FINISHED, 1_350, 1_950))
        assertEquals(listOf(ranges.first()), progress.pending())
    }

    @Test fun fullyObservedNaturalCompletionRestoresTheWholeParagraphWithoutRestartingIt() {
        val progress = model()
        progress.update(event(NarrationPlaybackPhase.PLAYING))
        progress.update(event(NarrationPlaybackPhase.SEGMENT_COMPLETED, 1_350, 2_350))
        progress.update(event(NarrationPlaybackPhase.FINISHED, 1_350, 2_350))
        assertTrue(progress.pending().isEmpty())
        progress.update(event(NarrationPlaybackPhase.RESTARTED))
        assertEquals(ranges, progress.pending())
    }

    @Test fun coldRestoredPrefixUsesNormalFallbackWhileTheFutureRemainsGray() {
        val progress = model()
        progress.update(event(NarrationPlaybackPhase.PREPARED, 550))
        assertEquals(ranges.drop(2), progress.pending())
        progress.update(event(NarrationPlaybackPhase.PLAYING, 550))
        progress.update(event(NarrationPlaybackPhase.ADVANCED, 650, 1_100))
        assertEquals(ranges.drop(2), progress.pending())
    }

    @Test fun stoppedOrUnsynchronizedPagesKeepTheirNormalReadablePresentation() {
        val progress = model()
        progress.update(event(NarrationPlaybackPhase.PLAYING))
        progress.update(event(NarrationPlaybackPhase.ADVANCED, 350, 1_350))
        progress.update(event(NarrationPlaybackPhase.STOPPED, 350, 1_350))
        assertTrue(progress.unread(identity).isEmpty())
        progress.bind(identity, mapping, replace = false)
        assertEquals(ranges.drop(1), progress.pending())
        progress.unavailable(identity)
        assertTrue(progress.unread(identity).isEmpty())
    }

    @Test fun mismatchedAlignmentCannotAssignTimingsToUnrelatedVisibleWords() {
        val progress = model(aligned = false)
        val unrelated = alignment.copy(words = alignment.words.map { it.copy(start = it.start + 1) })
        progress.update(event(NarrationPlaybackPhase.ALIGNMENT_AVAILABLE, alignment = unrelated))
        progress.update(event(NarrationPlaybackPhase.PLAYING))
        progress.update(event(NarrationPlaybackPhase.ADVANCED, 650, 1_650))
        assertEquals(ranges, progress.pending())
    }

    @Test fun playbackRateChangesUseSourcePositionsAndTheObservedClock() {
        val progress = model()
        progress.update(event(NarrationPlaybackPhase.PLAYING, rate = 2f))
        progress.update(event(NarrationPlaybackPhase.ADVANCED, 650, 1_325, rate = 2f))
        assertEquals(ranges.drop(2), progress.pending())
    }

    @Test fun replayedIntervalsMergeOnlyUpToTheCurrentPositionAfterARewind() {
        val progress = model()
        // Listen to 0..200, later 400..650, then rewind and replay only 200..350.
        progress.update(event(NarrationPlaybackPhase.PLAYING))
        progress.update(event(NarrationPlaybackPhase.PAUSED, 200, 1_200))
        progress.update(event(NarrationPlaybackPhase.POSITION_CHANGED, 400, 2_000))
        progress.update(event(NarrationPlaybackPhase.PLAYING, 400, 2_000))
        progress.update(event(NarrationPlaybackPhase.PAUSED, 650, 2_250))
        progress.update(event(NarrationPlaybackPhase.POSITION_CHANGED, 200, 3_000))
        progress.update(event(NarrationPlaybackPhase.PLAYING, 200, 3_000))
        progress.update(event(NarrationPlaybackPhase.PAUSED, 350, 3_150))
        // The prefix is replayed; the former 400..650 coverage remains in the future.
        assertEquals(ranges.drop(1), progress.pending())
    }

    @Test fun restoringAnObservedPageDoesNotTurnItsKnownSkippedPrefixIntoHeardWords() {
        val progress = model()
        progress.update(event(NarrationPlaybackPhase.PLAYING, 1_000))
        progress.update(event(NarrationPlaybackPhase.PAUSED, 1_250, 1_250))
        assertEquals(ranges.take(3), progress.pending())
        progress.update(event(NarrationPlaybackPhase.STOPPED, 1_250, 1_250))
        progress.bind(identity, mapping, replace = false)
        progress.update(event(NarrationPlaybackPhase.PREPARED, 1_250))
        assertEquals(ranges.take(3), progress.pending())
    }

    @Test fun changingPagesKeepsIndependentEvidenceAndEvictionRestartsTheOriginalPage() {
        val progress = model()
        progress.update(event(NarrationPlaybackPhase.PLAYING))
        progress.update(event(NarrationPlaybackPhase.PAUSED, 350, 1_350))
        progress.update(event(NarrationPlaybackPhase.STOPPED, 350, 1_350))
        val other = "another-lesson/page/0"
        progress.bind(other, mapping, replace = false)
        progress.update(event(NarrationPlaybackPhase.ALIGNMENT_AVAILABLE, alignment = alignment).copy(pageIdentity = other))
        progress.update(event(NarrationPlaybackPhase.PLAYING).copy(pageIdentity = other))
        progress.update(event(NarrationPlaybackPhase.SEGMENT_COMPLETED, 1_350, 2_350).copy(pageIdentity = other))
        progress.update(event(NarrationPlaybackPhase.FINISHED, 1_350, 2_350).copy(pageIdentity = other))
        assertTrue(progress.unread(other)[0].orEmpty().isEmpty())
        progress.bind(identity, mapping, replace = false)
        progress.update(event(NarrationPlaybackPhase.PREPARED, 350))
        assertEquals(ranges.drop(1), progress.pending())
        // The existing bounded bookmark store, not this visual observer, chooses zero after eviction.
        progress.update(event(NarrationPlaybackPhase.PREPARED))
        assertEquals(ranges, progress.pending())
        assertTrue(progress.unread(other)[0].orEmpty().isEmpty())
    }
}
