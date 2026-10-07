package com.learngraphicsprogramming.app.calculator

import android.accessibilityservice.AccessibilityServiceInfo
import android.content.Context
import android.content.Intent
import android.content.pm.ActivityInfo
import android.content.res.Configuration
import android.graphics.Color
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
import android.widget.EditText
import android.widget.ScrollView
import android.widget.TextView
import androidx.appcompat.widget.SwitchCompat
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
import com.learngraphicsprogramming.app.math.MathCartesianView
import com.learngraphicsprogramming.app.math.MathDelimiter
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
                    assertEquals("The independent tool has no answer destination", View.GONE,
                        calculator.findViewById<View>(R.id.calculator_paste_result).visibility)
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

    @Test fun percentageUsesTheRealKeypadAndKeepsItsDivisionByOneHundredMeaning() {
        ActivityScenario.launch<CalculatorActivity>(Intent(context, CalculatorActivity::class.java)).use { scenario ->
            for ((keys, expected) in listOf(
                listOf(R.id.calculator_key_5, R.id.calculator_key_0, R.id.calculator_key_percent) to 0.5,
                listOf(R.id.calculator_key_1, R.id.calculator_key_2, R.id.calculator_key_decimal,
                    R.id.calculator_key_5, R.id.calculator_key_percent) to 0.125,
                listOf(R.id.calculator_key_2, R.id.calculator_key_0, R.id.calculator_key_0,
                    R.id.calculator_key_multiply, R.id.calculator_key_1, R.id.calculator_key_0,
                    R.id.calculator_key_percent) to 20.0,
                listOf(R.id.calculator_key_5, R.id.calculator_key_0, R.id.calculator_key_percent,
                    R.id.calculator_key_add, R.id.calculator_key_2, R.id.calculator_key_5,
                    R.id.calculator_key_percent) to 0.75
            )) {
                scenario.onActivity { activity ->
                    key(activity, R.id.calculator_clear).performClick()
                    keys.forEach { key(activity, it).performClick() }
                    key(activity, R.id.calculator_evaluate).performClick()
                }
                await(scenario, "Percentage evaluates through the production editor and engine") { completedResult(it) }
                scenario.onActivity { assertEquals(expected, scalar(result(it).expression), 1e-10) }
            }
        }
    }

    @Test fun percentageCursorTraversesTheWholeExpressionAndNewOperatorsRemainDeletable() {
        ActivityScenario.launch<CalculatorActivity>(Intent(context, CalculatorActivity::class.java)).use { scenario ->
            scenario.onActivity { activity ->
                for (id in listOf(R.id.calculator_key_5, R.id.calculator_key_0, R.id.calculator_key_multiply,
                    R.id.calculator_key_1, R.id.calculator_key_0, R.id.calculator_key_percent)) {
                    key(activity, id).performClick()
                }
                val editor = input(activity)
                val original = editor.model.expression
                assertEquals(parse("50×10%"), original)
                repeat(24) { key(activity, R.id.calculator_key_left).performClick() }
                assertTrue("LEFT must get past 50 to the start of the root sequence", editor.model.cursor.path.isEmpty())
                assertEquals(0, editor.model.cursor.offset)
                assertEquals(original, editor.model.expression)
                repeat(24) { key(activity, R.id.calculator_key_right).performClick() }
                assertTrue("RIGHT must leave every token and reach the end", editor.model.cursor.path.isEmpty())
                assertEquals(original.children.size, editor.model.cursor.offset)
                assertEquals(original, editor.model.expression)
                key(activity, R.id.calculator_key_backspace).performClick()
                assertEquals("Backspace removes the percent atom", parse("50×10"), editor.model.expression)
                advancedMode(activity).performClick()
            }
            for (operator in listOf(R.id.calculator_key_combinations, R.id.calculator_key_permutations,
                R.id.calculator_key_factorial, R.id.calculator_key_percent)) {
                scenario.onActivity { activity ->
                    key(activity, R.id.calculator_clear).performClick()
                    key(activity, R.id.calculator_key_5).performClick()
                    key(activity, operator).performClick()
                    key(activity, R.id.calculator_key_backspace).performClick()
                    assertEquals("A newly entered or incomplete operator must be removable", parse("5"), input(activity).model.expression)
                    key(activity, R.id.calculator_key_add).performClick()
                    key(activity, R.id.calculator_key_2).performClick()
                    key(activity, R.id.calculator_evaluate).performClick()
                }
                await(scenario, "The expression remains editable after removing operator $operator") { completedResult(it) }
                scenario.onActivity { assertEquals(7.0, scalar(result(it).expression), 1e-10) }
            }
        }
    }

    @Test fun twoTapKeySwapsAreIndependentForBothModesAndPersistAfterRotationAndReopening() {
        var expectedBasic = emptyList<Int>()
        var expectedAdvanced = emptyList<Int>()
        var savedDraft = ""
        val intent = Intent(context, CalculatorActivity::class.java)
        ActivityScenario.launch<CalculatorActivity>(intent).use { scenario ->
            var originalBasic = emptyList<Int>()
            var originalAdvanced = emptyList<Int>()
            scenario.onActivity { activity ->
                originalBasic = gridKeyIds(activity)
                advancedMode(activity).performClick()
                originalAdvanced = gridKeyIds(activity)
                advancedMode(activity).performClick()
                assertTrue(input(activity).setLatex("50×10%"))
                key(activity, R.id.calculator_angle_mode).performClick()
                key(activity, R.id.calculator_evaluate).performClick()
            }
            await(scenario, "A valid result exists before customizing the keyboard") { completedResult(it) }
            scenario.onActivity { activity ->
                savedDraft = input(activity).model.saveDraft()
                assertFalse(activity.findViewById<View>(R.id.calculator_edit_scroll_up).isShown)
                assertFalse(activity.findViewById<View>(R.id.calculator_edit_scroll_down).isShown)
                key(activity, R.id.calculator_edit_keys).performClick()
                assertTrue(activity.findViewById<View>(R.id.calculator_edit_scroll_up).isShown)
                assertTrue(activity.findViewById<View>(R.id.calculator_edit_scroll_down).isShown)
                key(activity, R.id.calculator_key_7).performClick()
                assertTrue("The first swap key is visibly selected", key(activity, R.id.calculator_key_7).isSelected)
                assertSelectedGray(key(activity, R.id.calculator_key_7))
                assertEquals("Edit-mode keys never write into the editor", savedDraft, input(activity).model.saveDraft())
                assertEquals(5.0, scalar(result(activity).expression), 1e-10)
                activity.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
            }
            await(scenario, "Editing survives a real landscape rotation") {
                it.resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE && it.hasWindowFocus()
            }
            scenario.recreate()
            scenario.onActivity { activity ->
                assertTrue("The selected stable key survives rotation and recreation", key(activity, R.id.calculator_key_7).isSelected)
                assertSelectedGray(key(activity, R.id.calculator_key_7))
                assertTrue(activity.findViewById<View>(R.id.calculator_edit_scroll_down).isShown)
                assertEquals(savedDraft, input(activity).model.saveDraft())
                assertEquals("DEG", key(activity, R.id.calculator_angle_mode).text.toString())
                assertEquals(5.0, scalar(result(activity).expression), 1e-10)
                key(activity, R.id.calculator_key_8).performClick()
                expectedBasic = swapped(originalBasic, R.id.calculator_key_7, R.id.calculator_key_8)
                assertEquals(expectedBasic, gridKeyIds(activity))
                assertFalse(key(activity, R.id.calculator_key_7).isSelected)
                assertFalse(key(activity, R.id.calculator_key_8).isSelected)
                assertEquals(savedDraft, input(activity).model.saveDraft())
                advancedMode(activity).performClick()
                assertEquals("A basic swap does not reorder the advanced keyboard", originalAdvanced, gridKeyIds(activity))
                key(activity, R.id.calculator_key_1).performClick()
                key(activity, R.id.calculator_key_2).performClick()
                expectedAdvanced = swapped(originalAdvanced, R.id.calculator_key_1, R.id.calculator_key_2)
                assertEquals(expectedAdvanced, gridKeyIds(activity))
                advancedMode(activity).performClick()
                assertEquals("An advanced swap does not change the saved basic keyboard", expectedBasic, gridKeyIds(activity))
                assertEquals(savedDraft, input(activity).model.saveDraft())
                assertEquals(5.0, scalar(result(activity).expression), 1e-10)
                key(activity, R.id.calculator_edit_keys).performClick()
                assertFalse(activity.findViewById<View>(R.id.calculator_edit_scroll_up).isShown)
                assertFalse(activity.findViewById<View>(R.id.calculator_edit_scroll_down).isShown)
                activity.finish()
            }
        }
        ActivityScenario.launch<CalculatorActivity>(intent).use { scenario ->
            scenario.onActivity { activity ->
                assertFalse(advancedMode(activity).isChecked)
                assertEquals("Closing and reopening keeps the local basic order", expectedBasic, gridKeyIds(activity))
                assertEquals(savedDraft, input(activity).model.saveDraft())
                advancedMode(activity).performClick()
                assertEquals("Closing and reopening keeps the separate advanced order", expectedAdvanced, gridKeyIds(activity))
                key(activity, R.id.calculator_clear).performClick()
                key(activity, R.id.calculator_key_2).performClick()
                assertEquals("Leaving edit mode restores normal mathematical key actions", parse("2"), input(activity).model.expression)
            }
        }
    }

    @Test fun aUniformSixColumnKeypadScrollsWithoutMovingTheHeaderExpressionOrResult() {
        ActivityScenario.launch<CalculatorActivity>(Intent(context, CalculatorActivity::class.java)).use { scenario ->
            scenario.onActivity { it.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE }
            await(scenario, "The keypad surface settles before evaluation starts") {
                it.resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE && it.hasWindowFocus()
            }
            scenario.onActivity { activity ->
                assertTrue(input(activity).setLatex("1+2"))
                key(activity, R.id.calculator_evaluate).performClick()
            }
            await(scenario, "The compact landscape surface has a valid fixed result") {
                it.resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE && completedResult(it) && it.hasWindowFocus()
            }
            scenario.onActivity { advancedMode(it).performClick() }
            await(scenario, "The advanced grid is measured with a scrollable overflow") {
                val scroll = keypadScroll(it)
                scroll.height > 0 && scroll.getChildAt(0).height > scroll.height && !scroll.isLayoutRequested
            }
            var fixedBounds = emptyList<Rect>()
            var order = emptyList<Int>()
            var scrollBounds = Rect()
            scenario.onActivity { activity ->
                val buttons = gridKeys(activity)
                val reference = key(activity, R.id.calculator_key_parentheses)
                assertTrue(buttons.size > 6)
                buttons.forEach { button ->
                    assertTrue("Every mathematical key has the six-column cell width",
                        kotlin.math.abs(button.width - reference.width) <= 1)
                    assertEquals("Every mathematical key retains the same touch height", reference.height, button.height)
                }
                fixedBounds = fixedPanelBounds(activity)
                order = gridKeyIds(activity)
                keypadScroll(activity).scrollTo(0, 0)
                scrollBounds = screenBounds(keypadScroll(activity))
            }
            swipeUp(scrollBounds)
            await(scenario, "A vertical gesture scrolls the keyboard alone") { keypadScroll(it).scrollY > 0 }
            scenario.onActivity { activity ->
                assertEquals(fixedBounds, fixedPanelBounds(activity))
                assertEquals(order, gridKeyIds(activity))
                key(activity, R.id.calculator_edit_keys).performClick()
                fixedBounds = fixedPanelBounds(activity)
                keypadScroll(activity).fling(0)
                keypadScroll(activity).scrollTo(0, 0)
                key(activity, R.id.calculator_edit_scroll_down).performClick()
            }
            await(scenario, "The edit down control scrolls without moving keys") { keypadScroll(it).scrollY > 0 }
            var lowerPosition = 0
            scenario.onActivity { activity ->
                lowerPosition = keypadScroll(activity).scrollY
                assertEquals(order, gridKeyIds(activity))
                assertEquals(fixedBounds, fixedPanelBounds(activity))
                key(activity, R.id.calculator_edit_scroll_up).performClick()
            }
            await(scenario, "The edit up control moves the viewport back") { keypadScroll(it).scrollY < lowerPosition }
            scenario.onActivity { activity ->
                assertEquals(order, gridKeyIds(activity))
                assertEquals(fixedBounds, fixedPanelBounds(activity))
                assertEquals(parse("1+2"), input(activity).model.expression)
                assertEquals(3.0, scalar(result(activity).expression), 1e-10)
            }
        }
    }

    @Test fun touchingTheEditorPositionsTheCursorWithoutOpeningTheImeInEitherOrientation() {
        ActivityScenario.launch<CalculatorActivity>(Intent(context, CalculatorActivity::class.java)).use { scenario ->
            scenario.onActivity { assertTrue(input(it).setLatex("50×10%")) }
            for ((requested, expected) in listOf(
                ActivityInfo.SCREEN_ORIENTATION_PORTRAIT to Configuration.ORIENTATION_PORTRAIT,
                ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE to Configuration.ORIENTATION_LANDSCAPE
            )) {
                scenario.onActivity { it.requestedOrientation = requested }
                await(scenario, "The editor settles with the Android keyboard closed in orientation $expected") {
                    it.resources.configuration.orientation == expected && it.hasWindowFocus() && !imeVisible(it) &&
                        input(it).width > 0 && !input(it).isLayoutRequested
                }
                var bounds = Rect()
                scenario.onActivity { bounds = screenBounds(input(it)) }
                tap(bounds)
                SystemClock.sleep(400)
                var draft = ""
                scenario.onActivity { activity ->
                    assertTrue(input(activity).hasFocus())
                    assertFalse("A normal editor touch cannot automatically open Android's keyboard", imeVisible(activity))
                    assertEquals(parse("50×10%"), input(activity).model.expression)
                    draft = input(activity).model.saveDraft()
                    key(activity, R.id.calculator_native_keyboard).performClick()
                }
                await(scenario, "Only the explicit Teclado control opens the Android keyboard") { imeVisible(it) }
                scenario.onActivity { activity ->
                    assertEquals(draft, input(activity).model.saveDraft())
                    activity.getSystemService(InputMethodManager::class.java)
                        .hideSoftInputFromWindow(input(activity).windowToken, 0)
                }
                await(scenario, "Closing Android's keyboard restores the mathematical keypad") { !imeVisible(it) && keypadScroll(it).isShown }
                scenario.onActivity { assertEquals("Closing the IME preserves the expression and precise cursor", draft, input(it).model.saveDraft()) }
            }
        }
    }

    @Test fun anExplicitlyOpenedImeSurvivesRotationWithItsPreciseCursorResultAndReturnState() {
        val intent = Intent(context, CalculatorActivity::class.java)
            .putExtra(CalculatorActivity.EXTRA_RETURN_RESULT, true)
        ActivityScenario.launch<CalculatorActivity>(intent).use { scenario ->
            scenario.onActivity { it.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_PORTRAIT }
            await(scenario, "The calculator begins in portrait with its keypad available") {
                it.resources.configuration.orientation == Configuration.ORIENTATION_PORTRAIT &&
                    it.hasWindowFocus() && keypadScroll(it).isShown
            }
            var order = emptyList<Int>()
            scenario.onActivity { activity ->
                advancedMode(activity).performClick()
                key(activity, R.id.calculator_edit_keys).performClick()
                key(activity, R.id.calculator_key_7).performClick()
                key(activity, R.id.calculator_key_8).performClick()
                key(activity, R.id.calculator_edit_keys).performClick()
                order = gridKeyIds(activity)
                key(activity, R.id.calculator_angle_mode).performClick()
                assertTrue(input(activity).setLatex("\\sin{30}"))
                key(activity, R.id.calculator_evaluate).performClick()
            }
            await(scenario, "A valid degree-mode result is ready before explicitly opening the IME") { completedResult(it) }
            var draft = ""
            scenario.onActivity { activity ->
                // Keep a precise position inside the argument rather than a trivial end caret.
                input(activity).model.cursor = MathCursor(listOf(MathCursorStep(0, "argument")), 0, leafTextOffset = 1)
                input(activity).invalidate()
                draft = input(activity).model.saveDraft()
                key(activity, R.id.calculator_native_keyboard).performClick()
            }
            await(scenario, "The explicit keyboard is open before the first rotation") { imeVisible(it) && input(it).hasFocus() }
            for ((requested, expected) in listOf(
                ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE to Configuration.ORIENTATION_LANDSCAPE,
                ActivityInfo.SCREEN_ORIENTATION_PORTRAIT to Configuration.ORIENTATION_PORTRAIT
            )) {
                scenario.onActivity { it.requestedOrientation = requested }
                await(scenario, "The already requested IME remains open after rotating to $expected") {
                    it.resources.configuration.orientation == expected && it.hasWindowFocus() &&
                        imeVisible(it) && input(it).hasFocus()
                }
                if (expected == Configuration.ORIENTATION_LANDSCAPE) {
                    var previousBounds = emptyList<Rect>()
                    var stableSamples = 0
                    await(scenario, "The landscape result and IME arrows finish layout above the keyboard") { activity ->
                        val panels = listOf(activity.findViewById<View>(R.id.calculator_scroll),
                            result(activity), activity.findViewById(R.id.calculator_result_row),
                            activity.findViewById(R.id.calculator_ime_navigation))
                        val currentBounds = panels.map(::screenBounds)
                        if (currentBounds == previousBounds && panels.all { it.width > 0 && it.height > 0 && !it.isLayoutRequested }) {
                            stableSamples++
                        } else stableSamples = 0
                        previousBounds = currentBounds
                        stableSamples >= 3
                    }
                    scenario.onActivity { activity ->
                        assertFullyVisibleAboveIme(activity, result(activity), "Result renderer")
                        assertFullyVisibleAboveIme(activity, activity.findViewById(R.id.calculator_result_row), "Result and Paste row")
                        assertFullyVisibleAboveIme(activity, key(activity, R.id.calculator_ime_left), "Left IME arrow")
                        assertFullyVisibleAboveIme(activity, key(activity, R.id.calculator_ime_right), "Right IME arrow")
                    }
                }
                scenario.onActivity { activity ->
                    assertEquals("Rotation keeps the AST and the exact interior text cursor", draft, input(activity).model.saveDraft())
                    assertEquals("DEG", key(activity, R.id.calculator_angle_mode).text.toString())
                    assertTrue(advancedMode(activity).isChecked)
                    assertEquals(0.5, scalar(result(activity).expression), 1e-10)
                    assertTrue(key(activity, R.id.calculator_paste_result).isShown)
                    assertTrue(key(activity, R.id.calculator_paste_result).isEnabled)
                    assertFalse(keypadScroll(activity).isShown)
                    assertEquals("The hidden advanced grid keeps its personalized order", order,
                        tree(activity.findViewById(R.id.calculator_keypad)).filterIsInstance<Button>().map { it.id })
                }
            }
            scenario.onActivity { activity ->
                activity.getSystemService(InputMethodManager::class.java)
                    .hideSoftInputFromWindow(input(activity).windowToken, 0)
            }
            await(scenario, "Closing the rotated IME restores the customized mathematical grid") { !imeVisible(it) && keypadScroll(it).isShown }
            var editorBounds = Rect()
            scenario.onActivity { activity ->
                assertEquals(draft, input(activity).model.saveDraft())
                assertEquals(order, gridKeyIds(activity))
                editorBounds = screenBounds(input(activity))
            }
            tap(editorBounds)
            SystemClock.sleep(400)
            scenario.onActivity { activity ->
                assertFalse("Editor touches remain opt-in after an IME-open rotation", imeVisible(activity))
                assertEquals(parse("\\sin{30}"), input(activity).model.expression)
                assertEquals(0.5, scalar(result(activity).expression), 1e-10)
            }
        }
    }

    @Test fun subscriptKeyCreatesARealEditableIndexAndPreservesItsDraftOnNumericDomainError() {
        ActivityScenario.launch<CalculatorActivity>(Intent(context, CalculatorActivity::class.java)).use { scenario ->
            var draft = ""
            scenario.onActivity { activity ->
                key(activity, R.id.calculator_key_2).performClick()
                key(activity, R.id.calculator_key_subscript).performClick()
                val indexed = input(activity).model.expression.children.single() as MathExpression.Scripts
                assertEquals(parse("2"), indexed.base)
                assertTrue("The lower index is a genuine AST slot", indexed.lower != null)
                key(activity, R.id.calculator_key_1).performClick()
                draft = input(activity).model.saveDraft()
                key(activity, R.id.calculator_evaluate).performClick()
            }
            await(scenario, "A free numeric subscript receives a controlled unsupported-domain error") {
                key(it, R.id.calculator_evaluate).isEnabled &&
                    it.findViewById<TextView>(R.id.calculator_error).text.isNotBlank() &&
                    it.findViewById<View>(R.id.calculator_result_host).visibility == View.GONE
            }
            scenario.onActivity { activity ->
                assertEquals(draft, input(activity).model.saveDraft())
                assertTrue(result(activity).expression.children.isEmpty())
                assertFalse(key(activity, R.id.calculator_paste_result).isEnabled)
            }
        }
    }

    @Test fun advancedToolsAndModeSurviveRecreationWithoutChangingDraftResultOrAngle() {
        ActivityScenario.launch<CalculatorActivity>(Intent(context, CalculatorActivity::class.java)).use { scenario ->
            scenario.onActivity { activity ->
                assertAdvancedTools(activity, visible = false)
                key(activity, R.id.calculator_angle_mode).performClick()
                assertTrue(input(activity).setLatex("\\sin{30}"))
                key(activity, R.id.calculator_evaluate).performClick()
            }
            await(scenario, "A degree-mode result is ready before switching tools") { completedResult(it) }
            var draft = ""
            scenario.onActivity { activity ->
                draft = input(activity).model.saveDraft()
                advancedMode(activity).performClick()
                assertAdvancedTools(activity, visible = true)
                assertEquals(draft, input(activity).model.saveDraft())
                assertEquals("DEG", key(activity, R.id.calculator_angle_mode).text.toString())
                assertEquals(0.5, scalar(result(activity).expression), 1e-10)
                assertTrue(completedResult(activity))
            }
            scenario.recreate()
            scenario.onActivity { activity ->
                assertTrue(advancedMode(activity).isChecked)
                assertAdvancedTools(activity, visible = true)
                assertEquals(draft, input(activity).model.saveDraft())
                assertEquals("DEG", key(activity, R.id.calculator_angle_mode).text.toString())
                assertEquals(0.5, scalar(result(activity).expression), 1e-10)
                advancedMode(activity).performClick()
                assertAdvancedTools(activity, visible = false)
                assertEquals(draft, input(activity).model.saveDraft())
                assertEquals(0.5, scalar(result(activity).expression), 1e-10)
                assertTrue(completedResult(activity))
            }
        }
    }

    @Test fun advancedKeysBuildSemanticExpressionsAndRunTheProductionEngine() {
        ActivityScenario.launch<CalculatorActivity>(Intent(context, CalculatorActivity::class.java)).use { scenario ->
            scenario.onActivity { activity ->
                advancedMode(activity).performClick()
                key(activity, R.id.calculator_angle_mode).performClick()
            }
            for ((keys, expected) in listOf(
                listOf(R.id.calculator_key_5, R.id.calculator_key_combinations, R.id.calculator_key_2) to 10.0,
                listOf(R.id.calculator_key_5, R.id.calculator_key_permutations, R.id.calculator_key_2) to 20.0,
                listOf(R.id.calculator_key_5, R.id.calculator_key_factorial) to 120.0,
                listOf(R.id.calculator_key_asin, R.id.calculator_key_0, R.id.calculator_key_decimal,
                    R.id.calculator_key_5) to 30.0,
                listOf(R.id.calculator_key_acos, R.id.calculator_key_0, R.id.calculator_key_decimal,
                    R.id.calculator_key_5) to 60.0,
                listOf(R.id.calculator_key_atan, R.id.calculator_key_1) to 45.0
            )) {
                scenario.onActivity { activity ->
                    key(activity, R.id.calculator_clear).performClick()
                    keys.forEach { key(activity, it).performClick() }
                    key(activity, R.id.calculator_evaluate).performClick()
                }
                await(scenario, "An advanced key expression reaches the real engine") { completedResult(it) }
                scenario.onActivity { assertEquals(expected, scalar(result(it).expression), 1e-10) }
            }
            scenario.onActivity { activity ->
                val editor = input(activity)
                assertTrue(editor.setLatex("27"))
                editor.model.select(emptyList(), 0, editor.model.expression.children.size)
                key(activity, R.id.calculator_key_nth_root).performClick()
                val root = editor.model.expression.children.single() as MathExpression.Root
                assertEquals(parse("27"), root.radicand)
                assertTrue("An n-th root owns an editable index slot", root.index != null)
                key(activity, R.id.calculator_key_3).performClick()
                key(activity, R.id.calculator_evaluate).performClick()
            }
            await(scenario, "The n-th-root button evaluates its selected radicand and entered index") { completedResult(it) }
            scenario.onActivity { activity ->
                assertEquals(3.0, scalar(result(activity).expression), 1e-10)
                val editor = input(activity)
                assertTrue(editor.setLatex("-5"))
                editor.model.select(emptyList(), 0, editor.model.expression.children.size)
                key(activity, R.id.calculator_key_absolute).performClick()
                val absolute = editor.model.expression.children.single() as MathExpression.Group
                assertEquals(MathDelimiter.ABSOLUTE, absolute.delimiter)
                assertEquals(parse("-5"), absolute.content)
                key(activity, R.id.calculator_evaluate).performClick()
            }
            await(scenario, "The absolute-value button evaluates the semantic group") { completedResult(it) }
            scenario.onActivity { assertEquals(5.0, scalar(result(it).expression), 1e-10) }
        }
    }

    @Test fun calculatorFillsTheAvailableSurfaceInPortraitAndLandscape() {
        ActivityScenario.launch<CalculatorActivity>(Intent(context, CalculatorActivity::class.java)).use { scenario ->
            for ((requested, expected) in listOf(
                ActivityInfo.SCREEN_ORIENTATION_PORTRAIT to Configuration.ORIENTATION_PORTRAIT,
                ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE to Configuration.ORIENTATION_LANDSCAPE
            )) {
                scenario.onActivity { it.requestedOrientation = requested }
                await(scenario, "The edge-to-edge surface settles in orientation $expected") { activity ->
                    val panel = activity.findViewById<View>(R.id.calculator_scroll)
                    activity.resources.configuration.orientation == expected && activity.hasWindowFocus() &&
                        panel.width > 0 && panel.height > 0 && !panel.isLayoutRequested
                }
                scenario.onActivity { activity ->
                    val panel = activity.findViewById<View>(R.id.calculator_scroll)
                    val root = panel.parent as View
                    val available = screenBounds(root).apply {
                        left += root.paddingLeft
                        top += root.paddingTop
                        right -= root.paddingRight
                        bottom -= root.paddingBottom
                    }
                    assertEquals("No outer floating-window margins remain", available, screenBounds(panel))
                    assertEquals(View.GONE, activity.findViewById<View>(R.id.calculator_paste_result).visibility)
                }
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
                activity.openCalculatorFor(answer)
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
                it.openCalculatorFor(answer)
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

    @Test fun pasteReturnsToTheConcreteAnswerAfterRotatingTheCalculator() {
        ProgressRepository.resetLesson(context, route, lesson)
        ProgressRepository.saveCurrentPage(context, route, lesson, 10)
        val intent = Intent(context, LessonActivity::class.java)
            .putExtra("routeId", route)
            .putExtra("courseId", "math_course_01")
            .putExtra("unitId", "arithmetic_fundamentals")
            .putExtra("lessonId", lesson)
            .putExtra("lessonNumber", 1)
            .putExtra("lessonTotal", 1)
        ActivityScenario.launch<LessonActivity>(intent).use { scenario ->
            lateinit var answer: EditText
            lateinit var graph: MathCartesianView
            scenario.onActivity { activity ->
                val controls = tree(activity.findViewById(R.id.lessonContent))
                answer = controls.filterIsInstance<EditText>()
                    .single { it.contentDescription?.toString() == "Coordenada x" }
                graph = controls.filterIsInstance<MathCartesianView>().single()
                answer.setText("91")
                graph.movePoint(0, 2.0, 3.0)
                assertTrue("The actual answer exposes its calculator shortcut", answer.compoundDrawablesRelative[2] != null)
                val now = SystemClock.uptimeMillis()
                val release = MotionEvent.obtain(now, now, MotionEvent.ACTION_UP,
                    answer.width.toFloat() - 1, answer.height / 2f, 0)
                try { assertTrue(answer.dispatchTouchEvent(release)) } finally { release.recycle() }
            }
            var calculator = resumedCalculator()
            try {
                onMain {
                    assertEquals(View.VISIBLE, key(calculator, R.id.calculator_paste_result).visibility)
                    assertFalse(key(calculator, R.id.calculator_paste_result).isEnabled)
                    key(calculator, R.id.calculator_angle_mode).performClick()
                    advancedMode(calculator).performClick()
                    assertTrue(input(calculator).setLatex("8/4"))
                    key(calculator, R.id.calculator_evaluate).performClick()
                }
                awaitResult(calculator)
                var draft = ""
                onMain { draft = input(calculator).model.saveDraft() }
                for ((requested, expected) in listOf(
                    ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE to Configuration.ORIENTATION_LANDSCAPE,
                    ActivityInfo.SCREEN_ORIENTATION_PORTRAIT to Configuration.ORIENTATION_PORTRAIT
                )) {
                    onMain { calculator.requestedOrientation = requested }
                    awaitCondition("The returning calculator restores orientation $expected") {
                        var ready = false
                        onMain {
                            val resumed = ActivityLifecycleMonitorRegistry.getInstance().getActivitiesInStage(Stage.RESUMED)
                                .filterIsInstance<CalculatorActivity>().singleOrNull()
                            if (resumed != null && resumed.resources.configuration.orientation == expected && resumed.hasWindowFocus()) {
                                calculator = resumed
                                ready = true
                            }
                        }
                        ready
                    }
                    onMain {
                        assertEquals(draft, input(calculator).model.saveDraft())
                        assertEquals("DEG", key(calculator, R.id.calculator_angle_mode).text.toString())
                        assertTrue(advancedMode(calculator).isChecked)
                        assertEquals(2.0, scalar(result(calculator).expression), 1e-10)
                        assertEquals(View.VISIBLE, key(calculator, R.id.calculator_paste_result).visibility)
                        assertTrue(key(calculator, R.id.calculator_paste_result).isEnabled)
                        assertEquals("The answer is untouched until Paste is pressed", "91", answer.text.toString())
                    }
                }
                onMain { key(calculator, R.id.calculator_paste_result).performClick() }
                awaitCondition("The original lesson receives the result") {
                    var ready = false
                    scenario.onActivity { activity ->
                        ready = activity.hasWindowFocus() && answer.text.toString() == "2"
                    }
                    ready
                }
                scenario.onActivity { activity ->
                    val currentAnswer = tree(activity.findViewById(R.id.lessonContent)).filterIsInstance<EditText>()
                        .single { it.contentDescription?.toString() == "Coordenada x" }
                    assertSame("Rotation retains the concrete originating answer", answer, currentAnswer)
                    assertEquals("2", answer.text.toString())
                    assertEquals(answer.text.length, answer.selectionStart)
                    assertEquals("Pasting a scalar never changes the other response's coordinate", 2.0, graph.points().single().x, 0.0)
                    assertEquals(3.0, graph.points().single().y, 0.0)
                }
            } finally {
                onMain { if (!calculator.isDestroyed && !calculator.isFinishing) calculator.finish() }
                instrumentation.waitForIdleSync()
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
                    var lastSwiftKeyState = "No state sampled"
                    try {
                        await(scenario, "SwiftKey is visible and the custom keypad yields its space") { activity ->
                            val editor = input(activity)
                            val manager = activity.getSystemService(InputMethodManager::class.java)
                            if (activity.hasWindowFocus() && !imeVisible(activity)) {
                                manager.showSoftInput(editor, InputMethodManager.SHOW_IMPLICIT)
                            }
                            val editorFocused = editor.hasFocus()
                            val managerActive = manager.isActive(editor)
                            val insetsVisible = imeVisible(activity)
                            val imeBounds = imeWindowBounds()
                            val gridShown = activity.findViewById<View>(R.id.calculator_keypad).isShown
                            val navigation = activity.findViewById<View>(R.id.calculator_ime_navigation)
                            lastSwiftKeyState = "requestedOrientation=$requested, expectedOrientation=$expected, " +
                                "actualOrientation=${activity.resources.configuration.orientation}, " +
                                "windowFocus=${activity.hasWindowFocus()}, editorFocus=$editorFocused, " +
                                "managerActive=$managerActive, imeInsetsVisible=$insetsVisible, imeWindowBounds=$imeBounds, " +
                                "gridIsShown=$gridShown, navigationVisibility=${navigation.visibility}, navigationIsShown=${navigation.isShown}, " +
                                "editorBounds=${screenBounds(editor)}, scrollBounds=${screenBounds(keypadScroll(activity))}"
                            editorFocused && managerActive && insetsVisible && imeBounds.isNotEmpty() &&
                                !gridShown && navigation.visibility == View.VISIBLE
                        }
                    } catch (failure: AssertionError) {
                        throw AssertionError("${failure.message}. Last SwiftKey state: $lastSwiftKeyState", failure)
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
                    await(scenario, "The fixed evaluation control remains fully accessible above SwiftKey") { activity ->
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
                        !imeVisible(it) && it.findViewById<View>(R.id.calculator_keypad).isShown
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
    private fun advancedMode(activity: CalculatorActivity): SwitchCompat = activity.findViewById(R.id.calculator_advanced_mode)
    private fun keypadScroll(activity: CalculatorActivity): ScrollView = activity.findViewById(R.id.calculator_keypad_scroll)
    private fun gridKeys(activity: CalculatorActivity): List<Button> =
        tree(activity.findViewById(R.id.calculator_keypad)).filterIsInstance<Button>().filter { it.isShown }
    private fun gridKeyIds(activity: CalculatorActivity): List<Int> = gridKeys(activity).map { it.id }

    private fun assertSelectedGray(button: Button) {
        val tint = requireNotNull(button.backgroundTintList).getColorForState(button.drawableState, Color.TRANSPARENT)
        val channels = listOf(Color.red(tint), Color.green(tint), Color.blue(tint))
        assertTrue("The selected swap key uses a neutral gray tint", channels.max() - channels.min() <= 20)
    }

    private fun assertFullyVisibleAboveIme(activity: CalculatorActivity, view: View, label: String) {
        val expected = screenBounds(view)
        val visible = Rect()
        val isVisible = view.getGlobalVisibleRect(visible)
        val available = screenBounds(activity.findViewById(R.id.calculator_scroll))
        val root = activity.findViewById<View>(android.R.id.content)
        val imeBottom = ViewCompat.getRootWindowInsets(activity.window.decorView)
            ?.getInsets(WindowInsetsCompat.Type.ime())?.bottom
        val detail = "$label: measured=${view.width}×${view.height}, expected=$expected, visible=$visible, " +
            "available=$available, content=${root.width}×${root.height}, imeBottom=$imeBottom, " +
            "density=${activity.resources.displayMetrics.density}"
        assertTrue("The whole control must be visible above the IME. $detail", isVisible)
        assertEquals("The control cannot be clipped by the reduced landscape viewport. $detail", expected, visible)
        assertTrue("The complete control must stay within the available surface. $detail", available.contains(expected))
    }

    private fun swapped(order: List<Int>, first: Int, second: Int): List<Int> = order.toMutableList().apply {
        val firstPosition = indexOf(first)
        val secondPosition = indexOf(second)
        check(firstPosition >= 0 && secondPosition >= 0)
        this[firstPosition] = second
        this[secondPosition] = first
    }

    private fun fixedPanelBounds(activity: CalculatorActivity): List<Rect> = listOf(
        screenBounds(activity.findViewById(R.id.calculator_header)),
        screenBounds(input(activity)),
        screenBounds(result(activity)),
        screenBounds(key(activity, R.id.calculator_evaluate))
    )

    private fun assertAdvancedTools(activity: CalculatorActivity, visible: Boolean) {
        for (id in listOf(R.id.calculator_key_asin, R.id.calculator_key_acos, R.id.calculator_key_atan,
            R.id.calculator_key_nth_root, R.id.calculator_key_absolute, R.id.calculator_key_factorial,
            R.id.calculator_key_combinations, R.id.calculator_key_permutations)) {
            val advancedKey = activity.findViewById<View>(id)
            assertEquals("Advanced key $id follows the single calculator mode", visible, advancedKey?.isShown == true)
        }
        assertTrue("Percentage remains in the basic keypad", key(activity, R.id.calculator_key_percent).isShown)
    }

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

    /** Scroll the actual key-grid viewport while all fixed panels stay outside its gesture. */
    private fun swipeUp(bounds: Rect) {
        val automation = instrumentation.uiAutomation
        val downTime = SystemClock.uptimeMillis()
        val x = bounds.exactCenterX()
        val startY = bounds.top + bounds.height() * 0.8f
        val endY = bounds.top + bounds.height() * 0.2f
        fun event(action: Int, y: Float) {
            val motion = MotionEvent.obtain(downTime, SystemClock.uptimeMillis(), action, x, y, 0)
                .apply { source = InputDevice.SOURCE_TOUCHSCREEN }
            try { assertTrue(automation.injectInputEvent(motion, true)) } finally { motion.recycle() }
        }
        event(MotionEvent.ACTION_DOWN, startY)
        for (step in 1..12) {
            SystemClock.sleep(18)
            event(MotionEvent.ACTION_MOVE, startY + (endY - startY) * step / 12f)
        }
        event(MotionEvent.ACTION_UP, endY)
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
