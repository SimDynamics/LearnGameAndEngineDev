package com.learngraphicsprogramming.app.math

import android.content.Context
import android.content.Intent
import android.view.KeyEvent
import android.view.ViewGroup
import android.text.Selection
import android.view.inputmethod.BaseInputConnection
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.ExtractedTextRequest
import android.view.inputmethod.InputConnection
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.filters.SdkSuppress
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.LessonActivity
import com.learngraphicsprogramming.app.R
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/** IME deletion edits the active slot; structural deletion remains a separate toolbar action. */
@RunWith(AndroidJUnit4::class)
class MathImeBackspaceDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val snapshots = linkedMapOf<String, Map<String, *>>()

    private enum class DeleteTransport { SURROUNDING_TEXT, CODE_POINTS, KEY_EVENT }

    @Before fun preservePreferences() {
        listOf("learning_progress", "navigation_state", "lesson_text_size_settings").forEach {
            snapshots[it] = context.getSharedPreferences(it, Context.MODE_PRIVATE).all.toMap()
        }
    }

    @After fun restorePreferences() {
        snapshots.forEach { (name, original) ->
            val preferences = context.getSharedPreferences(name, Context.MODE_PRIVATE)
            val editor = preferences.edit()
            (preferences.all.keys - original.keys).forEach(editor::remove)
            original.forEach { (key, value) -> when (value) {
                is String -> editor.putString(key, value)
                is Int -> editor.putInt(key, value)
                is Long -> editor.putLong(key, value)
                is Float -> editor.putFloat(key, value)
                is Boolean -> editor.putBoolean(key, value)
                is Set<*> -> editor.putStringSet(key, value.filterIsInstance<String>().toSet())
            } }
            assertTrue(editor.commit())
        }
    }

    @Test fun imeBackspaceClearsOneDigitSubscriptAndKeepsTheEmptySlotEditable() = onMain {
        DeleteTransport.entries.forEach { transport ->
            val editor = editorAtLower("x_{1}")
            val input = connection(editor)
            assertEquals("1", input.getTextBeforeCursor(20, 0).toString())
            backspace(input, transport)
            assertScripts(editor, lower = "", upper = null)
            assertEquals("", input.getTextBeforeCursor(20, 0).toString())
            val emptyDraft = editor.saveDraft().toString()
            repeat(5) {
                backspace(input, transport)
                assertEquals("Empty-slot $transport cannot change AST, cursor or selection", emptyDraft, editor.saveDraft().toString())
                assertScripts(editor, lower = "", upper = null)
            }
            assertTrue(input.commitText("3", 1))
            assertScripts(editor, lower = "3", upper = null)
        }
    }

    @Test fun imeBackspaceRemovesOneCharacterFromAMultiDigitNumber() = onMain {
        DeleteTransport.entries.forEach { transport ->
            val editor = editorAtLower("x_{12}")
            val input = connection(editor)
            backspace(input, transport)
            assertScripts(editor, lower = "1", upper = null)
            assertEquals("1", input.getTextBeforeCursor(20, 0).toString())
            backspace(input, transport)
            assertScripts(editor, lower = "", upper = null)
        }
    }

    @Test fun imeBackspaceInSubscriptPreservesTheBaseAndUpperScript() = onMain {
        DeleteTransport.entries.forEach { transport ->
            val editor = editorAtLower("x_{1}^{2}")
            val input = connection(editor)
            backspace(input, transport)
            assertScripts(editor, lower = "", upper = "2")
            val empty = editor.model.expression
            repeat(4) {
                backspace(input, transport)
                assertEquals("$transport must stay in the empty lower slot", empty, editor.model.expression)
                assertScripts(editor, lower = "", upper = "2")
            }
            assertTrue(input.commitText("4", 1))
            assertScripts(editor, lower = "4", upper = "2")
        }
    }

    @Test fun genuineMathematicalSelectionDeletesTheWholeSelectedSubscript() = onMain {
        DeleteTransport.entries.forEach { transport ->
            val editor = editorAtLower("x_{12}^{2}")
            val path = editor.model.cursor.path
            editor.model.select(path, 0, 1)
            val input = connection(editor)
            assertEquals("12", input.getSelectedText(0).toString())
            assertNotNull(editor.model.selectionRange)
            backspace(input, transport)
            assertNull(editor.model.selectionRange)
            assertScripts(editor, lower = "", upper = "2")
            repeat(3) { backspace(input, transport) }
            assertScripts(editor, lower = "", upper = "2")
        }
    }

    @Test fun composingRegionReplacesExistingSubscriptInsteadOfAppendingDuplicates() = onMain {
        val editor = editorAtLower("x_{1}^{2}")
        val input = connection(editor)
        assertTrue(input.setComposingRegion(0, 1))
        assertNull("An IME composing region is not a user's mathematical selection", editor.model.selectionRange)
        assertTrue(input.setComposingText("1", 1))
        assertScripts(editor, lower = "1", upper = "2")
        assertTrue(input.setComposingText("12", 1))
        assertScripts(editor, lower = "12", upper = "2")
        assertTrue(input.setComposingText("12", 1))
        assertScripts(editor, lower = "12", upper = "2")
        assertTrue(input.commitText("12", 1))
        assertScripts(editor, lower = "12", upper = "2")
        assertEquals("12", input.getTextBeforeCursor(20, 0).toString())
        assertTrue(input.finishComposingText())
        assertScripts(editor, lower = "12", upper = "2")
    }

    @Test fun swiftKeyStyleCompositionAfterBackspaceCannotResurrectOrDuplicateText() = onMain {
        DeleteTransport.entries.forEach { transport ->
            val editor = editorAtLower("x_{12}^{2}")
            val input = connection(editor)
            backspace(input, transport)
            assertScripts(editor, lower = "1", upper = "2")
            assertTrue(input.setComposingRegion(0, 1))
            assertTrue(input.setComposingText("1", 1))
            assertTrue(input.commitText("1", 1))
            assertScripts(editor, lower = "1", upper = "2")
            assertTrue(input.setComposingRegion(0, 1))
            assertTrue(input.setComposingText("", 1))
            assertScripts(editor, lower = "", upper = "2")
            assertTrue(input.finishComposingText())
            repeat(3) { backspace(input, transport) }
            assertScripts(editor, lower = "", upper = "2")
        }
    }

    @Test fun temporaryImeSelectionDoesNotReplaceAnOperandButGenuineSelectionSupportsPaste() = onMain {
        val editor = MathExpressionEditor(context)
        assertTrue(editor.setLatex("x"))
        enterRootContent(editor)
        var input = connection(editor)
        assertTrue(input.setSelection(0, 1))
        assertNull("Temporary IME selection cannot become an AST selection", editor.model.selectionRange)
        assertTrue(input.commitText("+", 1))
        assertEquals("x+", editor.getLatex())
        assertTrue(input.setComposingText("1", 1))
        assertTrue(input.setComposingText("1", 1))
        assertTrue(input.commitText("1", 1))
        assertEquals("x+1", editor.getLatex())

        editor.model.selectAll()
        input = connection(editor)
        assertEquals("x+1", input.getSelectedText(0).toString())
        assertTrue(input.commitText("\\frac{x+1}{2}", 1))
        assertEquals("Pasted LaTeX replaces the genuine mathematical selection once", "\\frac{x+1}{2}", editor.getLatex())
        assertFalse(editor.getLatex().contains('□'))
    }

    @Test fun composingBackspaceCanReduceSignedOrCompoundTextToAnOperator() = onMain {
        listOf("-1" to "-", "x+1" to "+").forEach { (source, remainder) ->
            val editor = MathExpressionEditor(context)
            assertTrue(editor.setLatex(source))
            enterRootContent(editor)
            val input = connection(editor)
            assertTrue(input.setComposingRegion(0, source.length))
            assertTrue(input.setComposingText(remainder, 1))
            assertEquals("A composition update substitutes even when its remainder is an operator", remainder, editor.getLatex())
            assertTrue(input.commitText(remainder, 1))
            assertEquals("Finishing the shortened composition cannot append the operator again", remainder, editor.getLatex())
            assertTrue(input.deleteSurroundingText(1, 0))
            assertEquals("", editor.getLatex())
        }
        val infinity = MathExpressionEditor(context)
        assertTrue(infinity.setLatex("\\infty"))
        enterRootContent(infinity)
        val originalInfinity = infinity.getLatex()
        val input = connection(infinity)
        assertTrue(input.setComposingRegion(0, 1))
        assertTrue(input.commitText("+", 1))
        assertEquals("An isolated committed separator preserves a symbolic operand", originalInfinity + "+", infinity.getLatex())
    }

    @Test fun composedStructuralLatexIsReplacedOnceAndCannotConsumeAnExistingStructure() = onMain {
        val editor = MathExpressionEditor(context)
        val input = connection(editor)
        val fraction = "\\frac{1}{2}"
        assertTrue(input.setComposingText(fraction, 1))
        assertEquals(fraction, editor.getLatex())
        assertTrue(input.setComposingText(fraction, 1))
        assertEquals(fraction, editor.getLatex())
        assertTrue(input.commitText(fraction, 1))
        assertEquals("Confirming composed LaTeX cannot duplicate its AST structure", fraction, editor.getLatex())
        assertFalse("An IME word region cannot claim an existing AST structure", input.setComposingRegion(0, 1))
        assertTrue(input.setComposingText("\\sqrt{3}", 1))
        assertEquals(fraction + "\\sqrt{3}", editor.getLatex())
        assertTrue(input.setComposingText("", 1))
        assertEquals("Only the structure owned by this active composition can be removed", fraction, editor.getLatex())
        assertTrue(input.finishComposingText())
        assertEquals(fraction, editor.getLatex())
        assertTrue(input.commitText("\\sqrt{3}", 1))
        assertEquals("Ordinary LaTeX paste still inserts once", fraction + "\\sqrt{3}", editor.getLatex())
    }

    @Test fun obsoleteAttachedInputConnectionCannotDeleteOrComposeInAnotherSlot() {
        ActivityScenario.launch<LessonActivity>(intent()).use { scenario ->
            scenario.onActivity { activity ->
                val editor = MathExpressionEditor(activity)
                activity.findViewById<ViewGroup>(R.id.lessonContent).apply { removeAllViews(); addView(editor) }
                assertTrue(editor.requestFocus())
                assertTrue(editor.setLatex("x_{1}^{2}"))
                editor.model.cursor = lowerCursor(editor)
                val oldInput = connection(editor)
                assertTrue(oldInput.setComposingRegion(0, 1))
                editor.onCursorRequested?.invoke(MathCursor(listOf(MathCursorStep(0, "upper")), 1))
                val input = connection(editor)
                val unchanged = editor.saveDraft().toString()
                assertFalse(oldInput.deleteSurroundingText(1, 0))
                assertFalse(oldInput.deleteSurroundingTextInCodePoints(1, 0))
                assertFalse(oldInput.setComposingText("9", 1))
                assertFalse(oldInput.sendKeyEvent(KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_DEL)))
                assertEquals(unchanged, editor.saveDraft().toString())
                assertTrue(input.deleteSurroundingText(1, 0))
                val scripts = editor.model.expression.children.single() as MathExpression.Scripts
                assertEquals("x", MathLatexSerializer.serialize(scripts.base))
                assertEquals("1", MathLatexSerializer.serialize(requireNotNull(scripts.lower)))
                assertTrue(requireNotNull(scripts.upper).children.isEmpty())
                assertEquals("upper", editor.model.cursor.path.last().slot)
            }
        }
    }

    @Test fun imeDeletionInOtherStructuredSlotsNeverRemovesSiblingContentOrTheSlot() = onMain {
        val fixtures = listOf(
            "x_{1}^{2}" to "upper",
            "\\frac{1}{2}" to "numerator",
            "\\frac{1}{2}" to "denominator",
            "\\sqrt[3]{x}" to "index",
            "\\sqrt[3]{x}" to "radicand",
            "\\lim_{0}{1}" to "condition",
            "\\lim_{0}{1}" to "body",
            "\\begin{matrix}1&2\\\\3&4\\end{matrix}" to "r0c0",
            "\\begin{matrix}1&2\\\\3&4\\end{matrix}" to "r0c1",
            "\\begin{matrix}1&2\\\\3&4\\end{matrix}" to "r1c0",
            "\\begin{matrix}1&2\\\\3&4\\end{matrix}" to "r1c1"
        )
        fixtures.forEach { (source, slot) -> DeleteTransport.entries.forEach { transport ->
            val editor = editorAtSlot(source, slot)
            val node = editor.model.expression.children.single()
            val path = editor.model.cursor.path
            val expected = MathExpression.Sequence(listOf(node.withChildSlot(slot, MathExpression.Sequence())))
            val input = connection(editor)
            assertEquals("Fixture $slot uses one editable character", 1, requireNotNull(input.getTextBeforeCursor(20, 0)).length)
            backspace(input, transport)
            assertEquals("$transport changes only $slot in $source", expected, editor.model.expression)
            assertEquals(path, editor.model.cursor.path)
            assertEquals(0, editor.model.cursor.offset)
            val empty = editor.saveDraft().toString()
            repeat(3) { backspace(input, transport) }
            assertEquals("Repeated empty $slot deletion cannot escape or alter siblings", empty, editor.saveDraft().toString())
            assertTrue(input.commitText("9", 1))
            val inserted = node.withChildSlot(slot, MathExpression.Sequence(listOf(MathExpression.Number("9"))))
            assertEquals("The retained $slot accepts new IME input", MathExpression.Sequence(listOf(inserted)), editor.model.expression)
        } }
    }

    @Test fun newCursorPositionPreservesExactNumericMidpointWithoutSelectingMathematics() = onMain {
        val editor = editorAtLower("x_{12}^{2}")
        val input = connection(editor)
        assertTrue(input.setSelection(1, 1))
        assertNull(editor.model.selectionRange)
        assertEquals("1", input.getTextBeforeCursor(20, 0).toString())
        assertEquals("2", input.getTextAfterCursor(20, 0).toString())
        assertTrue(input.commitText("9", 0))
        assertSlotValues(editor, lower = "192", upper = "2")
        assertEquals("A zero cursor position stays before the inserted character", "1", input.getTextBeforeCursor(20, 0).toString())
        assertEquals("92", input.getTextAfterCursor(20, 0).toString())
        assertTrue(input.deleteSurroundingText(0, 1))
        assertSlotValues(editor, lower = "12", upper = "2")
        assertTrue(input.commitText("3", -1))
        assertSlotValues(editor, lower = "132", upper = "2")
        assertEquals("Negative positions are relative to the insertion start", "", input.getTextBeforeCursor(20, 0).toString())
        assertEquals("132", input.getTextAfterCursor(20, 0).toString())
        val start = editor.model.expression
        assertTrue(input.deleteSurroundingText(1, 0))
        assertEquals("Backspace at numeric offset zero stays inside this slot", start, editor.model.expression)
        assertTrue(input.setSelection(1, 1))
        assertTrue(input.commitText("4", 2))
        assertSlotValues(editor, lower = "1432", upper = "2")
        assertEquals("Positive positions account for text after insertion", "143", input.getTextBeforeCursor(20, 0).toString())
        assertEquals("2", input.getTextAfterCursor(20, 0).toString())
        assertNull(editor.model.selectionRange)
    }

    @Test @SdkSuppress(minSdkVersion = 34)
    fun explicitReplaceTextReplacesANumericRangeWithoutTouchingAnotherSlot() = onMain {
        val editor = editorAtLower("x_{123}^{2}")
        val input = connection(editor)
        assertTrue(input.replaceText(1, 2, "9", 1, null))
        assertSlotValues(editor, lower = "193", upper = "2")
        assertNull("Android's replacement range is not a mathematical selection", editor.model.selectionRange)
        assertEquals("19", input.getTextBeforeCursor(20, 0).toString())
        assertEquals("3", input.getTextAfterCursor(20, 0).toString())
        assertTrue(input.replaceText(0, 2, "", 0, null))
        assertSlotValues(editor, lower = "3", upper = "2")
        assertEquals("", input.getTextBeforeCursor(20, 0).toString())
        assertEquals("3", input.getTextAfterCursor(20, 0).toString())
        val valid = editor.model.expression
        assertFalse(input.replaceText(-1, 1, "8", 1, null))
        assertEquals(valid, editor.model.expression)
    }

    @Test fun batchEditableAndExtractedTextAreSnapshotsWhileCompositionEditsOnlyTheAstSlot() = onMain {
        val editor = editorAtLower("x_{12}^{2}")
        val input = connection(editor)
        assertTrue(input.beginBatchEdit())
        assertTrue(input.beginBatchEdit())
        assertTrue(input.setComposingRegion(0, 2))
        assertNull(editor.model.selectionRange)
        val nativeConnection = input as BaseInputConnection
        val snapshot = requireNotNull(nativeConnection.editable)
        assertEquals("12", snapshot.toString())
        assertEquals(0, BaseInputConnection.getComposingSpanStart(snapshot))
        assertEquals(2, BaseInputConnection.getComposingSpanEnd(snapshot))
        assertEquals(2, Selection.getSelectionStart(snapshot))
        assertEquals(2, Selection.getSelectionEnd(snapshot))
        snapshot.replace(0, 2, "999")
        assertSlotValues(editor, lower = "12", upper = "2")
        assertEquals("Mutating the IME snapshot cannot become another source of truth", "12", nativeConnection.editable.toString())
        assertTrue(input.setComposingText("123", 1))
        assertTrue(input.setComposingText("123", 1))
        assertSlotValues(editor, lower = "123", upper = "2")
        assertNull(editor.model.selectionRange)
        val extracted = requireNotNull(input.getExtractedText(ExtractedTextRequest(), 0))
        assertEquals("123", extracted.text.toString())
        assertEquals(3, extracted.selectionStart)
        assertEquals(3, extracted.selectionEnd)
        assertEquals(0, BaseInputConnection.getComposingSpanStart(requireNotNull(nativeConnection.editable)))
        assertEquals(3, BaseInputConnection.getComposingSpanEnd(requireNotNull(nativeConnection.editable)))
        assertTrue(input.deleteSurroundingText(1, 0))
        assertSlotValues(editor, lower = "12", upper = "2")
        assertTrue(input.setComposingText("12", 1))
        assertTrue(input.commitText("12", 1))
        assertTrue(input.finishComposingText())
        assertSlotValues(editor, lower = "12", upper = "2")
        assertEquals(-1, BaseInputConnection.getComposingSpanStart(requireNotNull(nativeConnection.editable)))
        assertTrue(input.endBatchEdit())
        assertFalse("The outer batch ends with no more pending nested batch edits", input.endBatchEdit())
        assertFalse(input.endBatchEdit())
    }

    @Test fun surroundingDeletionPreservesPrivateImeSelectionWhileDelDeletesThatSelection() = onMain {
        listOf(DeleteTransport.SURROUNDING_TEXT, DeleteTransport.CODE_POINTS).forEach { transport ->
            val editor = MathExpressionEditor(context)
            assertTrue(editor.setLatex("1234"))
            enterRootContent(editor)
            val input = connection(editor)
            assertTrue(input.setSelection(1, 3))
            assertEquals("23", input.getSelectedText(0).toString())
            assertNull("The native protocol range remains separate from AST selection", editor.model.selectionRange)
            backspace(input, transport)
            assertEquals("Surrounding deletion removes text before the selection", "234", editor.getLatex())
            assertEquals("23", input.getSelectedText(0).toString())
            assertNull(editor.model.selectionRange)
            assertEquals("", input.getTextBeforeCursor(20, 0).toString())
            assertEquals("4", input.getTextAfterCursor(20, 0).toString())
            backspace(input, DeleteTransport.KEY_EVENT)
            assertEquals("KEYCODE_DEL deletes the private selected range once", "4", editor.getLatex())
            assertNull(input.getSelectedText(0))
            assertNull(editor.model.selectionRange)
            assertEquals("", input.getTextBeforeCursor(20, 0).toString())
            assertEquals("4", input.getTextAfterCursor(20, 0).toString())
        }
    }

    @Test fun codePointDeletionLeavesUnpairedSurrogatesUntouchedAndDeletesAValidPairOnce() = onMain {
        fun editorWithSymbol(value: String, offset: Int): MathExpressionEditor {
            val editor = MathExpressionEditor(context)
            val scripts = MathExpression.Scripts(
                MathExpression.Sequence(listOf(MathExpression.Variable("x"))),
                upper = MathExpression.Sequence(listOf(MathExpression.Number("2"))),
                lower = MathExpression.Sequence(listOf(MathExpression.Symbol(value)))
            )
            editor.model.setExpression(MathExpression.Sequence(listOf(scripts)),
                MathCursor(listOf(MathCursorStep(0, "lower")), offset))
            return editor
        }
        for (invalid in listOf("\uD83D", "\uDC00")) for (atEnd in listOf(false, true)) {
            val editor = editorWithSymbol(invalid, if (atEnd) 1 else 0)
            val input = connection(editor)
            val expression = editor.model.expression
            val cursor = editor.model.cursor
            // Android's BaseInputConnection acknowledges even an invalid-surrogate no-op.
            // The essential contract is atomic preservation, regardless of that return value.
            input.deleteSurroundingTextInCodePoints(if (atEnd) 1 else 0, if (atEnd) 0 else 1)
            assertEquals("Invalid UTF-16 cannot cause a partial code-point deletion", expression, editor.model.expression)
            assertEquals(cursor, editor.model.cursor)
        }
        for (atEnd in listOf(false, true)) {
            val editor = editorWithSymbol("\uD83D\uDE00", if (atEnd) 1 else 0)
            val input = connection(editor)
            assertTrue(input.deleteSurroundingTextInCodePoints(if (atEnd) 1 else 0, if (atEnd) 0 else 1))
            assertScripts(editor, lower = "", upper = "2")
        }
    }

    private fun editorAtLower(latex: String) = MathExpressionEditor(context).also { editor ->
        assertTrue(editor.setLatex(latex))
        editor.model.cursor = lowerCursor(editor)
    }

    private fun editorAtSlot(latex: String, slot: String) = MathExpressionEditor(context).also { editor ->
        assertTrue("Supported fixture $latex", editor.setLatex(latex))
        val sequence = editor.model.expression.children.single().childSlots().single { it.first == slot }.second
        editor.model.cursor = MathCursor(listOf(MathCursorStep(0, slot)), sequence.children.size)
    }

    private fun assertSlotValues(editor: MathExpressionEditor, lower: String, upper: String) {
        val scripts = editor.model.expression.children.single() as MathExpression.Scripts
        assertEquals("x", MathLatexSerializer.serialize(scripts.base))
        assertEquals(lower, MathLatexSerializer.serialize(requireNotNull(scripts.lower)))
        assertEquals(upper, MathLatexSerializer.serialize(requireNotNull(scripts.upper)))
        assertEquals(listOf(MathCursorStep(0, "lower")), editor.model.cursor.path)
    }

    private fun lowerCursor(editor: MathExpressionEditor): MathCursor {
        val scripts = editor.model.expression.children.single() as MathExpression.Scripts
        return MathCursor(listOf(MathCursorStep(0, "lower")), requireNotNull(scripts.lower).children.size)
    }

    private fun assertScripts(editor: MathExpressionEditor, lower: String, upper: String?) {
        val node = editor.model.expression.children.singleOrNull()
        assertTrue("Subscript structure remains editable: ${editor.getLatex()}", node is MathExpression.Scripts)
        val scripts = node as MathExpression.Scripts
        assertEquals("x", MathLatexSerializer.serialize(scripts.base))
        assertNotNull("The lower slot must not disappear during IME deletion", scripts.lower)
        assertEquals(lower, MathLatexSerializer.serialize(requireNotNull(scripts.lower)))
        if (upper == null) assertNull(scripts.upper)
        else assertEquals(upper, MathLatexSerializer.serialize(requireNotNull(scripts.upper)))
        assertEquals(listOf(MathCursorStep(0, "lower")), editor.model.cursor.path)
        assertEquals(requireNotNull(scripts.lower).children.size, editor.model.cursor.offset)
        assertFalse("Soft Backspace cannot insert placeholders", editor.getLatex().contains('□'))
    }

    private fun connection(editor: MathExpressionEditor) = requireNotNull(editor.onCreateInputConnection(EditorInfo()))

    private fun enterRootContent(editor: MathExpressionEditor) {
        val end = editor.model.expression.children.size
        editor.model.cursor = MathCursor(offset = end, level = MathCursorLevel.INTERIOR,
            scope = MathContentScope.Atom(0, end))
    }

    private fun backspace(input: InputConnection, transport: DeleteTransport) {
        when (transport) {
            DeleteTransport.SURROUNDING_TEXT -> assertTrue(input.deleteSurroundingText(1, 0))
            DeleteTransport.CODE_POINTS -> assertTrue(input.deleteSurroundingTextInCodePoints(1, 0))
            DeleteTransport.KEY_EVENT -> {
                assertTrue(input.sendKeyEvent(KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_DEL)))
                assertTrue("KEYCODE_DEL up never inserts characters", input.sendKeyEvent(KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_DEL)))
            }
        }
    }

    private fun intent() = Intent(context, LessonActivity::class.java).putExtra("routeId", "math_physics")
        .putExtra("courseId", "math_course_01").putExtra("unitId", "math_course_01__arithmetic_fundamentals")
        .putExtra("lessonId", "math_course_01__arithmetic_fundamentals__numbers_operations").putExtra("lessonNumber", 1)

    private fun onMain(body: () -> Unit) = instrumentation.runOnMainSync { body() }
}
