package com.learngraphicsprogramming.app

import android.app.LocaleManager
import android.content.Context
import android.content.ContextWrapper
import android.content.SharedPreferences
import android.content.res.AssetManager
import android.os.Build
import android.os.LocaleList
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import java.util.UUID

/** Real repositories/preferences with small QA assets; the educational JSON is never rewritten. */
@RunWith(AndroidJUnit4::class)
class LocalizedStableLessonStateDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val target get() = instrumentation.targetContext
    private lateinit var context: PrivateContext
    private var originalLocale = ""
    private val names = linkedSetOf<String>()
    private lateinit var prefix: String

    @Before fun isolatePreferencesAndSelectSpanish() {
        assertEquals("Use the isolated LDPlayer app", "com.learngraphicsprogramming.app.realdownloadsqa", target.packageName)
        assertTrue("LDPlayer only", Build.SUPPORTED_ABIS.any { it == "x86" || it == "x86_64" })
        originalLocale = UiLanguageSettings.selectedTag(target)
        prefix = "qa_localized_state_${UUID.randomUUID()}_"
        context = PrivateContext(target, instrumentation.context.assets, prefix, names)
        select("es")
    }

    @After fun restoreOnlyTheQaLocaleAndDiscardIsolatedPreferences() {
        if (!::context.isInitialized) return
        instrumentation.runOnMainSync {
            if (Build.VERSION.SDK_INT >= 33) target.getSystemService(LocaleManager::class.java).applicationLocales =
                LocaleList.forLanguageTags(originalLocale)
            else androidx.appcompat.app.AppCompatDelegate.setApplicationLocales(
                androidx.core.os.LocaleListCompat.forLanguageTags(originalLocale))
        }
        instrumentation.waitForIdleSync()
        names.forEach { target.deleteSharedPreferences(it) }
    }

    @Test fun historicalLocaleDifferencesRetainTheOriginalAnswersDraftsAndNumericProgress() {
        seedLegacy(F.LESSON)
        val drafts = drafts().all.toMap()
        val heartBalance = context.getSharedPreferences("attempt_reserve_seconds_test", 0).all.toMap()
        val expected = F.mapped(F.minimalFamily())
        assertEquals(LessonStateLayoutJson.fingerprint(expected), LessonStateLayoutJson.fingerprint(load()))
        assertEquals(ProgressSnapshot(4, 6), ProgressRepository.getLessonPageProgress(context, F.ROUTE, F.LESSON))
        assertHistoricalOwners(F.LESSON, 4, 1, 4)
        assertEquals("7", ProgressRepository.getExerciseState(context, F.LESSON, 4, 1)!!.getString("answer"))
        assertTrue(ProgressRepository.getExerciseState(context, F.LESSON, 4, 1)!!.getBoolean("correct"))
        assertFreshAddition(F.LESSON, 4, 3)

        for (language in listOf("en", "fr")) {
            select(language)
            val raw = load()
            assertFalse("The historical translation stays unmapped", raw.has("stateLayout"))
            assertEquals(29, blockCount(raw))
            assertEquals("note", raw.getJSONArray("pages").getJSONObject(4).getJSONArray("blocks")
                .getJSONObject(3).getString("type"))
            assertEquals(oldDraft(F.LESSON), LessonStateCoordinates.draftKey(context, F.ROUTE, F.LESSON, 4, 2))
            assertEquals(ProgressSnapshot(4, 6), ProgressRepository.getLessonPageProgress(context, F.ROUTE, F.LESSON))
            assertEquals(4, ProgressRepository.getCurrentPage(context, F.LESSON))
            assertEquals(drafts, drafts().all)
        }
        select("es")
        load()
        assertHistoricalOwners(F.LESSON, 4, 1, 4)
        assertFreshAddition(F.LESSON, 4, 3)
        assertEquals(ProgressSnapshot(4, 6), ProgressRepository.getLessonPageProgress(context, F.ROUTE, F.LESSON))
        assertEquals(drafts, drafts().all)
        assertEquals(heartBalance, context.getSharedPreferences("attempt_reserve_seconds_test", 0).all)
    }

    @Test fun aSecondPermutationAndRepositoryReopeningKeepPageAndActivityOwners() {
        seedLegacy(F.LESSON)
        load()
        val drafts = drafts().all.toMap()
        val oldAnswer = progress().getString(answerKey(F.LESSON, 4, 3), null)
        val movedAgain = F.mapped(F.minimalFamily(), secondPermutation = true)
        LessonStateCoordinates.register(context, F.LESSON, movedAgain)
        assertEquals(5, ProgressRepository.getCurrentPage(context, F.LESSON))
        assertHistoricalOwners(F.LESSON, 5, 1, 4)
        assertFreshAddition(F.LESSON, 5, 3)
        LessonStateCoordinates.savePage(context, F.LESSON, 5)
        assertEquals(4, progress().getInt("lesson_${F.LESSON}_page", -1))

        // Recreate the repository context, then reload the installed first permutation. The bookmark
        // follows the page identity; no position, answer or draft is copied to its display address.
        context = PrivateContext(target, instrumentation.context.assets, prefix, names)
        load()
        assertEquals(4, ProgressRepository.getCurrentPage(context, F.LESSON))
        assertHistoricalOwners(F.LESSON, 4, 1, 4)
        assertEquals(oldAnswer, progress().getString(answerKey(F.LESSON, 4, 3), null))
        assertEquals(drafts, drafts().all)
        assertEquals(ProgressSnapshot(4, 6), ProgressRepository.getLessonPageProgress(context, F.ROUTE, F.LESSON))
    }

    @Test fun repositorySavingAndResettingMovedAnswersDoesNotTouchEditorsOrAnotherLesson() {
        seedLegacy(F.LESSON)
        load()
        val drafts = drafts().all.toMap()
        ProgressRepository.saveExerciseState(context, F.ROUTE, F.LESSON, 4, 5,
            JSONObject().put("selectedIndex", 0).put("correct", true))
        assertTrue(JSONObject(progress().getString(answerKey(F.LESSON, 4, 4), null)!!).getBoolean("correct"))
        assertEquals(ProgressSnapshot(5, 6), ProgressRepository.getLessonPageProgress(context, F.ROUTE, F.LESSON))
        ProgressRepository.resetExerciseState(context, F.ROUTE, F.LESSON, 4, 1)
        assertNull(progress().getString(answerKey(F.LESSON, 4, 3), null))
        assertNotNull(progress().getString(answerKey(F.LESSON, 4, 4), null))
        assertEquals(ProgressSnapshot(4, 6), ProgressRepository.getLessonPageProgress(context, F.ROUTE, F.LESSON))
        assertEquals(drafts, drafts().all)

        ProgressRepository.resetLesson(context, F.ROUTE, F.LESSON)
        assertFalse(progress().all.keys.any { it.startsWith("exercise_${F.LESSON}_") })
        assertEquals("unrelated answer", progress().getString("exercise_unrelated_page_0_block_0", null))
        assertEquals(0, ProgressRepository.getCurrentPage(context, F.LESSON))
        assertEquals(ProgressSnapshot(0, 6), ProgressRepository.getLessonPageProgress(context, F.ROUTE, F.LESSON))
        assertFalse(ProgressRepository.isLessonCompleted(context, F.LESSON))
        assertEquals(drafts, drafts().all)
        // The per-locale conservation metadata must survive reset and remain compatible with raw EN/FR.
        select("en"); assertFalse(load().has("stateLayout"))
        select("fr"); assertFalse(load().has("stateLayout"))
        select("es"); load()
        assertEquals(ProgressSnapshot(0, 6), ProgressRepository.getLessonPageProgress(context, F.ROUTE, F.LESSON))
    }

    @Test fun realUnalignedFamilyMapsInMemoryWithoutEditingOrTranslatingAnyCurricularAsset() {
        val real = F.realFamily(target)
        val before = real.mapValues { LessonStateLayoutJson.fingerprint(it.value) }
        assertEquals(listOf(34, 29, 29), F.languages.map { blockCount(real.getValue(it)) })
        seedLegacy(F.REAL_LESSON)
        val drafts = drafts().all.toMap()
        val mapped = F.mapped(real)
        LessonStateCoordinates.register(context, F.REAL_LESSON, mapped)
        assertHistoricalOwners(F.REAL_LESSON, 4, 1, 4)
        assertFreshAddition(F.REAL_LESSON, 4, 3)
        assertEquals(4, ProgressRepository.getCurrentPage(context, F.REAL_LESSON))
        val identity = LessonStateCoordinates.narrationPageIdentity(context, F.ROUTE, F.REAL_LESSON, 4)
        assertNotEquals("programming/${F.REAL_LESSON}/4", identity)

        for (language in listOf("en", "fr")) {
            select(language)
            LessonStateCoordinates.register(context, F.REAL_LESSON, real.getValue(language))
            assertEquals(oldDraft(F.REAL_LESSON), LessonStateCoordinates.draftKey(context, F.ROUTE, F.REAL_LESSON, 4, 2))
            assertEquals(drafts, drafts().all)
        }
        select("es")
        val again = F.mapped(real, secondPermutation = true)
        LessonStateCoordinates.register(context, F.REAL_LESSON, again)
        assertEquals(5, ProgressRepository.getCurrentPage(context, F.REAL_LESSON))
        assertHistoricalOwners(F.REAL_LESSON, 5, 1, 4)
        assertFreshAddition(F.REAL_LESSON, 5, 3)
        assertEquals(before, real.mapValues { LessonStateLayoutJson.fingerprint(it.value) })
        assertEquals(before, F.realFamily(target).mapValues { LessonStateLayoutJson.fingerprint(it.value) })
        assertEquals(drafts, drafts().all)
    }

    @Test fun anExistingVersionOnePinCanAdoptTheFamilyWithoutDeletingItsOriginalMetadata() {
        seedLegacy(F.LESSON)
        val original = F.minimalFamily().getValue("es")
        val oldOrigin = F.origin(original)
        val oldCurrent = org.json.JSONArray()
        repeat(oldOrigin.length()) { page ->
            val entry = oldOrigin.getJSONObject(page)
            oldCurrent.put(JSONObject().put("id", entry.getString("id")).put("blocks", org.json.JSONArray().also { ids ->
                val blocks = entry.getJSONArray("blocks")
                repeat(blocks.length()) { ids.put(blocks.getJSONObject(it).getString("id")) }
            }))
        }
        val v1 = F.copy(original).put("stateLayout", JSONObject().put("version", 1)
            .put("origin", oldOrigin).put("current", oldCurrent))
        LessonStateCoordinates.register(context, F.LESSON, v1)
        val prior = progress().all.filterKeys { it.endsWith("_v1") }
        assertTrue(prior.keys.any { it.endsWith("_state_lineage_v1") })
        val drafts = drafts().all.toMap()
        load()
        assertHistoricalOwners(F.LESSON, 4, 1, 4)
        assertFreshAddition(F.LESSON, 4, 3)
        assertEquals(prior, progress().all.filterKeys { it.endsWith("_v1") })
        for (language in listOf("en", "fr", "es")) {
            select(language); load()
            assertEquals(ProgressSnapshot(4, 6), ProgressRepository.getLessonPageProgress(context, F.ROUTE, F.LESSON))
        }
        assertEquals(prior, progress().all.filterKeys { it.endsWith("_v1") })
        assertEquals(drafts, drafts().all)
    }

    @Test fun aMissingSameLanguageMapAndChangedActivityOwnershipFailWithoutAlteringSavedData() {
        seedLegacy(F.LESSON)
        load()
        val before = progress().all.toMap()
        val drafts = drafts().all.toMap()
        assertTrue(runCatching { LessonStateCoordinates.register(context, F.LESSON, F.minimalFamily().getValue("es")) }
            .exceptionOrNull() is LessonStateLayoutException)
        assertEquals(before, progress().all)
        val changed = F.mapped(F.minimalFamily())
        changed.getJSONObject("stateLayout").getJSONArray("additions").getJSONObject(0)
            .getJSONObject("storage").put("block", 1)
        assertTrue(runCatching { LessonStateCoordinates.register(context, F.LESSON, changed) }
            .exceptionOrNull() is LessonStateLayoutException)
        assertEquals(before, progress().all)
        assertEquals(drafts, drafts().all)
    }

    @Test fun anUnownedIncompatibleVersionOnePinCannotBeReplacedByALocalizedFamily() {
        seedLegacy(F.LESSON)
        assertTrue(progress().edit()
            .putString("lesson_${F.LESSON}_state_lineage_v1", "f".repeat(64))
            .putStringSet("lesson_${F.LESSON}_read_pages_v1", setOf("foreign_page"))
            .putStringSet("lesson_${F.LESSON}_read_blocks_v1", setOf("foreign_block"))
            .putBoolean("lesson_${F.LESSON}_completed", true).commit())
        val progressBefore = progress().all.toMap()
        val draftsBefore = drafts().all.toMap()
        assertTrue(runCatching {
            LessonStateCoordinates.register(context, F.LESSON, F.mapped(F.minimalFamily()))
        }.exceptionOrNull() is LessonStateLayoutException)
        assertEquals("The previous pin, coverage, answers and completion flags are retained", progressBefore, progress().all)
        assertEquals(draftsBefore, drafts().all)
        assertFalse(progress().contains("lesson_${F.LESSON}_state_lineage_v2_es"))
        assertFalse(progress().contains("lesson_${F.LESSON}_state_v1_owner_locale"))
    }

    private fun seedLegacy(lesson: String) {
        val edit = progress().edit().putBoolean("lesson_${lesson}_started", true)
            .putInt("lesson_${lesson}_page", 4).putBoolean("lesson_${lesson}_completed", false)
            .putString("exercise_unrelated_page_0_block_0", "unrelated answer")
        listOf(0 to 4, 1 to 3, 1 to 4, 2 to 3, 2 to 4, 2 to 5, 3 to 2, 3 to 3).forEach { (page, block) ->
            edit.putString(answerKey(lesson, page, block), """{"correct":true,"answer":"old accepted response"}""")
        }
        edit.putString(answerKey(lesson, 4, 3), """{"correct":true,"answer":"7"}""")
            .putString(answerKey(lesson, 4, 4), """{"correct":false,"selectedIndex":1}""")
            .putString(answerKey(lesson, 6, 0), """{"correct":true,"answer":"unrelated virtual-slot decoy"}""")
        assertTrue(edit.commit())
        val draftEdit = drafts().edit()
        for ((key, source) in listOf(oldDraft(lesson) to "// exact student draft\nint main() { return 4; }\n",
            "programming/$lesson/5/6" to "// independent existing Spanish editor\n",
            "programming/$lesson/6/0" to "// never inherit this virtual-slot decoy\n")) {
            CppLessonSourceDraft.write(draftEdit, key, source)
            CppLessonInputDraft.write(draftEdit, key, " \tRita\n4\n  ")
            draftEdit.putString("$key/project", """{"main.cpp":"student project","CMakeLists.txt":"project(qa)"}""")
                .putString("$key/compiler", "clang")
        }
        assertTrue(draftEdit.commit())
        assertTrue(context.getSharedPreferences("attempt_reserve_seconds_test", 0).edit()
            .putFloat("reserve_programming", 3f).putLong("updated_at_programming", 42L).commit())
    }

    private fun assertHistoricalOwners(lesson: String, page: Int, prediction: Int, compiler: Int) {
        assertEquals(LessonStatePosition(4, 3), LessonStateCoordinates.block(context, lesson, page, prediction))
        assertEquals(LessonStatePosition(4, 2), LessonStateCoordinates.block(context, lesson, page, compiler))
        assertEquals(oldDraft(lesson), LessonStateCoordinates.draftKey(context, F.ROUTE, lesson, page, compiler))
        assertEquals("// exact student draft\nint main() { return 4; }\n",
            CppLessonSourceDraft.read(drafts(), oldDraft(lesson), "initial code"))
        assertEquals(" \tRita\n4\n  ", CppLessonInputDraft.read(drafts(), oldDraft(lesson), "initial input"))
        assertEquals("7", ProgressRepository.getExerciseState(context, lesson, page, prediction)!!.getString("answer"))
    }

    private fun assertFreshAddition(lesson: String, page: Int, block: Int) {
        val key = LessonStateCoordinates.draftKey(context, F.ROUTE, lesson, page, block)
        assertNotEquals("New activities cannot claim a coincident old positional draft", "programming/$lesson/6/0", key)
        assertNotEquals(oldDraft(lesson), key)
        assertEquals("new initial code", CppLessonSourceDraft.read(drafts(), key, "new initial code"))
        assertNull("A virtual-slot answer cannot complete a new activity", ProgressRepository.getExerciseState(context, lesson, page, block))
    }

    private fun load() = LessonRepository.loadLessonJson(context, F.ROUTE, F.LESSON)
    private fun drafts() = context.getSharedPreferences("cpp_lesson_drafts", 0)
    private fun progress() = context.getSharedPreferences("learning_progress", 0)
    private fun oldDraft(lesson: String) = "programming/$lesson/4/2"
    private fun answerKey(lesson: String, page: Int, block: Int) = "exercise_${lesson}_page_${page}_block_$block"
    private fun blockCount(document: JSONObject): Int = document.getJSONArray("pages").let { pages ->
        (0 until pages.length()).sumOf { pages.getJSONObject(it).getJSONArray("blocks").length() }
    }
    private fun select(tag: String) {
        instrumentation.runOnMainSync { UiLanguageSettings.select(context, tag) }
        instrumentation.waitForIdleSync()
        assertEquals(tag, CurriculumAssetLocale.current(context))
    }

    private class PrivateContext(base: Context, private val testAssets: AssetManager,
        private val prefix: String, private val files: MutableSet<String>) : ContextWrapper(base) {
        override fun getApplicationContext(): Context = this
        override fun getAssets(): AssetManager = testAssets
        override fun getSharedPreferences(name: String, mode: Int): SharedPreferences =
            baseContext.getSharedPreferences(prefix + name, mode).also { files += prefix + name }
    }

    private companion object { val F = LocalizedStableLessonQaFixture }
}
