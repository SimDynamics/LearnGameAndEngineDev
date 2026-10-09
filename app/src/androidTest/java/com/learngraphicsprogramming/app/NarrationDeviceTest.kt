package com.learngraphicsprogramming.app

import android.app.KeyguardManager
import android.content.Context
import android.content.Intent
import android.content.pm.ActivityInfo
import android.os.SystemClock
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.view.MotionEvent
import android.widget.TextView
import androidx.test.core.app.ActivityScenario
import androidx.test.espresso.Espresso.onView
import androidx.test.espresso.action.ViewActions.click
import androidx.test.espresso.matcher.ViewMatchers.withText
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.narration.LessonNarrationController
import com.learngraphicsprogramming.app.narration.NarrationEngine
import com.learngraphicsprogramming.app.narration.NarrationSettingsRepository
import com.learngraphicsprogramming.app.narration.NarrationLanguage
import org.json.JSONArray
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class NarrationDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val snapshots = linkedMapOf<String, Map<String, *>>()

    @Before fun preservePreferences() {
        listOf("lesson_narration_settings", "learning_progress", "navigation_state").forEach { name ->
            snapshots[name] = context.getSharedPreferences(name, Context.MODE_PRIVATE).all.toMap()
        }
        assertFalse("Desbloquea el teléfono para las pruebas de interfaz",
            context.getSystemService(KeyguardManager::class.java).isDeviceLocked)
        // An unlocked phone can still have its display off while the APK is installed.
        // Wake it at test start, rather than treating lack of window focus as a UI failure.
        android.os.ParcelFileDescriptor.AutoCloseInputStream(
            instrumentation.uiAutomation.executeShellCommand("input keyevent KEYCODE_WAKEUP")
        ).use { it.readBytes() }
        val keyguard = context.getSystemService(KeyguardManager::class.java)
        if (!keyguard.isKeyguardSecure) {
            android.os.ParcelFileDescriptor.AutoCloseInputStream(
                instrumentation.uiAutomation.executeShellCommand("wm dismiss-keyguard")
            ).use { it.readBytes() }
        }
        NarrationSettingsRepository.saveEnabled(context, true)
        NarrationSettingsRepository.saveRate(context, 1f)
        NarrationSettingsRepository.saveEngine(context, NarrationEngine.ANDROID)
        NarrationSettingsRepository.saveLanguage(context, NarrationLanguage.SPANISH)
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

    @Test fun floatingControlsKeepRateAcrossPagesAndRotationAndRespectDisable() {
        launch().use { scenario ->
            awaitShown(scenario)
            scenario.onActivity { activity ->
                val controls = activity.findViewById<View>(R.id.lessonNarrationControls)
                assertSame("Controles superpuestos, fuera del flujo del contenido",
                    activity.findViewById<View>(android.R.id.content), controls.parent)
                val rate = activity.findViewById<TextView>(R.id.buttonLessonSpeechRate)
                assertRate(activity, 1f)
                listOf(1.5f, 2f, 0.5f, 1f).forEach { expected ->
                    rate.performClick()
                    assertRate(activity, expected)
                }
                rate.performClick()
                assertRate(activity, 1.5f)
                injectPages(activity)
                setField(activity, "currentPage", 1)
                invoke(activity, "renderCurrentPage")
                assertRate(activity, 1.5f)
                activity.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
            }
            SystemClock.sleep(900)
            scenario.onActivity { activity ->
                assertRate(activity, 1.5f)
                NarrationSettingsRepository.saveEnabled(activity, false)
                invoke(activity, "updateNarrationVisibility")
                assertEquals(View.GONE, activity.findViewById<View>(R.id.lessonNarrationControls).visibility)
                NarrationSettingsRepository.saveEnabled(activity, true)
                invoke(activity, "updateNarrationVisibility")
                assertEquals(View.VISIBLE, activity.findViewById<View>(R.id.lessonNarrationControls).visibility)
                activity.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
            }
            // A fresh Activity reads the same persisted rate, rather than a per-page local variable.
            scenario.recreate()
            awaitShown(scenario)
            scenario.onActivity { activity ->
                assertRate(activity, 1.5f)
            }
        }
    }

    private fun assertRate(activity: LessonActivity, expected: Float) {
        assertEquals(expected, NarrationSettingsRepository.get(activity).rate)
        val label = when (expected) {
            0.5f -> R.string.narration_rate_half
            1.5f -> R.string.narration_rate_one_half
            2f -> R.string.narration_rate_double
            else -> R.string.narration_rate_normal
        }
        assertEquals(activity.getString(label),
            activity.findViewById<TextView>(R.id.buttonLessonSpeechRate).text.toString())
    }

    @Test fun realExerciseFullscreenHidesControlsAndRestoresThem() {
        launch().use { scenario ->
            awaitShown(scenario)
            scenario.onActivity { activity ->
                val method = LessonActivity::class.java.declaredMethods.single { it.name == "openExerciseFullscreenHost" }
                method.isAccessible = true
                val callback: (android.widget.FrameLayout, android.widget.LinearLayout, android.widget.FrameLayout) -> Unit = { _, _, _ -> }
                method.invoke(activity, false, callback)
                assertEquals(View.GONE, activity.findViewById<View>(R.id.lessonNarrationControls).visibility)
                invoke(activity, "closeExerciseFullscreenHost")
                assertEquals(View.VISIBLE, activity.findViewById<View>(R.id.lessonNarrationControls).visibility)
            }
        }
    }

    @Test fun realKeyboardHidesControlsAndPageChangeCancelsPlayback() {
        launch().use { scenario ->
            awaitShown(scenario)
            scenario.onActivity { activity ->
                injectPages(activity)
                val input = android.widget.EditText(activity).apply {
                    tag = "cpp_code_input"
                    setText("int valor = 3;")
                }
                activity.findViewById<ViewGroup>(R.id.lessonContent).addView(input)
                input.requestFocus()
                input.post {
                    (activity.getSystemService(Context.INPUT_METHOD_SERVICE) as android.view.inputmethod.InputMethodManager)
                        .showSoftInput(input, android.view.inputmethod.InputMethodManager.SHOW_IMPLICIT)
                }
            }
            var hidden = false
            repeat(30) {
                if (!hidden) {
                    SystemClock.sleep(100)
                    scenario.onActivity { hidden = it.findViewById<View>(R.id.lessonNarrationControls).visibility == View.GONE }
                }
            }
            assertTrue("El IME real oculta los dos controles", hidden)
            scenario.onActivity { activity ->
                (activity.getSystemService(Context.INPUT_METHOD_SERVICE) as android.view.inputmethod.InputMethodManager)
                    .hideSoftInputFromWindow(activity.window.decorView.windowToken, 0)
                activity.currentFocus?.clearFocus()
            }
            awaitShown(scenario)
            scenario.onActivity { activity ->
                val controller = field(activity, "narrationController") as LessonNarrationController
                controller.toggle(listOf("Esta es una explicación educativa que continúa durante varias frases.".repeat(15)))
                assertTrue("Inicio deliberado de narración", controller.isPlaying)
                setField(activity, "currentPage", 1)
                invoke(activity, "renderCurrentPage")
                assertFalse("Cambiar página cancela la narración anterior", controller.isPlaying)
            }
        }
    }

    @Test fun globalOptionsBelowPremiumPersistLanguageInstalledVoiceEngineAndDisable() {
        ActivityScenario.launch<OptionsActivity>(Intent(context, OptionsActivity::class.java)).use { scenario ->
            var kokoroChoice = ""
            var alexChoice = ""
            var frenchChoice = ""
            scenario.onActivity { activity ->
                kokoroChoice = activity.getString(R.string.narration_kokoro_neural)
                alexChoice = activity.getString(R.string.narration_voice_name_id, "Alex", "em_alex")
                frenchChoice = NarrationLanguage.FRENCH.label(activity)
                activity.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                val section = activity.findViewById<ViewGroup>(R.id.narrationSettingsSection)
                val parent = section.parent as ViewGroup
                assertSame("La sección completa está inmediatamente debajo de Premium",
                    activity.findViewById<View>(R.id.buttonPremiumPurchase).parent,
                    parent.getChildAt(parent.indexOfChild(section) - 1))
                val toggle = tree(section).filterIsInstance<android.widget.Switch>().single()
                toggle.isChecked = false
                assertFalse(NarrationSettingsRepository.get(activity).enabled)
                tree(section).filterIsInstance<android.widget.Button>().single {
                    it.text == activity.getString(R.string.narration_engine_android)
                }.performClick()
            }
            onView(withText(kokoroChoice)).inRoot(androidx.test.espresso.matcher.RootMatchers.isDialog()).perform(click())
            // Integrity checks run off the UI thread and include the shared FP32 graph.
            // Wait for the actual verified snapshot before opening the installed-only list.
            val verifiedDeadline = SystemClock.uptimeMillis() + 30_000L
            var voiceVerified = false
            while (!voiceVerified && SystemClock.uptimeMillis() < verifiedDeadline) {
                scenario.onActivity { activity ->
                    val section = activity.findViewById<ViewGroup>(R.id.narrationSettingsSection)
                    val selected = NarrationSettingsRepository.get(activity).kokoroVoice
                    val expected = activity.getString(R.string.narration_selected_kokoro,
                        selected, activity.getString(R.string.narration_verified))
                    voiceVerified = tree(section).filterIsInstance<android.widget.Button>().any { it.text == expected }
                }
                if (!voiceVerified) SystemClock.sleep(100)
            }
            assertTrue("El selector debe recibir los recursos realmente verificados", voiceVerified)
            scenario.onActivity { activity ->
                assertEquals(NarrationEngine.KOKORO, NarrationSettingsRepository.get(activity).engine)
                val section = activity.findViewById<ViewGroup>(R.id.narrationSettingsSection)
                val expected = activity.getString(R.string.narration_selected_kokoro,
                    NarrationSettingsRepository.get(activity).kokoroVoice,
                    activity.getString(R.string.narration_verified))
                tree(section).filterIsInstance<android.widget.Button>().single { it.text == expected }.performClick()
            }
            onView(withText(alexChoice)).inRoot(androidx.test.espresso.matcher.RootMatchers.isDialog()).perform(click())
            scenario.onActivity { activity ->
                val section = activity.findViewById<ViewGroup>(R.id.narrationSettingsSection)
                val expected = activity.getString(R.string.narration_selected_language,
                    NarrationSettingsRepository.get(activity).language.label(activity))
                tree(section).filterIsInstance<android.widget.Button>().single { it.text == expected }.performClick()
            }
            onView(withText(frenchChoice)).inRoot(androidx.test.espresso.matcher.RootMatchers.isDialog()).perform(click())
            scenario.recreate()
            SystemClock.sleep(400)
            scenario.onActivity { activity ->
                val settings = NarrationSettingsRepository.get(activity)
                assertFalse(settings.enabled)
                assertEquals(NarrationEngine.KOKORO, settings.engine)
                assertEquals(NarrationLanguage.FRENCH, settings.language)
                assertEquals("ff_siwis", settings.kokoroVoice)
                assertEquals("em_alex", NarrationSettingsRepository.forLanguage(activity, NarrationLanguage.SPANISH).kokoroVoice)
                val section = activity.findViewById<ViewGroup>(R.id.narrationSettingsSection)
                assertTrue(tree(section).filterIsInstance<TextView>().any { it.text.contains("Charsiu") })
            }
        }
    }

    @Test fun fiveControlsDragTogetherPersistAndStayWithinRotatedViewport() {
        launch().use { scenario ->
            awaitShown(scenario)
            var startX = 0f
            var startY = 0f
            scenario.onActivity { activity ->
                val row = activity.findViewById<ViewGroup>(R.id.lessonNarrationControls)
                assertEquals(5, row.childCount)
                val point = IntArray(2)
                row.getLocationOnScreen(point)
                startX = point[0] + row.width / 2f
                startY = point[1] + row.height / 2f
            }
            val began = SystemClock.uptimeMillis()
            fun inject(action: Int, x: Float, y: Float) {
                val event = MotionEvent.obtain(began, SystemClock.uptimeMillis(), action, x, y, 0).apply {
                    source = android.view.InputDevice.SOURCE_TOUCHSCREEN
                }
                try { assertTrue(instrumentation.uiAutomation.injectInputEvent(event, true)) } finally { event.recycle() }
            }
            inject(MotionEvent.ACTION_DOWN, startX, startY)
            repeat(8) { index ->
                SystemClock.sleep(25)
                inject(MotionEvent.ACTION_MOVE, startX - 15f * (index + 1), startY - 24f * (index + 1))
            }
            inject(MotionEvent.ACTION_UP, startX - 120f, startY - 192f)
            val saved = NarrationSettingsRepository.position(context)
            assertTrue("El arrastre guarda una posición distinta", saved.second < 0.85f)
            scenario.recreate()
            awaitShown(scenario)
            scenario.onActivity { activity -> activity.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE }
            SystemClock.sleep(900)
            scenario.onActivity { activity ->
                val row = activity.findViewById<ViewGroup>(R.id.lessonNarrationControls)
                val root = activity.findViewById<ViewGroup>(android.R.id.content)
                assertEquals(saved, NarrationSettingsRepository.position(context))
                assertTrue(row.x >= 0 && row.y >= 0 && row.x + row.width <= root.width && row.y + row.height <= root.height)
                assertEquals(5, row.childCount)
                activity.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
            }
        }
    }

    private fun tree(view: View): List<View> = buildList {
        add(view)
        if (view is ViewGroup) for (index in 0 until view.childCount) addAll(tree(view.getChildAt(index)))
    }

    private fun launch(): ActivityScenario<LessonActivity> = ActivityScenario.launch<LessonActivity>(
        Intent(context, LessonActivity::class.java)
            .putExtra("routeId", "programming").putExtra("courseId", "cpp_fundamentals")
            .putExtra("unitId", "intro_programming")
            .putExtra("lessonId", "cpp_fundamentals__intro_programming__what_is_programming")
            .putExtra("lessonNumber", 1).putExtra("lessonTotal", 1)
    ).also { scenario ->
        scenario.onActivity { it.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON) }
    }

    private fun awaitShown(scenario: ActivityScenario<LessonActivity>) {
        var shown = false
        repeat(40) {
            if (!shown) {
                SystemClock.sleep(75)
                scenario.onActivity { shown = it.findViewById<View>(R.id.lessonNarrationControls).visibility == View.VISIBLE }
            }
        }
        assertTrue("Los controles deben reaparecer en lectura normal", shown)
    }

    @Suppress("UNCHECKED_CAST")
    private fun injectPages(activity: LessonActivity) {
        val pages = JSONArray()
        repeat(2) { index ->
            pages.put(JSONObject().put("blocks", JSONArray().put(
                JSONObject().put("type", "paragraph").put("text", "Página ${index + 1}. Una fracción representa una parte del total.")
            )))
        }
        setField(activity, "lessonJson", JSONObject().put("title", "Prueba de lectura").put("pages", pages))
        setField(activity, "currentPage", 0)
        setField(activity, "totalPages", 2)
        listOf("pageViewCache", "pageRequiredExercises", "pageCompletedExercises").forEach { name ->
            (field(activity, name) as MutableMap<Int, Any>).clear()
        }
        invoke(activity, "renderCurrentPage")
    }

    private fun field(activity: LessonActivity, name: String): Any? = LessonActivity::class.java.getDeclaredField(name)
        .apply { isAccessible = true }.get(activity)
    private fun setField(activity: LessonActivity, name: String, value: Any) = LessonActivity::class.java.getDeclaredField(name)
        .apply { isAccessible = true }.set(activity, value)
    private fun invoke(activity: LessonActivity, name: String) = LessonActivity::class.java.getDeclaredMethod(name)
        .apply { isAccessible = true }.invoke(activity)
}
