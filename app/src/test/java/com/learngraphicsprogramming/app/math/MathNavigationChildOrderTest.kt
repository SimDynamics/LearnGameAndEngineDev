package com.learngraphicsprogramming.app.math

import org.junit.Assert.*
import org.junit.Test

/** Child order belongs to the common descriptors, independent of measured renderer geometry. */
class MathNavigationChildOrderTest {
    private fun atom(value: String) = MathExpression.Sequence(listOf(MathExpression.Variable(value)))

    @Test fun scriptsDeclareUpperBeforeLowerAndLeftVisitsThemAfterTheirParentContent() {
        val scripts = MathExpression.Scripts(atom("x"), upper = atom("a"), lower = atom("b"))
        assertEquals(listOf("base", "upper", "lower"), scripts.navigationChildren().map { it.id })
        assertEquals(listOf("upper", "lower"), scripts.navigationChildren()
            .filter { it.role == MathNavigationRole.CHILD }.map { it.id })
        val model = MathExpressionModel(MathExpression.Sequence(listOf(scripts)))
        assertTrue(model.stepLeft())
        assertEquals(MathCursor(listOf(MathCursorStep(0, "base")), 1), model.cursor)
        assertEquals(listOf("upper", "lower"), visitChildBoundaries(model))
    }

    @Test fun everyLargeOperatorDeclaresUpperBeforeLowerWhileLeftOpensItsBodyFirst() {
        for (kind in MathLargeOperator.entries) {
            val node = MathExpression.LargeOperator(
                kind = kind,
                lower = atom("c"),
                upper = atom("a")
            )
            assertEquals(listOf("upper", "lower", "body"), node.navigationChildren().map { it.id })
            val model = MathExpressionModel(MathExpression.Sequence(listOf(node)))
            assertTrue(model.stepLeft())
            assertEquals(MathCursor(listOf(MathCursorStep(0, "body")), 1), model.cursor)
            assertEquals(listOf("upper", "lower"), visitChildBoundaries(model))
        }
    }

    @Test fun fourDeclaredChildrenVisitUpperLeftUpperRightLowerLeftLowerRight() {
        // r0c0=A upper-left; r0c1=B upper-right; r1c0=C lower-left; r1c1=D lower-right.
        val matrix = MathExpression.Matrix(listOf(listOf(atom("a"), atom("b")), listOf(atom("c"), atom("d"))))
        val children = matrix.navigationChildren()
        assertEquals(listOf("r0c0", "r0c1", "r1c0", "r1c1"), children.map { it.id })
        assertEquals(listOf("a", "b", "c", "d"), children.map { MathLatexSerializer.serialize(it.content) })
        val model = MathExpressionModel(MathExpression.Sequence(listOf(matrix)))
        assertEquals(listOf("r0c0", "r0c1", "r1c0", "r1c1"), visitChildBoundaries(model))
    }

    @Test fun omittingAnOptionalScriptDoesNotCreateAnEmptyChildOrChangeTheRemainingOrder() {
        val scripts = MathExpression.Scripts(atom("x"), upper = atom("a"), lower = atom("b"))
        assertEquals(listOf("base", "upper"), scripts.copy(lower = null).navigationChildren().map { it.id })
        assertEquals(listOf("base", "lower"), scripts.copy(upper = null).navigationChildren().map { it.id })
        assertEquals(listOf("base"), scripts.copy(upper = null, lower = null).navigationChildren().map { it.id })
    }

    private fun visitChildBoundaries(model: MathExpressionModel): List<String> {
        val original = model.expression
        val visited = mutableListOf<String>()
        var steps = 0
        while (model.canNavigatePrevious) {
            assertTrue("The declared traversal terminates at HOME", steps++ < 100)
            assertTrue(model.stepLeft())
            (model.cursorPresentation as? MathCursorPresentation.ChildBoundary)?.let {
                assertTrue(it.after)
                visited += it.slotPath.last().slot
            }
            assertSame("Navigation is read-only with respect to the AST", original, model.expression)
        }
        assertEquals(MathCursor(), model.cursor)
        return visited
    }
}
