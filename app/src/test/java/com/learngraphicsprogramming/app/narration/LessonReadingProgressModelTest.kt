package com.learngraphicsprogramming.app.narration

import org.junit.Assert.*
import org.junit.Test

class LessonReadingProgressModelTest {
    private val page = "programming/lesson/0"
    private val key = "existing-audio-key"
    private fun plan(vararg blocks: Int) = NarrationReadingPlan(
        blocks.map { NarrationTextOrigin(it) }, blocks.joinToString(","))
    private fun event(phase: NarrationPlaybackPhase, index: Int = 0, count: Int = 3,
                      generation: Long = 1, position: Long = 0, pageIdentity: String = page,
                      playbackKey: String? = key) = NarrationPlaybackProgress(
        pageIdentity, generation, index, count, phase, playbackKey, position)
    private fun LessonReadingProgressModel.hear(index: Int, count: Int = 3,
                                               generation: Long = 1, pageIdentity: String = page) {
        update(event(NarrationPlaybackPhase.PLAYING, index, count, generation, pageIdentity = pageIdentity))
        update(event(NarrationPlaybackPhase.SEGMENT_COMPLETED, index, count, generation,
            pageIdentity = pageIdentity))
    }

    @Test fun preparedAndBufferingAndCompletionWithoutPlaybackNeverAccreditSpeech() {
        val model = LessonReadingProgressModel()
        model.bind(page, plan(-1, 0, 1), key)
        for (phase in listOf(NarrationPlaybackPhase.PREPARED, NarrationPlaybackPhase.BUFFERING,
            NarrationPlaybackPhase.SEGMENT_COMPLETED)) {
            model.update(event(phase))
            assertTrue(model.states(page).values.all { it == ReadingBlockState.UNREAD })
        }
    }

    @Test fun everyBodySegmentMustBeHeardBeforeItsBlockIsRead() {
        val model = LessonReadingProgressModel()
        model.bind(page, plan(0, 0, 1), key)
        model.hear(0)
        assertEquals(ReadingBlockState.UNREAD, model.states(page)[0])
        model.update(event(NarrationPlaybackPhase.PLAYING, 1))
        assertEquals(ReadingBlockState.CURRENT, model.states(page)[0])
        model.update(event(NarrationPlaybackPhase.SEGMENT_COMPLETED, 1))
        assertEquals(ReadingBlockState.READ, model.states(page)[0])
        assertEquals(ReadingBlockState.UNREAD, model.states(page)[1])
    }

    @Test fun feedbackDoesNotKeepAnAnsweredQuestionCurrentOrPreventItsReadState() {
        val origins = listOf(NarrationTextOrigin(0), NarrationTextOrigin(0, true), NarrationTextOrigin(1))
        val model = LessonReadingProgressModel()
        model.bind(page, NarrationReadingPlan(origins, "feedback"), key)
        model.hear(0)
        model.update(event(NarrationPlaybackPhase.PLAYING, 1))
        assertEquals(ReadingBlockState.READ, model.states(page)[0])
        assertEquals(ReadingBlockState.UNREAD, model.states(page)[1])
        model.update(event(NarrationPlaybackPhase.PAUSED, 1, position = 50))
        assertEquals(ReadingBlockState.READ, model.states(page)[0])
    }

    @Test fun pauseAndPositiveOffsetResumeRetainCoverageAndCachedBindRetainsCurrent() {
        val model = LessonReadingProgressModel()
        val plan = plan(0, 1, 2)
        model.bind(page, plan, key)
        model.update(event(NarrationPlaybackPhase.PLAYING))
        model.update(event(NarrationPlaybackPhase.PAUSED, position = 650))
        model.bind(page, plan, key)
        assertEquals(ReadingBlockState.CURRENT, model.states(page)[0])
        model.update(event(NarrationPlaybackPhase.BUFFERING, position = 650))
        model.update(event(NarrationPlaybackPhase.PLAYING, position = 650))
        model.update(event(NarrationPlaybackPhase.SEGMENT_COMPLETED, position = 650))
        assertEquals(ReadingBlockState.READ, model.states(page)[0])
    }

    @Test fun seekingIntoASegmentDoesNotAccreditItsUnheardBeginning() {
        val model = LessonReadingProgressModel()
        model.bind(page, plan(0, 1, 2), key)
        model.update(event(NarrationPlaybackPhase.POSITION_CHANGED, position = 800))
        model.update(event(NarrationPlaybackPhase.PLAYING, position = 800))
        model.update(event(NarrationPlaybackPhase.SEGMENT_COMPLETED, position = 800))
        assertEquals(ReadingBlockState.UNREAD, model.states(page)[0])
        model.update(event(NarrationPlaybackPhase.FINISHED, index = 2, position = 900))
        assertTrue(model.states(page).isEmpty())
    }

    @Test fun explicitManualSkipUpdatesUnmappedVisualBlocksWithoutCreditingHeardFeedback() {
        val model = LessonReadingProgressModel()
        val origins = listOf(NarrationTextOrigin(0), NarrationTextOrigin(1), NarrationTextOrigin(1, true))
        model.bind(page, NarrationReadingPlan(origins, "visual-skip-fallback"), key)
        model.update(event(NarrationPlaybackPhase.POSITION_CHANGED, index = 1).copy(
            manualForwardFrom = NarrationGatePosition(0)))
        assertEquals(ReadingBlockState.READ, model.states(page)[0])
        assertEquals(ReadingBlockState.UNREAD, model.states(page)[1])
        // The visual observer cannot add the skipped segment to its heard evidence.
        val pages = model.javaClass.getDeclaredField("pages").apply { isAccessible = true }.get(model) as Map<*, *>
        val state = requireNotNull(pages[page])
        val heard = state.javaClass.getDeclaredField("heard").apply { isAccessible = true }.get(state) as Set<*>
        assertTrue(heard.isEmpty())
        model.update(event(NarrationPlaybackPhase.RESTARTED))
        assertTrue(model.states(page).values.all { it == ReadingBlockState.UNREAD })
    }

    @Test fun seekingWithinTheCurrentSegmentCancelsCoverageUntilACompleteReplay() {
        val model = LessonReadingProgressModel()
        model.bind(page, plan(0, 1, 2), key)
        model.update(event(NarrationPlaybackPhase.PLAYING))
        model.update(event(NarrationPlaybackPhase.POSITION_CHANGED, position = 100))
        model.update(event(NarrationPlaybackPhase.PLAYING, position = 100))
        model.update(event(NarrationPlaybackPhase.SEGMENT_COMPLETED))
        assertEquals(ReadingBlockState.UNREAD, model.states(page)[0])
        model.update(event(NarrationPlaybackPhase.POSITION_CHANGED))
        model.hear(0)
        assertEquals(ReadingBlockState.READ, model.states(page)[0])
    }

    @Test fun finishedBookmarkWithoutEvidenceUsesNormalPresentationEvenAfterRepeatedBind() {
        val model = LessonReadingProgressModel()
        val plan = plan(0, 1, 2)
        model.bind(page, plan, key)
        model.update(event(NarrationPlaybackPhase.FINISHED, index = 2, position = 1_000))
        assertTrue(model.states(page).isEmpty())
        model.bind(page, plan, key)
        assertTrue(model.states(page).isEmpty())
    }

    @Test fun completedPageRetainsReadEvidenceWhenReturningFromAnotherCachedPage() {
        val model = LessonReadingProgressModel()
        val plan = plan(0, 1, 2)
        model.bind(page, plan, key)
        (0..2).forEach { model.hear(it) }
        model.update(event(NarrationPlaybackPhase.FINISHED, index = 2))
        assertTrue(model.states(page).values.all { it == ReadingBlockState.READ })
        model.update(event(NarrationPlaybackPhase.STOPPED, index = 2, generation = 2))
        assertTrue(model.states(page).isEmpty())
        val other = "programming/lesson/1"
        model.bind(other, plan, key)
        assertTrue(model.states(other).values.all { it == ReadingBlockState.UNREAD })
        model.bind(page, plan, key)
        assertTrue(model.states(page).values.all { it == ReadingBlockState.READ })
        assertTrue(model.states(other).values.all { it == ReadingBlockState.UNREAD })
    }

    @Test fun restartClearsHeardEvidenceOnlyForItsOwnPage() {
        val model = LessonReadingProgressModel()
        val plan = plan(0, 1, 2)
        val other = "math/lesson/0"
        model.bind(page, plan, key)
        model.bind(other, plan, key)
        model.hear(0)
        model.hear(0, pageIdentity = other)
        model.update(event(NarrationPlaybackPhase.RESTARTED))
        assertTrue(model.states(page).values.all { it == ReadingBlockState.UNREAD })
        assertEquals(ReadingBlockState.READ, model.states(other)[0])
    }

    @Test fun staleGenerationsCannotFinishOrEraseANewerSession() {
        val model = LessonReadingProgressModel()
        model.bind(page, plan(0, 1, 2), key)
        model.hear(0, generation = 5)
        model.update(event(NarrationPlaybackPhase.PLAYING, 1, generation = 5))
        model.update(event(NarrationPlaybackPhase.SEGMENT_COMPLETED, 1, generation = 4))
        model.update(event(NarrationPlaybackPhase.UNAVAILABLE, generation = 4, playbackKey = "old"))
        assertEquals(ReadingBlockState.CURRENT, model.states(page)[1])
        assertEquals(ReadingBlockState.READ, model.states(page)[0])
    }

    @Test fun countKeyAndIndexMismatchFallBackWithoutFabricatingReadEvidence() {
        val model = LessonReadingProgressModel()
        val plan = plan(0, 1, 2)
        model.bind(page, plan, key)
        model.hear(0)
        for (bad in listOf(event(NarrationPlaybackPhase.PLAYING, count = 4),
            event(NarrationPlaybackPhase.PLAYING, playbackKey = "other-engine-or-voice"),
            event(NarrationPlaybackPhase.PLAYING, index = 3),
            event(NarrationPlaybackPhase.PLAYING, position = -1))) {
            model.update(bad)
            assertTrue(model.states(page).isEmpty())
            model.bind(page, plan, key)
            assertTrue(model.states(page).isEmpty())
            model.update(event(NarrationPlaybackPhase.PREPARED, index = 1, position = 10))
            assertEquals(ReadingBlockState.READ, model.states(page)[0])
            assertNull("An unproven restored fragment keeps its original style", model.states(page)[1])
        }
    }

    @Test fun changingPlanOrPlaybackKeyClearsPreviousEvidenceEvenWithTheSameSegmentCount() {
        val model = LessonReadingProgressModel()
        model.bind(page, plan(0, 1, 2), key)
        model.hear(0)
        model.bind(page, NarrationReadingPlan(plan(0, 1, 2).origins, "translated-text"), key)
        assertTrue(model.states(page).values.all { it == ReadingBlockState.UNREAD })
        model.hear(0)
        model.bind(page, NarrationReadingPlan(plan(0, 1, 2).origins, "translated-text"), "new-voice")
        assertTrue(model.states(page).values.all { it == ReadingBlockState.UNREAD })
    }

    @Test fun newControllerAllowsResetGenerationButKeepsOnlyPreviouslyCompletedSpeech() {
        val model = LessonReadingProgressModel()
        val plan = plan(0, 1, 2)
        model.bind(page, plan, key)
        model.hear(0, generation = 9)
        model.update(event(NarrationPlaybackPhase.PLAYING, 1, generation = 9))
        model.newController()
        assertTrue(model.states(page).isEmpty())
        model.bind(page, plan, key)
        model.update(event(NarrationPlaybackPhase.PREPARED, 1, generation = 1, position = 500))
        model.update(event(NarrationPlaybackPhase.PLAYING, 1, generation = 1, position = 500))
        model.update(event(NarrationPlaybackPhase.SEGMENT_COMPLETED, 1, generation = 1, position = 500))
        assertEquals(ReadingBlockState.READ, model.states(page)[0])
        assertNull("A recovered partial segment has no reliable earlier hearing evidence", model.states(page)[1])
    }

    @Test fun coldRecoveryKeepsTheUnknownPrefixOrdinaryAndFutureBlocksUnread() {
        val model = LessonReadingProgressModel()
        model.bind(page, plan(0, 1, 2), key)
        model.update(event(NarrationPlaybackPhase.PREPARED, index = 1, position = 500))
        assertNull(model.states(page)[0])
        assertNull(model.states(page)[1])
        assertEquals(ReadingBlockState.UNREAD, model.states(page)[2])
        model.update(event(NarrationPlaybackPhase.PLAYING, index = 1, position = 500))
        assertEquals(ReadingBlockState.CURRENT, model.states(page)[1])
        model.update(event(NarrationPlaybackPhase.SEGMENT_COMPLETED, index = 1, position = 500))
        assertNull("A bookmark never proves that an entire partial segment was heard", model.states(page)[1])
        model.hear(2)
        assertEquals(ReadingBlockState.READ, model.states(page)[2])
    }

    @Test fun recoveryAtASegmentBoundaryCanCreditTheWholeResumedSegment() {
        val model = LessonReadingProgressModel()
        model.bind(page, plan(0, 1, 2), key)
        model.update(event(NarrationPlaybackPhase.PREPARED, index = 1))
        assertNull(model.states(page)[0])
        assertEquals(ReadingBlockState.UNREAD, model.states(page)[1])
        model.hear(1)
        assertEquals(ReadingBlockState.READ, model.states(page)[1])
        assertNull(model.states(page)[0])
    }

    @Test fun positiveOffsetRecoveryInTheFirstSegmentAlsoFallsBackWithoutInventingHistory() {
        val model = LessonReadingProgressModel()
        model.bind(page, plan(0, 1, 2), key)
        model.update(event(NarrationPlaybackPhase.PREPARED, position = 500))
        assertNull(model.states(page)[0])
        assertEquals(ReadingBlockState.UNREAD, model.states(page)[1])
        model.update(event(NarrationPlaybackPhase.RESTARTED))
        assertTrue(model.states(page).values.all { it == ReadingBlockState.UNREAD })
    }

    @Test fun actuallyReplayingAnUncertainPrefixResolvesItWithoutPersistentMetadata() {
        val model = LessonReadingProgressModel()
        model.bind(page, plan(0, 1, 2), key)
        model.update(event(NarrationPlaybackPhase.PREPARED, index = 1, position = 500))
        model.update(event(NarrationPlaybackPhase.POSITION_CHANGED))
        model.hear(0)
        assertEquals(ReadingBlockState.READ, model.states(page)[0])
        // The explicit rewind makes that restored future pending again.
        assertEquals(ReadingBlockState.UNREAD, model.states(page)[1])
        model.update(event(NarrationPlaybackPhase.PREPARED, generation = 2))
        assertTrue(model.states(page).values.all { it == ReadingBlockState.UNREAD })
    }

    @Test fun unboundPagesAndClearHaveNoVisualSideEffects() {
        val model = LessonReadingProgressModel()
        model.update(event(NarrationPlaybackPhase.PLAYING))
        assertTrue(model.states(page).isEmpty())
        model.bind(page, plan(0, 1, 2), key)
        model.clear()
        assertTrue(model.states(page).isEmpty())
    }

    @Test fun replayMarksTheCurrentBlockAndRestoresFutureBlocksToUnread() {
        val model = LessonReadingProgressModel()
        model.bind(page, plan(0, 1, 2), key)
        model.hear(0)
        model.hear(1)
        model.update(event(NarrationPlaybackPhase.POSITION_CHANGED))
        model.update(event(NarrationPlaybackPhase.PLAYING))
        assertEquals(ReadingBlockState.CURRENT, model.states(page)[0])
        assertEquals(ReadingBlockState.UNREAD, model.states(page)[1])
    }

    @Test fun readerRecoveryToZeroAfterEvictionClearsOldVisualPosition() {
        val model = LessonReadingProgressModel()
        model.bind(page, plan(0, 1, 2), key)
        (0..2).forEach { model.hear(it) }
        model.update(event(NarrationPlaybackPhase.FINISHED, index = 2))
        model.update(event(NarrationPlaybackPhase.STOPPED, index = 2, generation = 2))
        model.bind(page, plan(0, 1, 2), key)
        model.update(event(NarrationPlaybackPhase.PREPARED, generation = 3))
        assertTrue(model.states(page).values.all { it == ReadingBlockState.UNREAD })
    }

    @Test fun aMissingMappingCannotBeReactivatedByAnOldCallback() {
        val model = LessonReadingProgressModel()
        model.bind(page, plan(0, 1, 2), key)
        model.hear(0)
        model.forget(page)
        model.update(event(NarrationPlaybackPhase.PLAYING, index = 1))
        assertTrue(model.states(page).isEmpty())
    }
}
