package com.learngraphicsprogramming.app

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.runtime.Clang21FeatureDelivery
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
