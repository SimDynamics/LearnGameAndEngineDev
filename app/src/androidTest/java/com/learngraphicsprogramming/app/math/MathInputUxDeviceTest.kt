package com.learngraphicsprogramming.app.math

import android.content.Context
import android.content.Intent
import android.content.pm.ActivityInfo
import android.content.res.Configuration
import android.graphics.Rect
import android.view.View
import android.view.MotionEvent
import android.view.InputDevice
import android.view.ViewGroup
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputMethodManager
import android.text.InputType
import android.text.method.TextKeyListener
import android.util.TypedValue
import android.widget.Button
import android.widget.EditText
import android.widget.HorizontalScrollView
import android.widget.RadioButton
import android.widget.Spinner
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

/** Exercises the composed controls on Android without changing the user's saved answers. */
@RunWith(AndroidJUnit4::class)
class MathInputUxDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val snapshots = linkedMapOf<String, Map<String, *>>()
    private val lesson = "math_course_01__arithmetic_fundamentals__math_puzzle_sandbox"
    private val accent = 0xffa9e96f.toInt()

    @Before fun preserveUserPreferences() {
        for (command in listOf("input keyevent KEYCODE_WAKEUP", "wm dismiss-keyguard")) {
            android.os.ParcelFileDescriptor.AutoCloseInputStream(instrumentation.uiAutomation.executeShellCommand(command)).use { it.readBytes() }
        }
        listOf("learning_progress", "navigation_state", "lesson_text_size_settings").forEach {
            snapshots[it] = context.getSharedPreferences(it, Context.MODE_PRIVATE).all.toMap()
        }
        ProgressRepository.saveCurrentPage(context, "math_physics", lesson, 0)
    }

    @After fun restoreUserPreferences() {
        snapshots.forEach { (name, original) ->
            val preferences = context.getSharedPreferences(name, Context.MODE_PRIVATE)
            val editor = preferences.edit().clear()
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

    @Test fun imeNextUsesVisualRowsThenNextResponseAndDone() {
        val config = JSONObject("""{
            "layout":"table","headers":["X","Y"],
            "fields":[{"id":"x1"},{"id":"x2"},{"id":"y1"},{"id":"y2"}],
            "rows":[[{"field":"x1"},{"field":"y1"}],[{"field":"x2"},{"field":"y2"}]]
        }""")
        withControls({ activity ->
            MathWidgetUi.column(activity).apply {
                addView(control(activity, "math_multi_input", config).view)
                addView(control(activity, "math_numeric_input", JSONObject().put("label", "Resultado final")).view)
                MathInputFocusOrder.configure(this)
            }
        }) { _, root ->
            val inputs = descendants(root).filterIsInstance<EditText>()
            assertEquals(listOf("x1", "y1", "x2", "y2", "Resultado final"), inputs.map { it.contentDescription.toString() })
            inputs.forEachIndexed { index, input -> input.setText("$index.25") }
            assertTrue(inputs.first().requestFocus())
            inputs.dropLast(1).forEachIndexed { index, input ->
                assertEquals(EditorInfo.IME_ACTION_NEXT, input.imeOptions and EditorInfo.IME_MASK_ACTION)
                assertTrue(input.imeOptions and EditorInfo.IME_FLAG_NO_EXTRACT_UI != 0)
                assertTrue(input.imeOptions and EditorInfo.IME_FLAG_NO_FULLSCREEN != 0)
                assertEquals(0, input.imeOptions and EditorInfo.IME_FLAG_NO_ENTER_ACTION)
                assertEquals(inputs[index + 1].id, input.nextFocusForwardId)
                assertTrue(input.hasFocus())
                input.onEditorAction(EditorInfo.IME_ACTION_NEXT)
                assertTrue(inputs[index + 1].hasFocus())
                assertEquals(0, inputs[index + 1].selectionStart)
                assertEquals(inputs[index + 1].text.length, inputs[index + 1].selectionEnd)
            }
            assertEquals(EditorInfo.IME_ACTION_DONE, inputs.last().imeOptions and EditorInfo.IME_MASK_ACTION)
            inputs.last().onEditorAction(EditorInfo.IME_ACTION_DONE)
            assertFalse(inputs.last().hasFocus())
            inputs.last().isEnabled = false
            inputs[3].requestFocus()
            assertEquals(EditorInfo.IME_ACTION_DONE, inputs[3].imeOptions and EditorInfo.IME_MASK_ACTION)
            root.requestFocus()
            inputs.last().isEnabled = true
            inputs[3].requestFocus()
            assertEquals(EditorInfo.IME_ACTION_NEXT, inputs[3].imeOptions and EditorInfo.IME_MASK_ACTION)
            assertEquals(0, inputs[3].selectionStart)
            assertEquals(inputs[3].text.length, inputs[3].selectionEnd)
        }
    }

    @Test fun matchingMovesTheSameSourceViewAndReturnsDisplacedSources() {
        val config = JSONObject("""{
            "sources":[{"id":"a","label":"2 + 3"},{"id":"b","label":"3 × 4"}],
            "targets":[{"id":"five","label":"5"},{"id":"twelve","label":"12"}]
        }""")
        lateinit var response: MathResponseControl
        withControls({ activity ->
            control(activity, "math_matching_drag", config).also { response = it }.view
        }) { _, root ->
            val a = descendants(root).single { it.contentDescription == "Seleccionar ficha 2 + 3" }
            val b = descendants(root).single { it.contentDescription == "Seleccionar ficha 3 × 4" }
            val origin = a.parent as ViewGroup
            val five = descendants(root).single { it.contentDescription == "Destino 5" }
            val twelve = descendants(root).single { it.contentDescription == "Destino 12" }
            a.performClick(); five.performClick()
            assertNotSame(origin, a.parent)
            assertEquals(mapOf("a" to "five"), (response.answer() as MathAnswer.Matching).pairs)
            assertEquals(1, descendants(root).count { it === a })
            assertEquals(1, descendants(root).filterIsInstance<TextView>().count { it.text.toString() == "2 + 3" })
            a.performClick(); twelve.performClick()
            assertEquals(mapOf("a" to "twelve"), (response.answer() as MathAnswer.Matching).pairs)
            b.performClick(); twelve.performClick()
            assertSame(origin, a.parent)
            assertNotSame(origin, b.parent)
            assertEquals(mapOf("b" to "twelve"), (response.answer() as MathAnswer.Matching).pairs)
            twelve.performClick()
            assertSame(origin, b.parent)
            assertTrue((response.answer() as MathAnswer.Matching).pairs.isEmpty())
            response.restore(MathAnswer.Matching(mapOf("a" to "five")))
            assertNotSame(origin, a.parent)
            assertEquals(1, descendants(root).count { it === a })
            response.setEditable(false)
            a.performClick(); twelve.performClick()
            assertEquals(mapOf("a" to "five"), (response.answer() as MathAnswer.Matching).pairs)
        }
    }

    @Test fun richChoiceAppearsOnceStructuredFieldsCenterAndAllKeyboardModesHaveNoDigits() {
        lateinit var keyboard: MathKeyboardView
        withControls({ activity ->
            MathWidgetUi.column(activity).apply {
                addView(control(activity, "math_multiple_choice_single", JSONObject("""{
                    "options":[{"id":"half","label":"2/4","content":[{"type":"math_formula","text":"2/4"}]}]
                }""")).view)
                addView(control(activity, "math_structured_answer", JSONObject("""{
                    "fields":[{"id":"op","type":"dropdown","options":[{"id":"lt","label":"<"}]},{"id":"value"}],
                    "segments":[{"text":"−2"},{"field":"op"},{"field":"value"}]
                }""")).view)
                keyboard = MathKeyboardView(activity, accent)
                addView(keyboard)
                MathInputFocusOrder.configure(this)
            }
        }) { _, root ->
            val radio = descendants(root).filterIsInstance<RadioButton>().single()
            assertTrue(radio.text.isEmpty())
            assertEquals("2/4", radio.contentDescription.toString())
            assertEquals(1, descendants(root).filterIsInstance<TextView>().count { it.text.toString() == "2/4" })
            val horizontal = descendants(root).filterIsInstance<HorizontalScrollView>().first {
                it !is MathKeyboardView && (it.getChildAt(0) as? ViewGroup)?.childCount == 3
            }
            val row = horizontal.getChildAt(0) as ViewGroup
            val centers = (0 until row.childCount).map { childIndex -> row.getChildAt(childIndex).let { it.top + it.height / 2f } }
            assertTrue("Los segmentos se centran sin offsets de fuente", centers.max() - centers.min() <= 1f)
            assertNull(descendants(root).filterIsInstance<Spinner>().single().tag)
            val edit = descendants(root).filterIsInstance<EditText>().single()
            assertEquals(InputType.TYPE_CLASS_NUMBER, edit.inputType and InputType.TYPE_MASK_CLASS)
            assertTrue(edit.inputType and InputType.TYPE_NUMBER_FLAG_SIGNED != 0)
            assertTrue(edit.inputType and InputType.TYPE_NUMBER_FLAG_DECIMAL != 0)
            assertTrue(edit.keyListener is TextKeyListener)
            edit.text.replace(0, edit.text.length, "−3/4")
            assertEquals("−3/4", edit.text.toString())
            MathKeyboardView.Mode.entries.forEach { mode ->
                keyboard.bind(edit, mode.name)
                assertFalse("Sin dígitos en $mode", descendants(keyboard).filterIsInstance<Button>().any { it.text.toString().matches(Regex("[0-9]")) })
            }
        }
    }

    @Test fun inlineControlCentersMatchFixedMathAtNormalAndLargeTextScale() {
        val config = JSONObject("""{
            "fields":[
                {"id":"op","type":"dropdown","label":"Operador","options":[{"id":"lt","label":"<"}]},
                {"id":"right","label":"Límite"}
            ],
            "segments":[{"text":"−2"},{"field":"op"},{"field":"right"}]
        }""")
        for (factor in listOf(1f, 1.5f)) {
            withControls({ activity ->
                control(activity, "math_structured_answer", config).view.apply {
                    fun scale(view: View) {
                        if (view is MathTextScaleAware) view.scaleMathText(factor)
                        if (view is TextView) view.setTextSize(TypedValue.COMPLEX_UNIT_PX, view.textSize * factor)
                        if (view is ViewGroup) for (index in 0 until view.childCount) scale(view.getChildAt(index))
                    }
                    scale(this)
                }
            }) { activity, root ->
                val fixed = descendants(root).filterIsInstance<TextView>().single { it.text.toString() == "−2" }
                val spinner = descendants(root).filterIsInstance<Spinner>().single()
                val input = descendants(root).filterIsInstance<EditText>().single()
                fun center(view: View): Float {
                    val location = IntArray(2)
                    view.getLocationOnScreen(location)
                    return location[1] + view.height / 2f
                }
                val tolerance = MathWidgetUi.dp(activity, 2).toFloat()
                assertEquals("Spinner y texto fijo centrados a $factor", center(fixed), center(spinner), tolerance)
                assertEquals("Input y texto fijo centrados a $factor", center(fixed), center(input), tolerance)
                assertEquals("Operador", spinner.contentDescription.toString())
                assertEquals("Límite", input.contentDescription.toString())
                assertFalse(descendants(root).filterIsInstance<TextView>().any { it.text.toString() in listOf("Operador", "Límite") })
            }
        }
    }

    @Test fun orderingTapsFillSuccessiveSlotsWithoutArrowsOrInternalNumbers() {
        val config = JSONObject("""{
            "items":[{"id":"a","label":"−3"},{"id":"b","label":"0"},{"id":"c","label":"2"}],
            "initialOrder":["c","a","b"]
        }""")
        lateinit var response: MathResponseControl
        withControls({ activity ->
            control(activity, "math_ordering_drag", config).also { response = it }.view
        }) { _, root ->
            assertTrue(descendants(root).filterIsInstance<Button>().isEmpty())
            assertFalse(descendants(root).filterIsInstance<TextView>().any { it.text.toString().matches(Regex("[1-3]\\.-")) })
            listOf("a", "b", "c").forEach { id ->
                descendants(root).single { it.contentDescription?.startsWith("Ordenar tarjeta $id:") == true }.performClick()
            }
            assertEquals(listOf("a", "b", "c"), (response.answer() as MathAnswer.Order).ids)
            assertEquals(3, response.saveDraft()!!.getInt("nextTapPosition"))
            response.restore(MathAnswer.Order(listOf("c", "a", "b")))
            descendants(root).single { it.contentDescription?.startsWith("Ordenar tarjeta b:") == true }.performClick()
            assertEquals(listOf("b", "c", "a"), (response.answer() as MathAnswer.Order).ids)
        }
    }

    @Test fun directMatchingDragStartsFromTheRichCardWithoutLongPressOrClones() {
        val config = JSONObject("""{
            "sources":[{"id":"a","label":"2 + 3","content":[{"type":"math_formula","text":"2 + 3"}]}],
            "targets":[{"id":"five","label":"5"},{"id":"twelve","label":"12"}]
        }""")
        lateinit var response: MathResponseControl
        lateinit var source: View
        lateinit var origin: ViewGroup
        val from = FloatArray(2)
        val to = FloatArray(2)
        ActivityScenario.launch<LessonActivity>(lessonIntent()).use { scenario ->
            var originalOrientation = ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
            scenario.onActivity { activity ->
                originalOrientation = activity.requestedOrientation
                activity.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
            }
            try {
            awaitPortrait(scenario)
            scenario.onActivity { activity ->
                response = control(activity, "math_matching_drag", config)
                activity.findViewById<ViewGroup>(R.id.lessonContent).apply { removeAllViews(); addView(response.view) }
            }
            settleWindow(scenario, response.view)
            scenario.onActivity {
                source = descendants(response.view).single { it.contentDescription == "Seleccionar ficha 2 + 3" }
                origin = source.parent as ViewGroup
                val target = descendants(response.view).single { it.contentDescription == "Destino 5" }
                revealPair(response.view as ViewGroup, source, target)
                screenPoint(source, from, .85f)
                screenPoint(target, to)
            }
            drag(from, to)
            instrumentation.waitForIdleSync()
            // Moving the real source expands the first destination on the next layout frame.
            android.os.SystemClock.sleep(100)
            instrumentation.waitForIdleSync()
            scenario.onActivity {
                assertEquals(mapOf("a" to "five"), (response.answer() as MathAnswer.Matching).pairs)
                assertNotSame(origin, source.parent)
                assertEquals(1, descendants(response.view).count { it === source })
                val target = descendants(response.view).single { it.contentDescription == "Destino 12" }
                revealPair(response.view as ViewGroup, source, target)
                screenPoint(source, from, .85f)
                screenPoint(target, to)
            }
            drag(from, to)
            instrumentation.waitForIdleSync()
            scenario.onActivity {
                assertEquals(mapOf("a" to "twelve"), (response.answer() as MathAnswer.Matching).pairs)
                assertEquals(1, descendants(response.view).count { it === source })
            }
            } finally {
                scenario.onActivity { it.requestedOrientation = originalOrientation }
            }
        }
    }

    @Test fun richOptionTapWorksOverFormulaAndGraphSurfaces() {
        lateinit var response: MathResponseControl
        lateinit var root: View
        val point = FloatArray(2)
        ActivityScenario.launch<LessonActivity>(lessonIntent()).use { scenario ->
            scenario.onActivity { activity ->
                response = control(activity, "math_multiple_choice_single", JSONObject("""{
                    "options":[{"id":"half","label":"2/4","content":[
                        {"type":"math_formula","text":"2/4"},
                        {"type":"math_coordinate_graph","mode":"line","bounds":{"xMin":-5,"xMax":5,"yMin":-5,"yMax":5},
                         "points":[{"x":0,"y":0,"draggable":false},{"x":1,"y":1,"draggable":false}]}
                    ]}]
                }"""))
                root = response.view
                activity.findViewById<ViewGroup>(R.id.lessonContent).apply { removeAllViews(); addView(root) }
            }
            settleWindow(scenario, root)
            scenario.onActivity {
                val formula = descendants(root).filterIsInstance<MathTextView>().single { it.text.toString() == "2/4" }
                reveal(formula)
                screenPoint(formula, point)
            }
            tap(point)
            instrumentation.waitForIdleSync()
            scenario.onActivity {
                assertEquals(setOf("half"), (response.answer() as MathAnswer.Choice).selected)
                response.restore(MathAnswer.Choice(emptySet()))
                val graph = descendants(root).filterIsInstance<MathCartesianView>().single()
                reveal(graph)
                screenPoint(graph, point)
            }
            tap(point)
            instrumentation.waitForIdleSync()
            scenario.onActivity { assertEquals(setOf("half"), (response.answer() as MathAnswer.Choice).selected) }
        }
    }

    @Test fun directOrderingDragMovesACardWithoutLongPress() {
        lateinit var response: MathResponseControl
        lateinit var source: View
        val from = FloatArray(2)
        val to = FloatArray(2)
        ActivityScenario.launch<LessonActivity>(lessonIntent()).use { scenario ->
            scenario.onActivity { activity ->
                response = control(activity, "math_ordering_drag", JSONObject("""{
                    "items":[{"id":"a","label":"−3"},{"id":"b","label":"0"},{"id":"c","label":"2"}],
                    "initialOrder":["c","a","b"]
                }"""))
                activity.findViewById<ViewGroup>(R.id.lessonContent).apply { removeAllViews(); addView(response.view) }
            }
            settleWindow(scenario, response.view)
            scenario.onActivity {
                source = descendants(response.view).single { it.contentDescription?.startsWith("Ordenar tarjeta a:") == true }
                val target = descendants(response.view).single { it.contentDescription?.startsWith("Ordenar tarjeta c:") == true }
                revealPair(response.view as ViewGroup, source, target)
                screenPoint(source, from, .85f)
                screenPoint(target, to)
            }
            drag(from, to)
            instrumentation.waitForIdleSync()
            scenario.onActivity {
                assertEquals(listOf("a", "c", "b"), (response.answer() as MathAnswer.Order).ids)
                assertEquals(1, descendants(response.view).count { it === source })
                assertEquals(1f, source.alpha, 0f)
            }
        }
    }

    private fun screenPoint(view: View, target: FloatArray, xFraction: Float = .5f) {
        val visible = Rect()
        assertTrue("El control debe estar visible antes del gesto: ${view.contentDescription}", view.getGlobalVisibleRect(visible))
        assertTrue(visible.width() > 0 && visible.height() > 0)
        target[0] = visible.left + visible.width() * xFraction
        target[1] = visible.top + visible.height() * .5f
    }

    private fun reveal(view: View) {
        // Large canvases can exceed the lesson viewport in landscape. Reveal their center
        // region, then measure the actual clipped bounds instead of tapping below the fold.
        val halfRegion = minOf(view.height / 2, MathWidgetUi.dp(view.context, 24))
        val center = view.height / 2
        view.requestRectangleOnScreen(Rect(0, center - halfRegion, view.width, center + halfRegion), true)
    }

    private fun revealPair(root: ViewGroup, source: View, target: View) {
        val sourceBounds = Rect().also { source.getDrawingRect(it); root.offsetDescendantRectToMyCoords(source, it) }
        val targetBounds = Rect().also { target.getDrawingRect(it); root.offsetDescendantRectToMyCoords(target, it) }
        sourceBounds.union(targetBounds)
        root.requestRectangleOnScreen(sourceBounds, true)
    }

    private fun awaitPortrait(scenario: ActivityScenario<LessonActivity>) {
        val deadline = android.os.SystemClock.uptimeMillis() + 3000
        var ready = false
        while (!ready && android.os.SystemClock.uptimeMillis() < deadline) {
            scenario.onActivity {
                ready = it.hasWindowFocus() && it.resources.configuration.orientation == Configuration.ORIENTATION_PORTRAIT
            }
            if (!ready) android.os.SystemClock.sleep(30)
        }
        assertTrue("Matching necesita origen y destinos visibles simultáneamente", ready)
        instrumentation.waitForIdleSync()
        android.os.SystemClock.sleep(400)
    }

    private fun tap(point: FloatArray) {
        val started = android.os.SystemClock.uptimeMillis()
        pointer(started, MotionEvent.ACTION_DOWN, point[0], point[1])
        pointer(started, MotionEvent.ACTION_UP, point[0], point[1])
    }

    private fun drag(from: FloatArray, to: FloatArray) {
        val started = android.os.SystemClock.uptimeMillis()
        pointer(started, MotionEvent.ACTION_DOWN, from[0], from[1])
        for (step in 1..12) {
            android.os.SystemClock.sleep(24)
            val ratio = step / 12f
            pointer(started, MotionEvent.ACTION_MOVE, from[0] + (to[0] - from[0]) * ratio, from[1] + (to[1] - from[1]) * ratio)
        }
        pointer(started, MotionEvent.ACTION_UP, to[0], to[1])
    }

    private fun pointer(started: Long, action: Int, x: Float, y: Float) {
        val event = MotionEvent.obtain(started, android.os.SystemClock.uptimeMillis(), action, x, y, 0)
        event.source = InputDevice.SOURCE_TOUCHSCREEN
        try { assertTrue("El evento táctil debe inyectarse", instrumentation.uiAutomation.injectInputEvent(event, true)) }
        finally { event.recycle() }
    }

    private fun lessonIntent() = Intent(context, LessonActivity::class.java)
        .putExtra("routeId", "math_physics").putExtra("courseId", "math_course_01")
        .putExtra("unitId", "arithmetic_fundamentals").putExtra("lessonId", lesson).putExtra("lessonNumber", 4)

    private fun control(context: Context, type: String, config: JSONObject) =
        MathInputControls.create(context, MathResponseSpec("test", type, config, JSONObject()), accent) {}
            ?: MathDragControls.create(context, MathResponseSpec("test", type, config, JSONObject()), accent) {}
            ?: error("Control no reconocido")

    private fun withControls(build: (LessonActivity) -> View, assertion: (LessonActivity, View) -> Unit) {
        ActivityScenario.launch<LessonActivity>(lessonIntent()).use { scenario ->
            lateinit var root: View
            scenario.onActivity { activity ->
                root = build(activity)
                activity.findViewById<ViewGroup>(R.id.lessonContent).apply { removeAllViews(); addView(root) }
            }
            settleWindow(scenario, root)
            scenario.onActivity { activity -> assertion(activity, root) }
        }
    }

    private fun settleWindow(scenario: ActivityScenario<LessonActivity>, root: View) {
        scenario.onActivity { activity ->
            root.isFocusableInTouchMode = true
            root.requestFocus()
            (activity.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager)
                .hideSoftInputFromWindow(activity.window.decorView.windowToken, 0)
        }
        val deadline = android.os.SystemClock.uptimeMillis() + 3000
        var focused = false
        while (!focused && android.os.SystemClock.uptimeMillis() < deadline) {
            scenario.onActivity { focused = it.hasWindowFocus() }
            if (!focused) android.os.SystemClock.sleep(30)
        }
        assertTrue("La ventana debe recibir foco antes de inyectar gestos", focused)
        instrumentation.waitForIdleSync()
        android.os.SystemClock.sleep(400)
        instrumentation.waitForIdleSync()
    }

    private fun descendants(view: View): List<View> = listOf(view) +
        if (view is ViewGroup) (0 until view.childCount).flatMap { descendants(view.getChildAt(it)) } else emptyList()
}
