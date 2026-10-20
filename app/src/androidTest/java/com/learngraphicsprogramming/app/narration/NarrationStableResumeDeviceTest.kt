package com.learngraphicsprogramming.app.narration

import android.content.Context
import android.content.ContextWrapper
import android.content.SharedPreferences
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import java.util.UUID

/** Real Android SharedPreferences when instrumented; isolated from every student's key. */
@RunWith(AndroidJUnit4::class)
class NarrationStableResumeDeviceTest {
    private lateinit var context: PrivateContext
    private val before = NarrationStablePage("programming/fixture/lineage", "c".repeat(64), listOf(
        NarrationStablePart("a", false, "a".repeat(64), 0),
        NarrationStablePart("b", false, "b".repeat(64), 0)))

    @Before fun prepare() {
        context = PrivateContext(InstrumentationRegistry.getInstrumentation().targetContext)
    }
    @After fun cleanup() { if (::context.isInitialized) context.close() }

    @Test fun legacyPreferenceRowsMigrateInPlaceAndSurviveStoreRecreation() {
        val preferences = context.getSharedPreferences("lesson_narration_resume_bounded_v1", 0)
        assertTrue(preferences.edit().putString("key_0", "recent").putInt("segment_0", 0)
            .putLong("position_0", 10).putString("key_1", "legacy").putInt("segment_1", 1)
            .putLong("position_1", 1_537).putString("key_2", "older").putInt("segment_2", 0)
            .putLong("position_2", 20).commit())
        val reordered = before.copy(parts = before.parts.reversed())
        NarrationResumeStore(context).prepareStable("current", reordered, mapOf("legacy" to before))
        val reopened = NarrationResumeStore(context)
        assertEquals(NarrationResumePoint(0, 1_537), reopened.restore("current", 2))
        assertEquals("recent", preferences.getString("key_0", null))
        assertEquals("current", preferences.getString("key_1", null))
        assertEquals("older", preferences.getString("key_2", null))
        assertEquals(reordered, NarrationStablePageJson.decode(preferences.getString("stable_1", null)))
        reopened.prepareStable("twice", before, emptyMap())
        assertEquals(NarrationResumePoint(1, 1_537), NarrationResumeStore(context).restore("twice", 2))
    }

    @Test fun finishedBookmarkKeepsTheSameThreeSlotLimitAndCannotBeSavedByItsFormerOwner() {
        val store = NarrationResumeStore(context)
        store.begin("older", NarrationResumePoint())
        val owner = store.begin("old", NarrationResumePoint(), before)
        store.save("old", owner, NarrationResumePoint(1, 4_100, finished = true))
        store.prepareStable("new", before, emptyMap())
        assertEquals(NarrationResumePoint(1, 4_100, finished = true), store.restore("new", 2))
        store.save("old", owner, NarrationResumePoint(0, 99))
        store.save("new", owner, NarrationResumePoint(0, 99))
        assertEquals(NarrationResumePoint(1, 4_100, finished = true), store.restore("new", 2))
        store.begin("third", NarrationResumePoint())
        store.begin("fourth", NarrationResumePoint())
        assertNull(store.restore("older", 2))
        assertNotNull(store.restore("new", 2))
    }

    @Test fun malformedAdditionalMetadataDoesNotDeleteAnExistingLegacyPosition() {
        val preferences = context.getSharedPreferences("lesson_narration_resume_bounded_v1", 0)
        assertTrue(preferences.edit().putString("key_0", "legacy").putInt("segment_0", 1)
            .putLong("position_0", 531).putString("stable_0", "broken-json").commit())
        val store = NarrationResumeStore(context)
        assertEquals(NarrationResumePoint(1, 531), store.restore("legacy", 2))
        store.prepareStable("current", before.copy(parts = before.parts.reversed()), mapOf("legacy" to before))
        assertEquals(NarrationResumePoint(0, 531), store.restore("current", 2))
    }

    private class PrivateContext(base: Context) : ContextWrapper(base) {
        private val prefix = "qa_narration_state_${UUID.randomUUID()}_"
        private val files = mutableSetOf<String>()
        // Store intentionally uses applicationContext; keep that inside the test namespace too.
        override fun getApplicationContext(): Context = this
        override fun getSharedPreferences(name: String, mode: Int): SharedPreferences {
            val privateName = prefix + name
            files += privateName
            return super.getSharedPreferences(privateName, mode)
        }
        fun close() { files.forEach { baseContext.deleteSharedPreferences(it) } }
    }
}
