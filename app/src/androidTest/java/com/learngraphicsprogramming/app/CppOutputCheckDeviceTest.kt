package com.learngraphicsprogramming.app

import android.app.Activity
import android.app.Application
import android.app.KeyguardManager
import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.Color
import android.graphics.Rect
import android.os.Build
import android.os.Bundle
import android.os.SystemClock
import android.text.Spanned
import android.text.style.ForegroundColorSpan
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.Button
import android.widget.CheckBox
import android.widget.EditText
import android.widget.ScrollView
import android.widget.TextView
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.narration.NarrationSettingsRepository
import com.learngraphicsprogramming.app.narration.ReadingTextRange
import com.learngraphicsprogramming.app.runtime.CppCmakePhase
import com.learngraphicsprogramming.app.runtime.CppCmakeResult
import org.json.JSONArray
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.security.MessageDigest
import java.util.UUID

/** Real authored output-check integration plus isolated regression contracts on six laboratories. */
@RunWith(AndroidJUnit4::class)
class CppOutputCheckDeviceTest : CppOutputCheckQaSupport() {
    private var originalFiniteHeartState: Map<String, *> = emptyMap<String, Any?>()

    @Before fun requireFiniteHeartsAndRecordTheirUnmodifiedLedger() {
        PremiumRepository.setVerifiedFree(context)
        assertFalse("A Premium exemption must not conceal an accidental deduction",
            AttemptRepository.getState(context, "programming").unlimited)
        originalFiniteHeartState = context.getSharedPreferences("attempt_reserve_seconds_test", Context.MODE_PRIVATE).all.toMap()
    }

    @Test fun firstAuthoredLabUsesItsDeclaredCheckWithOneExistingRunButtonInStableRealWindows() {
        val location = LOCATIONS.first()
        val authored = authoredBlock(location)
        assertTrue("The requested real lesson declares the capability; this test must not invent an expected answer", authored.has("outputCheck"))
        val declared = authored.getJSONObject("outputCheck")
        val declaredCase = declared.getJSONArray("cases").getJSONObject(0)
        assertEquals("", declaredCase.getString("standardInput"))
        assertEquals(listOf("Caja recibida"), declaredCase.getJSONArray("outputs").let { values -> (0 until values.length()).map { values.getString(it) } })
        val explanation = declaredCase.optString("explanation").takeIf { it.isNotBlank() }
        val fixture = fixture(location, declared)
        val reference = source(location.path).getJSONArray("pages").getJSONObject(5).getJSONArray("blocks").getJSONObject(2).getString("code")
        val frames = JSONArray()
        withFixture(fixture) { host ->
            host.onActivity { activity ->
                assertEquals(fixture.block.getString("initialCode"), editor(activity).text.toString())
                assertEquals(fixture.block.optString("initialStandardInput"), input(activity).text.toString())
                assertEquals("The first output-only laboratory does not request a visible stdin field", View.GONE, input(activity).visibility)
                assertSingleRunButton(activity, checking = true)
                assertTrue(runButton(activity).isEnabled)
                assertEquals(View.GONE, result(activity).visibility)
                assertTrue(tree(content(activity)).filterIsInstance<TextView>().any {
                    it.text.toString() == source(location.path).getJSONArray("pages").getJSONObject(4)
                        .getJSONArray("blocks").let { blocks -> (0 until blocks.length()).map(blocks::getJSONObject)
                            .single { it.optString("id") == "prediccion_zona_inicial" }.getString("question") }
                })
                assertUnchangedExercises(activity, fixture)
            }
            frames.put(captureRealOutputWindow(host, "first-lab-before-run", fixture.block.getString("initialCode"), false))
            edit(host, reference)
            val execution = runAndCheck(host, fixture, "Caja recibida", true, explanation, "published_reference_for_window")
            frames.put(captureRealOutputWindow(host, "first-lab-after-check", reference, true))
            saveEvidence("first-lab-real-window", JSONObject().put("source", location.path)
                .put("page", fixture.location.page).put("block", fixture.location.number)
                .put("execution", execution).put("frames", frames)
                .put("kind", "Unmodified Android display screenshots, legal lessonScroll only; no Canvas composition"))
        }
    }

    @Test fun sixAuthoredLaboratoriesCheckActualExecutionsWithoutCompletingTheirPredictions() {
        val evidence = JSONArray()
        for ((index, location) in LOCATIONS.withIndex()) {
            val fixture = fixture(location, contracts(index))
            val expectedDraft = when (index) {
                0 -> program("Caja recibida")
                1 -> program("Informe\nZona Este\nFin\n")
                2 -> program("Revision completa")
                3 -> program("Taller\nTurno C\nMaterial preparado\n")
                else -> if (index == 4) fixture.block.getString("initialCode") else
                    source(location.path).getJSONArray("pages").getJSONObject(10).getJSONArray("blocks").getJSONObject(1).getString("code")
            }
            var finalInput = fixture.block.optString("initialStandardInput")
            withFixture(fixture) { host ->
                host.onActivity { activity ->
                    assertSingleRunButton(activity, checking = true)
                    assertEquals("Only authored input laboratories request stdin", if (index >= 4) View.VISIBLE else View.GONE, input(activity).visibility)
                    assertTrue(runButton(activity).isEnabled)
                    assertEquals(View.GONE, result(activity).visibility)
                    assertNull("Authored compiler blocks keep their absence of an ID", fixture.block.opt("id"))
                }
                when (index) {
                    0 -> {
                        evidence.put(runAndCheck(host, fixture, "Zona inicial", false, null, "original_initial"))
                        edit(host, program("Zona segura"))
                        evidence.put(runAndCheck(host, fixture, "Zona segura", false, GUIDE, "known_earlier_variation"))
                        edit(host, expectedDraft)
                        evidence.put(runAndCheck(host, fixture, "Caja recibida", true, EXPLANATION, "construction"))
                        // The same visible source after edit/undo still needs a new execution.
                        host.onActivity { activity ->
                            val original = editor(activity).text.toString()
                            editor(activity).append(" ")
                            assertTrue("Editing remains available without a second check button", runButton(activity).isEnabled)
                            assertEquals(View.GONE, result(activity).visibility)
                            editor(activity).setText(original)
                            assertTrue(runButton(activity).isEnabled)
                            assertEquals(View.GONE, result(activity).visibility)
                        }
                        evidence.put(runAndCheck(host, fixture, "Caja recibida", true, EXPLANATION, "new_run_after_restored_bytes"))
                    }
                    1 -> {
                        evidence.put(runAndCheck(host, fixture, "Resumen\nZona Oeste", false, null, "original_initial"))
                        edit(host, program("Informe\nZona Este\nFin"))
                        evidence.put(runAndCheck(host, fixture, "Informe\nZona Este\nFin", true, null, "without_optional_lf"))
                        edit(host, expectedDraft)
                        evidence.put(runAndCheck(host, fixture, "Informe\nZona Este\nFin\n", true, null, "with_optional_lf"))
                    }
                    2 -> {
                        evidence.put(runAndCheck(host, fixture, "Revision inicial", false, null, "original_initial"))
                        edit(host, program("Revision completa").replace(";", ""))
                        evidence.put(runTechnicalFailure(host, fixture, "compile_error"))
                        edit(host, expectedDraft)
                        evidence.put(runAndCheck(host, fixture, "Revision completa", true, null, "corrected_compilation"))
                    }
                    3 -> {
                        edit(host, program("Taller\nTurno B\nMaterial preparado"))
                        evidence.put(runAndCheck(host, fixture, "Taller\nTurno B\nMaterial preparado", true, null, "turno_b"))
                        edit(host, expectedDraft)
                        evidence.put(runAndCheck(host, fixture, "Taller\nTurno C\nMaterial preparado\n", true, null, "turno_c_optional_lf"))
                    }
                    4 -> {
                        evidence.put(runAndCheck(host, fixture, "Rita: 7", true, null, "rita"))
                        edit(host, expectedDraft, "Leo 2")
                        evidence.put(runAndCheck(host, fixture, "Leo: 5", true, null, "leo_spaces"))
                        finalInput = "Leo\n2\n"
                        edit(host, expectedDraft, finalInput)
                        evidence.put(runAndCheck(host, fixture, "Leo: 5", true, null, "leo_newlines"))
                        host.onActivity { activity -> input(activity).setText("Mia 3") }
                        evidence.put(runWithoutKnownCase(host, fixture, "Mia: 6"))
                        host.onActivity { activity -> input(activity).setText(finalInput) }
                    }
                    5 -> {
                        for ((stdin, expected) in listOf("Iris 4 2" to "Iris: 22", "Leo 0 4" to "Leo: 4", "Vera 3 1" to "Vera: 16", "Sol 20 4" to "Sol: 104")) {
                            edit(host, expectedDraft, stdin)
                            evidence.put(runAndCheck(host, fixture, expected, true, null, "three_inputs_$stdin"))
                            finalInput = stdin
                        }
                    }
                }
                assertNoExerciseChanges(host, fixture)
            }
            withFixture(fixture) { host -> host.onActivity { activity ->
                assertEquals("The actual lab draft survives reopening", expectedDraft, editor(activity).text.toString())
                assertEquals(finalInput, input(activity).text.toString())
                assertTrue("Reopening restores editable drafts and permits a fresh run", runButton(activity).isEnabled)
                assertEquals(View.GONE, result(activity).visibility)
            } }
        }
        saveEvidence("six-native-laboratories", JSONObject().put("executions", evidence))
    }

    @Test fun projectHeaderAndSourceSelectionInvalidateTheActualRunWhileFreeLabsKeepTheirNormalRunButton() {
        val original = LOCATIONS.first()
        val free = fixture(original, null)
        withFixture(free) { host -> host.onActivity { activity ->
            assertSingleRunButton(activity, checking = false)
            assertTrue(compilerViews(activity).none { it.tag == "cpp_output_check_result" })
        } }
        val fixture = fixture(original, contracts(0)).also { value ->
            value.block.put("projectFiles", JSONArray()
                .put(JSONObject().put("path", "main.cpp").put("initialCode", "#include <iostream>\n#include \"message.hpp\"\nint main() { std::cout << message; }\n"))
                .put(JSONObject().put("path", "message.hpp").put("initialCode", "inline constexpr auto message = \"Caja recibida\";\n")))
                .put("sourceFiles", JSONArray(listOf("main.cpp")))
        }
        val observations = JSONArray()
        withFixture(fixture) { host ->
            observations.put(runAndCheck(host, fixture, "Caja recibida", true, EXPLANATION, "project_initial"))
            host.onActivity { activity -> tagged(activity, "cpp_file_message.hpp").performClick() }
            edit(host, "inline constexpr auto message = \"Zona segura\";\n")
            host.onActivity { activity ->
                assertTrue("Changing a header permits rebuilding without locking the editor", runButton(activity).isEnabled)
                assertEquals(View.GONE, result(activity).visibility)
                tagged(activity, "cpp_file_main.cpp").performClick()
            }
            observations.put(runAndCheck(host, fixture, "Zona segura", false, GUIDE, "changed_header"))
            host.onActivity { activity ->
                val selected = tagged(activity, "cpp_source_enabled") as CheckBox
                assertTrue(selected.isChecked)
                selected.performClick()
                assertEquals(View.GONE, result(activity).visibility)
                selected.performClick()
                assertTrue(runButton(activity).isEnabled)
                assertEquals("Restoring translation units must never restore an old verdict", View.GONE, result(activity).visibility)
            }
            observations.put(runAndCheck(host, fixture, "Zona segura", false, GUIDE, "restored_translation_unit"))
            assertNoExerciseChanges(host, fixture)
        }
        saveEvidence("project-invalidation", JSONObject().put("executions", observations))
    }

    @Test fun bothModalitiesUseTheActualInterfaceTranslationsAndFreeLabsReallyExecute() {
        val originalLanguage = UiLanguageSettings.selectedTag(context)
        val observations = JSONArray()
        val translations = listOf(
            Triple("es", "Compilar y ejecutar", "Compilar, ejecutar y comprobar"),
            Triple("en", "Compile and Run", "Compile, run and check"),
            Triple("fr", "Compiler et exécuter", "Compiler, exécuter et vérifier"))
        try {
            for ((language, normalCaption, checkingCaption) in translations) {
                instrumentation.runOnMainSync { UiLanguageSettings.select(context, language) }
                val free = fixture(LOCATIONS.first(), null)
                withFixture(free) { host ->
                    host.onActivity { activity ->
                        assertSingleRunButton(activity, checking = false)
                        assertEquals(normalCaption, runButton(activity).text.toString())
                        assertEquals("An output-only free lab has no stdin field in $language", View.GONE, input(activity).visibility)
                        assertTrue(compilerViews(activity).none { it.tag == "cpp_output_check_result" })
                        runButton(activity).performClick()
                    }
                    await(host, "The ordinary compiler still executes in $language", 120_000L) { activity -> runButton(activity).isEnabled }
                    host.onActivity { activity ->
                        assertEquals("Zona inicial", (tagged(activity, "cpp_stdout") as TextView).text.toString())
                        assertEquals(activity.getString(R.string.cpp_runtime_finished, 0), (tagged(activity, "cpp_status") as TextView).text.toString())
                        assertTrue(compilerViews(activity).none { it.tag == "cpp_output_check_result" || it.tag == "cpp_check_output" })
                        assertUnchangedExercises(activity, free)
                        observations.put(JSONObject().put("language", language).put("mode", "compile_run")
                            .put("caption", runButton(activity).text.toString()).put("stdout", "Zona inicial"))
                    }
                }
                val checked = fixture(LOCATIONS.first(), contracts(0))
                withFixture(checked) { host ->
                    host.onActivity { activity ->
                        assertSingleRunButton(activity, checking = true)
                        assertEquals(checkingCaption, runButton(activity).text.toString())
                        assertEquals("An output-only checked lab has no stdin field in $language", View.GONE, input(activity).visibility)
                    }
                    val execution = runAndCheck(host, checked, "Zona inicial", false, null, "translated_combined_action")
                    host.onActivity { activity ->
                        val expected = when (language) { "es" -> "✗ Incorrecto"; "fr" -> "✗ Incorrect"; else -> "✗ Incorrect" }
                        assertEquals(expected, result(activity).text.toString())
                        observations.put(execution.put("language", language).put("mode", "compile_run_check")
                            .put("caption", runButton(activity).text.toString()))
                    }
                }
            }
        } finally { instrumentation.runOnMainSync { UiLanguageSettings.select(context, originalLanguage) } }
        saveEvidence("both-modalities-translated-native-runs", JSONObject().put("runs", observations))
    }

    @Test fun verdictFormattingMatchesPuzzlesAndKeepsTechnicalInkAndShadingInAllInterfaceLanguages() {
        val originalLanguage = UiLanguageSettings.selectedTag(context)
        val observations = JSONArray()
        try {
            for (language in listOf("es", "en", "fr")) {
                instrumentation.runOnMainSync { UiLanguageSettings.select(context, language) }
                val technical = "`std::cout << \"Caja recibida\";`"
                val explanation = when (language) {
                    "es" -> "La instrucción $technical conserva el literal."
                    "fr" -> "L’instruction $technical conserve le littéral."
                    else -> "The statement $technical preserves the literal."
                }
                val guidance = when (language) {
                    "es" -> "Revisa la instrucción $technical antes de ejecutar."
                    "fr" -> "Vérifie l’instruction $technical avant de l’exécuter."
                    else -> "Review the statement $technical before running it."
                }
                // Only these isolated QA cases add rich feedback; no curricular asset changes.
                val contract = JSONObject().put("cases", JSONArray().put(JSONObject()
                    .put("standardInput", "").put("outputs", JSONArray(listOf("Caja recibida")))
                    .put("explanation", explanation).put("reviewFeedback", JSONArray().put(JSONObject()
                        .put("outputs", JSONArray(listOf("Zona segura"))).put("text", guidance)))))
                val fixture = fixture(LOCATIONS.first(), contract)
                withFixture(fixture) { host ->
                    for (correct in listOf(true, false)) {
                        val stdout = if (correct) "Caja recibida" else "Zona segura"
                        edit(host, program(stdout))
                        runAndCheck(host, fixture, stdout, correct,
                            if (correct) explanation else guidance, "rich_format_$language")
                        host.onActivity { activity ->
                            val view = result(activity)
                            val text = view.text.toString()
                            val heading = if (language == "es") {
                                if (correct) "✓ Correcto" else "✗ Incorrecto"
                            } else if (correct) "✓ Correct" else "✗ Incorrect"
                            assertEquals(heading + (if (correct) "\n\n" else "\n") +
                                (if (correct) explanation else guidance), text)
                            fun dp(value: Int) = (value * activity.resources.displayMetrics.density + .5f).toInt()
                            assertEquals(dp(12), view.paddingTop)
                            assertEquals(dp(24), view.paddingBottom)
                            val ink = if (correct) Color.rgb(180, 220, 190) else Color.rgb(240, 150, 150)
                            assertEquals("The verdict keeps the existing success/error color", ink, view.currentTextColor)
                            val styled = view.text as? Spanned ?: error("Technical feedback needs its existing syntax spans")
                            val start = text.indexOf("std::cout")
                            val end = text.indexOf(';', start) + 1
                            assertTrue(start >= 0 && end > start)
                            val syntax = styled.getSpans(start, end, ForegroundColorSpan::class.java)
                            assertTrue("Technical syntax retains its console palette", syntax.any { it.foregroundColor != ink })
                            assertTrue("The same native text remains eligible for word shading", view.safeSupportsWordReading)
                            // Presentation compatibility only; actual audio timing is exercised
                            // separately by CppOutputCheckNarrationDeviceTest.
                            val pending = listOf(ReadingTextRange(start, end))
                            view.setUnreadWordRanges(pending, Color.rgb(110, 115, 125))
                            assertEquals(pending, field<List<ReadingTextRange>>(view.readingInk, "unreadRanges"))
                            assertSame("Shading never replaces source text or syntax spans", styled, view.text)
                            assertEquals(ink, view.currentTextColor)
                            view.setUnreadWordRanges(emptyList(), Color.rgb(110, 115, 125))
                            assertEquals(text, view.text.toString())
                            assertUnchangedExercises(activity, fixture)
                            observations.put(JSONObject().put("language", language).put("correct", correct)
                                .put("visibleResult", text).put("paddingTop", view.paddingTop)
                                .put("paddingBottom", view.paddingBottom).put("technicalSpans", syntax.size)
                                .put("shadingPreservesSourceAndSpans", true))
                        }
                    }
                }
            }
        } finally { instrumentation.runOnMainSync { UiLanguageSettings.select(context, originalLanguage) } }
        saveEvidence("verdict-puzzle-format-and-technical-ink", JSONObject().put("checks", observations)
            .put("scope", "Actual native compiler results and rendering; shading presentation compatibility, not simulated playback timing"))
    }

    @Test fun declaredInputVisibilityPreservesHiddenDraftsAndFeedsOnlyRequestedInput() {
        val observations = JSONArray()
        // This isolated QA program exposes whether the runtime received the persisted hidden draft.
        val eofProgram = "#include <iostream>\nint main() { int value = 0; if (std::cin >> value) std::cout << value; else std::cout << \"EOF\"; }\n"
        fun contract(stdin: String, expected: String) = JSONObject().put("cases", JSONArray().put(
            JSONObject().put("standardInput", stdin).put("outputs", JSONArray(listOf(expected)))))
        val hidden = fixture(LOCATIONS.first(), contract("", "EOF")).also {
            it.block.remove("initialStandardInput")
            it.block.put("initialCode", eofProgram)
        }
        val drafts = context.getSharedPreferences("cpp_lesson_drafts", Context.MODE_PRIVATE)
        val key = "programming/${hidden.id}/${hidden.location.page - 1}/${hidden.location.number - 1}/input"
        val preservedDraft = "917\n"
        assertTrue(drafts.edit().putString(key, preservedDraft).commit())
        repeat(2) { reopening ->
            withFixture(hidden) { host ->
                host.onActivity { activity ->
                    assertEquals("No source-code inference may expose stdin without curricular metadata", View.GONE, input(activity).visibility)
                    assertEquals("Hiding an old draft must not erase it", preservedDraft, input(activity).text.toString())
                    assertEquals(CppDraftTextCodec.encode(preservedDraft, "input"), drafts.getString(key, null))
                    assertEquals(CppDraftTextCodec.ENCODING,
                        drafts.getString(key.removeSuffix("/input") + "/inputEncoding", null))
                }
                observations.put(runAndCheck(host, hidden, "EOF", true, null, "hidden_input_eof_$reopening")
                    .put("storedHiddenDraft", preservedDraft).put("effectiveStandardInput", ""))
                host.onActivity { activity ->
                    tagged(activity, "cpp_reset").performClick()
                    assertEquals("Resetting a task without input also preserves its hidden draft", preservedDraft, input(activity).text.toString())
                    assertEquals(CppDraftTextCodec.encode(preservedDraft, "input"), drafts.getString(key, null))
                    assertUnchangedExercises(activity, hidden)
                }
            }
        }
        val explicitEmpty = fixture(LOCATIONS.first(), contract("", "EOF")).also {
            it.block.put("initialStandardInput", "").put("initialCode", eofProgram)
        }
        withFixture(explicitEmpty) { host ->
            host.onActivity { activity ->
                assertEquals("An explicitly declared empty input still requests the input control", View.VISIBLE, input(activity).visibility)
                assertEquals("", input(activity).text.toString())
            }
            observations.put(runAndCheck(host, explicitEmpty, "EOF", true, null, "explicit_empty_input_is_visible"))
            host.onActivity { activity -> input(activity).setText("23") }
            observations.put(runWithoutKnownCase(host, explicitEmpty, "23"))
        }
        val contractInput = fixture(LOCATIONS.first(), contract("23", "23")).also {
            it.block.remove("initialStandardInput")
            it.block.put("initialCode", eofProgram)
        }
        withFixture(contractInput) { host ->
            host.onActivity { activity ->
                assertEquals("A nonempty input case is sufficient curricular metadata", View.VISIBLE, input(activity).visibility)
                input(activity).setText("23")
            }
            observations.put(runAndCheck(host, contractInput, "23", true, null, "contract_declares_visible_input"))
        }
        assertEquals("Every run and reset preserved the exact escaped hidden draft",
            CppDraftTextCodec.encode(preservedDraft, "input"), drafts.getString(key, null))
        saveEvidence("metadata-input-visibility-and-hidden-draft", JSONObject().put("runs", observations)
            .put("hiddenDraftPreserved", true).put("inputVisibilityDependsOnMetadataOnly", true))
    }

    @Test fun editsAndCachedPageChangesInvalidateFreshRunTicketsButNeverPreventCorrection() {
        val fixture = fixture(LOCATIONS.first(), contracts(0))
        val observations = JSONArray()
        val correctProgram = program("Caja recibida")
        withFixture(fixture) { host ->
            edit(host, correctProgram)
            host.onActivity { activity ->
                runButton(activity).performClick()
                assertFalse(runButton(activity).isEnabled)
                editor(activity).append(" ")
                editor(activity).setText(correctProgram)
            }
            await(host, "The native run captured before edit/undo completes", 120_000L) { activity -> runButton(activity).isEnabled }
            host.onActivity { activity ->
                assertEquals("Caja recibida", (tagged(activity, "cpp_stdout") as TextView).text.toString())
                assertEquals("Even restoring identical bytes cannot reuse an earlier run ticket", View.GONE, result(activity).visibility)
                assertEquals(correctProgram, editor(activity).text.toString())
                assertUnchangedExercises(activity, fixture)
            }
            observations.put(runAndCheck(host, fixture, "Caja recibida", true, EXPLANATION, "fresh_run_after_edit_undo"))
            host.onActivity { activity ->
                val selectedAction = runButton(activity)
                setField(activity, "currentPage", fixture.location.page)
                invoke(activity, "renderCurrentPage")
                assertFalse("The checked compiler is detached on the next authored page",
                    tree(content(activity)).any { it === selectedAction })
            }
            host.onActivity { activity ->
                setField(activity, "currentPage", fixture.location.page - 1)
                invoke(activity, "renderCurrentPage")
                assertSingleRunButton(activity, checking = true)
                assertEquals(correctProgram, editor(activity).text.toString())
                assertTrue("Returning to the cached editor permits compiling", runButton(activity).isEnabled)
                assertEquals("A cached page must not redisplay its previous verdict", View.GONE, result(activity).visibility)
                runButton(activity).performClick()
                // Both real render/detach transitions occur before the native callback can run.
                setField(activity, "currentPage", fixture.location.page)
                invoke(activity, "renderCurrentPage")
                setField(activity, "currentPage", fixture.location.page - 1)
                invoke(activity, "renderCurrentPage")
            }
            await(host, "The run crossing cached-page detach/reattach completes", 120_000L) { activity -> runButton(activity).isEnabled }
            host.onActivity { activity ->
                assertEquals("A run started before changing page cannot grade the returned page", View.GONE, result(activity).visibility)
                assertUnchangedExercises(activity, fixture)
            }
            observations.put(runAndCheck(host, fixture, "Caja recibida", true, EXPLANATION, "fresh_run_after_returning_to_cached_page"))
            lateinit var inactiveRunButton: Button
            host.onActivity { activity ->
                inactiveRunButton = runButton(activity)
                inactiveRunButton.performClick()
                assertFalse(inactiveRunButton.isEnabled)
                setField(activity, "currentPage", fixture.location.page)
                invoke(activity, "renderCurrentPage")
                assertFalse("The checked compiler stays detached while another page is visible",
                    tree(content(activity)).any { it === inactiveRunButton })
            }
            await(host, "The native run releases its busy button even while its cached page is detached", 120_000L) { activity ->
                assertEquals("Wait on the other page until the native callback has completed", fixture.location.page, field<Int>(activity, "currentPage"))
                inactiveRunButton.isEnabled
            }
            host.onActivity { activity ->
                setField(activity, "currentPage", fixture.location.page - 1)
                invoke(activity, "renderCurrentPage")
                assertSame("Returning reuses the actual cached compiler action", inactiveRunButton, runButton(activity))
                assertTrue("Finishing while detached must never leave the cached compiler blocked", runButton(activity).isEnabled)
                assertEquals("The detached execution is not a fresh check after returning", View.GONE, result(activity).visibility)
                assertEquals(correctProgram, editor(activity).text.toString())
                assertUnchangedExercises(activity, fixture)
            }
            observations.put(runAndCheck(host, fixture, "Caja recibida", true, EXPLANATION, "fresh_run_after_inactive_completion"))
            edit(host, "int main() { return 7; }\n")
            host.onActivity { activity -> runButton(activity).performClick() }
            await(host, "A genuine nonzero native execution completes", 120_000L) { activity -> runButton(activity).isEnabled }
            host.onActivity { activity ->
                assertEquals(activity.getString(R.string.cpp_execution_failed, 7), (tagged(activity, "cpp_status") as TextView).text.toString())
                assertEquals("Execution errors are diagnostics, never an incorrect answer", View.GONE, result(activity).visibility)
                assertUnchangedExercises(activity, fixture)
                observations.put(JSONObject().put("stage", "runtime_error_no_verdict").put("exitCode", 7))
            }
            edit(host, correctProgram)
            observations.put(runAndCheck(host, fixture, "Caja recibida", true, EXPLANATION, "fresh_run_after_runtime_failure"))
        }
        saveEvidence("page-and-inflight-invalidation", JSONObject().put("runs", observations))
    }

    @Test fun cmakeChecksOnlyFreshRealConfigureBuildAndRunResultsWhileKeepingCorrectionsAvailable() {
        assertTrue("Real Android CMake requires API 28 or later", Build.VERSION.SDK_INT >= 28)
        // Use the existing native CMake regression's two-source inventory project.
        // Only this in-memory QA block adds an explicit stdout oracle.
        val cmake = """
            cmake_minimum_required(VERSION 3.20)
            project(Existencias LANGUAGES CXX)
            option(USAR_COMPARACION_ESTRICTA "Comparar sin incluir la igualdad" OFF)
            add_executable(consulta main.cpp existencias.cpp)
            target_compile_features(consulta PRIVATE cxx_std_20)
            set_target_properties(consulta PROPERTIES CXX_EXTENSIONS OFF)
            if(USAR_COMPARACION_ESTRICTA)
                target_compile_definitions(consulta PRIVATE LGP_STRICT_RESTOCK=1)
            endif()
            add_executable(no_construir fuente_otro_objetivo.cpp)
        """.trimIndent() + "\n"
        val implementation = """
            #include "existencias.h"
            bool debe_reponer(int disponibles, int minimo) {
            #ifdef LGP_STRICT_RESTOCK
                return disponibles < minimo;
            #else
                return disponibles <= minimo;
            #endif
            }
        """.trimIndent() + "\n"
        val files = linkedMapOf(
            "CMakeLists.txt" to cmake,
            "existencias.h" to "#ifndef QA_EXISTENCIAS_H\n#define QA_EXISTENCIAS_H\nbool debe_reponer(int disponibles, int minimo);\n#endif\n",
            "existencias.cpp" to implementation,
            "main.cpp" to "#include \"existencias.h\"\n#include <iostream>\nint main() {\n    std::cout << debe_reponer(4, 5) << ':' << debe_reponer(5, 5) << ':' << debe_reponer(6, 5);\n    return 0;\n}\n",
            "fuente_otro_objetivo.cpp" to "este objetivo no debe construirse como parte de consulta;\n")
        val contract = JSONObject().put("cases", JSONArray().put(JSONObject().put("standardInput", "")
            .put("outputs", JSONArray(listOf("1:1:0")))))
        val fixture = fixture(LOCATIONS.first(), contract)
        fixture.block.remove("sourceFiles")
        fixture.block.put("initialCode", "").put("initialStandardInput", "")
            .put("projectFiles", JSONArray().apply { files.forEach { (path, code) ->
                put(JSONObject().put("path", path).put("initialCode", code))
            } }).put("cmake", JSONObject().put("target", "consulta").put("buildType", "Debug")
                .put("options", JSONObject().put("USAR_COMPARACION_ESTRICTA", "OFF")))
        val observations = JSONArray()
        withFixture(fixture) { host ->
            host.onActivity { activity ->
                assertSingleRunButton(activity, checking = true)
                assertTrue("A new project can compile/run/check without any pre-existing built artifact", runButton(activity).isEnabled)
                assertNull(tagged(activity, "cpp_status").getTag(R.id.cpp_lab_cmake_result))
                assertEquals(View.GONE, result(activity).visibility)
            }
            fun execute(stage: String, expected: String?, correct: Boolean?): CppCmakeResult {
                host.onActivity { activity ->
                    assertSingleRunButton(activity, checking = true)
                    assertTrue(runButton(activity).isEnabled)
                    runButton(activity).performClick()
                    assertFalse(runButton(activity).isEnabled)
                    assertEquals(View.GONE, result(activity).visibility)
                }
                await(host, "The actual CMake pipeline finishes: $stage", 180_000L) { activity ->
                    runButton(activity).isEnabled && tagged(activity, "cpp_status")
                        .getTag(R.id.cpp_lab_cmake_result) is CppCmakeResult
                }
                lateinit var actual: CppCmakeResult
                host.onActivity { activity ->
                    actual = tagged(activity, "cpp_status").getTag(R.id.cpp_lab_cmake_result) as CppCmakeResult
                    assertTrue("CMake genuinely generated Ninja rules", File(actual.workspace, "build/build.ninja").isFile)
                    assertTrue("The requested target is built by CMake, never by a direct cpp shortcut", actual.records.any { record ->
                        record.phase == CppCmakePhase.BUILD && "--build" in record.command &&
                            record.command.windowed(2).any { it == listOf("--target", "consulta") }
                    })
                    if (expected != null) {
                        assertEquals(CppCmakePhase.EXECUTION, actual.phase)
                        assertEquals(0, actual.exitCode)
                        assertNotNull(actual.execution)
                        assertEquals(expected, actual.execution!!.stdout)
                        assertEquals(expected, (tagged(activity, "cpp_stdout") as TextView).text.toString())
                        assertTrue(requireNotNull(actual.artifact).isFile)
                        assertEquals(expectedVerdict(activity, correct == true), result(activity).text.toString())
                        assertEquals(View.VISIBLE, result(activity).visibility)
                    } else {
                        assertEquals(CppCmakePhase.BUILD, actual.phase)
                        assertNotEquals(0, actual.exitCode)
                        assertNull(actual.execution)
                        assertEquals(View.GONE, result(activity).visibility)
                        assertTrue((tagged(activity, "cpp_compilation_output") as TextView).text.isNotBlank())
                    }
                    assertUnchangedExercises(activity, fixture)
                    observations.put(JSONObject().put("stage", stage).put("phase", actual.phase.name)
                        .put("exitCode", actual.exitCode).put("stdout", actual.execution?.stdout ?: JSONObject.NULL)
                        .put("verdict", result(activity).text.toString()).put("verdictVisibility", result(activity).visibility)
                        .put("projectFingerprint", actual.projectFingerprint)
                        .put("artifactSha256", actual.artifact?.takeIf(File::isFile)?.readBytes()?.let { bytes ->
                            MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it) }
                        } ?: JSONObject.NULL)
                        .put("records", JSONArray().apply { actual.records.forEach { record -> put(JSONObject()
                            .put("phase", record.phase.name).put("exitCode", record.exitCode)
                            .put("command", JSONArray(record.command)).put("stdout", record.stdout).put("stderr", record.stderr)) } }))
                }
                return actual
            }
            val first = execute("new_project_one_click_configures_builds_and_checks", "1:1:0", true)
            assertTrue(first.records.any { it.phase == CppCmakePhase.CONFIGURATION })
            assertTrue(first.records.any { it.phase == CppCmakePhase.GENERATION })
            host.onActivity { activity -> tagged(activity, "cpp_file_existencias.cpp").performClick() }
            edit(host, implementation.replace("disponibles <= minimo", "disponibles < minimo"))
            val edited = execute("source_edit_incrementally_builds_new_behavior", "1:0:0", false)
            assertFalse("Source-only edits reuse valid generation", edited.records.any {
                it.phase == CppCmakePhase.CONFIGURATION || it.phase == CppCmakePhase.GENERATION })
            assertNotEquals(first.projectFingerprint, edited.projectFingerprint)
            edit(host, implementation + "\nesto no es C++;\n")
            execute("genuine_build_error_has_no_verdict", null, null)
            edit(host, implementation)
            execute("corrected_source_builds_and_checks_again", "1:1:0", true)
            host.onActivity { activity ->
                val selectedAction = runButton(activity)
                setField(activity, "currentPage", fixture.location.page)
                invoke(activity, "renderCurrentPage")
                assertFalse("The selected CMake lab is detached even if the next page has another lab",
                    tree(content(activity)).any { it === selectedAction })
            }
            host.onActivity { activity ->
                setField(activity, "currentPage", fixture.location.page - 1)
                invoke(activity, "renderCurrentPage")
            }
            await(host, "The cached CMake page reattaches without rechecking old output") { activity ->
                runButton(activity).isEnabled && editor(activity).isAttachedToWindow
            }
            host.onActivity { activity ->
                assertSingleRunButton(activity, checking = true)
                assertEquals("Observer replay is not a new requested output check", View.GONE, result(activity).visibility)
                assertUnchangedExercises(activity, fixture)
            }
            execute("fresh_pipeline_after_cached_page_return", "1:1:0", true)
        }
        saveEvidence("cmake-real-compile-run-check", JSONObject().put("runs", observations)
            .put("fixture", "Existing two-source native CMake inventory project; stdout oracle is QA-only"))
    }

    private fun edit(host: ActivityScenario<LessonActivity>, code: String, stdin: String? = null) = host.onActivity { activity ->
        editor(activity).setText(code)
        stdin?.let { input(activity).setText(it) }
        assertTrue("Every source/input edit permits a new run", runButton(activity).isEnabled)
        assertEquals("Every source/input edit invalidates the last verdict", View.GONE, result(activity).visibility)
    }
    private fun runAndCheck(host: ActivityScenario<LessonActivity>, fixture: LabFixture, expected: String,
                            correct: Boolean, explanation: String?, stage: String): JSONObject {
        val evidence = run(host, fixture, expected)
        host.onActivity { activity ->
            assertTrue("A completed check never prevents another execution", runButton(activity).isEnabled)
            assertSingleRunButton(activity, checking = true)
            assertEquals(expectedVerdict(activity, correct, explanation), result(activity).text.toString())
            assertEquals(View.VISIBLE, result(activity).visibility)
            evidence.put("visibleResult", result(activity).text.toString()).put("stage", stage)
            assertUnchangedExercises(activity, fixture)
        }
        return evidence
    }
    private fun runWithoutKnownCase(host: ActivityScenario<LessonActivity>, fixture: LabFixture, expected: String): JSONObject {
        val evidence = run(host, fixture, expected)
        host.onActivity { activity -> assertTrue(runButton(activity).isEnabled); assertEquals(View.GONE, result(activity).visibility) }
        return evidence.put("stage", "input_not_in_declared_cases_no_verdict")
    }
    private fun runTechnicalFailure(host: ActivityScenario<LessonActivity>, fixture: LabFixture, stage: String): JSONObject {
        var owner: LessonActivity? = null
        host.onActivity { activity -> owner = activity; tagged(activity, "cpp_run").performClick() }
        await(host, "The real diagnostic operation completes", 120_000L) { activity ->
            assertSame(owner, activity); tagged(activity, "cpp_run").isEnabled
        }
        var evidence = JSONObject()
        host.onActivity { activity ->
            assertTrue("A genuine native compiler diagnostic is retained", (tagged(activity, "cpp_compilation_output") as TextView).text.isNotBlank())
            assertTrue(runButton(activity).isEnabled)
            assertEquals(View.GONE, result(activity).visibility)
            evidence = JSONObject().put("stage", stage).put("diagnostics", (tagged(activity, "cpp_compilation_output") as TextView).text.toString())
            assertUnchangedExercises(activity, fixture)
        }
        return evidence
    }
    private fun run(host: ActivityScenario<LessonActivity>, fixture: LabFixture, expected: String): JSONObject {
        var owner: LessonActivity? = null
        host.onActivity { activity ->
            owner = activity
            assertTrue(tagged(activity, "cpp_run").isEnabled)
            tagged(activity, "cpp_run").performClick()
            assertFalse(tagged(activity, "cpp_run").isEnabled)
            assertEquals("Starting a new execution clears the old verdict", View.GONE, result(activity).visibility)
        }
        await(host, "This native Clang 23 execution completes", 120_000L) { activity ->
            assertSame(owner, activity); tagged(activity, "cpp_run").isEnabled
        }
        var evidence = JSONObject()
        host.onActivity { activity ->
            assertEquals(activity.getString(R.string.cpp_runtime_finished, 0), (tagged(activity, "cpp_status") as TextView).text.toString())
            assertEquals("", (tagged(activity, "cpp_compilation_output") as TextView).text.toString())
            assertEquals(expected, (tagged(activity, "cpp_stdout") as TextView).text.toString())
            assertEquals("", (tagged(activity, "cpp_stderr") as TextView).text.toString())
            assertUnchangedExercises(activity, fixture)
            evidence = JSONObject().put("source", fixture.location.path).put("page", fixture.location.page)
                .put("block", fixture.location.number)
                .put("stdin", if (input(activity).visibility == View.VISIBLE) input(activity).text.toString() else "")
                .put("storedInputDraft", input(activity).text.toString()).put("stdout", expected)
                .put("sourceSha256", sha(editor(activity).text.toString())).put("compiler", "Clang23/default")
        }
        return evidence
    }
    private fun assertNoExerciseChanges(host: ActivityScenario<LessonActivity>, fixture: LabFixture) =
        host.onActivity { activity -> assertUnchangedExercises(activity, fixture) }

    /** Observe four pixel-identical frames after legal page scrolling. The floating reader
     * retains its actual visibility and position; its occupied vertical interval is excluded. */
    private fun captureRealOutputWindow(host: ActivityScenario<LessonActivity>, name: String,
                                        expectedCode: String, checked: Boolean): JSONObject {
        var consecutive = 0
        var samples = 0
        var last: Bitmap? = null
        var prior: Bitmap? = null
        var metadata = JSONObject()
        val deadline = SystemClock.uptimeMillis() + 12_000L
        try {
            while (SystemClock.uptimeMillis() < deadline && consecutive < 4) {
                instrumentation.waitForIdleSync()
                var safe = Rect()
                var group = Rect()
                var ready = false
                host.onActivity { activity ->
                    val scroll = activity.findViewById<ScrollView>(R.id.lessonScroll)
                    assertTrue(scroll.getGlobalVisibleRect(safe))
                    val reader = activity.findViewById<View>(R.id.lessonNarrationControls)
                    val readerRect = Rect()
                    val readerVisible = reader != null && reader.alpha > 0f && reader.getGlobalVisibleRect(readerRect)
                    val margin = (8 * activity.resources.displayMetrics.density).toInt()
                    if (readerVisible && Rect.intersects(safe, readerRect)) {
                        val above = readerRect.top - safe.top - margin
                        val below = safe.bottom - readerRect.bottom - margin
                        if (above >= below) safe.bottom = readerRect.top - margin else safe.top = readerRect.bottom + margin
                    }
                    assertTrue("A real viewport remains without moving or hiding the reader", safe.width() > 0 && safe.height() > 0)
                    val console = tagged(activity, "cpp_console")
                    val child = requireNotNull(scroll.getChildAt(0))
                    val margins = (child.layoutParams as? ViewGroup.MarginLayoutParams)?.let { it.topMargin + it.bottomMargin } ?: 0
                    val limit = (child.height + margins + scroll.paddingTop + scroll.paddingBottom - scroll.height).coerceAtLeast(0)
                    val runPosition = IntArray(2).also(runButton(activity)::getLocationOnScreen)
                    val desired = (scroll.scrollY + runPosition[1] - safe.top - margin).coerceIn(0, limit)
                    if (scroll.scrollY != desired) scroll.scrollTo(0, desired)
                    fun bounds(view: View): Rect {
                        val position = IntArray(2).also(view::getLocationOnScreen)
                        return Rect(position[0], position[1], position[0] + view.width, position[1] + view.height)
                    }
                    val consoleBounds = bounds(console)
                    val buttonBounds = bounds(runButton(activity))
                    val resultBounds = if (checked) bounds(result(activity)) else Rect()
                    group = Rect(consoleBounds).apply { union(buttonBounds); if (checked) union(resultBounds) }
                    assertTrue("The original run button stays before the original console", buttonBounds.bottom <= consoleBounds.top)
                    assertSingleRunButton(activity, checking = true)
                    assertEquals(activity.getString(R.string.cpp_program_stdout), (tagged(activity, "cpp_console_title") as TextView).text.toString())
                    assertEquals(expectedCode, editor(activity).text.toString())
                    assertEquals("", input(activity).text.toString())
                    assertTrue("The single run action remains usable after every result", runButton(activity).isEnabled)
                    assertEquals(if (checked) View.VISIBLE else View.GONE, result(activity).visibility)
                    if (checked) {
                        assertEquals("Caja recibida", (tagged(activity, "cpp_console_display") as TextView).text.toString())
                        assertTrue(result(activity).text.toString().startsWith(activity.getString(R.string.lesson_correct_feedback)))
                    }
                    ready = activity.hasWindowFocus() && !scroll.isLayoutRequested && !console.isLayoutRequested &&
                        !runButton(activity).isLayoutRequested && (!checked || !result(activity).isLayoutRequested) && safe.contains(group)
                    fun json(rect: Rect) = JSONArray(listOf(rect.left, rect.top, rect.right, rect.bottom))
                    metadata = JSONObject().put("safeViewport", json(safe)).put("consoleBounds", json(consoleBounds))
                        .put("buttonBounds", json(buttonBounds)).put("resultBounds", json(resultBounds))
                        .put("scrollY", scroll.scrollY).put("maxLegalScrollY", limit).put("buttonEnabled", runButton(activity).isEnabled)
                        .put("visibleResult", result(activity).text.toString()).put("stdout", (tagged(activity, "cpp_stdout") as TextView).text.toString())
                        .put("readerVisible", readerVisible).put("readerBounds", json(readerRect)).put("readerHiddenOrMoved", false)
                        .put("sourceSha256", sha(expectedCode)).put("stdin", input(activity).text.toString())
                        .put("neighborPredictionRemainsUnanswered", ProgressRepository.getExerciseState(context, field<String>(activity, "currentLessonId"), 4, 3) == null)
                }
                val image = requireNotNull(instrumentation.uiAutomation.takeScreenshot())
                assertTrue(group.left >= 0 && group.top >= 0 && group.right <= image.width && group.bottom <= image.height)
                val crop = Bitmap.createBitmap(image, group.left, group.top, group.width(), group.height())
                consecutive = if (ready && prior?.sameAs(crop) == true) consecutive + 1 else if (ready) 1 else 0
                prior?.recycle(); prior = crop
                last?.recycle(); last = image
                samples++
                if (consecutive < 4) SystemClock.sleep(100L)
            }
            assertTrue("Console, complete button and displayed result fit in four stable native frames", consecutive >= 4)
            val folder = File(requireNotNull(context.getExternalFilesDir(null)), "cpp-output-check-qa/$session").apply { mkdirs() }
            val file = File(folder, "$name.png")
            file.outputStream().use { assertTrue(requireNotNull(last).compress(Bitmap.CompressFormat.PNG, 100, it)) }
            val bytes = file.readBytes()
            return metadata.put("file", file.name).put("kind", "native-window-screenshot")
                .put("imageSha256", MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it) })
                .put("stableConsecutiveFrames", consecutive).put("sampledFrames", samples).put("audioTimingEvidence", false)
        } finally { prior?.recycle(); last?.recycle() }
    }

    private fun assertUnchangedExercises(activity: LessonActivity, fixture: LabFixture) {
        assertFalse(PremiumRepository.isPremium(context))
        assertEquals("Correct, incorrect, technical and repeated checks never write the finite-heart balance",
            originalFiniteHeartState, context.getSharedPreferences("attempt_reserve_seconds_test", Context.MODE_PRIVATE).all)
        assertEquals("Checking stdout never completes a neighboring puzzle", 0, field<Int>(activity, "completedExercises"))
        assertEquals("No evaluated attempts for unscored compiler checks", 0,
            field<ExerciseAttemptLedger>(activity, "exerciseAttempts").save().getJSONArray("exercises").length())
        val authored = source(fixture.location.path).getJSONArray("pages").getJSONObject(fixture.location.page - 1).getJSONArray("blocks")
        for (index in 0 until authored.length()) if (authored.getJSONObject(index).optString("type") == "output_prediction")
            assertNull("The nearby prediction remains unanswered", ProgressRepository.getExerciseState(context, fixture.id, fixture.location.page - 1, index))
    }
    companion object {
        const val GUIDE = "Revisa si realizaste y ejecutaste la segunda modificación."
        const val EXPLANATION = "Las comillas delimitan el literal, pero no forman parte del mensaje."
        val LOCATIONS = listOf(
            LabLocation("programas_y_ejecucion/lesson_primer_programa_y_salida.json", 5, 3),
            LabLocation("programas_y_ejecucion/lesson_texto_secuencia_y_formato.json", 7, 3),
            LabLocation("programas_y_ejecucion/lesson_fuente_compilacion_y_diagnostico.json", 4, 4),
            LabLocation("programas_y_ejecucion/lesson_resolver_probar_y_recuperar.json", 4, 3),
            LabLocation("objetos_y_estado/lesson_leer_una_entrada_conocida.json", 7, 3),
            LabLocation("objetos_y_estado/lesson_leer_una_entrada_conocida.json", 10, 4))
        fun program(output: String): String = "#include <iostream>\n\nint main() {\n    std::cout << " + JSONObject.quote(output) + ";\n    return 0;\n}\n"
        fun contracts(index: Int): JSONObject {
            fun case(input: String, vararg outputs: String) = JSONObject().put("standardInput", input).put("outputs", JSONArray(outputs.toList()))
            val cases = when (index) {
                0 -> listOf(case("", "Caja recibida").put("explanation", EXPLANATION).put("reviewFeedback", JSONArray()
                    .put(JSONObject().put("outputs", JSONArray(listOf("Zona segura"))).put("text", GUIDE))))
                1 -> listOf(case("", "Informe\nZona Este\nFin", "Informe\nZona Este\nFin\n"))
                2 -> listOf(case("", "Revision completa"))
                3 -> listOf(case("", "Taller\nTurno B\nMaterial preparado", "Taller\nTurno B\nMaterial preparado\n",
                    "Taller\nTurno C\nMaterial preparado", "Taller\nTurno C\nMaterial preparado\n"))
                4 -> listOf(case("Rita 4", "Rita: 7"), case("Leo 2", "Leo: 5"))
                else -> listOf(case("Iris 4 2", "Iris: 22"), case("Leo 0 4", "Leo: 4"), case("Vera 3 1", "Vera: 16"), case("Sol 20 4", "Sol: 104"))
            }
            return JSONObject().put("inputComparison", if (index >= 4) "tokens" else "exact").put("cases", JSONArray(cases))
        }
    }
}

/** Shared fixture and state guard for the new native output-check tests only. */
abstract class CppOutputCheckQaSupport {
    protected val instrumentation = InstrumentationRegistry.getInstrumentation()
    protected val context = instrumentation.targetContext
    protected val session = UUID.randomUUID().toString()
    private val preferences = linkedMapOf<String, Map<String, *>>()
    private var original = emptySet<String>()
    private var preserved = false
    private var activeLab: LabLocation? = null
    private val guard = object : Application.ActivityLifecycleCallbacks {
        override fun onActivityCreated(activity: Activity, state: Bundle?) { activity.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON) }
        override fun onActivityStarted(activity: Activity) = Unit
        override fun onActivityResumed(activity: Activity) = Unit
        override fun onActivityPaused(activity: Activity) = Unit
        override fun onActivityStopped(activity: Activity) = Unit
        override fun onActivitySaveInstanceState(activity: Activity, state: Bundle) = Unit
        override fun onActivityDestroyed(activity: Activity) = Unit
    }
    @Before fun preserveEveryOriginalQaValue() {
        assertEquals("Never touch the normal student app", "com.learngraphicsprogramming.app.realdownloadsqa", context.packageName)
        assertTrue("LDPlayer only", Build.SUPPORTED_ABIS.any { it == "x86" || it == "x86_64" })
        assertTrue(BuildConfig.LGP_CLANG23_ENABLED)
        val lock = context.getSystemService(KeyguardManager::class.java)
        assertFalse(lock.isDeviceLocked || lock.isKeyguardLocked)
        original = preferenceFiles()
        (original + setOf("learning_progress", "cpp_lesson_drafts", "narration_settings", "app_settings")).forEach { name ->
            preferences[name] = context.getSharedPreferences(name, Context.MODE_PRIVATE).all.mapValues { (_, value) -> if (value is Set<*>) value.toSet() else value }
        }
        preserved = true
        (context.applicationContext as Application).registerActivityLifecycleCallbacks(guard)
        NarrationSettingsRepository.saveEnabled(context, false)
        PremiumRepository.setVerifiedPremium(context)
    }
    @After fun restoreEveryOriginalQaValue() {
        if (!preserved) return
        instrumentation.waitForIdleSync()
        try {
            (preferenceFiles() + preferences.keys).forEach { name ->
                if (name !in original) context.deleteSharedPreferences(name)
                else {
                    val prefs = context.getSharedPreferences(name, Context.MODE_PRIVATE)
                    val edit = prefs.edit().clear()
                    preferences.getValue(name).forEach { (key, value) -> when (value) {
                        is String -> edit.putString(key, value)
                        is Int -> edit.putInt(key, value)
                        is Long -> edit.putLong(key, value)
                        is Float -> edit.putFloat(key, value)
                        is Boolean -> edit.putBoolean(key, value)
                        is Set<*> -> edit.putStringSet(key, value.filterIsInstance<String>().toSet())
                        else -> error("Unsupported preference $name/$key")
                    } }
                    assertTrue(edit.commit())
                    assertEquals(preferences.getValue(name), prefs.all)
                }
            }
            assertEquals(original, preferenceFiles())
        } finally { (context.applicationContext as Application).unregisterActivityLifecycleCallbacks(guard) }
    }
    data class LabLocation(val path: String, val page: Int, val number: Int)
    data class LabFixture(val location: LabLocation, val id: String, val sourceLesson: String, val lesson: JSONObject, val block: JSONObject)
    protected fun historicalLab(location: LabLocation): LabLocation {
        val lesson = source(location.path)
        val layout = LessonStateLayoutJson.read(lesson) ?: return location
        val original = LessonStatePosition(location.page - 1, location.number - 1)
        val visible = layout.current.flatMapIndexed { p, page -> page.blocks.indices.map { b -> LessonStatePosition(p, b) } }
            .single { layout.storagePosition(it.page, it.block) == original }
        return location.copy(page = visible.page + 1, number = visible.block + 1)
    }
    protected fun authoredBlock(location: LabLocation): JSONObject {
        val visible = historicalLab(location)
        return source(location.path).getJSONArray("pages").getJSONObject(visible.page - 1)
            .getJSONArray("blocks").getJSONObject(visible.number - 1)
    }
    protected fun fixture(location: LabLocation, contract: JSONObject?): LabFixture {
        val lesson = source(location.path)
        val visible = historicalLab(location)
        val original = lesson.getString("id")
        val id = original + "__output_check_qa_" + UUID.randomUUID().toString().replace("-", "")
        lesson.put("id", id)
        val block = lesson.getJSONArray("pages").getJSONObject(visible.page - 1).getJSONArray("blocks").getJSONObject(visible.number - 1)
        assertEquals("cpp_compiler", block.getString("type"))
        if (contract == null) block.remove("outputCheck") else block.put("outputCheck", contract)
        // These independent execution fixtures intentionally change a contract. They are
        // isolated lessons, not a rewritten version of the published immutable lineage.
        lesson.remove("stateLayout")
        return LabFixture(visible, id, original, lesson, block)
    }
    protected fun withFixture(fixture: LabFixture, body: (ActivityScenario<LessonActivity>) -> Unit) {
        activeLab = fixture.location
        ProgressRepository.saveCurrentPage(context, "programming", fixture.sourceLesson, 0)
        val intent = Intent(context, LessonActivity::class.java).putExtra("routeId", "programming")
            .putExtra("courseId", "cpp_fundamentals").putExtra("unitId", fixture.location.path.substringBefore('/'))
            .putExtra("lessonId", fixture.sourceLesson).putExtra("lessonNumber", 1).putExtra("lessonTotal", 1)
        ActivityScenario.launch<LessonActivity>(intent).use { host ->
            var stableSince = 0L
            var prior = 0
            await(host, "The original host is stable before QA injection") { activity ->
                val loaded = field<JSONObject?>(activity, "lessonJson")
                val ready = field<String>(activity, "currentLessonId") == fixture.sourceLesson && loaded?.optString("id") == fixture.sourceLesson &&
                    activity.hasWindowFocus() && content(activity).width > 0 && !content(activity).isLayoutRequested
                val key = System.identityHashCode(activity) xor System.identityHashCode(loaded)
                if (!ready || key != prior) { prior = key; stableSince = if (ready) SystemClock.uptimeMillis() else 0L }
                ready && SystemClock.uptimeMillis() - stableSince > 500L
            }
            host.onActivity { activity ->
                setField(activity, "currentLessonId", fixture.id)
                setField(activity, "lessonJson", JSONObject(fixture.lesson.toString()))
                setField(activity, "currentPage", fixture.location.page - 1)
                setField(activity, "totalPages", fixture.lesson.getJSONArray("pages").length())
                listOf("pageViewCache", "pageRequiredExercises", "pageCompletedExercises").forEach { field<MutableMap<Int, Any>>(activity, it).clear() }
                invoke(activity, "renderCurrentPage")
            }
            await(host, "The QA compiler is laid out") { activity ->
                assertEquals(fixture.id, field<String>(activity, "currentLessonId"))
                content(activity).width > 0 && !content(activity).isLayoutRequested
            }
            body(host)
        }
    }
    protected fun source(path: String) = JSONObject(context.assets.open("content/es/programming/cpp_fundamentals/$path").bufferedReader().use { it.readText() })
    protected fun editor(activity: LessonActivity): FreeScrollEditText {
        val page = content(activity)
        var runChild: View = runButton(activity)
        while (runChild.parent !== page) runChild = runChild.parent as? View
            ?: error("The compiler Run control is not inside the lesson content")
        val card = (page.indexOfChild(runChild) - 1 downTo 0).asSequence().map(page::getChildAt)
            .first { child -> tree(child).any { it.tag == "cpp_code_input" } }
        return tree(card).filterIsInstance<FreeScrollEditText>().single { it.tag == "cpp_code_input" }
    }
    protected fun input(activity: LessonActivity) = tagged(activity, "cpp_input") as EditText
    protected fun runButton(activity: LessonActivity) = tagged(activity, "cpp_run") as Button
    protected fun assertSingleRunButton(activity: LessonActivity, checking: Boolean) {
        val blocks = requireNotNull(field<JSONObject?>(activity, "lessonJson")).getJSONArray("pages")
            .getJSONObject(requireNotNull(activeLab).page - 1).getJSONArray("blocks")
        assertEquals("Each displayed compiler exposes one compile/run action",
            (0 until blocks.length()).count { blocks.getJSONObject(it).optString("type") == "cpp_compiler" },
            tree(content(activity)).count { it.tag == "cpp_run" })
        assertTrue("No separate check-output button exists", tree(content(activity)).none { it.tag == "cpp_check_output" })
        assertEquals(activity.getString(if (checking) R.string.cpp_compile_run_check else R.string.cpp_runtime_run), runButton(activity).text.toString())
    }
    protected fun result(activity: LessonActivity) = tagged(activity, "cpp_output_check_result") as com.learngraphicsprogramming.app.narration.ReadingTextView
    protected fun expectedVerdict(activity: LessonActivity, correct: Boolean, explanation: String? = null): String = when {
        correct && explanation != null -> activity.getString(R.string.lesson_correct_explanation, explanation)
        correct -> activity.getString(R.string.lesson_correct_feedback)
        else -> activity.getString(R.string.exercise_incorrect) + explanation?.let { "\n$it" }.orEmpty()
    }
    protected fun tagged(activity: LessonActivity, tag: String): View = compilerViews(activity).single { it.tag == tag }

    protected fun compilerViews(activity: LessonActivity): List<View> {
        val location = requireNotNull(activeLab)
        val page = content(activity)
        val blocks = requireNotNull(field<JSONObject?>(activity, "lessonJson")).getJSONArray("pages")
            .getJSONObject(location.page - 1).getJSONArray("blocks")
        val ordinal = (0 until location.number - 1).count { blocks.getJSONObject(it).optString("type") == "cpp_compiler" }
        val runs = tree(page).filter { it.tag == "cpp_run" }
        assertEquals((0 until blocks.length()).count { blocks.getJSONObject(it).optString("type") == "cpp_compiler" }, runs.size)
        fun top(view: View): View {
            var result = view
            while (result.parent !== page) result = result.parent as? View ?: error("Compiler is outside lesson content")
            return result
        }
        fun card(run: View) = (page.indexOfChild(top(run)) - 1 downTo 0).first { index ->
            tree(page.getChildAt(index)).any { it.tag == "cpp_code_input" }
        }
        val start = card(runs[ordinal])
        val end = runs.getOrNull(ordinal + 1)?.let(::card) ?: page.childCount
        return (start until end).flatMap { tree(page.getChildAt(it)) }
    }
    protected fun content(activity: LessonActivity): ViewGroup = activity.findViewById(R.id.lessonContent)
    protected fun tree(view: View): List<View> = listOf(view) + if (view is ViewGroup) (0 until view.childCount).flatMap { tree(view.getChildAt(it)) } else emptyList()
    protected inline fun <reified T> field(owner: Any, name: String): T = owner.javaClass.getDeclaredField(name).apply { isAccessible = true }.get(owner) as T
    protected fun setField(activity: LessonActivity, name: String, value: Any) = LessonActivity::class.java.getDeclaredField(name).apply { isAccessible = true }.set(activity, value)
    protected fun invoke(activity: LessonActivity, name: String) = LessonActivity::class.java.getDeclaredMethod(name).apply { isAccessible = true }.invoke(activity)
    protected fun sha(text: String) = MessageDigest.getInstance("SHA-256").digest(text.toByteArray()).joinToString("") { "%02x".format(it) }
    protected fun saveEvidence(name: String, value: JSONObject) {
        val folder = File(requireNotNull(context.getExternalFilesDir(null)), "cpp-output-check-qa/$session").apply { mkdirs() }
        File(folder, "$name.json").writeText(value.put("package", context.packageName).put("device", Build.MODEL)
            .put("qaOnly", "Contracts injected in memory; no curricular asset is edited; all original preferences restored").toString(2))
    }
    protected fun await(host: ActivityScenario<LessonActivity>, reason: String, timeout: Long = 15_000L, predicate: (LessonActivity) -> Boolean) {
        val end = SystemClock.uptimeMillis() + timeout
        while (SystemClock.uptimeMillis() < end) {
            instrumentation.waitForIdleSync()
            var done = false
            host.onActivity { done = predicate(it) }
            if (done) return
            SystemClock.sleep(60L)
        }
        fail(reason)
    }
    private fun preferenceFiles() = File(context.applicationInfo.dataDir, "shared_prefs").listFiles().orEmpty().filter { it.name.endsWith(".xml") }.map { it.name.removeSuffix(".xml") }.toSet()
}
