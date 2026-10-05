package com.learngraphicsprogramming.app.math

import org.junit.Assert.*
import org.junit.Test

class MathLargeOperatorSiblingNavigationTest {
    @Test fun theEmptyIndependentContinuationIsEditableBeforeItsExistingNeutralFrontier() {
        val integral = MathExpression.LargeOperator(
            MathLargeOperator.INTEGRAL,
            lower = MathExpression.Sequence(listOf(MathExpression.Number("0"))),
            upper = MathExpression.Sequence(listOf(MathExpression.Number("1")))
        )
        val original = MathExpression.Sequence(listOf(integral))
        val model = MathExpressionModel(original)
        val parent = snapshot(model)
        assertEquals(MathCursorPresentation.ParentBoundary(emptyList(), 0), model.cursorPresentation)

        assertTrue(model.canNavigateNext)
        assertTrue(model.stepRight())
        val interior = snapshot(model)
        val emptySiblingScope = MathContentScope.Atom(1, 1)
        assertEquals(MathCursor(emptyList(), 1, MathCursorLevel.INTERIOR, emptySiblingScope), model.cursor)
        assertEquals(MathCursorPresentation.Interior(emptySiblingScope), model.cursorPresentation)
        assertTrue("The empty sibling needs an editable checkpoint before its right frontier", model.canNavigateNext)
        assertFalse(model.isAtSequenceInsertion)
        assertSame(original, model.expression)
        val interiorDraft = model.saveDraft()

        assertTrue(model.stepRight())
        val frontier = snapshot(model)
        assertEquals(MathCursor(offset = 1), model.cursor)
        assertEquals(MathCursorPresentation.Neutral, model.cursorPresentation)
        assertTrue(model.isAtSequenceInsertion)
        assertFalse(model.canNavigateNext)
        assertSame(original, model.expression)
        assertTrue(model.stepLeft())
        assertEquals(interior, snapshot(model))
        assertTrue(model.stepLeft())
        assertEquals(parent, snapshot(model))

        val restoredRight = MathExpressionModel()
        assertTrue(restoredRight.restoreDraft(interiorDraft))
        assertEquals(interior, snapshot(restoredRight))
        assertTrue(restoredRight.canNavigateNext)
        assertTrue(restoredRight.stepRight())
        assertEquals(frontier, snapshot(restoredRight))
        val restoredLeft = MathExpressionModel()
        assertTrue(restoredLeft.restoreDraft(interiorDraft))
        assertTrue(restoredLeft.stepLeft())
        assertEquals(parent, snapshot(restoredLeft))

        assertTrue(model.stepRight())
        assertEquals(interior, snapshot(model))
        assertTrue(model.insertText("x"))
        assertEquals(listOf(integral, MathExpression.Variable("x")), model.expression.children)
        assertSame(integral, model.expression.children.first())
        assertEquals(listOf("upper", "lower"), integral.navigationChildren().map { it.id })
        assertNull(model.sequenceAt(listOf(MathCursorStep(0, "body"))))
    }

    @Test fun rightEntersTheIndependentSiblingAndLeftExactlyRetracesEveryCheckpoint() {
        val siblings = listOf<MathExpression>(
            MathExpression.Number("12"),
            MathExpression.Scripts(
                MathExpression.Sequence(listOf(MathExpression.Variable("x"))),
                upper = MathExpression.Sequence(listOf(MathExpression.Number("2")))
            )
        )
        for (sibling in siblings) {
            val integral = MathExpression.LargeOperator(
                MathLargeOperator.INTEGRAL,
                lower = MathExpression.Sequence(listOf(MathExpression.Number("0"))),
                upper = MathExpression.Sequence(listOf(MathExpression.Number("1")))
            )
            val original = MathExpression.Sequence(listOf(integral, sibling))
            val model = MathExpressionModel(original)
            model.cursor = MathCursor()
            val states = mutableListOf(snapshot(model))
            var enteredSibling = false
            var visitedIntegralBorder = false
            repeat(40) {
                if (!model.canNavigateNext) return@repeat
                val restored = MathExpressionModel()
                assertTrue(restored.restoreDraft(model.saveDraft()))
                assertTrue(restored.canNavigateNext)
                assertTrue(restored.stepRight())
                assertTrue(model.stepRight())
                assertEquals("Transitions must not depend on navigation history", snapshot(restored), snapshot(model))
                assertSame("Navigation must leave the Sequence AST unchanged", original, model.expression)
                val state = snapshot(model)
                assertFalse("RIGHT must never remain at the same checkpoint", state == states.last())
                states += state
                if (state.third == MathCursorPresentation.ParentBoundary(emptyList(), 0)) {
                    visitedIntegralBorder = true
                }
                val siblingInterior = model.navigationLevel == MathCursorLevel.INTERIOR &&
                    (model.cursor.path.firstOrNull()?.nodeIndex == 1 ||
                        (model.cursor.path.isEmpty() && model.editingScope == MathContentScope.Atom(1, 2)))
                if (siblingInterior) {
                    assertTrue("The sibling is reached after leaving the integral", visitedIntegralBorder)
                    assertTrue("An editable sibling must still be able to reach its right border", model.canNavigateNext)
                    enteredSibling = true
                }
            }
            assertTrue("RIGHT must enter the independent expression's editable content", enteredSibling)
            assertEquals(MathCursorPresentation.ParentBoundary(emptyList(), 1), model.cursorPresentation)
            assertFalse(model.canNavigateNext)
            val reverse = mutableListOf(snapshot(model))
            repeat(states.size - 1) {
                assertTrue(model.canNavigatePrevious)
                assertTrue(model.stepLeft())
                assertSame(original, model.expression)
                reverse += snapshot(model)
            }
            assertEquals("LEFT must traverse exactly the same checkpoints in reverse", states.reversed(), reverse)
            assertFalse(model.canNavigatePrevious)
            assertSame(integral, original.children[0])
            assertSame(sibling, original.children[1])
            assertEquals(listOf("upper", "lower"), integral.navigationChildren().map { it.id })
            assertNull(model.sequenceAt(listOf(MathCursorStep(0, "body"))))
        }
    }

    private fun snapshot(model: MathExpressionModel) =
        Triple(model.cursor, model.selectionRange, model.cursorPresentation)
}
