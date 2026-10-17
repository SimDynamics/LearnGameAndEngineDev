package com.learngraphicsprogramming.app

import org.junit.Assert.*
import org.junit.Test

class LessonPageAdvancePolicyTest {
    private val enter = 66
    private val go = LessonPageAdvanceEnterGate.Route.NAVIGATE
    private val puzzle = LessonPageAdvanceEnterGate.Route.PUZZLE
    private val edit = LessonPageAdvanceEnterGate.Route.EDITOR
    private val skip = LessonPageAdvanceEnterGate.Route.CONSUME

    @Test fun noPuzzlesOrCompletedPageAllowsContinue() {
        assertTrue(LessonPageAdvancePolicy.canAdvance(0, 0))
        assertFalse(LessonPageAdvancePolicy.canAdvance(1, 0))
        assertFalse(LessonPageAdvancePolicy.canAdvance(3, 2))
        assertTrue(LessonPageAdvancePolicy.canAdvance(3, 3))
    }

    @Test fun theFirstDownChecksAndItsReleaseCannotAdvanceAfterSolving() {
        val gate = LessonPageAdvanceEnterGate()
        assertEquals(puzzle, gate.down("lesson/5", enter, 900L, 1000L, 0, false, false))
        // The check handler has just solved the last puzzle, making Continue ready.
        assertEquals(skip, gate.up("lesson/5", enter, 900L, 1020L, false))
        assertEquals(go, gate.down("lesson/5", enter, 900L, 1030L, 0, true, false))
        assertEquals(skip, gate.up("lesson/5", enter, 900L, 1040L, false))
    }

    @Test fun virtualEnterCanReuseTheExactTimestampAfterEachRelease() {
        val gate = LessonPageAdvanceEnterGate()
        val sameClock = 500L
        assertEquals(puzzle, gate.down("lesson/p1", enter, sameClock, sameClock, 0, false, false))
        assertEquals(skip, gate.up("lesson/p1", enter, sameClock, sameClock, false))
        // LDPlayer may inject a new synthetic key event with the same time values.
        // The prior implementation permanently swallowed this second press.
        assertEquals(puzzle, gate.down("lesson/p2", enter, sameClock, sameClock, 0, false, false))
        assertEquals(skip, gate.up("lesson/p2", enter, sameClock, sameClock, false))
        assertEquals(go, gate.down("lesson/p2", enter, sameClock, sameClock, 0, true, false))
        assertEquals(skip, gate.up("lesson/p3", enter, sameClock, sameClock, false))
    }

    @Test fun duplicatedDownInsideOnePressStillCannotDoubleSubmit() {
        val gate = LessonPageAdvanceEnterGate()
        assertEquals(puzzle, gate.down("lesson/p2", enter, 42L, 43L, 0, false, false))
        assertEquals(skip, gate.down("lesson/p2", enter, 42L, 43L, 0, false, false))
        assertEquals(skip, gate.down("lesson/p2", enter, 42L, 45L, 1, true, false))
        assertEquals(skip, gate.up("lesson/p2", enter, 42L, 46L, false))
        assertEquals(go, gate.down("lesson/p2", enter, 42L, 43L, 0, true, false))
    }

    @Test fun secondDistinctPressActsImmediatelyWithoutWaiting650msOrChangingDownTime() {
        val gate = LessonPageAdvanceEnterGate()
        assertEquals(go, gate.down("lesson/5", enter, 0L, 1000L, 0, true, false))
        assertEquals(skip, gate.up("lesson/6", enter, 0L, 1005L, false))
        assertEquals(go, gate.down("lesson/6", enter, 0L, 1010L, 0, true, false))
        assertEquals(skip, gate.up("lesson/6", enter, 0L, 1020L, false))
    }

    @Test fun repeatedAndRedispatchedDownCannotAdvanceTwice() {
        val gate = LessonPageAdvanceEnterGate()
        assertEquals(go, gate.down("lesson/5", enter, 100L, 100L, 0, true, false))
        assertEquals(skip, gate.down("lesson/6", enter, 100L, 101L, 1, true, false))
        assertEquals(skip, gate.down("lesson/6", enter, 100L, 100L, 0, true, false))
        assertEquals(skip, gate.up("lesson/6", enter, 100L, 150L, false))
        assertEquals(skip, gate.up("lesson/6", enter, 100L, 150L, false))
        assertEquals(go, gate.down("lesson/6", enter, 200L, 200L, 0, true, false))
    }

    @Test fun compilerKeepsBothKeyHalvesForMultilineEditing() {
        val gate = LessonPageAdvanceEnterGate()
        assertEquals(edit, gate.down("lesson/5", enter, 11L, 11L, 0, true, true))
        assertEquals(edit, gate.up("lesson/5", enter, 11L, 20L, false))
        assertEquals(edit, gate.down("lesson/5", enter, 25L, 25L, 0, false, true))
        assertEquals(skip, gate.up("lesson/5", enter, 25L, 30L, true))
    }

    @Test fun evenAfterPageSwitchStaleUpCanNeverFinishLesson() {
        val gate = LessonPageAdvanceEnterGate()
        assertEquals(go, gate.down("lesson/5", enter, 9L, 9L, 0, true, false))
        assertEquals(skip, gate.up("lesson/6", enter, 9L, 10L, false))
        assertEquals(skip, gate.up("lesson/6", enter, 9L, 11L, false))
        assertEquals(go, gate.down("lesson/6", enter, 10L, 12L, 0, true, false))
    }
}
