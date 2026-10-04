package com.learngraphicsprogramming.app.math

import org.junit.Assert.*
import org.junit.Test

class MathAtomicParentInsertionTest {
    private fun parse(source: String) = (MathLatexParser.parse(source) as MathLatexParser.Result.Success).expression
    private fun model(source: String = "") = MathExpressionModel(parse(source))
    private fun letters(value: String) = value.map { MathExpression.Variable(it.toString()) }
    private fun lastAtom(m: MathExpressionModel, index: Int, path: List<MathCursorStep> = emptyList()) {
        assertEquals(MathCursor(path, index + 1, MathCursorLevel.INTERIOR, MathContentScope.Atom(index, index + 1)), m.cursor)
        assertEquals(MathContentScope.Atom(index, index + 1), m.editingScope)
        assertNull(m.selectionRange)
    }
    private fun selectLast(m: MathExpressionModel, index: Int, path: List<MathCursorStep> = emptyList()) {
        val published = m.expression
        assertTrue(m.navigatePrevious())
        assertSame(published, m.expression)
        assertEquals(MathSelection(path, index, index + 1), m.selectionRange)
    }

    @Test fun batchAbcPublishesThreeParentsAndImmediateLeftSelectsOnlyC() {
        val m = model("x^2")
        val existing = m.expression.children.single()
        assertTrue(m.insertText("abc"))
        assertEquals(listOf(existing) + letters("abc"), m.expression.children)
        assertSame(existing, m.expression.children.first())
        lastAtom(m, 3)
        selectLast(m, 3)
        assertTrue(m.navigatePrevious())
        assertNull(m.selectionRange)
        assertEquals(3, m.cursor.offset)
        assertTrue(m.navigatePrevious())
        assertEquals(MathCursorPresentation.ParentBoundary(emptyList(), 2), m.cursorPresentation)
    }

    @Test fun eachFastCharacterCallPublishesExactlyOneNewSiblingAndItsOwnCursor() {
        val m = model("x^2")
        "abc".forEachIndexed { index, char ->
            val before = m.expression
            assertTrue(m.insertText(char.toString()))
            val published = m.expression
            assertNotSame(before, published)
            assertEquals(before.children + MathExpression.Variable(char.toString()), published.children)
            before.children.indices.forEach { assertSame(before.children[it], published.children[it]) }
            lastAtom(m, index + 1)
            repeat(3) { assertTrue(m.canNavigatePrevious); assertSame(published, m.expression) }
        }
        selectLast(m, 3)
    }

    @Test fun nativeCollapsedAppendsKeepEachLetterIndependentImmediately() {
        val m = MathExpressionModel()
        "abc".forEachIndexed { index, char ->
            val before = m.expression
            assertTrue(m.replaceTextRangeInCurrentSlot(index, index, char.toString()))
            assertEquals(before.children + MathExpression.Variable(char.toString()), m.expression.children)
            lastAtom(m, index)
        }
        selectLast(m, 2)
    }

    @Test fun cumulativeNativeWordReplacementOwnsTextPrivatelyWithoutExpandingNavigationScope() {
        val m = model("x^2")
        val parent = m.expression.children.single()
        var previousLength = 0
        listOf("a", "ab", "abc").forEach { word ->
            assertTrue(m.replaceTextRangeInCurrentSlot(1, 1 + previousLength, word))
            assertEquals(listOf(parent) + letters(word), m.expression.children)
            assertSame(parent, m.expression.children.first())
            lastAtom(m, word.length)
            previousLength = word.length
        }
        selectLast(m, 3)
    }

    @Test fun nativePrefixReplacementOutsideTheLastAtomKeepsTheUntouchedSuffix() {
        val m = MathExpressionModel()
        assertTrue(m.insertText("abc"))
        val suffix = m.expression.children.last()
        assertTrue(m.replaceTextRangeInCurrentSlot(0, 2, "xy"))
        assertEquals(letters("xyc"), m.expression.children)
        assertSame(suffix, m.expression.children.last())
        lastAtom(m, 1)
        selectLast(m, 1)
    }

    @Test fun replacingASelectedLetterActivatesOnlyTheLastNewLetter() {
        val m = MathExpressionModel()
        assertTrue(m.insertText("abc"))
        selectLast(m, 2)
        assertTrue(m.replaceTextRangeInCurrentSlot(2, 3, "yz"))
        assertEquals(letters("abyz"), m.expression.children)
        lastAtom(m, 3)
        selectLast(m, 3)
    }

    @Test fun aNeutralLocalSequenceGetsIndependentParentsWhileAnExplicitSlotRetainsItsWholeScope() {
        val path = listOf(MathCursorStep(0, "numerator"))
        val neutral = MathExpressionModel(MathExpression.Sequence(listOf(MathExpression.Fraction(
            MathExpression.Sequence(listOf(MathExpression.Root(parse("1")), MathExpression.Operator("+"))), parse("2")))))
        neutral.cursor = MathCursor(path, 2, MathCursorLevel.NEUTRAL, null)
        assertTrue(neutral.insertText("abc"))
        assertEquals(letters("abc"), neutral.sequenceAt(path)!!.children.takeLast(3))
        lastAtom(neutral, 4, path)
        selectLast(neutral, 4, path)

        val slot = model("\\frac{}{2}")
        slot.cursor = MathCursor(path, 0)
        assertTrue(slot.insertText("abc"))
        assertEquals(MathContentScope.Slot, slot.editingScope)
        assertEquals(letters("abc"), slot.sequenceAt(path)!!.children)
        assertTrue(slot.navigatePrevious())
        assertEquals(MathSelection(path, 0, 3), slot.selectionRange)
    }

    @Test fun nativeReplacementInsideARequiredSlotKeepsTheIntentionalSlotScope() {
        val m = model("\\frac{}{2}")
        val path = listOf(MathCursorStep(0, "numerator"))
        m.cursor = MathCursor(path, 0)
        assertTrue(m.replaceTextRangeInCurrentSlot(0, 0, "abc"))
        assertEquals(MathContentScope.Slot, m.editingScope)
        assertTrue(m.replaceTextRangeInCurrentSlot(0, 3, "xyz"))
        assertEquals(letters("xyz"), m.sequenceAt(path)!!.children)
        assertEquals(MathContentScope.Slot, m.editingScope)
        assertTrue(m.navigatePrevious())
        assertEquals(MathSelection(path, 0, 3), m.selectionRange)
    }

    @Test fun normalSymbolsAndOperatorsAlsoActivateOnlyTheirOwnParent() {
        val m = MathExpressionModel()
        listOf<MathExpression>(MathExpression.Variable("a"), MathExpression.Symbol("π"), MathExpression.Operator("+"))
            .forEachIndexed { index, node -> assertTrue(m.insert(node)); lastAtom(m, index) }
        selectLast(m, 2)
    }

    @Test fun numericAppendsKeepOneNumberAndPartialNumericReplacementKeepsItsSuffixScope() {
        val number = MathExpressionModel()
        assertTrue(number.insertText("1"))
        assertTrue(number.replaceTextRangeInCurrentSlot(1, 1, "2"))
        assertEquals(listOf(MathExpression.Number("12")), number.expression.children)
        lastAtom(number, 0)
        selectLast(number, 0)

        val partial = model("12")
        assertTrue(partial.placeTextCursor(MathCursor(), 0))
        assertTrue(partial.replaceTextRangeInCurrentSlot(0, 1, "3"))
        assertEquals(listOf(MathExpression.Number("3"), MathExpression.Number("2")), partial.expression.children)
        assertEquals(MathContentScope.Atom(0, 2), partial.editingScope)
        assertEquals(1, partial.cursor.offset)
    }

    @Test fun newScopesSurviveDraftsAndUnsupportedReplacementIsAtomic() {
        val m = MathExpressionModel()
        assertTrue(m.insertText("abc"))
        val before = m.saveDraft()
        val published = m.expression
        assertFalse(m.replaceTextRangeInCurrentSlot(0, 3, "\\unsupported{x}"))
        assertSame(published, m.expression)
        assertEquals(before, m.saveDraft())
        val restored = MathExpressionModel()
        assertTrue(restored.restoreDraft(before))
        lastAtom(restored, 2)
        selectLast(restored, 2)
    }

    @Test fun aLegacyPersistedWordScopeRestoresItsLastIndependentParentBeforeLeft() {
        val legacy = model("abc")
        legacy.cursor = MathCursor(offset = 3, level = MathCursorLevel.INTERIOR, scope = MathContentScope.Atom(0, 3))
        val restored = MathExpressionModel()
        assertTrue(restored.restoreDraft(legacy.saveDraft()))
        assertEquals(legacy.expression, restored.expression)
        lastAtom(restored, 2)
        selectLast(restored, 2)
    }

    @Test fun legacyLetterScopeMigrationKeepsTheExactRealCaretAtEveryBoundary() {
        for (offset in 0..3) {
            val legacy = model("abc")
            legacy.cursor = MathCursor(offset = offset, level = MathCursorLevel.INTERIOR, scope = MathContentScope.Atom(0, 3))
            val restored = MathExpressionModel()
            assertTrue(restored.restoreDraft(legacy.saveDraft()))
            assertEquals(legacy.expression, restored.expression)
            assertEquals(legacy.cursor.path, restored.cursor.path)
            assertEquals(offset, restored.cursor.offset)
            assertEquals(MathContentScope.Atom((offset - 1).coerceAtLeast(0), offset.coerceAtLeast(1)), restored.editingScope)
        }
    }

    @Test fun aGenuineSelectionInAnOldWordDraftIsPreservedAsARealMathSelection() {
        val legacy = model("abc")
        legacy.cursor = MathCursor(offset = 3, level = MathCursorLevel.INTERIOR, scope = MathContentScope.Atom(0, 3))
        legacy.select(emptyList(), 0, 3)
        val restored = MathExpressionModel()
        assertTrue(restored.restoreDraft(legacy.saveDraft()))
        assertEquals(legacy.expression, restored.expression)
        assertEquals(legacy.cursor, restored.cursor)
        assertEquals(MathSelection(emptyList(), 0, 3), restored.selectionRange)
        assertTrue(restored.navigatePrevious())
        assertNull(restored.selectionRange)
        assertEquals(0, restored.cursor.offset)
    }

    @Test fun numericSplitSymbolSplitAndDeliberateSlotScopesRemainLegalAcrossDraftRestore() {
        val numeric = model("12")
        assertTrue(numeric.placeTextCursor(MathCursor(), 0))
        assertTrue(numeric.replaceTextRangeInCurrentSlot(0, 1, "3"))
        assertEquals(MathContentScope.Atom(0, 2), numeric.editingScope)

        val symbol = model("\\mathrm{kg}")
        assertTrue(symbol.placeTextCursor(MathCursor(), 1))
        assertEquals(listOf(MathExpression.Symbol("k"), MathExpression.Symbol("g")), symbol.expression.children)
        assertEquals(MathContentScope.Atom(0, 2), symbol.editingScope)

        val slot = model("\\frac{abc}{2}")
        slot.cursor = MathCursor(listOf(MathCursorStep(0, "numerator")), 3)
        assertEquals(MathContentScope.Slot, slot.editingScope)

        for (original in listOf(numeric, symbol, slot)) {
            val restored = MathExpressionModel()
            assertTrue(restored.restoreDraft(original.saveDraft()))
            assertEquals(original.expression, restored.expression)
            assertEquals(original.cursor, restored.cursor)
            assertEquals(original.editingScope, restored.editingScope)
        }
    }
}
