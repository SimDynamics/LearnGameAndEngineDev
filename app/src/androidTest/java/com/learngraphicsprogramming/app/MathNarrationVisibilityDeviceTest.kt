package com.learngraphicsprogramming.app

import android.app.KeyguardManager
import android.content.Context
import android.content.Intent
import android.content.pm.ActivityInfo
import android.content.res.Configuration
import android.graphics.Rect
import android.os.ParcelFileDescriptor
import android.os.SystemClock
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.view.inputmethod.InputMethodManager
import android.widget.PopupWindow
import androidx.activity.ComponentDialog
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.math.MathCartesianView
import com.learngraphicsprogramming.app.math.MathExpressionEditor
import com.learngraphicsprogramming.app.math.MathKeyboardHost
import com.learngraphicsprogramming.app.math.MathKeyboardView
import com.learngraphicsprogramming.app.narration.NarrationSettingsRepository
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/** Uses real authored sandbox pages, native IME, math toolbar and expandable graph dialog. */
@RunWith(AndroidJUnit4::class)
class MathNarrationVisibilityDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val route = "math_physics"
    private val lesson = "math_course_01__arithmetic_fundamentals__math_puzzle_sandbox"
    private val snapshots = linkedMapOf<String, Map<String, *>>()

    @Before fun preservePreferencesAndIsolateSandbox() {
        listOf("lesson_narration_settings", "learning_progress", "navigation_state", "lesson_text_size_settings").forEach { name ->
            snapshots[name] = context.getSharedPreferences(name, Context.MODE_PRIVATE).all.toMap()
        }
        assertFalse("Desbloquea el teléfono para las pruebas del teclado real",
            context.getSystemService(KeyguardManager::class.java).isDeviceLocked)
        val wakeCommands = mutableListOf("input keyevent KEYCODE_WAKEUP")
        if (!context.getSystemService(KeyguardManager::class.java).isKeyguardSecure) wakeCommands.add("wm dismiss-keyguard")
        wakeCommands.forEach { command ->
            ParcelFileDescriptor.AutoCloseInputStream(instrumentation.uiAutomation.executeShellCommand(command)).use { it.readBytes() }
        }
        ProgressRepository.resetLesson(context, route, lesson)
        NarrationSettingsRepository.saveEnabled(context, true)
        LessonTextSizeSettingsRepository.saveUsesGlobalScale(context, "math_course_01", true)
        LessonTextSizeSettingsRepository.saveGlobalScale(context, 100)
    }

    @After fun restorePreferences() {
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

    @Test fun structuredMathImeAndToolbarHideControlsThroughRotationAndRestoreThem() {
        launchPage(16).use { scenario ->
            await(scenario, "La página real del editor está visible") { normalReading(it) }
            lateinit var editor: MathExpressionEditor
            lateinit var host: MathKeyboardHost
            lateinit var mathBar: MathKeyboardView
            var expression = ""
            scenario.onActivity { activity ->
                assertEquals(16, ProgressRepository.getCurrentPage(activity, lesson))
                editor = tree(activity.findViewById(R.id.lessonContent)).filterIsInstance<MathExpressionEditor>().last()
                assertTrue("El editor de la lección debe estar habilitado", editor.isEnabled)
                assertTrue(editor.setLatex("x+1"))
                expression = editor.getLatex()
                host = MathKeyboardHost.install(activity)
                assertTrue("Solicitud real de apertura del teclado", editor.showSystemKeyboard())
            }
            await(scenario, "El IME y la barra matemática real ocultan los controles") { activity ->
                if (!imeVisible(activity) && activity.hasWindowFocus()) editor.showSystemKeyboard()
                val bar = toolbar(host)
                imeVisible(activity) && bar != null && bar.getGlobalVisibleRect(Rect()) &&
                    popup(host)?.isShowing == true && host.isKeyboardVisible && controlsHidden(activity)
            }
            scenario.onActivity { activity ->
                mathBar = requireNotNull(toolbar(host))
                assertTrue(editor.hasFocus())
                assertTrue("Las acciones matemáticas están dibujadas", mathBar.isAttachedToWindow && mathBar.height > 0)
                assertFalse(activity.findViewById<View>(R.id.buttonLessonListen).isShown)
                assertFalse(activity.findViewById<View>(R.id.buttonLessonSpeechRate).isShown)
                activity.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
            }
            await(scenario, "Rotar conserva el editor y los controles ocultos con IME real") { activity ->
                activity.resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE &&
                    imeVisible(activity) && host.isKeyboardVisible && popup(host)?.isShowing == true &&
                    mathBar.getGlobalVisibleRect(Rect()) && controlsHidden(activity)
            }
            scenario.onActivity { activity ->
                assertSame(editor, activity.currentFocus)
                assertEquals(expression, editor.getLatex())
                assertSame(mathBar, toolbar(host))
                (activity.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager)
                    .hideSoftInputFromWindow(editor.windowToken, 0)
                editor.clearFocus()
            }
            await(scenario, "Cerrar el teclado restaura los dos controles") { activity ->
                !imeVisible(activity) && !host.isKeyboardVisible && normalReading(activity)
            }
            scenario.onActivity { activity ->
                assertFalse(popup(host)?.isShowing == true)
                assertTrue(activity.findViewById<View>(R.id.buttonLessonListen).isShown)
                assertTrue(activity.findViewById<View>(R.id.buttonLessonSpeechRate).isShown)
                activity.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
            }
            await(scenario, "Regresar a vertical mantiene los controles visibles") {
                it.resources.configuration.orientation == Configuration.ORIENTATION_PORTRAIT && normalReading(it)
            }
            scenario.recreate()
            await(scenario, "Recrear la Activity restaura el modo de lectura") { normalReading(it) }
            scenario.onActivity { activity ->
                val restored = tree(activity.findViewById(R.id.lessonContent)).filterIsInstance<MathExpressionEditor>().last()
                assertEquals(expression, restored.getLatex())
            }
        }
    }

    @Test fun actualExpandableGraphDialogHidesControlsAndReturnsSameVisual() {
        launchPage(1).use { scenario ->
            await(scenario, "La página con opciones gráficas está visible") { normalReading(it) }
            lateinit var wrapper: ViewGroup
            lateinit var visual: MathCartesianView
            lateinit var dialog: ComponentDialog
            scenario.onActivity { activity ->
                val expand = tree(activity.findViewById(R.id.lessonContent)).first { it.tag == "math_expand_representation" }
                wrapper = expand.parent.parent as ViewGroup
                visual = tree(wrapper).filterIsInstance<MathCartesianView>().single()
                assertTrue(expand.isAttachedToWindow)
                assertTrue("Se abre la representación real desde su botón", expand.performClick())
                dialog = requireNotNull(expanded(wrapper))
                assertTrue(dialog.isShowing)
            }
            await(scenario, "El diálogo de gráfica toma el foco y oculta narración") { activity ->
                dialog.isShowing && dialog.window?.decorView?.hasWindowFocus() == true &&
                    !activity.hasWindowFocus() && controlsHidden(activity)
            }
            scenario.onActivity { activity ->
                assertFalse(activity.findViewById<View>(R.id.buttonLessonListen).isShown)
                assertFalse(activity.findViewById<View>(R.id.buttonLessonSpeechRate).isShown)
                val dialogRoot = requireNotNull(dialog.window).decorView
                assertTrue("El fullscreen contiene la misma gráfica", tree(dialogRoot).any { it === visual })
                assertFalse("La gráfica ya no está en su contenedor de lectura", tree(wrapper).any { it === visual })
                assertTrue(tree(dialogRoot).first { it.tag == "math_exit_representation" }.performClick())
            }
            await(scenario, "Cerrar la representación devuelve la gráfica y los controles") {
                !dialog.isShowing && expanded(wrapper) == null && normalReading(it) &&
                    tree(wrapper).any { view -> view === visual }
            }
            scenario.onActivity { activity ->
                assertTrue(activity.findViewById<View>(R.id.buttonLessonListen).isShown)
                assertTrue(activity.findViewById<View>(R.id.buttonLessonSpeechRate).isShown)
                assertTrue(visual.isAttachedToWindow)
            }
        }
    }

    private fun launchPage(page: Int): ActivityScenario<LessonActivity> {
        ProgressRepository.saveCurrentPage(context, route, lesson, page)
        return ActivityScenario.launch<LessonActivity>(Intent(context, LessonActivity::class.java)
            .putExtra("routeId", route).putExtra("courseId", "math_course_01")
            .putExtra("unitId", "arithmetic_fundamentals").putExtra("lessonId", lesson)
            .putExtra("lessonNumber", 4)).also { scenario ->
            scenario.onActivity { activity ->
                activity.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                activity.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
            }
        }
    }

    private fun normalReading(activity: LessonActivity): Boolean = activity.hasWindowFocus() &&
        activity.findViewById<View>(R.id.lessonNarrationControls).visibility == View.VISIBLE &&
        activity.findViewById<View>(R.id.buttonLessonListen).isShown &&
        activity.findViewById<View>(R.id.buttonLessonSpeechRate).isShown

    private fun controlsHidden(activity: LessonActivity): Boolean =
        activity.findViewById<View>(R.id.lessonNarrationControls).visibility == View.GONE

    private fun imeVisible(activity: LessonActivity): Boolean = ViewCompat.getRootWindowInsets(activity.window.decorView)
        ?.isVisible(WindowInsetsCompat.Type.ime()) == true

    private fun toolbar(host: MathKeyboardHost): MathKeyboardView? = MathKeyboardHost::class.java
        .getDeclaredField("keyboard").apply { isAccessible = true }.get(host) as? MathKeyboardView

    private fun popup(host: MathKeyboardHost): PopupWindow? = MathKeyboardHost::class.java
        .getDeclaredField("popup").apply { isAccessible = true }.get(host) as? PopupWindow

    private fun expanded(wrapper: ViewGroup): ComponentDialog? = wrapper.javaClass
        .getDeclaredField("expanded").apply { isAccessible = true }.get(wrapper) as? ComponentDialog

    private fun tree(view: View): List<View> = listOf(view) + if (view is ViewGroup)
        (0 until view.childCount).flatMap { tree(view.getChildAt(it)) } else emptyList()

    private fun await(scenario: ActivityScenario<LessonActivity>, message: String, condition: (LessonActivity) -> Boolean) {
        val deadline = SystemClock.uptimeMillis() + 6_000
        var satisfied = false
        while (!satisfied && SystemClock.uptimeMillis() < deadline) {
            scenario.onActivity { satisfied = condition(it) }
            if (!satisfied) SystemClock.sleep(75)
        }
        assertTrue(message, satisfied)
    }
}
