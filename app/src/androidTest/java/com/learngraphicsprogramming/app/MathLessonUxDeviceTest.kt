package com.learngraphicsprogramming.app

import android.app.Activity
import android.app.Instrumentation
import android.content.Context
import android.content.Intent
import android.content.pm.ActivityInfo
import android.content.res.Configuration
import android.graphics.Bitmap
import android.graphics.Rect
import android.os.ParcelFileDescriptor
import android.os.SystemClock
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.widget.EditText
import android.widget.SeekBar
import android.widget.TextView
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.math.MathPuzzleRenderer
import com.learngraphicsprogramming.app.math.MathExpressionEditor
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class MathLessonUxDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val lesson = "math_course_01__arithmetic_fundamentals__math_puzzle_sandbox"
    private val course = "math_course_01"
    private val route = "math_physics"
    private val snapshots = linkedMapOf<String, Map<String, *>>()

    @Before fun preserveAndIsolate() {
        listOf("learning_progress", "navigation_state", "lesson_text_size_settings", "app_settings").forEach {
            snapshots[it] = context.getSharedPreferences(it, Context.MODE_PRIVATE).all.toMap()
        }
        context.getSharedPreferences("app_settings", Context.MODE_PRIVATE).edit()
            .putBoolean("swipeNavigationEnabled", true).commit()
        ProgressRepository.resetLesson(context, route, lesson)
        for (command in listOf("input keyevent KEYCODE_WAKEUP", "wm dismiss-keyguard")) {
            ParcelFileDescriptor.AutoCloseInputStream(instrumentation.uiAutomation.executeShellCommand(command)).use { it.readBytes() }
        }
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

    @Test fun checkAndResetTrackRealChangesAndRestoredPartialAnswers() {
        ActivityScenario.launch<LessonActivity>(intent()).use { scenario ->
            scenario.onActivity { activity ->
                val content = activity.findViewById<ViewGroup>(R.id.lessonContent)
                val check = named(content, "Comprobar")
                val reset = named(content, "Reiniciar")
                assertFalse(check.isEnabled)
                assertFalse(reset.isEnabled)
                val input = tree(content).filterIsInstance<MathExpressionEditor>().first()
                assertTrue(input.setLatex("1"))
                assertTrue(check.isEnabled)
                assertTrue(reset.isEnabled)
                assertTrue(input.setLatex(""))
                assertFalse(check.isEnabled)
                assertFalse(reset.isEnabled)
                assertTrue(input.setLatex("7.5"))
            }
            scenario.recreate()
            scenario.onActivity { activity ->
                val content = activity.findViewById<ViewGroup>(R.id.lessonContent)
                val input = tree(content).filterIsInstance<MathExpressionEditor>().first()
                assertEquals("The actual structured partial answer survives recreation", "7.5", input.getLatex())
                assertTrue(named(content, "Comprobar").isEnabled)
                assertTrue(named(content, "Reiniciar").isEnabled)
                named(content, "Reiniciar").performClick()
                assertEquals("Reset clears the actual structured answer", "", input.getLatex())
                assertFalse(named(content, "Comprobar").isEnabled)
                assertFalse(named(content, "Reiniciar").isEnabled)
            }
        }
    }

    @Test fun horizontalSwipeInsideOrdinaryPuzzleTextNavigatesButInputsOwnGestures() {
        ProgressRepository.saveCurrentPage(context, route, lesson, 1)
        ActivityScenario.launch<LessonActivity>(intent()).use { scenario ->
            instrumentation.waitForIdleSync()
            var bounds = Rect()
            scenario.onActivity { activity ->
                val card = tree(activity.findViewById<View>(R.id.lessonContent)).first { it.tag == MathPuzzleRenderer.INTERACTION_TAG }
                val prompt = tree(card).filterIsInstance<TextView>().first { it.text.isNotBlank() && !it.isClickable }
                assertTrue(prompt.getGlobalVisibleRect(bounds))
                assertFalse(LessonGesturePolicy.isOwnedByControl(activity.window.decorView, bounds.centerX().toFloat(), bounds.centerY().toFloat()))
            }
            swipe(bounds, toRight = true)
            instrumentation.waitForIdleSync()
            SystemClock.sleep(300)
            scenario.onActivity { activity ->
                assertEquals("1 de 18", activity.findViewById<TextView>(R.id.textProgress).text.toString())
                val input = tree(activity.findViewById<View>(R.id.lessonContent)).filterIsInstance<EditText>().first()
                input.requestRectangleOnScreen(Rect(0, 0, input.width, input.height), true)
                assertTrue("El input debe quedar visible tras desplazar la página", input.getGlobalVisibleRect(bounds))
                assertFalse("Un input inactivo permite swipe", LessonGesturePolicy.isOwnedByControl(activity.window.decorView, bounds.centerX().toFloat(), bounds.centerY().toFloat()))
                input.requestFocus()
                assertTrue("La edición activa conserva gesto", LessonGesturePolicy.isOwnedByControl(activity.window.decorView, bounds.centerX().toFloat(), bounds.centerY().toFloat()))
            }
            swipe(bounds, toRight = false)
            instrumentation.waitForIdleSync()
            scenario.onActivity { activity ->
                assertEquals("1 de 18", activity.findViewById<TextView>(R.id.textProgress).text.toString())
            }
        }
    }

    @Test fun interactiveGraphAndSliderSurfacesRetainGestureOwnership() {
        for (page in listOf(10, 14)) {
            ProgressRepository.saveCurrentPage(context, route, lesson, page)
            ActivityScenario.launch<LessonActivity>(intent()).use { scenario ->
                instrumentation.waitForIdleSync()
                scenario.onActivity { activity ->
                    val surface = tree(activity.findViewById<View>(R.id.lessonContent)).first {
                        (it.tag == LessonGesturePolicy.INTERACTIVE_TAG || it is SeekBar) && it !is EditText
                    }
                    val bounds = Rect()
                    surface.requestRectangleOnScreen(Rect(0, 0, surface.width, surface.height), true)
                    assertTrue("Superficie visible en página ${page + 1}", surface.getGlobalVisibleRect(bounds))
                    assertTrue(LessonGesturePolicy.isOwnedByControl(activity.window.decorView, bounds.centerX().toFloat(), bounds.centerY().toFloat()))
                }
            }
        }
    }

    @Test fun completedRichChoicesAndDisabledResetAllowPageSwipe() {
        ProgressRepository.saveCurrentPage(context, route, lesson, 2)
        ActivityScenario.launch<LessonActivity>(intent()).use { scenario ->
            instrumentation.waitForIdleSync()
            val bounds = Rect()
            scenario.onActivity { activity ->
                val content = activity.findViewById<View>(R.id.lessonContent)
                listOf("2/4", "0,5", "Proporción: 3 de 6").forEach { named(content, it).performClick() }
                named(content, "Comprobar").performClick()
                assertTrue(tree(content).filterIsInstance<TextView>().any { it.text == "Correcto ✓" })
                val indicator = named(content, "2/4")
                val option = (indicator.parent as View).parent as View
                assertFalse(option.isEnabled)
                option.requestRectangleOnScreen(Rect(0, 0, option.width, option.height), true)
                assertTrue(option.getGlobalVisibleRect(bounds))
                assertTrue("Se desliza la tarjeta completa", bounds.width() > 128 * activity.resources.displayMetrics.density)
                assertFalse(LessonGesturePolicy.isOwnedByControl(activity.window.decorView, bounds.centerX().toFloat(), bounds.centerY().toFloat()))
            }
            swipe(bounds, toRight = true)
            instrumentation.waitForIdleSync()
            scenario.onActivity { activity ->
                assertEquals("2 de 18", activity.findViewById<TextView>(R.id.textProgress).text.toString())
                val reset = named(activity.findViewById<View>(R.id.lessonContent), "Reiniciar")
                assertFalse(reset.isEnabled)
                assertTrue(reset.getGlobalVisibleRect(bounds))
                assertFalse(LessonGesturePolicy.isOwnedByControl(activity.window.decorView, bounds.centerX().toFloat(), bounds.centerY().toFloat()))
            }
        }
    }

    @Test fun lessonOptionsHideProgrammingSectionsOnlyForMath() {
        for (testRoute in listOf("math_physics", "programming")) {
            val optionsIntent = Intent(context, LessonOptionsActivity::class.java)
                .putExtra("routeId", testRoute).putExtra("courseId", course)
                .putExtra("lessonId", lesson).putExtra("lessonNumber", 4)
            ActivityScenario.launch<LessonOptionsActivity>(optionsIntent).use { scenario ->
                scenario.onActivity { activity ->
                    val expected = if (testRoute == "programming") View.VISIBLE else View.GONE
                    listOf(R.id.textCodeWrapHeading, R.id.codeWrapSettings, R.id.textProgrammingHeading,
                        R.id.buttonEditProgrammingShortcuts, R.id.textProgrammingShortcutsDescription).forEach {
                        assertEquals(expected, activity.findViewById<View>(it).visibility)
                    }
                }
            }
        }
    }

    @Test fun completingLastMathLessonReturnsToItsContainingUnitWithRouteContext() {
        ProgressRepository.saveCurrentPage(context, route, lesson, 17)
        val monitor = instrumentation.addMonitor(UnitActivity::class.java.name, null, false)
        var destination: Activity? = null
        try {
            ActivityScenario.launch<LessonActivity>(intent()).use { scenario ->
                scenario.onActivity { activity ->
                    val content = activity.findViewById<ViewGroup>(R.id.lessonContent)
                    val asset = "content/es/math_physics/math_course_01/arithmetic_fundamentals/lesson_math_puzzle_sandbox.json"
                    val blocks = JSONObject(activity.assets.open(asset).bufferedReader().use { it.readText() })
                        .getJSONArray("pages").getJSONObject(17).getJSONArray("blocks")
                    val response = (0 until blocks.length()).map { blocks.getJSONObject(it) }
                        .single { it.optString("type") == "math_puzzle" }.getJSONArray("responses").getJSONObject(0)
                    val fields = response.getJSONArray("fields")
                    val validators = response.getJSONObject("validator").getJSONObject("fields")
                    val editors = tree(content).filterIsInstance<MathExpressionEditor>()
                    assertEquals(fields.length(), editors.size)
                    editors.forEachIndexed { index, editor ->
                        assertTrue(editor.setLatex(validators.getJSONObject(fields.getJSONObject(index).getString("id")).getString("expected")))
                    }
                    tree(content).filterIsInstance<TextView>().filter { it.text == "Comprobar" }.forEach { it.performClick() }
                    assertEquals(1, tree(content).filterIsInstance<TextView>().count { it.text == "Correcto ✓" })
                    activity.findViewById<View>(R.id.buttonContinue).performClick()
                }
                destination = instrumentation.waitForMonitorWithTimeout(monitor, 5000)
                assertNotNull("La finalización debe abrir UnitActivity", destination)
                val result = destination!!.intent
                assertEquals(route, result.getStringExtra("routeId"))
                assertEquals(course, result.getStringExtra("courseId"))
                assertEquals("math_course_01__arithmetic_fundamentals", result.getStringExtra("unitId"))
                assertEquals(NavigationStateRepository.SCREEN_UNIT, NavigationStateRepository.getState(context).screen)
            }
        } finally {
            destination?.let { instrumentation.runOnMainSync { it.finish() } }
            instrumentation.removeMonitor(monitor)
        }
    }

    @Test fun portraitCompositionAtLargeTextSizeProducesFocusedQaCaptures() {
        LessonTextSizeSettingsRepository.saveUsesGlobalScale(context, course, true)
        LessonTextSizeSettingsRepository.saveGlobalScale(context, 150)
        for (page in listOf(2, 5, 8)) {
            ProgressRepository.saveCurrentPage(context, route, lesson, page)
            ActivityScenario.launch<LessonActivity>(intent()).use { scenario ->
                var originalOrientation = ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
                scenario.onActivity { activity ->
                    originalOrientation = activity.requestedOrientation
                    activity.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
                }
                SystemClock.sleep(800)
                instrumentation.waitForIdleSync()
                try {
                    scenario.onActivity { activity ->
                        assertEquals(Configuration.ORIENTATION_PORTRAIT, activity.resources.configuration.orientation)
                        val content = activity.findViewById<ViewGroup>(R.id.lessonContent)
                        val card = tree(content).first { it.tag == MathPuzzleRenderer.INTERACTION_TAG }
                        assertTrue("Tarjeta aprovecha ancho útil", card.width >= content.width * .9f)
                        named(content, "Pantalla completa").performClick()
                        if (page == 5) {
                            tree(card).filterIsInstance<android.widget.Spinner>().single().setSelection(1)
                            tree(card).filterIsInstance<EditText>().single().setText("1")
                        }
                        if (page == 8) {
                            tree(card).first { it.contentDescription == "Seleccionar ficha 2 + 3" }.performClick()
                            tree(card).first { it.contentDescription == "Destino 5" }.performClick()
                        }
                    }
                    instrumentation.waitForIdleSync()
                    screenshot("sandbox-page-${page + 1}-portrait-150.png")
                } finally {
                    scenario.onActivity { activity ->
                        named(activity.window.decorView, "Salir de pantalla completa").performClick()
                        activity.requestedOrientation = originalOrientation
                    }
                    SystemClock.sleep(400)
                }
            }
        }
    }

    private fun screenshot(name: String) {
        SystemClock.sleep(800)
        val bitmap = instrumentation.uiAutomation.takeScreenshot() ?: error("No se pudo capturar QA")
        if (android.os.Build.VERSION.SDK_INT >= 31) {
            ParcelFileDescriptor.AutoCloseInputStream(instrumentation.uiAutomation.executeShellCommand(
                "mkdir -p /data/local/tmp/math-puzzle-smoke"
            )).use { it.readBytes() }
            val streams = instrumentation.uiAutomation.executeShellCommandRw("dd of=/data/local/tmp/math-puzzle-smoke/$name")
            ParcelFileDescriptor.AutoCloseOutputStream(streams[1]).use { assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) }
            ParcelFileDescriptor.AutoCloseInputStream(streams[0]).use { it.readBytes() }
        }
        bitmap.recycle()
    }

    private fun swipe(bounds: Rect, toRight: Boolean) {
        // Keep the gesture inside the content; the screen edge belongs to Android Back.
        val start = bounds.left + bounds.width() * if (toRight) .25f else .75f
        val end = bounds.left + bounds.width() * if (toRight) .75f else .25f
        val y = bounds.centerY().toFloat()
        val downTime = SystemClock.uptimeMillis()
        fun inject(event: MotionEvent) {
            event.source = android.view.InputDevice.SOURCE_TOUCHSCREEN
            try { assertTrue(instrumentation.uiAutomation.injectInputEvent(event, true)) }
            finally { event.recycle() }
        }
        inject(MotionEvent.obtain(downTime, downTime, MotionEvent.ACTION_DOWN, start, y, 0))
        for (step in 1..6) {
            SystemClock.sleep(20)
            val x = start + (end - start) * step / 6
            inject(MotionEvent.obtain(downTime, SystemClock.uptimeMillis(), MotionEvent.ACTION_MOVE, x, y, 0))
        }
        inject(MotionEvent.obtain(downTime, SystemClock.uptimeMillis(), MotionEvent.ACTION_UP, end, y, 0))
    }

    private fun tree(view: View): List<View> = listOf(view) + if (view is ViewGroup)
        (0 until view.childCount).flatMap { tree(view.getChildAt(it)) } else emptyList()
    private fun named(view: View, name: String): View = tree(view).first {
        it.contentDescription?.toString() == name || it is TextView && it.text.toString() == name
    }
    private fun intent() = Intent(context, LessonActivity::class.java).putExtra("routeId", route)
        .putExtra("courseId", course).putExtra("lessonId", lesson).putExtra("lessonNumber", 4)
}
