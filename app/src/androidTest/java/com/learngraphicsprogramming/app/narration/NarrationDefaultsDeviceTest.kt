package com.learngraphicsprogramming.app.narration

import android.content.Context
import android.content.ContextWrapper
import android.content.res.Resources
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.util.UUID

/** Fresh preference namespaces, never clear an installed application's settings. */
@RunWith(AndroidJUnit4::class)
class NarrationDefaultsDeviceTest {
    private val app = InstrumentationRegistry.getInstrumentation().targetContext

    @Test fun firstReadUsesKokoroAndTheDeviceLanguageAndSubsequentSelectionsSurvive() {
        requireQa()
        val name = "initial_defaults_${UUID.randomUUID()}"
        val isolated = preferenceContext(name)
        try {
            val expected = NarrationInitialSettings.language(Resources.getSystem().configuration.locales.toLanguageTags())
            val fresh = NarrationSettingsRepository.get(isolated)
            assertEquals(NarrationEngine.KOKORO, fresh.engine)
            assertEquals(expected, fresh.language)
            assertEquals(expected.defaultVoice, fresh.kokoroVoice)
            assertEquals(expected.tag, isolated.getSharedPreferences("lesson_narration_settings", 0).getString("language", null))
            NarrationSettingsRepository.saveEngine(isolated, NarrationEngine.ANDROID)
            NarrationSettingsRepository.saveLanguage(isolated, NarrationLanguage.FRENCH)
            NarrationSettingsRepository.saveKokoroVoice(isolated, "ff_siwis")
            NarrationSettingsRepository.saveRate(isolated, 1.5f)
            NarrationSettingsRepository.saveEnabled(isolated, false)
            val selected = NarrationSettingsRepository.get(isolated)
            repeat(3) { assertEquals(selected, NarrationSettingsRepository.get(preferenceContext(name))) }
            assertEquals(NarrationEngine.ANDROID, selected.engine)
            assertEquals(NarrationLanguage.FRENCH, selected.language)
            assertEquals(1.5f, selected.rate)
            assertFalse(selected.enabled)
            // Create only this QA package's external directory for subsequent copied-model QA.
            assertNotNull(app.getExternalFilesDir(null))
        } finally { app.deleteSharedPreferences(name) }
    }

    @Test fun legacyAndExplicitAutoChoicesArePreservedWithoutPreferenceMigration() {
        requireQa()
        val name = "legacy_defaults_${UUID.randomUUID()}"
        val isolated = preferenceContext(name)
        try {
            val preferences = isolated.getSharedPreferences("lesson_narration_settings", 0)
            assertTrue(preferences.edit().putString("engine", "AUTO").putString("language", "en-GB")
                .putString("kokoro_voice:en-GB", "bf_emma").putFloat("rate", 2f).commit())
            val before = preferences.all.toMap()
            val selected = NarrationSettingsRepository.get(isolated)
            assertEquals(NarrationEngine.AUTO, selected.engine)
            assertEquals(NarrationLanguage.BRITISH_ENGLISH, selected.language)
            assertEquals("bf_emma", selected.kokoroVoice)
            assertEquals(before, preferences.all)
            assertTrue(preferences.edit().clear().putString("kokoro_voice", "ff_siwis").commit())
            assertEquals(NarrationLanguage.FRENCH, NarrationSettingsRepository.get(isolated).language)
        } finally { app.deleteSharedPreferences(name) }
    }

    @Test fun concurrentFirstReadNeverOverwritesAnExplicitAndroidChoice() {
        requireQa()
        repeat(30) {
            val name = "concurrent_defaults_${UUID.randomUUID()}"
            val isolated = preferenceContext(name)
            val start = java.util.concurrent.CountDownLatch(1)
            val failure = java.util.concurrent.atomic.AtomicReference<Throwable>()
            val readers = listOf(Thread {
                try { start.await(); NarrationSettingsRepository.get(isolated) }
                catch (error: Throwable) { failure.set(error) }
            }, Thread {
                try { start.await(); NarrationSettingsRepository.saveEngine(isolated, NarrationEngine.ANDROID) }
                catch (error: Throwable) { failure.set(error) }
            })
            try {
                readers.forEach(Thread::start)
                start.countDown()
                readers.forEach { thread -> thread.join(5_000); assertFalse(thread.isAlive) }
                failure.get()?.let { throw AssertionError("Concurrent first settings access failed", it) }
                assertEquals(NarrationEngine.ANDROID, NarrationSettingsRepository.get(isolated).engine)
            } finally { app.deleteSharedPreferences(name) }
        }
    }

    private fun requireQa() = assertEquals("Never modify the user's ordinary app", QA, app.packageName)
    private fun preferenceContext(name: String) = object : ContextWrapper(app) {
        override fun getApplicationContext(): Context = this
        override fun getSharedPreferences(ignored: String, mode: Int) = app.getSharedPreferences(name, mode)
    }
    companion object { private const val QA = "com.learngraphicsprogramming.app.realdownloadsqa" }
}
