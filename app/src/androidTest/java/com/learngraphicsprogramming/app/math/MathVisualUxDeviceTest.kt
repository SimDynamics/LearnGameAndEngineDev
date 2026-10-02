package com.learngraphicsprogramming.app.math

import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.Rect
import android.graphics.RectF
import android.os.ParcelFileDescriptor
import android.os.SystemClock
import android.view.View
import android.view.ViewGroup
import android.view.MotionEvent
import android.widget.EditText
import android.widget.CheckBox
import android.widget.Spinner
import android.widget.TextView
import android.widget.ArrayAdapter
import androidx.test.core.app.ActivityScenario
import androidx.test.espresso.Espresso.onView
import androidx.test.espresso.Espresso.pressBack
import androidx.test.espresso.assertion.ViewAssertions.doesNotExist
import androidx.test.espresso.assertion.ViewAssertions.matches
import androidx.test.espresso.matcher.ViewMatchers.isDisplayed
import androidx.test.espresso.matcher.ViewMatchers.withTagValue
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.LessonActivity
import com.learngraphicsprogramming.app.LessonTextSizeSettingsRepository
import com.learngraphicsprogramming.app.ProgressRepository
import com.learngraphicsprogramming.app.R
import org.hamcrest.Matchers.equalTo
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/** Covers actual View reparenting and lifecycle, not a second graph with copied answer data. */
@RunWith(AndroidJUnit4::class)
class MathVisualUxDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val lesson = "math_course_01__arithmetic_fundamentals__math_puzzle_sandbox"
    private val snapshots = linkedMapOf<String, Map<String, *>>()

    @Before fun prepareLargeTextSandbox() {
        listOf("input keyevent KEYCODE_WAKEUP", "wm dismiss-keyguard").forEach { command ->
            ParcelFileDescriptor.AutoCloseInputStream(instrumentation.uiAutomation.executeShellCommand(command)).use { it.readBytes() }
        }
        listOf("learning_progress", "navigation_state", "lesson_text_size_settings").forEach {
            snapshots[it] = context.getSharedPreferences(it, Context.MODE_PRIVATE).all.toMap()
        }
        ProgressRepository.resetLesson(context, "math_physics", lesson)
        ProgressRepository.saveCurrentPage(context, "math_physics", lesson, 10)
        LessonTextSizeSettingsRepository.saveUsesGlobalScale(context, "math_course_01", true)
        LessonTextSizeSettingsRepository.saveGlobalScale(context, 150)
    }

    @After fun restorePreferences() {
        snapshots.forEach { (name, original) ->
            val preferences = context.getSharedPreferences(name, Context.MODE_PRIVATE)
            val editor = preferences.edit()
            (preferences.all.keys - original.keys).forEach(editor::remove)
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

    @Test fun graphUsesAvailableWidthAndSameViewSurvivesExpandBackAndRecreation() {
        lateinit var graph: MathCartesianView
        lateinit var originalParent: ViewGroup
        lateinit var originalRoot: View
        var inlineAspectRatio = 1f
        ActivityScenario.launch<LessonActivity>(intent()).use { scenario ->
            instrumentation.waitForIdleSync()
            scenario.onActivity { activity ->
                val content = activity.findViewById<ViewGroup>(R.id.lessonContent)
                graph = descendants(content).filterIsInstance<MathCartesianView>().first { it.editable }
                originalParent = graph.parent as ViewGroup
                originalRoot = graph.rootView
                val visualControl = originalParent.parent.parent as View
                assertTrue("Gráfica con ancho completo del control", graph.width >= visualControl.width * .95f)
                inlineAspectRatio = graph.width.toFloat() / graph.height
                val coordinates = pointCoordinateEditors(content)
                coordinates[0].setText("2")
                coordinates[1].setText("3")
                assertEquals(MathPoint(2.0, 3.0), graph.points().first())
                descendants(content).first { it.tag == "math_expand_representation" }.performClick()
            }
            instrumentation.waitForIdleSync()
            onView(withTagValue(equalTo("math_exit_representation"))).check(matches(isDisplayed()))
            scenario.onActivity {
                assertNotSame(originalParent, graph.parent)
                assertNotSame(originalRoot, graph.rootView)
                assertTrue(graph.editable)
                assertEquals("math_gesture_owner", graph.tag)
                val visualBounds = Rect()
                val exitBounds = Rect()
                assertTrue(graph.getGlobalVisibleRect(visualBounds))
                val exit = descendants(graph.rootView).first { it.tag == "math_exit_representation" }
                assertTrue(exit.getGlobalVisibleRect(exitBounds))
                assertTrue("Salida fuera del área manipulable", exitBounds.top >= visualBounds.bottom)
                assertTrue("Área táctil accesible", exitBounds.height() >= 48 * graph.resources.displayMetrics.density - 1)
                val viewport = graph.parent as View
                assertEquals("La ampliación conserva la proporción inline", inlineAspectRatio, graph.width.toFloat() / graph.height, .02f)
                assertEquals("Visual centrado horizontalmente", viewport.width / 2f, graph.left + graph.width / 2f, 1.5f)
                assertEquals("Visual centrado verticalmente", viewport.height / 2f, graph.top + graph.height / 2f, 1.5f)
                assertTrue("Visual ocupa todo el ancho o alto disponible", graph.width >= viewport.width - 1 || graph.height >= viewport.height - 2)
                assertEquals(0, viewport.paddingLeft + viewport.paddingRight)
                dragPointInExpandedGraph(graph, MathPoint(-1.0, 4.0))
                assertEquals(MathPoint(-1.0, 4.0), graph.points().first())
            }
            screenshot("sandbox-graph-expanded-150.png")
            pressBack()
            instrumentation.waitForIdleSync()
            scenario.onActivity { activity ->
                assertSame("Se devuelve la misma View a su holder", originalParent, graph.parent)
                assertSame(originalRoot, graph.rootView)
                assertEquals(MathPoint(-1.0, 4.0), graph.points().first())
                val content = activity.findViewById<ViewGroup>(R.id.lessonContent)
                val coordinates = pointCoordinateEditors(content)
                assertEquals(listOf("-1", "4"), coordinates.map { it.text.toString() })
                descendants(content).first { it.tag == "math_expand_representation" }.performClick()
            }
            instrumentation.waitForIdleSync()
            scenario.recreate()
            instrumentation.waitForIdleSync()
            onView(withTagValue(equalTo("math_exit_representation"))).check(doesNotExist())
            scenario.onActivity { activity ->
                assertSame("La destrucción también restaura el parent original", originalParent, graph.parent)
                assertFalse(graph.isAttachedToWindow)
                val recreatedGraph = descendants(activity.findViewById(R.id.lessonContent)).filterIsInstance<MathCartesianView>().first { it.editable }
                assertEquals(MathPoint(-1.0, 4.0), recreatedGraph.points().first())
            }
        }
    }

    @Test fun incompleteCoordinateAndNumberLineDraftsEnableActionsPersistAndReset() {
        ActivityScenario.launch<LessonActivity>(intent()).use { scenario ->
            scenario.onActivity { activity ->
                val content = activity.findViewById<ViewGroup>(R.id.lessonContent)
                val coordinates = pointCoordinateEditors(content)
                val check = checkButton(content)
                val reset = resetButton(content)
                assertFalse(check.isEnabled)
                assertFalse(reset.isEnabled)
                coordinates[0].setText("-")
                assertTrue(check.isEnabled)
                assertTrue(reset.isEnabled)
                reset.performClick()
                assertEquals(listOf("0", "0"), coordinates.map { it.text.toString() })
                assertFalse(check.isEnabled)
                assertFalse(reset.isEnabled)
                coordinates[0].setText("2")
                coordinates[1].setText("3")
                descendants(content).filterIsInstance<EditText>().first { it.contentDescription?.toString() == "Coordenada x" }.setText("2")
                coordinates[0].setText("-")
                check.performClick()
                assertEquals(MathPoint(2.0, 3.0), descendants(content).filterIsInstance<MathCartesianView>().first { it.editable }.points().first())
                assertTrue("El borrador inválido impide validar el último punto correcto", check.isEnabled)
                assertTrue(descendants(content).filterIsInstance<TextView>().any { it.text.contains("valores numéricos válidos") })
                assertEquals(0, ProgressRepository.getLessonPageProgress(activity, "math_physics", lesson).completed)
            }
            scenario.recreate()
            scenario.onActivity { activity ->
                val content = activity.findViewById<ViewGroup>(R.id.lessonContent)
                assertEquals("-", pointCoordinateEditors(content)[0].text.toString())
                assertTrue(checkButton(content).isEnabled)
                assertTrue(resetButton(content).isEnabled)
                resetButton(content).performClick()
                assertEquals(listOf("0", "0"), pointCoordinateEditors(content).map { it.text.toString() })
                assertFalse(checkButton(content).isEnabled)
                assertFalse(resetButton(content).isEnabled)
            }
        }
        ProgressRepository.saveCurrentPage(context, "math_physics", lesson, 9)
        ActivityScenario.launch<LessonActivity>(intent()).use { scenario ->
            scenario.onActivity { activity ->
                val content = activity.findViewById<ViewGroup>(R.id.lessonContent)
                val line = descendants(content).filterIsInstance<MathNumberLineView>().first { it.editable }
                val puzzle = puzzleFor(line)
                val position = descendants(puzzle).filterIsInstance<EditText>().single()
                assertFalse(checkButton(puzzle).isEnabled)
                assertFalse(resetButton(puzzle).isEnabled)
                position.setText("-")
                assertTrue(checkButton(puzzle).isEnabled)
                assertTrue(resetButton(puzzle).isEnabled)
                resetButton(puzzle).performClick()
                assertEquals("0", position.text.toString())
                assertFalse(checkButton(puzzle).isEnabled)
                assertFalse(resetButton(puzzle).isEnabled)
                position.setText("2")
                descendants(puzzle).filterIsInstance<CheckBox>().single().isChecked = true
                descendants(puzzle).filterIsInstance<Spinner>().single().setSelection(0)
            }
            instrumentation.waitForIdleSync()
            scenario.onActivity { activity ->
                val line = descendants(activity.findViewById(R.id.lessonContent)).filterIsInstance<MathNumberLineView>().first { it.editable }
                val puzzle = puzzleFor(line)
                descendants(puzzle).filterIsInstance<EditText>().single().setText("-")
                checkButton(puzzle).performClick()
                assertEquals(MathAnswer.NumberLine(2.0, true, "left"), line.answer())
                assertTrue("El borrador inválido impide validar el último intervalo correcto", checkButton(puzzle).isEnabled)
                assertTrue(descendants(puzzle).filterIsInstance<TextView>().any { it.text.contains("posición numérica válida") })
            }
            scenario.recreate()
            scenario.onActivity { activity ->
                val line = descendants(activity.findViewById(R.id.lessonContent)).filterIsInstance<MathNumberLineView>().first { it.editable }
                val puzzle = puzzleFor(line)
                assertEquals("-", descendants(puzzle).filterIsInstance<EditText>().single().text.toString())
                assertTrue(checkButton(puzzle).isEnabled)
                assertTrue(resetButton(puzzle).isEnabled)
                resetButton(puzzle).performClick()
                assertEquals(MathAnswer.NumberLine(0.0, false, "right"), line.answer())
                assertEquals("0", descendants(puzzle).filterIsInstance<EditText>().single().text.toString())
                assertFalse(checkButton(puzzle).isEnabled)
                assertFalse(resetButton(puzzle).isEnabled)
            }
        }
    }

    @Test fun numberLineDirectionOptionsHaveAccessibleHeightAndFollowTextScale() {
        ProgressRepository.saveCurrentPage(context, "math_physics", lesson, 9)
        ActivityScenario.launch<LessonActivity>(intent()).use { scenario ->
            instrumentation.waitForIdleSync()
            scenario.onActivity { activity ->
                val line = descendants(activity.findViewById(R.id.lessonContent)).filterIsInstance<MathNumberLineView>().first { it.editable }
                val puzzle = puzzleFor(line)
                val direction = descendants(puzzle).filterIsInstance<Spinner>().single()
                assertTrue(direction.height >= 48 * direction.resources.displayMetrics.density - 1)
                val selectedTextSize = (direction.selectedView as TextView).textSize
                val themeSize = (ArrayAdapter<String>(activity, android.R.layout.simple_spinner_item, listOf("Izquierda")).getView(0, null, direction) as TextView).textSize
                assertEquals("Selección creada tarde conserva escala150%", themeSize * 1.5f, selectedTextSize, .5f)
                val adapter = direction.adapter
                for (position in 0..1) {
                    val option = adapter.getDropDownView(position, null, direction) as TextView
                    option.measure(View.MeasureSpec.makeMeasureSpec(direction.width, View.MeasureSpec.EXACTLY), View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED))
                    assertTrue("Opción ${option.text} accesible", option.measuredHeight >= 48 * direction.resources.displayMetrics.density - 1)
                    assertEquals("Escala heredada de la selección", selectedTextSize, option.textSize, .1f)
                }
                val coordinatesLabels = descendants(puzzle).filterIsInstance<TextView>()
                assertFalse(coordinatesLabels.any { it.text.toString().matches(Regex("\\d+\\.-.*")) })
            }
        }
    }

    private fun intent(): Intent = Intent(context, LessonActivity::class.java)
        .putExtra("routeId", "math_physics").putExtra("courseId", "math_course_01")
        .putExtra("unitId", "arithmetic_fundamentals").putExtra("lessonId", lesson).putExtra("lessonNumber", 4)

    private fun puzzleFor(view: View): View {
        var current = view
        while (current.tag != MathPuzzleRenderer.INTERACTION_TAG) current = current.parent as View
        return current
    }

    private fun checkButton(view: View): TextView = descendants(view).filterIsInstance<TextView>().first { it.text.toString() == "Comprobar" }
    private fun resetButton(view: View): View = descendants(view).first { it.contentDescription?.toString() == "Reiniciar" }

    private fun descendants(view: View): List<View> = listOf(view) +
        if (view is ViewGroup) (0 until view.childCount).flatMap { descendants(view.getChildAt(it)) } else emptyList()

    private fun pointCoordinateEditors(view: View): List<EditText> {
        val inputs = descendants(view).filterIsInstance<EditText>()
        return listOf("Coordenada x del punto 1", "Coordenada y del punto 1").map { description ->
            inputs.first { it.contentDescription?.toString() == description }
        }
    }

    private fun dragPointInExpandedGraph(graph: MathCartesianView, target: MathPoint) {
        // Read the measured plotting rectangle only to locate the touch; exercise real touch handling.
        val plotField = MathCartesianView::class.java.getDeclaredField("plot").apply { isAccessible = true }
        val plot = plotField.get(graph) as RectF
        assertTrue(plot.width() > 0 && plot.height() > 0)
        fun x(point: MathPoint) = (plot.left + (point.x + 5) / 10 * plot.width()).toFloat()
        fun y(point: MathPoint) = (plot.bottom - (point.y + 5) / 12 * plot.height()).toFloat()
        val start = graph.points().first()
        val down = SystemClock.uptimeMillis()
        listOf(MotionEvent.ACTION_DOWN to start, MotionEvent.ACTION_MOVE to target, MotionEvent.ACTION_UP to target).forEachIndexed { index, (action, point) ->
            val event = MotionEvent.obtain(down, down + index * 16, action, x(point), y(point), 0)
            try { assertTrue(graph.dispatchTouchEvent(event)) } finally { event.recycle() }
        }
    }

    private fun screenshot(name: String) {
        instrumentation.waitForIdleSync()
        SystemClock.sleep(800)
        val bitmap = instrumentation.uiAutomation.takeScreenshot() ?: error("No se pudo capturar gráfica ampliada")
        if (android.os.Build.VERSION.SDK_INT >= 31) {
            ParcelFileDescriptor.AutoCloseInputStream(instrumentation.uiAutomation.executeShellCommand("mkdir -p /data/local/tmp/math-puzzle-smoke")).use { it.readBytes() }
            val streams = instrumentation.uiAutomation.executeShellCommandRw("dd of=/data/local/tmp/math-puzzle-smoke/$name")
            ParcelFileDescriptor.AutoCloseOutputStream(streams[1]).use { assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) }
            ParcelFileDescriptor.AutoCloseInputStream(streams[0]).use { it.readBytes() }
        }
        bitmap.recycle()
    }
}
