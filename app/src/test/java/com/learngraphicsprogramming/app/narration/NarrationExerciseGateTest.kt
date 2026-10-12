package com.learngraphicsprogramming.app.narration

import org.junit.Assert.*
import org.junit.Test

class NarrationExerciseGateTest {
    private val gates = listOf(NarrationFeedbackGate(2,3,5),NarrationFeedbackGate(6,8,10))
    @Test fun unsolvedExerciseAllowsItsQuestionButNotFeedbackOrLaterText() {
        val gate = NarrationExerciseGate(12,gates)
        for(i in 0..2) assertTrue(gate.canPlay(i))
        for(i in 3..11) assertFalse(gate.canPlay(i))
        assertEquals(NarrationGatePosition(3),gate.clamp(NarrationGatePosition(11,6000)))
    }
    @Test fun correctAnswerReleasesFeedbackSequentiallyBeforeLaterText() {
        val gate = NarrationExerciseGate(12,gates,setOf(2))
        assertTrue(gate.canPlay(3)); assertFalse(gate.canPlay(4)); assertFalse(gate.canPlay(5))
        gate.completedNaturally(3)
        assertTrue(gate.canPlay(4)); assertFalse(gate.canPlay(5))
        gate.completedNaturally(4)
        assertTrue(gate.canPlay(7)); assertFalse(gate.canPlay(8))
    }
    @Test fun correctAnswerOutOfOrderDoesNotBypassFirstUnsolvedExercise() {
        val gate = NarrationExerciseGate(12,gates,setOf(6))
        assertFalse(gate.canPlay(3)); assertFalse(gate.canPlay(10))
        gate.updateCorrect(setOf(2,6))
        assertTrue(gate.canPlay(3)); assertFalse(gate.canPlay(8))
    }
    @Test fun seekingForwardByThreeFiveOrMoreDoesNotSubstituteForHearingFeedback() {
        val gate = NarrationExerciseGate(12,gates,setOf(2))
        for(ms in listOf(3000L,5000L,999999L)) {
            assertEquals(NarrationGatePosition(3),gate.seek(NarrationGatePosition(2,1000),NarrationGatePosition(9,ms)))
            assertEquals(NarrationGatePosition(3,700),gate.seek(NarrationGatePosition(3,700),NarrationGatePosition(3,ms)))
        }
        assertEquals(NarrationGatePosition(3,200),gate.seek(NarrationGatePosition(3,700),NarrationGatePosition(3,200)))
    }
    @Test fun onlyNaturalCompletionInOrderUnlocksFollowingFeedback() {
        val gate = NarrationExerciseGate(12,gates,setOf(2))
        gate.completedNaturally(4)
        assertFalse(gate.canPlay(4))
        gate.completedNaturally(3); gate.completedNaturally(3)
        assertFalse(gate.canPlay(5))
        gate.completedNaturally(4)
        assertTrue(gate.canPlay(5))
    }
    @Test fun resetInvalidatesOldBookmarkPermissionWithoutDeletingAudio() {
        val gate = NarrationExerciseGate(12,gates,setOf(2,6),10)
        assertTrue(gate.canPlay(11))
        gate.updateCorrect(setOf(6))
        assertEquals(3,gate.heardThrough)
        assertEquals(NarrationGatePosition(3),gate.clamp(NarrationGatePosition(11,4000)))
        assertFalse(gate.canPlay(3))
    }
    @Test fun partialFeedbackBookmarkCanResumeWithoutRehearingEarlierSentences() {
        val gate = NarrationExerciseGate(12,gates,setOf(2),4)
        val at = NarrationGatePosition(4,1250)
        assertEquals(at,gate.clamp(at))
        assertTrue(gate.canPlay(4)); assertFalse(gate.canPlay(5))
    }
    @Test fun restartRequiresFeedbackAgainButDoesNotResetAnswers() {
        val gate = NarrationExerciseGate(12,gates,setOf(2,6),10)
        gate.restart()
        assertTrue(gate.canPlay(0)); assertTrue(gate.canPlay(3)); assertFalse(gate.canPlay(4))
    }
    @Test fun pageWithoutExercisesHasNoArtificialGate() {
        val gate = NarrationExerciseGate(4,emptyList())
        for(i in 0..3) assertTrue(gate.canPlay(i))
        assertFalse(gate.canPlay(4)); assertEquals(null,gate.boundary())
    }
    @Test fun lastSuccessfulFeedbackCanFinishThePage() {
        val gate = NarrationExerciseGate(3,listOf(NarrationFeedbackGate(0,1,3)),setOf(0))
        gate.completedNaturally(1); gate.completedNaturally(2)
        assertEquals(null,gate.boundary())
        assertFalse(gate.canPlay(3))
    }
    @Test fun everyGateInLongPageIsVisitedInOrder() {
        val all = (0 until 1000).map { NarrationFeedbackGate(it,it*3+1,it*3+3) }
        val gate = NarrationExerciseGate(3000,all)
        val solved = mutableSetOf<Int>()
        for(g in all) {
            assertTrue(gate.canPlay(g.start-1)); assertFalse(gate.canPlay(g.start))
            solved += g.blockIndex; gate.updateCorrect(solved)
            assertTrue(gate.canPlay(g.start)); gate.completedNaturally(g.start)
            assertTrue(gate.canPlay(g.start+1)); gate.completedNaturally(g.start+1)
        }
        assertEquals(null,gate.boundary())
    }
}
