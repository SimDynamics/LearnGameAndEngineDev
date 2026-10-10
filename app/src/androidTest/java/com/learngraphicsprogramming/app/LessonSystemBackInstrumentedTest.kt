package com.learngraphicsprogramming.app

import android.app.Activity
import android.app.Application
import android.content.Context
import android.content.Intent
import android.graphics.Rect
import android.os.Bundle
import android.os.ParcelFileDescriptor
import android.os.SystemClock
import android.view.InputDevice
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.SeekBar
import android.widget.TextView
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/**
 * Android's edge-back gesture and navigation button both dispatch Back through
 * OnBackPressedDispatcher. Simulate this action without synthesizing a custom
 * edge swipe, preserving all editor drag and horizontal-page gesture ownership.
 */
@RunWith(AndroidJUnit4::class)
class LessonSystemBackInstrumentedTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val application = context.applicationContext as Application
    private val screenGuard = object : Application.ActivityLifecycleCallbacks {
        override fun onActivityCreated(activity: Activity, state: Bundle?) {
            if (activity is LessonActivity) {
                activity.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
            }
        }
        override fun onActivityStarted(activity: Activity) = Unit
        override fun onActivityResumed(activity: Activity) = Unit
        override fun onActivityPaused(activity: Activity) = Unit
        override fun onActivityStopped(activity: Activity) = Unit
        override fun onActivitySaveInstanceState(activity: Activity, state: Bundle) = Unit
        override fun onActivityDestroyed(activity: Activity) = Unit
    }
    private val programmingRoute = "programming"
    private val programmingCourse = "cpp_fundamentals"
    private val programmingUnit = "cpp_fundamentals__intro_programming"
    private val programmingLesson = "cpp_fundamentals__intro_programming__what_is_programming"

    private val preferenceSnapshots = linkedMapOf<String, Map<String, *>>()

    @Before
    fun keepOnlyTheTestActivitiesAwake() {
        application.registerActivityLifecycleCallbacks(screenGuard)
        for (command in listOf("input keyevent KEYCODE_WAKEUP", "wm dismiss-keyguard")) {
            ParcelFileDescriptor.AutoCloseInputStream(
                instrumentation.uiAutomation.executeShellCommand(command)
            ).use { it.readBytes() }
        }
    }

    @After
    fun releaseTestScreenGuard() {
        application.unregisterActivityLifecycleCallbacks(screenGuard)
    }

    @Before
    fun verifyApplicationHasCurrentNavigationFix() {
        // Gradle may build the test APK while a previous app bundle is
        // still installed. Detect that before interpreting any navigation.
        assertTrue(
            "The installed app is older than the Back fix. Rebuild :app:bundleDebug " +
                "and reinstall base,clang21 before running the tests.",
            LessonActivity::class.java.declaredFields.any {
                it.name == "compilerFullscreenBackExit"
            }
        )
    }

    @Before
    fun preserveProgress() {
        listOf("navigation_state", "learning_progress").forEach { name ->
            preferenceSnapshots[name] =
                context.getSharedPreferences(name, Context.MODE_PRIVATE).all.toMap()
        }
    }

    @After
    fun restoreProgress() {
        preferenceSnapshots.forEach { (name, before) ->
            val prefs = context.getSharedPreferences(name, Context.MODE_PRIVATE)
            val editor = prefs.edit()
            (prefs.all.keys - before.keys).forEach(editor::remove)
            before.forEach { (key, value) ->
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

    private fun programmingIntent() =
        Intent(context, LessonActivity::class.java)
            .putExtra("routeId", programmingRoute)
            .putExtra("courseId", programmingCourse)
            .putExtra("unitId", programmingUnit)
            .putExtra("lessonId", programmingLesson)
            .putExtra("lessonNumber", 1)

    private fun mathIntent() =
        Intent(context, LessonActivity::class.java)
            .putExtra("routeId", "math_physics")
            .putExtra("courseId", "math_course_01")
            .putExtra("unitId", "math_course_01__arithmetic_fundamentals")
            .putExtra(
                "lessonId",
                "math_course_01__arithmetic_fundamentals__numbers_operations"
            )
            .putExtra("lessonNumber", 1)

    @Test
    fun programmingLessonRespondsToSystemBackOutsideFullscreen() {
        ProgressRepository.saveCurrentPage(context, programmingRoute, programmingLesson, 0)
        ActivityScenario.launch<LessonActivity>(programmingIntent()).use { scenario ->
            scenario.onActivity { activity ->
                assertFalse(activity.isFinishing)
                activity.onBackPressedDispatcher.onBackPressed()
                assertTrue("Back must leave a normal programming lesson", activity.isFinishing)
            }
        }
    }

    @Test
    fun mathematicsLessonStillRespondsToSystemBack() {
        ActivityScenario.launch<LessonActivity>(mathIntent()).use { scenario ->
            scenario.onActivity { activity ->
                assertFalse(activity.isFinishing)
                activity.onBackPressedDispatcher.onBackPressed()
                assertTrue("Mathematics Back behavior must stay intact", activity.isFinishing)
            }
        }
    }

    @Test
    fun programmingExerciseFullscreenClosesBeforeLeavingLesson() {
        // The first programming lesson's sixth page has a block-order puzzle.
        ProgressRepository.saveCurrentPage(context, programmingRoute, programmingLesson, 5)
        ActivityScenario.launch<LessonActivity>(programmingIntent()).use { scenario ->
            scenario.onActivity { activity ->
                val root = activity.findViewById<ViewGroup>(android.R.id.content)
                val fullscreen = root.findViewWithTag<View>("order_code_fullscreen_0")
                assertNotNull("Expected the programming ordering-puzzle fullscreen control", fullscreen)
                assertTrue(fullscreen.performClick())
                assertNotNull(
                    "Exercise fullscreen should be open",
                    root.findViewWithTag<View>("programming_fullscreen_overlay")
                )
                activity.onBackPressedDispatcher.onBackPressed()
                assertNull(
                    "Back must close the exercise fullscreen without rebuilding the lesson",
                    root.findViewWithTag<View>("programming_fullscreen_overlay")
                )
                assertFalse("Back should not exit the lesson on the first gesture", activity.isFinishing)
                activity.onBackPressedDispatcher.onBackPressed()
                assertTrue("Second Back must exit the lesson after fullscreen closes", activity.isFinishing)
            }
        }
    }

    @Test
    fun programmingCompilerFullscreenClosesBeforeLeavingLesson() {
        // Page eight uses the real compiler, whose fullscreen is independent.
        ProgressRepository.saveCurrentPage(context, programmingRoute, programmingLesson, 7)
        ActivityScenario.launch<LessonActivity>(programmingIntent()).use { scenario ->
            scenario.onActivity { activity ->
                val root = activity.findViewById<ViewGroup>(android.R.id.content)
                val control = root.findViewWithTag<View>("cpp_compiler_fullscreen")
                assertNotNull(
                    "Expected the compiler fullscreen control on page eight; " +
                        "check that the application APK is current and the page was loaded",
                    control
                )
                assertTrue(control!!.performClick())
                assertEquals(
                    activity.getString(R.string.exercise_exit_fullscreen),
                    control.contentDescription.toString()
                )
                activity.onBackPressedDispatcher.onBackPressed()
                assertEquals(
                    "Back should use the compiler's normal fullscreen-exit path",
                    activity.getString(R.string.exercise_fullscreen),
                    control.contentDescription.toString()
                )
                assertFalse("The lesson must remain open after closing the compiler", activity.isFinishing)
                activity.onBackPressedDispatcher.onBackPressed()
                assertTrue("Second Back should exit the normal lesson", activity.isFinishing)
            }
        }
    }

    @Test
    fun cachedProgrammingCompilerRetainsViewportAfterReturningToItsPage() {
        val longCode = (1..140).joinToString("\n") { line ->
            "// Line $line: " + "long compiler viewport sample ".repeat(12)
        }
        val shortCode = "int main() { return 0; }"
        ActivityScenario.launch<LessonActivity>(programmingIntent()).use { scenario ->
            // Render directly: this exercises the page cache without completing
            // exercises, clearing progress, or resetting the user's lesson.
            scenario.onActivity { renderPage(it, 7) }
            awaitLesson(scenario, "The real compiler source must attach on page eight") { activity ->
                attachedCompiler(activity) != null
            }
            lateinit var controller: Any
            lateinit var input: FreeScrollEditText
            lateinit var horizontal: SeekBar
            lateinit var vertical: SeekBar
            lateinit var gutter: TextView
            scenario.onActivity { activity ->
                controller = requireNotNull(attachedCompiler(activity))
                input = compilerField(controller, "source")
                horizontal = compilerField(controller, "horizontalBar")
                vertical = compilerField(controller, "verticalBar")
                gutter = compilerField(controller, "lineNumbers")
                input.showSoftInputOnFocus = false
                input.setText(longCode)
            }
            awaitLesson(scenario, "Long code must initially expose both viewport ranges") {
                horizontal.max > 0 && vertical.max > 0 && gutter.text.toString().endsWith("140")
            }
            scenario.onActivity { input.setText(shortCode) }
            awaitLesson(scenario, "Short code must clear both actual viewport ranges") {
                horizontal.max == 0 && vertical.max == 0
            }
            scenario.onActivity { renderPage(it, 0) }
            awaitLesson(scenario, "The cached compiler must detach while another page is shown") {
                !input.isAttachedToWindow
            }
            scenario.onActivity { renderPage(it, 7) }
            awaitLesson(scenario, "Returning to page eight must reattach its cached editor") {
                // A cached View can retain valid geometry without a new layout()
                // after attach. Verify the actual ready viewport before real drags.
                input.isAttachedToWindow && input.width > 0 && input.height > 0 &&
                    input.layout != null && !input.isLayoutRequested
            }
            scenario.onActivity { activity ->
                val restoredController = requireNotNull(attachedCompiler(activity))
                assertSame("Navigation must reuse the cached compiler controller", controller, restoredController)
                assertSame("Navigation must reuse the cached compiler source", input,
                    compilerField<FreeScrollEditText>(restoredController, "source"))
                assertSame("Navigation must reuse the cached gutter", gutter,
                    compilerField<TextView>(restoredController, "lineNumbers"))
                assertEquals(shortCode, input.text.toString())
                input.setText(longCode)
                val control = activity.findViewById<ViewGroup>(android.R.id.content)
                    .findViewWithTag<View>("cpp_compiler_fullscreen")
                assertNotNull("The cached compiler fullscreen control must remain available", control)
                assertTrue(requireNotNull(control).performClick())
            }
            val bounds = Rect()
            awaitLesson(scenario, "Editing after reattachment must revive both real scroll ranges") {
                horizontal.max > 0 && vertical.max > 0 && input.getGlobalVisibleRect(bounds) &&
                    bounds.width() > 100 && bounds.height() > 100
            }
            scenario.onActivity {
                input.requestFocus()
                input.setSelection(4, 12)
                input.suppressCursorAutoScroll = true
                input.scrollTo(0, 0)
            }
            InstrumentationRegistry.getInstrumentation().waitForIdleSync()
            scenario.onActivity { assertTrue(input.getGlobalVisibleRect(bounds)) }
            swipeCompiler(bounds.left + bounds.width() * 0.8f, bounds.top + bounds.height() * 0.4f,
                bounds.left + bounds.width() * 0.2f, bounds.top + bounds.height() * 0.4f)
            awaitLesson(scenario, "The cached editor's horizontal drag listener must remain active") {
                input.scrollX > 0 && horizontal.progress == input.scrollX
            }
            scenario.onActivity {
                assertEquals(4, input.selectionStart)
                assertEquals(12, input.selectionEnd)
                assertEquals(0, input.scrollY)
            }
            swipeCompiler(bounds.left + bounds.width() * 0.5f, bounds.top + bounds.height() * 0.8f,
                bounds.left + bounds.width() * 0.5f, bounds.top + bounds.height() * 0.2f)
            awaitLesson(scenario, "The reattached source, vertical bar and gutter must scroll together") {
                input.scrollY > 0 && vertical.progress == input.scrollY &&
                    gutter.translationY == -input.scrollY.toFloat()
            }
            scenario.onActivity {
                assertEquals(4, input.selectionStart)
                assertEquals(12, input.selectionEnd)
            }
        }
    }

    private fun renderPage(activity: LessonActivity, page: Int) {
        LessonActivity::class.java.getDeclaredField("currentPage").apply { isAccessible = true }
            .setInt(activity, page)
        LessonActivity::class.java.getDeclaredMethod("renderCurrentPage").apply { isAccessible = true }
            .invoke(activity)
    }

    private fun attachedCompiler(activity: LessonActivity): Any? {
        val controllers = LessonActivity::class.java.getDeclaredField("cppEditorViewportControllers")
            .apply { isAccessible = true }.get(activity) as List<*>
        return controllers.filterNotNull().firstOrNull {
            compilerField<FreeScrollEditText>(it, "source").isAttachedToWindow
        }
    }

    @Suppress("UNCHECKED_CAST")
    private fun <T> compilerField(controller: Any, name: String): T =
        controller.javaClass.getDeclaredField(name).apply { isAccessible = true }.get(controller) as T

    private fun awaitLesson(scenario: ActivityScenario<LessonActivity>, message: String,
        condition: (LessonActivity) -> Boolean) {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val deadline = SystemClock.uptimeMillis() + 10_000
        while (SystemClock.uptimeMillis() < deadline) {
            instrumentation.waitForIdleSync()
            var ready = false
            scenario.onActivity { ready = condition(it) }
            if (ready) return
            SystemClock.sleep(40) // Instrumentation thread only; leave UI layout runnable.
        }
        scenario.onActivity { assertTrue(message, condition(it)) }
    }

    private fun swipeCompiler(startX: Float, startY: Float, endX: Float, endY: Float) {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val downTime = SystemClock.uptimeMillis()
        fun inject(action: Int, x: Float, y: Float) {
            val event = MotionEvent.obtain(downTime, SystemClock.uptimeMillis(), action, x, y, 0)
                .apply { source = InputDevice.SOURCE_TOUCHSCREEN }
            try {
                assertTrue("The real compiler touchscreen event must be delivered",
                    instrumentation.uiAutomation.injectInputEvent(event, true))
            } finally { event.recycle() }
        }
        inject(MotionEvent.ACTION_DOWN, startX, startY)
        for (step in 1..8) {
            SystemClock.sleep(16)
            inject(MotionEvent.ACTION_MOVE, startX + (endX - startX) * step / 8,
                startY + (endY - startY) * step / 8)
        }
        inject(MotionEvent.ACTION_UP, endX, endY)
        instrumentation.waitForIdleSync()
    }

}
