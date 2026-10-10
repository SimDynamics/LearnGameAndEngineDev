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
                        state.phase == Clang21FeatureDelivery.Phase.INSTALLED
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
