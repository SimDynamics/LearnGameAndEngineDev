package com.learngraphicsprogramming.app

import android.content.Context
import android.view.View
import android.view.inputmethod.EditorInfo
import android.widget.TextView
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

/** The original U2 reading laboratory and its original outputCheck are rendered
 * under an exclusive QA lesson identity. Source, input contract and expected stdout
 * are read from the installed curriculum; no code or expected answer is injected.
 */
@RunWith(AndroidJUnit4::class)
class CppPreparedInputLessonDeviceTest : CppOutputCheckQaSupport() {
    @Test fun authoredReadingInputRemainsExactThroughRecreationReopeningAndRealExecutions() {
        PremiumRepository.setVerifiedFree(context)
        assertFalse(AttemptRepository.getState(context, "programming").unlimited)
        val heartState = context.getSharedPreferences("attempt_reserve_seconds_test", Context.MODE_PRIVATE).all.toMap()
        val location = LabLocation("objetos_y_estado/lesson_leer_una_entrada_conocida.json", 7, 3)
        val original = source(location.path).getJSONArray("pages").getJSONObject(6).getJSONArray("blocks").getJSONObject(2)
        assertEquals("cpp_compiler", original.getString("type"))
        val contract = original.getJSONObject("outputCheck")
        val fixture = fixture(location, contract)
        assertEquals("The compiler block itself remains identical", original.toString(), fixture.block.toString())
        val initialSource = original.getString("initialCode")
        val draftKey = "programming/${fixture.id}/6/2"
        val preferences = context.getSharedPreferences("cpp_lesson_drafts", Context.MODE_PRIVATE)
        val observations = JSONArray()

        for (preparedInput in listOf("Rita 4", "Leo\n2\n", "Leo\n2\n    ")) {
            val specification = requireNotNull(CppOutputCheck.fromBlock(original))
            val expected = specification.caseFor(preparedInput)
            assertNotNull("Every test input must match the original curricular contract", expected)
            val stdout = requireNotNull(expected).outputs.first()
            var enteredText = ""
            withFixture(fixture) { host ->
                host.onActivity { activity ->
                    assertEquals(View.VISIBLE, input(activity).visibility)
                    assertEquals(initialSource, compilerEditor(activity).text.toString())
                    input(activity).setText("")
                    input(activity).setSelection(0)
                    val connection = requireNotNull(input(activity).onCreateInputConnection(EditorInfo()))
                    try { assertTrue(connection.commitText(preparedInput, 1)) } finally { connection.closeConnection() }
                    enteredText = input(activity).text.toString()
                    assertEquals(preparedInput, enteredText)
                    assertTrue("Flush the actual TextWatcher's preference write", preferences.edit().commit())
                    assertExactInput(activity, fixture, preparedInput, initialSource)
                }
                observations.put(execute(host, fixture, preparedInput, stdout, initialSource, heartState, "before_recreate"))
                host.recreate()
                restoreFixtureAfterRecreation(host, fixture)
                host.onActivity { activity ->
                    assertEquals(enteredText, input(activity).text.toString())
                    assertExactInput(activity, fixture, preparedInput, initialSource)
                }
                observations.put(execute(host, fixture, preparedInput, stdout, initialSource, heartState, "after_recreate"))
            }
            withFixture(fixture) { host ->
                host.onActivity { activity -> assertExactInput(activity, fixture, preparedInput, initialSource) }
                observations.put(execute(host, fixture, preparedInput, stdout, initialSource, heartState, "after_close_reopen"))
            }
            assertEquals(preparedInput, CppLessonInputDraft.read(preferences, draftKey, "missing"))
        }
        assertEquals("All original source content and output contracts are untouched", original.toString(),
            source(location.path).getJSONArray("pages").getJSONObject(6).getJSONArray("blocks").getJSONObject(2).toString())
        saveEvidence("prepared-input-exact-real-lesson", JSONObject().put("source", location.path)
            .put("page", 7).put("block", 3).put("originalBlock", original).put("runs", observations)
            .put("kind", "Actual LessonActivity, native EditText, Clang23 and WASI; Activity recreation and reopening")
            .put("processColdEvidence", "CppPreparedInputPersistenceDeviceTest separate-process phases"))
    }

    private fun assertExactInput(activity: LessonActivity, fixture: LabFixture, expected: String, source: String) {
        val key = "programming/${fixture.id}/6/2"
        val preferences = context.getSharedPreferences("cpp_lesson_drafts", Context.MODE_PRIVATE)
        assertEquals(expected, input(activity).text.toString())
        assertEquals(expected.length, input(activity).length())
        assertEquals(expected, CppLessonInputDraft.read(preferences, key, "missing"))
        assertEquals(expected, JSONObject(requireNotNull(preferences.getString("$key/input", null))).getString("input"))
        assertEquals(source, compilerEditor(activity).text.toString())
        assertEquals(source, CppLessonSourceDraft.read(preferences, key, source))
        assertSingleRunButton(activity, checking = true)
    }

    private fun execute(host: ActivityScenario<LessonActivity>, fixture: LabFixture, preparedInput: String, expected: String,
                        source: String, hearts: Map<String, *>, stage: String): JSONObject {
        var owner: LessonActivity? = null
        var ledger = ""
        host.onActivity { activity ->
            owner = activity
            ledger = field<ExerciseAttemptLedger>(activity, "exerciseAttempts").save().toString()
            assertExactInput(activity, fixture, preparedInput, source)
            assertTrue(runButton(activity).isEnabled)
            runButton(activity).performClick()
            assertFalse("A new native compilation/execution actually starts", runButton(activity).isEnabled)
        }
        await(host, "The native Clang23/WASI run completes", 120_000L) { activity ->
            assertSame(owner, activity)
            runButton(activity).isEnabled
        }
        var output = JSONObject()
        host.onActivity { activity ->
            assertExactInput(activity, fixture, preparedInput, source)
            assertEquals(activity.getString(R.string.cpp_runtime_finished, 0), (tagged(activity, "cpp_status") as TextView).text.toString())
            assertEquals("", (tagged(activity, "cpp_compilation_output") as TextView).text.toString())
            assertEquals(expected, (tagged(activity, "cpp_stdout") as TextView).text.toString())
            assertEquals("", (tagged(activity, "cpp_stderr") as TextView).text.toString())
            val matching = requireNotNull(CppOutputCheck.fromBlock(fixture.block)).caseFor(preparedInput)!!
            val explanation = matching.explanation
            val expectedVerdict = activity.getString(R.string.cpp_output_check_correct) +
                (explanation?.let { "\n$it" } ?: "")
            assertEquals(expectedVerdict, result(activity).text.toString())
            assertEquals(View.VISIBLE, result(activity).visibility)
            assertEquals(hearts, context.getSharedPreferences("attempt_reserve_seconds_test", Context.MODE_PRIVATE).all)
            assertEquals(0, field<Int>(activity, "completedExercises"))
            assertEquals(ledger, field<ExerciseAttemptLedger>(activity, "exerciseAttempts").save().toString())
            val blocks = fixture.lesson.getJSONArray("pages").getJSONObject(6).getJSONArray("blocks")
            for (index in 0 until blocks.length()) if (blocks.getJSONObject(index).has("id"))
                assertNull("A compiler run must not answer another activity", ProgressRepository.getExerciseState(context, fixture.id, 6, index))
            output = JSONObject().put("stage", stage).put("input", preparedInput).put("characters", preparedInput.length)
                .put("stdout", expected).put("sourceSha256", sha(source)).put("inputExact", true)
                .put("heartLedgerUnchanged", true).put("neighborExercisesUnanswered", true)
        }
        return output
    }

    private fun restoreFixtureAfterRecreation(host: ActivityScenario<LessonActivity>, fixture: LabFixture) {
        // Activity startup loads its published lesson from the unchanged Intent.
        // Reinstall only the same in-memory QA identity, then let the real renderer
        // recover that identity's persisted drafts, just as withFixture does on launch.
        await(host, "The published host reloads after Activity recreation") { activity ->
            field<String>(activity, "currentLessonId") == fixture.sourceLesson &&
                field<JSONObject>(activity, "lessonJson").optString("id") == fixture.sourceLesson &&
                activity.hasWindowFocus() && content(activity).width > 0 && !content(activity).isLayoutRequested
        }
        host.onActivity { activity ->
            setField(activity, "currentLessonId", fixture.id)
            setField(activity, "lessonJson", JSONObject(fixture.lesson.toString()))
            setField(activity, "currentPage", 6)
            setField(activity, "totalPages", fixture.lesson.getJSONArray("pages").length())
            listOf("pageViewCache", "pageRequiredExercises", "pageCompletedExercises").forEach {
                field<MutableMap<Int, Any>>(activity, it).clear()
            }
            invoke(activity, "renderCurrentPage")
        }
        await(host, "The recreated QA compiler has laid out") { activity ->
            assertEquals(fixture.id, field<String>(activity, "currentLessonId"))
            content(activity).width > 0 && !content(activity).isLayoutRequested
        }
    }

    private fun compilerEditor(activity: LessonActivity): FreeScrollEditText {
        val page = content(activity)
        var runChild: View = runButton(activity)
        while (runChild.parent !== page) runChild = runChild.parent as? View
            ?: error("The compiler Run control is not inside the lesson content")
        val card = (page.indexOfChild(runChild) - 1 downTo 0).asSequence().map(page::getChildAt)
            .first { child -> tree(child).any { it.tag == "cpp_code_input" } }
        // The following output_prediction legitimately has another identically
        // tagged editor. Select only the actual compiler card before its Run button.
        val matches = tree(card).filter { it.tag == "cpp_code_input" }
        assertEquals("The compiler card owns exactly one source editor", 1, matches.size)
        return matches.single() as? FreeScrollEditText
            ?: error("The selected compiler source is not the native code editor")
    }
}
