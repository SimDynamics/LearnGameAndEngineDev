package com.learngraphicsprogramming.app

import org.junit.Assert.*
import org.junit.Test

class LessonPageAdvancePolicyTest {
    private val key = 66

    @Test fun noPuzzlesMeansContinueIsEnabled() {
        assertTrue(LessonPageAdvancePolicy.canAdvance(0, 0))
    }

    @Test fun pendingPuzzlesKeepContinueAndFinishDisabled() {
        assertFalse(LessonPageAdvancePolicy.canAdvance(1, 0))
        assertFalse(LessonPageAdvancePolicy.canAdvance(3, 2))
        assertTrue(LessonPageAdvancePolicy.canAdvance(3, 3))
        assertFalse(LessonPageAdvancePolicy.canAdvance(3, 2))
    }

    @Test fun solvedLastPuzzleOnKeyDownNeverTurnsSameReleaseIntoNavigation() {
        val gate = LessonPageAdvanceEnterGate()
        assertEquals(LessonPageAdvanceEnterGate.Route.PUZZLE,
            gate.down("5/6", key, 1000, 1000, 0, canNavigate = false, editorOwnsEnter = false))
        // The real Check handler solved the last exercise between DOWN and UP.
        assertEquals(LessonPageAdvanceEnterGate.Route.PUZZLE,
            gate.up("5/6", key, 1000, 1070, canceled = false))
        assertEquals(LessonPageAdvanceEnterGate.Route.CONSUME,
            gate.up("5/6", key, 1000, 1070, canceled = false))
        assertEquals(LessonPageAdvanceEnterGate.Route.CONSUME,
            gate.down("5/6", key, 1100, 1100, 0, canNavigate = true, editorOwnsEnter = false))
        assertEquals(LessonPageAdvanceEnterGate.Route.NAVIGATE,
            gate.up("5/6", key, 1100, 1160, canceled = false))
    }

    @Test fun onePressAdvancesOnePageAndAnImmediateNewPressFinishesTheNextPage() {
        val gate = LessonPageAdvanceEnterGate()
        assertEquals(LessonPageAdvanceEnterGate.Route.CONSUME,
            gate.down("5/6", key, 2000, 2000, 0, true, false))
        // Held Enter is a repeat, never a second navigation command.
        assertEquals(LessonPageAdvanceEnterGate.Route.CONSUME,
            gate.down("5/6", key, 2000, 2040, 1, true, false))
        assertEquals(LessonPageAdvanceEnterGate.Route.NAVIGATE,
            gate.up("5/6", key, 2000, 2100, false))
        assertEquals(LessonPageAdvanceEnterGate.Route.CONSUME,
            gate.up("6/6", key, 2000, 2100, false))
        // A duplicated DOWN/UP from the first press must not finish page 6.
        assertEquals(LessonPageAdvanceEnterGate.Route.CONSUME,
            gate.down("6/6", key, 2000, 2110, 0, true, false))
        assertEquals(LessonPageAdvanceEnterGate.Route.CONSUME,
            gate.up("6/6", key, 2000, 2150, false))
        // A genuinely new press works even before 650 ms. No artificial delay.
        assertEquals(LessonPageAdvanceEnterGate.Route.CONSUME,
            gate.down("6/6", key, 2160, 2160, 0, true, false))
        assertEquals(LessonPageAdvanceEnterGate.Route.NAVIGATE,
            gate.up("6/6", key, 2160, 2200, false))
    }

    @Test fun editorOwnsEnterRegardlessOfExerciseCompletion() {
        val gate = LessonPageAdvanceEnterGate()
        assertEquals(LessonPageAdvanceEnterGate.Route.EDITOR,
            gate.down("6/6", key, 100, 100, 0, true, true))
        assertEquals(LessonPageAdvanceEnterGate.Route.EDITOR,
            gate.up("6/6", key, 100, 140, false))
    }

    @Test fun canceledOrOldPageReleaseNeverAdvances() {
        val gate = LessonPageAdvanceEnterGate()
        gate.down("5/6", key, 100, 100, 0, true, false)
        assertEquals(LessonPageAdvanceEnterGate.Route.CONSUME,
            gate.up("6/6", key, 100, 145, false))
        gate.down("6/6", key, 200, 200, 0, true, false)
        assertEquals(LessonPageAdvanceEnterGate.Route.CONSUME,
            gate.up("6/6", key, 200, 245, true))
    }
}
