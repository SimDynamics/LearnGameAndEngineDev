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

    @Test fun virtualUpWithDifferentDownTimeStillEndsTheHeldPress() {
        val gate = LessonPageAdvanceEnterGate()
        assertEquals(puzzle, gate.down("lesson/p2", enter, 99L, 120L, 0, false, false))
        assertEquals(skip, gate.up("lesson/p2", enter, 0L, 125L, false))
        assertEquals(go, gate.down("lesson/p2", enter, 99L, 120L, 0, true, false))
        assertEquals(skip, gate.up("lesson/p3", enter, 0L, 125L, false))
    }

    @Test fun repeatedKeyDownCannotDoubleSubmitButNewPressAlwaysActs() {
        val gate = LessonPageAdvanceEnterGate()
        assertEquals(puzzle, gate.down("lesson/p2", enter, 42L, 43L, 0, false, false))
        assertEquals(skip, gate.down("lesson/p2", enter, 42L, 45L, 1, true, false))
        assertEquals(skip, gate.up("lesson/p2", enter, 42L, 46L, false))
        assertEquals(go, gate.down("lesson/p2", enter, 42L, 43L, 0, true, false))
    }

    @Test fun missingKeyUpCannotConsumeTheNextRealEnterAfterMappedGravePause() {
        val gate = LessonPageAdvanceEnterGate()
        val mapped = 500L
        // The first wrong-answer Enter was handled, but Android/IME did not
        // deliver its UP before the remapped Grave key tapped the reader.
        assertEquals(puzzle, gate.down("lesson4/p1", enter, mapped, mapped, 0, false, false))
        // The student's new correct selection must work on the very first Enter,
        // including when LDPlayer reuses the exact same timestamps.
        assertEquals(puzzle, gate.down("lesson4/p1", enter, mapped, mapped, 0, false, false))
        assertEquals(skip, gate.up("lesson4/p1", enter, mapped, mapped, false))
        // After the second correct answer the next Enter can advance.
        assertEquals(go, gate.down("lesson4/p1", enter, mapped, mapped, 0, true, false))
        assertEquals(skip, gate.up("lesson4/p2", enter, mapped, mapped, false))
    }

    @Test fun pendingOlderPageDoesNotPreventTheFirstEnterOnNextPage() {
        val gate = LessonPageAdvanceEnterGate()
        assertEquals(go, gate.down("lesson3/p5", enter, 0L, 0L, 0, true, false))
        // The virtual-key UP was intercepted; next page still receives a fresh DOWN.
        assertEquals(go, gate.down("lesson3/p6", enter, 0L, 0L, 0, true, false))
        assertEquals(skip, gate.up("lesson3/p6", enter, 0L, 0L, false))
    }

    @Test fun secondDistinctPressActsImmediatelyWithoutWaiting650msOrChangingDownTime() {
        val gate = LessonPageAdvanceEnterGate()
        assertEquals(go, gate.down("lesson/5", enter, 0L, 1000L, 0, true, false))
        assertEquals(skip, gate.up("lesson/6", enter, 0L, 1005L, false))
        assertEquals(go, gate.down("lesson/6", enter, 0L, 1010L, 0, true, false))
        assertEquals(skip, gate.up("lesson/6", enter, 0L, 1020L, false))
    }

    @Test fun repeatCountProtectsAgainstHeldEnterAndAnIndependentPressStillWorks() {
        val gate = LessonPageAdvanceEnterGate()
        assertEquals(go, gate.down("lesson/5", enter, 100L, 100L, 0, true, false))
        // Holding the same physical key on a newly opened page generates a repeat,
        // not a second navigation.
        assertEquals(skip, gate.down("lesson/6", enter, 100L, 101L, 1, true, false))
        assertEquals(skip, gate.up("lesson/6", enter, 100L, 150L, false))
        assertEquals(skip, gate.up("lesson/6", enter, 100L, 150L, false))
        // The next genuinely new press works even when LDPlayer sends 100/100 again.
        assertEquals(go, gate.down("lesson/6", enter, 100L, 100L, 0, true, false))
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
    @Test fun aLonePhysicalKeyUpChecksOnceAndOnlyANewReleaseCanAdvance() {
        val gate = LessonPageAdvanceEnterGate()
        // Observed in LDPlayer even when keyboard mapping is disabled:
        // the Activity sees ACTION_UP but its ACTION_DOWN never arrives.
        assertEquals(puzzle, gate.up("lesson4/p1", enter, 1000L, 1130L, false,
            canNavigate = false, editorOwnsEnter = false))
        // After grading correctly the same release must not also continue.
        assertEquals(skip, gate.up("lesson4/p1", enter, 1000L, 1140L, false,
            canNavigate = true, editorOwnsEnter = false))
        assertEquals(go, gate.up("lesson4/p1", enter, 1500L, 1610L, false,
            canNavigate = true, editorOwnsEnter = false))
        // Even a late duplicate cannot act on the new page.
        assertEquals(skip, gate.up("lesson4/p2", enter, 1500L, 1615L, false,
            canNavigate = true, editorOwnsEnter = false))
    }

    @Test fun completedDownUpCannotRepeatOrAdvanceAfterPuzzleIsSolved() {
        val gate = LessonPageAdvanceEnterGate()
        assertEquals(puzzle, gate.down("lesson3/p5", enter, 1000L, 1000L, 0, false, false))
        assertEquals(skip, gate.up("lesson3/p5", enter, 1000L, 1120L, false,
            canNavigate = true, editorOwnsEnter = false))
        assertEquals(skip, gate.up("lesson3/p5", enter, 1000L, 1140L, false,
            canNavigate = true, editorOwnsEnter = false))
        assertEquals(go, gate.down("lesson3/p5", enter, 1000L, 1000L, 0, true, false))
        assertEquals(skip, gate.up("lesson3/p6", enter, 1000L, 1120L, false,
            canNavigate = true, editorOwnsEnter = false))
    }

    @Test fun canceledInvalidAndEditorOwnedOrphanReleasesAreNotPuzzleClicks() {
        val gate = LessonPageAdvanceEnterGate()
        assertEquals(skip, gate.up("lesson/p1", enter, 0L, 100L, false,
            canNavigate = false, editorOwnsEnter = false))
        assertEquals(skip, gate.up("lesson/p1", enter, 200L, 199L, false,
            canNavigate = false, editorOwnsEnter = false))
        assertEquals(skip, gate.up("lesson/p1", enter, 300L, 400L, true,
            canNavigate = false, editorOwnsEnter = false))
        assertEquals(skip, gate.up("lesson/p1", enter, 300L, 401L, false,
            canNavigate = false, editorOwnsEnter = false))
        assertEquals(edit, gate.up("lesson/p1", enter, 500L, 520L, false,
            canNavigate = true, editorOwnsEnter = true))
        assertEquals(puzzle, gate.up("lesson/p1", enter, 600L, 630L, false,
            canNavigate = false, editorOwnsEnter = false))
    }

    @Test fun imeActionAndItsUnseenPhysicalPressAreOneSubmission() {
        val gate = LessonPageAdvanceEnterGate()
        gate.editorSubmitted(1100L)
        assertEquals(skip, gate.up("lesson/p4", enter, 1000L, 1150L, false,
            canNavigate = true))
        assertEquals(skip, gate.up("lesson/p4", enter, 1000L, 1151L, false,
            canNavigate = true))
        assertEquals(go, gate.up("lesson/p4", enter, 1200L, 1250L, false,
            canNavigate = true))
    }

    @Test fun softImeDoneWithoutAPhysicalReleaseNeverConsumesTheNextNewPress() {
        val gate = LessonPageAdvanceEnterGate()
        gate.editorSubmitted(1000L)
        assertEquals(go, gate.up("lesson/p4", enter, 1001L, 1010L, false,
            canNavigate = true))
        gate.editorSubmitted(2000L)
        assertEquals(go, gate.down("lesson/p4", enter, 2000L, 2000L, 0, true, false))
        assertEquals(skip, gate.up("lesson/p5", enter, 2000L, 2001L, false,
            canNavigate = true))
    }

    @Test fun aNewDownStillActsImmediatelyEvenIfTheImeClockWasReused() {
        val gate = LessonPageAdvanceEnterGate()
        gate.editorSubmitted(500L, enter)
        assertEquals(go, gate.down("lesson/p4", enter, 500L, 500L, 0, true, false))
        assertEquals(skip, gate.down("lesson/p5", enter, 500L, 501L, 1, true, false))
        assertEquals(skip, gate.up("lesson/p5", enter, 500L, 510L, false, true))
    }

    @Test fun theReleaseAfterAnIncorrectImeAnswerCannotCheckAgain() {
        val gate = LessonPageAdvanceEnterGate()
        gate.editorSubmitted(1000L)
        assertEquals(skip, gate.up("lesson/p4", enter, 900L, 1001L, false,
            canNavigate = false))
        assertEquals(puzzle, gate.up("lesson/p4", enter, 1100L, 1110L, false,
            canNavigate = false))
    }

    @Test fun aLateReleaseFromTheImeCannotSkipAnotherPage() {
        val gate = LessonPageAdvanceEnterGate()
        gate.editorSubmitted(1000L)
        assertEquals(skip, gate.up("lesson/p5", enter, 900L, 1001L, false,
            canNavigate = true))
        assertEquals(go, gate.up("lesson/p5", enter, 1100L, 1110L, false,
            canNavigate = true))
    }

    @Test fun numpadImeReleaseAndEditorNewlinesRetainTheirOwnership() {
        val gate = LessonPageAdvanceEnterGate()
        gate.editorSubmitted(1000L, 160)
        assertEquals(skip, gate.up("lesson/p4", 160, 900L, 1001L, false, true))
        assertEquals(edit, gate.down("lesson/p4", enter, 1100L, 1100L, 0, true, true))
        assertEquals(edit, gate.up("lesson/p4", enter, 1100L, 1110L, false, true, true))
    }


}
