package com.learngraphicsprogramming.app.narration

import org.junit.Assert.*
import org.junit.Test

class ReadingRewindRegressionTest {
    private val page = "original-lesson/page/3"
    private val key = "unchanged-playback-key"
    private val ranges = listOf(ReadingTextRange(0, 3), ReadingTextRange(4, 7),
        ReadingTextRange(8, 12), ReadingTextRange(13, 19))
    private val alignment = NarrationWordAlignment("original-pcm-sha", 32_400, listOf(
        NarrationWordTiming(0, 3, 2_400, 7_200), NarrationWordTiming(4, 7, 9_600, 14_400),
        NarrationWordTiming(8, 12, 16_800, 21_600), NarrationWordTiming(13, 19, 24_000, 28_800)))

    private fun event(phase: NarrationPlaybackPhase, segment: Int = 0, position: Long = 0,
                      count: Int = 1, clock: Long = 1_000) = NarrationPlaybackProgress(
        page, 1, segment, count, phase, key, position, clock)

    private fun words(count: Int = 1) = LessonWordReadingProgress().apply {
        bind(page, (0 until count).associateWith { NarrationWordMapping(it, ranges, ranges) }, false)
        repeat(count) { update(event(NarrationPlaybackPhase.ALIGNMENT_AVAILABLE, it, count = count)
            .copy(alignment = alignment)) }
    }

    private fun LessonWordReadingProgress.listen(segment: Int, count: Int) {
        update(event(NarrationPlaybackPhase.PLAYING, segment, count = count))
        update(event(NarrationPlaybackPhase.SEGMENT_COMPLETED, segment, 1_350, count, 2_350))
    }

    @Test fun repeatedRewindWithinAParagraphRestoresFutureGrayAndReplaysWordsProgressively() {
        val model = words()
        model.listen(0, 1)
        assertTrue(model.unread(page)[0].orEmpty().isEmpty())
        model.update(event(NarrationPlaybackPhase.POSITION_CHANGED, position = 350))
        assertEquals(ranges.drop(1), model.unread(page)[0])
        model.update(event(NarrationPlaybackPhase.PLAYING, position = 350))
        model.update(event(NarrationPlaybackPhase.ADVANCED, position = 450, clock = 1_100))
        assertEquals(ranges.drop(2), model.unread(page)[0])
        model.update(event(NarrationPlaybackPhase.PAUSED, position = 450, clock = 1_100))
        model.update(event(NarrationPlaybackPhase.POSITION_CHANGED, position = 100))
        assertEquals(ranges, model.unread(page)[0])
        model.update(event(NarrationPlaybackPhase.PLAYING, position = 100))
        assertEquals(ranges.drop(1), model.unread(page)[0])
        model.update(event(NarrationPlaybackPhase.PAUSED, position = 100))
        assertEquals(ranges.drop(1), model.unread(page)[0])
    }

    @Test fun crossingCompletedSentencesAndBlocksKeepsOnlyThePrefixBeforeTheNewPosition() {
        val model = words(3)
        repeat(3) { model.listen(it, 3) }
        model.update(event(NarrationPlaybackPhase.POSITION_CHANGED, 0, 650, 3))
        assertEquals(ranges.drop(2), model.unread(page)[0])
        assertEquals(ranges, model.unread(page)[1])
        assertEquals(ranges, model.unread(page)[2])
        assertTrue(model.visuallyCompletedSegments(page).isEmpty())
        model.update(event(NarrationPlaybackPhase.PLAYING, 0, 650, 3))
        model.update(event(NarrationPlaybackPhase.ADVANCED, 0, 750, 3, 1_100))
        assertEquals(listOf(ranges.last()), model.unread(page)[0])
        assertEquals(ranges, model.unread(page)[1])
    }

    @Test fun rewindAlsoTruncatesVoluntaryForwardCoverageAndSupportsAnotherForwardControl() {
        val model = words(3)
        model.update(event(NarrationPlaybackPhase.POSITION_CHANGED, 2, 1_350, 3).copy(
            manualForwardFrom = NarrationGatePosition(0, 0), manualForwardToEnd = true))
        model.update(event(NarrationPlaybackPhase.FINISHED, 2, 1_350, 3))
        assertTrue(model.unread(page).values.all { it.isEmpty() })
        model.update(event(NarrationPlaybackPhase.POSITION_CHANGED, 1, 650, 3))
        assertTrue(model.unread(page)[0].orEmpty().isEmpty())
        assertEquals(ranges.drop(2), model.unread(page)[1])
        assertEquals(ranges, model.unread(page)[2])
        assertEquals(setOf(0), model.visuallyCompletedSegments(page))
        model.update(event(NarrationPlaybackPhase.POSITION_CHANGED, 2, 650, 3).copy(
            manualForwardFrom = NarrationGatePosition(1, 650)))
        assertTrue(model.unread(page)[1].orEmpty().isEmpty())
        assertEquals(ranges.drop(2), model.unread(page)[2])
    }

    @Test fun aColdRestoredFinishedPositionCanBeRewoundWithoutKeepingItsFutureOrdinary() {
        val model = words(2)
        model.update(event(NarrationPlaybackPhase.PREPARED, 1, 1_350, 2))
        model.update(event(NarrationPlaybackPhase.FINISHED, 1, 1_350, 2))
        assertTrue(model.unread(page).values.all { it.isEmpty() })
        model.update(event(NarrationPlaybackPhase.POSITION_CHANGED, 0, 650, 2))
        assertEquals(ranges.drop(2), model.unread(page)[0])
        assertEquals(ranges, model.unread(page)[1])
        model.update(event(NarrationPlaybackPhase.STOPPED, 0, 650, 2))
        model.bind(page, (0..1).associateWith { NarrationWordMapping(it, ranges, ranges) }, false)
        model.update(event(NarrationPlaybackPhase.PREPARED, 0, 650, 2))
        assertEquals(ranges.drop(2), model.unread(page)[0])
        assertEquals(ranges, model.unread(page)[1])
    }

    @Test fun blockFallbackAlsoFollowsTheNewPositionWhileEarlierBlocksRemainRead() {
        val model = LessonReadingProgressModel()
        model.bind(page, NarrationReadingPlan((0..2).map { NarrationTextOrigin(it) }, "original-plan"), key)
        repeat(3) { index ->
            model.update(event(NarrationPlaybackPhase.PLAYING, index, count = 3))
            model.update(event(NarrationPlaybackPhase.SEGMENT_COMPLETED, index, 1_350, 3))
        }
        model.update(event(NarrationPlaybackPhase.POSITION_CHANGED, 1, 650, 3))
        assertEquals(mapOf(0 to ReadingBlockState.READ, 1 to ReadingBlockState.UNREAD,
            2 to ReadingBlockState.UNREAD), model.states(page))
        model.update(event(NarrationPlaybackPhase.PLAYING, 1, 650, 3))
        assertEquals(ReadingBlockState.CURRENT, model.states(page)[1])
        assertEquals(ReadingBlockState.UNREAD, model.states(page)[2])
        model.update(event(NarrationPlaybackPhase.PAUSED, 1, 650, 3))
        assertEquals(ReadingBlockState.READ, model.states(page)[0])
        assertEquals(ReadingBlockState.UNREAD, model.states(page)[2])
    }
}
