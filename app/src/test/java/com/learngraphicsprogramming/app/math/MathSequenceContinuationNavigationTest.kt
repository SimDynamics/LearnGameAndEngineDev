package com.learngraphicsprogramming.app.math

import org.junit.Assert.*
import org.junit.Test

class MathSequenceContinuationNavigationTest {
    @Test fun emptyLargeOperatorVisitsBothRealBoundsThenParentThenIndependentSequenceInsertion() {
        for (kind in MathLargeOperator.entries) {
            val operator = MathExpression.LargeOperator(kind, MathExpression.Sequence(), MathExpression.Sequence())
            val model = MathExpressionModel()
            assertTrue(model.insert(operator))
            val original = model.expression
            val upper = listOf(MathCursorStep(0, "upper"))
            val lower = listOf(MathCursorStep(0, "lower"))
            assertEquals(MathCursor(upper, 0), model.cursor)
            assertEquals(listOf("upper", "lower"), operator.navigationChildren().map { it.id })

            assertTrue(model.stepRight())
            assertEquals(MathCursorPresentation.ChildBoundary(upper), model.cursorPresentation)
            assertSame(original, model.expression)

            assertTrue(model.stepRight())
            assertEquals(MathCursor(lower, 0), model.cursor)
            assertEquals(MathCursorPresentation.Interior(MathContentScope.Slot), model.cursorPresentation)
            assertSame(operator.lower, model.sequenceAt(lower))
            assertTrue(model.stepRight())
            assertEquals(MathCursorPresentation.ChildBoundary(lower), model.cursorPresentation)
            assertTrue(model.stepRight())
            assertEquals(MathCursorPresentation.ParentBoundary(emptyList(), 0), model.cursorPresentation)
            assertEquals(MathCursor(offset = 1), model.cursor)

            assertTrue("The parent border cannot hide the remaining Sequence insertion point", model.canNavigateNext)
            assertTrue(model.stepRight())
            assertEquals(MathCursor(offset = 1), model.cursor)
            assertEquals(MathCursorPresentation.Neutral, model.cursorPresentation)
            assertSame("The continuation is presentation, not an AST child", original, model.expression)
            assertEquals(MathLatexSerializer.serialize(original), MathLatexSerializer.serialize(model.expression))
            assertFalse(model.canNavigateNext)

            assertTrue(model.insertText("x"))
            assertEquals(MathExpression.Sequence(listOf(operator, MathExpression.Variable("x"))), model.expression)
            assertFalse(operator.navigationChildren().any { it.id == "body" })
        }
    }

    @Test fun continuationAndParentBorderAreDistinctReciprocalStatesAtTheSameAstCursor() {
        val model = MathExpressionModel(MathExpression.Sequence(listOf(emptyIntegral())))
        val original = model.expression
        val parent = model.saveDraft()
        assertEquals(MathCursorPresentation.ParentBoundary(emptyList(), 0), model.cursorPresentation)
        assertTrue(model.stepRight())
        val continuation = model.saveDraft()
        assertEquals(MathCursor(offset = 1), model.cursor)
        assertEquals(MathCursorPresentation.Neutral, model.cursorPresentation)
        assertNotEquals(parent, continuation)
        repeat(3) {
            assertTrue(model.canNavigatePrevious)
            assertFalse(model.canNavigateNext)
            assertEquals(continuation, model.saveDraft())
        }
        assertTrue(model.stepLeft())
        assertEquals(parent, model.saveDraft())
        assertTrue(model.stepRight())
        assertEquals(continuation, model.saveDraft())
        assertSame(original, model.expression)
    }

    @Test fun draftsPreserveTheSequenceInsertionWithoutAddingASiblingOrBody() {
        val operator = emptyIntegral()
        val source = MathExpressionModel(MathExpression.Sequence(listOf(operator)))
        assertTrue(source.stepRight())
        val draft = source.saveDraft()
        val restored = MathExpressionModel()
        assertTrue(restored.restoreDraft(draft))
        assertEquals(MathExpression.Sequence(listOf(operator)), restored.expression)
        assertEquals(MathCursor(offset = 1), restored.cursor)
        assertEquals(MathCursorPresentation.Neutral, restored.cursorPresentation)
        assertEquals(MathLatexSerializer.serialize(source.expression), MathLatexSerializer.serialize(restored.expression))
        assertEquals(listOf("upper", "lower"), restored.expression.children.single().navigationChildren().map { it.id })
        assertTrue(restored.stepLeft())
        assertEquals(MathCursorPresentation.ParentBoundary(emptyList(), 0), restored.cursorPresentation)
        assertTrue(restored.stepRight())
        assertTrue(restored.insertText("x"))
        assertEquals(MathExpression.Sequence(listOf(operator, MathExpression.Variable("x"))), restored.expression)
    }

    @Test fun deletingTheIntegralAtSequenceInsertionLeavesOnlyHome() {
        val model = MathExpressionModel(MathExpression.Sequence(listOf(emptyIntegral())))
        assertTrue(model.stepRight())
        assertTrue(model.deleteBackwardAtCurrentLevel())
        assertEquals(MathExpression.Sequence(), model.expression)
        assertEquals(MathCursor(), model.cursor)
        assertEquals(MathCursorPresentation.Neutral, model.cursorPresentation)
        assertFalse(model.canNavigateNext)
        assertFalse(model.canNavigatePrevious)
        val empty = model.saveDraft()
        repeat(5) { model.deleteBackward() }
        assertEquals(empty, model.saveDraft())
    }

    @Test fun operatorsWithoutLimitsStillExposeTheNormalFollowingSiblingInsertion() {
        for (kind in MathLargeOperator.entries) {
            val operator = MathExpression.LargeOperator(kind)
            val model = MathExpressionModel(MathExpression.Sequence(listOf(operator)))
            assertTrue(model.stepRight())
            assertEquals(MathCursorPresentation.Neutral, model.cursorPresentation)
            assertEquals(MathCursor(offset = 1), model.cursor)
            assertTrue(model.insertText("x"))
            assertEquals(MathExpression.Sequence(listOf(operator, MathExpression.Variable("x"))), model.expression)
            assertTrue("A following sibling must remain valid LaTeX without optional limits", model.isComplete())
            assertEquals(model.expression, (MathLatexParser.parse(MathLatexSerializer.serialize(model.expression)) as MathLatexParser.Result.Success).expression)
        }
    }

    @Test fun anExistingFollowingSiblingReplacesTheContinuationAndRightReachesItsIndependentParentBorder() {
        val model = MathExpressionModel(MathExpression.Sequence(listOf(emptyIntegral(), MathExpression.Variable("x"))))
        model.cursor = MathCursor(offset = 1)
        assertTrue(model.stepRight())
        assertEquals(MathCursor(offset = 2), model.cursor)
        assertEquals(MathCursorPresentation.ParentBoundary(emptyList(), 1), model.cursorPresentation)
        assertTrue(model.stepLeft())
        assertEquals(MathCursor(emptyList(), 2, MathCursorLevel.INTERIOR, MathContentScope.Atom(1, 2)), model.cursor)
        model.cursor = MathCursor(offset = 2)
        assertEquals(MathCursorPresentation.ParentBoundary(emptyList(), 1), model.cursorPresentation)
        assertFalse("An ordinary parent does not gain a FINISH state", model.canNavigateNext)
    }

    @Test fun nestedSequenceContinuationIsBetweenItsLastParentAndItsOwningChildBoundary() {
        val numerator = MathExpression.Sequence(listOf(emptyIntegral()))
        val fraction = MathExpression.Fraction(numerator, MathExpression.Sequence())
        val model = MathExpressionModel(MathExpression.Sequence(listOf(fraction)))
        val slot = listOf(MathCursorStep(0, "numerator"))
        val original = model.expression
        assertTrue(model.stepLeft())
        assertEquals(MathCursorPresentation.ChildBoundary(slot), model.cursorPresentation)
        assertTrue(model.stepLeft())
        assertEquals(MathCursor(slot, 1, MathCursorLevel.NEUTRAL, null), model.cursor)
        assertEquals(MathCursorPresentation.Neutral, model.cursorPresentation)
        val continuation = model.saveDraft()
        assertTrue(model.stepLeft())
        assertEquals(MathCursorPresentation.ParentBoundary(slot, 0), model.cursorPresentation)
        assertTrue(model.stepRight())
        assertEquals(continuation, model.saveDraft())
        assertTrue(model.stepRight())
        assertEquals(MathCursorPresentation.ChildBoundary(slot), model.cursorPresentation)
        assertTrue(model.stepLeft())
        assertEquals(continuation, model.saveDraft())
        assertSame(original, model.expression)

        val restored = MathExpressionModel()
        assertTrue(restored.restoreDraft(continuation))
        assertEquals(MathCursorPresentation.Neutral, restored.cursorPresentation)
        assertEquals(model.cursor, restored.cursor)
        assertTrue(restored.insertText("x"))
        val updated = restored.expression.children.single() as MathExpression.Fraction
        assertEquals(MathExpression.Sequence(listOf(emptyIntegral(), MathExpression.Variable("x"))), updated.numerator)
    }

    @Test fun nestedOperatorsWithoutOptionalLimitsKeepTheirContinuationAndThenReachTheNextRealOuterChild() {
        for (kind in MathLargeOperator.entries) {
            val operator = MathExpression.LargeOperator(kind)
            val fraction = MathExpression.Fraction(MathExpression.Sequence(listOf(operator)), MathExpression.Sequence())
            val model = MathExpressionModel(MathExpression.Sequence(listOf(fraction)))
            fun state(m: MathExpressionModel) = Triple(m.cursor, m.selectionRange, m.cursorPresentation)
            fun rightAfterRecreation() {
                val restored = MathExpressionModel()
                assertTrue(restored.restoreDraft(model.saveDraft()))
                assertTrue(restored.stepRight())
                assertTrue(model.stepRight())
                assertEquals("The next real child cannot depend on optional limits or remembered inverse edges", state(restored), state(model))
            }
            val original = model.expression
            val numerator = listOf(MathCursorStep(0, "numerator"))
            val denominator = listOf(MathCursorStep(0, "denominator"))
            model.cursor = MathCursor(numerator, 0, MathCursorLevel.NEUTRAL, null)
            rightAfterRecreation()
            assertEquals(MathCursorPresentation.ParentBoundary(numerator, 0), model.cursorPresentation)
            rightAfterRecreation()
            assertEquals(MathCursor(numerator, 1, MathCursorLevel.NEUTRAL, null), model.cursor)
            assertEquals(MathCursorPresentation.Neutral, model.cursorPresentation)
            rightAfterRecreation()
            assertEquals(MathCursorPresentation.ChildBoundary(numerator), model.cursorPresentation)
            rightAfterRecreation()
            assertEquals(MathCursor(denominator, 0), model.cursor)
            assertSame(fraction.denominator, model.sequenceAt(denominator))
            rightAfterRecreation()
            assertEquals(MathCursorPresentation.ChildBoundary(denominator), model.cursorPresentation)
            rightAfterRecreation()
            assertEquals(MathCursorPresentation.ParentBoundary(emptyList(), 0), model.cursorPresentation)
            assertFalse(model.canNavigateNext)
            assertSame(original, model.expression)
            assertEquals(listOf("numerator", "denominator"), fraction.navigationChildren().map { it.id })
        }
    }

    private fun emptyIntegral() = MathExpression.LargeOperator(
        MathLargeOperator.INTEGRAL, MathExpression.Sequence(), MathExpression.Sequence()
    )
}
