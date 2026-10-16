package com.learngraphicsprogramming.app

import android.text.InputType
import android.view.ContextThemeWrapper
import android.view.KeyEvent
import android.view.View
import android.view.inputmethod.EditorInfo
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.math.MathExpressionEditor
import com.learngraphicsprogramming.app.math.MathInputFocusOrder
import com.learngraphicsprogramming.app.math.MathKeyboardAction
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

/** Native key/IME paths, without changing saved lessons or starting audio. */
@RunWith(AndroidJUnit4::class)
class PuzzleAnswerSubmitDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context get() = ContextThemeWrapper(instrumentation.targetContext,
        R.style.Theme_LearnGraphicsProgramming)

    @Test fun singleLineEnterAndNumpadSubmitOnceWithoutChangingTheSelectionOrFocus() = onMain {
        val root = LinearLayout(context).apply { orientation = LinearLayout.VERTICAL }
        val input = EditText(context).apply { setSingleLine(true); setText("1234") }
        val following = EditText(context).apply { setSingleLine(true) }
        var checks = 0
        val check = Button(context).apply { setOnClickListener { checks++ } }
        root.addView(input); root.addView(following); root.addView(check)
        assertTrue(PuzzleAnswerSubmit.install(input, check))
        layout(root)
        assertTrue(input.requestFocus())
        input.setSelection(1, 3)
        for (key in listOf(KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_NUMPAD_ENTER)) {
            val before = checks
            assertTrue(input.dispatchKeyEvent(KeyEvent(KeyEvent.ACTION_DOWN, key)))
            assertEquals("A key press submits only once, on release", before, checks)
            assertTrue(input.dispatchKeyEvent(KeyEvent(KeyEvent.ACTION_UP, key)))
            assertEquals(before + 1, checks)
            assertTrue(input.hasFocus())
            assertFalse(following.hasFocus())
            assertEquals("1234", input.text.toString())
            assertEquals(1, input.selectionStart)
            assertEquals(3, input.selectionEnd)
        }
        for (action in listOf(EditorInfo.IME_ACTION_DONE, EditorInfo.IME_ACTION_GO,
            EditorInfo.IME_ACTION_SEND, EditorInfo.IME_ACTION_NEXT)) {
            val before = checks
            input.onEditorAction(action)
            assertEquals(before + 1, checks)
            assertTrue(input.hasFocus())
        }
        check.isEnabled = false
        val before = checks
        input.onEditorAction(EditorInfo.IME_ACTION_DONE)
        assertEquals("A disabled Comprobar never evaluates an answer", before, checks)
    }

    @Test fun multilineCppAndStdinKeepTheirRealNewlinesAndAreNeverBoundToSubmit() = onMain {
        for (cpp in listOf(false, true)) {
            val root = LinearLayout(context)
            val input: EditText = if (cpp) FreeScrollEditText(context) else EditText(context)
            input.inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_MULTI_LINE
            input.setSingleLine(false)
            input.setText("value")
            if (cpp) CppCodeInputRules.install(input)
            root.addView(input)
            layout(root)
            input.requestFocus(); input.setSelection(input.length())
            var checks = 0
            assertFalse(PuzzleAnswerSubmit.install(input) { checks++ })
            assertTrue(input.dispatchKeyEvent(KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_ENTER)))
            input.dispatchKeyEvent(KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_ENTER))
            assertEquals("value\n", input.text.toString())
            assertEquals(0, checks)
        }
    }

    @Test fun inlineCppEnterSubmitsButTabAndItsNativeNextGuardStillInsertOnlyOneTab() = onMain {
        val root = LinearLayout(context)
        val input = FreeScrollEditText(context).apply {
            setSingleLine(true)
            CppCodeInputRules.install(this)
        }
        root.addView(input)
        var checks = 0
        PuzzleAnswerSubmit.install(input) { checks++ }
        layout(root); input.requestFocus()
        assertEquals(EditorInfo.IME_ACTION_DONE, input.imeOptions and EditorInfo.IME_MASK_ACTION)
        assertTrue(input.dispatchKeyEvent(KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_TAB)))
        assertTrue(input.dispatchKeyEvent(KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_TAB)))
        assertEquals("\t", input.text.toString())
        // Some native keyboards follow physical TAB with NEXT; the existing guard
        // must keep that navigation from submitting or adding a second tab.
        input.onEditorAction(EditorInfo.IME_ACTION_NEXT)
        assertEquals("\t", input.text.toString())
        assertEquals(0, checks)
        input.dispatchKeyEvent(KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_ENTER))
        input.dispatchKeyEvent(KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_ENTER))
        assertEquals(1, checks)
        input.onEditorAction(EditorInfo.IME_ACTION_DONE)
        assertEquals(2, checks)
        assertTrue(input.hasFocus())
    }

    @Test fun composedMathOnlySubmitsGradedResponsesAndKeepsStructuredSlotNavigation() = onMain {
        val root = LinearLayout(context).apply { orientation = LinearLayout.VERTICAL }
        val auxiliary = EditText(context).apply { setSingleLine(true) }
        val answer = EditText(context).apply { setSingleLine(true); setText("2") }
        val structured = MathExpressionEditor(context)
        val finalAnswer = EditText(context).apply { setSingleLine(true) }
        val graded = LinearLayout(context).apply {
            orientation = LinearLayout.VERTICAL
            addView(answer); addView(structured); addView(finalAnswer)
        }
        root.addView(auxiliary); root.addView(graded)
        var checks = 0
        MathInputFocusOrder.configure(root, listOf(graded)) { checks++ }
        MathInputFocusOrder.configure(root)
        layout(root)
        auxiliary.requestFocus()
        assertEquals(EditorInfo.IME_ACTION_NEXT, auxiliary.imeOptions and EditorInfo.IME_MASK_ACTION)
        auxiliary.onEditorAction(EditorInfo.IME_ACTION_NEXT)
        assertTrue(answer.hasFocus())
        assertEquals(0, checks)
        assertEquals(EditorInfo.IME_ACTION_DONE, answer.imeOptions and EditorInfo.IME_MASK_ACTION)
        answer.onEditorAction(EditorInfo.IME_ACTION_NEXT)
        assertTrue(answer.hasFocus())
        assertEquals(1, checks)

        structured.requestFocus()
        structured.perform(MathKeyboardAction.Fraction)
        assertEquals("numerator", structured.model.cursor.path.last().slot)
        structured.dispatchKeyEvent(KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_TAB))
        structured.dispatchKeyEvent(KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_TAB))
        assertEquals("denominator", structured.model.cursor.path.last().slot)
        assertEquals("TAB still navigates the expression without evaluating", 1, checks)
        structured.dispatchKeyEvent(KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_ENTER))
        structured.dispatchKeyEvent(KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_ENTER))
        assertEquals(2, checks)
        assertTrue(structured.hasFocus())
        assertEquals("\\frac{}{}", structured.getLatex())
        val connection = requireNotNull(structured.onCreateInputConnection(EditorInfo()))
        assertTrue(connection.performEditorAction(EditorInfo.IME_ACTION_DONE))
        assertEquals(3, checks)
        assertTrue(structured.hasFocus())
    }

    @Test fun pageAndFullscreenReconfigurationKeepEachPuzzlesOwnSubmitAction() = onMain {
        val lesson = LinearLayout(context).apply { orientation = LinearLayout.VERTICAL }
        val firstCard = LinearLayout(context).apply { orientation = LinearLayout.VERTICAL }
        val secondCard = LinearLayout(context).apply { orientation = LinearLayout.VERTICAL }
        val auxiliary = EditText(context).apply { setSingleLine(true) }
        val first = EditText(context).apply { setSingleLine(true); setText("300") }
        val anotherFirstAnswer = EditText(context).apply { setSingleLine(true); setText("cm") }
        val second = EditText(context).apply { setSingleLine(true); setText("2") }
        val answerRow = LinearLayout(context).apply { addView(first); addView(anotherFirstAnswer) }
        firstCard.addView(auxiliary); firstCard.addView(answerRow)
        secondCard.addView(second)
        lesson.addView(firstCard); lesson.addView(secondCard)
        var firstChecks = 0
        var secondChecks = 0
        MathInputFocusOrder.configure(firstCard, listOf(answerRow)) { firstChecks++ }
        MathInputFocusOrder.configure(secondCard, listOf(second)) { secondChecks++ }
        layout(lesson)
        // These are the actual three callers: final lesson layout, entering fullscreen
        // around one puzzle, then restoring the lesson's complete focus order.
        for (host in listOf(lesson, firstCard, lesson)) {
            MathInputFocusOrder.configure(host)
            first.requestFocus()
            first.onEditorAction(EditorInfo.IME_ACTION_NEXT)
            assertTrue(first.hasFocus())
            anotherFirstAnswer.requestFocus()
            anotherFirstAnswer.onEditorAction(EditorInfo.IME_ACTION_DONE)
            assertTrue(anotherFirstAnswer.hasFocus())
            second.requestFocus()
            second.onEditorAction(EditorInfo.IME_ACTION_DONE)
            assertTrue(second.hasFocus())
            auxiliary.requestFocus()
            auxiliary.onEditorAction(EditorInfo.IME_ACTION_NEXT)
            assertTrue("An auxiliary control keeps its existing field navigation", first.hasFocus())
        }
        assertEquals(6, firstChecks)
        assertEquals(3, secondChecks)
    }

    private fun layout(root: View) {
        root.measure(View.MeasureSpec.makeMeasureSpec(900, View.MeasureSpec.EXACTLY),
            View.MeasureSpec.makeMeasureSpec(1400, View.MeasureSpec.AT_MOST))
        root.layout(0, 0, root.measuredWidth, root.measuredHeight)
    }

    private fun onMain(block: () -> Unit) = instrumentation.runOnMainSync(block)
}
