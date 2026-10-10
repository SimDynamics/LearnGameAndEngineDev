package com.learngraphicsprogramming.app

import android.view.View
import android.app.Activity
import android.app.Application
import android.content.pm.ActivityInfo
import android.content.res.Configuration
import android.os.Bundle
import android.os.ParcelFileDescriptor
import android.os.SystemClock
import android.view.WindowManager
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.runtime.Clang21FeatureDelivery
import com.learngraphicsprogramming.app.runtime.CppCompiler
import com.learngraphicsprogramming.app.runtime.CppCompilerVariant
import com.learngraphicsprogramming.app.runtime.CppExecutionEngine
import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Before
import org.junit.After
import org.junit.Test
import org.junit.runner.RunWith

/**
 * Smoke checks for Play Feature Delivery wiring.
 *
 * Safe in normal debug APKs and experimental App Bundles.
 * These tests NEVER request or download the optional module.
 */
@RunWith(AndroidJUnit4::class)
class Clang21FeatureDeliveryInstrumentedTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val application = instrumentation.targetContext.applicationContext as Application
    private val screenGuard = object : Application.ActivityLifecycleCallbacks {
        override fun onActivityCreated(activity: Activity, state: Bundle?) {
            if (activity is CppPrototypeActivity)
                activity.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        }
        override fun onActivityStarted(activity: Activity) = Unit
        override fun onActivityResumed(activity: Activity) = Unit
        override fun onActivityPaused(activity: Activity) = Unit
        override fun onActivityStopped(activity: Activity) = Unit
        override fun onActivitySaveInstanceState(activity: Activity, state: Bundle) = Unit
        override fun onActivityDestroyed(activity: Activity) = Unit
    }

    @Before fun prepareOnlyTheTestActivities() {
        application.registerActivityLifecycleCallbacks(screenGuard)
        for (command in listOf("input keyevent KEYCODE_WAKEUP", "wm dismiss-keyguard", "cmd statusbar collapse")) {
            ParcelFileDescriptor.AutoCloseInputStream(instrumentation.uiAutomation.executeShellCommand(command)).use { it.readBytes() }
        }
    }

    @After fun releaseTestScreenGuard() {
        application.unregisterActivityLifecycleCallbacks(screenGuard)
    }

    @Test
    fun applicationEntryPointExists() {
        val app = InstrumentationRegistry.getInstrumentation()
            .targetContext.applicationContext
        assertTrue(
            "AndroidManifest.xml must resolve LearnGraphicsApplication",
            app is LearnGraphicsApplication
        )
    }

    @Test
    fun downloadPanelAppearsOnlyForFeatureBuilds() {
        ActivityScenario.launch(CppPrototypeActivity::class.java).use { scenario ->
            scenario.onActivity { activity ->
                val section = activity.findViewById<View>(
                    R.id.clang21FeatureSection
                )
                assertEquals(
                    if (BuildConfig.LGP_PLAY_CLANG21_FEATURE_ENABLED)
                        View.VISIBLE
                    else
                        View.GONE,
                    section.visibility
                )
                assertTrue(
                    "The default Clang 23 run button must remain available",
                    activity.findViewById<View>(R.id.runButton).isEnabled
                )
                assertEquals("Compiler choice remains visible in every APK",
                    View.VISIBLE,
                    activity.findViewById<View>(R.id.clangCompilerSelectorSection).visibility)
                val defaultRadio = activity.findViewById<android.widget.RadioButton>(
                    R.id.clang23RadioButton
                )
                val optionalRadio = activity.findViewById<android.widget.RadioButton>(
                    R.id.clang21RadioButton
                )
                assertEquals("The checked row must name the actual default compiler",
                    BuildConfig.LGP_CLANG23_ENABLED, defaultRadio.isChecked)
                assertEquals(!BuildConfig.LGP_CLANG23_ENABLED, optionalRadio.isChecked)
                assertEquals(BuildConfig.LGP_CLANG23_ENABLED, defaultRadio.isEnabled)
                assertEquals(
                    "Clang 21 must only be selectable with an executable native installation",
                    CppCompiler.isClang21Available(activity),
                    optionalRadio.isEnabled
                )
                if (BuildConfig.LGP_CLANG23_ENABLED && !BuildConfig.LGP_PLAY_CLANG21_FEATURE_ENABLED) {
                    assertFalse("This APK cannot pretend Clang 21 can be executed", optionalRadio.isEnabled)
                    assertEquals(activity.getString(R.string.clang21_unavailable_in_build),
                        activity.findViewById<android.widget.TextView>(R.id.clangCompilerAvailability).text.toString())
                }
                if (optionalRadio.isEnabled) {
                    optionalRadio.performClick()
                    assertTrue("Clang 21 must be selectable", optionalRadio.isChecked)
                    if (defaultRadio.isEnabled) {
                        defaultRadio.performClick()
                        assertTrue("The user must be able to return to Clang 23", defaultRadio.isChecked)
                    }
                }
            }
        }
    }

    @Test
    fun unavailableOptionalSelectionIsRejectedByCompilerAndEngineBeforeCreatingJobs() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        assumeTrue("This regression needs an unavailable optional compiler",
            !CppCompiler.isOptionalClang21Available(context))
        val jobs = File(context.cacheDir, "cpp-jobs")
        val before = jobs.list().orEmpty().toSet()
        val source = "int main() { return 0; }"
        val compileError = runCatching {
            CppCompiler(context).compile(source, CppCompilerVariant.OPTIONAL_CLANG_21)
        }.exceptionOrNull()
        val engineError = runCatching {
            CppExecutionEngine(context).compileAndRun(source,
                compilerVariant = CppCompilerVariant.OPTIONAL_CLANG_21)
        }.exceptionOrNull()
        for (error in listOf(compileError, engineError)) {
            assertTrue("Unavailable Clang 21 must fail explicitly instead of compiling with Clang 23",
                error is IllegalStateException && error.message?.contains("Clang 21") == true)
        }
        assertEquals("Rejected selection must not create a compiler workspace", before, jobs.list().orEmpty().toSet())
    }

    @Test
    fun unavailableRestoredClang21ChoiceSurvivesFullscreenRecreationWithoutSilentFallback() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        assumeTrue("This regression uses the modern APK without executable Clang 21",
            BuildConfig.LGP_CLANG23_ENABLED && !CppCompiler.isOptionalClang21Available(context))
        ActivityScenario.launch(CppPrototypeActivity::class.java).use { scenario ->
            var originalOrientation = ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
            scenario.onActivity { originalOrientation = it.requestedOrientation }
            try {
                scenario.onActivity { activity ->
                    // Represent a saved choice whose optional split is no longer available;
                    // changing only this test Activity avoids removing any installed module.
                    activity.javaClass.getDeclaredField("selectedCompiler").apply {
                        isAccessible = true
                        set(activity, CppCompilerVariant.OPTIONAL_CLANG_21)
                    }
                    activity.findViewById<android.widget.EditText>(R.id.sourceCode).apply {
                        setText("int main() { return 7; }")
                        setSelection(4, 8)
                    }
                    activity.findViewById<View>(R.id.fullscreenCodeButton).performClick()
                }
                scenario.recreate()
                awaitCompiler(scenario, "Recreated compiler must finish restoring fullscreen") { activity ->
                    activity.findViewById<View>(R.id.fullscreenCodeButton).contentDescription ==
                        activity.getString(R.string.exercise_exit_fullscreen)
                }
                scenario.onActivity { activity ->
                    val clang21 = activity.findViewById<android.widget.RadioButton>(R.id.clang21RadioButton)
                    val clang23 = activity.findViewById<android.widget.RadioButton>(R.id.clang23RadioButton)
                    assertTrue("The restored compiler choice must not switch silently", clang21.isChecked)
                    assertFalse("Unavailable Clang 21 remains disabled", clang21.isEnabled)
                    assertFalse(clang23.isChecked)
                    assertEquals("The actual compiler choice must also remain Clang 21",
                        CppCompilerVariant.OPTIONAL_CLANG_21,
                        activity.javaClass.getDeclaredField("selectedCompiler").apply { isAccessible = true }.get(activity))
                    assertEquals(activity.getString(R.string.clang21_selected_unavailable),
                        activity.findViewById<android.widget.TextView>(R.id.clangCompilerAvailability).text.toString())
                    val source = activity.findViewById<android.widget.EditText>(R.id.sourceCode)
                    assertEquals("int main() { return 7; }", source.text.toString())
                    assertEquals(4, source.selectionStart)
                    assertEquals(8, source.selectionEnd)
                    val fullscreen = activity.findViewById<View>(R.id.fullscreenCodeButton)
                    assertEquals(activity.getString(R.string.exercise_exit_fullscreen), fullscreen.contentDescription)
                    fullscreen.performClick()
                    assertUnavailableChoice(activity)
                    fullscreen.performClick()
                    activity.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
                }
                awaitCompiler(scenario, "The device must rotate the fullscreen compiler to landscape") { activity ->
                    activity.resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE &&
                        activity.findViewById<View>(R.id.sourceCode).isLaidOut
                }
                scenario.onActivity { activity ->
                    assertUnavailableChoice(activity)
                    val fullscreen = activity.findViewById<View>(R.id.fullscreenCodeButton)
                    assertEquals(activity.getString(R.string.exercise_exit_fullscreen), fullscreen.contentDescription)
                    fullscreen.performClick()
                    assertUnavailableChoice(activity)
                    fullscreen.performClick()
                    activity.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
                }
                awaitCompiler(scenario, "The device must rotate the fullscreen compiler back to portrait") { activity ->
                    activity.resources.configuration.orientation == Configuration.ORIENTATION_PORTRAIT &&
                        activity.findViewById<View>(R.id.sourceCode).isLaidOut
                }
                scenario.onActivity { activity ->
                    assertUnavailableChoice(activity)
                    activity.findViewById<View>(R.id.fullscreenCodeButton).performClick()
                    assertUnavailableChoice(activity)
                    val output = activity.findViewById<android.widget.TextView>(R.id.outputText).text.toString()
                    activity.findViewById<View>(R.id.runButton).performClick()
                    assertEquals("An unavailable choice must not launch a different compiler", output,
                        activity.findViewById<android.widget.TextView>(R.id.outputText).text.toString())
                    assertUnavailableChoice(activity)
                    activity.findViewById<android.widget.RadioButton>(R.id.clang23RadioButton).apply {
                        performClick()
                        assertTrue("Returning to Clang 23 requires an explicit user choice", isChecked)
                    }
                }
            } finally {
                scenario.onActivity { it.requestedOrientation = originalOrientation }
            }
        }
    }

    private fun assertUnavailableChoice(activity: CppPrototypeActivity) {
        val clang21 = activity.findViewById<android.widget.RadioButton>(R.id.clang21RadioButton)
        assertTrue("Rotation and fullscreen must not silently replace Clang 21", clang21.isChecked)
        assertFalse("Unavailable Clang 21 must stay disabled", clang21.isEnabled)
        assertFalse(activity.findViewById<android.widget.RadioButton>(R.id.clang23RadioButton).isChecked)
        assertEquals(CppCompilerVariant.OPTIONAL_CLANG_21,
            activity.javaClass.getDeclaredField("selectedCompiler").apply { isAccessible = true }.get(activity))
        assertEquals(activity.getString(R.string.clang21_selected_unavailable),
            activity.findViewById<android.widget.TextView>(R.id.clangCompilerAvailability).text.toString())
        activity.findViewById<android.widget.EditText>(R.id.sourceCode).also { source ->
            assertEquals("int main() { return 7; }", source.text.toString())
            assertEquals(4, source.selectionStart)
            assertEquals(8, source.selectionEnd)
        }
    }

    private fun awaitCompiler(scenario: ActivityScenario<CppPrototypeActivity>, message: String,
                              ready: (CppPrototypeActivity) -> Boolean) {
        val deadline = SystemClock.elapsedRealtime() + 10_000
        var completed = false
        while (SystemClock.elapsedRealtime() < deadline) {
            scenario.onActivity { completed = ready(it) }
            if (completed) return
            SystemClock.sleep(50)
        }
        assertTrue(message, completed)
    }

    @Test
    fun compilerSelectorAppearsAfterCompilationResults() {
        ActivityScenario.launch(CppPrototypeActivity::class.java).use { scenario ->
            scenario.onActivity { activity ->
                val run = activity.findViewById<View>(R.id.runButton)
                val output = activity.findViewById<View>(R.id.outputText)
                val selector = activity.findViewById<View>(R.id.clangCompilerSelectorSection)
                val feature = activity.findViewById<View>(R.id.clang21FeatureSection)
                val parent = output.parent as android.view.ViewGroup

                assertTrue("Run button and output must share a container", run.parent === parent)
                assertTrue("Compiler selector must share the output container", selector.parent === parent)
                assertTrue("Download panel must share the output container", feature.parent === parent)
                assertTrue(
                    "The compilation output must remain below the Run button",
                    parent.indexOfChild(run) < parent.indexOfChild(output)
                )
                assertTrue(
                    "Compiler selection must appear below the result box",
                    parent.indexOfChild(output) < parent.indexOfChild(selector)
                )
                assertTrue(
                    "Optional module download controls must follow compiler selection",
                    parent.indexOfChild(selector) < parent.indexOfChild(feature)
                )
            }
        }
    }

    @Test
    fun realClang21InstallationDisplaysAccurateCompilerStatus() {
        ActivityScenario.launch(CppPrototypeActivity::class.java).use { scenario ->
            scenario.onActivity { activity ->
                val phase = Clang21FeatureDelivery(activity).use { installer ->
                    installer.currentState().phase
                }
                if (phase == Clang21FeatureDelivery.Phase.INSTALLED) {
                    val ready = CppCompiler.isOptionalClang21Available(activity)
                    val expected = activity.getString(
                        if (ready) R.string.clang21_ready else R.string.clang21_installed
                    )
                    assertEquals(
                        expected,
                        activity.findViewById<android.widget.TextView>(
                            R.id.clang21FeatureStatus
                        ).text.toString()
                    )
                    assertEquals(
                        "The optional compiler choice follows native executable availability",
                        ready,
                        activity.findViewById<android.widget.RadioButton>(
                            R.id.clang21RadioButton
                        ).isEnabled
                    )
                }
            }
        }
    }

    @Test
    fun installedMeansAndroidHasTheActualModuleSplit() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val installer = Clang21FeatureDelivery(context)
        try {
            val installed = installer.currentState().phase ==
                Clang21FeatureDelivery.Phase.INSTALLED
            val realSplit = context.packageManager.getPackageInfo(
                context.packageName, 0
            ).splitNames?.contains(Clang21FeatureDelivery.MODULE_NAME) == true
            assertEquals(
                "Fake SplitCompat installations must not count as executable native splits",
                realSplit && BuildConfig.LGP_PLAY_CLANG21_FEATURE_ENABLED,
                installed
            )
        } finally {
            installer.close()
        }
    }

    @Test
    fun installerDoesNotRequestAnythingOnCreation() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val installer = Clang21FeatureDelivery(context)
        try {
            assertEquals("clang21", Clang21FeatureDelivery.MODULE_NAME)
            val state = installer.currentState()
            if (BuildConfig.LGP_PLAY_CLANG21_FEATURE_ENABLED) {
                assertTrue(
                    "An optional feature must initially be absent or installed",
                    state.phase == Clang21FeatureDelivery.Phase.NOT_INSTALLED ||
                        state.phase == Clang21FeatureDelivery.Phase.INSTALLED ||
                        state.phase == Clang21FeatureDelivery.Phase.EMULATED
                )
            } else {
                assertEquals(
                    Clang21FeatureDelivery.Phase.NOT_IN_BUILD,
                    state.phase
                )
            }
        } finally {
            installer.close()
        }
    }
}
