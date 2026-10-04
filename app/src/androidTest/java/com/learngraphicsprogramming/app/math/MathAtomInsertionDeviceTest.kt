package com.learngraphicsprogramming.app.math

import android.text.Selection
import android.view.inputmethod.BaseInputConnection
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputConnection
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

/** Each input callback publishes its AST and the last atom's navigation context synchronously. */
@RunWith(AndroidJUnit4::class)
class MathAtomInsertionDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private fun parse(source: String) = (MathLatexParser.parse(source) as MathLatexParser.Result.Success).expression
    private fun editor(source: String = "") = MathExpressionEditor(context).also { assertTrue(it.setLatex(source)) }
    private fun connection(editor: MathExpressionEditor): InputConnection = editor.onCreateInputConnection(EditorInfo())
    private fun onMain(block: () -> Unit) = instrumentation.runOnMainSync(block)

    @Test fun rapidCharacterCommitsMaterializeIndependentParentsBeforeImmediateLeft() = onMain {
        val editor = editor("x^{2}")
        val existing = editor.model.expression.children.single()
        val input = connection(editor)
        var notifications = 0
        editor.onExpressionChanged = {
            notifications++
            val count = editor.model.expression.children.size
            assertEquals(MathContentScope.Atom(count - 1, count), editor.model.editingScope)
            assertEquals(count, editor.model.cursor.offset)
            assertNull(editor.model.selectionRange)
        }
        for ((index, letter) in "abc".withIndex()) {
            assertTrue(input.commitText(letter.toString(), 1))
            assertEquals("One text callback publishes one logical expression change", index + 1, notifications)
            assertSame(existing, editor.model.expression.children.first())
            assertEquals("abc".take(index + 1).map { MathExpression.Variable(it.toString()) }, editor.model.expression.children.drop(1))
        }
        editor.onExpressionChanged = null
        editor.perform(MathKeyboardAction.PreviousSlot)
        assertEquals(MathSelection(emptyList(), 3, 4), editor.model.selectionRange)
        assertEquals("c", connection(editor).getSelectedText(0).toString())
    }

    @Test fun aSingleCommittedWordAlreadyHasSeparateParentsAndOnlyTheLastAtomAsItsScope() = onMain {
        val editor = editor()
        var notifications = 0
        editor.onExpressionChanged = {
            notifications++
            assertEquals(listOf(MathExpression.Variable("a"), MathExpression.Variable("b"), MathExpression.Variable("c")),
                editor.model.expression.children)
            assertEquals(MathContentScope.Atom(2, 3), editor.model.editingScope)
        }
        assertTrue(connection(editor).commitText("abc", 1))
        assertEquals(1, notifications)
        editor.onExpressionChanged = null
        editor.perform(MathKeyboardAction.PreviousSlot)
        assertEquals(MathSelection(emptyList(), 2, 3), editor.model.selectionRange)
    }

    @Test fun cumulativeComposingWordsStayPrivateWhileImmediateLeftSelectsOnlyTheLastAtom() = onMain {
        val editor = editor("x^{2}")
        val existing = editor.model.expression.children.single()
        val input = connection(editor)
        var notifications = 0
        editor.onExpressionChanged = { notifications++ }
        for ((index, word) in listOf("a", "ab", "abc").withIndex()) {
            assertTrue(input.setComposingText(word, 1))
            assertEquals(index + 1, notifications)
            assertSame(existing, editor.model.expression.children.first())
            assertEquals(word.map { MathExpression.Variable(it.toString()) }, editor.model.expression.children.drop(1))
            assertEquals(MathContentScope.Atom(word.length, word.length + 1), editor.model.editingScope)
            assertNull("A composing word is never a mathematical selection", editor.model.selectionRange)
            assertEquals("\uFFFC$word", input.getTextBeforeCursor(20, 0).toString())
        }
        assertTrue(input.setComposingText("abc", 1))
        assertEquals("An unchanged composition does not publish another logical mutation", 3, notifications)
        editor.onExpressionChanged = null
        editor.perform(MathKeyboardAction.PreviousSlot)
        assertEquals(MathSelection(emptyList(), 3, 4), editor.model.selectionRange)
        val unchanged = editor.model.expression
        assertFalse("Navigation retires the old word's protocol session", input.setComposingText("abcd", 1))
        assertSame(unchanged, editor.model.expression)
        assertEquals("c", connection(editor).getSelectedText(0).toString())
    }

    @Test fun composingWordOffsetsAfterACompressedNumberPreserveEveryPreexistingParent() = onMain {
        val editor = editor("x^{2}12")
        val oldParents = editor.model.expression.children.toList()
        val input = connection(editor)
        for (word in listOf("a", "ab", "abc", "abd", "xy", "abc")) {
            assertTrue(input.setComposingText(word, 1))
            assertEquals(oldParents + word.map { MathExpression.Variable(it.toString()) }, editor.model.expression.children)
            assertEquals(MathContentScope.Atom(word.length + 1, word.length + 2), editor.model.editingScope)
            val snapshot = requireNotNull((input as BaseInputConnection).editable)
            assertEquals("\uFFFC\uFFFC$word", snapshot.toString())
            assertEquals(2, BaseInputConnection.getComposingSpanStart(snapshot))
            assertEquals(word.length + 2, BaseInputConnection.getComposingSpanEnd(snapshot))
            assertEquals(word.length + 2, Selection.getSelectionEnd(snapshot))
        }
        assertFalse("A native region cannot acquire the old power or Number(12)", input.setComposingRegion(0, 2))
        assertTrue(input.setComposingText("abc", 1))
        val beforeFinish = editor.model.expression
        val cursorBefore = editor.model.cursor
        var notifications = 0
        editor.onExpressionChanged = { notifications++ }
        assertTrue(input.finishComposingText())
        assertSame("Finishing a word never restores or rewrites the AST", beforeFinish, editor.model.expression)
        assertEquals(cursorBefore, editor.model.cursor)
        assertEquals(0, notifications)
        assertEquals("\uFFFC\uFFFC\uFFFC\uFFFCc", input.getTextBeforeCursor(20, 0).toString())
        assertEquals(-1, BaseInputConnection.getComposingSpanStart(requireNotNull((input as BaseInputConnection).editable)))
        editor.onExpressionChanged = null
        editor.perform(MathKeyboardAction.PreviousSlot)
        assertEquals(MathSelection(emptyList(), 4, 5), editor.model.selectionRange)
    }

    @Test fun shortenedOwnedWordsAndBackspaceCannotReplayDeletedTextOrClaimOldParents() = onMain {
        val editor = editor("x^{2}12")
        val oldParents = editor.model.expression.children.toList()
        val input = connection(editor)
        assertTrue(input.setComposingText("abc", 1))
        assertTrue(input.deleteSurroundingText(1, 0))
        assertEquals(oldParents + listOf(MathExpression.Variable("a"), MathExpression.Variable("b")), editor.model.expression.children)
        val emptyLastAtom = editor.model.expression
        assertTrue(input.deleteSurroundingText(1, 0))
        assertSame("An empty c atom cannot delete b through native word context", emptyLastAtom, editor.model.expression)
        assertTrue(input.setComposingText("ab", 1))
        assertTrue(input.setComposingText("ab", 1))
        assertTrue(input.setComposingText("a", 1))
        assertTrue(input.commitText("a", 1))
        assertTrue(input.finishComposingText())
        assertEquals(oldParents + MathExpression.Variable("a"), editor.model.expression.children)
        assertEquals(MathContentScope.Atom(2, 3), editor.model.editingScope)
        assertFalse(input.setComposingRegion(0, 2))
    }

    @Test fun endingAWordContainingANumberReprojectsPrivateOffsetsWithoutEditingTheAst() = onMain {
        val editor = editor()
        val input = connection(editor)
        assertTrue(input.setComposingText("12abc", 1))
        val original = editor.model.expression
        val cursor = editor.model.cursor
        val expanded = requireNotNull((input as BaseInputConnection).editable)
        assertEquals("12abc", expanded.toString())
        assertEquals(5, Selection.getSelectionEnd(expanded))
        var notifications = 0
        editor.onExpressionChanged = { notifications++ }
        assertTrue(input.setComposingRegion(0, 0))
        val collapsed = requireNotNull(input.editable)
        assertEquals("\uFFFC\uFFFC\uFFFCc", collapsed.toString())
        assertEquals(collapsed.length, Selection.getSelectionStart(collapsed))
        assertEquals(collapsed.length, Selection.getSelectionEnd(collapsed))
        assertTrue(input.finishComposingText())
        assertSame(original, editor.model.expression)
        assertEquals(cursor, editor.model.cursor)
        assertEquals(0, notifications)
        editor.onExpressionChanged = null
        editor.perform(MathKeyboardAction.PreviousSlot)
        assertEquals(MathSelection(emptyList(), 3, 4), editor.model.selectionRange)
    }

    @Test fun deliberateStructuralSlotsKeepTheirOwnContentScopeWhileRootAtomsStayIndependent() = onMain {
        val editor = editor("x^{}")
        val upper = listOf(MathCursorStep(0, "upper"))
        editor.model.cursor = MathCursor(upper, 0)
        val input = connection(editor)
        for (word in listOf("a", "ab", "abc")) assertTrue(input.setComposingText(word, 1))
        assertEquals(MathContentScope.Slot, editor.model.editingScope)
        assertEquals(parse("abc").children, requireNotNull(editor.model.sequenceAt(upper)).children)
        editor.perform(MathKeyboardAction.PreviousSlot)
        assertEquals(MathSelection(upper, 0, 3), editor.model.selectionRange)
    }

    @Test fun discardedFactoryProbeKeepsTheServedConnectionsPrivateComposingWord() = onMain {
        val editor = editor()
        val served = connection(editor)
        assertTrue(served.setComposingText("a", 1))
        val beforeProbe = editor.model.expression
        val cursorBefore = editor.model.cursor
        val discardedProbe = connection(editor)
        assertSame("A connection factory query cannot edit the AST", beforeProbe, editor.model.expression)
        assertEquals(cursorBefore, editor.model.cursor)
        assertTrue("Android may discard the probe and keep the first connection", served.setComposingText("ab", 1))
        assertEquals(parse("ab").children, editor.model.expression.children)
        assertTrue(served.setComposingText("abc", 1))
        assertEquals(parse("abc").children, editor.model.expression.children)
        assertEquals(MathContentScope.Atom(2, 3), editor.model.editingScope)
        val snapshot = requireNotNull((served as BaseInputConnection).editable)
        assertEquals("abc", snapshot.toString())
        assertEquals(0, BaseInputConnection.getComposingSpanStart(snapshot))
        assertEquals(3, BaseInputConnection.getComposingSpanEnd(snapshot))
        discardedProbe.closeConnection()
    }

    @Test fun closingADiscardedProbeClosesOnlyThatTransportAndNotTheServedWord() = onMain {
        val editor = editor()
        val served = connection(editor)
        assertTrue(served.setComposingText("abc", 1))
        val discardedProbe = connection(editor)
        discardedProbe.closeConnection()
        assertFalse("The closed probe cannot edit the AST", discardedProbe.commitText("z", 1))
        assertTrue("Closing the discarded probe cannot close the served context", served.setComposingText("abcd", 1))
        assertEquals(parse("abcd").children, editor.model.expression.children)
        assertEquals(MathContentScope.Atom(3, 4), editor.model.editingScope)
        assertTrue(served.commitText("abcd", 1))
        assertEquals("The live word is committed once", parse("abcd").children, editor.model.expression.children)
    }

    @Test fun closingTheOldTransportCannotEraseTheNewTransportWordInTheSameContext() = onMain {
        val editor = editor()
        val old = connection(editor)
        val replacement = connection(editor)
        assertTrue(replacement.setComposingText("abc", 1))
        old.closeConnection()
        assertFalse(old.setComposingText("z", 1))
        assertTrue(replacement.setComposingText("abcd", 1))
        assertEquals(parse("abcd").children, editor.model.expression.children)
        assertEquals(MathContentScope.Atom(3, 4), editor.model.editingScope)
    }

    @Test fun explicitNavigationRetiresEveryTransportFromThePreviousMathematicalContext() = onMain {
        val editor = editor()
        val served = connection(editor)
        val probe = connection(editor)
        assertTrue(probe.setComposingText("abc", 1))
        editor.perform(MathKeyboardAction.PreviousSlot)
        assertEquals(MathSelection(emptyList(), 2, 3), editor.model.selectionRange)
        val unchanged = editor.model.expression
        assertFalse(served.commitText("z", 1))
        assertFalse(probe.setComposingText("abcd", 1))
        assertSame(unchanged, editor.model.expression)
        assertEquals(MathSelection(emptyList(), 2, 3), editor.model.selectionRange)
        assertEquals("c", connection(editor).getSelectedText(0).toString())
    }

    @Test fun probeCannotEndAnotherTransportBatchAndClosingItPreservesServedNesting() = onMain {
        val editor = editor()
        val served = connection(editor)
        assertTrue(served.beginBatchEdit())
        assertTrue(served.beginBatchEdit())
        assertTrue(served.setComposingText("a", 1))
        val probe = connection(editor)
        assertFalse("A probe without a local batch cannot end the served connection's batch", probe.endBatchEdit())
        assertTrue(probe.beginBatchEdit())
        probe.closeConnection()
        assertFalse(probe.endBatchEdit())
        assertTrue("Closing the probe preserves both nested served batches", served.endBatchEdit())
        assertTrue(served.setComposingText("ab", 1))
        assertFalse("The served connection closes its own final batch", served.endBatchEdit())
        assertFalse(served.endBatchEdit())
        assertTrue(served.setComposingText("abc", 1))
        assertEquals(parse("abc").children, editor.model.expression.children)
        assertEquals(MathContentScope.Atom(2, 3), editor.model.editingScope)
    }
}
