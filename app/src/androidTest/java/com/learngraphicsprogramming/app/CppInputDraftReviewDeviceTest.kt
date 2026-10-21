package com.learngraphicsprogramming.app

import android.content.Context
import android.content.SharedPreferences
import android.graphics.Bitmap
import android.graphics.Rect
import android.os.SystemClock
import android.view.View
import androidx.test.core.app.ActivityScenario
import androidx.test.espresso.Espresso.onView
import androidx.test.espresso.action.ViewActions.click
import androidx.test.espresso.assertion.ViewAssertions.doesNotExist
import androidx.test.espresso.assertion.ViewAssertions.matches
import androidx.test.espresso.matcher.RootMatchers.isDialog
import androidx.test.espresso.matcher.ViewMatchers.isDisplayed
import androidx.test.espresso.matcher.ViewMatchers.withId
import androidx.test.espresso.matcher.ViewMatchers.withTagValue
import androidx.test.espresso.matcher.ViewMatchers.withText
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.hamcrest.Matchers.equalTo
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

/** The published reading laboratory is rendered under an exclusive QA lesson ID.
 * Existing support restores every original QA preference after each test; this
 * class captures only the native review dialog and never compiles or changes curricular assets.
 */
@RunWith(AndroidJUnit4::class)
class CppInputDraftReviewDeviceTest : CppOutputCheckQaSupport() {
    private val originalTag = "cpp-input-draft-review-original"
    private val proposedTag = "cpp-input-draft-review-proposed"
    private val original = "Leo\n2\n    "
    private val repaired = "Leo\n2\n"
    private var capturedReview = false

    @Test fun explicitKeepAndRemoveResolveOnlyThePresentedInputAndSurviveReopening() {
        for (encoded in listOf(false, true)) for (remove in listOf(false, true)) {
            val state = prepare(encoded)
            val expected = if (remove) repaired else original
            withFixture(state.fixture) { host ->
                exposeInput(host)
                assertReviewPreview()
                assertPendingUnchanged(host, state)
                onView(withId(if (remove) android.R.id.button2 else android.R.id.button1))
                    .inRoot(isDialog()).perform(click())
                assertNoReviewDialog()
                assertConfirmed(host, state, expected)
            }
            withFixture(state.fixture) { host ->
                exposeInput(host)
                assertNoReviewDialog()
                assertConfirmed(host, state, expected)
            }
        }
    }

    @Test fun decidingLaterDoesNotInventConfirmationAndTheSameQuestionReturnsOnReopening() {
        for (encoded in listOf(false, true)) {
            val state = prepare(encoded)
            repeat(2) {
                withFixture(state.fixture) { host ->
                    exposeInput(host)
                    assertReviewPreview()
                    assertPendingUnchanged(host, state)
                    onView(withId(android.R.id.button3)).inRoot(isDialog()).perform(click())
                    assertNoReviewDialog()
                    assertPendingUnchanged(host, state)
                }
            }
        }
    }

    @Test fun aNewConfirmedInputWithManualSpacesTabsAndLineBreaksNeverRequestsRepair() {
        val state = prepare(encoded = true)
        val typed = "  Leo\t2\r\n    "
        assertTrue(CppLessonInputDraft.write(state.preferences.edit(), state.key, typed).commit())
        repeat(2) {
            withFixture(state.fixture) { host ->
                exposeInput(host)
                assertNoReviewDialog()
                assertConfirmed(host, state, typed)
            }
        }
    }

    private data class State(
        val fixture: LabFixture,
        val key: String,
        val preferences: SharedPreferences,
        val sourceValues: Map<String, *>,
        val siblingValues: Map<String, *>,
        val heartValues: Map<String, *>
    )

    private fun prepare(encoded: Boolean): State {
        PremiumRepository.setVerifiedFree(context)
        assertFalse(AttemptRepository.getState(context, "programming").unlimited)
        val hearts = context.getSharedPreferences("attempt_reserve_seconds_test", Context.MODE_PRIVATE).all.toMap()
        val location = LabLocation("objetos_y_estado/lesson_leer_una_entrada_conocida.json", 7, 3)
        val lesson = source(location.path)
        val block = lesson.getJSONArray("pages").getJSONObject(6).getJSONArray("blocks").getJSONObject(2)
        val fixture = fixture(location, block.getJSONObject("outputCheck"))
        assertEquals("The real laboratory and its declared contract remain identical", block.toString(), fixture.block.toString())
        val key = "programming/${fixture.id}/6/2"
        val preferences = context.getSharedPreferences("cpp_lesson_drafts", Context.MODE_PRIVATE)
        val source = CppIndentationUnits.loadedText(block.getString("initialCode"), seedPredefined = true)
        val edit = CppLessonSourceDraft.write(preferences.edit(), key, source)
            .putString("$key/input", if (encoded) CppDraftTextCodec.encode(original, "input") else original)
            .remove("$key/inputSha256")
        if (encoded) edit.putString("$key/inputEncoding", CppDraftTextCodec.ENCODING)
        else edit.remove("$key/inputEncoding")
        val siblingKey = "programming/${fixture.id}/6/99"
        CppLessonInputDraft.write(edit, siblingKey, "neighbor \t\n    ")
        assertTrue(edit.commit())
        return State(fixture, key, preferences,
            preferences.all.filterKeys { it.startsWith("$key/source") },
            preferences.all.filterKeys { it.startsWith("$siblingKey/") }, hearts)
    }

    private fun exposeInput(host: ActivityScenario<LessonActivity>) {
        host.onActivity { activity ->
            val field = input(activity)
            assertEquals(View.VISIBLE, field.visibility)
            field.requestRectangleOnScreen(Rect(0, 0, field.width.coerceAtLeast(1), field.height.coerceAtLeast(1)), true)
        }
        instrumentation.waitForIdleSync()
    }

    private fun assertReviewPreview() {
        onView(withTagValue(equalTo<Any>(originalTag))).inRoot(isDialog())
            .check(matches(isDisplayed())).check(matches(withText("Leo\\n2\\n····")))
        onView(withTagValue(equalTo<Any>(proposedTag))).inRoot(isDialog())
            .check(matches(isDisplayed())).check(matches(withText("Leo\\n2\\n")))
        if (!capturedReview) {
            val bounds = Rect()
            onView(withTagValue(equalTo<Any>(originalTag))).inRoot(isDialog()).check { view, error ->
                if (error != null) throw error
                val root = requireNotNull(view).rootView
                val location = IntArray(2)
                root.getLocationOnScreen(location)
                bounds.set(location[0], location[1], location[0] + root.width, location[1] + root.height)
            }
            var previousPixels: IntArray? = null
            var stableFrames = 0
            var settled: Bitmap? = null
            val deadline = SystemClock.uptimeMillis() + 5_000
            while (SystemClock.uptimeMillis() < deadline) {
                val frame = requireNotNull(instrumentation.uiAutomation.takeScreenshot())
                assertTrue(bounds.left >= 0 && bounds.top >= 0 && bounds.right <= frame.width && bounds.bottom <= frame.height)
                val pixels = IntArray(bounds.width() * bounds.height())
                frame.getPixels(pixels, 0, bounds.width(), bounds.left, bounds.top, bounds.width(), bounds.height())
                stableFrames = if (previousPixels?.contentEquals(pixels) == true) stableFrames + 1 else 0
                previousPixels = pixels
                if (stableFrames >= 3) { settled = frame; break }
                frame.recycle()
                SystemClock.sleep(20)
            }
            val bitmap = requireNotNull(settled) { "The actual review window never reached four stable frames" }
            try {
                File(context.getExternalFilesDir(null), "cpp-input-review-native.png").outputStream().use {
                    assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it))
                }
            } finally { bitmap.recycle() }
            capturedReview = true
        }
    }

    private fun assertNoReviewDialog() {
        onView(withTagValue(equalTo<Any>(originalTag))).check(doesNotExist())
    }

    private fun assertPendingUnchanged(host: ActivityScenario<LessonActivity>, state: State) {
        host.onActivity { activity ->
            assertEquals("No decision may alter even one character of the displayed input", original, input(activity).text.toString())
            assertEquals("Escaping legacy storage cannot alter its characters or decide whether to remove them", original,
                CppLessonInputDraft.decode(requireNotNull(state.preferences.getString("${state.key}/input", null)),
                    state.preferences.getString("${state.key}/inputEncoding", null)))
            assertNotNull(CppLessonInputDraft.review(state.preferences, state.key))
            assertNull(state.preferences.getString("${state.key}/inputSha256", null))
            assertUnrelatedUnchanged(activity, state)
        }
    }

    private fun assertConfirmed(host: ActivityScenario<LessonActivity>, state: State, expected: String) {
        host.onActivity { activity ->
            assertEquals(expected, input(activity).text.toString())
            assertEquals(expected, CppLessonInputDraft.read(state.preferences, state.key, "missing"))
            assertEquals(CppDraftTextCodec.ENCODING, state.preferences.getString("${state.key}/inputEncoding", null))
            assertEquals(expected, JSONObject(requireNotNull(state.preferences.getString("${state.key}/input", null))).getString("input"))
            assertEquals(CppLessonInputDraft.checksum(expected), state.preferences.getString("${state.key}/inputSha256", null))
            assertNull(CppLessonInputDraft.review(state.preferences, state.key))
            assertUnrelatedUnchanged(activity, state)
        }
    }

    private fun assertUnrelatedUnchanged(activity: LessonActivity, state: State) {
        assertEquals(state.sourceValues, state.preferences.all.filterKeys { it.startsWith("${state.key}/source") })
        val compilerSource = tree(content(activity)).filterIsInstance<FreeScrollEditText>()
            .first { it.tag == "cpp_code_input" }
        assertEquals(state.fixture.block.getString("initialCode"), compilerSource.text.toString())
        assertEquals(state.siblingValues, state.preferences.all.filterKeys { it.startsWith("programming/${state.fixture.id}/6/99/") })
        assertEquals(state.heartValues, context.getSharedPreferences("attempt_reserve_seconds_test", Context.MODE_PRIVATE).all)
        assertEquals(0, field<Int>(activity, "completedExercises"))
        val blocks = state.fixture.lesson.getJSONArray("pages").getJSONObject(6).getJSONArray("blocks")
        for (index in 0 until blocks.length()) if (blocks.getJSONObject(index).has("id"))
            assertNull("The review must never answer a neighboring puzzle",
                ProgressRepository.getExerciseState(context, state.fixture.id, 6, index))
        val current = field<JSONObject>(activity, "lessonJson").getJSONArray("pages").getJSONObject(6)
            .getJSONArray("blocks").getJSONObject(2)
        assertEquals(state.fixture.block.toString(), current.toString())
    }
}
