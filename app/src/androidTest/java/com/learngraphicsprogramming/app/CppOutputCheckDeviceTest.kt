package com.learngraphicsprogramming.app

import android.app.Activity
import android.app.Application
import android.app.KeyguardManager
import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.Rect
import android.os.Build
import android.os.Bundle
import android.os.SystemClock
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

/** Optional, QA-only contracts on the six unchanged authored compiler locations. */
@RunWith(AndroidJUnit4::class)
class CppOutputCheckDeviceTest : CppOutputCheckQaSupport() {
    private var originalFiniteHeartState: Map<String, *> = emptyMap<String, Any?>()

    @Before fun requireFiniteHeartsAndRecordTheirUnmodifiedLedger() {
        PremiumRepository.setVerifiedFree(context)
        assertFalse("A Premium exemption must not conceal an accidental deduction",
            AttemptRepository.getState(context, "programming").unlimited)
        originalFiniteHeartState = context.getSharedPreferences("attempt_reserve_seconds_test", Context.MODE_PRIVATE).all.toMap()
    }

    @Test fun firstAuthoredLabShowsTheOptionalButtonBelowItsConsoleInStableRealWindows() {
        val location = LOCATIONS.first()
        val fixture = fixture(location, contracts(0))
        val reference = source(location.path).getJSONArray("pages").getJSONObject(5).getJSONArray("blocks").getJSONObject(2).getString("code")
        val frames = JSONArray()
        withFixture(fixture) { host ->
            host.onActivity { activity ->
                assertEquals(fixture.block.getString("initialCode"), editor(activity).text.toString())
                assertEquals(fixture.block.optString("initialStandardInput"), input(activity).text.toString())
                assertFalse(check(activity).isEnabled)
                assertEquals(View.GONE, result(activity).visibility)
                assertTrue(tree(content(activity)).filterIsInstance<TextView>().any {
                    it.text.toString() == source(location.path).getJSONArray("pages").getJSONObject(4)
                        .getJSONArray("blocks").getJSONObject(3).getString("question")
                })
                assertUnchangedExercises(activity, fixture)
            }
            frames.put(captureRealOutputWindow(host, "first-lab-before-run", fixture.block.getString("initialCode"), false))
            edit(host, reference)
            val execution = runAndCheck(host, fixture, "Caja recibida", true, EXPLANATION, "published_reference_for_window")
            frames.put(captureRealOutputWindow(host, "first-lab-after-check", reference, true))
            saveEvidence("first-lab-real-window", JSONObject().put("source", location.path).put("page", 5).put("block", 3)
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
                    assertFalse(check(activity).isEnabled)
                    assertEquals(View.GONE, result(activity).visibility)
                    assertNull("Authored compiler blocks keep their absence of an ID", fixture.block.opt("id"))
                    assertTrue("Only the explicitly opted-in compiler has the button", tree(content(activity)).count { it.tag == "cpp_check_output" } == 1)
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
                            assertFalse(check(activity).isEnabled)
                            editor(activity).setText(original)
                            assertFalse(check(activity).isEnabled)
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
                assertFalse("Reopening restores drafts but never a stale run verdict", check(activity).isEnabled)
                assertEquals(View.GONE, result(activity).visibility)
            } }
        }
        saveEvidence("six-native-laboratories", JSONObject().put("executions", evidence))
    }

    @Test fun projectHeaderAndSourceSelectionInvalidateTheActualRunWhileFreeLabsKeepNoButton() {
        val original = LOCATIONS.first()
        val free = fixture(original, null)
        withFixture(free) { host -> host.onActivity { activity ->
            assertTrue(tree(content(activity)).none { it.tag == "cpp_check_output" || it.tag == "cpp_output_check_result" })
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
                assertFalse("A non-selected header belongs to the executable identity", check(activity).isEnabled)
                assertEquals(View.GONE, result(activity).visibility)
                tagged(activity, "cpp_file_main.cpp").performClick()
            }
            observations.put(runAndCheck(host, fixture, "Zona segura", false, GUIDE, "changed_header"))
            host.onActivity { activity ->
                val selected = tagged(activity, "cpp_source_enabled") as CheckBox
                assertTrue(selected.isChecked)
                selected.performClick()
                assertFalse(check(activity).isEnabled)
                selected.performClick()
                assertFalse("Restoring the selected translation units needs a new run", check(activity).isEnabled)
            }
            observations.put(runAndCheck(host, fixture, "Zona segura", false, GUIDE, "restored_translation_unit"))
            assertNoExerciseChanges(host, fixture)
        }
        saveEvidence("project-invalidation", JSONObject().put("executions", observations))
    }

    private fun edit(host: ActivityScenario<LessonActivity>, code: String, stdin: String? = null) = host.onActivity { activity ->
        editor(activity).setText(code)
        stdin?.let { input(activity).setText(it) }
        assertFalse("Every source/input edit invalidates the last check", check(activity).isEnabled)
        assertEquals(View.GONE, result(activity).visibility)
    }
    private fun runAndCheck(host: ActivityScenario<LessonActivity>, fixture: LabFixture, expected: String,
                            correct: Boolean, explanation: String?, stage: String): JSONObject {
        val evidence = run(host, fixture, expected)
        host.onActivity { activity ->
            assertTrue(check(activity).isEnabled)
            check(activity).performClick()
            val verdict = activity.getString(if (correct) R.string.cpp_output_check_correct else R.string.cpp_output_check_incorrect)
            assertEquals(verdict + (explanation?.let { "\n$it" } ?: ""), result(activity).text.toString())
            assertEquals(View.VISIBLE, result(activity).visibility)
            evidence.put("visibleResult", result(activity).text.toString()).put("stage", stage)
            assertUnchangedExercises(activity, fixture)
        }
        return evidence
    }
    private fun runWithoutKnownCase(host: ActivityScenario<LessonActivity>, fixture: LabFixture, expected: String): JSONObject {
        val evidence = run(host, fixture, expected)
        host.onActivity { activity -> assertFalse(check(activity).isEnabled); assertEquals(View.GONE, result(activity).visibility) }
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
            assertFalse(check(activity).isEnabled)
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
            assertFalse(check(activity).isEnabled)
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
                .put("block", fixture.location.number).put("stdin", input(activity).text.toString()).put("stdout", expected)
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
                    val consolePosition = IntArray(2).also(console::getLocationOnScreen)
                    val child = requireNotNull(scroll.getChildAt(0))
                    val margins = (child.layoutParams as? ViewGroup.MarginLayoutParams)?.let { it.topMargin + it.bottomMargin } ?: 0
                    val limit = (child.height + margins + scroll.paddingTop + scroll.paddingBottom - scroll.height).coerceAtLeast(0)
                    val desired = (scroll.scrollY + consolePosition[1] - safe.top - margin).coerceIn(0, limit)
                    if (scroll.scrollY != desired) scroll.scrollTo(0, desired)
                    fun bounds(view: View): Rect {
                        val position = IntArray(2).also(view::getLocationOnScreen)
                        return Rect(position[0], position[1], position[0] + view.width, position[1] + view.height)
                    }
                    val consoleBounds = bounds(console)
                    val buttonBounds = bounds(check(activity))
                    val resultBounds = if (checked) bounds(result(activity)) else Rect()
                    group = Rect(consoleBounds).apply { union(buttonBounds); if (checked) union(resultBounds) }
                    assertTrue("The new button is immediately after the original console", buttonBounds.top >= consoleBounds.bottom)
                    assertEquals(activity.getString(R.string.cpp_program_stdout), (tagged(activity, "cpp_console_title") as TextView).text.toString())
                    assertEquals(expectedCode, editor(activity).text.toString())
                    assertEquals("", input(activity).text.toString())
                    assertEquals(checked, check(activity).isEnabled)
                    assertEquals(if (checked) View.VISIBLE else View.GONE, result(activity).visibility)
                    if (checked) {
                        assertEquals("Caja recibida", (tagged(activity, "cpp_console_display") as TextView).text.toString())
                        assertEquals(activity.getString(R.string.cpp_output_check_correct) + "\n" + EXPLANATION, result(activity).text.toString())
                    }
                    ready = activity.hasWindowFocus() && !scroll.isLayoutRequested && !console.isLayoutRequested &&
                        !check(activity).isLayoutRequested && (!checked || !result(activity).isLayoutRequested) && safe.contains(group)
                    fun json(rect: Rect) = JSONArray(listOf(rect.left, rect.top, rect.right, rect.bottom))
                    metadata = JSONObject().put("safeViewport", json(safe)).put("consoleBounds", json(consoleBounds))
                        .put("buttonBounds", json(buttonBounds)).put("resultBounds", json(resultBounds))
                        .put("scrollY", scroll.scrollY).put("maxLegalScrollY", limit).put("buttonEnabled", check(activity).isEnabled)
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
    protected fun fixture(location: LabLocation, contract: JSONObject?): LabFixture {
        val lesson = source(location.path)
        val original = lesson.getString("id")
        val id = original + "__output_check_qa_" + UUID.randomUUID().toString().replace("-", "")
        lesson.put("id", id)
        val block = lesson.getJSONArray("pages").getJSONObject(location.page - 1).getJSONArray("blocks").getJSONObject(location.number - 1)
        assertEquals("cpp_compiler", block.getString("type"))
        contract?.let { block.put("outputCheck", it) }
        return LabFixture(location, id, original, lesson, block)
    }
    protected fun withFixture(fixture: LabFixture, body: (ActivityScenario<LessonActivity>) -> Unit) {
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
    protected fun editor(activity: LessonActivity) = tree(content(activity)).filterIsInstance<FreeScrollEditText>().single { it.tag == "cpp_code_input" }
    protected fun input(activity: LessonActivity) = tagged(activity, "cpp_input") as EditText
    protected fun check(activity: LessonActivity) = tagged(activity, "cpp_check_output") as Button
    protected fun result(activity: LessonActivity) = tagged(activity, "cpp_output_check_result") as com.learngraphicsprogramming.app.narration.ReadingTextView
    protected fun tagged(activity: LessonActivity, tag: String) = tree(content(activity)).single { it.tag == tag }
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
