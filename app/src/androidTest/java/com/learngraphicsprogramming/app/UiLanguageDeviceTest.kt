package com.learngraphicsprogramming.app

import android.app.KeyguardManager
import android.content.Context
import android.content.Intent
import android.os.SystemClock
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.Spinner
import android.widget.TextView
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.narration.NarrationLanguage
import com.learngraphicsprogramming.app.narration.NarrationSettingsRepository
import com.learngraphicsprogramming.app.math.MathAnswer
import com.learngraphicsprogramming.app.math.MathValidators
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class UiLanguageDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val snapshots = linkedMapOf<String, Map<String, *>>()
    private var originalLocale = ""

    @Before fun preserveState() {
        originalLocale = UiLanguageSettings.selectedTag(context)
        listOf("ui_language_settings", "lesson_narration_settings", "navigation_state", "learning_progress",
            "appearance_settings", "learning_flow_settings").forEach { name ->
            snapshots[name] = context.getSharedPreferences(name, Context.MODE_PRIVATE).all.toMap()
        }
        assertFalse("Unlock the phone to check interface language changes",
            context.getSystemService(KeyguardManager::class.java).isDeviceLocked)
        android.os.ParcelFileDescriptor.AutoCloseInputStream(
            instrumentation.uiAutomation.executeShellCommand("input keyevent KEYCODE_WAKEUP")
        ).use { it.readBytes() }
        val keyguard = context.getSystemService(KeyguardManager::class.java)
        if (!keyguard.isKeyguardSecure) {
            android.os.ParcelFileDescriptor.AutoCloseInputStream(
                instrumentation.uiAutomation.executeShellCommand("wm dismiss-keyguard")
            ).use { it.readBytes() }
        }
        instrumentation.runOnMainSync { UiLanguageSettings.select(context, "es") }
        instrumentation.waitForIdleSync()
    }

    @After fun restoreState() {
        instrumentation.runOnMainSync {
            if (android.os.Build.VERSION.SDK_INT >= 33) {
                context.getSystemService(android.app.LocaleManager::class.java).applicationLocales =
                    android.os.LocaleList.forLanguageTags(originalLocale)
            } else {
                androidx.appcompat.app.AppCompatDelegate.setApplicationLocales(
                    androidx.core.os.LocaleListCompat.forLanguageTags(originalLocale))
            }
        }
        snapshots.forEach { (name, original) ->
            val editor = context.getSharedPreferences(name, Context.MODE_PRIVATE).edit().clear()
            original.forEach { (key, value) -> when (value) {
                is String -> editor.putString(key, value)
                is Int -> editor.putInt(key, value)
                is Long -> editor.putLong(key, value)
                is Float -> editor.putFloat(key, value)
                is Boolean -> editor.putBoolean(key, value)
                is Set<*> -> editor.putStringSet(key, value.filterIsInstance<String>().toSet())
            } }
            assertTrue(editor.commit())
        }
    }

    @Test fun selectorUpdatesAllThreeLanguagesAndPersistsAcrossRecreation() {
        ActivityScenario.launch<OptionsActivity>(Intent(context, OptionsActivity::class.java)).use { scenario ->
            for ((tag, title) in listOf("en" to "Settings", "fr" to "Paramètres", "es" to "Opciones")) {
                scenario.onActivity { activity ->
                    activity.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                    val position = UiLanguageSettings.supportedTags(activity).indexOf(tag) + 1
                    activity.findViewById<Spinner>(R.id.spinnerAppLanguage).setSelection(position)
                }
                awaitLocale(scenario, tag)
                scenario.onActivity { activity ->
                    assertTrue("The recreated interface must display $title",
                        descendants(activity.findViewById(android.R.id.content)).filterIsInstance<TextView>()
                            .any { it.text.toString() == title })
                    assertEquals(tag, UiLanguageSettings.selectedTag(activity))
                }
                scenario.recreate()
                awaitLocale(scenario, tag)
                scenario.onActivity { activity ->
                    assertEquals(UiLanguageSettings.supportedTags(activity).indexOf(tag) + 1,
                        activity.findViewById<Spinner>(R.id.spinnerAppLanguage).selectedItemPosition)
                }
            }
        }
    }

    @Test fun uiChangesPreserveEducationalDataNarrationAndProgress() {
        val route = AppearanceSettingsRepository.ROUTE_PROGRAMMING
        val originalCourses = CourseRepository.loadCourses(context, route)
        assertTrue(originalCourses.isNotEmpty())
        val originalUnits = UnitRepository.loadUnits(context, route, originalCourses.first().id)
        val originalLessons = LessonRepository.loadLessons(context, route, originalCourses.first().id, originalUnits.first().id)
        val lessonId = originalLessons.first().id
        val originalLessonJson = LessonRepository.loadLessonJson(context, route, lessonId).toString()
        val progressBefore = context.getSharedPreferences("learning_progress", Context.MODE_PRIVATE).all.toMap()
        NarrationSettingsRepository.saveLanguage(context, NarrationLanguage.FRENCH)
        val narrationBefore = context.getSharedPreferences("lesson_narration_settings", Context.MODE_PRIVATE).all.toMap()
        for (tag in listOf("en", "fr", "es")) {
            instrumentation.runOnMainSync { UiLanguageSettings.select(context, tag) }
            instrumentation.waitForIdleSync()
            val localized = UiLanguageSettings.localizedContext(context)
            assertEquals(originalCourses, CourseRepository.loadCourses(localized, route))
            assertEquals(originalUnits, UnitRepository.loadUnits(localized, route, originalCourses.first().id))
            assertEquals(originalLessons, LessonRepository.loadLessons(localized, route, originalCourses.first().id, originalUnits.first().id))
            assertEquals(originalLessonJson, LessonRepository.loadLessonJson(localized, route, lessonId).toString())
            assertEquals(narrationBefore, context.getSharedPreferences("lesson_narration_settings", Context.MODE_PRIVATE).all)
            assertEquals(progressBefore, context.getSharedPreferences("learning_progress", Context.MODE_PRIVATE).all)
        }
    }

    @Test fun applicationInitializationPreservesExplicitSelectionAndSpanishDefault() {
        instrumentation.runOnMainSync {
            UiLanguageSettings.select(context, "fr")
            UiLanguageSettings.initialize(context)
        }
        assertEquals("fr", UiLanguageSettings.selectedTag(context))
        instrumentation.runOnMainSync {
            UiLanguageSettings.select(context, "")
            context.getSharedPreferences("ui_language_settings", Context.MODE_PRIVATE).edit().clear().commit()
            UiLanguageSettings.initialize(context)
        }
        assertEquals("es", UiLanguageSettings.selectedTag(context))
        assertEquals("es", UiLanguageSettings.DEFAULT_LANGUAGE)
        assertTrue(UiLanguageSettings.supportedTags(context).containsAll(listOf("es", "en", "fr")))
    }

    @Test fun automaticAndRegionalSelectionsSurviveOpeningTheSelector() {
        instrumentation.runOnMainSync { UiLanguageSettings.select(context, "") }
        ActivityScenario.launch<OptionsActivity>(Intent(context, OptionsActivity::class.java)).use { scenario ->
            instrumentation.waitForIdleSync()
            scenario.onActivity {
                it.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                assertEquals(0, it.findViewById<Spinner>(R.id.spinnerAppLanguage).selectedItemPosition)
            }
            scenario.recreate()
            instrumentation.waitForIdleSync()
            assertEquals("", UiLanguageSettings.selectedTag(context))
        }
        instrumentation.runOnMainSync {
            if (android.os.Build.VERSION.SDK_INT >= 33) {
                context.getSystemService(android.app.LocaleManager::class.java).applicationLocales =
                    android.os.LocaleList.forLanguageTags("en-GB")
            } else {
                context.getSharedPreferences("ui_language_settings", Context.MODE_PRIVATE).edit()
                    .putString("selection", "en-GB").commit()
                androidx.appcompat.app.AppCompatDelegate.setApplicationLocales(
                    androidx.core.os.LocaleListCompat.forLanguageTags("en-GB"))
            }
        }
        ActivityScenario.launch<OptionsActivity>(Intent(context, OptionsActivity::class.java)).use { scenario ->
            awaitLocale(scenario, "en")
            scenario.onActivity { assertEquals(2, it.findViewById<Spinner>(R.id.spinnerAppLanguage).selectedItemPosition) }
            instrumentation.waitForIdleSync()
            assertEquals("en-GB", UiLanguageSettings.selectedTag(context))
        }
    }

    @Test fun invalidMathInputKeepsItsSemanticsAndUsesTheInterfaceLanguage() {
        val validator = JSONObject().put("type", "numeric").put("expected", 4)
        val invalid = MathValidators.validate(validator, MathAnswer.Text("bad"))
        assertFalse(invalid.correct)
        assertFalse(invalid.evaluable)
        for ((tag, expected) in listOf(
            "es" to "Escribe un número o una fracción válidos.",
            "en" to "Enter a valid number or fraction.",
            "fr" to "Saisissez un nombre ou une fraction valide."
        )) {
            instrumentation.runOnMainSync { UiLanguageSettings.select(context, tag) }
            assertEquals(expected, invalid.resolveMessage(UiLanguageSettings.localizedContext(context)))
        }
    }

    private fun awaitLocale(scenario: ActivityScenario<OptionsActivity>, tag: String) {
        val deadline = SystemClock.uptimeMillis() + 12000
        while (SystemClock.uptimeMillis() < deadline) {
            var ready = false
            instrumentation.waitForIdleSync()
            scenario.onActivity { ready = it.resources.configuration.locales[0].language == tag }
            if (ready) return
            SystemClock.sleep(150)
        }
        var actual = ""
        scenario.onActivity {
            actual = "resources=${it.resources.configuration.locales}, selected=${UiLanguageSettings.selectedTag(it)}, " +
                "spinner=${it.findViewById<Spinner>(R.id.spinnerAppLanguage).selectedItemPosition}"
        }
        fail("Interface did not update to $tag: $actual")
    }

    private fun descendants(view: View): Sequence<View> = sequence {
        yield(view)
        if (view is ViewGroup) for (index in 0 until view.childCount) yieldAll(descendants(view.getChildAt(index)))
    }
}
