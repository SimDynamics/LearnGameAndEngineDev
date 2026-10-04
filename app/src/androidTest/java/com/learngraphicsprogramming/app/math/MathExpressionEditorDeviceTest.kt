package com.learngraphicsprogramming.app.math

import android.content.Context
import android.content.Intent
import android.content.pm.ActivityInfo
import android.content.res.Configuration
import android.graphics.Bitmap
import android.graphics.Rect
import android.os.ParcelFileDescriptor
import android.os.SystemClock
import android.view.KeyEvent
import android.view.View
import android.view.ViewGroup
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputConnection
import android.view.inputmethod.InputMethodManager
import android.widget.Button
import android.widget.EditText
import android.widget.TextView
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.LessonActivity
import com.learngraphicsprogramming.app.LessonTextSizeSettingsRepository
import com.learngraphicsprogramming.app.ProgressRepository
import com.learngraphicsprogramming.app.R
import org.json.JSONArray
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/** Tests the native input connection and composed lesson lifecycle, preserving user state. */
@RunWith(AndroidJUnit4::class)
class MathExpressionEditorDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val route = "math_physics"
    private val lesson = "math_course_01__arithmetic_fundamentals__math_puzzle_sandbox"
    private val accent = 0xffa9e96f.toInt()
    private val snapshots = linkedMapOf<String, Map<String, *>>()

    @Before fun prepare() {
        for (command in listOf("input keyevent KEYCODE_WAKEUP", "wm dismiss-keyguard")) {
            ParcelFileDescriptor.AutoCloseInputStream(instrumentation.uiAutomation.executeShellCommand(command)).use { it.readBytes() }
        }
        listOf("learning_progress", "navigation_state", "lesson_text_size_settings").forEach {
            snapshots[it] = context.getSharedPreferences(it, Context.MODE_PRIVATE).all.toMap()
        }
        ProgressRepository.resetLesson(context, route, lesson)
        LessonTextSizeSettingsRepository.saveUsesGlobalScale(context, "math_course_01", true)
        LessonTextSizeSettingsRepository.saveGlobalScale(context, 100)
    }

    @After fun restoreUserState() {
        snapshots.forEach { (name, original) ->
            val editor = context.getSharedPreferences(name, Context.MODE_PRIVATE).edit().clear()
            original.forEach { (key, value) ->
                when (value) {
                    is String -> editor.putString(key, value)
                    is Int -> editor.putInt(key, value)
                    is Long -> editor.putLong(key, value)
                    is Float -> editor.putFloat(key, value)
                    is Boolean -> editor.putBoolean(key, value)
                    is Set<*> -> editor.putStringSet(key, value.filterIsInstance<String>().toSet())
                }
            }
            assertTrue(editor.commit())
        }
    }

    @Test fun nativeImeEditsFractionSlotsDeletesEmptyStructureAndPastesSupportedLatex() {
        ActivityScenario.launch<LessonActivity>(intent()).use { scenario ->
            scenario.onActivity { activity ->
                val editor = MathExpressionEditor(activity)
                activity.findViewById<ViewGroup>(R.id.lessonContent).apply { removeAllViews(); addView(editor) }
                assertTrue(editor.requestFocus())
                assertTrue(editor.onCheckIsTextEditor())
                val info = EditorInfo()
                var connection = requireNotNull(editor.onCreateInputConnection(info))
                fun fresh() { connection = requireNotNull(editor.onCreateInputConnection(EditorInfo())) }
                fun perform(action: MathKeyboardAction) { editor.perform(action); fresh() }
                fun setLatex(source: String): Boolean = editor.setLatex(source).also { if (it) fresh() }
                fun nextEditableSlot() {
                    assertTrue(connection.performEditorAction(EditorInfo.IME_ACTION_NEXT))
                    fresh()
                }
                assertTrue(info.imeOptions and EditorInfo.IME_FLAG_NO_EXTRACT_UI != 0)
                assertTrue(info.imeOptions and EditorInfo.IME_FLAG_NO_FULLSCREEN != 0)

                perform(MathKeyboardAction.Fraction)
                assertEquals("\\frac{}{}", editor.getLatex())
                assertFalse(editor.isComplete())
                assertTrue(connection.commitText("x+1", 1))
                assertEquals("\\frac{x+1}{}", editor.getLatex())
                nextEditableSlot()
                assertTrue(connection.commitText("2", 1))
                assertEquals("\\frac{x+1}{2}", editor.getLatex())
                assertTrue(editor.isComplete())
                assertTrue(editor.hasFocus())
                assertTrue(connection.deleteSurroundingText(1, 0))
                assertEquals("\\frac{x+1}{}", editor.getLatex())
                assertFalse(editor.isComplete())

                assertTrue(setLatex(""))
                perform(MathKeyboardAction.Fraction)
                assertTrue(connection.deleteSurroundingText(1, 0))
                assertEquals("IME Backspace preserves an empty editable slot", "\\frac{}{}", editor.getLatex())
                assertEquals("numerator", editor.model.cursor.path.last().slot)
                perform(MathKeyboardAction.Backspace)
                assertEquals("Toolbar Backspace inside an empty slot also preserves its owner", "\\frac{}{}", editor.getLatex())
                perform(MathKeyboardAction.NextSlot)
                assertEquals(MathCursorPresentation.ChildBoundary(listOf(MathCursorStep(0, "numerator"))), editor.model.cursorPresentation)
                perform(MathKeyboardAction.NextSlot)
                assertEquals(MathCursor(listOf(MathCursorStep(0, "denominator")), 0), editor.model.cursor)
                perform(MathKeyboardAction.NextSlot)
                assertEquals(MathCursorPresentation.ChildBoundary(listOf(MathCursorStep(0, "denominator"))), editor.model.cursorPresentation)
                perform(MathKeyboardAction.NextSlot)
                assertEquals(MathCursorLevel.NEUTRAL, editor.model.navigationLevel)
                assertEquals(MathCursorPresentation.ParentBoundary(emptyList(), 0), editor.model.cursorPresentation)
                perform(MathKeyboardAction.Backspace)
                // Whole-owner removal belongs to the neutral Sequence level.
                assertEquals("", editor.getLatex())
                assertTrue(setLatex(""))
                assertTrue(connection.commitText("\\sqrt{x+4}", 1))
                assertEquals("\\sqrt{x+4}", editor.getLatex())
                val supported = editor.getLatex()
                assertFalse(setLatex("\\frac{x}{"))
                assertEquals("Invalid LaTeX preserves the current AST", supported, editor.getLatex())

                assertTrue(setLatex(""))
                assertTrue(connection.commitText("x", 1))
                perform(MathKeyboardAction.Subscript)
                assertTrue(connection.commitText("1", 1))
                nextEditableSlot()
                perform(MathKeyboardAction.Power)
                assertTrue(connection.commitText("2", 1))
                assertEquals("Scripts combine on the same base", "{x}_{1}^{2}", editor.getLatex())
                assertTrue(editor.isComplete())

                assertTrue(setLatex(""))
                editor.configureExpected("\\log{x}")
                perform(MathKeyboardAction.Function("log"))
                val logarithm = editor.model.expression.children.single() as MathExpression.Function
                assertNull("A logarithm without a declared base has no empty base slot", logarithm.base)
                assertTrue(connection.commitText("x", 1))
                assertEquals("\\log{x}", editor.getLatex())
                assertTrue(editor.isComplete())

                assertTrue(setLatex(""))
                editor.configureExpected("\\int{x}")
                perform(MathKeyboardAction.Integral)
                val integral = editor.model.expression.children.single() as MathExpression.LargeOperator
                assertNull("An indefinite integral has no lower placeholder", integral.lower)
                assertNull("An indefinite integral has no upper placeholder", integral.upper)
                assertTrue(connection.commitText("x", 1))
                assertEquals("\\int x", editor.getLatex())
                assertEquals(listOf(integral, MathExpression.Variable("x")), editor.model.expression.children)
                assertTrue(editor.isComplete())

                assertTrue(setLatex(""))
                editor.configureExpected("\\begin{matrix}1&2&3\\end{matrix}")
                perform(MathKeyboardAction.Matrix)
                val matrix = editor.model.expression.children.single() as MathExpression.Matrix
                assertEquals("Template determines row count", 1, matrix.rows.size)
                assertEquals("Template determines column count", 3, matrix.rows.single().size)
                listOf("1", "2", "3").forEachIndexed { index, number ->
                    assertTrue(connection.commitText(number, 1))
                    if (index < 2) nextEditableSlot()
                }
                assertEquals("\\begin{matrix}1&2&3\\end{matrix}", editor.getLatex())
                assertTrue(editor.isComplete())

                assertTrue(setLatex(""))
                perform(MathKeyboardAction.Symbol("θ"))
                assertEquals(MathExpression.Symbol("θ"), editor.model.expression.children.single())
                assertEquals("\\theta{}", editor.getLatex())

                assertTrue(setLatex(""))
                assertTrue(connection.commitText("1", 1))
                assertTrue(connection.commitText("2", 1))
                perform(MathKeyboardAction.Power)
                assertTrue(connection.commitText("3", 1))
                assertEquals("Successive IME digits form one power base", "{12}^{3}", editor.getLatex())

                assertTrue(setLatex(""))
                assertTrue(connection.commitText("e", 1))
                assertEquals("The Android keyboard can insert the Euler constant", MathExpression.Symbol("e"), editor.model.expression.children.single())
                assertTrue(editor.isComplete())

                assertTrue(setLatex("x"))
                assertTrue(connection.setComposingText("y", 1))
                assertTrue(setLatex(""))
                assertTrue(connection.commitText("z", 1))
                assertEquals("Reset cancels the previous composing AST", "z", editor.getLatex())
                val restored = editor.saveDraft()
                assertTrue(connection.setComposingText("y", 1))
                editor.restoreDraft(restored)
                fresh()
                assertTrue(connection.commitText("x", 1))
                assertEquals("Restored drafts cancel the previous composing AST", "zx", editor.getLatex())
                assertTrue(connection.deleteSurroundingText(0, 1))
                assertEquals("Forward deletion at the end preserves the previous atom", "zx", editor.getLatex())
            }
        }
    }

    @Test fun simpleAndStructuredControlsCoexistAndKeepTextAnswerContracts() {
        ActivityScenario.launch<LessonActivity>(intent()).use { scenario ->
            scenario.onActivity { activity ->
                val validator = JSONObject().put("type", "equivalent_expression").put("expected", "x/2")
                val simple = control(activity, "simple", "math_expression_input", JSONObject(), validator)
                val structured = control(activity, "structured", "math_expression_input",
                    JSONObject().put("inputMode", "structured"), validator)
                val compoundConfig = JSONObject().put("fields", JSONArray()
                    .put(JSONObject().put("id", "linear").put("type", "numeric"))
                    .put(JSONObject().put("id", "formula").put("type", "expression").put("inputMode", "structured")))
                val compoundValidator = JSONObject().put("type", "fields").put("fields", JSONObject()
                    .put("linear", JSONObject().put("type", "numeric").put("expected", "3/4"))
                    .put("formula", validator))
                val compound = control(activity, "combined", "math_multi_input", compoundConfig, compoundValidator)
                val root = MathWidgetUi.column(activity).apply {
                    addView(simple.view); addView(structured.view); addView(compound.view)
                    MathInputFocusOrder.configure(this)
                }
                activity.findViewById<ViewGroup>(R.id.lessonContent).apply { removeAllViews(); addView(root) }
                val legacy = tree(simple.view).filterIsInstance<EditText>().single()
                assertTrue("SIMPLE keeps the existing EditText", tree(simple.view).none { it is MathExpressionEditor })
                legacy.setText("0.5x")
                val editor = tree(structured.view).filterIsInstance<MathExpressionEditor>().single()
                assertTrue(tree(structured.view).none { it is EditText })
                assertTrue(editor.setLatex("\\frac{x}{2}"))
                assertEquals(MathAnswer.Text("0.5x"), simple.answer())
                assertEquals(MathAnswer.Text("\\frac{x}{2}"), structured.answer())
                assertTrue(MathValidators.validate(validator, simple.answer()).correct)
                assertTrue(MathValidators.validate(validator, structured.answer()).correct)

                val linear = tree(compound.view).filterIsInstance<EditText>().single()
                val formula = tree(compound.view).filterIsInstance<MathExpressionEditor>().single()
                linear.setText("0,75")
                assertTrue(formula.setLatex("\\frac{x}{2}"))
                assertEquals(MathAnswer.Fields(mapOf("linear" to "0,75", "formula" to "\\frac{x}{2}")), compound.answer())
                assertTrue(MathValidators.validate(compoundValidator, compound.answer()).correct)
                val draft = requireNotNull(compound.saveDraft())
                compound.restore(MathAnswer.Fields(mapOf("linear" to "", "formula" to "")))
                compound.restoreDraft(draft)
                assertEquals("\\frac{x}{2}", formula.getLatex())
                assertTrue(linear.requestFocus())
                linear.onEditorAction(EditorInfo.IME_ACTION_NEXT)
                assertTrue("IME Next moves to the structured field", formula.hasFocus())
            }
        }
    }

    @Test fun incompleteSandboxDraftSurvivesRecreationAndResetThenChecksAndLocks() {
        ProgressRepository.saveCurrentPage(context, route, lesson, 16)
        val block = structuredSandboxBlock()
        var incompleteDraft = ""
        var completeLatex = emptyList<String>()
        ActivityScenario.launch<LessonActivity>(intent()).use { scenario ->
            scenario.onActivity { activity ->
                val content = activity.findViewById<View>(R.id.lessonContent)
                val editor = tree(content).filterIsInstance<MathExpressionEditor>().first()
                assertEquals("", editor.getLatex())
                editor.perform(MathKeyboardAction.Fraction)
                requireNotNull(editor.onCreateInputConnection(EditorInfo())).commitText("x+1", 1)
                assertFalse(editor.isComplete())
                incompleteDraft = editor.saveDraft().toString()
                assertTrue(named(content, "Comprobar").isEnabled)
                named(content, "Comprobar").performClick()
                assertTrue("Incomplete answer remains editable", editor.isEnabled)
                assertFalse(tree(content).filterIsInstance<TextView>().any { it.text == "Correcto ✓" })
            }
            scenario.recreate()
            scenario.onActivity { activity ->
                val content = activity.findViewById<View>(R.id.lessonContent)
                val editors = tree(content).filterIsInstance<MathExpressionEditor>()
                assertEquals("\\frac{x+1}{}", editors.first().getLatex())
                assertEquals("Both AST and structural cursor restore", incompleteDraft, editors.first().saveDraft().toString())
                assertFalse(editors.first().isComplete())
                named(content, "Reiniciar").performClick()
                assertTrue(editors.all { it.getLatex().isEmpty() && it.isEnabled })
                assertFalse(named(content, "Comprobar").isEnabled)
                fillSandboxExamples(block, editors)
                completeLatex = editors.map { it.getLatex() }
                named(content, "Comprobar").performClick()
                assertTrue(tree(content).filterIsInstance<TextView>().any { it.text == "Correcto ✓" })
                assertTrue(editors.all { !it.isEnabled })
            }
            scenario.recreate()
            scenario.onActivity { activity ->
                val content = activity.findViewById<View>(R.id.lessonContent)
                val editors = tree(content).filterIsInstance<MathExpressionEditor>()
                assertEquals(completeLatex, editors.map { it.getLatex() })
                assertTrue(editors.all { !it.isEnabled })
                named(content, "Reiniciar").performClick()
                assertTrue(editors.all { it.isEnabled && it.getLatex().isEmpty() })
            }
        }
    }

    @Test fun actualImeToolbarKeepsStructuredFieldAndItsCheckButtonVisible() {
        val block = JSONObject().put("type", "math_puzzle").put("schemaVersion", 1).put("id", "structured_ime_test")
            .put("prompt", "Escribe la fracción con el editor.")
            .put("responses", JSONArray().put(JSONObject().put("id", "answer").put("type", "math_expression_input")
                .put("inputMode", "structured").put("validator", JSONObject().put("type", "latex").put("expected", "\\frac{x+1}{2}"))))
        ActivityScenario.launch<LessonActivity>(intent()).use { scenario ->
            orient(scenario, ActivityInfo.SCREEN_ORIENTATION_PORTRAIT, Configuration.ORIENTATION_PORTRAIT)
            lateinit var editor: MathExpressionEditor
            lateinit var toolbar: MathKeyboardView
            lateinit var check: View
            scenario.onActivity { activity ->
                val puzzle = MathPuzzleRenderer(activity, route, lesson, 0, 200, block) {}
                activity.findViewById<ViewGroup>(R.id.lessonContent).apply { removeAllViews(); addView(puzzle.view) }
                editor = tree(puzzle.view).filterIsInstance<MathExpressionEditor>().single()
                toolbar = tree(puzzle.view).filterIsInstance<MathKeyboardView>().single()
                check = tree(puzzle.view).single { it.tag == MathPuzzleRenderer.CHECK_ACTION_TAG }
                editor.perform(MathKeyboardAction.Fraction)
                editor.requestFocus()
                (activity.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager)
                    .showSoftInput(editor, InputMethodManager.SHOW_IMPLICIT)
            }
            var shown = false
            repeat(15) {
                if (!shown) {
                    SystemClock.sleep(200)
                    scenario.onActivity { activity ->
                        shown = ViewCompat.getRootWindowInsets(activity.window.decorView)?.isVisible(WindowInsetsCompat.Type.ime()) == true
                        if (!shown && activity.hasWindowFocus()) {
                            editor.requestFocus()
                            (activity.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager)
                                .showSoftInput(editor, InputMethodManager.SHOW_IMPLICIT)
                        }
                    }
                }
            }
            assertTrue("Android IME opens for the native structured View", shown)
            instrumentation.waitForIdleSync()
            SystemClock.sleep(350)
            scenario.onActivity { activity ->
                assertTrue(editor.hasFocus())
                assertTrue("Structured toolbar attached above the real IME", toolbar.isAttachedToWindow)
                assertEquals(View.VISIBLE, toolbar.visibility)
                val barBounds = screenBounds(toolbar)
                val editorBounds = screenBounds(editor)
                val checkBounds = screenBounds(check)
                val visible = Rect()
                assertTrue(editor.getGlobalVisibleRect(visible))
                assertEquals("Entire editor remains visible", editor.height, visible.height())
                assertTrue(check.getGlobalVisibleRect(visible))
                assertEquals("Same puzzle Comprobar remains visible", check.height, visible.height())
                val frame = Rect()
                activity.window.decorView.getWindowVisibleDisplayFrame(frame)
                assertEquals("Math bar ends just above IME", frame.bottom.toDouble(), barBounds.bottom.toDouble(), 3.0)
                assertTrue(editorBounds.bottom <= barBounds.top)
                assertTrue(checkBounds.bottom <= barBounds.top)
                val buttons = tree(toolbar).filterIsInstance<Button>()
                assertTrue(buttons.any { it.contentDescription == "Insertar fracción" })
                val minimumKeySize = MathWidgetUi.dp(activity, 48)
                assertTrue("Structured actions retain accessible touch targets", buttons.all {
                    it.width >= minimumKeySize - 1 && it.height >= minimumKeySize - 1
                })
                assertTrue("Structured previews remain compact above the IME", buttons.all {
                    it.width <= MathWidgetUi.dp(activity, 180) && it.height <= MathWidgetUi.dp(activity, 120)
                })
                assertTrue("Simple symbol keys retain their square targets", buttons.filterNot {
                    it is MathKeyboardActionPreviewView
                }.all { kotlin.math.abs(it.width - it.height) <= 2 })
                buttons.filterIsInstance<MathKeyboardActionPreviewView>().forEach { preview ->
                    assertTrue("The real popup fits the full mathematical preview vertically",
                        preview.height >= kotlin.math.ceil(preview.previewLayout.height).toInt() + preview.paddingTop + preview.paddingBottom)
                    assertTrue("The real popup fits the full mathematical preview horizontally",
                        preview.width >= kotlin.math.ceil(preview.previewLayout.width).toInt() + preview.paddingLeft + preview.paddingRight)
                    val accessibility = preview.createAccessibilityNodeInfo()
                    try {
                        assertEquals("android.widget.Button", accessibility.className.toString())
                        assertEquals(preview.contentDescription.toString(), accessibility.contentDescription.toString())
                        assertTrue(accessibility.isClickable)
                    } finally { accessibility.recycle() }
                }
                assertFalse(buttons.any { it.text.toString().matches(Regex("[0-9xy]")) })
            }
            screenshot("structured-ime-portrait.png")
            orient(scenario, ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE, Configuration.ORIENTATION_LANDSCAPE)
            instrumentation.waitForIdleSync()
            SystemClock.sleep(350)
            scenario.onActivity { activity ->
                assertTrue("The small landscape viewport settles without losing window focus", activity.hasWindowFocus())
                assertTrue("The structured field retains focus after rotation: attached=${editor.isAttachedToWindow}, " +
                    "currentFocus=${activity.currentFocus}, model=${editor.model.cursor}", editor.hasFocus())
                assertTrue(toolbar.isAttachedToWindow)
                assertEquals(View.VISIBLE, toolbar.visibility)
                assertEquals("\\frac{}{}", editor.getLatex())
                val frame = Rect()
                activity.window.decorView.getWindowVisibleDisplayFrame(frame)
                assertEquals(frame.bottom.toDouble(), screenBounds(toolbar).bottom.toDouble(), 3.0)
            }
            scenario.onActivity {
                // LEFT follows the declared numerator→denominator order through the
                // denominator's own border, rather than using an obsolete RIGHT shortcut.
                editor.perform(MathKeyboardAction.PreviousSlot)
                editor.perform(MathKeyboardAction.PreviousSlot)
                assertEquals("denominator", editor.model.cursor.path.last().slot)
                requireNotNull(editor.onCreateInputConnection(EditorInfo())).commitText("2", 1)
            }
            instrumentation.waitForIdleSync()
            SystemClock.sleep(200)
            scenario.onActivity {
                val caret = requireNotNull(editor.expressionLayout.caretBounds(editor.model.cursor))
                val bounds = screenBounds(editor)
                assertTrue("The lower slot remains editable in a small viewport",
                    bounds.top + editor.expressionBaseline + caret.bottom <= screenBounds(toolbar).top)
                assertEquals("\\frac{}{2}", editor.getLatex())
                assertTrue(toolbar.isAttachedToWindow)
            }
            screenshot("structured-ime-landscape.png")
        }
    }

    private fun orient(scenario: ActivityScenario<LessonActivity>, requested: Int, expected: Int) {
        scenario.onActivity { it.requestedOrientation = requested }
        var settled = false
        repeat(25) {
            if (!settled) {
                SystemClock.sleep(100)
                scenario.onActivity { settled = it.resources.configuration.orientation == expected && it.hasWindowFocus() }
            }
        }
        assertTrue("Test window reaches the requested orientation", settled)
    }

    private fun screenshot(name: String) {
        val bitmap = instrumentation.uiAutomation.takeScreenshot() ?: error("No screenshot of structured editor")
        if (android.os.Build.VERSION.SDK_INT >= 31) {
            ParcelFileDescriptor.AutoCloseInputStream(instrumentation.uiAutomation.executeShellCommand("mkdir -p /data/local/tmp/math-puzzle-smoke")).use { it.readBytes() }
            val streams = instrumentation.uiAutomation.executeShellCommandRw("dd of=/data/local/tmp/math-puzzle-smoke/$name")
            ParcelFileDescriptor.AutoCloseOutputStream(streams[1]).use { assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) }
            ParcelFileDescriptor.AutoCloseInputStream(streams[0]).use { it.readBytes() }
        }
        bitmap.recycle()
    }

    private fun control(activity: LessonActivity, id: String, type: String, config: JSONObject, validator: JSONObject) =
        requireNotNull(MathInputControls.create(activity, MathResponseSpec(id, type, config, validator), accent) {})

    private fun structuredSandboxBlock(): JSONObject {
        val path = "content/es/math_physics/math_course_01/arithmetic_fundamentals/lesson_math_puzzle_sandbox.json"
        val lessonJson = JSONObject(context.assets.open(path).bufferedReader().use { it.readText() })
        val blocks = lessonJson.getJSONArray("pages").getJSONObject(16).getJSONArray("blocks")
        return (0 until blocks.length()).map { blocks.getJSONObject(it) }.single { it.optString("type") == "math_puzzle" }
    }

    private fun fillSandboxExamples(block: JSONObject, editors: List<MathExpressionEditor>) {
        val response = block.getJSONArray("responses").getJSONObject(0)
        val definitions = response.getJSONArray("fields")
        val validators = response.getJSONObject("validator").getJSONObject("fields")
        assertEquals(definitions.length(), editors.size)
        editors.forEachIndexed { index, editor ->
            val id = definitions.getJSONObject(index).getString("id")
            assertTrue("Supported example $id", editor.setLatex(validators.getJSONObject(id).getString("expected")))
            assertTrue("Complete example $id", editor.isComplete())
        }
    }

    private fun arrow(connection: InputConnection, key: Int) {
        assertTrue(connection.sendKeyEvent(KeyEvent(KeyEvent.ACTION_DOWN, key)))
        connection.sendKeyEvent(KeyEvent(KeyEvent.ACTION_UP, key))
    }

    private fun named(view: View, text: String): View = tree(view).first {
        (it is TextView && it.text.toString() == text) || it.contentDescription?.toString() == text
    }

    private fun screenBounds(view: View): Rect = IntArray(2).let { location ->
        view.getLocationOnScreen(location)
        Rect(location[0], location[1], location[0] + view.width, location[1] + view.height)
    }

    private fun tree(view: View): List<View> = listOf(view) + if (view is ViewGroup)
        (0 until view.childCount).flatMap { tree(view.getChildAt(it)) } else emptyList()

    private fun intent() = Intent(context, LessonActivity::class.java)
        .putExtra("routeId", route).putExtra("courseId", "math_course_01")
        .putExtra("unitId", "arithmetic_fundamentals").putExtra("lessonId", lesson).putExtra("lessonNumber", 4)
}
