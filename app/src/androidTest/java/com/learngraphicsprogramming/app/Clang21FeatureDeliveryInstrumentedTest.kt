package com.learngraphicsprogramming.app

import android.view.View
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.runtime.Clang21FeatureDelivery
import com.learngraphicsprogramming.app.runtime.CppCompiler
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
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
                assertEquals(
                    if (BuildConfig.LGP_PLAY_CLANG21_FEATURE_ENABLED)
                        View.VISIBLE
                    else
                        View.GONE,
                    activity.findViewById<View>(R.id.clangCompilerSelectorSection).visibility
                )
                val defaultRadio = activity.findViewById<android.widget.RadioButton>(
                    R.id.clang23RadioButton
                )
                val optionalRadio = activity.findViewById<android.widget.RadioButton>(
                    R.id.clang21RadioButton
                )
                assertTrue("Clang 23 must stay the initial choice", defaultRadio.isChecked)
                val featureInstalled = Clang21FeatureDelivery(activity).use { installer ->
                    installer.currentState().phase == Clang21FeatureDelivery.Phase.INSTALLED
                }
                assertEquals(
                    "Clang 21 must only be selectable with an executable native installation",
                    featureInstalled && CppCompiler.isOptionalClang21Available(activity),
                    optionalRadio.isEnabled
                )
                if (optionalRadio.isEnabled) {
                    optionalRadio.performClick()
                    assertTrue("Clang 21 must be selectable", optionalRadio.isChecked)
                    defaultRadio.performClick()
                    assertTrue("The user must be able to return to Clang 23", defaultRadio.isChecked)
                }
            }
        }
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
