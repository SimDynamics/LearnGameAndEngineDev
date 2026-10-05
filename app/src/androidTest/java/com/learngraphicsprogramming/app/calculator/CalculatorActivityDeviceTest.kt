package com.learngraphicsprogramming.app.calculator

import android.accessibilityservice.AccessibilityServiceInfo
import android.content.Context
import android.content.Intent
import android.content.pm.ActivityInfo
import android.content.res.Configuration
import android.graphics.Rect
import android.os.Parcel
import android.os.ParcelFileDescriptor
import android.os.SystemClock
import android.provider.Settings
import android.view.InputDevice
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.view.accessibility.AccessibilityNodeInfo
import android.view.accessibility.AccessibilityWindowInfo
import android.view.inputmethod.InputMethodManager
import android.widget.Button
import android.widget.TextView
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import androidx.test.runner.lifecycle.ActivityLifecycleMonitorRegistry
import androidx.test.runner.lifecycle.Stage
import com.learngraphicsprogramming.app.CalculatorActivity
import com.learngraphicsprogramming.app.LessonActivity
import com.learngraphicsprogramming.app.MainActivity
import com.learngraphicsprogramming.app.ProgressRepository
import com.learngraphicsprogramming.app.R
import com.learngraphicsprogramming.app.math.MathCursor
import com.learngraphicsprogramming.app.math.MathCursorStep
import com.learngraphicsprogramming.app.math.MathExpression
import com.learngraphicsprogramming.app.math.MathExpressionEditor
import com.learngraphicsprogramming.app.math.MathExpressionView
import com.learngraphicsprogramming.app.math.MathLatexParser
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotSame
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import kotlin.math.pow

/** Uses the actual calculator buttons, shared editor, background worker, and Symja engine. */
@RunWith(AndroidJUnit4::class)
class CalculatorActivityDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val snapshots = linkedMapOf<String, Map<String, *>>()
    private val route = "math_physics"
    private val lesson = "math_course_01__arithmetic_fundamentals__math_puzzle_sandbox"

    @Before fun isolateCalculatorAndPreserveUserState() {
        for (command in listOf("input keyevent KEYCODE_WAKEUP", "wm dismiss-keyguard")) {
            ParcelFileDescriptor.AutoCloseInputStream(instrumentation.uiAutomation.executeShellCommand(command)).use { it.readBytes() }
        }
        for (name in listOf("scientific_calculator", "navigation_state", "learning_progress")) {
            val preferences = context.getSharedPreferences(name, Context.MODE_PRIVATE)
            snapshots[name] = preferences.all.toMap()
            assertTrue(preferences.edit().clear().commit())
        }
    }

    @After fun restoreUserState() {
        snapshots.forEach { (name, original) ->
            val preferences = context.getSharedPreferences(name, Context.MODE_PRIVATE).edit().clear()
            original.forEach { (key, value) ->
                when (value) {
                    is String -> preferences.putString(key, value)
                    is Int -> preferences.putInt(key, value)
                    is Long -> preferences.putLong(key, value)
                    is Float -> preferences.putFloat(key, value)
                    is Boolean -> preferences.putBoolean(key, value)
                    is Set<*> -> preferences.putStringSet(key, value.filterIsInstance<String>().toSet())
                }
            }
            assertTrue(preferences.commit())
        }
    }

    @Test fun homeEntryAndRealKeypadProduceAnAstThenAnEvaluatedStructuredResult() {
        ActivityScenario.launch<MainActivity>(Intent(context, MainActivity::class.java)).use { scenario ->
            scenario.onActivity { it.findViewById<Button>(R.id.buttonCalculator).performClick() }
            val calculator = resumedCalculator()
            try {
                onMain {
                    for (id in listOf(R.id.calculator_key_1, R.id.calculator_key_add,
                        R.id.calculator_key_2, R.id.calculator_key_multiply, R.id.calculator_key_3)) {
                        key(calculator, id).performClick()
                    }
                    assertEquals(parse("1+2×3"), input(calculator).model.expression)
                    key(calculator, R.id.calculator_evaluate).performClick()
                }
                awaitResult(calculator)
                onMain {
                    assertEquals(7.0, scalar(result(calculator).expression), 1e-10)
                    assertEquals("Evaluation leaves the entry editable", parse("1+2×3"), input(calculator).model.expression)
                    key(calculator, R.id.calculator_clear).performClick()
                    assertTrue(input(calculator).model.expression.children.isEmpty())
                    assertTrue(result(calculator).expression.children.isEmpty())
                    assertEquals(View.GONE, calculator.findViewById<View>(R.id.calculator_result_host).visibility)
                }
            } finally {
                onMain { calculator.finish() }
                instrumentation.waitForIdleSync()
            }
        }
    }

    @Test fun nestedAstAndDegreeModeHaveRenderedResultsThatSurviveRecreation() {
        ActivityScenario.launch<CalculatorActivity>(Intent(context, CalculatorActivity::class.java)).use { scenario ->
            scenario.onActivity { activity ->
                assertTrue(input(activity).setLatex("\\frac{2^3+\\sqrt{16}}{\\sin{\\frac{\\pi}{2}}+1}"))
                key(activity, R.id.calculator_evaluate).performClick()
            }
            await(scenario, "Symja evaluates a nested fraction, power, root, and function") { completedResult(it) }
            var original = MathExpression.Sequence()
            var draft = ""
            scenario.onActivity { activity ->
                assertEquals(6.0, scalar(result(activity).expression), 1e-10)
                original = input(activity).model.expression
                draft = input(activity).model.saveDraft()
            }
            scenario.recreate()
            scenario.onActivity { activity ->
                assertEquals(original, input(activity).model.expression)
                assertEquals(draft, input(activity).model.saveDraft())
                assertEquals(6.0, scalar(result(activity).expression), 1e-10)
                key(activity, R.id.calculator_angle_mode).performClick()
                assertEquals("DEG", key(activity, R.id.calculator_angle_mode).text.toString())
                assertTrue(input(activity).setLatex("\\sin{30}"))
                key(activity, R.id.calculator_evaluate).performClick()
            }
            await(scenario, "Degree mode converts the argument semantically") { completedResult(it) }
            scenario.onActivity { assertEquals(0.5, scalar(result(it).expression), 1e-10) }
            scenario.recreate()
            scenario.onActivity { activity ->
                assertEquals("DEG", key(activity, R.id.calculator_angle_mode).text.toString())
                assertEquals(parse("\\sin{30}"), input(activity).model.expression)
                assertEquals(0.5, scalar(result(activity).expression), 1e-10)
            }
        }
    }

    @Test fun anEvaluationErrorKeepsTheExactInputTreeSelectionAndDraft() {
        ActivityScenario.launch<CalculatorActivity>(Intent(context, CalculatorActivity::class.java)).use { scenario ->
            lateinit var expression: MathExpression.Sequence
            var draft = ""
            scenario.onActivity { activity ->
                val editor = input(activity)
                assertTrue(editor.setLatex("\\frac{12}{0}"))
                val numerator = listOf(MathCursorStep(0, "numerator"))
                editor.model.cursor = MathCursor(numerator, 1)
                editor.model.select(numerator, 0, 1)
                expression = editor.model.expression
                draft = editor.model.saveDraft()
                key(activity, R.id.calculator_evaluate).performClick()
            }
            await(scenario, "Division by zero produces a visible controlled error") { activity ->
                key(activity, R.id.calculator_evaluate).isEnabled &&
                    activity.findViewById<TextView>(R.id.calculator_error).text.isNotBlank() &&
                    activity.findViewById<View>(R.id.calculator_result_host).visibility == View.GONE
            }
            scenario.onActivity { activity ->
                assertSame(expression, input(activity).model.expression)
                assertEquals(draft, input(activity).model.saveDraft())
                assertFalse(activity.findViewById<TextView>(R.id.calculator_error).text.isBlank())
                assertTrue(result(activity).expression.children.isEmpty())
            }
            scenario.recreate()
            scenario.onActivity { activity ->
                assertEquals(expression, input(activity).model.expression)
                assertEquals(draft, input(activity).model.saveDraft())
            }
        }
    }

    @Test fun lessonCalculatorHasItsOwnEditorAndCannotReplaceTheAnswer() {
        ProgressRepository.resetLesson(context, route, lesson)
        ProgressRepository.saveCurrentPage(context, route, lesson, 16)
        val intent = Intent(context, LessonActivity::class.java)
            .putExtra("routeId", route)
            .putExtra("courseId", "math_course_01")
            .putExtra("unitId", "arithmetic_fundamentals")
            .putExtra("lessonId", lesson)
            .putExtra("lessonNumber", 1)
            .putExtra("lessonTotal", 1)
        ActivityScenario.launch<LessonActivity>(intent).use { scenario ->
            lateinit var answer: MathExpressionEditor
            lateinit var answerExpression: MathExpression.Sequence
            var draft = ""
            scenario.onActivity { activity ->
                answer = tree(activity.findViewById(R.id.lessonContent)).filterIsInstance<MathExpressionEditor>().first()
                assertTrue(answer.setLatex("x_1^2"))
                val lower = listOf(MathCursorStep(0, "lower"))
                answer.model.cursor = MathCursor(lower, 1)
                answer.model.select(lower, 0, 1)
                answerExpression = answer.model.expression
                draft = answer.model.saveDraft()
                val show = activity.findViewById<Button>(R.id.buttonShowCalculator)
                assertEquals(View.VISIBLE, show.visibility)
                val buttonBounds = android.graphics.Rect()
                assertTrue("The auxiliary tool is visible without scrolling through all exercises", show.getGlobalVisibleRect(buttonBounds))
                assertEquals(show.height, buttonBounds.height())
                show.performClick()
            }
            val calculator = resumedCalculator()
            try {
                onMain {
                    assertNotSame("The calculator owns a distinct structured input", answer, input(calculator))
                    assertNotSame("Each editor owns its own model", answer.model, input(calculator).model)
                    assertNotSame("The tool never imports the answer's AST", answer.model.expression, input(calculator).model.expression)
                    assertTrue(input(calculator).model.expression.children.isEmpty())
                    key(calculator, R.id.calculator_key_8).performClick()
                    key(calculator, R.id.calculator_key_divide).performClick()
                    key(calculator, R.id.calculator_key_2).performClick()
                    key(calculator, R.id.calculator_evaluate).performClick()
                }
                awaitResult(calculator)
                onMain { assertEquals(4.0, scalar(result(calculator).expression), 1e-10) }
            } finally {
                onMain { key(calculator, R.id.calculator_back).performClick() }
                instrumentation.waitForIdleSync()
            }
            scenario.onActivity {
                assertEquals(parse("x_1^2"), answer.model.expression)
                assertSame(answerExpression, answer.model.expression)
                assertEquals("Opening and closing the tool keeps the answer's cursor and selection", draft, answer.model.saveDraft())
                it.findViewById<Button>(R.id.buttonShowCalculator).performClick()
            }
            val reopened = resumedCalculator()
            try {
                onMain {
                    assertNotSame(answer, input(reopened))
                    assertEquals("The tool restores only its own draft", parse("8÷2"), input(reopened).model.expression)
                    assertEquals(4.0, scalar(result(reopened).expression), 1e-10)
                    assertSame(answerExpression, answer.model.expression)
                    assertEquals(draft, answer.model.saveDraft())
                }
            } finally {
                onMain { key(reopened, R.id.calculator_back).performClick() }
                instrumentation.waitForIdleSync()
            }
            scenario.onActivity {
                assertSame(answerExpression, answer.model.expression)
                assertEquals(draft, answer.model.saveDraft())
            }
        }
    }

    @Test fun realSwiftKeyKeepsTheFloatingEditorAndEvaluationAccessibleInBothOrientations() {
        assertTrue("This regression uses the actual installed SwiftKey keyboard",
            Settings.Secure.getString(context.contentResolver, Settings.Secure.DEFAULT_INPUT_METHOD)
                .orEmpty().contains("swiftkey"))
        val automation = instrumentation.uiAutomation
        val originalServiceInfo = automation.serviceInfo
        val parcel = Parcel.obtain()
        try {
            originalServiceInfo.writeToParcel(parcel, 0)
            parcel.setDataPosition(0)
            automation.serviceInfo = AccessibilityServiceInfo.CREATOR.createFromParcel(parcel).apply {
                flags = flags or AccessibilityServiceInfo.FLAG_RETRIEVE_INTERACTIVE_WINDOWS
            }
        } finally {
            parcel.recycle()
        }
        try {
            ActivityScenario.launch<CalculatorActivity>(Intent(context, CalculatorActivity::class.java)).use { scenario ->
                for ((requested, expected) in listOf(
                    ActivityInfo.SCREEN_ORIENTATION_PORTRAIT to Configuration.ORIENTATION_PORTRAIT,
                    ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE to Configuration.ORIENTATION_LANDSCAPE
                )) {
                    scenario.onActivity { it.requestedOrientation = requested }
                    await(scenario, "The calculator settles in orientation $expected") {
                        it.resources.configuration.orientation == expected && it.hasWindowFocus()
                    }
                    scenario.onActivity { activity ->
                        assertTrue(input(activity).setLatex(""))
                        key(activity, R.id.calculator_native_keyboard).performClick()
                    }
                    await(scenario, "SwiftKey is visible and the custom keypad yields its space") { activity ->
                        val editor = input(activity)
                        val manager = activity.getSystemService(InputMethodManager::class.java)
                        if (activity.hasWindowFocus() && !imeVisible(activity)) {
                            manager.showSoftInput(editor, InputMethodManager.SHOW_IMPLICIT)
                        }
                        editor.hasFocus() && manager.isActive(editor) && imeVisible(activity) &&
                            imeWindowBounds().isNotEmpty() &&
                            activity.findViewById<View>(R.id.calculator_keypad).visibility == View.GONE &&
                            activity.findViewById<View>(R.id.calculator_ime_navigation).visibility == View.VISIBLE
                    }
                    scenario.onActivity { activity ->
                        val editor = input(activity)
                        editor.requestRectangleOnScreen(Rect(0, 0, editor.width, editor.height), true)
                    }
                    await(scenario, "The structured editor is fully visible above the real SwiftKey window") { activity ->
                        val editor = input(activity)
                        val bounds = Rect()
                        val imeTop = imeWindowBounds().minOfOrNull { it.top }
                        !editor.isLayoutRequested && editor.getGlobalVisibleRect(bounds) &&
                            bounds.height() == editor.height && imeTop != null && bounds.bottom <= imeTop
                    }
                    tapImeKey("3")
                    await(scenario, "One physical SwiftKey digit produces exactly one AST number") {
                        input(it).model.expression == parse("3") && input(it).hasFocus()
                    }
                    scenario.onActivity { activity ->
                        val evaluate = key(activity, R.id.calculator_evaluate)
                        evaluate.requestRectangleOnScreen(Rect(0, 0, evaluate.width, evaluate.height), true)
                    }
                    await(scenario, "Scrolling exposes the complete evaluation control above SwiftKey") { activity ->
                        val evaluate = key(activity, R.id.calculator_evaluate)
                        val bounds = Rect()
                        val imeTop = imeWindowBounds().minOfOrNull { it.top }
                        evaluate.getGlobalVisibleRect(bounds) && bounds.height() == evaluate.height &&
                            imeTop != null && bounds.bottom <= imeTop && imeVisible(activity)
                    }
                    var evaluateBounds = Rect()
                    scenario.onActivity { evaluateBounds = screenBounds(key(it, R.id.calculator_evaluate)) }
                    tap(evaluateBounds)
                    await(scenario, "The exposed evaluation button publishes the real Symja result") { completedResult(it) }
                    scenario.onActivity { activity ->
                        assertEquals(3.0, scalar(result(activity).expression), 1e-10)
                        assertTrue("Evaluating does not transfer the native IME away from the editor", input(activity).hasFocus())
                    }
                    tapImeBackspace()
                    await(scenario, "One physical SwiftKey Backspace empties the input without replaying text") {
                        input(it).model.expression.children.isEmpty() && input(it).hasFocus()
                    }
                    scenario.onActivity { activity ->
                        assertTrue(result(activity).expression.children.isEmpty())
                        activity.getSystemService(InputMethodManager::class.java)
                            .hideSoftInputFromWindow(input(activity).windowToken, 0)
                    }
                    await(scenario, "The custom keypad returns when SwiftKey closes") {
                        !imeVisible(it) && it.findViewById<View>(R.id.calculator_keypad).visibility == View.VISIBLE
                    }
                }
            }
        } finally {
            automation.serviceInfo = originalServiceInfo
        }
    }

    private fun input(activity: CalculatorActivity): MathExpressionEditor = activity.findViewById(R.id.calculator_input)
    private fun result(activity: CalculatorActivity): MathExpressionView = activity.findViewById(R.id.calculator_result)
    private fun key(activity: CalculatorActivity, id: Int): Button = activity.findViewById(id)
    private fun onMain(block: () -> Unit) = instrumentation.runOnMainSync(block)
    private fun completedResult(activity: CalculatorActivity) = key(activity, R.id.calculator_evaluate).isEnabled &&
        activity.findViewById<View>(R.id.calculator_result_host).visibility == View.VISIBLE &&
        result(activity).expression.children.isNotEmpty()

    private fun resumedCalculator(): CalculatorActivity {
        var activity: CalculatorActivity? = null
        awaitCondition("The calculator opens from its entry point") {
            onMain {
                activity = ActivityLifecycleMonitorRegistry.getInstance().getActivitiesInStage(Stage.RESUMED)
                    .filterIsInstance<CalculatorActivity>().singleOrNull()
            }
            activity != null
        }
        return requireNotNull(activity)
    }

    private fun awaitResult(activity: CalculatorActivity) {
        awaitCondition("A real Symja result reaches the separate renderer") {
            var done = false
            onMain { done = completedResult(activity) }
            done
        }
    }

    private fun await(scenario: ActivityScenario<CalculatorActivity>, message: String, predicate: (CalculatorActivity) -> Boolean) {
        awaitCondition(message) {
            var done = false
            scenario.onActivity { done = predicate(it) }
            done
        }
    }

    private fun awaitCondition(message: String, condition: () -> Boolean) {
        val deadline = SystemClock.uptimeMillis() + 30_000
        while (SystemClock.uptimeMillis() < deadline) {
            instrumentation.waitForIdleSync()
            if (condition()) return
            SystemClock.sleep(50)
        }
        assertTrue(message, condition())
    }

    private fun tree(view: View): List<View> = listOf(view) +
        if (view is ViewGroup) (0 until view.childCount).flatMap { tree(view.getChildAt(it)) } else emptyList()

    private fun parse(source: String) = (MathLatexParser.parse(source) as MathLatexParser.Result.Success).expression

    private fun imeVisible(activity: CalculatorActivity) = ViewCompat.getRootWindowInsets(activity.window.decorView)
        ?.isVisible(WindowInsetsCompat.Type.ime()) == true

    private fun imeWindowBounds(): List<Rect> = instrumentation.uiAutomation.windows
        .filter { it.type == AccessibilityWindowInfo.TYPE_INPUT_METHOD }
        .map { Rect().also(it::getBoundsInScreen) }.filterNot { it.isEmpty }

    private fun screenBounds(view: View): Rect = IntArray(2).let { location ->
        view.getLocationOnScreen(location)
        Rect(location[0], location[1], location[0] + view.width, location[1] + view.height)
    }

    private data class ImeKey(val label: String, val description: String, val bounds: Rect, val clickable: Boolean)

    private fun imeKeys(): List<ImeKey> {
        val keys = mutableListOf<ImeKey>()
        fun visit(node: AccessibilityNodeInfo) {
            if (!node.refresh()) return
            if (node.isVisibleToUser) {
                val bounds = Rect().also(node::getBoundsInScreen)
                if (!bounds.isEmpty) keys += ImeKey(node.text?.toString().orEmpty(),
                    node.contentDescription?.toString().orEmpty(), bounds, node.isClickable)
            }
            for (index in 0 until node.childCount) {
                val child = node.getChild(index) ?: continue
                try { visit(child) } finally { child.recycle() }
            }
        }
        instrumentation.uiAutomation.windows.filter { it.type == AccessibilityWindowInfo.TYPE_INPUT_METHOD }
            .forEach { window ->
                val root = window.root ?: return@forEach
                try { visit(root) } finally { root.recycle() }
            }
        return keys
    }

    private fun stableImeKeys(): List<ImeKey> {
        var previous = emptyList<ImeKey>()
        var previousWindows = emptyList<Rect>()
        repeat(30) {
            val current = imeKeys()
            val windows = imeWindowBounds()
            if (current.isNotEmpty() && current == previous && windows == previousWindows) return current
            previous = current
            previousWindows = windows
            SystemClock.sleep(80)
        }
        error("SwiftKey's real key bounds did not settle: windows=$previousWindows, keys=$previous")
    }

    private fun tapImeKey(label: String) {
        var keys = stableImeKeys()
        if (keys.none { it.label == label || it.description == label }) {
            val modes = setOf("123", "?123", "? 123", "Símbolos", "Números y símbolos", "Symbols", "Numbers and symbols")
            val mode = keys.firstOrNull { it.label in modes || it.description in modes }
                ?: error("SwiftKey exposes neither the '$label' digit nor its numeric mode: $keys")
            tap(mode.bounds)
            keys = stableImeKeys()
        }
        val key = keys.filter { it.label == label || it.description == label }.maxByOrNull { it.clickable }
            ?: error("SwiftKey exposes no visible '$label' digit: $keys")
        tap(key.bounds)
    }

    private fun tapImeBackspace() {
        val keys = stableImeKeys()
        val key = keys.filter {
            Regex("(?i)(backspace|delete|borrar|retroceso|eliminar)").containsMatchIn(it.label + " " + it.description)
        }.maxByOrNull { it.clickable } ?: error("SwiftKey exposes no accessible Backspace: $keys")
        tap(key.bounds)
    }

    /** Inject a screen touch into the actual virtual key; never synthesize commitText or KEYCODE_DEL. */
    private fun tap(bounds: Rect) {
        val automation = instrumentation.uiAutomation
        val downTime = SystemClock.uptimeMillis()
        val down = MotionEvent.obtain(downTime, downTime, MotionEvent.ACTION_DOWN,
            bounds.exactCenterX(), bounds.exactCenterY(), 0).apply { source = InputDevice.SOURCE_TOUCHSCREEN }
        try { assertTrue(automation.injectInputEvent(down, true)) } finally { down.recycle() }
        SystemClock.sleep(45)
        val up = MotionEvent.obtain(downTime, SystemClock.uptimeMillis(), MotionEvent.ACTION_UP,
            bounds.exactCenterX(), bounds.exactCenterY(), 0).apply { source = InputDevice.SOURCE_TOUCHSCREEN }
        try { assertTrue(automation.injectInputEvent(up, true)) } finally { up.recycle() }
        instrumentation.waitForIdleSync()
    }

    /** Numeric result formatting may use a sign, a fraction, or a power of ten. */
    private fun scalar(expression: MathExpression): Double = when (expression) {
        is MathExpression.Number -> expression.value.toDouble()
        is MathExpression.Fraction -> scalar(expression.numerator) / scalar(expression.denominator)
        is MathExpression.Scripts -> scalar(expression.base).pow(scalar(requireNotNull(expression.upper)))
        is MathExpression.Group -> scalar(expression.content)
        is MathExpression.Sequence -> {
            var value = 1.0
            expression.children.forEachIndexed { index, child ->
                if (child is MathExpression.Operator) {
                    when (child.value) {
                        "-", "−" -> { check(index == 0); value = -value }
                        "×", "·", "*" -> Unit
                        else -> error("Unexpected numeric display operator: $child")
                    }
                } else value *= scalar(child)
            }
            value
        }
        else -> error("Unexpected scalar result: $expression")
    }
}
