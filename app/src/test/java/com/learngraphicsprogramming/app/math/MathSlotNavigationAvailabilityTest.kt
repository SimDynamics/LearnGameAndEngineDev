package com.learngraphicsprogramming.app.math

import org.junit.Assert.*
import org.junit.Test

class MathSlotNavigationAvailabilityTest {
    private fun parse(source: String) = (MathLatexParser.parse(source) as MathLatexParser.Result.Success).expression
    private fun path(vararg names: String) = names.map { MathCursorStep(0, it) }

    @Test fun plainExpressionHasNoSlotNavigationEvenWhenItsAtomicCursorCanMove() {
        val model = MathExpressionModel(parse("x+1"))
        assertFalse(model.hasNavigableSlots)
        assertTrue(model.canNavigatePrevious)
        assertFalse(model.canNavigateNext)
        assertFalse(model.previousSlot())
        assertFalse(model.nextSlot())
        assertTrue(model.moveLeft())
        model.cursor = MathCursor()
        assertFalse(model.canNavigatePrevious)
        assertTrue(model.canNavigateNext)
        assertTrue(model.moveRight())
    }

    @Test fun controlsRemainPresentAtEveryFractionPositionIncludingItsOuterBoundaries() {
        val model = MathExpressionModel(parse("\\frac{3}{4}"))
        model.cursor = MathCursor()
        assertTrue(model.hasNavigableSlots)
        assertFalse(model.canNavigatePrevious)
        assertTrue(model.canNavigateNext)
        assertTrue(model.nextSlot())
        assertEquals(path("numerator"), model.cursor.path)
        assertTrue(model.hasNavigableSlots)
        assertTrue(model.canNavigatePrevious)
        assertTrue(model.canNavigateNext)
        assertTrue(model.nextSlot())
        assertEquals(path("denominator"), model.cursor.path)
        assertTrue(model.hasNavigableSlots)
        assertTrue(model.canNavigatePrevious)
        assertTrue(model.canNavigateNext)
        assertTrue(model.nextSlot())
        assertTrue(model.cursor.path.isEmpty())
        assertTrue(model.hasNavigableSlots)
        assertTrue(model.canNavigatePrevious)
        assertFalse(model.canNavigateNext)
        model.clear()
        assertFalse(model.hasNavigableSlots)
    }

    @Test fun queryingAvailabilityNeverChangesExpressionCursorOrSelection() {
        val model = MathExpressionModel(parse("x_1^2"))
        model.select(path("upper"), 0, 1)
        val before = model.saveDraft()
        repeat(30) {
            assertTrue(model.hasNavigableSlots)
            assertTrue(model.canNavigatePrevious)
            assertTrue(model.canNavigateNext)
        }
        assertEquals(before, model.saveDraft())
        assertNotNull(model.selectionRange)
    }

    @Test fun nestedSlotAvailabilityMatchesTraversalInBothDirections() {
        val model = MathExpressionModel(parse("\\frac{\\sqrt[3]{x}}{2}"))
        val original = MathLatexSerializer.serialize(model.expression)
        model.cursor = MathCursor()
        val forward = listOf(
            MathCursor(path("numerator"), 0),
            MathCursor(path("numerator", "index"), 0),
            MathCursor(path("numerator", "radicand"), 0),
            MathCursor(path("numerator"), 1),
            MathCursor(path("denominator"), 0),
            MathCursor(offset = 1)
        )
        forward.forEach { expected ->
            assertTrue(model.hasNavigableSlots)
            assertTrue(model.canNavigateNext)
            assertTrue(model.nextSlot())
            assertEquals(expected, model.cursor)
        }
        assertFalse(model.canNavigateNext)
        assertFalse(model.nextSlot())
        val backward = listOf(
            MathCursor(path("denominator"), 1),
            MathCursor(path("numerator"), 1),
            MathCursor(path("numerator", "radicand"), 1),
            MathCursor(path("numerator", "index"), 1),
            MathCursor(path("numerator"), 0),
            MathCursor()
        )
        backward.forEach { expected ->
            assertTrue(model.canNavigatePrevious)
            assertTrue(model.previousSlot())
            assertEquals(expected, model.cursor)
        }
        assertFalse(model.canNavigatePrevious)
        assertFalse(model.previousSlot())
        assertEquals(original, MathLatexSerializer.serialize(model.expression))
    }

    @Test fun slotNavigationSkipsOrdinaryAtomsWithoutPretendingTheyAreAdditionalSlots() {
        val model = MathExpressionModel(parse("x+\\frac{1}{2}+y+\\sqrt{3}"))
        model.cursor = MathCursor(offset = 2)
        assertTrue(model.canNavigatePrevious)
        assertTrue(model.nextSlot())
        assertEquals(listOf(MathCursorStep(2, "numerator")), model.cursor.path)
        assertTrue(model.nextSlot())
        assertEquals(listOf(MathCursorStep(2, "denominator")), model.cursor.path)
        assertTrue(model.nextSlot())
        assertEquals(MathCursor(offset = 3), model.cursor)
        assertTrue(model.nextSlot())
        assertEquals(listOf(MathCursorStep(6, "radicand")), model.cursor.path)
        assertTrue(model.nextSlot())
        assertFalse(model.canNavigateNext)
        assertFalse(model.nextSlot())
        assertTrue(model.previousSlot())
        assertTrue(model.previousSlot())
        assertEquals(MathCursor(offset = 6), model.cursor)
        assertTrue(model.previousSlot())
        assertEquals(listOf(MathCursorStep(2, "denominator")), model.cursor.path)
    }

    @Test fun emptyAndFilledMatrixCellsHaveTheSamePersistentNavigationControls() {
        val empty = MathExpression.Sequence()
        val model = MathExpressionModel(MathExpression.Sequence(listOf(MathExpression.Matrix(List(2) { List(2) { empty } }))))
        model.cursor = MathCursor()
        listOf("r0c0", "r0c1", "r1c0", "r1c1").forEachIndexed { index, slot ->
            assertTrue(model.hasNavigableSlots)
            assertTrue(model.canNavigateNext)
            assertTrue(model.nextSlot())
            assertEquals(path(slot), model.cursor.path)
            assertTrue(model.insertText((index + 1).toString()))
            assertTrue(model.hasNavigableSlots)
            assertTrue(model.canNavigatePrevious)
        }
        assertTrue(model.nextSlot())
        assertFalse(model.canNavigateNext)
        assertTrue(model.canNavigatePrevious)
        assertTrue(model.hasNavigableSlots)
        assertEquals("\\begin{matrix}1&2\\\\3&4\\end{matrix}", MathLatexSerializer.serialize(model.expression))
    }

    @Test fun slotActionsAreDistinctFromTheMathematicalArrowAndAtomicMovement() {
        assertSame(MathKeyboardAction.PreviousSlot, MathKeyboardAction.fromShortcut("PREVIOUS_SLOT"))
        assertSame(MathKeyboardAction.NextSlot, MathKeyboardAction.fromShortcut("NEXT_SLOT"))
        assertEquals("Slot anterior", MathKeyboardAction.PreviousSlot.contentDescription)
        assertEquals("Slot siguiente", MathKeyboardAction.NextSlot.contentDescription)
        assertEquals("", MathKeyboardAction.PreviousSlot.label)
        assertEquals("", MathKeyboardAction.NextSlot.label)
        assertTrue(MathKeyboardAction.fromShortcut("→") is MathKeyboardAction.Symbol)
        assertSame(MathKeyboardAction.MoveRight, MathKeyboardAction.fromShortcut("MOVE_RIGHT"))
        assertSame(MathKeyboardAction.MoveLeft, MathKeyboardAction.fromShortcut("MOVE_LEFT"))
    }
}
