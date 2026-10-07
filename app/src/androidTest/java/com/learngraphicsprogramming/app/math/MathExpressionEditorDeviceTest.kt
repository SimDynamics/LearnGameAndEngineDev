package com.learngraphicsprogramming.app.math

import android.accessibilityservice.AccessibilityServiceInfo
import android.content.Context
import android.content.Intent
import android.content.pm.ActivityInfo
import android.content.res.Configuration
import android.graphics.Rect
import android.os.Parcel
import android.os.ParcelFileDescriptor
import android.os.Parcelable
import android.os.SystemClock
import android.provider.Settings
import android.util.SparseArray
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.view.accessibility.AccessibilityNodeInfo
import android.view.accessibility.AccessibilityWindowInfo
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
                assertEquals("A substructure exits to its owner's border, without a child border",
                    MathCursorPresentation.ParentBoundary(emptyList(), 0), editor.model.cursorPresentation)
                perform(MathKeyboardAction.PreviousSlot)
                assertEquals(MathCursor(listOf(MathCursorStep(0, "numerator")), 0), editor.model.cursor)
                perform(MathKeyboardAction.PreviousSlot)
                assertEquals(MathCursor(listOf(MathCursorStep(0, "denominator")), 0), editor.model.cursor)
                perform(MathKeyboardAction.NextSlot)
                assertEquals("RIGHT retraces the declared substructure order",
                    MathCursor(listOf(MathCursorStep(0, "numerator")), 0), editor.model.cursor)
                perform(MathKeyboardAction.NextSlot)
                assertEquals(MathCursorPresentation.ParentBoundary(emptyList(), 0), editor.model.cursorPresentation)
                perform(MathKeyboardAction.NextSlot)
                assertEquals("Whole-owner removal requires leaving the owner border for the neutral Sequence",
                    MathCursorLevel.NEUTRAL, editor.model.navigationLevel)
                assertEquals(MathCursor(offset = 1), editor.model.cursor)
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

    @Test fun explicitKeyboardPolicyRestoresWithTheMathematicalDraftAndCursor() {
        ActivityScenario.launch<LessonActivity>(intent()).use { scenario ->
            scenario.onActivity { activity ->
                val editor = MathExpressionEditor(activity).apply { id = View.generateViewId() }
                assertTrue("Lesson editors keep their current touch behavior by default", editor.showSystemKeyboardOnTouch)
                editor.showSystemKeyboardOnTouch = false
                editor.perform(MathKeyboardAction.Fraction)
                editor.perform(MathKeyboardAction.Symbol("12"))
                editor.perform(MathKeyboardAction.NextSlot)
                val draft = editor.model.saveDraft()
                val state = SparseArray<Parcelable>()
                editor.saveHierarchyState(state)

                val restored = MathExpressionEditor(activity).apply { id = editor.id }
                restored.restoreHierarchyState(state)
                assertFalse("An explicit-only editor keeps that policy after view restoration", restored.showSystemKeyboardOnTouch)
                assertEquals(draft, restored.model.saveDraft())
                assertEquals(editor.model.expression, restored.model.expression)
                assertEquals(editor.model.cursor, restored.model.cursor)
                assertTrue("Explicit-only policy retains the native editing connection", restored.onCheckIsTextEditor())
                assertNotNull(restored.onCreateInputConnection(EditorInfo()))
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
            lateinit var header: View
            lateinit var progress: View
            var normalToolbarHeight = 0
            var normalEditorPadding = 0 to 0
            var normalTextSize = 0f
            val focusLosses = mutableListOf<String>()
            scenario.onActivity { activity ->
                val puzzle = MathPuzzleRenderer(activity, route, lesson, 0, 200, block) {}
                activity.findViewById<ViewGroup>(R.id.lessonContent).apply { removeAllViews(); addView(puzzle.view) }
                editor = tree(puzzle.view).filterIsInstance<MathExpressionEditor>().single()
                toolbar = tree(puzzle.view).filterIsInstance<MathKeyboardView>().single()
                check = tree(puzzle.view).single { it.tag == MathPuzzleRenderer.CHECK_ACTION_TAG }
                header = activity.findViewById(R.id.lessonHeader)
                progress = activity.findViewById(R.id.lessonProgress)
                editor.perform(MathKeyboardAction.Fraction)
                activity.window.decorView.viewTreeObserver.addOnGlobalFocusChangeListener { oldFocus, newFocus ->
                    if (oldFocus === editor && newFocus !== editor) {
                        val ancestors = generateSequence<View>(editor) { it.parent as? View }.joinToString(" > ") {
                            "${it.javaClass.simpleName}(id=${it.id},height=${it.height},measured=${it.measuredHeight},visibility=${it.visibility})"
                        }
                        focusLosses += "newFocus=$newFocus, ancestors=$ancestors"
                    }
                }
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
            awaitUi(scenario, "Portrait editor and check action fit above the real IME") { activity ->
                isImeVisible(activity) && toolbar.isAttachedToWindow && toolbar.visibility == View.VISIBLE &&
                    screenBounds(editor).bottom <= screenBounds(toolbar).top &&
                    screenBounds(check).bottom <= screenBounds(toolbar).top
            }
            scenario.onActivity { activity ->
                assertTrue(editor.hasFocus())
                assertEquals("Portrait retains the full lesson header with IME open", View.VISIBLE, header.visibility)
                assertTrue("Portrait retains the progress line", progress.isShown)
                assertTrue("Structured toolbar attached above the real IME", toolbar.isAttachedToWindow)
                assertEquals(View.VISIBLE, toolbar.visibility)
                val barBounds = screenBounds(toolbar)
                normalToolbarHeight = toolbar.height
                normalEditorPadding = editor.paddingTop to editor.paddingBottom
                normalTextSize = editor.textSizePx
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
            lateinit var modelBeforeRotation: MathExpressionModel
            lateinit var expressionBeforeRotation: MathExpression.Sequence
            lateinit var cursorBeforeRotation: MathCursor
            lateinit var draftBeforeRotation: String
            scenario.onActivity {
                modelBeforeRotation = editor.model
                expressionBeforeRotation = editor.model.expression
                cursorBeforeRotation = editor.model.cursor
                draftBeforeRotation = editor.model.saveDraft()
            }
            orient(scenario, ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE, Configuration.ORIENTATION_LANDSCAPE)
            awaitUi(scenario, "Landscape hides the lesson header and lays out the same IME toolbar",
                diagnostic = { "focus losses=$focusLosses" }) { activity ->
                val frame = Rect().also { activity.window.decorView.getWindowVisibleDisplayFrame(it) }
                isImeVisible(activity) && header.visibility == View.GONE && toolbar.isAttachedToWindow &&
                    toolbar.visibility == View.VISIBLE && kotlin.math.abs(screenBounds(toolbar).bottom - frame.bottom) <= 3
            }
            scenario.onActivity { activity ->
                assertTrue("The small landscape viewport settles without losing window focus", activity.hasWindowFocus())
                assertTrue("The structured field retains focus after rotation: attached=${editor.isAttachedToWindow}, " +
                    "currentFocus=${activity.currentFocus}, model=${editor.model.cursor}, losses=$focusLosses", editor.hasFocus())
                assertTrue("Rotation never clears focus temporarily: $focusLosses", focusLosses.isEmpty())
                assertSame("Rotation keeps the original editor active", editor, activity.currentFocus)
                assertSame("Rotation keeps the original mathematical model", modelBeforeRotation, editor.model)
                assertSame("Rotation does not rebuild the AST", expressionBeforeRotation, editor.model.expression)
                assertEquals("Rotation preserves the exact mathematical cursor", cursorBeforeRotation, editor.model.cursor)
                assertEquals("Rotation preserves slot and selection state", draftBeforeRotation, editor.model.saveDraft())
                assertEquals("Landscape with real IME removes the entire lesson header", View.GONE, header.visibility)
                assertFalse("The green progress line disappears with the header", progress.isShown)
                assertTrue(toolbar.isAttachedToWindow)
                assertEquals(View.VISIBLE, toolbar.visibility)
                assertEquals("\\frac{}{}", editor.getLatex())
                val frame = Rect()
                activity.window.decorView.getWindowVisibleDisplayFrame(frame)
                assertEquals(frame.bottom.toDouble(), screenBounds(toolbar).bottom.toDouble(), 3.0)
                assertTrue("Landscape math toolbar occupies at most half of the real safe viewport: H=${frame.height()}, bar=${toolbar.height}",
                    toolbar.height <= frame.height() / 2)
                assertEquals("Responsive rendering preserves the lesson text-size setting", normalTextSize, editor.textSizePx, 0f)
                tree(toolbar).filterIsInstance<MathKeyboardActionPreviewView>().forEach { preview ->
                    assertTrue("Compact mode keeps every preview complete",
                        preview.height >= kotlin.math.ceil(preview.previewLayout.height).toInt() + preview.paddingTop + preview.paddingBottom)
                }
            }
            scenario.onActivity {
                // Fraction slots are substructures: LEFT moves directly from the empty
                // numerator to the empty denominator, without an intermediate child border.
                assertEquals("numerator", editor.model.cursor.path.last().slot)
                editor.perform(MathKeyboardAction.PreviousSlot)
                assertEquals("denominator", editor.model.cursor.path.last().slot)
                requireNotNull(editor.onCreateInputConnection(EditorInfo())).commitText("2", 1)
            }
            var lowerViewportDiagnostic = "Viewport not yet sampled"
            awaitUi(scenario, "The lower slot settles above the same toolbar",
                diagnostic = { lowerViewportDiagnostic }) { activity ->
                val caret = editor.expressionLayout.caretBounds(editor.model.cursor)
                val frame = Rect().also(activity.window.decorView::getWindowVisibleDisplayFrame)
                val scroll = activity.findViewById<View>(R.id.lessonScroll)
                lowerViewportDiagnostic = "frame=$frame, root=${screenBounds(activity.window.decorView)}, " +
                    "scrollHeight=${scroll.height}, scrollY=${scroll.scrollY}, editor=${screenBounds(editor)}, " +
                    "baseline=${editor.expressionBaseline}, caret=$caret, toolbar=${screenBounds(toolbar)}"
                toolbar.isAttachedToWindow && caret != null &&
                    screenBounds(editor).top + editor.expressionBaseline + caret.bottom <= screenBounds(toolbar).top
            }
            scenario.onActivity {
                val caret = requireNotNull(editor.expressionLayout.caretBounds(editor.model.cursor))
                val bounds = screenBounds(editor)
                assertTrue("The lower slot remains editable in a small viewport",
                    bounds.top + editor.expressionBaseline + caret.bottom <= screenBounds(toolbar).top)
                assertEquals("\\frac{}{2}", editor.getLatex())
                assertTrue(toolbar.isAttachedToWindow)
            }
            scenario.onActivity { activity ->
                expressionBeforeRotation = editor.model.expression
                cursorBeforeRotation = editor.model.cursor
                draftBeforeRotation = editor.model.saveDraft()
                (activity.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager)
                    .hideSoftInputFromWindow(editor.windowToken, 0)
            }
            awaitUi(scenario, "Closing the real IME restores the landscape lesson header") { activity ->
                !isImeVisible(activity) && header.visibility == View.VISIBLE && progress.isShown
            }
            scenario.onActivity { activity ->
                assertEquals(View.VISIBLE, header.visibility)
                assertTrue(progress.isShown)
                assertNull("Closing the IME removes the temporary editor viewport budget", editor.viewportHeightLimit)
                assertEquals("Closing IME restores the exact editor padding", normalEditorPadding, editor.paddingTop to editor.paddingBottom)
                assertSame(expressionBeforeRotation, editor.model.expression)
                assertEquals(draftBeforeRotation, editor.model.saveDraft())
                (activity.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager)
                    .showSoftInput(editor, InputMethodManager.SHOW_IMPLICIT)
            }
            awaitUi(scenario, "Reopening the real IME hides the landscape lesson header again") { activity ->
                isImeVisible(activity) && header.visibility == View.GONE && toolbar.isAttachedToWindow &&
                    toolbar.visibility == View.VISIBLE
            }
            orient(scenario, ActivityInfo.SCREEN_ORIENTATION_PORTRAIT, Configuration.ORIENTATION_PORTRAIT)
            var portraitDiagnostic = "Portrait geometry not yet sampled"
            awaitUi(scenario, "Returning to portrait restores the header while the real IME stays open",
                diagnostic = { portraitDiagnostic }) { activity ->
                val frame = Rect().also(activity.window.decorView::getWindowVisibleDisplayFrame)
                val scroll = activity.findViewById<View>(R.id.lessonScroll)
                portraitDiagnostic = "frame=$frame, root=${screenBounds(activity.window.decorView)}, " +
                    "scroll=${screenBounds(scroll)}, header=${header.visibility}, progress=${progress.isShown}, " +
                    "IMEvisible=${isImeVisible(activity)}, toolbar=${screenBounds(toolbar)}, " +
                    "toolbarAttached=${toolbar.isAttachedToWindow}, toolbarVisibility=${toolbar.visibility}, " +
                    "normalToolbarHeight=$normalToolbarHeight, toolbarMeasuredHeight=${toolbar.measuredHeight}, " +
                    "toolbarLayoutRequested=${toolbar.isLayoutRequested}, " +
                    "focus=${editor.hasFocus()}, currentFocus=${activity.currentFocus}, losses=$focusLosses"
                isImeVisible(activity) && header.visibility == View.VISIBLE && progress.isShown &&
                    toolbar.isAttachedToWindow && toolbar.visibility == View.VISIBLE &&
                    toolbar.height == normalToolbarHeight && !toolbar.isLayoutRequested
            }
            scenario.onActivity { activity ->
                assertTrue("The original structured field keeps focus when returning to portrait", editor.hasFocus())
                assertTrue("Responsive layout never clears focus temporarily: $focusLosses", focusLosses.isEmpty())
                assertSame(editor, activity.currentFocus)
                assertSame(modelBeforeRotation, editor.model)
                assertSame(expressionBeforeRotation, editor.model.expression)
                assertEquals(cursorBeforeRotation, editor.model.cursor)
                assertEquals(draftBeforeRotation, editor.model.saveDraft())
                assertEquals(View.VISIBLE, header.visibility)
                assertTrue(progress.isShown)
                assertEquals("\\frac{}{2}", editor.getLatex())
                assertEquals("Portrait restores the exact normal toolbar height", normalToolbarHeight, toolbar.height)
                assertNull(editor.viewportHeightLimit)
                assertEquals(normalEditorPadding, editor.paddingTop to editor.paddingBottom)
            }
        }
    }

    @Test fun realSandboxPage17KeepsSwiftKeyFocusAndSlotAcrossRotation() {
        assertTrue("Use the installed SwiftKey keyboard for the page 17 rotation regression",
            Settings.Secure.getString(context.contentResolver, Settings.Secure.DEFAULT_INPUT_METHOD).orEmpty().contains("swiftkey"))
        ProgressRepository.saveCurrentPage(context, route, lesson, 16)
        val automation = instrumentation.uiAutomation
        val originalServiceInfo = automation.serviceInfo
        val parcel = Parcel.obtain()
        try {
            originalServiceInfo.writeToParcel(parcel, 0)
            parcel.setDataPosition(0)
            automation.serviceInfo = AccessibilityServiceInfo.CREATOR.createFromParcel(parcel).apply {
                flags = flags or AccessibilityServiceInfo.FLAG_RETRIEVE_INTERACTIVE_WINDOWS
            }
        } finally { parcel.recycle() }
        try {
            ActivityScenario.launch<LessonActivity>(intent()).use { scenario ->
                orient(scenario, ActivityInfo.SCREEN_ORIENTATION_PORTRAIT, Configuration.ORIENTATION_PORTRAIT)
                lateinit var editor: MathExpressionEditor
                lateinit var fields: List<MathExpressionEditor>
                lateinit var toolbar: MathKeyboardView
                lateinit var header: View
                lateinit var progress: View
                val focusLosses = mutableListOf<String>()
                scenario.onActivity { activity ->
                    val content = activity.findViewById<View>(R.id.lessonContent)
                    fields = tree(content).filterIsInstance<MathExpressionEditor>()
                    assertEquals("The real page 17 contains its eight structured fields", 8, fields.size)
                    assertTrue(tree(content).filterIsInstance<TextView>().any { it.text.toString() == "Potencia y subíndice" })
                    editor = fields[1]
                    toolbar = tree(content).filterIsInstance<MathKeyboardView>().single()
                    header = activity.findViewById(R.id.lessonHeader)
                    progress = activity.findViewById(R.id.lessonProgress)
                    assertTrue(editor.setLatex("x_1^2"))
                    activity.window.decorView.viewTreeObserver.addOnGlobalFocusChangeListener { oldFocus, newFocus ->
                        if (oldFocus === editor && newFocus !== editor) {
                            val ancestors = generateSequence<View>(editor) { it.parent as? View }.joinToString(" > ") {
                                "${it.javaClass.simpleName}(id=${it.id},height=${it.height},measured=${it.measuredHeight},visibility=${it.visibility})"
                            }
                            focusLosses += "newFocus=$newFocus, ancestors=$ancestors"
                        }
                    }
                    assertTrue(editor.requestFocus())
                    (activity.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager)
                        .showSoftInput(editor, InputMethodManager.SHOW_IMPLICIT)
                }
                val lower = MathCursor(listOf(MathCursorStep(0, "lower")), 1)
                var lowerX = 0f
                var lowerY = 0f
                var lowerSetupDiagnostic = "Subscript geometry not yet sampled"
                var previousSubscriptGeometry: String? = null
                var stableSubscriptSamples = 0
                awaitUi(scenario, "The real subscript is visible with SwiftKey open",
                    diagnostic = { lowerSetupDiagnostic }) { activity ->
                    val caret = editor.expressionLayout.caretBounds(lower)
                    val scroll = activity.findViewById<android.widget.ScrollView>(R.id.lessonScroll)
                    val frame = Rect().also(activity.window.decorView::getWindowVisibleDisplayFrame)
                    val viewport = Rect().also { scroll.getGlobalVisibleRect(it) }
                    val toolbarBounds = screenBounds(toolbar)
                    val imeBounds = automation.windows.filter { it.type == AccessibilityWindowInfo.TYPE_INPUT_METHOD }
                        .map { Rect().also(it::getBoundsInScreen) }
                    lowerSetupDiagnostic = "frame=$frame, scrollHeight=${scroll.height}, scrollY=${scroll.scrollY}, " +
                        "viewport=$viewport, editor=${screenBounds(editor)}, baseline=${editor.expressionBaseline}, " +
                        "caret=$caret, toolbar=$toolbarBounds, IME=$imeBounds, focus=${editor.hasFocus()}"
                    if (caret == null || !isImeVisible(activity) || !editor.hasFocus() || !toolbar.isAttachedToWindow ||
                        toolbar.visibility != View.VISIBLE || !viewport.intersect(frame)) {
                        previousSubscriptGeometry = null
                        stableSubscriptSamples = 0
                        false
                    } else {
                        val bounds = screenBounds(editor)
                        lowerX = bounds.left + editor.expressionOriginX + caret.centerX()
                        lowerY = bounds.top + editor.expressionBaseline + caret.centerY()
                        viewport.bottom = minOf(viewport.bottom, toolbarBounds.top - MathWidgetUi.dp(activity, 4))
                        val visible = Rect()
                        val visibleSlot = !viewport.isEmpty && editor.getGlobalVisibleRect(visible) &&
                            visible.contains(lowerX.toInt(), lowerY.toInt()) && viewport.contains(lowerX.toInt(), lowerY.toInt())
                        if (!visibleSlot && !viewport.isEmpty) {
                            // Like a user scrolling page 17 before touching its second field, align the
                            // slot while replacing Android's pending scroll target. Plain scrollBy can
                            // be overwritten by the next OverScroller frame after coordinates are read.
                            scroll.smoothScrollBy(0, kotlin.math.round(lowerY - viewport.exactCenterY()).toInt())
                        }
                        val geometry = "$bounds|${scroll.scrollY}|${scroll.height}|$toolbarBounds|$frame|$imeBounds|$lowerX,$lowerY"
                        stableSubscriptSamples = if (visibleSlot && !editor.isLayoutRequested && !scroll.isLayoutRequested &&
                            previousSubscriptGeometry == geometry) stableSubscriptSamples + 1 else 0
                        previousSubscriptGeometry = geometry
                        stableSubscriptSamples >= 2
                    }
                }
                tapScreen(lowerX, lowerY)
                var touchDiagnostic = "Touch result not yet sampled"
                awaitUi(scenario, "A real touch enters the subscript before rotating",
                    diagnostic = { touchDiagnostic }) { activity ->
                    val scroll = activity.findViewById<View>(R.id.lessonScroll)
                    touchDiagnostic = "cursor=${editor.model.cursor}, expected=$lower, touch=($lowerX,$lowerY), " +
                        "editor=${screenBounds(editor)}, scrollY=${scroll.scrollY}, viewport=${screenBounds(scroll)}"
                    editor.model.cursor == lower
                }
                val originalModel = editor.model
                val originalExpression = editor.model.expression
                val originalDraft = editor.model.saveDraft()
                val otherDrafts = fields.filterNot { it === editor }.map { it.model.saveDraft() }
                orient(scenario, ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE, Configuration.ORIENTATION_LANDSCAPE)
                awaitUi(scenario, "Landscape retains SwiftKey and hides the real lesson header",
                    diagnostic = { "focus losses=$focusLosses" }) { activity ->
                    val frame = Rect().also(activity.window.decorView::getWindowVisibleDisplayFrame)
                    val caret = editor.expressionLayout.caretBounds(editor.model.cursor)
                    val caretTop = caret?.let { screenBounds(editor).top + editor.expressionBaseline + it.top }
                    val caretBottom = caret?.let { screenBounds(editor).top + editor.expressionBaseline + it.bottom }
                    isImeVisible(activity) && header.visibility == View.GONE && toolbar.isAttachedToWindow &&
                        toolbar.visibility == View.VISIBLE && !editor.isLayoutRequested &&
                        toolbar.height <= frame.height() / 2 && caretTop != null && caretBottom != null &&
                        caretTop >= frame.top && caretBottom <= screenBounds(toolbar).top
                }
                scenario.onActivity { activity ->
                    assertTrue("The real page 17 editor keeps focus across rotation", editor.hasFocus())
                    assertTrue("Rotation never clears focus temporarily: $focusLosses", focusLosses.isEmpty())
                    assertSame(editor, activity.currentFocus)
                    assertSame(originalModel, editor.model)
                    assertSame(originalExpression, editor.model.expression)
                    assertEquals(lower, editor.model.cursor)
                    assertEquals(originalDraft, editor.model.saveDraft())
                    assertEquals(View.GONE, header.visibility)
                    assertFalse(progress.isShown)
                }
                tapSwiftKeyTwo()
                val expectedLatex = MathLatexSerializer.serialize(
                    (MathLatexParser.parse("x_{12}^{2}") as MathLatexParser.Result.Success).expression)
                awaitUi(scenario, "A physical SwiftKey press edits the same subscript after rotation",
                    diagnostic = { "latex=${editor.getLatex()}, cursor=${editor.model.cursor}, losses=$focusLosses" }) {
                    editor.getLatex() == expectedLatex
                }
                scenario.onActivity { activity ->
                    assertTrue(editor.hasFocus())
                    assertSame(editor, activity.currentFocus)
                    assertEquals("lower", editor.model.cursor.path.last().slot)
                    assertEquals(otherDrafts, fields.filterNot { it === editor }.map { it.model.saveDraft() })
                    (activity.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager)
                        .hideSoftInputFromWindow(editor.windowToken, 0)
                }
                awaitUi(scenario, "Closing SwiftKey restores the real landscape header") { activity ->
                    !isImeVisible(activity) && header.visibility == View.VISIBLE && progress.isShown
                }
                scenario.onActivity { activity ->
                    assertTrue("Closing SwiftKey retains editor focus: currentFocus=${activity.currentFocus}, losses=$focusLosses",
                        editor.hasFocus())
                    assertTrue("Closing SwiftKey never clears focus temporarily: $focusLosses", focusLosses.isEmpty())
                }
                val editedDraft = editor.model.saveDraft()
                val editedExpression = editor.model.expression
                val editedCursor = editor.model.cursor
                scenario.onActivity { activity ->
                    (activity.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager)
                        .showSoftInput(editor, InputMethodManager.SHOW_IMPLICIT)
                }
                var reopenDiagnostic = "IME reopening not yet sampled"
                awaitUi(scenario, "Reopening SwiftKey hides the real landscape header again",
                    diagnostic = { reopenDiagnostic }) { activity ->
                    val scroll = activity.findViewById<View>(R.id.lessonScroll)
                    reopenDiagnostic = "IMEvisible=${isImeVisible(activity)}, header=${header.visibility}, " +
                        "toolbarAttached=${toolbar.isAttachedToWindow}, toolbarVisibility=${toolbar.visibility}, " +
                        "focus=${editor.hasFocus()}, currentFocus=${activity.currentFocus}, " +
                        "root=${screenBounds(activity.window.decorView)}, scrollHeight=${scroll.height}, losses=$focusLosses"
                    isImeVisible(activity) && header.visibility == View.GONE && toolbar.isAttachedToWindow &&
                        toolbar.visibility == View.VISIBLE
                }
                orient(scenario, ActivityInfo.SCREEN_ORIENTATION_PORTRAIT, Configuration.ORIENTATION_PORTRAIT)
                var finalPortraitDiagnostic = "Page 17 portrait geometry not yet sampled"
                awaitUi(scenario, "Portrait restores the real page 17 header while SwiftKey stays open",
                    diagnostic = { finalPortraitDiagnostic }) { activity ->
                    val frame = Rect().also(activity.window.decorView::getWindowVisibleDisplayFrame)
                    val parent = header.parent as View
                    finalPortraitDiagnostic = "frame=$frame, header=${header.visibility}/${header.measuredHeight}, " +
                        "parent=${screenBounds(parent)}/requested=${parent.isLayoutRequested}, " +
                        "rootRequested=${activity.findViewById<View>(android.R.id.content).isLayoutRequested}, " +
                        "progress=${progress.isShown}, IMEvisible=${isImeVisible(activity)}, " +
                        "toolbar=${screenBounds(toolbar)}/attached=${toolbar.isAttachedToWindow}/visible=${toolbar.visibility}, " +
                        "focus=${editor.hasFocus()}, currentFocus=${activity.currentFocus}, losses=$focusLosses"
                    isImeVisible(activity) && header.visibility == View.VISIBLE && progress.isShown &&
                        toolbar.isAttachedToWindow && toolbar.visibility == View.VISIBLE
                }
                scenario.onActivity { activity ->
                    assertTrue("The same real editor remains focused in portrait with SwiftKey open", editor.hasFocus())
                    assertTrue("Neither rotation nor IME visibility clears focus temporarily: $focusLosses", focusLosses.isEmpty())
                    assertSame(editor, activity.currentFocus)
                    assertSame(originalModel, editor.model)
                    assertSame(editedExpression, editor.model.expression)
                    assertEquals(editedCursor, editor.model.cursor)
                    assertEquals(editedDraft, editor.model.saveDraft())
                    assertEquals(otherDrafts, fields.filterNot { it === editor }.map { it.model.saveDraft() })
                }
            }
        } finally { automation.serviceInfo = originalServiceInfo }
    }

    private data class ImeKey(val text: String, val description: String, val bounds: Rect, val clickable: Boolean)

    private fun imeKeys(): List<ImeKey> {
        val keys = mutableListOf<ImeKey>()
        fun collect(node: AccessibilityNodeInfo) {
            if (!node.refresh()) return
            if (node.isVisibleToUser) {
                val bounds = Rect().also(node::getBoundsInScreen)
                if (!bounds.isEmpty) keys += ImeKey(node.text?.toString().orEmpty(),
                    node.contentDescription?.toString().orEmpty(), bounds, node.isClickable)
            }
            for (index in 0 until node.childCount) {
                val child = node.getChild(index) ?: continue
                try { collect(child) } finally { child.recycle() }
            }
        }
        instrumentation.uiAutomation.windows.filter { it.type == AccessibilityWindowInfo.TYPE_INPUT_METHOD }.forEach { window ->
            val root = window.root ?: return@forEach
            try { collect(root) } finally { root.recycle() }
        }
        return keys
    }

    private fun stableImeKeys(): List<ImeKey> {
        var previous = emptyList<ImeKey>()
        val deadline = SystemClock.uptimeMillis() + 3_000
        do {
            val keys = imeKeys()
            if (keys.isNotEmpty() && keys == previous) return keys
            previous = keys
            SystemClock.sleep(80)
        } while (SystemClock.uptimeMillis() < deadline)
        throw AssertionError("SwiftKey key labels and bounds did not settle before a physical press: $previous")
    }

    /** Only actual touches on the installed IME are allowed; no InputConnection or hardware-key substitute. */
    private fun tapSwiftKeyTwo() {
        var keys = stableImeKeys()
        var key = keys.filter { it.text == "2" || it.description == "2" }.maxByOrNull { it.clickable }
        if (key == null) {
            val modeLabels = setOf("123", "?123", "? 123", "Símbolos", "Números y símbolos", "Symbols", "Numbers and symbols")
            val mode = requireNotNull(keys.firstOrNull { it.text in modeLabels || it.description in modeLabels }) {
                "SwiftKey exposes neither digit 2 nor its numeric-mode key: $keys"
            }
            tapScreen(mode.bounds.exactCenterX(), mode.bounds.exactCenterY())
            keys = stableImeKeys()
            key = keys.filter { it.text == "2" || it.description == "2" }.maxByOrNull { it.clickable }
        }
        val bounds = requireNotNull(key) { "SwiftKey exposes no accessible numeric 2 key: $keys" }.bounds
        tapScreen(bounds.exactCenterX(), bounds.exactCenterY())
    }

    private fun tapScreen(x: Float, y: Float) {
        val automation = instrumentation.uiAutomation
        val downTime = SystemClock.uptimeMillis()
        val down = MotionEvent.obtain(downTime, downTime, MotionEvent.ACTION_DOWN, x, y, 0).apply {
            source = InputDevice.SOURCE_TOUCHSCREEN
        }
        try { assertTrue(automation.injectInputEvent(down, true)) } finally { down.recycle() }
        SystemClock.sleep(45)
        val up = MotionEvent.obtain(downTime, SystemClock.uptimeMillis(), MotionEvent.ACTION_UP, x, y, 0).apply {
            source = InputDevice.SOURCE_TOUCHSCREEN
        }
        try { assertTrue(automation.injectInputEvent(up, true)) } finally { up.recycle() }
        instrumentation.waitForIdleSync()
    }

    private fun isImeVisible(activity: LessonActivity): Boolean =
        ViewCompat.getRootWindowInsets(activity.window.decorView)?.isVisible(WindowInsetsCompat.Type.ime()) == true

    private fun awaitUi(
        scenario: ActivityScenario<LessonActivity>,
        message: String,
        diagnostic: () -> String = { "" },
        condition: (LessonActivity) -> Boolean
    ) {
        val deadline = SystemClock.uptimeMillis() + 5_000
        var settled = false
        do {
            instrumentation.waitForIdleSync()
            scenario.onActivity { settled = condition(it) }
            if (!settled) SystemClock.sleep(50)
        } while (!settled && SystemClock.uptimeMillis() < deadline)
        assertTrue("$message ${diagnostic()}", settled)
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
