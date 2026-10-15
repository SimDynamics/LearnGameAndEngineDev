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

    @Test fun uiChangesLoadTranslatedEducationalAssetsAndKeepIdentifiersAndProgress() {
        val route = AppearanceSettingsRepository.ROUTE_PROGRAMMING
        val courseId = "cpp_fundamentals"
        val unitId = "cpp_fundamentals__programas_y_ejecucion"
        val lessonId = "${unitId}__primer_programa_y_salida"
        val spanishCourses = CourseRepository.loadCourses(context, route)
        val spanishUnits = UnitRepository.loadUnits(context, route, courseId)
        val spanishLessons = LessonRepository.loadLessons(context, route, courseId, unitId)
        val spanishLesson = LessonRepository.loadLessonJson(context, route, lessonId)
        val originalParagraph = spanishLesson.getJSONArray("pages")
            .getJSONObject(0).getJSONArray("blocks").getJSONObject(1).getString("text")

        // This math sandbox has no EN/FR asset, even though its course manifests do.
        val mathId = "math_course_01__arithmetic_fundamentals__numbers_operations"
        val spanishMath = LessonRepository.loadLessonJson(
            context, AppearanceSettingsRepository.ROUTE_MATH_PHYSICS, mathId).toString()
        val progressBefore = context.getSharedPreferences("learning_progress", Context.MODE_PRIVATE).all.toMap()
        // Explicit UI changes now initially select narration too, retaining the
        // last chosen English variant even after selecting another language.
        NarrationSettingsRepository.saveLanguage(context, NarrationLanguage.BRITISH_ENGLISH)
        NarrationSettingsRepository.saveLanguage(context, NarrationLanguage.FRENCH)
        val settingsBefore = NarrationSettingsRepository.get(context)
        val voicesBefore = NarrationLanguage.entries.associateWith {
            NarrationSettingsRepository.forLanguage(context, it)
        }
        val narrationBefore = context.getSharedPreferences("lesson_narration_settings", Context.MODE_PRIVATE).all.toMap()
        fun languageStorageKey(key: String) = key == "language" || key == "last_english_language" ||
            key == "android_voice" || key.startsWith("kokoro_voice:") || key.startsWith("android_voice:")

        for (tag in listOf("en", "fr", "es")) {
            instrumentation.runOnMainSync { UiLanguageSettings.select(context, tag) }
            instrumentation.waitForIdleSync()
            val localized = UiLanguageSettings.localizedContext(context)
            fun original(relative: String): String = localized.assets.open("content/$tag/$relative")
                .bufferedReader().use { it.readText() }

            val courses = CourseRepository.loadCourses(localized, route)
            val units = UnitRepository.loadUnits(localized, route, courseId)
            val lessons = LessonRepository.loadLessons(localized, route, courseId, unitId)
            assertEquals(spanishCourses.map { it.id to it.number }, courses.map { it.id to it.number })
            assertEquals(spanishUnits.map { it.id to it.number }, units.map { it.id to it.number })
            assertEquals(spanishLessons.map { it.id to it.number }, lessons.map { it.id to it.number })

            val expectedCourses = org.json.JSONArray(original("$route/courses.json"))
            val expectedCourse = (0 until expectedCourses.length()).map { expectedCourses.getJSONObject(it) }
                .first { it.getString("id") == courseId }
            assertEquals(expectedCourse.getString("title"), courses.first { it.id == courseId }.title)

            val expectedCourseDetails = JSONObject(original("$route/$courseId/course_$courseId.json"))
            assertEquals(expectedCourseDetails.getJSONArray("units").getJSONObject(0).getString("title"),
                units.first { it.id == unitId }.title)

            val expectedUnit = JSONObject(original(
                "$route/$courseId/programas_y_ejecucion/unit_programas_y_ejecucion.json"))
            val expectedLesson = (0 until expectedUnit.getJSONArray("lessons").length())
                .map { expectedUnit.getJSONArray("lessons").getJSONObject(it) }
                .first { it.getString("id") == lessonId }
            assertEquals(expectedLesson.getString("title"), lessons.first { it.id == lessonId }.title)

            val expectedLessonJson = JSONObject(original(
                "$route/$courseId/programas_y_ejecucion/lesson_primer_programa_y_salida.json"))
            val lessonJson = LessonRepository.loadLessonJson(localized, route, lessonId)
            assertEquals(expectedLessonJson.toString(), lessonJson.toString())
            assertEquals(lessonId, lessonJson.getString("id"))
            assertEquals(spanishLesson.getJSONArray("pages").length(), lessonJson.getJSONArray("pages").length())

            // Mathematics 1 also has real translated lessons, not only the navigation catalog.
            val mathRoute = AppearanceSettingsRepository.ROUTE_MATH_PHYSICS
            val mathCourse = "math_course_01"
            val mathUnitId = "math_course_01__numeros_y_operaciones"
            val mathLessonId = "${mathUnitId}__magnitudes_y_numeracion"
            val translatedMathUnit = JSONObject(original(
                "$mathRoute/$mathCourse/numeros_y_operaciones/unit_numeros_y_operaciones.json"))
            val localizedMathLessons = LessonRepository.loadLessons(
                localized, mathRoute, mathCourse, mathUnitId)
            assertEquals(translatedMathUnit.getJSONArray("lessons")
                .getJSONObject(0).getString("title"), localizedMathLessons.first().title)
            val translatedMathLesson = JSONObject(original(
                "$mathRoute/$mathCourse/numeros_y_operaciones/lesson_magnitudes_y_numeracion.json"))
            assertEquals(translatedMathLesson.toString(),
                LessonRepository.loadLessonJson(localized, mathRoute, mathLessonId).toString())
            val paragraph = lessonJson.getJSONArray("pages")
                .getJSONObject(0).getJSONArray("blocks").getJSONObject(1).getString("text")
            if (tag != "es") assertNotEquals("The translated body must differ from Spanish", originalParagraph, paragraph)
            else assertEquals(originalParagraph, paragraph)

            assertTrue(LessonRepository.lessonExists(localized, AppearanceSettingsRepository.ROUTE_MATH_PHYSICS, mathId))
            assertEquals("Missing translated lessons must fall back to Spanish without hiding them",
                spanishMath, LessonRepository.loadLessonJson(localized,
                    AppearanceSettingsRepository.ROUTE_MATH_PHYSICS, mathId).toString())
            val expectedLanguage = when (tag) {
                "en" -> NarrationLanguage.BRITISH_ENGLISH
                "fr" -> NarrationLanguage.FRENCH
                else -> NarrationLanguage.SPANISH
            }
            val narration = NarrationSettingsRepository.get(context)
            val expectedVoices = voicesBefore.getValue(expectedLanguage)
            assertEquals(settingsBefore.copy(language = expectedLanguage,
                kokoroVoice = expectedVoices.kokoroVoice, androidVoice = expectedVoices.androidVoice), narration)
            for ((language, remembered) in voicesBefore) {
                val current = NarrationSettingsRepository.forLanguage(context, language)
                assertEquals("Kokoro voice for ${language.tag} must be preserved", remembered.kokoroVoice, current.kokoroVoice)
                assertEquals("Android voice for ${language.tag} must be preserved", remembered.androidVoice, current.androidVoice)
            }
            assertEquals("Preferences unrelated to language must remain intact",
                narrationBefore.filterKeys { !languageStorageKey(it) },
                context.getSharedPreferences("lesson_narration_settings", Context.MODE_PRIVATE).all
                    .filterKeys { !languageStorageKey(it) })
            assertEquals(progressBefore, context.getSharedPreferences("learning_progress", Context.MODE_PRIVATE).all)
        }
    }

    @Test fun changingLanguageDuringLessonRecreatesItsTranslatedTextWithoutResettingProgress() {
        val route = AppearanceSettingsRepository.ROUTE_PROGRAMMING
        val lessonId = "cpp_fundamentals__programas_y_ejecucion__primer_programa_y_salida"
        ProgressRepository.saveCurrentPage(context, route, lessonId, 0)
        NarrationSettingsRepository.saveEnabled(context, false)
        ActivityScenario.launch<LessonActivity>(Intent(context, LessonActivity::class.java)
            .putExtra("routeId", route)
            .putExtra("courseId", "cpp_fundamentals")
            .putExtra("unitId", "cpp_fundamentals__programas_y_ejecucion")
            .putExtra("lessonId", lessonId)
            .putExtra("lessonNumber", 2)).use { scenario ->
            val savedProgress = context.getSharedPreferences("learning_progress", Context.MODE_PRIVATE).all.toMap()
            for ((tag, heading) in listOf(
                "es" to "Un programa pequeño y completo",
                "en" to "A small, complete program",
                "fr" to "Un petit programme complet",
                "es" to "Un programa pequeño y completo"
            )) {
                instrumentation.runOnMainSync { UiLanguageSettings.select(context, tag) }
                val deadline = SystemClock.uptimeMillis() + 12_000
                var correctlyRendered = false
                while (!correctlyRendered && SystemClock.uptimeMillis() < deadline) {
                    instrumentation.waitForIdleSync()
                    scenario.onActivity { activity ->
                        val json = LessonActivity::class.java.getDeclaredField("lessonJson")
                            .apply { isAccessible = true }.get(activity) as JSONObject
                        val headingInJson = json.getJSONArray("pages")
                            .getJSONObject(0).getJSONArray("blocks").getJSONObject(0).getString("text")
                        correctlyRendered = headingInJson == heading &&
                            descendants(activity.findViewById(R.id.lessonContent))
                                .filterIsInstance<TextView>().any { it.text.toString().contains(heading) }
                    }
                    if (!correctlyRendered) SystemClock.sleep(120)
                }
                assertTrue("Switching to $tag must display the actual lesson in that language", correctlyRendered)
                assertEquals(savedProgress,
                    context.getSharedPreferences("learning_progress", Context.MODE_PRIVATE).all)
            }
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
