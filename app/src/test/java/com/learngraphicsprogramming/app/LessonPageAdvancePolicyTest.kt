package com.learngraphicsprogramming.app

import org.junit.Assert.*
import org.junit.Test

class LessonPageAdvancePolicyTest {
    @Test fun noPuzzlesMeansContinueIsEnabled() {
        assertTrue(LessonPageAdvancePolicy.canAdvance(0, 0))
    }

    @Test fun pendingPuzzlesKeepContinueAndFinishDisabled() {
        assertFalse(LessonPageAdvancePolicy.canAdvance(1, 0))
        assertFalse(LessonPageAdvancePolicy.canAdvance(3, 2))
        assertTrue(LessonPageAdvancePolicy.canAdvance(3, 3))
        assertFalse(LessonPageAdvancePolicy.canAdvance(3, 2))
    }

    @Test fun enterDoesNotDoubleAdvanceOnRepeatedKeyOrStaleRelease() {
        val gate = LessonPageAdvanceEnterGate()
        assertFalse(gate.up("p1", false))
        gate.down("p1", 0)
        gate.down("p1", 1)
        assertTrue(gate.up("p1", false))
        assertFalse(gate.up("p1", false))
        gate.down("p1", 0)
        assertFalse(gate.up("p2", false))
        gate.down("p2", 0)
        assertFalse(gate.up("p2", true))
        gate.down("p2", 0)
        gate.cancel()
        assertFalse(gate.up("p2", false))
    }
}
