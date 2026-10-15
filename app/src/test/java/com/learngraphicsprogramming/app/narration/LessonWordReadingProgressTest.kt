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

    @Test fun explicitForwardSeekNeverCreditsTheWordsItSkipped() {
        val progress = model()
        progress.update(event(NarrationPlaybackPhase.PLAYING))
        progress.update(event(NarrationPlaybackPhase.POSITION_CHANGED, 1_000, 1_010))
        progress.update(event(NarrationPlaybackPhase.PLAYING, 1_000, 1_020))
        progress.update(event(NarrationPlaybackPhase.ADVANCED, 1_250, 1_270))
        assertEquals(ranges.take(3), progress.pending())
        progress.update(event(NarrationPlaybackPhase.SEGMENT_COMPLETED, 1_350, 1_370))
        assertEquals(ranges.take(3), progress.pending())
    }

    @Test fun implausibleClockJumpIsTreatedAsASeekInsteadOfListeningEvidence() {
        val progress = model()
        progress.update(event(NarrationPlaybackPhase.PLAYING))
        progress.update(event(NarrationPlaybackPhase.ADVANCED, 1_000, 1_050))
        progress.update(event(NarrationPlaybackPhase.ADVANCED, 1_250, 1_300))
        assertEquals(ranges.take(3), progress.pending())
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

    @Test fun separatelyPlayedIntervalsAreUnionedAcrossARewindWithoutLosingHeardWords() {
        val progress = model()
        // First listen to 0..200, later 400..650, then fill the gap 200..400.
        progress.update(event(NarrationPlaybackPhase.PLAYING))
        progress.update(event(NarrationPlaybackPhase.PAUSED, 200, 1_200))
        progress.update(event(NarrationPlaybackPhase.POSITION_CHANGED, 400, 2_000))
        progress.update(event(NarrationPlaybackPhase.PLAYING, 400, 2_000))
        progress.update(event(NarrationPlaybackPhase.PAUSED, 650, 2_250))
        progress.update(event(NarrationPlaybackPhase.POSITION_CHANGED, 200, 3_000))
        progress.update(event(NarrationPlaybackPhase.PLAYING, 200, 3_000))
        progress.update(event(NarrationPlaybackPhase.PAUSED, 400, 3_200))
        // The first word spans 100..300, crossing the two now-adjacent observed intervals.
        assertEquals(ranges.drop(2), progress.pending())
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
