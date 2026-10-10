package com.learngraphicsprogramming.app

import android.content.Context
import android.content.ContextWrapper
import android.content.SharedPreferences
import android.widget.Button
import androidx.test.core.app.ActivityScenario
import androidx.test.espresso.Espresso.onView
import androidx.test.espresso.action.ViewActions.click
import androidx.test.espresso.assertion.ViewAssertions.matches
import androidx.test.espresso.matcher.RootMatchers.isDialog
import androidx.test.espresso.matcher.ViewMatchers.isDisplayed
import androidx.test.espresso.matcher.ViewMatchers.withText
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import java.util.UUID

/** Positive resets only receive a private UUID preferences namespace, never the user's data. */
@RunWith(AndroidJUnit4::class)
class RouteProgressResetInstrumentedTest {
    private lateinit var context: IsolatedPreferencesContext
    private lateinit var programming: RouteFixture
    private lateinit var math: RouteFixture
    private lateinit var programmingKeys: Set<String>
    private lateinit var mathKeys: Set<String>
    private lateinit var settingsBefore: Map<String, Map<String, *>>

    @Before fun preparePrivateProgress() {
        context = IsolatedPreferencesContext(InstrumentationRegistry.getInstrumentation().targetContext)
        programming = fixture(
            AppearanceSettingsRepository.ROUTE_PROGRAMMING,
            "cpp_fundamentals", "cpp_fundamentals__intro_programming", "course_44"
        )
        math = fixture(
            AppearanceSettingsRepository.ROUTE_MATH_PHYSICS,
            "math_course_01", "math_course_01__porcentajes_escalas_modelos", "math_course_72"
        )
        programmingKeys = seedProgress(programming)
        mathKeys = seedProgress(math)
        context.getSharedPreferences("learning_progress", Context.MODE_PRIVATE).edit()
            .putString("unrelated_state", "preserve")
            .putBoolean("course_course_440_completed", true)
            .putInt("lesson_course_440__old_unit__old_lesson_page", 8)
            .putBoolean("unit_math_course_720__old_unit_completed", true)
            .putString("exercise_math_course_720__old_unit__old_lesson_page_0_block_0", "preserve")
            .putString("course_course_44_notes", "preserve")
            .commit()
        settingsBefore = seedPreservedSettings()
    }

    @After fun discardOnlyPrivateTestPreferences() {
        if (::context.isInitialized) context.close()
    }

    @Test fun mathResetClearsItsProgressAndPreservesProgrammingAndSettings() {
        assertRouteHasProgress(math)
        assertRouteHasProgress(programming)
        NavigationStateRepository.saveLesson(context, programming.lessonId, programming.routeId)
        val navigationBefore = context.getSharedPreferences("navigation_state", Context.MODE_PRIVATE).all.toMap()
        val before = context.getSharedPreferences("learning_progress", Context.MODE_PRIVATE).all.toMap()
        val otherProgress = routeSnapshot(programming)

        ProgressRepository.resetMathPhysicsRoute(context)

        assertEquals(before.filterKeys { it !in mathKeys }, progressValues())
        assertRouteReset(math)
        assertEquals(otherProgress, routeSnapshot(programming))
        assertRouteHasProgress(programming)
        assertEquals(navigationBefore, context.getSharedPreferences("navigation_state", Context.MODE_PRIVATE).all)
        assertSettingsPreserved()
    }

    @Test fun programmingResetClearsItsProgressAndPreservesMathAndSettings() {
        assertRouteHasProgress(programming)
        assertRouteHasProgress(math)
        NavigationStateRepository.saveLesson(context, math.lessonId, math.routeId)
        val navigationBefore = context.getSharedPreferences("navigation_state", Context.MODE_PRIVATE).all.toMap()
        val before = context.getSharedPreferences("learning_progress", Context.MODE_PRIVATE).all.toMap()
        val otherProgress = routeSnapshot(math)

        ProgressRepository.resetProgrammingRoute(context)

        assertEquals(before.filterKeys { it !in programmingKeys }, progressValues())
        assertRouteReset(programming)
        assertEquals(otherProgress, routeSnapshot(math))
        assertRouteHasProgress(math)
        assertEquals(navigationBefore, context.getSharedPreferences("navigation_state", Context.MODE_PRIVATE).all)
        assertSettingsPreserved()
    }

    @Test fun legacyAnswersWithoutLessonJsonAndLegacyNavigationAreResetForBothRoutes() {
        for (route in listOf(programming, math)) {
            // The last catalog course exists, but its unit/lesson JSON has never been shipped.
            assertTrue(CourseRepository.loadCourses(context, route.routeId).any { it.id == route.unavailableCourse })
            assertTrue(runCatching {
                LessonRepository.loadLessonJson(context, route.routeId, route.legacyLessonId)
            }.isFailure)
            assertNotNull(ProgressRepository.getExerciseState(context, route.legacyLessonId, 17, 4))
            assertTrue(ProgressRepository.isCourseCompleted(context, route.unavailableCourse))

            context.getSharedPreferences("navigation_state", Context.MODE_PRIVATE).edit()
                .putString("last_screen", NavigationStateRepository.SCREEN_LESSON)
                .putString("last_lesson_id", route.legacyLessonId)
                .remove("last_route_id").remove("last_course_id").remove("last_unit_id").commit()
            assertEquals(route.routeId, NavigationStateRepository.getState(context).routeId)

            ProgressRepository.resetRoute(context, route.routeId)

            assertRouteReset(route)
            assertFalse(ProgressRepository.isCourseCompleted(context, route.unavailableCourse))
            assertFalse(ProgressRepository.isUnitCompleted(context, route.legacyUnitId))
            assertFalse(ProgressRepository.isLessonStarted(context, route.legacyLessonId))
            assertFalse(ProgressRepository.isLessonCompleted(context, route.legacyLessonId))
            assertEquals(0, ProgressRepository.getCurrentPage(context, route.legacyLessonId))
            assertNull(ProgressRepository.getExerciseState(context, route.legacyLessonId, 17, 4))
            val navigation = NavigationStateRepository.getState(context)
            assertEquals(NavigationStateRepository.SCREEN_COURSES, navigation.screen)
            assertEquals(route.routeId, navigation.routeId)
            assertNull(navigation.courseId)
            assertNull(navigation.unitId)
            assertNull(navigation.lessonId)
        }
        assertEquals("preserve", progressValues()["unrelated_state"])
        assertEquals(true, progressValues()["course_course_440_completed"])
        assertEquals(8, progressValues()["lesson_course_440__old_unit__old_lesson_page"])
        assertEquals(true, progressValues()["unit_math_course_720__old_unit_completed"])
        assertEquals("preserve", progressValues()["exercise_math_course_720__old_unit__old_lesson_page_0_block_0"])
        assertEquals("preserve", progressValues()["course_course_44_notes"])
        assertSettingsPreserved()
    }

    @Test fun routePickerNavigationIsPreservedAndRepeatingAResetIsIdempotent() {
        NavigationStateRepository.saveRoutes(context)
        val navigationBefore = context.getSharedPreferences("navigation_state", Context.MODE_PRIVATE).all.toMap()
        ProgressRepository.resetMathPhysicsRoute(context)
        val afterFirstReset = progressValues()

        ProgressRepository.resetMathPhysicsRoute(context)

        assertEquals(afterFirstReset, progressValues())
        assertEquals(navigationBefore, context.getSharedPreferences("navigation_state", Context.MODE_PRIVATE).all)
        assertRouteHasProgress(programming)
        assertSettingsPreserved()
    }

    @Test fun unknownRouteCannotChangeProgressNavigationOrSettings() {
        NavigationStateRepository.saveLesson(context, math.lessonId, math.routeId)
        val progressBefore = progressValues()
        val navigationBefore = context.getSharedPreferences("navigation_state", Context.MODE_PRIVATE).all.toMap()

        assertTrue("An unavailable catalog must fail before editing any preferences", runCatching {
            ProgressRepository.resetRoute(context, "unknown-test-route-${UUID.randomUUID()}")
        }.isFailure)

        assertEquals(progressBefore, progressValues())
        assertEquals(navigationBefore, context.getSharedPreferences("navigation_state", Context.MODE_PRIVATE).all)
        assertSettingsPreserved()
    }

    @Test fun bothOptionsButtonsShowTheirOwnConfirmationAndCancelWithoutResettingUserData() {
        val app = InstrumentationRegistry.getInstrumentation().targetContext
        val actualProgressBefore = app.getSharedPreferences("learning_progress", Context.MODE_PRIVATE).all.toMap()
        val actualNavigationBefore = app.getSharedPreferences("navigation_state", Context.MODE_PRIVATE).all.toMap()
        ActivityScenario.launch(OptionsActivity::class.java).use { scenario ->
            for ((buttonId, titleId, messageId) in listOf(
                Triple(R.id.buttonResetProgrammingRoute, R.string.settings_programming_progress_reset,
                    R.string.settings_programming_progress_confirmation),
                Triple(R.id.buttonResetMathPhysicsRoute, R.string.settings_math_physics_progress_reset,
                    R.string.settings_math_physics_progress_confirmation)
            )) {
                scenario.onActivity { activity ->
                    assertEquals(activity.getString(titleId), activity.findViewById<Button>(buttonId).text.toString())
                    activity.findViewById<Button>(buttonId).performClick()
                }
                onView(withText(titleId)).inRoot(isDialog()).check(matches(isDisplayed()))
                onView(withText(messageId)).inRoot(isDialog()).check(matches(isDisplayed()))
                // No test ever clicks the positive button in an Activity using actual preferences.
                onView(withText(R.string.settings_cancel)).inRoot(isDialog()).perform(click())
            }
        }
        assertEquals(actualProgressBefore, app.getSharedPreferences("learning_progress", Context.MODE_PRIVATE).all)
        assertEquals(actualNavigationBefore, app.getSharedPreferences("navigation_state", Context.MODE_PRIVATE).all)
    }

    private fun fixture(routeId: String, courseId: String, unitId: String, unavailableCourse: String): RouteFixture {
        val lessons = LessonRepository.loadLessons(context, routeId, courseId, unitId)
        assertTrue("The fixture unit must contain real lessons", lessons.isNotEmpty())
        val lessonId = lessons.first().id
        val lesson = LessonRepository.loadLessonJson(context, routeId, lessonId)
        val pages = lesson.getJSONArray("pages")
        val exercises = mutableListOf<Pair<Int, Int>>()
        for (page in 0 until pages.length()) {
            val blocks = pages.getJSONObject(page).optJSONArray("blocks") ?: continue
            for (block in 0 until blocks.length()) {
                if (isExercise(blocks.getJSONObject(block).optString("type"))) exercises += page to block
            }
        }
        assertTrue("The fixture lesson must contain real saved exercises", exercises.isNotEmpty())
        return RouteFixture(routeId, courseId, unitId, lessons.map { it.id }, lessonId, exercises, unavailableCourse)
    }

    private fun seedProgress(route: RouteFixture): Set<String> {
        val keys = mutableSetOf<String>()
        val editor = context.getSharedPreferences("learning_progress", Context.MODE_PRIVATE).edit()
        fun boolean(key: String) { keys += key; editor.putBoolean(key, true) }
        fun page(key: String, value: Int) { keys += key; editor.putInt(key, value) }
        fun answer(key: String) { keys += key; editor.putString(key, JSONObject().put("correct", true)
            .put("answer", "historical saved response").toString()) }
        for (lessonId in route.lessonIds) {
            val lesson = LessonRepository.loadLessonJson(context, route.routeId, lessonId)
            page("lesson_${lessonId}_page", lesson.getJSONArray("pages").length() - 1)
            boolean("lesson_${lessonId}_started")
            boolean("lesson_${lessonId}_completed")
        }
        route.exercises.forEach { (page, block) -> answer("exercise_${route.lessonId}_page_${page}_block_${block}") }
        boolean("unit_${route.unitId}_completed")
        boolean("course_${route.courseId}_completed")
        page("lesson_${route.legacyLessonId}_page", 17)
        boolean("lesson_${route.legacyLessonId}_started")
        boolean("lesson_${route.legacyLessonId}_completed")
        answer("exercise_${route.legacyLessonId}_page_17_block_4")
        boolean("unit_${route.legacyUnitId}_completed")
        boolean("course_${route.unavailableCourse}_completed")
        assertTrue(editor.commit())
        return keys
    }

    private fun seedPreservedSettings(): Map<String, Map<String, *>> {
        val settings = mapOf(
            "appearance_settings" to mapOf("route_accent_red_programming" to 91, "route_accent_blue_math_physics" to 131),
            "learning_flow_settings" to mapOf("global_between_lessons" to false, "course_override_math_course_01_enabled" to true),
            "code_wrap_settings" to mapOf("global_wrap" to false, "course_wrap_cpp_fundamentals" to true),
            "lesson_text_size_settings" to mapOf("global_scale" to 1.2f, "course_scale_math_course_01" to 1.3f),
            "ui_language_settings" to mapOf("selection" to "fr", "initialized" to true),
            "lesson_narration_settings" to mapOf("enabled" to true, "engine" to "kokoro", "kokoro_voice" to "ff_siwis", "language" to "fr"),
            "attempt_reserve_seconds_test" to mapOf("reserve_programming" to 3.5f, "reserve_math_physics" to 6.5f,
                "updated_at_programming" to 1000L, "updated_at_math_physics" to 2000L)
        )
        for ((name, values) in settings) {
            val editor = context.getSharedPreferences(name, Context.MODE_PRIVATE).edit()
            for ((key, value) in values) when (value) {
                is String -> editor.putString(key, value)
                is Boolean -> editor.putBoolean(key, value)
                is Int -> editor.putInt(key, value)
                is Float -> editor.putFloat(key, value)
                is Long -> editor.putLong(key, value)
            }
            assertTrue(editor.commit())
        }
        return settings.keys.associateWith { context.getSharedPreferences(it, Context.MODE_PRIVATE).all.toMap() }
    }

    private fun assertSettingsPreserved() {
        settingsBefore.forEach { (name, values) ->
            assertEquals("Reset must preserve $name", values, context.getSharedPreferences(name, Context.MODE_PRIVATE).all)
        }
    }

    private fun assertRouteHasProgress(route: RouteFixture) {
        assertTrue(ProgressRepository.getCurrentPage(context, route.lessonId) > 0)
        assertTrue(ProgressRepository.isLessonStarted(context, route.lessonId))
        assertTrue(ProgressRepository.isLessonCompleted(context, route.lessonId))
        assertTrue(ProgressRepository.isUnitCompleted(context, route.unitId))
        assertTrue(ProgressRepository.isCourseCompleted(context, route.courseId))
        routeSnapshot(route).forEach { assertTrue("The fixture must have nonzero progress", it.percentage > 0) }
        route.exercises.forEach { (page, block) ->
            assertNotNull(ProgressRepository.getExerciseState(context, route.lessonId, page, block))
        }
    }

    private fun assertRouteReset(route: RouteFixture) {
        for (lessonId in route.lessonIds) {
            assertEquals(0, ProgressRepository.getCurrentPage(context, lessonId))
            assertFalse(ProgressRepository.isLessonStarted(context, lessonId))
            assertFalse(ProgressRepository.isLessonCompleted(context, lessonId))
        }
        assertFalse(ProgressRepository.isUnitCompleted(context, route.unitId))
        assertFalse(ProgressRepository.isCourseCompleted(context, route.courseId))
        routeSnapshot(route).forEach {
            assertTrue("The real lesson/unit/course totals must remain", it.total > 0)
            assertEquals(0, it.completed)
            assertEquals(0, it.percentage)
        }
        route.exercises.forEach { (page, block) ->
            assertNull(ProgressRepository.getExerciseState(context, route.lessonId, page, block))
        }
    }

    private fun routeSnapshot(route: RouteFixture): List<ProgressSnapshot> = listOf(
        ProgressRepository.getLessonPageProgress(context, route.routeId, route.lessonId),
        ProgressRepository.getUnitProgress(context, route.routeId, route.courseId, route.unitId),
        ProgressRepository.getUnitLearningProgress(context, route.routeId, route.courseId, route.unitId),
        ProgressRepository.getCourseProgress(context, route.routeId, route.courseId)
    )

    private fun progressValues(): Map<String, *> =
        context.getSharedPreferences("learning_progress", Context.MODE_PRIVATE).all.toMap()

    private fun isExercise(type: String) = type in setOf(
        "multiple_choice", "fill_code", "output_prediction", "order_code", "drag_fill_code", "inline_fill_code"
    ) || com.learngraphicsprogramming.app.math.MathPuzzleTypes.isExercise(type)

    private data class RouteFixture(
        val routeId: String, val courseId: String, val unitId: String, val lessonIds: List<String>,
        val lessonId: String, val exercises: List<Pair<Int, Int>>, val unavailableCourse: String
    ) {
        val legacyUnitId = "${unavailableCourse}__old_unit"
        val legacyLessonId = "${legacyUnitId}__old_lesson"
    }

    private class IsolatedPreferencesContext(base: Context) : ContextWrapper(base), AutoCloseable {
        private val prefix = "route-progress-test-${UUID.randomUUID()}-"
        private val names = linkedSetOf<String>()
        override fun getApplicationContext(): Context = this
        override fun getSharedPreferences(name: String, mode: Int): SharedPreferences {
            val isolatedName = prefix + name
            names += isolatedName
            return baseContext.getSharedPreferences(isolatedName, mode)
        }
        override fun close() {
            // Only the UUID preferences files created by this fixture are emptied.
            names.forEach { baseContext.getSharedPreferences(it, Context.MODE_PRIVATE).edit().clear().commit() }
        }
    }
}
