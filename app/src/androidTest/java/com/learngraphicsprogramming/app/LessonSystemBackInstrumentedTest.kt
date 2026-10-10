package com.learngraphicsprogramming.app

import android.content.Context
import android.content.Intent
import android.view.View
import android.view.ViewGroup
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
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
    private val context = InstrumentationRegistry.getInstrumentation().targetContext
    private val programmingRoute = "programming"
    private val programmingCourse = "cpp_fundamentals"
    private val programmingUnit = "cpp_fundamentals__intro_programming"
    private val programmingLesson = "cpp_fundamentals__intro_programming__what_is_programming"

    private val preferenceSnapshots = linkedMapOf<String, Map<String, *>>()

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
                val control = findDescendants(root).firstOrNull {
                    it.contentDescription == activity.getString(R.string.exercise_fullscreen) &&
                        it.isShown
                }
                assertNotNull("Expected the compiler fullscreen control on page eight", control)
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

    private fun findDescendants(root: View): List<View> = buildList {
        add(root)
        if (root is ViewGroup) {
            for (index in 0 until root.childCount) {
                addAll(findDescendants(root.getChildAt(index)))
            }
        }
    }
}
