package com.learngraphicsprogramming.app

import android.app.Activity
import android.app.Instrumentation
import android.content.Context
import android.content.Intent
import android.os.ParcelFileDescriptor
import android.view.View
import android.view.ViewGroup
import android.widget.EditText
import android.widget.TextView
import androidx.recyclerview.widget.RecyclerView
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.math.MathPuzzleParser
import com.learngraphicsprogramming.app.math.MathPuzzleTypes
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/** Cold MainActivity reconstruction uses persisted state, preserving the user's preferences.
 * This verifies the real navigation/UI path without terminating the instrumentation process.
 */
@RunWith(AndroidJUnit4::class)
class MathNavigationDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val route = "math_physics"
    private val course = "math_course_01"
    private val unit = "math_course_01__arithmetic_fundamentals"
    private val sandbox = "${unit}__math_puzzle_sandbox"
    private val snapshots = linkedMapOf<String, Map<String, *>>()

    @Before fun preservePreferencesAndWakeDevice() {
        listOf("navigation_state", "learning_progress", "lesson_text_size_settings").forEach {
            snapshots[it] = context.getSharedPreferences(it, Context.MODE_PRIVATE).all.toMap()
        }
        for (command in listOf("input keyevent KEYCODE_WAKEUP", "wm dismiss-keyguard")) {
            ParcelFileDescriptor.AutoCloseInputStream(instrumentation.uiAutomation.executeShellCommand(command)).use { it.readBytes() }
        }
    }

    @After fun restorePreferences() {
        instrumentation.waitForIdleSync()
        snapshots.forEach { (name, original) ->
            val preferences = context.getSharedPreferences(name, Context.MODE_PRIVATE)
            val editor = preferences.edit()
            (preferences.all.keys - original.keys).forEach(editor::remove)
            original.forEach { (key, value) ->
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

    @Test fun routeAwareStateAndLegacyProgrammingDefaultsPersist() {
        NavigationStateRepository.saveCourses(context, route)
        assertEquals(route, NavigationStateRepository.getState(context).routeId)
        NavigationStateRepository.saveCourse(context, course, route)
        NavigationStateRepository.saveUnit(context, course, unit, route)
        NavigationStateRepository.saveLesson(context, sandbox, route)
        val state = NavigationStateRepository.getState(context)
        assertEquals(NavigationStateRepository.SCREEN_LESSON, state.screen)
        assertEquals(route, state.routeId)
        assertEquals(course, state.courseId)
        assertEquals(unit, state.unitId)
        assertEquals(sandbox, state.lessonId)

        context.getSharedPreferences("navigation_state", Context.MODE_PRIVATE).edit().remove("last_route_id").commit()
        assertEquals("Older math IDs migrate the missing route", route, NavigationStateRepository.getState(context).routeId)
        NavigationStateRepository.saveCourse(context, "cpp_fundamentals")
        assertEquals("programming", NavigationStateRepository.getState(context).routeId)
    }

    @Test fun threeCoursesHaveThreeUnitsWithThreeValidOriginalQaLessons() {
        val courses = CourseRepository.loadCourses(context, route).take(3)
        assertEquals(3, courses.size)
        for (currentCourse in courses) {
            val units = UnitRepository.loadUnits(context, route, currentCourse.id)
            assertEquals(3, units.size)
            for (currentUnit in units) {
                val lessons = LessonRepository.loadLessons(context, route, currentCourse.id, currentUnit.id)
                val chain = lessons.filterNot { it.id == sandbox }
                assertEquals(3, chain.size)
                for (lesson in chain) {
                    assertTrue(LessonRepository.lessonExists(context, route, lesson.id))
                    val content = LessonRepository.loadLessonJson(context, route, lesson.id)
                    assertTrue(content.getBoolean("developmentContent"))
                    val pages = content.getJSONArray("pages")
                    assertTrue(pages.length() > 0)
                    val blocks = pages.getJSONObject(0).getJSONArray("blocks")
                    assertTrue((0 until blocks.length()).any {
                        blocks.getJSONObject(it).optString("text").contains("Contenido provisional de desarrollo")
                    })
                    for (index in 0 until blocks.length()) {
                        val block = blocks.getJSONObject(index)
                        if (MathPuzzleTypes.isExercise(block.optString("type"))) MathPuzzleParser.parse(block)
                    }
                }
                assertEquals(chain[1].id, NavigationRepository.getNextLessonInUnit(context, route, chain[0].id)?.lessonId)
                assertEquals(chain[2].id, NavigationRepository.getNextLessonInUnit(context, route, chain[1].id)?.lessonId)
                assertNull("Last lesson cannot cross to another unit/course", NavigationRepository.getNextLessonInUnit(context, route, chain[2].id))
            }
        }
        assertTrue("Sandbox remains manually available", LessonRepository.lessonExists(context, route, sandbox))
        assertNull(NavigationRepository.getNextLessonInUnit(context, route, sandbox))
    }

    @Test fun firstThreeLessonsContinueThenReturnToTheCorrectUnit() {
        val lessonIds = listOf("numbers_operations", "fractions_decimals", "powers_roots").map { "${unit}__$it" }
        lessonIds.forEach { ProgressRepository.resetLesson(context, route, it) }
        val monitor = instrumentation.addMonitor(UnitActivity::class.java.name, null, false)
        var destination: Activity? = null
        try {
            val intent = Intent(context, LessonActivity::class.java).putExtra("routeId", route)
                .putExtra("courseId", course).putExtra("unitId", unit).putExtra("lessonId", lessonIds[0]).putExtra("lessonNumber", 1)
            ActivityScenario.launch<LessonActivity>(intent).use { scenario ->
                scenario.onActivity { activity ->
                    activity.findViewById<View>(R.id.buttonContinue).performClick()
                    assertEquals("Lección 2", activity.findViewById<TextView>(R.id.textTopLesson).text.toString())
                    assertEquals(lessonIds[1], NavigationStateRepository.getState(context).lessonId)
                }
                // Recreating after automatic continuation must keep the new lesson, not its old intent.
                scenario.recreate()
                scenario.onActivity { activity ->
                    assertEquals("Lección 2", activity.findViewById<TextView>(R.id.textTopLesson).text.toString())
                    answer(activity, "1/2")
                    activity.findViewById<View>(R.id.buttonContinue).performClick()
                    assertEquals("Lección 3", activity.findViewById<TextView>(R.id.textTopLesson).text.toString())
                    assertEquals(lessonIds[2], NavigationStateRepository.getState(context).lessonId)
                    answer(activity, "9")
                    activity.findViewById<View>(R.id.buttonContinue).performClick()
                }
                destination = instrumentation.waitForMonitorWithTimeout(monitor, 5000)
                assertNotNull(destination)
                assertEquals(route, destination!!.intent.getStringExtra("routeId"))
                assertEquals(course, destination!!.intent.getStringExtra("courseId"))
                assertEquals(unit, destination!!.intent.getStringExtra("unitId"))
                assertEquals(NavigationStateRepository.SCREEN_UNIT, NavigationStateRepository.getState(context).screen)
            }
        } finally {
            destination?.let { instrumentation.runOnMainSync { it.finish() } }
            instrumentation.removeMonitor(monitor)
            instrumentation.waitForIdleSync()
        }
    }

    @Test fun coldMainRestoresMathLessonAndItsEighthPage() {
        ProgressRepository.saveCurrentPage(context, route, sandbox, 7)
        NavigationStateRepository.saveLesson(context, sandbox, route)
        val monitors = listOf(CoursesActivity::class.java, CourseActivity::class.java, UnitActivity::class.java, LessonActivity::class.java)
            .map { instrumentation.addMonitor(it.name, null, false) }
        try {
            ActivityScenario.launch<MainActivity>(Intent(context, MainActivity::class.java)).use {
                val destination = instrumentation.waitForMonitorWithTimeout(monitors.last(), 5000) as? LessonActivity
                assertNotNull("Cold route restore must open the remembered math lesson", destination)
                instrumentation.waitForIdleSync()
                instrumentation.runOnMainSync {
                    assertEquals(route, destination!!.intent.getStringExtra("routeId"))
                    assertEquals(course, destination.intent.getStringExtra("courseId"))
                    assertEquals(unit, destination.intent.getStringExtra("unitId"))
                    assertEquals(sandbox, destination.intent.getStringExtra("lessonId"))
                    assertEquals("8 de 16", destination.findViewById<TextView>(R.id.textProgress).text.toString())
                    val saved = NavigationStateRepository.getState(context)
                    assertEquals(NavigationStateRepository.SCREEN_LESSON, saved.screen)
                    assertEquals(route, saved.routeId)
                    monitors.asReversed().mapNotNull { monitor -> monitor.lastActivity }.forEach { activity -> activity.finish() }
                }
                instrumentation.waitForIdleSync()
            }
        } finally {
            finishMonitored(monitors)
        }
    }

    @Test fun coldMainRestoresProgrammingLessonItsPageAndParentStack() {
        val programmingRoute = AppearanceSettingsRepository.ROUTE_PROGRAMMING
        val programmingCourse = CourseRepository.loadCourses(context, programmingRoute).first()
        val programmingUnit = UnitRepository.loadUnits(context, programmingRoute, programmingCourse.id).first()
        val (programmingLesson, totalPages) = LessonRepository.loadLessons(context, programmingRoute, programmingCourse.id, programmingUnit.id)
            .filter { LessonRepository.lessonExists(context, programmingRoute, it.id) }
            .map { it to LessonRepository.loadLessonJson(context, programmingRoute, it.id).getJSONArray("pages").length() }
            .first { (_, pageCount) -> pageCount > 1 }
        val rememberedPage = 1
        ProgressRepository.saveCurrentPage(context, programmingRoute, programmingLesson.id, rememberedPage)
        NavigationStateRepository.saveLesson(context, programmingLesson.id, programmingRoute)
        // Older Programming state has no route key and must still rebuild the same stack.
        assertTrue(context.getSharedPreferences("navigation_state", Context.MODE_PRIVATE).edit().remove("last_route_id").commit())
        val monitors = listOf(CoursesActivity::class.java, CourseActivity::class.java, UnitActivity::class.java, LessonActivity::class.java)
            .map { instrumentation.addMonitor(it.name, null, false) }
        try {
            ActivityScenario.launch<MainActivity>(Intent(context, MainActivity::class.java)).use {
                val destination = instrumentation.waitForMonitorWithTimeout(monitors.last(), 5000) as? LessonActivity
                assertNotNull("Cold route restore must open the remembered Programming lesson", destination)
                instrumentation.waitForIdleSync()
                instrumentation.runOnMainSync {
                    assertEquals(programmingRoute, destination!!.intent.getStringExtra("routeId"))
                    assertEquals(programmingCourse.id, destination.intent.getStringExtra("courseId"))
                    assertEquals(programmingUnit.id, destination.intent.getStringExtra("unitId"))
                    assertEquals(programmingLesson.id, destination.intent.getStringExtra("lessonId"))
                    assertEquals("${rememberedPage + 1} de $totalPages", destination.findViewById<TextView>(R.id.textProgress).text.toString())
                    val saved = NavigationStateRepository.getState(context)
                    assertEquals(NavigationStateRepository.SCREEN_LESSON, saved.screen)
                    assertEquals(programmingRoute, saved.routeId)
                    assertEquals(programmingCourse.id, saved.courseId)
                    assertEquals(programmingUnit.id, saved.unitId)
                    assertEquals(programmingLesson.id, saved.lessonId)
                    destination.finish()
                }
                instrumentation.waitForIdleSync()
                val parentUnit = instrumentation.waitForMonitorWithTimeout(monitors[2], 5000) as? UnitActivity
                assertNotNull("Restored lesson must retain its Unit parent", parentUnit)
                instrumentation.runOnMainSync {
                    assertEquals(programmingRoute, parentUnit!!.intent.getStringExtra("routeId"))
                    assertEquals(programmingCourse.id, parentUnit.intent.getStringExtra("courseId"))
                    assertEquals(programmingUnit.id, parentUnit.intent.getStringExtra("unitId"))
                    parentUnit.finish()
                }
                instrumentation.waitForIdleSync()
                val parentCourse = instrumentation.waitForMonitorWithTimeout(monitors[1], 5000) as? CourseActivity
                assertNotNull("Restored Unit must retain its Course parent", parentCourse)
                instrumentation.runOnMainSync {
                    assertEquals(programmingRoute, parentCourse!!.intent.getStringExtra("routeId"))
                    assertEquals(programmingCourse.id, parentCourse.intent.getStringExtra("courseId"))
                    parentCourse.finish()
                }
                instrumentation.waitForIdleSync()
                val parentCourses = instrumentation.waitForMonitorWithTimeout(monitors[0], 5000) as? CoursesActivity
                assertNotNull("Restored Course must retain its route's Courses parent", parentCourses)
                instrumentation.runOnMainSync {
                    assertEquals(programmingRoute, parentCourses!!.intent.getStringExtra("routeId"))
                    parentCourses.finish()
                }
                instrumentation.waitForIdleSync()
            }
        } finally {
            finishMonitored(monitors)
        }
    }

    @Test fun coursesTwoAndThreeAreClickableThroughTheExistingCourseUi() {
        for (index in listOf(1, 2)) {
            val monitor = instrumentation.addMonitor(CourseActivity::class.java.name, null, false)
            var destination: Activity? = null
            try {
                ActivityScenario.launch<CoursesActivity>(Intent(context, CoursesActivity::class.java).putExtra("routeId", route)).use { scenario ->
                    scenario.onActivity { it.findViewById<RecyclerView>(R.id.recyclerCourses).scrollToPosition(index) }
                    instrumentation.waitForIdleSync()
                    scenario.onActivity { activity ->
                        val recycler = activity.findViewById<RecyclerView>(R.id.recyclerCourses)
                        val row = recycler.findViewHolderForAdapterPosition(index)!!.itemView
                        row.findViewById<View>(R.id.courseContainer).performClick()
                    }
                    destination = instrumentation.waitForMonitorWithTimeout(monitor, 5000)
                    assertNotNull("Available QA course must open", destination)
                    assertEquals("math_course_0${index + 1}", destination!!.intent.getStringExtra("courseId"))
                    assertEquals(route, destination!!.intent.getStringExtra("routeId"))
                }
            } finally {
                destination?.let { instrumentation.runOnMainSync { it.finish() } }
                instrumentation.removeMonitor(monitor)
                instrumentation.waitForIdleSync()
            }
        }
    }

    private fun answer(activity: LessonActivity, value: String) {
        val content = activity.findViewById<View>(R.id.lessonContent)
        descendants(content).filterIsInstance<EditText>().single().setText(value)
        descendants(content).filterIsInstance<TextView>().single { it.text.toString() == "Comprobar" }.performClick()
        assertTrue(descendants(content).filterIsInstance<TextView>().any { it.text.toString() == "Correcto ✓" })
    }

    private fun finishMonitored(monitors: List<Instrumentation.ActivityMonitor>) {
        instrumentation.runOnMainSync { monitors.asReversed().mapNotNull { it.lastActivity }.forEach { it.finish() } }
        monitors.forEach(instrumentation::removeMonitor)
        instrumentation.waitForIdleSync()
    }

    private fun descendants(root: View): List<View> = listOf(root) + if (root is ViewGroup)
        (0 until root.childCount).flatMap { descendants(root.getChildAt(it)) } else emptyList()
}
