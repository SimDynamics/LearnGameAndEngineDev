package com.learngraphicsprogramming.app.math

import android.os.Build
import android.os.SystemClock
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputConnection
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

/** Native events follow the model's Sequence, child-boundary and content levels. */
@RunWith(AndroidJUnit4::class)
class MathStructuralNavigationDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private enum class Transport { TEXT, CODE_POINTS, KEY, TOOLBAR }

    @Test fun neutralBackspaceDeletesThePreviousDirectGroupWholeWithoutEnteringItsSlots() = onMain {
        for (transport in Transport.entries) {
            val editor = neutralEditor("x_{1}^{2}")
            assertEquals(MathCursorLevel.NEUTRAL, editor.model.navigationLevel)
            delete(editor, transport)
            assertEquals("", editor.getLatex())
            assertEquals(MathCursor(), editor.model.cursor)
            assertNull(editor.model.selectionRange)
        }
    }

    @Test fun neutralNumberIsAnObjectToTheImeAndBackspaceDeletesTheEntireGroup() = onMain {
        for (transport in Transport.entries) {
            val editor = neutralEditor("12")
            val input = connection(editor)
            assertEquals("\uFFFC", input.getTextBeforeCursor(20, 0).toString())
            assertFalse("A neutral group is not a predictive word", input.setComposingRegion(0, 1))
            assertNull(editor.model.selectionRange)
            delete(editor, transport)
            assertEquals("Neutral deletion removes Number(12) whole", "", editor.getLatex())
        }
    }

    @Test fun neutralBackspaceTreatsIntegralAndPowerAsIndependentDirectSiblings() = onMain {
        for (transport in Transport.entries) {
            val editor = integralAndPower()
            val integral = editor.model.expression.children.first()
            delete(editor, transport)
            assertEquals(MathExpression.Sequence(listOf(integral)), editor.model.expression)
            assertEquals(MathCursor(offset = 1), editor.model.cursor)
            delete(editor, transport)
            assertTrue(editor.model.expression.children.isEmpty())
            assertEquals(MathCursor(), editor.model.cursor)
        }
    }

    @Test fun leftVisitsThePowerParentAndChildBeforeTheIndependentIntegral() = onMain {
        val editor = integralAndPower()
        val original = editor.model.expression
        editor.perform(MathKeyboardAction.PreviousSlot)
        assertEquals(MathCursor(listOf(MathCursorStep(1, "base")), 1), editor.model.cursor)
        editor.perform(MathKeyboardAction.NextSlot)
        assertEquals(MathCursorPresentation.ParentBoundary(emptyList(), 1), editor.model.cursorPresentation)
        assertSame(original, editor.model.expression)
        editor.perform(MathKeyboardAction.MoveLeft)
        assertEquals(MathCursor(listOf(MathCursorStep(1, "base")), 1), editor.model.cursor)
        editor.perform(MathKeyboardAction.PreviousSlot)
        assertEquals(MathSelection(listOf(MathCursorStep(1, "base")), 0, 1), editor.model.selectionRange)
        editor.perform(MathKeyboardAction.PreviousSlot)
        assertEquals(MathCursor(listOf(MathCursorStep(1, "base")), 0), editor.model.cursor)
        editor.perform(MathKeyboardAction.PreviousSlot)
        assertEquals(MathCursorPresentation.ChildBoundary(listOf(MathCursorStep(1, "upper")), true), editor.model.cursorPresentation)
        assertSame(original, editor.model.expression)
        var steps = 0
        while (editor.model.cursor != MathCursor(offset = 1) && steps++ < 24) {
            editor.perform(MathKeyboardAction.PreviousSlot)
            assertSame("Finishing the power's own children never edits the integral sibling", original, editor.model.expression)
        }
        assertEquals("The traversal exits x² into the parent Sequence before its previous sibling", MathCursor(offset = 1), editor.model.cursor)
        editor.perform(MathKeyboardAction.PreviousSlot)
        assertEquals("Only now does the previous independent Integral group become active",
            MathCursor(listOf(MathCursorStep(0, "body")), 0), editor.model.cursor)
        assertSame(original, editor.model.expression)
    }

    @Test fun onlyTheRightChildBoundaryRemovesTheSelectedOptionalScript() = onMain {
        for (name in listOf("lower", "upper")) for (transport in Transport.entries) {
            val editor = boundaryEditor("x_{1}^{2}+4", name, after = true)
            delete(editor, transport)
            assertEquals(canonical(if (name == "lower") "x^{2}+4" else "x_{1}+4"), editor.getLatex())
            assertEquals(MathCursorLevel.NEUTRAL, editor.model.navigationLevel)
            assertNull(editor.model.selectionRange)
        }
    }

    @Test fun onlyTheRightRequiredBoundaryClearsItsSlotAndRetainsTheOwnerAndSibling() = onMain {
        for (name in listOf("numerator", "denominator")) for (transport in Transport.entries) {
            val editor = boundaryEditor("\\frac{12}{34}+5", name, after = true)
            delete(editor, transport)
            assertEquals(if (name == "numerator") "\\frac{}{34}+5" else "\\frac{12}{}+5", editor.getLatex())
            assertNull(editor.model.selectionRange)
            assertNotNull(editor.model.sequenceAt(listOf(MathCursorStep(0, name))))
        }
    }

    @Test fun interiorDigitsDeleteOneAtATimeAndAllTransportsRetainAnEmptySlot() = onMain {
        for (transport in Transport.entries) {
            val editor = slotEditor("x^{12}", "upper", atEnd = true)
            delete(editor, transport)
            assertEquals(canonical("x^{1}"), editor.getLatex())
            delete(editor, transport)
            assertEquals(canonical("x^{}"), editor.getLatex())
            val empty = snapshot(editor)
            repeat(4) { delete(editor, transport) }
            assertEquals(empty, snapshot(editor))
            assertEquals(MathCursorLevel.INTERIOR, editor.model.navigationLevel)
            assertTrue(connection(editor).commitText("3", 1))
            assertEquals(canonical("x^{3}"), editor.getLatex())
        }
    }

    @Test fun explicitContentSelectionDeletesAndComposesWithoutBecomingAnImeSelection() = onMain {
        for (transport in Transport.entries) {
            val editor = slotEditor("x_{12}^{2}", "lower", atEnd = true)
            editor.perform(MathKeyboardAction.PreviousSlot)
            assertEquals(MathSelection(listOf(MathCursorStep(0, "lower")), 0, 1), editor.model.selectionRange)
            delete(editor, transport)
            assertEquals(canonical("x_{}^{2}"), editor.getLatex())
            assertNull(editor.model.selectionRange)
        }
        val editor = slotEditor("x_{12}^{2}", "lower", atEnd = true)
        editor.perform(MathKeyboardAction.PreviousSlot)
        val input = connection(editor)
        assertTrue(input.setComposingText("3", 1))
        assertTrue(input.setComposingText("3", 1))
        assertTrue(input.commitText("3", 1))
        assertEquals(canonical("x_{3}^{2}"), editor.getLatex())
        assertNull(editor.model.selectionRange)
    }

    @Test fun powerAndSubscriptImmediatelyEnterTheirNewChildInsteadOfRemainingInTheBase() = onMain {
        val power = MathExpressionEditor(context)
        assertTrue(connection(power).commitText("x", 1))
        power.perform(MathKeyboardAction.Power)
        assertEquals(MathCursor(listOf(MathCursorStep(0, "upper")), 0), power.model.cursor)
        assertEquals(MathCursorLevel.INTERIOR, power.model.navigationLevel)
        assertEquals(canonical("x^{}"), power.getLatex())

        val subscript = slotEditor("x^{2}", "base", atEnd = true)
        subscript.perform(MathKeyboardAction.Subscript)
        assertEquals(MathCursor(listOf(MathCursorStep(0, "lower")), 0), subscript.model.cursor)
        assertEquals(canonical("x_{}^{2}"), subscript.getLatex())
    }

    @Test fun insertingInsideASlotActivatesTheNewDeepestChildRecursively() = onMain {
        val actions = listOf(MathKeyboardAction.Fraction to "numerator", MathKeyboardAction.Root to "radicand",
            MathKeyboardAction.NthRoot to "index", MathKeyboardAction.Matrix to "r0c0")
        actions.forEach { (action, initial) ->
            val editor = slotEditor("x^{2}", "upper", atEnd = true)
            editor.perform(action)
            val upper = listOf(MathCursorStep(0, "upper"))
            assertEquals(upper + MathCursorStep(1, initial), editor.model.cursor.path)
            assertEquals(0, editor.model.cursor.offset)
            assertEquals(MathCursorLevel.INTERIOR, editor.model.navigationLevel)
            assertTrue(connection(editor).commitText("3", 1))
            assertTrue(requireNotNull(editor.model.sequenceAt(editor.model.cursor.path)).children.isNotEmpty())
        }
        val power = slotEditor("x^{2}", "upper", atEnd = true)
        power.perform(MathKeyboardAction.Power)
        assertEquals(listOf(MathCursorStep(0, "upper"), MathCursorStep(0, "upper")), power.model.cursor.path)
        assertEquals(canonical("x^{{2}^{}}"), power.getLatex())
    }

    @Test fun aStructuralInsertionAtNeutralCreatesANewSiblingAndEntersItsInitialSlot() = onMain {
        val editor = neutralEditor("x^{2}")
        val existing = editor.model.expression.children.single()
        editor.perform(MathKeyboardAction.Fraction)
        assertEquals(2, editor.model.expression.children.size)
        assertEquals(existing, editor.model.expression.children.first())
        assertEquals(MathCursor(listOf(MathCursorStep(1, "numerator")), 0), editor.model.cursor)
        assertEquals(MathCursorLevel.INTERIOR, editor.model.navigationLevel)
    }

    @Test fun aNewAtomAfterNeutralNumberHasItsOwnCompositionAndEmptyEditingScope() = onMain {
        val editor = neutralEditor("12")
        var input = connection(editor)
        assertTrue(input.commitText("y", 1))
        assertEquals("12y", editor.getLatex())
        assertEquals(MathContentScope.Atom(1, 2), editor.model.editingScope)
        input = connection(editor)
        assertEquals("\uFFFCy", input.getTextBeforeCursor(20, 0).toString())
        assertTrue(input.setComposingRegion(1, 2))
        assertTrue(input.setComposingText("yz", 1))
        assertTrue(input.setComposingText("yz", 1))
        assertTrue(input.commitText("yz", 1))
        assertEquals("12yz", editor.getLatex())
        assertEquals(MathExpression.Number("12"), editor.model.expression.children.first())
        assertEquals(MathContentScope.Atom(2, 3), editor.model.editingScope)
        delete(editor, Transport.TEXT)
        assertEquals("12y", editor.getLatex())
        val emptyLastAtom = snapshot(editor)
        delete(editor, Transport.TEXT)
        assertEquals("An emptied z atom cannot edit the independently inserted y", emptyLastAtom, snapshot(editor))
        editor.perform(MathKeyboardAction.PreviousSlot)
        editor.perform(MathKeyboardAction.PreviousSlot)
        delete(editor, Transport.TEXT)
        assertEquals("12", editor.getLatex())
        val empty = snapshot(editor)
        repeat(3) { delete(editor, Transport.KEY) }
        assertEquals("An empty new atom cannot reach the previous Number(12)", empty, snapshot(editor))
        assertTrue(connection(editor).commitText("z", 1))
        assertEquals("12z", editor.getLatex())
        assertNull(editor.model.selectionRange)
    }

    @Test fun whitespaceTouchChoosesNeutralWhileTouchingTheNumericInkChoosesContent() = onMain {
        for (inside in listOf(false, true)) {
            val editor = neutralEditor("12")
            editor.measure(View.MeasureSpec.makeMeasureSpec(500, View.MeasureSpec.EXACTLY),
                View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED))
            editor.layout(0, 0, editor.measuredWidth, editor.measuredHeight)
            val leaf = MathCursor(offset = 0)
            val caret = requireNotNull(editor.expressionLayout.textCaretBounds(leaf, 2))
            val x = if (inside) editor.expressionOriginX + caret.left else editor.width - 2f
            val y = editor.expressionBaseline + caret.centerY()
            val now = SystemClock.uptimeMillis()
            fun event(action: Int, time: Long) = MotionEvent.obtain(now, time, action, x, y, 0)
            val down = event(MotionEvent.ACTION_DOWN, now)
            val up = event(MotionEvent.ACTION_UP, now + 30)
            try { editor.dispatchTouchEvent(down); editor.dispatchTouchEvent(up) } finally { down.recycle(); up.recycle() }
            assertEquals(if (inside) MathCursorLevel.INTERIOR else MathCursorLevel.NEUTRAL, editor.model.navigationLevel)
            delete(editor, Transport.TEXT)
            assertEquals(if (inside) "1" else "", editor.getLatex())
        }
    }

    @Test fun nativeSelectedRangeCannotComposeOrCommitAcrossAPreexistingStructuralObject() = onMain {
        val editor = neutralEditor("\\sqrt{x+\\frac{1}{2}+y}")
        val path = listOf(MathCursorStep(0, "radicand"))
        editor.model.placeTextCursor(MathCursor(path, 0), 1)
        assertEquals(MathCursorLevel.INTERIOR, editor.model.navigationLevel)
        assertEquals(MathContentScope.Slot, editor.model.editingScope)
        val original = editor.model.expression
        val input = connection(editor)
        assertTrue(input.setSelection(0, 5))
        assertNull("A private native range is not a structural MathSelection", editor.model.selectionRange)
        assertEquals("x+\uFFFC+y", input.getSelectedText(0).toString())
        val unchanged = snapshot(editor)
        assertFalse("An IME composing region cannot claim the preexisting fraction", input.setComposingRegion(0, 5))
        assertFalse(input.setComposingText("z", 1))
        assertSame(original, editor.model.expression)
        assertEquals(unchanged, snapshot(editor))
        assertFalse(input.commitText("z", 1))
        assertSame(original, editor.model.expression)
        assertEquals(unchanged, snapshot(editor))
        if (Build.VERSION.SDK_INT >= 34) {
            assertFalse(input.replaceText(0, 5, "z", 1, null))
            assertSame(original, editor.model.expression)
            assertEquals(unchanged, snapshot(editor))
        }
        assertNull(editor.model.selectionRange)
    }

    @Test fun composedRootWordAfterANeutralNumberKeepsOnlyItsLastAtomAsNavigationContext() = onMain {
        val editor = neutralEditor("12")
        val input = connection(editor)
        assertTrue(input.setComposingText("abc", 1))
        assertTrue(input.setComposingText("abc", 1))
        assertTrue(input.commitText("abc", 1))
        assertEquals("12abc", editor.getLatex())
        assertEquals(MathExpression.Number("12"), editor.model.expression.children.first())
        assertEquals(MathContentScope.Atom(3, 4), editor.model.editingScope)
        assertEquals("\uFFFC\uFFFC\uFFFCc", input.getTextBeforeCursor(20, 0).toString())
        assertFalse(input.setComposingRegion(0, 1))
        assertTrue(input.finishComposingText())
        delete(editor, Transport.TEXT)
        assertEquals("12ab", editor.getLatex())
        val emptyLastAtom = snapshot(editor)
        repeat(3) { delete(editor, Transport.TEXT) }
        assertEquals(emptyLastAtom, snapshot(editor))
        repeat(3) { delete(editor, Transport.KEY) }
        assertEquals("The empty c atom cannot delete previous independently inserted parents", "12ab", editor.getLatex())
        assertNull(editor.model.selectionRange)
    }

    @Test fun composedLatexAfterANeutralNumberOwnsOnlyTheNewlyInsertedStructure() = onMain {
        val editor = neutralEditor("12")
        val input = connection(editor)
        val fraction = "\\frac{1}{2}"
        assertTrue(input.setComposingText(fraction, 1))
        assertTrue(input.setComposingText(fraction, 1))
        assertTrue(input.commitText(fraction, 1))
        assertEquals("12" + fraction, editor.getLatex())
        assertEquals(MathExpression.Number("12"), editor.model.expression.children.first())
        assertEquals(2, editor.model.expression.children.size)
        assertNull(editor.model.selectionRange)
    }

    @Test fun composingRangeInsideASurrogatePairReplacesTheWholePairExactlyOnce() = onMain {
        val editor = surrogateSlot()
        val input = connection(editor)
        assertTrue(input.setComposingRegion(1, 2))
        assertNull(editor.model.selectionRange)
        assertTrue(input.setComposingText("x", 1))
        assertTrue(input.setComposingText("x", 1))
        assertTrue(input.commitText("x", 1))
        assertEquals(listOf(MathExpression.Variable("x")), requireNotNull(editor.model.sequenceAt(editor.model.cursor.path)).children)
        assertEquals("x", input.getTextBeforeCursor(20, 0).toString())
        assertNull(editor.model.selectionRange)
        assertEquals("2", MathLatexSerializer.serialize((editor.model.expression.children.single() as MathExpression.Scripts).upper!!))
    }

    @Test fun committingAReplacementForHalfASurrogatePairKeepsAZeroCursorBeforeTheInsertion() = onMain {
        val editor = surrogateSlot()
        val input = connection(editor)
        assertTrue(input.setComposingRegion(1, 2))
        assertTrue(input.commitText("x", 0))
        assertEquals(listOf(MathExpression.Variable("x")), requireNotNull(editor.model.sequenceAt(editor.model.cursor.path)).children)
        assertEquals("", input.getTextBeforeCursor(20, 0).toString())
        assertEquals("x", input.getTextAfterCursor(20, 0).toString())
        assertEquals(0, editor.model.cursor.offset)
        assertNull(editor.model.selectionRange)
    }

    @Test fun explicitCollapsedReplacementInsideASurrogatePairMovesAfterThePairBeforeInsertion() = onMain {
        if (Build.VERSION.SDK_INT >= 34) {
            val editor = surrogateSlot()
            val input = connection(editor)
            assertTrue(input.replaceText(1, 1, "x", 0, null))
            assertEquals(listOf(MathExpression.Symbol("\uD835\uDEFC"), MathExpression.Variable("x")),
                requireNotNull(editor.model.sequenceAt(editor.model.cursor.path)).children)
            assertEquals("\uD835\uDEFC", input.getTextBeforeCursor(20, 0).toString())
            assertEquals("x", input.getTextAfterCursor(20, 0).toString())
            assertEquals(1, editor.model.cursor.offset)
            assertNull(editor.model.selectionRange)
        }
    }

    @Test fun exponentHasExactlyNineStatesOnLeftFromWholeParentToHome() = onMain {
        val editor = neutralEditor("x^{2}")
        val upper = listOf(MathCursorStep(0, "upper"))
        val base = listOf(MathCursorStep(0, "base"))
        val original = editor.model.expression
        assertEquals(MathCursorPresentation.ParentBoundary(emptyList(), 0), editor.model.cursorPresentation)
        stepLeft(editor)
        assertEquals(MathCursor(base, 1), editor.model.cursor)
        stepLeft(editor)
        assertEquals(MathSelection(base, 0, 1), editor.model.selectionRange)
        stepLeft(editor)
        assertEquals(MathCursor(base, 0), editor.model.cursor)
        assertNull(editor.model.selectionRange)
        stepLeft(editor)
        assertEquals(MathCursorPresentation.ChildBoundary(upper, true), editor.model.cursorPresentation)
        stepLeft(editor)
        assertEquals(MathCursor(upper, 1), editor.model.cursor)
        stepLeft(editor)
        assertEquals(MathSelection(upper, 0, 1), editor.model.selectionRange)
        stepLeft(editor)
        assertEquals(MathCursor(upper, 0), editor.model.cursor)
        assertNull(editor.model.selectionRange)
        stepLeft(editor)
        assertEquals(MathCursor(), editor.model.cursor)
        assertEquals(MathCursorPresentation.Neutral, editor.model.cursorPresentation)
        assertFalse(editor.model.canNavigatePrevious)
        editor.perform(MathKeyboardAction.NextSlot)
        assertEquals(MathCursorPresentation.ParentBoundary(emptyList(), 0), editor.model.cursorPresentation)
        assertFalse(editor.model.canNavigateNext)
        assertSame(original, editor.model.expression)
    }

    @Test fun nativeAndHardwareArrowsUseTheSameNineStateLeftDiagram() = onMain {
        for (native in listOf(false, true)) {
            val editor = neutralEditor("x^{2}")
            val base = listOf(MathCursorStep(0, "base"))
            val upper = listOf(MathCursorStep(0, "upper"))
            val original = editor.model.expression
            val expected = listOf(
                Triple(MathCursor(offset = 1), null, MathCursorPresentation.ParentBoundary(emptyList(), 0)),
                Triple(MathCursor(base, 1), null, MathCursorPresentation.Interior(MathContentScope.Slot)),
                Triple(MathCursor(base, 1), MathSelection(base, 0, 1), MathCursorPresentation.Interior(MathContentScope.Slot)),
                Triple(MathCursor(base, 0), null, MathCursorPresentation.Interior(MathContentScope.Slot)),
                Triple(MathCursor(offset = 1, level = MathCursorLevel.CHILD_BOUNDARY), null, MathCursorPresentation.ChildBoundary(upper)),
                Triple(MathCursor(upper, 1), null, MathCursorPresentation.Interior(MathContentScope.Slot)),
                Triple(MathCursor(upper, 1), MathSelection(upper, 0, 1), MathCursorPresentation.Interior(MathContentScope.Slot)),
                Triple(MathCursor(upper, 0), null, MathCursorPresentation.Interior(MathContentScope.Slot)),
                Triple(MathCursor(), null, MathCursorPresentation.Neutral)
            )
            assertEquals(expected.first(), snapshot(editor))
            expected.drop(1).forEach { target ->
                fineArrow(editor, forward = false, native = native)
                assertEquals("Every left arrow consumes exactly one structural state", target, snapshot(editor))
                assertSame(original, editor.model.expression)
            }
            assertFalse(editor.model.canNavigatePrevious)
            fineArrow(editor, forward = true, native = native)
            assertEquals(expected.first(), snapshot(editor))
            assertFalse("There is no FINISH state after the parent boundary", editor.model.canNavigateNext)
        }
    }

    @Test fun rightVisitsOnlyTheNearestParentBoundaryAndEmptyRootHasOnlyHome() = onMain {
        for (native in listOf(false, true)) {
            val editor = integralAndPower()
            editor.model.cursor = MathCursor()
            val original = editor.model.expression
            fineArrow(editor, forward = true, native = native)
            assertEquals(MathCursorPresentation.ParentBoundary(emptyList(), 0), editor.model.cursorPresentation)
            assertEquals(MathCursor(offset = 1), editor.model.cursor)
            fineArrow(editor, forward = true, native = native)
            assertEquals(MathCursorPresentation.ParentBoundary(emptyList(), 1), editor.model.cursorPresentation)
            assertEquals(MathCursor(offset = 2), editor.model.cursor)
            assertFalse(editor.model.canNavigateNext)
            fineArrow(editor, forward = true, native = native)
            assertEquals(MathCursorPresentation.ParentBoundary(emptyList(), 1), editor.model.cursorPresentation)
            assertSame(original, editor.model.expression)
            assertNull(editor.model.selectionRange)

            val empty = neutralEditor("")
            val home = snapshot(empty)
            assertFalse(empty.model.canNavigatePrevious)
            assertFalse(empty.model.canNavigateNext)
            fineArrow(empty, forward = true, native = native)
            fineArrow(empty, forward = false, native = native)
            assertEquals(home, snapshot(empty))
            assertEquals(MathCursor(), empty.model.cursor)
            assertEquals(MathCursorPresentation.Neutral, empty.model.cursorPresentation)
        }
    }

    @Test fun emptySlotExitsRightButPreviousContinuesToTheParentWithoutALeftExterior() = onMain {
        val editor = slotEditor("x^{}", "upper", atEnd = false)
        val inside = snapshot(editor)
        editor.perform(MathKeyboardAction.NextSlot)
        assertEquals(MathCursorPresentation.ChildBoundary(listOf(MathCursorStep(0, "upper")), true), editor.model.cursorPresentation)
        stepLeft(editor)
        assertEquals(inside, snapshot(editor))
        stepLeft(editor)
        assertEquals(MathCursor(), editor.model.cursor)
        assertNull(editor.model.selectionRange)
        editor.onCursorRequested?.invoke(MathCursor(listOf(MathCursorStep(0, "upper")), 0))
        assertEquals(inside, snapshot(editor))
        editor.perform(MathKeyboardAction.NextSlot)
        assertEquals(MathCursorPresentation.ChildBoundary(listOf(MathCursorStep(0, "upper")), true), editor.model.cursorPresentation)
        delete(editor, Transport.TOOLBAR)
        assertEquals("Only the empty child's RIGHT exterior permits removing its optional slot", "x", editor.getLatex())
    }

    @Test fun backspaceAtInteriorStartNeverTreatsThePositionAsAnExterior() = onMain {
        val fixtures = listOf("x^{2}" to "upper", "x^{}" to "upper", "\\frac{12}{34}" to "numerator")
        fixtures.forEach { (source, name) -> Transport.entries.forEach { transport ->
            val editor = slotEditor(source, name, atEnd = false)
            val original = editor.model.expression
            val inside = snapshot(editor)
            delete(editor, transport)
            assertSame("$transport at START edits only this slot and cannot remove its owner", original, editor.model.expression)
            assertEquals(inside, snapshot(editor))
            assertEquals(MathCursorLevel.INTERIOR, editor.model.navigationLevel)
        } }
    }

    @Test fun everyReachableBoundaryInBothDirectionsIsOnTheRightOfItsChild() = onMain {
        val fixtures = listOf("x_{1}^{2}", "\\frac{12}{34}", "\\sqrt[3]{12}",
            "\\begin{matrix}1&2\\\\3&4\\end{matrix}", "\\frac{x^{2}}{\\sqrt[3]{4}}")
        fixtures.forEach { source ->
            val editor = neutralEditor(source)
            editor.model.cursor = MathCursor()
            val original = editor.model.expression
            for (forward in listOf(true, false)) {
                var count = 0
                while (if (forward) editor.model.canNavigateNext else editor.model.canNavigatePrevious) {
                    assertTrue("The structural walk must terminate", count++ < 150)
                    editor.perform(if (forward) MathKeyboardAction.NextSlot else MathKeyboardAction.PreviousSlot)
                    assertRightBoundary(editor)
                    assertSame(original, editor.model.expression)
                    val cursor = editor.model.cursor
                    val sequence = requireNotNull(editor.model.sequenceAt(cursor.path))
                    assertTrue(cursor.offset in 0..sequence.children.size)
                }
            }
            assertEquals(MathCursor(), editor.model.cursor)
            assertNull(editor.model.selectionRange)
        }
    }

    private fun neutralEditor(source: String) = MathExpressionEditor(context).also {
        assertTrue(it.setLatex(source))
        it.model.cursor = MathCursor(offset = it.model.expression.children.size)
    }

    private fun slotEditor(source: String, name: String, atEnd: Boolean) = neutralEditor(source).also {
        val path = listOf(MathCursorStep(0, name))
        val end = requireNotNull(it.model.sequenceAt(path)).children.size
        it.model.cursor = MathCursor(path, if (atEnd) end else 0)
    }

    private fun boundaryEditor(source: String, name: String, after: Boolean) = slotEditor(source, name, after).also {
        require(after) { "An exterior fixture can only represent a child RIGHT boundary" }
        it.perform(if (after) MathKeyboardAction.NextSlot else MathKeyboardAction.PreviousSlot)
        assertEquals(MathCursorLevel.CHILD_BOUNDARY, it.model.navigationLevel)
        assertRightBoundary(it)
    }

    private fun integralAndPower() = MathExpressionEditor(context).also {
        it.model.setExpression(MathExpression.Sequence(listOf(
            MathExpression.LargeOperator(MathLargeOperator.INTEGRAL, MathExpression.Sequence(),
                MathExpression.Sequence(listOf(MathExpression.Number("0"))), MathExpression.Sequence(listOf(MathExpression.Number("1")))),
            MathExpression.Scripts(MathExpression.Sequence(listOf(MathExpression.Variable("x"))),
                upper = MathExpression.Sequence(listOf(MathExpression.Number("2"))))
        )), MathCursor(offset = 2))
    }

    private fun surrogateSlot() = MathExpressionEditor(context).also {
        val path = listOf(MathCursorStep(0, "lower"))
        it.model.setExpression(MathExpression.Sequence(listOf(MathExpression.Scripts(
            MathExpression.Sequence(listOf(MathExpression.Variable("x"))),
            upper = MathExpression.Sequence(listOf(MathExpression.Number("2"))),
            lower = MathExpression.Sequence(listOf(MathExpression.Symbol("\uD835\uDEFC")))
        ))), MathCursor(path, 1))
    }

    private fun delete(editor: MathExpressionEditor, transport: Transport) {
        val input = connection(editor)
        when (transport) {
            Transport.TEXT -> assertTrue(input.deleteSurroundingText(1, 0))
            Transport.CODE_POINTS -> assertTrue(input.deleteSurroundingTextInCodePoints(1, 0))
            Transport.KEY -> {
                assertTrue(input.sendKeyEvent(KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_DEL)))
                val afterDown = snapshot(editor)
                input.sendKeyEvent(KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_DEL))
                assertEquals("Key-up cannot perform another logical deletion", afterDown, snapshot(editor))
            }
            Transport.TOOLBAR -> editor.perform(MathKeyboardAction.Backspace)
        }
    }

    private fun snapshot(editor: MathExpressionEditor) = Triple(editor.model.cursor, editor.model.selectionRange, editor.model.cursorPresentation)

    private fun assertRightBoundary(editor: MathExpressionEditor) {
        val boundary = editor.model.cursorPresentation as? MathCursorPresentation.ChildBoundary ?: return
        assertTrue("OUTSIDE exists only after a child", boundary.after)
        val step = boundary.slotPath.last()
        assertEquals(boundary.slotPath.dropLast(1), editor.model.cursor.path)
        assertEquals(step.nodeIndex + 1, editor.model.cursor.offset)
        assertEquals(MathCursorLevel.CHILD_BOUNDARY, editor.model.navigationLevel)
    }

    private fun stepLeft(editor: MathExpressionEditor) {
        val expression = editor.model.expression
        editor.perform(MathKeyboardAction.PreviousSlot)
        assertSame(expression, editor.model.expression)
        assertRightBoundary(editor)
    }

    private fun fineArrow(editor: MathExpressionEditor, forward: Boolean, native: Boolean) {
        if (native) {
            val input = connection(editor)
            val key = if (forward) KeyEvent.KEYCODE_DPAD_RIGHT else KeyEvent.KEYCODE_DPAD_LEFT
            assertTrue(input.sendKeyEvent(KeyEvent(KeyEvent.ACTION_DOWN, key)))
            val afterDown = snapshot(editor)
            input.sendKeyEvent(KeyEvent(KeyEvent.ACTION_UP, key))
            assertEquals("Arrow key-up cannot consume a second navigation state", afterDown, snapshot(editor))
        } else editor.perform(if (forward) MathKeyboardAction.MoveRight else MathKeyboardAction.MoveLeft)
    }
    private fun connection(editor: MathExpressionEditor): InputConnection = requireNotNull(editor.onCreateInputConnection(EditorInfo()))
    private fun canonical(source: String) = MathLatexSerializer.serialize((MathLatexParser.parse(source) as MathLatexParser.Result.Success).expression)
    private fun onMain(body: () -> Unit) = instrumentation.runOnMainSync { body() }
}
