package com.learngraphicsprogramming.app.math

import org.junit.Assert.*
import org.junit.Test

class MathImeSlotDeletionTest {
    private fun parse(source: String) = (MathLatexParser.parse(source) as MathLatexParser.Result.Success).expression
    private fun model(source: String, slot: String, offset: Int): MathExpressionModel = MathExpressionModel(parse(source)).apply {
        cursor = MathCursor(listOf(MathCursorStep(0, slot)), offset)
    }
    private fun latex(model: MathExpressionModel) = MathLatexSerializer.serialize(model.expression)
    private fun scripts(model: MathExpressionModel) = model.expression.children.single() as MathExpression.Scripts

    @Test fun deletingTheLastSubscriptCharacterLeavesTheSlotValidAndRepeatedBackspaceIsHarmless() {
        val model = model("x_1", "lower", 1)
        val path = model.cursor.path
        assertTrue(model.deleteSurroundingTextInCurrentSlot(1, 0))
        assertEquals("{x}_{}", latex(model))
        assertEquals(MathExpression.Sequence(), scripts(model).lower)
        assertEquals(MathCursor(path, 0), model.cursor)
        val emptyDraft = model.saveDraft()
        repeat(15) {
            assertTrue(model.deleteSurroundingTextInCurrentSlot(1, 0))
            assertTrue(model.deleteSurroundingTextInCurrentSlot(0, 1))
        }
        assertEquals(emptyDraft, model.saveDraft())
        assertTrue(model.insertText("3"))
        assertEquals("{x}_{3}", latex(model))
    }

    @Test fun aMergedNumericSubscriptLosesOneCharacterInsteadOfItsWholeAtom() {
        val model = model("x_{12}", "lower", 1)
        assertEquals(listOf(MathExpression.Number("12")), scripts(model).lower!!.children)
        assertTrue(model.deleteSurroundingTextInCurrentSlot(1, 0))
        assertEquals(listOf(MathExpression.Number("1")), scripts(model).lower!!.children)
        assertEquals("{x}_{1}", latex(model))
        assertEquals("lower", model.cursor.path.last().slot)
    }

    @Test fun deletingSubscriptContentNeverConsumesTheExponentOrBase() {
        val model = model("x_{12}^{23}", "lower", 1)
        val upper = scripts(model).upper
        val base = scripts(model).base
        assertTrue(model.deleteSurroundingTextInCurrentSlot(1, 0))
        assertEquals("{x}_{1}^{23}", latex(model))
        assertTrue(model.deleteSurroundingTextInCurrentSlot(Int.MAX_VALUE, Int.MAX_VALUE))
        assertEquals("{x}_{}^{23}", latex(model))
        assertEquals(upper, scripts(model).upper)
        assertEquals(base, scripts(model).base)
        assertEquals("lower", model.cursor.path.last().slot)
    }

    @Test fun genuineSelectionDeletesOnlyTheSelectedContentOnce() {
        val model = model("x_{12+34}^{2}", "lower", 3)
        model.select(model.cursor.path, 0, 1)
        assertTrue(model.deleteSurroundingTextInCurrentSlot(100, 100))
        assertNull(model.selectionRange)
        assertEquals("{x}_{+34}^{2}", latex(model))
        assertEquals(MathCursor(listOf(MathCursorStep(0, "lower")), 0), model.cursor)
        val entireNumber = this.model("x_{12}^{2}", "lower", 1)
        entireNumber.select(entireNumber.cursor.path, 0, 1)
        assertTrue(entireNumber.deleteSurroundingTextInCurrentSlot(1, 0))
        assertEquals("{x}_{}^{2}", latex(entireNumber))
        assertNotNull(scripts(entireNumber).lower)
    }

    @Test fun forwardDeletionStaysInsideTheActiveSlotAndPreservesItsCursor() {
        val model = model("x_{123}^{4}", "lower", 0)
        val cursor = model.cursor
        assertTrue(model.deleteSurroundingTextInCurrentSlot(0, 1))
        assertEquals("{x}_{23}^{4}", latex(model))
        assertEquals(cursor, model.cursor)
        assertTrue(model.deleteSurroundingTextInCurrentSlot(0, 100))
        assertEquals("{x}_{}^{4}", latex(model))
        assertEquals(cursor, model.cursor)
        assertTrue(model.deleteSurroundingTextInCurrentSlot(0, 1))
        assertEquals("{x}_{}^{4}", latex(model))
    }

    @Test fun backwardAndForwardLengthsRemoveUtf16TextAroundTheCursorRatherThanWholeNodes() {
        val model = model("x_{12+34}^{5}", "lower", 2)
        assertTrue(model.deleteSurroundingTextInCurrentSlot(1, 1))
        assertEquals("{x}_{124}^{5}", latex(model))
        assertEquals(listOf(MathExpression.Number("12"), MathExpression.Number("4")), scripts(model).lower!!.children)
        assertEquals(MathCursor(listOf(MathCursorStep(0, "lower")), 1), model.cursor)
        assertTrue(model.insertText("9"))
        assertEquals("{x}_{1294}^{5}", latex(model))
    }

    @Test fun composingReplacementCanUpdatePartOfAMergedNumberWithoutLosingTheRest() {
        val model = model("x_{123}", "lower", 1)
        assertTrue(model.replaceTextRangeInCurrentSlot(1, 2, "9"))
        assertEquals("{x}_{193}", latex(model))
        assertEquals(MathCursor(listOf(MathCursorStep(0, "lower")), 1), model.cursor)
        assertEquals(listOf(MathExpression.Number("19"), MathExpression.Number("3")), scripts(model).lower!!.children)
        assertTrue(model.deleteSurroundingTextInCurrentSlot(1, 0))
        assertEquals("{x}_{13}", latex(model))
    }

    @Test fun replacingTheWholeNumericComposingRangeDoesNotAppendOrDuplicatePreviousText() {
        val model = model("x_{12}^{2}", "lower", 1)
        assertTrue(model.replaceTextRangeInCurrentSlot(0, 2, "1"))
        assertEquals("{x}_{1}^{2}", latex(model))
        assertTrue(model.replaceTextRangeInCurrentSlot(0, 1, "13"))
        assertEquals("{x}_{13}^{2}", latex(model))
        assertTrue(model.replaceTextRangeInCurrentSlot(0, 2, ""))
        assertEquals("{x}_{}^{2}", latex(model))
        assertEquals("lower", model.cursor.path.last().slot)
    }

    @Test fun utf16AndCodePointDeletionNeverLeaveAHalfSurrogateOrCrossTheSlotBoundary() {
        val supplementary = "\uD835\uDC65"
        fun unicodeModel(offset: Int) = MathExpressionModel(MathExpression.Sequence(listOf(
            MathExpression.Scripts(parse("x"), lower = MathExpression.Sequence(listOf(
                MathExpression.Variable("a"), MathExpression.Symbol(supplementary), MathExpression.Variable("b"))))
        ))).apply { cursor = MathCursor(listOf(MathCursorStep(0, "lower")), offset) }
        val utf16 = unicodeModel(2)
        assertTrue(utf16.deleteSurroundingTextInCurrentSlot(1, 0))
        assertEquals(parse("ab"), scripts(utf16).lower)
        val codePoints = unicodeModel(2)
        assertTrue(codePoints.deleteSurroundingTextInCurrentSlot(1, 0, inCodePoints = true))
        assertEquals(parse("ab"), scripts(codePoints).lower)
        val forward = unicodeModel(1)
        assertTrue(forward.deleteSurroundingTextInCurrentSlot(0, 1, inCodePoints = true))
        assertEquals(parse("ab"), scripts(forward).lower)
        assertEquals(MathCursor(listOf(MathCursorStep(0, "lower")), 1), forward.cursor)
    }

    @Test fun implicitImeDeletionProtectsStructuralObjectsButExplicitMathematicalSelectionCanRemoveThem() {
        val model = model("x_{\\frac{1}{2}}", "lower", 1)
        val original = model.saveDraft()
        assertTrue(model.deleteSurroundingTextInCurrentSlot(1, 0))
        assertEquals(original, model.saveDraft())
        model.select(model.cursor.path, 0, 1)
        assertTrue(model.deleteSurroundingTextInCurrentSlot(1, 0))
        assertEquals("{x}_{}", latex(model))
        assertNotNull(scripts(model).lower)
    }

    @Test fun anEmptyRequiredSlotAlsoSurvivesImeBackspaceWithoutDeletingTheParent() {
        val model = model("\\frac{x}{}", "denominator", 0)
        val original = model.saveDraft()
        repeat(5) { assertTrue(model.deleteSurroundingTextInCurrentSlot(3, 3)) }
        assertEquals(original, model.saveDraft())
        assertTrue(model.insertText("2"))
        assertEquals("\\frac{x}{2}", latex(model))
    }

    @Test fun failedTextReplacementRollsBackTreeCursorAndGenuineSelection() {
        val model = model("x_{12}^{2}", "lower", 1)
        val original = model.saveDraft()
        assertFalse(model.replaceTextRangeInCurrentSlot(0, 2, "\\unknown{x}"))
        assertEquals(original, model.saveDraft())
        model.select(model.cursor.path, 0, 1)
        val selected = model.saveDraft()
        assertFalse(model.replaceTextRangeInCurrentSlot(0, 2, "a".repeat(5_000)))
        assertEquals(selected, model.saveDraft())
        assertFalse(model.deleteSurroundingTextInCurrentSlot(-1, 0))
        assertFalse(model.replaceTextRangeInCurrentSlot(2, 1, "9"))
        assertEquals(selected, model.saveDraft())
    }

    @Test fun aDraftAfterNumericDeletionRestoresTheSameEditableEmptySlot() {
        val original = model("x_{12}^{2}", "lower", 1)
        assertTrue(original.deleteSurroundingTextInCurrentSlot(2, 0))
        val restored = MathExpressionModel()
        assertTrue(restored.restoreDraft(original.saveDraft()))
        assertEquals(original.cursor, restored.cursor)
        assertEquals(original.expression, restored.expression)
        assertTrue(restored.deleteSurroundingTextInCurrentSlot(1, 0))
        assertTrue(restored.insertText("4"))
        assertEquals("{x}_{4}^{2}", latex(restored))
    }

    @Test fun deletingAroundANativeMidNumberCursorPreservesTheOppositeSide() {
        val backward = model("x_{123}^{2}", "lower", 1)
        assertTrue(backward.setTextCursorInCurrentSlot(1))
        assertTrue(backward.deleteSurroundingTextInCurrentSlot(1, 0))
        assertEquals("{x}_{23}^{2}", latex(backward))
        assertEquals(MathCursor(listOf(MathCursorStep(0, "lower")), 0), backward.cursor)
        val forward = model("x_{123}^{2}", "lower", 1)
        assertTrue(forward.setTextCursorInCurrentSlot(1))
        assertTrue(forward.deleteSurroundingTextInCurrentSlot(0, 1))
        assertEquals("{x}_{13}^{2}", latex(forward))
        assertEquals(MathCursor(listOf(MathCursorStep(0, "lower")), 1), forward.cursor)
    }

    @Test fun placingANativeCursorNeverSplitsASurrogatePair() {
        val supplementary = "\uD835\uDC65"
        val model = MathExpressionModel(MathExpression.Sequence(listOf(MathExpression.Scripts(parse("x"),
            lower = MathExpression.Sequence(listOf(MathExpression.Symbol(supplementary), MathExpression.Number("12")))))))
        model.cursor = MathCursor(listOf(MathCursorStep(0, "lower")), 0)
        assertTrue(model.setTextCursorInCurrentSlot(1))
        assertEquals(MathCursor(listOf(MathCursorStep(0, "lower")), 1), model.cursor)
        assertEquals(MathExpression.Symbol(supplementary), scripts(model).lower!!.children.first())
        assertTrue(model.deleteSurroundingTextInCurrentSlot(1, 0, inCodePoints = true))
        assertEquals(parse("12"), scripts(model).lower)
        assertEquals(MathCursor(listOf(MathCursorStep(0, "lower")), 0), model.cursor)
    }

    @Test fun disjointSurroundingDeletionPreservesTheNativeSelectedTextBetweenBothIntervals() {
        val model = model("x_{1234}^{2}", "lower", 1)
        assertTrue(model.setTextCursorInCurrentSlot(3))
        assertTrue(model.deleteTextRangesInCurrentSlot(listOf(3 to 4, 0 to 1)))
        assertEquals("{x}_{23}^{2}", latex(model))
        assertEquals(listOf(MathExpression.Number("23")), scripts(model).lower!!.children)
        assertEquals(MathCursor(listOf(MathCursorStep(0, "lower")), 1), model.cursor)
        assertTrue(model.insertText("9"))
        assertEquals("{x}_{239}^{2}", latex(model))
    }

    @Test fun disjointDeletionPreservesStructuralObjectsBetweenAndInsideTheIntervals() {
        val model = model("x_{1\\frac{3}{4}2}^{5}", "lower", 3)
        val upper = scripts(model).upper
        val fraction = scripts(model).lower!!.children[1]
        assertTrue(model.deleteTextRangesInCurrentSlot(listOf(0 to 1, 2 to 3)))
        assertEquals("{x}_{\\frac{3}{4}}^{5}", latex(model))
        assertSame(fraction, scripts(model).lower!!.children.single())
        assertSame(upper, scripts(model).upper)
        assertEquals(MathCursor(listOf(MathCursorStep(0, "lower")), 1), model.cursor)
        val original = model.saveDraft()
        assertTrue(model.deleteTextRangesInCurrentSlot(listOf(0 to 1, 0 to 1)))
        assertEquals(original, model.saveDraft())
    }

    @Test fun disjointUtf16DeletionNormalizesSurrogateBoundariesAndMergesOverlaps() {
        val supplementary = "\uD835\uDC65"
        val model = MathExpressionModel(MathExpression.Sequence(listOf(MathExpression.Scripts(parse("x"),
            lower = MathExpression.Sequence(listOf(MathExpression.Number("1"), MathExpression.Symbol(supplementary), MathExpression.Number("23")))))))
        model.cursor = MathCursor(listOf(MathCursorStep(0, "lower")), 3)
        assertTrue(model.deleteTextRangesInCurrentSlot(listOf(2 to 3, 1 to 2, 4 to 5)))
        assertEquals("{x}_{12}", latex(model))
        assertEquals(MathCursor(listOf(MathCursorStep(0, "lower")), 2), model.cursor)
    }

    @Test fun codePointDeletionRejectsMalformedSurrogatesOnlyInsideTheRequestedRange() {
        listOf("\uD835", "\uDC65").forEach { malformed ->
            val model = MathExpressionModel(MathExpression.Sequence(listOf(MathExpression.Scripts(parse("x"),
                lower = MathExpression.Sequence(listOf(MathExpression.Symbol(malformed), MathExpression.Number("1")))))))
            model.cursor = MathCursor(listOf(MathCursorStep(0, "lower")), 1)
            val original = model.saveDraft()
            assertFalse(model.deleteSurroundingTextInCurrentSlot(1, 0, inCodePoints = true))
            assertEquals(original, model.saveDraft())
            model.cursor = model.cursor.copy(offset = 0)
            val atStart = model.saveDraft()
            assertFalse(model.deleteSurroundingTextInCurrentSlot(0, 1, inCodePoints = true))
            assertEquals(atStart, model.saveDraft())
            model.cursor = model.cursor.copy(offset = 2)
            assertTrue(model.deleteSurroundingTextInCurrentSlot(1, 0, inCodePoints = true))
            assertEquals(listOf(MathExpression.Symbol(malformed)), scripts(model).lower!!.children)
        }
    }

    @Test fun committingAnOwnedFractionCompositionReplacesItsMarkerWithoutDuplication() {
        val model = model("x_{\\frac{1}{2}}", "lower", 1)
        assertTrue(model.replaceTextRangeInCurrentSlot(0, 1, "\\frac{1}{2}", replaceStructures = true))
        assertEquals("{x}_{\\frac{1}{2}}", latex(model))
        assertEquals(1, scripts(model).lower!!.children.size)
        assertTrue(scripts(model).lower!!.children.single() is MathExpression.Fraction)
        val original = model.saveDraft()
        assertFalse(model.replaceTextRangeInCurrentSlot(0, 1, "\\unknown{x}", replaceStructures = true))
        assertEquals(original, model.saveDraft())
    }

    @Test fun clearingAnOwnedStructuralCompositionLeavesItsContainingSlotEditable() {
        val model = model("x_{\\frac{1}{2}}^{3}", "lower", 1)
        assertTrue(model.replaceTextRangeInCurrentSlot(0, 1, "", replaceStructures = true))
        assertEquals("{x}_{}^{3}", latex(model))
        assertNotNull(scripts(model).lower)
        assertEquals(MathCursor(listOf(MathCursorStep(0, "lower")), 0), model.cursor)
        assertTrue(model.insertText("4"))
        assertEquals("{x}_{4}^{3}", latex(model))
    }

    @Test fun ownedReplacementDoesNotConsumeSurroundingStructuresAndProtectionRemainsTheDefault() {
        val model = model("x_{\\sqrt{3}+\\frac{1}{2}+\\sqrt{4}}", "lower", 5)
        val left = scripts(model).lower!!.children.first()
        val right = scripts(model).lower!!.children.last()
        assertTrue(model.replaceTextRangeInCurrentSlot(2, 3, "\\frac{5}{6}", replaceStructures = true))
        assertEquals("{x}_{\\sqrt{3}+\\frac{5}{6}+\\sqrt{4}}", latex(model))
        assertSame(left, scripts(model).lower!!.children.first())
        assertSame(right, scripts(model).lower!!.children.last())
        val original = model.saveDraft()
        assertTrue(model.replaceTextRangeInCurrentSlot(2, 3, ""))
        assertEquals(original, model.saveDraft())
    }
}
