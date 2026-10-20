package com.learngraphicsprogramming.app

import android.content.Context
import android.content.ContextWrapper
import android.content.SharedPreferences
import android.content.res.AssetManager
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

/**
 * Device-only integration through the real asset and repository paths. The test APK contains a
 * Python-generated reorder; no intermediate app version registers the original positional lesson.
 * Every preferences file is UUID-scoped, including calls through applicationContext.
 */
@RunWith(AndroidJUnit4::class)
class StableLessonRepositoryDeviceTest {
    private lateinit var context: IsolatedAssetContext
    private lateinit var progress: SharedPreferences
    private lateinit var drafts: SharedPreferences
    private lateinit var originalAnswers: Map<String, String>
    private lateinit var originalDrafts: Map<String, *>

    @Before fun seedPreviousVersionBeforeLoadingReorderedLesson() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        context = IsolatedAssetContext(instrumentation.targetContext, instrumentation.context.assets)
        progress = context.getSharedPreferences("learning_progress", Context.MODE_PRIVATE)
        drafts = context.getSharedPreferences("cpp_lesson_drafts", Context.MODE_PRIVATE)

        // Read raw historical JSON only to document the positional keys written by the old app.
        // In particular, do not pass this original through LessonRepository or register a layout.
        val original = JSONObject(context.assets.open("$ASSET_DIRECTORY/legacy_original.json")
            .bufferedReader().use { it.readText() })
        assertFalse(original.has("stateLayout"))
        val pages = original.getJSONArray("pages")
        assertEquals("correct_prediction", pages.getJSONObject(1).getJSONArray("blocks")
            .getJSONObject(2).getString("id"))
        assertEquals("partial_inline", pages.getJSONObject(3).getJSONArray("blocks")
            .getJSONObject(1).getString("id"))
        assertEquals("cpp_compiler", pages.getJSONObject(4).getJSONArray("blocks")
            .getJSONObject(1).getString("type"))

        originalAnswers = linkedMapOf(
            answerKey(1, 1) to """{"selectedIndex":1,"correct":true}""",
            answerKey(1, 2) to """{"answer":"7","correct":true}""",
            answerKey(3, 1) to """{"answers":["2",""],"correct":false}""",
            answerKey(3, 2) to """{"order":[1,0],"correct":false}"""
        )
        val editor = progress.edit()
            .putBoolean("lesson_${LESSON}_started", true)
            .putInt("lesson_${LESSON}_page", 2)
            .putBoolean("lesson_${LESSON}_completed", false)
            .putBoolean("unit_${UNIT}_completed", false)
            .putBoolean("course_${COURSE}_completed", false)
            .putString(UNRELATED_ANSWER, "preserve unrelated progress")
        originalAnswers.forEach { (key, value) -> editor.putString(key, value) }
        assertTrue(editor.commit())
        val draftSource = "// Mi borrador sin compilar\nint answer = 42;\n"
        assertTrue(drafts.edit()
            .putString("$DRAFT_KEY/source", draftSource)
            .putString("$DRAFT_KEY/input", "Rita 4\n")
            .putString("$DRAFT_KEY/project", """{"main.cpp":"// proyecto del alumno"}""")
            .putString("$DRAFT_KEY/sourceIndentation_v2", CppIndentationUnits.snapshot(draftSource))
            .putString("$DRAFT_KEY/compiler", "clang")
            .commit())
        originalDrafts = drafts.all.toMap()
        assertFalse(progress.contains("lesson_${LESSON}_state_lineage_v1"))
    }

    @After fun discardOnlyIsolatedPreferences() {
        if (::context.isInitialized) context.close()
    }

    @Test fun directLegacyUpgradeLoadsRealAssetsAndRetainsAnswersDraftsAndNumericProgress() {
        loadReorderedLesson()

        assertEquals(listOf(COURSE), CourseRepository.loadCourses(context, ROUTE).map { it.id })
        assertEquals(listOf(UNIT), UnitRepository.loadUnits(context, ROUTE, COURSE).map { it.id })
        assertEquals(listOf(LESSON), LessonRepository.loadLessons(context, ROUTE, COURSE, UNIT).map { it.id })
        assertEquals(4, ProgressRepository.getCurrentPage(context, LESSON))
        assertTrue(ProgressRepository.isLessonStarted(context, LESSON))
        assertEquals(1, saved(1, 1).getInt("selectedIndex"))
        assertTrue(saved(1, 1).getBoolean("correct"))
        assertEquals("7", saved(3, 1).getString("answer"))
        assertTrue(saved(3, 1).getBoolean("correct"))
        assertEquals("[\"2\",\"\"]", saved(1, 2).getJSONArray("answers").toString())
        assertFalse(saved(1, 2).getBoolean("correct"))
        assertEquals("[1,0]", saved(3, 2).getJSONArray("order").toString())
        assertFalse(saved(3, 2).getBoolean("correct"))
        originalAnswers.forEach { (key, value) -> assertEquals(value, progress.getString(key, null)) }
        assertEquals(DRAFT_KEY, LessonStateCoordinates.draftKey(context, ROUTE, LESSON, 1, 3))
        assertEquals("// Mi borrador sin compilar\nint answer = 42;\n",
            CppLessonSourceDraft.read(drafts,
                LessonStateCoordinates.draftKey(context, ROUTE, LESSON, 1, 3), "initial code"))
        assertEquals(originalDrafts, drafts.all)

        // Two visited theory groups plus the original solved group: mixing its two solved puzzles
        // with pending puzzles on different visible pages must preserve 3/5 (60%), not produce 2/5.
        assertProgress(3)
        assertCompletion(false)
        val firstLoad = progress.all.toMap()
        loadReorderedLesson()
        assertProgress(3)
        assertEquals(firstLoad, progress.all)
        assertEquals(originalDrafts, drafts.all)
    }

    @Test fun savingAndResettingMovedExercisesUsesOriginalKeysAndRecomputesAllCompletionFlags() {
        loadReorderedLesson()
        completeRemainingWork()

        val inlineAnswer = progress.getString(answerKey(3, 1), null)
        assertNotNull(inlineAnswer)
        // Displayed (3,1) is the prediction, while legacy (3,1) is the independent inline answer.
        ProgressRepository.resetExerciseState(context, ROUTE, LESSON, 3, 1)
        assertFalse(progress.contains(answerKey(1, 2)))
        assertEquals(inlineAnswer, progress.getString(answerKey(3, 1), null))
        assertNull(ProgressRepository.getExerciseState(context, LESSON, 3, 1))
        assertTrue(saved(1, 2).getBoolean("correct"))
        assertProgress(4)
        assertCompletion(false)

        ProgressRepository.saveExerciseState(context, ROUTE, LESSON, 3, 1,
            JSONObject(originalAnswers.getValue(answerKey(1, 2))))
        assertEquals("7", JSONObject(progress.getString(answerKey(1, 2), null)!!).getString("answer"))
        assertEquals(inlineAnswer, progress.getString(answerKey(3, 1), null))
        assertProgress(5)
        assertCompletion(true)
        assertEquals(originalDrafts, drafts.all)
    }

    @Test fun resetLessonClearsItsAnswersAndCoverageWithoutReseedingHistoryOrTouchingDrafts() {
        loadReorderedLesson()
        completeRemainingWork()
        val lineage = progress.getString("lesson_${LESSON}_state_lineage_v1", null)
        assertNotNull(lineage)

        ProgressRepository.resetLesson(context, ROUTE, LESSON)

        assertFalse(progress.all.keys.any { it.startsWith("exercise_${LESSON}_") })
        assertFalse(progress.contains("lesson_${LESSON}_page"))
        assertFalse(ProgressRepository.isLessonStarted(context, LESSON))
        assertEquals(lineage, progress.getString("lesson_${LESSON}_state_lineage_v1", null))
        assertEquals(emptySet<String>(), progress.getStringSet("lesson_${LESSON}_read_pages_v1", null))
        assertEquals(emptySet<String>(), progress.getStringSet("lesson_${LESSON}_read_blocks_v1", null))
        assertEquals("preserve unrelated progress", progress.getString(UNRELATED_ANSWER, null))
        assertEquals(originalDrafts, drafts.all)
        assertCompletion(false)

        loadReorderedLesson()
        assertEquals(0, ProgressRepository.getCurrentPage(context, LESSON))
        assertProgress(0)
        assertNull(ProgressRepository.getExerciseState(context, LESSON, 1, 2))
        assertNull(ProgressRepository.getExerciseState(context, LESSON, 3, 1))
        assertEquals(originalDrafts, drafts.all)
    }

    @Test fun openingAndAdvancingWithoutPauseRetainsFirstVisiblePageCoverageAfterReset() {
        loadReorderedLesson()
        ProgressRepository.resetLesson(context, ROUTE, LESSON)
        ProgressRepository.markLessonStarted(context, ROUTE, LESSON)
        val firstPage = ProgressRepository.getCurrentPage(context, LESSON)
        assertEquals(0, firstPage)
        assertProgress(0)

        // loadLesson saves the page it has rendered before any navigation or onPause.
        // Navigation then saves its destination, not the page that was just left.
        ProgressRepository.saveCurrentPage(context, ROUTE, LESSON, firstPage)
        val coverageKey = "lesson_${LESSON}_read_blocks_v1"
        val firstCoverage = progress.getStringSet(coverageKey, emptySet()).orEmpty().toSet()
        assertEquals(setOf("block_0004_0000"), firstCoverage)
        assertTrue(LessonStateCoordinates.isTheoryPageRead(context, LESSON, firstPage) == true)
        assertProgress(0) // The other block of this original theory group is displayed on page 1.

        ProgressRepository.saveCurrentPage(context, ROUTE, LESSON, firstPage + 1)
        val afterAdvance = progress.getStringSet(coverageKey, emptySet()).orEmpty()
        assertTrue(afterAdvance.containsAll(firstCoverage))
        assertTrue(afterAdvance.contains("block_0004_0001"))
        assertTrue(LessonStateCoordinates.isTheoryPageRead(context, LESSON, firstPage) == true)
        assertEquals(1, ProgressRepository.getCurrentPage(context, LESSON))
        assertProgress(1)
        assertCompletion(false)
        assertEquals(originalDrafts, drafts.all)
    }

    private fun loadReorderedLesson() {
        val lesson = LessonRepository.loadLessonJson(context, ROUTE, LESSON)
        assertTrue(lesson.has("stateLayout"))
        assertEquals(5, lesson.getJSONArray("pages").length())
        assertEquals("partial_inline", lesson.getJSONArray("pages").getJSONObject(1)
            .getJSONArray("blocks").getJSONObject(2).getString("id"))
    }

    private fun completeRemainingWork() {
        assertProgress(3)
        val predictionBefore = progress.getString(answerKey(1, 2), null)
        ProgressRepository.saveExerciseState(context, ROUTE, LESSON, 1, 2,
            JSONObject("""{"answers":["2","3"],"correct":true}"""))
        assertEquals(predictionBefore, progress.getString(answerKey(1, 2), null))
        assertTrue(JSONObject(progress.getString(answerKey(3, 1), null)!!).getBoolean("correct"))
        assertProgress(3)
        ProgressRepository.saveExerciseState(context, ROUTE, LESSON, 3, 2,
            JSONObject("""{"order":[0,1],"correct":true}"""))
        assertProgress(4)
        assertCompletion(false)

        ProgressRepository.saveCurrentPage(context, ROUTE, LESSON, 0)
        assertEquals(4, progress.getInt("lesson_${LESSON}_page", -1))
        assertEquals(0, ProgressRepository.getCurrentPage(context, LESSON))
        // The original theory group also contains the editor now displayed elsewhere.
        assertProgress(4)
        assertCompletion(false)
        ProgressRepository.saveCurrentPage(context, ROUTE, LESSON, 1)
        assertProgress(5)
        assertCompletion(true)
    }

    private fun saved(page: Int, block: Int): JSONObject =
        requireNotNull(ProgressRepository.getExerciseState(context, LESSON, page, block))

    private fun assertProgress(completed: Int) {
        val snapshot = ProgressRepository.getLessonPageProgress(context, ROUTE, LESSON)
        assertEquals(ProgressSnapshot(completed, 5), snapshot)
        assertEquals(completed * 20, snapshot.percentage)
    }

    private fun assertCompletion(completed: Boolean) {
        assertEquals(completed, ProgressRepository.isLessonCompleted(context, LESSON))
        assertEquals(completed, ProgressRepository.isUnitCompleted(context, UNIT))
        assertEquals(completed, ProgressRepository.isCourseCompleted(context, COURSE))
        val count = if (completed) 1 else 0
        assertEquals(ProgressSnapshot(count, 1), ProgressRepository.getUnitProgress(context, ROUTE, COURSE, UNIT))
        assertEquals(ProgressSnapshot(count, 1), ProgressRepository.getCourseProgress(context, ROUTE, COURSE))
    }

    private fun answerKey(page: Int, block: Int) = "exercise_${LESSON}_page_${page}_block_$block"

    private class IsolatedAssetContext(base: Context, private val testAssets: AssetManager) :
        ContextWrapper(base), AutoCloseable {
        private val prefix = "qa_state_repository_${UUID.randomUUID()}_"
        private val names = linkedSetOf<String>()
        override fun getApplicationContext(): Context = this
        override fun getAssets(): AssetManager = testAssets
        override fun getSharedPreferences(name: String, mode: Int): SharedPreferences {
            val isolated = prefix + name
            names += isolated
            return baseContext.getSharedPreferences(isolated, mode)
        }
        override fun close() {
            names.forEach { baseContext.deleteSharedPreferences(it) }
        }
    }

    private companion object {
        const val ROUTE = "programming"
        const val COURSE = "state_order_fixture"
        const val UNIT = "state_order_fixture__order"
        const val LESSON = "state_order_fixture__order__relocated"
        const val ASSET_DIRECTORY = "content/es/programming/$COURSE/order"
        const val DRAFT_KEY = "$ROUTE/$LESSON/4/1"
        const val UNRELATED_ANSWER = "exercise_unrelated__unit__lesson_page_0_block_0"
    }
}
