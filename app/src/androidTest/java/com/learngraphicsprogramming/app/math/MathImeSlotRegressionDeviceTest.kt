package com.learngraphicsprogramming.app.math

import android.content.Context
import android.content.Intent
import android.os.ParcelFileDescriptor
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputConnection
import android.widget.EditText
import android.widget.TextView
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.LessonActivity
import com.learngraphicsprogramming.app.ProgressRepository
import com.learngraphicsprogramming.app.R
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class MathImeSlotRegressionDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val lesson = "math_course_01__arithmetic_fundamentals__math_puzzle_sandbox"
    private val snapshots = linkedMapOf<String, Map<String, *>>()

    @Before fun prepare() {
        listOf("input keyevent KEYCODE_WAKEUP", "wm dismiss-keyguard").forEach { command ->
            ParcelFileDescriptor.AutoCloseInputStream(instrumentation.uiAutomation.executeShellCommand(command)).use { it.readBytes() }
        }
        listOf("learning_progress", "navigation_state", "lesson_text_size_settings").forEach {
            snapshots[it] = context.getSharedPreferences(it, Context.MODE_PRIVATE).all.toMap()
        }
        ProgressRepository.resetLesson(context, "math_physics", lesson)
    }

    @After fun restorePreferences() {
        snapshots.forEach { (name, values) ->
            val editor = context.getSharedPreferences(name, Context.MODE_PRIVATE).edit().clear()
            values.forEach { (key, value) -> when (value) {
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

    @Test fun repeatedComposingSlotChangesCannotRestoreAnOldPathOrDuplicateCommits() {
        ActivityScenario.launch<LessonActivity>(intent()).use { scenario -> scenario.onActivity { activity ->
            val editor = installEditor(activity)
            editor.configureExpected("\\log_{2}{x}")
            editor.perform(MathKeyboardAction.Function("log"))
            var connection = connection(editor)
            assertTrue(connection.setComposingText("2", 1))
            assertTrue(connection.setComposingText("2", 1))
            assertEquals("\\log_{2}{}", editor.getLatex())
            val stale = connection
            selectSlot(editor, "argument")
            connection = connection(editor)
            assertFalse("An obsolete IME session cannot replay base text", stale.commitText("2", 1))
            assertTrue(connection.setComposingText("x", 1))
            assertTrue(connection.setComposingText("xy", 1))
            assertTrue(connection.setSelection(2, 2))
            assertTrue(connection.commitText("xy", 1))
            assertEquals("\\log_{2}{xy}", editor.getLatex())
            repeat(6) { index ->
                selectSlot(editor, if (index % 2 == 0) "base" else "argument")
                connection = connection(editor)
                assertTrue(connection.commitText(if (index % 2 == 0) "3" else "z", 1))
            }
            assertEquals("\\log_{2333}{xyzzz}", editor.getLatex())

            editor.setLatex("")
            connection = connection(editor)
            assertTrue(connection.commitText("3", 1))
            assertTrue(connection.commitText("3", 1))
            assertEquals("Two intentional key presses still produce two digits", "33", editor.getLatex())
            editor.setLatex("")
            connection = connection(editor)
            assertTrue(connection.sendKeyEvent(KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_3)))
            assertTrue(connection.sendKeyEvent(KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_3)))
            assertEquals("A single down/up event produces one digit", "3", editor.getLatex())
            assertEquals("3", connection.getTextBeforeCursor(10, 0).toString())
        } }
    }

    @Test fun imeNextExhaustsScriptsAndFractionBeforeTheNextExternalField() {
        ActivityScenario.launch<LessonActivity>(intent()).use { scenario -> scenario.onActivity { activity ->
            val editor = MathExpressionEditor(activity)
            val next = EditText(activity)
            val root = MathWidgetUi.column(activity).apply { addView(editor); addView(next); MathInputFocusOrder.configure(this) }
            activity.findViewById<ViewGroup>(R.id.lessonContent).apply { removeAllViews(); addView(root) }
            editor.setLatex("x_{1}^{2}")
            editor.requestFocus()
            selectSlot(editor, "base")
            assertTrue(connection(editor).performEditorAction(EditorInfo.IME_ACTION_NEXT))
            assertEquals("upper", editor.model.cursor.path.last().slot)
            assertTrue(editor.hasFocus())
            assertTrue(connection(editor).performEditorAction(EditorInfo.IME_ACTION_NEXT))
            assertEquals("lower", editor.model.cursor.path.last().slot)
            assertTrue(editor.hasFocus())
            assertTrue(connection(editor).performEditorAction(EditorInfo.IME_ACTION_NEXT))
            assertTrue(editor.model.cursor.path.isEmpty())
            assertTrue(editor.hasFocus())
            assertTrue(connection(editor).performEditorAction(EditorInfo.IME_ACTION_NEXT))
            assertTrue(next.hasFocus())

            editor.requestFocus()
            editor.setLatex("\\frac{x}{2}")
            selectSlot(editor, "numerator")
            assertTrue(connection(editor).performEditorAction(EditorInfo.IME_ACTION_NEXT))
            assertEquals("denominator", editor.model.cursor.path.last().slot)
            assertTrue(editor.hasFocus())
        } }
    }

    @Test fun restoredDraftAndTouchedSlotsRemainAuthoritativeForRootsGroupsAndScripts() {
        ActivityScenario.launch<LessonActivity>(intent()).use { scenario -> scenario.onActivity { activity ->
            val editor = installEditor(activity)
            editor.setLatex("\\sqrt[3]{x}")
            selectSlot(editor, "index")
            connection(editor).setComposingText("4", 1)
            selectSlot(editor, "radicand")
            val draft = editor.saveDraft()
            editor.setLatex("")
            editor.restoreDraft(draft)
            assertEquals("radicand", editor.model.cursor.path.last().slot)
            assertTrue(connection(editor).commitText("+1", 1))
            assertEquals("\\sqrt[34]{x+1}", editor.getLatex())
            editor.setLatex("")
            connection(editor).commitText("x", 1)
            editor.perform(MathKeyboardAction.Power)
            connection(editor).setComposingText("2", 1)
            selectSlot(editor, "base")
            editor.perform(MathKeyboardAction.Subscript)
            connection(editor).commitText("1", 1)
            assertEquals("{x}_{1}^{2}", editor.getLatex())
            editor.perform(MathKeyboardAction.Backspace)
            editor.perform(MathKeyboardAction.Backspace)
            assertEquals("Backspace inside the empty lower slot preserves its placeholder", "{x}_{}^{2}", editor.getLatex())
            editor.perform(MathKeyboardAction.NextSlot)
            editor.perform(MathKeyboardAction.Backspace)
            assertEquals("{x}^{2}", editor.getLatex())
            editor.setLatex("")
            editor.perform(MathKeyboardAction.Parentheses)
            val groupInput = connection(editor)
            assertTrue(groupInput.setComposingText("x", 1))
            // Finalize x before appending a new token; a factory query does not finish a word.
            assertTrue(groupInput.finishComposingText())
            assertTrue(groupInput.commitText("+1", 1))
            assertEquals("\\left(x+1\\right)", editor.getLatex())
        } }
    }

    @Test fun sandboxPageSeventeenAnswersBuiltWithImeAndActionsValidateAndStaySpanish() {
        ProgressRepository.saveCurrentPage(context, "math_physics", lesson, 16)
        ActivityScenario.launch<LessonActivity>(intent()).use { scenario -> scenario.onActivity { activity ->
            val content = activity.findViewById<View>(R.id.lessonContent)
            val editors = tree(content).filterIsInstance<MathExpressionEditor>()
            assertEquals(8, editors.size)
            fun type(editor: MathExpressionEditor, text: String) { assertTrue(connection(editor).commitText(text, 1)) }
            fun nextSlot(editor: MathExpressionEditor) { assertTrue(connection(editor).performEditorAction(EditorInfo.IME_ACTION_NEXT)) }
            editors[0].apply { requestFocus(); perform(MathKeyboardAction.Fraction); type(this, "x+1"); nextSlot(this); type(this, "2") }
            editors[1].apply { requestFocus(); type(this, "x"); perform(MathKeyboardAction.Power); type(this, "2"); selectSlot(this, "base"); perform(MathKeyboardAction.Subscript); type(this, "1") }
            editors[2].apply { requestFocus(); perform(MathKeyboardAction.Root); type(this, "x") }
            editors[3].apply { requestFocus(); perform(MathKeyboardAction.Fraction); type(this, "1"); nextSlot(this); perform(MathKeyboardAction.NthRoot); type(this, "3"); nextSlot(this); type(this, "x+4") }
            editors[4].apply { requestFocus(); perform(MathKeyboardAction.Parentheses); type(this, "x+1") }
            editors[5].apply { requestFocus(); perform(MathKeyboardAction.AbsoluteValue); type(this, "x") }
            editors[6].apply { requestFocus(); perform(MathKeyboardAction.Function("sin")); type(this, "x"); nextSlot(this); perform(MathKeyboardAction.Symbol("+")); perform(MathKeyboardAction.Function("log")); type(this, "2"); nextSlot(this); type(this, "x") }
            editors[7].apply { requestFocus(); perform(MathKeyboardAction.Symbol("π")); type(this, "+e≤2") }
            val asset = "content/es/math_physics/math_course_01/arithmetic_fundamentals/lesson_math_puzzle_sandbox.json"
            val page = JSONObject(context.assets.open(asset).bufferedReader().use { it.readText() }).getJSONArray("pages").getJSONObject(16)
            val blocks = page.getJSONArray("blocks")
            val puzzle = (0 until blocks.length()).map { blocks.getJSONObject(it) }.single { it.optString("type") == "math_puzzle" }
            val response = puzzle.getJSONArray("responses").getJSONObject(0)
            val fields = response.getJSONArray("fields")
            val validators = response.getJSONObject("validator").getJSONObject("fields")
            editors.forEachIndexed { index, editor ->
                val id = fields.getJSONObject(index).getString("id")
                assertTrue("Actual edited $id: ${editor.getLatex()}", MathValidators.validate(validators.getJSONObject(id), MathAnswer.Text(editor.getLatex())).correct)
            }
            named(content, "Comprobar").performClick()
            assertTrue(tree(content).filterIsInstance<TextView>().any { it.text == "Correcto ✓" })
            named(content, "Reiniciar").performClick()
            editors[1].perform(MathKeyboardAction.Power)
            named(content, "Comprobar").performClick()
            val visibleText = tree(content).filterIsInstance<TextView>().joinToString("\n") { it.text.toString() }
            assertFalse(visibleText.contains("expressions:"))
            assertFalse(visibleText.contains("scripts:"))
        } }
    }

    private fun installEditor(activity: LessonActivity): MathExpressionEditor = MathExpressionEditor(activity).also { editor ->
        activity.findViewById<ViewGroup>(R.id.lessonContent).apply { removeAllViews(); addView(editor) }
        editor.requestFocus()
    }
    private fun connection(editor: MathExpressionEditor): InputConnection = requireNotNull(editor.onCreateInputConnection(EditorInfo()))
    private fun selectSlot(editor: MathExpressionEditor, slot: String) {
        val sequence = editor.model.expression
        val nodeIndex = sequence.children.indexOfFirst { it.childSlots().any { child -> child.first == slot } }
        assertTrue("Slot $slot exists", nodeIndex >= 0)
        val sequenceSlot = sequence.children[nodeIndex].childSlots().single { it.first == slot }.second
        val cursor = MathCursor(listOf(MathCursorStep(nodeIndex, slot)), sequenceSlot.children.size)
        val bounds = requireNotNull(editor.expressionLayout.caretBounds(cursor))
        val x = editor.expressionOriginX + bounds.centerX()
        val y = editor.expressionBaseline + bounds.centerY()
        val now = android.os.SystemClock.uptimeMillis()
        val down = MotionEvent.obtain(now, now, MotionEvent.ACTION_DOWN, x, y, 0)
        val up = MotionEvent.obtain(now, now + 30, MotionEvent.ACTION_UP, x, y, 0)
        try { editor.dispatchTouchEvent(down); editor.dispatchTouchEvent(up) } finally { down.recycle(); up.recycle() }
        assertEquals("Touched slot is authoritative", slot, editor.model.cursor.path.last().slot)
    }
    private fun tree(view: View): List<View> = listOf(view) + if (view is ViewGroup) (0 until view.childCount).flatMap { tree(view.getChildAt(it)) } else emptyList()
    private fun named(view: View, text: String): View = tree(view).first { it is TextView && it.text.toString() == text || it.contentDescription?.toString() == text }
    private fun intent() = Intent(context, LessonActivity::class.java).putExtra("routeId", "math_physics")
        .putExtra("courseId", "math_course_01").putExtra("unitId", "arithmetic_fundamentals")
        .putExtra("lessonId", lesson).putExtra("lessonNumber", 4)
}
