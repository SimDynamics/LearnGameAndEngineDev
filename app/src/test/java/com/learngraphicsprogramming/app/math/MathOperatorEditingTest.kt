package com.learngraphicsprogramming.app.math

import org.junit.Assert.*
import org.junit.Test

/** Incomplete operators remain editable ASTs; navigation and deletion never require evaluation. */
class MathOperatorEditingTest {
    private fun parse(source: String) = (MathLatexParser.parse(source) as MathLatexParser.Result.Success).expression
    private data class State(val cursor: MathCursor, val presentation: MathCursorPresentation, val selection: MathSelection?)
    private fun state(model: MathExpressionModel) = State(model.cursor, model.cursorPresentation, model.selectionRange)

    private fun traverse(model: MathExpressionModel, forward: Boolean): List<State> {
        val result = mutableListOf(state(model))
        repeat(512) {
            val before = state(model)
            val moved = if (forward) model.stepRight() else model.stepLeft()
            if (!moved) return result
            val after = state(model)
            assertNotEquals("A reported move must advance the semantic cursor", before, after)
            assertFalse("A directional traversal cannot loop", after in result)
            assertNull("Navigation never creates a mathematical selection", model.selectionRange)
            result += after
        }
        error("Navigation did not reach its endpoint")
    }

    @Test fun percentageNavigationTraversesBothEndsEveryOperatorAndEachNumberCharacter() {
        val tree = parse("50×10%")
        val model = MathExpressionModel(tree)
        val left = traverse(model, forward = false)
        assertEquals(MathCursor(offset = 0), model.cursor)
        assertTrue(left.any { it.cursor.leafTextOffset == 1 && it.cursor.offset == 0 })
        assertTrue(left.any { it.cursor.leafTextOffset == 1 && it.cursor.offset == 2 })
        assertTrue(left.any { it.presentation == MathCursorPresentation.ParentBoundary(emptyList(), 1) })
        assertTrue(left.any { it.presentation == MathCursorPresentation.ParentBoundary(emptyList(), 3) })
        val right = traverse(model, forward = true)
        assertEquals(MathCursor(offset = tree.children.size), model.cursor)
        assertEquals("RIGHT must retrace exactly the same states as LEFT", left.reversed(), right)
        assertSame(tree, model.expression)
    }

    @Test fun incompleteAndNestedOperatorsNeverBlockNavigationOrMutateTheirDrafts() {
        for (source in listOf("nCr", "5nCr", "nCr2", "5nPr", "%5", "!", "5!%", "1+nPr",
            "5nCr2+4!", "\\frac{50×10%}{5nCr2}", "\\sqrt[3]{(5nPr2)!}", "{5!}^{2}+|50%|")) {
            val tree = parse(source)
            val model = MathExpressionModel(tree)
            traverse(model, forward = false)
            assertEquals("LEFT reaches the root's real beginning: $source", MathCursor(offset = 0), model.cursor)
            traverse(model, forward = true)
            assertEquals("RIGHT reaches the root's real end: $source", MathCursor(offset = tree.children.size), model.cursor)
            assertSame("Navigation preserves complete and incomplete trees: $source", tree, model.expression)
        }
    }

    @Test fun largeOperatorContinuationRetainsItsOwnedBorderInBothDirections() {
        for (source in listOf("\\int_{0}^{5nPr}50×10%", "\\sum_{i=0}^{5nCr}50×10%",
            "\\frac{\\int_{0}^{5nPr}50×10%}{2}")) {
            val tree = parse(source)
            val model = MathExpressionModel(tree)
            val left = traverse(model, forward = false)
            assertTrue("The continuation keeps the border of its owning large operator: $source",
                left.any { (it.presentation as? MathCursorPresentation.ParentBoundary)?.includePrevious == true })
            val right = traverse(model, forward = true)
            assertEquals("The owned border must be visited in both directions: $source", left.reversed(), right)
            assertSame(tree, model.expression)
        }
    }

    @Test fun newlyInsertedOperatorsAreDeletedAtomicallyAndBackspaceContinuesInTheNumber() {
        for (operator in listOf("nCr", "nPr", "%", "!", "×", "≤")) {
            val model = MathExpressionModel()
            assertTrue(model.insertText("50"))
            assertTrue(model.insertText(operator))
            assertNull("An operator cannot have a cursor inside its letters", model.cursor.leafTextOffset)
            model.deleteBackward()
            assertEquals("One Backspace removes the whole operator: $operator", parse("50"), model.expression)
            model.deleteBackward()
            assertEquals("Backspace continues without retaining an empty operator scope", parse("5"), model.expression)
            model.deleteBackward()
            assertEquals(MathExpression.Sequence(), model.expression)
            repeat(3) { model.deleteBackward() }
            assertTrue(model.insertText("3"))
            assertEquals(parse("3"), model.expression)
        }
        val chained = MathExpressionModel()
        for (part in listOf("5", "%", "!")) assertTrue(chained.insertText(part))
        chained.deleteBackward()
        assertEquals(parse("5%"), chained.expression)
        chained.deleteBackward()
        assertEquals(parse("5"), chained.expression)
    }

    @Test fun operatorBordersCanBeDeletedAndRestoreTheirCursorWithoutPartialOperatorText() {
        for (operator in listOf("nCr", "nPr", "%", "!")) {
            val model = MathExpressionModel(parse("5$operator"))
            assertTrue(model.stepLeft())
            assertEquals(MathCursorPresentation.ParentBoundary(emptyList(), 1), model.cursorPresentation)
            val restored = MathExpressionModel()
            assertTrue(restored.restoreDraft(model.saveDraft()))
            assertEquals(state(model), state(restored))
            restored.deleteBackward()
            assertEquals(parse("5"), restored.expression)
            assertTrue(restored.insertText("+2"))
            assertEquals(parse("5+2"), restored.expression)
        }
    }

    @Test fun nativeDeletionAndReplacementCannotSplitSemanticOperators() {
        for (operator in listOf("nCr", "nPr")) {
            val model = MathExpressionModel(parse("5${operator}2"))
            assertTrue(model.deleteTextRangeInCurrentSlot(2, 3))
            assertEquals(listOf(MathExpression.Number("5"), MathExpression.Number("2")), model.expression.children)
            val replace = MathExpressionModel(parse("5${operator}2"))
            assertTrue(replace.replaceTextRangeInCurrentSlot(2, 3, "+"))
            assertEquals(parse("5+2"), replace.expression)
            val touch = MathExpressionModel(parse("5${operator}2"))
            assertTrue(touch.placeTextCursor(MathCursor(offset = 1), leafTextOffset = 1))
            assertEquals(2, touch.cursor.offset)
            assertNull(touch.cursor.leafTextOffset)
            touch.deleteBackward()
            assertEquals(listOf(MathExpression.Number("5"), MathExpression.Number("2")), touch.expression.children)
        }
    }

    @Test fun incompleteOperatorsInsideRequiredSlotsDeleteWithoutDestroyingTheirOwners() {
        for (operator in listOf("nCr", "nPr", "%", "!")) {
            val model = MathExpressionModel(parse("\\frac{5$operator}{2}"))
            val path = listOf(MathCursorStep(0, "numerator"))
            model.cursor = MathCursor(path, 2)
            model.deleteBackward()
            assertEquals(parse("\\frac{5}{2}"), model.expression)
            assertEquals(path, model.cursor.path)
            model.deleteBackward()
            assertEquals(parse("\\frac{}{2}"), model.expression)
            repeat(3) { model.deleteBackward() }
            assertEquals(parse("\\frac{}{2}"), model.expression)
            assertTrue(model.insertText("3"))
            assertEquals(parse("\\frac{3}{2}"), model.expression)
        }
    }
}
