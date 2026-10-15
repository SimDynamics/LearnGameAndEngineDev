package com.learngraphicsprogramming.app

import android.app.KeyguardManager
import android.content.Context
import android.content.Intent
import android.os.ParcelFileDescriptor
import android.os.SystemClock
import android.view.WindowManager
import android.widget.Button
import android.widget.TextView
import androidx.test.core.app.ActivityScenario
import androidx.test.espresso.Espresso.onData
import androidx.test.espresso.Espresso.onView
import androidx.test.espresso.action.ViewActions.click
import androidx.test.espresso.assertion.ViewAssertions.matches
import androidx.test.espresso.matcher.RootMatchers.isDialog
import androidx.test.espresso.matcher.ViewMatchers.isDisplayed
import androidx.test.espresso.matcher.ViewMatchers.withId
import androidx.test.espresso.matcher.ViewMatchers.withText
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.narration.KokoroModelState
import com.learngraphicsprogramming.app.narration.KokoroModelStatus
import com.learngraphicsprogramming.app.narration.NarrationLanguage
import com.learngraphicsprogramming.app.narration.NarrationEngine
import com.learngraphicsprogramming.app.narration.SpanishVoice
import com.learngraphicsprogramming.app.narration.AndroidVoiceCatalog
import com.learngraphicsprogramming.app.narration.NarrationSettingsRepository
import org.hamcrest.Matchers.anything
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class LessonNarrationSettingsDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val snapshots = linkedMapOf<String, Map<String, *>>()

    @Before fun preservePreferencesAndWakeDevice() {
        listOf("lesson_narration_settings", "learning_progress", "navigation_state").forEach {
            snapshots[it] = context.getSharedPreferences(it, Context.MODE_PRIVATE).all.toMap()
        }
        assertFalse("Unlock LDPlayer or the phone to check lesson settings",
            context.getSystemService(KeyguardManager::class.java).isDeviceLocked)
        for (command in listOf("input keyevent KEYCODE_WAKEUP", "wm dismiss-keyguard")) {
            ParcelFileDescriptor.AutoCloseInputStream(
                instrumentation.uiAutomation.executeShellCommand(command)
            ).use { it.readBytes() }
        }
    }

    @After fun restorePreferences() {
        snapshots.forEach { (name, previous) ->
            val editor = context.getSharedPreferences(name, Context.MODE_PRIVATE).edit().clear()
            previous.forEach { (key, value) ->
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

    private fun launch(route: String): ActivityScenario<LessonOptionsActivity> {
        val programming = route == AppearanceSettingsRepository.ROUTE_PROGRAMMING
        return ActivityScenario.launch(
            Intent(context, LessonOptionsActivity::class.java)
                .putExtra("routeId", route)
                .putExtra("courseId", if (programming) "cpp_fundamentals" else "math_course_01")
                .putExtra("lessonId", if (programming)
                    "cpp_fundamentals__programas_y_ejecucion__primer_programa_y_salida"
                    else "math_course_01__numeros_y_operaciones__magnitudes_y_numeracion")
                .putExtra("lessonNumber", 1)
        )
    }

    @Test fun bothRoutesChangeNarrationLanguageImmediatelyWithoutResettingProgress() {
        NarrationSettingsRepository.saveKokoroVoice(context, "af_heart")
        NarrationSettingsRepository.saveLanguage(context, NarrationLanguage.SPANISH)
        val beforeProgress = context.getSharedPreferences("learning_progress", Context.MODE_PRIVATE).all.toMap()
        val beforeUiLanguage = UiLanguageSettings.selectedTag(context)
        for (route in listOf(
            AppearanceSettingsRepository.ROUTE_PROGRAMMING,
            AppearanceSettingsRepository.ROUTE_MATH_PHYSICS
        )) {
            launch(route).use { scenario ->
                scenario.onActivity { activity ->
                    activity.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                    val label = activity.getString(R.string.narration_selected_language,
                        NarrationLanguage.SPANISH.label(activity))
                    assertEquals(label,
                        activity.findViewById<Button>(R.id.buttonLessonNarrationLanguage).text.toString())
                    assertEquals(activity.getString(R.string.lesson_settings_narration_global_hint),
                        activity.findViewById<TextView>(R.id.textLessonNarrationGlobalHint).text.toString())
                    assertFalse(activity.findViewById<Button>(R.id.buttonSaveCourseSettings).isEnabled)
                }
                onView(withId(R.id.buttonLessonNarrationLanguage)).perform(click())
                onData(anything()).inRoot(isDialog()).atPosition(1).perform(click())
                assertEquals(NarrationLanguage.AMERICAN_ENGLISH, NarrationSettingsRepository.get(context).language)
                scenario.onActivity { activity ->
                    assertEquals(activity.getString(R.string.narration_selected_language,
                        NarrationLanguage.AMERICAN_ENGLISH.label(activity)),
                        activity.findViewById<Button>(R.id.buttonLessonNarrationLanguage).text.toString())
                    assertFalse(activity.findViewById<Button>(R.id.buttonSaveCourseSettings).isEnabled)
                }
                scenario.recreate()
                scenario.onActivity { activity ->
                    assertEquals(activity.getString(R.string.narration_selected_language,
                        NarrationLanguage.AMERICAN_ENGLISH.label(activity)),
                        activity.findViewById<Button>(R.id.buttonLessonNarrationLanguage).text.toString())
                }
            }
            assertEquals(beforeProgress, context.getSharedPreferences("learning_progress", Context.MODE_PRIVATE).all)
            NarrationSettingsRepository.saveLanguage(context, NarrationLanguage.SPANISH)
        }
        assertEquals(beforeUiLanguage, UiLanguageSettings.selectedTag(context))
    }

    @Test fun voiceSelectorOffersOnlyVerifiedChoicesAndExplainsMissingDownloads() {
        NarrationSettingsRepository.saveEngine(context, NarrationEngine.KOKORO)
        NarrationSettingsRepository.saveKokoroVoice(context, "af_heart")
        launch(AppearanceSettingsRepository.ROUTE_PROGRAMMING).use { scenario ->
            // Wait for the real lightweight inspection before injecting an isolated
            // snapshot. No real voice downloads or user files are altered by this test.
            val deadline = SystemClock.uptimeMillis() + 12_000L
            var ready = false
            while (!ready && SystemClock.uptimeMillis() < deadline) {
                instrumentation.waitForIdleSync()
                scenario.onActivity { activity ->
                    ready = statusField(activity) != null
                }
                if (!ready) SystemClock.sleep(100)
            }
            assertTrue("The voice-only inspection must finish", ready)

            scenario.onActivity { activity ->
                setStatus(activity, emptyMap())
            }
            onView(withId(R.id.buttonLessonNarrationVoice)).perform(click())
            onView(withText(R.string.lesson_settings_narration_no_voices))
                .inRoot(isDialog()).check(matches(isDisplayed()))
            onView(withText(R.string.narration_close)).inRoot(isDialog()).perform(click())

            scenario.onActivity { activity ->
                setStatus(activity, mapOf(
                    "af_alloy" to KokoroModelState(KokoroModelStatus.INSTALLED),
                    "af_heart" to KokoroModelState(KokoroModelStatus.INSTALLED),
                    "af_aoede" to KokoroModelState(KokoroModelStatus.NOT_INSTALLED)
                ))
            }
            onView(withId(R.id.buttonLessonNarrationVoice)).perform(click())
            onData(anything()).inRoot(isDialog()).atPosition(0).perform(click())
            assertEquals("af_alloy", NarrationSettingsRepository.get(context).kokoroVoice)
            assertEquals(NarrationLanguage.AMERICAN_ENGLISH,
                NarrationSettingsRepository.get(context).language)
            scenario.onActivity { activity ->
                assertEquals(activity.getString(R.string.narration_selected_kokoro,
                    "af_alloy", activity.getString(R.string.narration_verified)),
                    activity.findViewById<Button>(R.id.buttonLessonNarrationVoice).text.toString())
                assertFalse(activity.findViewById<Button>(R.id.buttonSaveCourseSettings).isEnabled)
            }
        }
    }

    @Test fun androidVoiceSettingsUseGlobalTtsChoicesAndPreserveKokoroOnBothRoutes() {
        val progressBefore = context.getSharedPreferences(
            "learning_progress", Context.MODE_PRIVATE).all.toMap()
        // Ensure a deterministic "automatic" choice for French, even when the
        // real user's saved French Android voice preference is nonempty.
        NarrationSettingsRepository.saveLanguage(context, NarrationLanguage.FRENCH)
        NarrationSettingsRepository.saveAndroidVoice(context, null)
        NarrationSettingsRepository.saveKokoroVoice(context, "af_heart")
        NarrationSettingsRepository.saveEngine(context, NarrationEngine.ANDROID)
        val voice = SpanishVoice(
            "test.android.voice.us", "Test Android US voice · offline", false, 400
        )

        for (route in listOf(
            AppearanceSettingsRepository.ROUTE_PROGRAMMING,
            AppearanceSettingsRepository.ROUTE_MATH_PHYSICS
        )) {
            NarrationSettingsRepository.saveAndroidVoice(context, null)
            launch(route).use { scenario ->
                scenario.onActivity { activity ->
                    activity.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                    val settings = NarrationSettingsRepository.get(activity)
                    assertEquals(NarrationEngine.ANDROID, settings.engine)
                    assertEquals(activity.getString(R.string.narration_selected_language,
                        settings.language.label(activity)),
                        activity.findViewById<Button>(R.id.buttonLessonNarrationLanguage).text.toString())
                    assertEquals(activity.getString(
                        R.string.narration_selected_android,
                        activity.getString(R.string.narration_auto_voice)),
                        activity.findViewById<Button>(R.id.buttonLessonNarrationVoice).text.toString())
                    assertEquals(activity.getString(
                        R.string.lesson_settings_narration_global_hint_android),
                        activity.findViewById<TextView>(R.id.textLessonNarrationGlobalHint).text.toString())
                    assertFalse(activity.findViewById<Button>(R.id.buttonSaveCourseSettings).isEnabled)

                    // Do not let an asynchronous real-device TTS callback
                    // race the synthetic choice used by this isolated UI test.
                    (LessonOptionsActivity::class.java.getDeclaredField("androidVoiceCatalog")
                        .apply { isAccessible = true }.get(activity) as? AndroidVoiceCatalog)?.close()
                    // Fake only the catalog's already-discovered voice choices; no
                    // Android TTS engine or installed device voices are required.
                    setAndroidVoiceCatalog(activity, listOf(voice),
                        NarrationLanguage.AMERICAN_ENGLISH)
                    assertEquals(activity.getString(R.string.narration_selected_android,
                        activity.getString(R.string.narration_auto_voice)),
                        activity.findViewById<Button>(R.id.buttonLessonNarrationVoice).text.toString())
                    activity.findViewById<Button>(R.id.buttonLessonNarrationVoice).performClick()
                }
                onData(anything()).inRoot(isDialog()).atPosition(1).perform(click())
                assertEquals(voice.name, NarrationSettingsRepository.get(context).androidVoice)
                assertEquals(NarrationEngine.ANDROID, NarrationSettingsRepository.get(context).engine)
                assertEquals("af_heart", NarrationSettingsRepository.get(context).kokoroVoice)
                scenario.onActivity { activity ->
                    assertEquals(activity.getString(R.string.narration_selected_android, voice.label),
                        activity.findViewById<Button>(R.id.buttonLessonNarrationVoice).text.toString())
                    assertFalse(activity.findViewById<Button>(R.id.buttonSaveCourseSettings).isEnabled)
                }

                onView(withId(R.id.buttonLessonNarrationLanguage)).perform(click())
                onData(anything()).inRoot(isDialog()).atPosition(3).perform(click())
                assertEquals(NarrationLanguage.FRENCH, NarrationSettingsRepository.get(context).language)
                assertEquals(NarrationEngine.ANDROID, NarrationSettingsRepository.get(context).engine)
                scenario.onActivity { activity ->
                    assertEquals(activity.getString(R.string.narration_selected_android,
                        activity.getString(R.string.narration_auto_voice)),
                        activity.findViewById<Button>(R.id.buttonLessonNarrationVoice).text.toString())
                    assertEquals(activity.getString(
                        R.string.lesson_settings_narration_global_hint_android),
                        activity.findViewById<TextView>(R.id.textLessonNarrationGlobalHint).text.toString())
                    // The English system voice is never offered in French.
                    activity.findViewById<Button>(R.id.buttonLessonNarrationVoice).performClick()
                }
                onData(anything()).inRoot(isDialog()).atPosition(0).perform(click())
                assertEquals(null, NarrationSettingsRepository.get(context).androidVoice)
                onView(withId(R.id.buttonLessonNarrationLanguage)).perform(click())
                onData(anything()).inRoot(isDialog()).atPosition(1).perform(click())
                assertEquals(voice.name, NarrationSettingsRepository.get(context).androidVoice)
                assertEquals("af_heart", NarrationSettingsRepository.get(context).kokoroVoice)
                assertEquals(NarrationEngine.ANDROID, NarrationSettingsRepository.get(context).engine)
                scenario.recreate()
                scenario.onActivity { activity ->
                    val title = activity.findViewById<Button>(R.id.buttonLessonNarrationVoice).text
                    assertTrue(title.toString().startsWith(
                        activity.getString(R.string.narration_selected_android, "").trim()))
                }
            }
            assertEquals(progressBefore,
                context.getSharedPreferences("learning_progress", Context.MODE_PRIVATE).all)
            assertEquals(NarrationEngine.ANDROID, NarrationSettingsRepository.get(context).engine)
        }
    }

    @Test fun automaticEngineKeepsKokoroUiAndItsGlobalSettingsHint() {
        NarrationSettingsRepository.saveLanguage(context, NarrationLanguage.SPANISH)
        NarrationSettingsRepository.saveEngine(context, NarrationEngine.AUTO)
        launch(AppearanceSettingsRepository.ROUTE_PROGRAMMING).use { scenario ->
            scenario.onActivity { activity ->
                assertTrue(activity.findViewById<Button>(R.id.buttonLessonNarrationVoice)
                    .text.toString().startsWith(
                        activity.getString(R.string.narration_selected_kokoro, "", "").substringBefore(':')))
                assertEquals(activity.getString(R.string.lesson_settings_narration_global_hint),
                    activity.findViewById<TextView>(R.id.textLessonNarrationGlobalHint).text.toString())
                assertEquals(NarrationEngine.AUTO, NarrationSettingsRepository.get(activity).engine)
            }
        }
    }

    private fun setAndroidVoiceCatalog(
        activity: LessonOptionsActivity,
        voices: List<SpanishVoice>,
        language: NarrationLanguage
    ) {
        LessonOptionsActivity::class.java.getDeclaredField("lessonAndroidVoices")
            .apply { isAccessible = true }.set(activity, voices)
        LessonOptionsActivity::class.java.getDeclaredField("loadedAndroidVoiceLanguage")
            .apply { isAccessible = true }.set(activity, language)
        LessonOptionsActivity::class.java.getDeclaredMethod("refreshNarrationSelectors")
            .apply { isAccessible = true }.invoke(activity)
    }

    @Suppress("UNCHECKED_CAST")
    private fun statusField(activity: LessonOptionsActivity): Map<String, KokoroModelState>? =
        LessonOptionsActivity::class.java.getDeclaredField("lessonNarrationVoiceStates")
            .apply { isAccessible = true }.get(activity) as? Map<String, KokoroModelState>

    private fun setStatus(activity: LessonOptionsActivity, states: Map<String, KokoroModelState>) {
        LessonOptionsActivity::class.java.getDeclaredField("lessonNarrationVoiceStates")
            .apply { isAccessible = true }.set(activity, states)
        LessonOptionsActivity::class.java.getDeclaredMethod("refreshNarrationSelectors")
            .apply { isAccessible = true }.invoke(activity)
    }
}
