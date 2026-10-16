package com.learngraphicsprogramming.app

import android.app.Activity
import android.app.Application
import android.app.KeyguardManager
import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.Rect
import android.os.Build
import android.os.Bundle
import android.os.SystemClock
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.view.accessibility.AccessibilityNodeInfo
import android.view.inputmethod.InputMethodManager
import android.widget.EditText
import android.widget.Spinner
import android.widget.ScrollView
import android.widget.TextView
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.test.core.app.ActivityScenario
import androidx.test.espresso.Espresso.onView
import androidx.test.espresso.Espresso.pressBack
import androidx.test.espresso.action.ViewActions.click
import androidx.test.espresso.assertion.ViewAssertions.matches
import androidx.test.espresso.matcher.RootMatchers.isDialog
import androidx.test.espresso.matcher.RootMatchers.isPlatformPopup
import androidx.test.espresso.matcher.ViewMatchers.isDisplayed
import androidx.test.espresso.matcher.ViewMatchers.isDisplayingAtLeast
import androidx.test.espresso.matcher.ViewMatchers.isEnabled
import androidx.test.espresso.matcher.ViewMatchers.withId
import androidx.test.espresso.matcher.ViewMatchers.withText
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.CppLabPresentationPolicy.Channel
import com.learngraphicsprogramming.app.narration.KokoroContinuousNarrationService
import com.learngraphicsprogramming.app.narration.KokoroPagePreparationQueue
import com.learngraphicsprogramming.app.narration.NarrationAudioPlanner
import com.learngraphicsprogramming.app.narration.NarrationSettingsRepository
import com.learngraphicsprogramming.app.runtime.CppCompiler
import com.learngraphicsprogramming.app.runtime.CppCompilerVariant
import java.io.File
import org.hamcrest.Matchers.not
import org.json.JSONObject
import org.json.JSONArray
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/** Real UI/native compiler checks restricted to LDPlayer's isolated APK; no lesson file is written. */
@RunWith(AndroidJUnit4::class)
class CppCompactConsolesDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val application = context.applicationContext as Application
    private val preferences = linkedMapOf<String, Map<String, *>>()
    private val narrationFiles = linkedMapOf<String, ByteArray?>()
    private var preferenceNames = emptySet<String>()
    private var captured = false
    private var appLocale = ""
    private val screenGuard = object : Application.ActivityLifecycleCallbacks {
        override fun onActivityCreated(activity: Activity, state: Bundle?) {
            if (activity.packageName == QA_PACKAGE) activity.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        }
        override fun onActivityStarted(activity: Activity) = Unit
        override fun onActivityResumed(activity: Activity) = Unit
        override fun onActivityPaused(activity: Activity) = Unit
        override fun onActivityStopped(activity: Activity) = Unit
        override fun onActivitySaveInstanceState(activity: Activity, state: Bundle) = Unit
        override fun onActivityDestroyed(activity: Activity) = Unit
    }

    @Before fun preserveIsolatedQaState() {
        assertEquals("Never exercise the ordinary application", QA_PACKAGE, context.packageName)
        assertEquals(QA_PACKAGE, BuildConfig.APPLICATION_ID)
        assertTrue("These acceptance checks use LDPlayer exclusively", Build.SUPPORTED_ABIS.any { it == "x86" || it == "x86_64" })
        assertTrue("This QA verifies the production Clang 23 default", BuildConfig.LGP_CLANG23_ENABLED)
        val lock = context.getSystemService(KeyguardManager::class.java)
        assertFalse("Unlock LDPlayer before UI acceptance checks", lock.isDeviceLocked || lock.isKeyguardLocked)
        preferenceNames = preferenceFiles()
        (preferenceNames + KNOWN_PREFERENCES).forEach { name ->
            preferences[name] = context.getSharedPreferences(name, Context.MODE_PRIVATE).all.mapValues { (_, value) ->
                if (value is Set<*>) value.toSet() else value
            }
        }
        appLocale = UiLanguageSettings.selectedTag(context)
        listOf(KokoroPagePreparationQueue.FILE_NAME, NarrationAudioPlanner.FILE_NAME).forEach { name ->
            narrationFiles[name] = File(context.filesDir, name).takeIf(File::isFile)?.readBytes()
        }
        captured = true
        instrumentation.runOnMainSync {
            NarrationSettingsRepository.saveEnabled(context, false)
            UiLanguageSettings.select(context, "es")
        }
        context.stopService(Intent(context, KokoroContinuousNarrationService::class.java))
        await("Existing QA background generation releases before UI fixtures", 30_000) {
            Thread.getAllStackTraces().keys.none { it.isAlive && it.name == "KokoroOfflineNarration" }
        }
        application.registerActivityLifecycleCallbacks(screenGuard)
    }

    @After fun restoreEveryQaPreferenceAndNarrationPosition() {
        if (!captured) return
        instrumentation.waitForIdleSync()
        application.unregisterActivityLifecycleCallbacks(screenGuard)
        instrumentation.runOnMainSync { UiLanguageSettings.select(context, appLocale) }
        (preferenceFiles() + preferences.keys).forEach { name ->
            if (name !in preferenceNames) assertTrue(context.deleteSharedPreferences(name))
            else {
                val target = context.getSharedPreferences(name, Context.MODE_PRIVATE)
                val edit = target.edit().clear()
                preferences.getValue(name).forEach { (key, value) ->
                    when (value) {
                        is String -> edit.putString(key, value)
                        is Int -> edit.putInt(key, value)
                        is Long -> edit.putLong(key, value)
                        is Float -> edit.putFloat(key, value)
                        is Boolean -> edit.putBoolean(key, value)
                        is Set<*> -> edit.putStringSet(key, value.filterIsInstance<String>().toSet())
                        else -> error("Unsupported preference $name/$key")
                    }
                }
                assertTrue(edit.commit())
                assertEquals(preferences[name], target.all)
            }
        }
        narrationFiles.forEach { (name, bytes) ->
            val file = File(context.filesDir, name)
            if (bytes == null) file.delete() else file.writeBytes(bytes)
        }
        synchronized(KokoroPagePreparationQueue) {
            for (name in listOf("directory", "queue")) KokoroPagePreparationQueue::class.java.getDeclaredField(name)
                .apply { isAccessible = true }.set(KokoroPagePreparationQueue, null)
        }
        synchronized(NarrationAudioPlanner) {
            @Suppress("UNCHECKED_CAST")
            val stores = NarrationAudioPlanner::class.java.getDeclaredField("stores").apply { isAccessible = true }
                .get(NarrationAudioPlanner) as MutableMap<File, Any>
            stores.remove(File(context.filesDir, NarrationAudioPlanner.FILE_NAME).canonicalFile)
        }
        assertEquals(preferenceNames, preferenceFiles())
    }

    @Test fun standaloneCompilerChoiceLivesOnlyInItsToolbarAndPreservesTheEditorAcrossRecreationAndFullscreen() {
        val source = "#include <iostream>\nint main() {\n  std::cout << \"standalone\";\n}\n"
        ActivityScenario.launch(CppPrototypeActivity::class.java).use { scenario ->
            awaitActivity(scenario, "Standalone editor completes native layout") { activity ->
                activity.hasWindowFocus() && activity.findViewById<EditText>(R.id.sourceCode).layout != null
            }
            scenario.onActivity { activity ->
                val input = activity.findViewById<EditText>(R.id.sourceCode)
                input.showSoftInputOnFocus = false
                input.setText(source)
                input.setSelection(2, 5)
                val button = activity.findViewById<View>(R.id.clangCompilerMenuButton)
                assertTrue(button.isShown)
                assertTrue("Only the standalone code header owns the compiler action",
                    descendants(activity.findViewById(R.id.codeHeader)).contains(button))
                assertFalse(activity.findViewById<View>(R.id.clangCompilerSelectorSection)?.isShown == true)
                assertFalse(activity.findViewById<View>(R.id.clang21FeatureSection)?.isShown == true)
                assertEquals(CppCompilerVariant.DEFAULT, selectedCompiler(activity))
                assertStandaloneOutput(activity)
                button.performClick()
            }
            onView(withId(R.id.clang23RadioButton)).inRoot(isDialog()).check(matches(isDisplayed()))
            onView(withId(R.id.clang21RadioButton)).inRoot(isDialog()).check(matches(isDisplayed()))
            val optional = CppCompiler.isOptionalClang21Available(context)
            if (optional) onView(withId(R.id.clang21RadioButton)).inRoot(isDialog()).perform(click())
            else onView(withId(R.id.clang21RadioButton)).inRoot(isDialog()).check(matches(not(isEnabled())))
            screenshot("standalone-explicit-compiler-dialog")
            pressBack()
            scenario.onActivity { activity ->
                assertEquals(if (optional) CppCompilerVariant.OPTIONAL_CLANG_21 else CppCompilerVariant.DEFAULT,
                    selectedCompiler(activity))
                assertEquals(source, activity.findViewById<EditText>(R.id.sourceCode).text.toString())
            }
            scenario.recreate()
            awaitActivity(scenario, "Recreation preserves the source and explicit compiler") { activity ->
                activity.findViewById<EditText>(R.id.sourceCode).text.toString() == source &&
                    selectedCompiler(activity) == if (optional) CppCompilerVariant.OPTIONAL_CLANG_21 else CppCompilerVariant.DEFAULT
            }
            scenario.onActivity { activity ->
                val input = activity.findViewById<EditText>(R.id.sourceCode)
                assertEquals(2, input.selectionStart)
                assertEquals(5, input.selectionEnd)
                activity.findViewById<View>(R.id.fullscreenCodeButton).performClick()
            }
            awaitActivity(scenario, "Fullscreen reclaims the original viewport") { activity ->
                val card = activity.findViewById<View>(R.id.codeCard)
                val viewport = activity.findViewById<View>(R.id.compilerScroll)
                card.height > 0 && kotlin.math.abs(card.height - viewport.height) <= 2
            }
            scenario.onActivity { activity ->
                assertTrue(activity.findViewById<View>(R.id.clangCompilerMenuButton).isShown)
                assertFalse(activity.findViewById<View>(R.id.outputText).isShown)
                assertEquals(source, activity.findViewById<EditText>(R.id.sourceCode).text.toString())
                activity.onBackPressedDispatcher.onBackPressed()
            }
            awaitActivity(scenario, "Leaving fullscreen restores the single console") { it.findViewById<View>(R.id.outputText).isShown }
            scenario.onActivity { activity ->
                assertFalse(activity.isFinishing)
                assertStandaloneOutput(activity)
                assertFalse(activity.findViewById<View>(R.id.clangCompilerSelectorSection)?.isShown == true)
                assertEquals(source, activity.findViewById<EditText>(R.id.sourceCode).text.toString())
            }
            runStandalone(scenario)
            if (optional) {
                scenario.onActivity { it.findViewById<View>(R.id.clangCompilerMenuButton).performClick() }
                onView(withId(R.id.clang23RadioButton)).inRoot(isDialog()).perform(click())
                pressBack()
                scenario.onActivity { assertEquals(CppCompilerVariant.DEFAULT, selectedCompiler(it)) }
                runStandalone(scenario)
            }
        }
    }

    @Test fun realLessonUsesOneConsoleForStdoutDiagnosticsAndRuntimeErrorsWithoutChangingDraftsOrAnswers() {
        val lesson = LessonRepository.loadLessonJson(context, ROUTE, READING)
        val authored = lesson.toString()
        val pages = lesson.getJSONArray("pages")
        val locations = buildList {
            for (page in 0 until pages.length()) {
                val blocks = pages.getJSONObject(page).getJSONArray("blocks")
                for (block in 0 until blocks.length()) {
                    val item = blocks.getJSONObject(block)
                    if (item.optString("type") == "cpp_compiler" && item.optString("initialStandardInput") == "Rita 4") add(page)
                }
            }
        }
        assertEquals(1, locations.size)
        context.getSharedPreferences("cpp_lesson_drafts", Context.MODE_PRIVATE).edit().clear().commit()
        ProgressRepository.resetLesson(context, ROUTE, READING)
        ProgressRepository.saveCurrentPage(context, ROUTE, READING, locations.single())
        val parts = READING.split("__")
        val intent = Intent(context, LessonActivity::class.java).putExtra("routeId", ROUTE)
            .putExtra("courseId", parts[0]).putExtra("unitId", parts[1]).putExtra("lessonId", READING)
            .putExtra("lessonNumber", 1).putExtra("lessonTotal", 1)
        ActivityScenario.launch<LessonActivity>(intent).use { scenario ->
            awaitActivity(scenario, "The published prepared-input lab is stable") { activity ->
                activity.hasWindowFocus() && descendants(activity.window.decorView).any { it.tag == "cpp_code_input" }
            }
            var completed = 0
            var draft = ""
            scenario.onActivity { activity ->
                completed = field(activity, "completedExercises") as Int
                assertCompactConsole(activity)
                assertTrue(descendants(activity.window.decorView).none { it.id == R.id.clangCompilerMenuButton && it.isShown })
                assertEquals(View.GONE, tagged(activity, "cpp_compiler_variant").visibility)
                val selector = tagged(activity, "cpp_compiler_variant") as Spinner
                assertEquals(0, selector.selectedItemPosition)
                assertEquals("Rita 4", (tagged(activity, "cpp_input") as EditText).text.toString())
            }
            runLab(scenario)
            scenario.onActivity { activity ->
                assertEquals("Rita: 7", console(activity).stdout.text.toString().trimEnd())
                assertEquals(console(activity).stdout.text.toString(), console(activity).display.text.toString())
                assertEquals(View.GONE, tagged(activity, "cpp_console_channels").visibility)
                val editor = tagged(activity, "cpp_code_input") as EditText
                draft = editor.text.toString() + "\n// compact console draft\n"
                editor.setText(draft)
                (tagged(activity, "cpp_input") as EditText).setText("Leo 2")
            }
            screenshot("curricular-single-stdout")
            scenario.recreate()
            awaitActivity(scenario, "Draft and prepared input survive recreation") { activity ->
                (tagged(activity, "cpp_code_input") as EditText).text.toString() == draft &&
                    (tagged(activity, "cpp_input") as EditText).text.toString() == "Leo 2"
            }
            runLab(scenario)
            scenario.onActivity { activity ->
                assertEquals("Leo: 5", console(activity).stdout.text.toString().trimEnd())
                (tagged(activity, "cpp_code_input") as EditText).setText("int main() { return missing_compact_console_identifier; }")
            }
            runLab(scenario)
            scenario.onActivity { activity ->
                val console = console(activity)
                assertTrue(console.diagnostics.text.toString().contains("missing_compact_console_identifier"))
                assertEquals(console.diagnostics.text.toString(), console.display.text.toString())
                assertEquals("", console.stdout.text.toString())
                assertEquals("", console.stderr.text.toString())
                assertEquals(View.GONE, tagged(activity, "cpp_console_channels").visibility)
                (tagged(activity, "cpp_code_input") as EditText).setText(
                    "#include <iostream>\nint main() { std::cout << \"kept out\"; std::cerr << \"kept err\"; return 3; }\n")
            }
            runLab(scenario)
            scenario.onActivity { activity ->
                val console = console(activity)
                assertEquals("kept out", console.stdout.text.toString())
                assertEquals("kept err", console.stderr.text.toString())
                assertEquals("", console.diagnostics.text.toString())
                assertEquals("A failing runtime emphasizes its actual stderr", "kept err", console.display.text.toString())
                assertEquals(View.VISIBLE, tagged(activity, "cpp_console_channels").visibility)
                assertCompactConsole(activity)
            }
            scenario.onActivity { activity ->
                (activity.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager)
                    .hideSoftInputFromWindow(activity.window.decorView.windowToken, 0)
                activity.currentFocus?.clearFocus()
            }
            awaitActivity(scenario, "The native IME is closed before testing the console selector") { activity ->
                ViewCompat.getRootWindowInsets(activity.window.decorView)?.isVisible(WindowInsetsCompat.Type.ime()) != true
            }
            scenario.onActivity { activity ->
                val selector = tagged(activity, "cpp_console_channels")
                val scroll = activity.findViewById<ScrollView>(R.id.lessonScroll)
                val bounds = Rect()
                selector.getDrawingRect(bounds)
                scroll.offsetDescendantRectToMyCoords(selector, bounds)
                val margin = (12 * activity.resources.displayMetrics.density).toInt()
                scroll.scrollTo(0, (bounds.bottom - scroll.height + margin).coerceAtLeast(0))
            }
            awaitActivity(scenario, "The selector remains fully visible near the lower viewport edge") { activity ->
                val selector = tagged(activity, "cpp_console_channels")
                val visible = Rect()
                selector.getGlobalVisibleRect(visible) && visible.width() == selector.width && visible.height() == selector.height
            }
            screenshot("curricular-channel-selector-lower-edge-before-popup")
            scenario.onActivity { tagged(it, "cpp_console_channels").performClick() }
            instrumentation.waitForIdleSync()
            screenshot("curricular-channel-selector-lower-edge-popup-open")
            popupGeometry(scenario, "lower-viewport-edge")
            onView(withText(R.string.cpp_program_stdout)).inRoot(isPlatformPopup()).check(matches(isDisplayingAtLeast(90)))
            pressBack()
            scenario.onActivity { activity ->
                val selector = tagged(activity, "cpp_console_channels")
                val scroll = activity.findViewById<ScrollView>(R.id.lessonScroll)
                val bounds = Rect()
                selector.getDrawingRect(bounds)
                scroll.offsetDescendantRectToMyCoords(selector, bounds)
                scroll.scrollTo(0, (bounds.centerY() - scroll.height / 2).coerceAtLeast(0))
            }
            awaitActivity(scenario, "The full real selector is visible before its popup opens") { activity ->
                val selector = tagged(activity, "cpp_console_channels")
                val visible = Rect()
                selector.getGlobalVisibleRect(visible) && visible.width() == selector.width && visible.height() == selector.height
            }
            instrumentation.waitForIdleSync()
            screenshot("curricular-channel-selector-before-popup")
            scenario.onActivity { tagged(it, "cpp_console_channels").performClick() }
            instrumentation.waitForIdleSync()
            screenshot("curricular-channel-selector-popup-open")
            popupGeometry(scenario, "centered-selector")
            onView(withText(R.string.cpp_program_stdout)).inRoot(isPlatformPopup()).check(matches(isDisplayed()))
            onView(withText(R.string.cpp_program_stdout)).inRoot(isPlatformPopup()).perform(click())
            scenario.onActivity { activity ->
                val console = console(activity)
                assertEquals("kept out", console.display.text.toString())
                console.selectChannel(Channel.STDERR)
                assertEquals("kept err", console.display.text.toString())
                assertEquals("kept out", console.stdout.text.toString())
                assertEquals("kept err", console.stderr.text.toString())
                assertEquals("A lab still cannot complete any evaluated exercise", completed, field(activity, "completedExercises"))
            }
            screenshot("curricular-runtime-channel-selector")
        }
        assertEquals("UI acceptance cannot rewrite curriculum", authored,
            LessonRepository.loadLessonJson(context, ROUTE, READING).toString())
    }

    private fun runStandalone(scenario: ActivityScenario<CppPrototypeActivity>) {
        scenario.onActivity { activity ->
            assertTrue(activity.findViewById<View>(R.id.runButton).isEnabled)
            activity.findViewById<View>(R.id.runButton).performClick()
            assertFalse(activity.findViewById<View>(R.id.runButton).isEnabled)
        }
        awaitActivity(scenario, "The explicitly selected native compiler executes the same source", 120_000) { activity ->
            activity.findViewById<View>(R.id.runButton).isEnabled
        }
        scenario.onActivity { activity ->
            assertEquals(activity.getString(R.string.cpp_runtime_finished, 0),
                activity.findViewById<TextView>(R.id.statusText).text.toString())
            assertEquals("standalone", activity.findViewById<TextView>(R.id.outputText).text.toString())
            val input = activity.findViewById<EditText>(R.id.sourceCode)
            assertEquals(2, input.selectionStart)
            assertEquals(5, input.selectionEnd)
        }
    }

    private fun runLab(scenario: ActivityScenario<LessonActivity>) {
        scenario.onActivity { activity ->
            val run = tagged(activity, "cpp_run")
            assertTrue(run.isEnabled)
            run.performClick()
            assertFalse("The real native job disables Run", run.isEnabled)
        }
        awaitActivity(scenario, "Native compilation/execution finishes", 120_000) { tagged(it, "cpp_run").isEnabled }
    }

    private fun assertCompactConsole(activity: Activity) {
        val consoles = descendants(activity.window.decorView).filterIsInstance<CppConsoleView>()
        assertEquals("There is one result console", 1, consoles.size)
        assertEquals("There is one displayed output body", 1,
            descendants(activity.window.decorView).count { it.tag == "cpp_console_display" && it.visibility == View.VISIBLE })
        listOf(consoles.single().diagnostics, consoles.single().stdout, consoles.single().stderr).forEach {
            assertEquals("Raw channels occupy no layout space", View.GONE, it.visibility)
            assertEquals(View.IMPORTANT_FOR_ACCESSIBILITY_NO, it.importantForAccessibility)
        }
    }

    private fun assertStandaloneOutput(activity: CppPrototypeActivity) {
        assertTrue("The standalone compiler keeps its existing single output console", activity.findViewById<View>(R.id.outputText).isShown)
        assertEquals(1, descendants(activity.window.decorView).count { it.id == R.id.outputText && it.visibility == View.VISIBLE })
        assertTrue("The existing standalone console is not rebuilt with another result renderer",
            descendants(activity.window.decorView).none { it is CppConsoleView })
    }

    private fun console(activity: Activity) = descendants(activity.window.decorView).filterIsInstance<CppConsoleView>().single()
    private fun tagged(activity: Activity, tag: String) = descendants(activity.window.decorView).single { it.tag == tag }
    private fun selectedCompiler(activity: CppPrototypeActivity) = field(activity, "selectedCompiler") as CppCompilerVariant
    private fun field(owner: Any, name: String): Any? = owner.javaClass.getDeclaredField(name).apply { isAccessible = true }.get(owner)
    private fun descendants(root: View): List<View> = buildList {
        add(root)
        if (root is ViewGroup) for (index in 0 until root.childCount) addAll(descendants(root.getChildAt(index)))
    }
    private fun preferenceFiles() = File(context.applicationInfo.dataDir, "shared_prefs").listFiles().orEmpty()
        .filter { it.extension == "xml" }.map { it.nameWithoutExtension }.toSet()
    private fun screenshot(name: String) {
        val bitmap = requireNotNull(instrumentation.uiAutomation.takeScreenshot())
        try {
            val directory = File(requireNotNull(context.getExternalFilesDir(null)), "cpp-compact-console-qa").apply { mkdirs() }
            File(directory, "$name-${SystemClock.uptimeMillis()}.png").outputStream().use {
                assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it))
            }
        } finally { bitmap.recycle() }
    }

    private fun popupGeometry(scenario: ActivityScenario<LessonActivity>, position: String) {
        fun rect(rect: Rect) = JSONArray(listOf(rect.left, rect.top, rect.right, rect.bottom))
        val metadata = JSONObject().put("selectorPosition", position)
        scenario.onActivity { activity ->
            val selector = tagged(activity, "cpp_console_channels")
            val visible = Rect()
            selector.getGlobalVisibleRect(visible)
            val origin = IntArray(2)
            selector.getLocationOnScreen(origin)
            val frame = Rect()
            activity.window.decorView.getWindowVisibleDisplayFrame(frame)
            metadata.put("imeVisible", ViewCompat.getRootWindowInsets(activity.window.decorView)
                ?.isVisible(WindowInsetsCompat.Type.ime()) == true)
                .put("selectorBounds", rect(Rect(origin[0], origin[1], origin[0] + selector.width, origin[1] + selector.height)))
                .put("selectorVisibleBounds", rect(visible)).put("windowVisibleFrame", rect(frame))
                .put("orientation", activity.resources.configuration.orientation)
                .put("configurationFontScale", activity.resources.configuration.fontScale)
        }
        val windows = JSONArray()
        for (window in instrumentation.uiAutomation.windows) {
            val bounds = Rect()
            window.getBoundsInScreen(bounds)
            val entry = JSONObject().put("type", window.type).put("focused", window.isFocused)
                .put("active", window.isActive).put("bounds", rect(bounds))
            val root = window.root
            if (root != null) {
                entry.put("package", root.packageName?.toString())
                if (root.packageName?.toString() == QA_PACKAGE) {
                    val nodes = JSONArray()
                    fun inspect(node: AccessibilityNodeInfo) {
                        val nodeBounds = Rect()
                        node.getBoundsInScreen(nodeBounds)
                        nodes.put(JSONObject().put("class", node.className?.toString())
                            .put("text", node.text?.toString()?.take(200)).put("bounds", rect(nodeBounds))
                            .put("visible", node.isVisibleToUser).put("clickable", node.isClickable))
                        for (index in 0 until node.childCount) node.getChild(index)?.let { child ->
                            try { inspect(child) } finally { @Suppress("DEPRECATION") child.recycle() }
                        }
                    }
                    inspect(root)
                    entry.put("qaNodes", nodes)
                }
                @Suppress("DEPRECATION")
                root.recycle()
            }
            windows.put(entry)
            @Suppress("DEPRECATION")
            window.recycle()
        }
        metadata.put("windows", windows)
        val directory = File(requireNotNull(context.getExternalFilesDir(null)), "cpp-compact-console-qa").apply { mkdirs() }
        File(directory, "popup-geometry-$position-${SystemClock.uptimeMillis()}.json").writeText(metadata.toString(2))
        println("Compact console popup geometry: $metadata")
    }
    private fun await(label: String, timeout: Long = 15_000, ready: () -> Boolean) {
        val deadline = SystemClock.uptimeMillis() + timeout
        do {
            instrumentation.waitForIdleSync()
            if (ready()) return
            SystemClock.sleep(50)
        } while (SystemClock.uptimeMillis() < deadline)
        fail(label)
    }
    private fun <T : Activity> awaitActivity(scenario: ActivityScenario<T>, label: String, timeout: Long = 15_000,
                                           ready: (T) -> Boolean) = await(label, timeout) {
        var result = false
        scenario.onActivity { result = ready(it) }
        result
    }

    companion object {
        private const val QA_PACKAGE = "com.learngraphicsprogramming.app.realdownloadsqa"
        private const val ROUTE = "programming"
        private const val READING = "cpp_fundamentals__objetos_y_estado__leer_una_entrada_conocida"
        private val KNOWN_PREFERENCES = setOf("cpp_lesson_drafts", "learning_progress", "navigation_state",
            "lesson_narration_settings", "app_settings", "ui_language_settings", "attempt_reserve_seconds_test")
    }
}
