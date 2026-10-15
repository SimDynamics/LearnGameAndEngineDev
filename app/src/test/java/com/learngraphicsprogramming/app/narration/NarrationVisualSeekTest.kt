package com.learngraphicsprogramming.app.narration

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class NarrationVisualSeekTest {
    private val durations = listOf(1_000, 2_000, 3_000)

    private fun project(start: NarrationGatePosition, measured: List<Int?> = durations) =
        NarrationVisualSeek.resolve(start, measured) { _, to -> to }

    @Test fun aThreeSecondForwardControlFromAPausedPositionCrossesKnownSentences() {
        val paused = NarrationGatePosition(0, 500)
        val result = project(paused.copy(positionMs = paused.positionMs + 3_000))
        assertEquals(NarrationGatePosition(2, 500), result.destination)
        assertTrue(result.resolved)
        assertEquals(NarrationGatePosition(0, 500), paused)
    }

    @Test fun aFiveSecondForwardControlUsesTheSameMeasuredSourceAudioTimeline() {
        val result = project(NarrationGatePosition(0, 5_500))
        assertEquals(NarrationGatePosition(2, 2_500), result.destination)
        assertTrue(result.resolved)
    }

    @Test fun anUnknownFollowingDurationDefersOnlyTheUnresolvedRemainder() {
        val result = project(NarrationGatePosition(0, 3_500), listOf(1_000, null, 3_000))
        assertEquals(NarrationGatePosition(1), result.destination)
        assertFalse(result.resolved)
    }

    @Test fun aLaterMeasuredWavResolvesTheOriginalControlWithoutInventingADuration() {
        val original = NarrationGatePosition(0, 3_500)
        assertFalse(project(original, listOf(1_000, null, 3_000)).resolved)
        assertEquals(NarrationVisualSeek.Result(NarrationGatePosition(2, 500), true), project(original))
    }

    @Test fun anUnknownCurrentDurationDoesNotClaimAnyTimeWithinThatSegment() {
        val result = project(NarrationGatePosition(1, 3_000), listOf(1_000, null, 3_000))
        assertEquals(NarrationGatePosition(1), result.destination)
        assertFalse(result.resolved)
    }

    @Test fun anExactBoundaryNeedsNoGuessAboutTheFollowingWav() {
        val result = project(NarrationGatePosition(0, 1_000), listOf(1_000, null, 3_000))
        assertEquals(NarrationGatePosition(1), result.destination)
        assertTrue(result.resolved)
    }

    @Test fun anOversizedForwardControlClampsAtTheActualFinalWavEnd() {
        val result = project(NarrationGatePosition(0, 99_000))
        assertEquals(NarrationGatePosition(2, 3_000), result.destination)
        assertTrue(result.resolved)
    }

    @Test fun anUnsolvedExerciseStopsVisualProjectionBeforeItsFeedback() {
        val gate = NarrationExerciseGate(3, listOf(NarrationFeedbackGate(7, 1, 2)))
        val result = NarrationVisualSeek.resolve(NarrationGatePosition(0, 5_000), durations, gate::seek)
        assertEquals(NarrationGatePosition(1), result.destination)
        assertTrue(result.resolved)
        assertEquals(0, gate.heardThrough)
        assertFalse(gate.canPlay(1))
    }

    @Test fun aCorrectAnswerStillRequiresItsFeedbackToBeActuallyHeard() {
        val gate = NarrationExerciseGate(3, listOf(NarrationFeedbackGate(7, 1, 2)), setOf(7))
        val result = NarrationVisualSeek.resolve(NarrationGatePosition(0, 5_000), durations, gate::seek)
        assertEquals(NarrationGatePosition(1), result.destination)
        assertTrue(result.resolved)
        assertEquals(0, gate.heardThrough)
        assertTrue(gate.canPlay(1))
        assertFalse(gate.canPlay(2))
    }

    @Test fun projectingFromFeedbackCannotReplaceItsNaturalCompletion() {
        val gate = NarrationExerciseGate(3, listOf(NarrationFeedbackGate(7, 1, 2)), setOf(7))
        val result = NarrationVisualSeek.resolve(NarrationGatePosition(1, 5_000), durations, gate::seek)
        // An invalid offset past unheard feedback is re-clamped by the existing gate.
        // The visual projection must use that exact result, not invent an end position.
        assertEquals(NarrationGatePosition(1), result.destination)
        assertTrue(result.resolved)
        assertEquals(0, gate.heardThrough)
        assertFalse(gate.canPlay(2))
    }

    @Test fun previouslyHeardFeedbackDoesNotBlockANewVisualForwardControl() {
        val gate = NarrationExerciseGate(3, listOf(NarrationFeedbackGate(7, 1, 2)), setOf(7))
        gate.completedNaturally(1)
        val heardBefore = gate.heardThrough
        val result = NarrationVisualSeek.resolve(NarrationGatePosition(0, 3_500), durations, gate::seek)
        assertEquals(NarrationGatePosition(2, 500), result.destination)
        assertTrue(result.resolved)
        assertEquals(heardBefore, gate.heardThrough)
    }
}
