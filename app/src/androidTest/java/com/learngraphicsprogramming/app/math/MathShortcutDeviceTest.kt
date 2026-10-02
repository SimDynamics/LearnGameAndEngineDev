package com.learngraphicsprogramming.app.math

import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.os.ParcelFileDescriptor
import android.os.SystemClock
import android.view.View
import android.view.ViewGroup
import android.view.inputmethod.InputMethodManager
import android.widget.Button
import android.widget.EditText
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.LessonActivity
import com.learngraphicsprogramming.app.LessonTextSizeSettingsRepository
import com.learngraphicsprogramming.app.ProgressRepository
import com.learngraphicsprogramming.app.R
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/** Exercises parser → field metadata → focus → real shortcut buttons → unchanged validators. */
@RunWith(AndroidJUnit4::class)
class MathShortcutDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val lesson = "math_course_01__arithmetic_fundamentals__math_puzzle_sandbox"
    private val snapshots = linkedMapOf<String, Map<String, *>>()
    private val accent = 0xff66cc55.toInt()

    @Before fun prepare() {
        listOf("input keyevent KEYCODE_WAKEUP", "wm dismiss-keyguard").forEach { command ->
            ParcelFileDescriptor.AutoCloseInputStream(instrumentation.uiAutomation.executeShellCommand(command)).use { it.readBytes() }
        }
        listOf("learning_progress", "navigation_state", "lesson_text_size_settings").forEach {
            snapshots[it] = context.getSharedPreferences(it, Context.MODE_PRIVATE).all.toMap()
        }
        ProgressRepository.resetLesson(context, "math_physics", lesson)
        LessonTextSizeSettingsRepository.saveUsesGlobalScale(context, "math_course_01", true)
        LessonTextSizeSettingsRepository.saveGlobalScale(context, 100)
    }

    @After fun restore() {
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

    @Test fun sandboxPiAndEquationHaveOnlyRequiredKeysAndInsertedSymbolsValidate() {
        val puzzles = sandboxPuzzles()
        val piSpec = puzzles.single { it.first.optString("id") == "pi_expression" }.second.responses.single()
        val equationSpec = puzzles.flatMap { it.second.responses }.single { it.id == "equation" }
        ActivityScenario.launch<LessonActivity>(intent()).use { scenario ->
            lateinit var pi: BoundControl
            scenario.onActivity { activity ->
                pi = install(activity, piSpec)
                val input = pi.inputs.single()
                input.setText("4")
                input.requestFocus()
                input.setSelection(input.text.length)
                assertKeys(pi, listOf("π"))
                key(pi, "π").performClick()
                assertEquals("4π", input.text.toString())
                assertTrue(MathValidators.validate(piSpec.validator, pi.control.answer()).correct)
                (activity.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager)
                    .showSoftInput(input, InputMethodManager.SHOW_IMPLICIT)
            }
            awaitIme(scenario, pi.inputs.single())
            instrumentation.waitForIdleSync()
            SystemClock.sleep(300)
            scenario.onActivity {
                assertTrue("La barra contextual se conecta a la ventana sobre IME", pi.keyboard.isAttachedToWindow)
                assertEquals(View.VISIBLE, pi.keyboard.visibility)
                assertKeys(pi, listOf("π"))
                assertTrue(tree(pi.keyboard).filterIsInstance<Button>().all { kotlin.math.abs(it.width - it.height) <= 2 })
            }
            screenshot("contextual-pi-shortcuts.png")
            scenario.onActivity { activity ->
                val equation = install(activity, equationSpec)
                val input = equation.inputs.single()
                input.setText("y3x2")
                input.requestFocus()
                assertKeys(equation, listOf("=", "+"))
                input.setSelection(1)
                key(equation, "=").performClick()
                input.setSelection(input.text.indexOf('x') + 1)
                key(equation, "+").performClick()
                assertEquals("y=3x+2", input.text.toString())
                assertTrue(MathValidators.validate(equationSpec.validator, equation.control.answer()).correct)
            }
        }
    }

    @Test fun sandboxIntegerFractionAndDecimalsDoNotInheritAlgebraOrPiKeys() {
        val responses = sandboxPuzzles().flatMap { it.second.responses }
        val fractionSpec = responses.single { it.id == "fraction" }
        val integerSpec = responses.single { it.id == "median" }
        val decimalSpec = responses.single { it.id == "difference" }
        val approximationSpec = responses.single { it.id == "approximation" }
        ActivityScenario.launch<LessonActivity>(intent()).use { scenario ->
            scenario.onActivity { activity ->
                val fraction = install(activity, fractionSpec)
                val input = fraction.inputs.single()
                input.setText("3")
                input.requestFocus()
                input.setSelection(1)
                assertKeys(fraction, listOf("/"))
                key(fraction, "/").performClick()
                input.text.append("4")
                assertEquals("3/4", input.text.toString())
                assertTrue(MathValidators.validate(fractionSpec.validator, fraction.control.answer()).correct)

                val integer = install(activity, integerSpec)
                integer.inputs.single().requestFocus()
                assertKeys(integer, emptyList())
                integer.inputs.single().setText("2")
                assertTrue(MathValidators.validate(integerSpec.validator, integer.control.answer()).correct)

                val decimal = install(activity, decimalSpec)
                decimal.inputs.single().requestFocus()
                assertKeys(decimal, listOf("."))
                decimal.inputs.single().setText("7")
                decimal.inputs.single().setSelection(1)
                key(decimal, ".").performClick()
                decimal.inputs.single().text.append("5")
                assertTrue(MathValidators.validate(decimalSpec.validator, decimal.control.answer()).correct)

                val approximation = install(activity, approximationSpec)
                approximation.inputs.single().requestFocus()
                assertKeys(approximation, listOf("."))
                approximation.inputs.single().setText("0.33")
                assertTrue(MathValidators.validate(approximationSpec.validator, approximation.control.answer()).correct)
            }
        }
    }

    @Test fun structuredFieldsUseTheirOwnValidatorAndLiteralInequalityKeepsItsOperator() {
        val tableSpec = sandboxPuzzles().flatMap { it.second.responses }.single { it.id == "table" }
        // There is no textual inequality field in the current sandbox. Parse the same public
        // content contract rather than passing precomputed shortcuts directly to the keyboard.
        val inequalitySpec = MathPuzzleParser.parse(JSONObject("""{
            "type":"math_expression_input","prompt":"Escribe la desigualdad indicada.",
            "keyboard":"INEQUALITY","validator":{"type":"exact_text","expected":"x ≥ 2"}
        }""")).responses.single()
        ActivityScenario.launch<LessonActivity>(intent()).use { scenario ->
            scenario.onActivity { activity ->
                val table = install(activity, tableSpec)
                assertEquals(3, table.inputs.size)
                val negative = table.inputs[0]
                negative.requestFocus()
                assertKeys(table, listOf("−"))
                negative.setText("1")
                negative.setSelection(0)
                key(table, "−").performClick()
                assertEquals("-1", negative.text.toString())
                table.inputs[1].requestFocus()
                assertKeys(table, emptyList())
                table.inputs[1].setText("1")
                table.inputs[2].requestFocus()
                assertKeys(table, emptyList())
                table.inputs[2].setText("5")
                assertTrue(MathValidators.validate(tableSpec.validator, table.control.answer()).correct)

                val inequality = install(activity, inequalitySpec)
                val input = inequality.inputs.single()
                input.requestFocus()
                assertKeys(inequality, listOf("≥"))
                input.setText("x 2")
                input.setSelection(2)
                key(inequality, "≥").performClick()
                input.text.insert(3, " ")
                assertEquals("x ≥ 2", input.text.toString())
                assertTrue(MathValidators.validate(inequalitySpec.validator, inequality.control.answer()).correct)
            }
        }
    }

    private data class BoundControl(val control: MathResponseControl, val inputs: List<EditText>, val keyboard: MathKeyboardView)

    private fun install(activity: LessonActivity, spec: MathResponseSpec): BoundControl {
        val control = MathInputControls.create(activity, spec, accent) {} ?: error("Respuesta no textual")
        val initial = tree(control.view)
        val bound = BoundControl(control, initial.filterIsInstance<EditText>(), initial.filterIsInstance<MathKeyboardView>().single())
        activity.findViewById<ViewGroup>(R.id.lessonContent).apply { removeAllViews(); addView(control.view) }
        MathInputFocusOrder.configure(control.view)
        return bound
    }

    private fun assertKeys(bound: BoundControl, expected: List<String>) {
        val labels = tree(bound.keyboard).filterIsInstance<Button>().map { it.text.toString() }
        assertEquals("Atajos exactos para el campo enfocado", expected + "⌫", labels)
        assertFalse("Las letras y los números quedan en el teclado Android", labels.any { it.matches(Regex("[A-Za-z0-9]")) })
        assertEquals(labels.size, labels.distinct().size)
    }

    private fun key(bound: BoundControl, symbol: String): Button =
        tree(bound.keyboard).filterIsInstance<Button>().single { it.text.toString() == symbol }

    private fun sandboxPuzzles(): List<Pair<JSONObject, MathPuzzleSpec>> {
        val asset = "content/es/math_physics/math_course_01/arithmetic_fundamentals/lesson_math_puzzle_sandbox.json"
        val pages = JSONObject(context.assets.open(asset).bufferedReader().use { it.readText() }).getJSONArray("pages")
        return (0 until pages.length()).flatMap { page ->
            val blocks = pages.getJSONObject(page).getJSONArray("blocks")
            (0 until blocks.length()).mapNotNull { index ->
                val block = blocks.getJSONObject(index)
                if (MathPuzzleTypes.isExercise(block.optString("type"))) block to MathPuzzleParser.parse(block) else null
            }
        }
    }

    private fun awaitIme(scenario: ActivityScenario<LessonActivity>, input: EditText) {
        var shown = false
        repeat(15) {
            if (!shown) {
                SystemClock.sleep(200)
                scenario.onActivity { activity ->
                    shown = ViewCompat.getRootWindowInsets(activity.window.decorView)?.isVisible(WindowInsetsCompat.Type.ime()) == true
                    if (!shown && activity.hasWindowFocus()) {
                        input.requestFocus()
                        (activity.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager)
                            .showSoftInput(input, InputMethodManager.SHOW_IMPLICIT)
                    }
                }
            }
        }
        assertTrue("El teclado Android real debe abrirse para revisar π", shown)
    }

    private fun intent() = Intent(context, LessonActivity::class.java)
        .putExtra("routeId", "math_physics").putExtra("courseId", "math_course_01")
        .putExtra("unitId", "arithmetic_fundamentals").putExtra("lessonId", lesson).putExtra("lessonNumber", 4)

    private fun tree(view: View): List<View> = listOf(view) + if (view is ViewGroup)
        (0 until view.childCount).flatMap { tree(view.getChildAt(it)) } else emptyList()

    private fun screenshot(name: String) {
        val bitmap = instrumentation.uiAutomation.takeScreenshot() ?: error("No se pudo capturar barra contextual")
        if (android.os.Build.VERSION.SDK_INT >= 31) {
            ParcelFileDescriptor.AutoCloseInputStream(instrumentation.uiAutomation.executeShellCommand("mkdir -p /data/local/tmp/math-puzzle-smoke")).use { it.readBytes() }
            val streams = instrumentation.uiAutomation.executeShellCommandRw("dd of=/data/local/tmp/math-puzzle-smoke/$name")
            ParcelFileDescriptor.AutoCloseOutputStream(streams[1]).use { assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) }
            ParcelFileDescriptor.AutoCloseInputStream(streams[0]).use { it.readBytes() }
        }
        bitmap.recycle()
    }
}
