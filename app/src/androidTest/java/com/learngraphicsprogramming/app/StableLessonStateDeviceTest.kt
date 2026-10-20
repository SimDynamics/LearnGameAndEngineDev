package com.learngraphicsprogramming.app

import android.content.Context
import android.content.ContextWrapper
import android.content.SharedPreferences
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.json.JSONArray
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import java.util.UUID

/** Every write goes to a UUID preference namespace, never the student's progress. */
@RunWith(AndroidJUnit4::class)
class StableLessonStateDeviceTest {
    private lateinit var context: PrivateContext
    private lateinit var id: String

    @Before fun prepare() {
        context = PrivateContext(InstrumentationRegistry.getInstrumentation().targetContext)
        id = "state_order_test_" + UUID.randomUUID().toString().replace("-", "")
    }

    @After fun cleanup() { if (::context.isInitialized) context.close() }

    @Test fun reorderedAnswersAndAllEditorDraftSuffixesKeepTheirOldKeys() {
        val progress = context.getSharedPreferences("learning_progress", 0)
        val answer = "{\"correct\":true,\"answer\":\"original answer\"}"
        val answerKey = "exercise_${id}_page_0_block_1"
        progress.edit().putString(answerKey, answer).commit()
        val drafts = context.getSharedPreferences("cpp_lesson_drafts", 0)
        val prefix = "programming/$id/1/0"
        for (suffix in listOf("source", "input", "project", "sourceIndentation_v2", "compiler")) {
            drafts.edit().putString("$prefix/$suffix", "private-$suffix").commit()
        }
        val before = drafts.all.toMap()
        LessonStateCoordinates.register(context, id, fixture(reversed = true))

        val key = LessonStateCoordinates.block(context, id, 1, 1)
        assertEquals(LessonStatePosition(0, 1), key)
        assertEquals(answer, progress.getString("exercise_${id}_page_${key.page}_block_${key.block}", null))
        assertEquals(prefix, LessonStateCoordinates.draftKey(context, "programming", id, 0, 0))
        assertEquals(before, drafts.all)
    }

    @Test fun adoptionCanSkipTheIntermediateUnmovedRelease() {
        val prefs = context.getSharedPreferences("learning_progress", 0)
        prefs.edit().putBoolean("lesson_${id}_started", true).putInt("lesson_${id}_page", 1).commit()
        LessonStateCoordinates.register(context, id, fixture(reversed = true))
        assertEquals(0, LessonStateCoordinates.currentPage(context, id, 1))
        assertTrue(LessonStateCoordinates.isTheoryPageRead(context, id, 0) == true)
        assertTrue(LessonStateCoordinates.isTheoryPageRead(context, id, 1) == true)
    }

    @Test fun freshStudentStartsAtDisplayedPageZeroWithoutCreditingOldPageZero() {
        LessonStateCoordinates.register(context, id, fixture(reversed = true))
        val prefs = context.getSharedPreferences("learning_progress", 0)
        assertEquals(0, LessonStateCoordinates.currentPage(context, id, 0))
        prefs.edit().putBoolean("lesson_${id}_started", true).commit()
        LessonStateCoordinates.savePage(context, id, 0)
        assertEquals(1, prefs.getInt("lesson_${id}_page", -1))
        assertTrue(LessonStateCoordinates.isTheoryPageRead(context, id, 0) == true)
        assertFalse(LessonStateCoordinates.isTheoryPageRead(context, id, 1) == true)
    }

    @Test fun reorderingAndAdoptionAreIdempotentAndDoNotTouchHeartsOrDrafts() {
        val hearts = context.getSharedPreferences("attempt_reserve_seconds_test", 0)
        hearts.edit().putFloat("reserve_programming", 3f).putLong("updated_at_programming", 42L).commit()
        val heartBefore = hearts.all.toMap()
        LessonStateCoordinates.register(context, id, fixture(false))
        val prefs = context.getSharedPreferences("learning_progress", 0)
        val before = prefs.all.toMap()
        repeat(4) {
            LessonStateCoordinates.register(context, id, fixture(true))
            LessonStateCoordinates.register(context, id, fixture(false))
        }
        assertEquals(before, prefs.all)
        assertEquals(heartBefore, hearts.all)
    }

    @Test fun previousCompletionFlagsAreNotClearedByMigration() {
        val prefs = context.getSharedPreferences("learning_progress", 0)
        prefs.edit().putBoolean("lesson_${id}_completed", true).putBoolean("unit_fixture_completed", true)
            .putBoolean("course_fixture_completed", true).commit()
        LessonStateCoordinates.register(context, id, fixture(true))
        assertTrue(prefs.getBoolean("lesson_${id}_completed", false))
        assertTrue(prefs.getBoolean("unit_fixture_completed", false))
        assertTrue(prefs.getBoolean("course_fixture_completed", false))
    }

    @Test fun missingManifestAfterAdoptionFailsWithoutDeletingOrReassigningData() {
        LessonStateCoordinates.register(context, id, fixture(false))
        val prefs = context.getSharedPreferences("learning_progress", 0)
        val before = prefs.all.toMap()
        assertTrue(runCatching { LessonStateCoordinates.register(context, id, JSONObject("{\"pages\":[]}")) }
            .exceptionOrNull() is LessonStateLayoutException)
        assertEquals(before, prefs.all)
    }

    @Test fun mapFromAnotherLessonIsRejectedBeforeWritingMetadata() {
        val other = fixture(false).put("id", "another_lesson")
        assertTrue(runCatching { LessonStateCoordinates.register(context, id, other) }
            .exceptionOrNull() is LessonStateLayoutException)
        assertTrue(context.getSharedPreferences("learning_progress", 0).all.isEmpty())
    }

    @Test fun corruptCoverageFailsWithoutClearingCompletion() {
        val prefs = context.getSharedPreferences("learning_progress", 0)
        LessonStateCoordinates.register(context, id, fixture(false))
        prefs.edit().putBoolean("lesson_${id}_completed", true)
            .putInt("lesson_${id}_read_blocks_v1", 13).commit()
        val before = prefs.all.toMap()
        assertTrue(runCatching { LessonStateCoordinates.prepare(context, id) }
            .exceptionOrNull() is LessonStateLayoutException)
        assertEquals(before, prefs.all)
    }

    @Test fun loadFailureOfMappedLessonDoesNotBecomeZeroProgress() {
        LessonStateCoordinates.register(context, id, fixture(false))
        assertTrue(runCatching { LessonStateCoordinates.rethrowIfMapped(context, id,
            java.io.FileNotFoundException("unavailable")) }.exceptionOrNull() is LessonStateLayoutException)
    }

    @Test fun invalidFingerprintCannotTransferAnAcceptedAnswer() {
        val changed = fixture(true)
        changed.getJSONArray("pages").getJSONObject(0).getJSONArray("blocks").getJSONObject(0)
            .put("initialCode", "different program")
        assertTrue(runCatching { LessonStateCoordinates.register(context, id, changed) }
            .exceptionOrNull() is LessonStateLayoutException)
        assertTrue(context.getSharedPreferences("learning_progress", 0).all.isEmpty())
    }

    @Test fun attemptsUseTheSameIdentityAfterMovingAPuzzle() {
        LessonStateCoordinates.register(context, id, fixture(false))
        val before = LessonStateCoordinates.attemptKey(context, "programming", id, 0, 1)
        LessonStateCoordinates.register(context, id, fixture(true))
        assertEquals(before, LessonStateCoordinates.attemptKey(context, "programming", id, 1, 1))
    }

    @Test fun explicitResetClearsOnlyCoverageAndKeepsPinnedIdentity() {
        val prefs = context.getSharedPreferences("learning_progress", 0)
        prefs.edit().putBoolean("lesson_${id}_started", true).putInt("lesson_${id}_page", 1).commit()
        LessonStateCoordinates.register(context, id, fixture(true))
        val editor = prefs.edit()
        LessonStateCoordinates.clearReading(context, id, editor)
        editor.remove("lesson_${id}_page").remove("lesson_${id}_started").commit()
        assertEquals(0, LessonStateCoordinates.currentPage(context, id, 0))
        assertFalse(LessonStateCoordinates.isTheoryPageRead(context, id, 0) == true)
        assertTrue(prefs.contains("lesson_${id}_state_lineage_v1"))
    }

    private fun fixture(reversed: Boolean): JSONObject {
        val page0 = JSONObject().put("blocks", JSONArray()
            .put(JSONObject().put("type", "paragraph").put("text", "Intro"))
            .put(JSONObject().put("type", "output_prediction").put("id", "prediction").put("answers", JSONArray().put("Original"))))
        val page1 = JSONObject().put("blocks", JSONArray()
            .put(JSONObject().put("type", "cpp_compiler").put("initialCode", "CODE")))
        val pages = listOf(page0, page1)
        val origin = JSONArray()
        pages.forEachIndexed { p, page ->
            val blocks = page.getJSONArray("blocks")
            val entries = JSONArray()
            for (b in 0 until blocks.length()) entries.put(JSONObject().put("id", "b${p}_$b")
                .put("sha256", LessonStateLayoutJson.stateFingerprint(blocks.getJSONObject(b))))
            origin.put(JSONObject().put("id", "p$p").put("blocks", entries))
        }
        val order = if (reversed) listOf(1, 0) else listOf(0, 1)
        val current = JSONArray()
        val content = JSONArray()
        order.forEach { p ->
            val ids = JSONArray()
            for (b in 0 until pages[p].getJSONArray("blocks").length()) ids.put("b${p}_$b")
            current.put(JSONObject().put("id", "p$p").put("blocks", ids))
            content.put(pages[p])
        }
        return JSONObject().put("id", id).put("pages", content)
            .put("stateLayout", JSONObject().put("version", 1).put("origin", origin).put("current", current))
    }

    private class PrivateContext(base: Context) : ContextWrapper(base) {
        private val prefix = "qa_state_order_${UUID.randomUUID()}_"
        private val files = mutableSetOf<String>()
        override fun getSharedPreferences(name: String, mode: Int): SharedPreferences {
            val privateName = prefix + name
            files += privateName
            return super.getSharedPreferences(privateName, mode)
        }
        fun close() { files.forEach { baseContext.deleteSharedPreferences(it) } }
    }
}
