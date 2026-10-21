package com.learngraphicsprogramming.app

import android.app.KeyguardManager
import android.app.Activity
import android.app.Application
import android.content.Context
import android.content.Intent
import android.content.pm.ActivityInfo
import android.content.res.Configuration
import android.graphics.Rect
import android.graphics.Bitmap
import android.os.Build
import android.os.Bundle
import android.os.SystemClock
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.view.inputmethod.InputMethodManager
import android.widget.CheckBox
import android.widget.EditText
import android.widget.Spinner
import android.widget.TextView
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.test.core.app.ActivityScenario
import androidx.test.espresso.Espresso.onView
import androidx.test.espresso.action.ViewActions.click
import androidx.test.espresso.action.ViewActions.closeSoftKeyboard
import androidx.test.espresso.action.ViewActions.replaceText
import androidx.test.espresso.matcher.RootMatchers.isDialog
import androidx.test.espresso.matcher.ViewMatchers.withId
import androidx.test.espresso.matcher.ViewMatchers.withTagValue
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.narration.NarrationSettingsRepository
import com.learngraphicsprogramming.app.runtime.CppCompiler
import java.io.File
import java.util.WeakHashMap
import org.json.JSONArray
import org.json.JSONObject
import org.hamcrest.Matchers.equalTo
import org.junit.After
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/**
 * Acceptance checks through the real curricular renderer and its Run listener.
 * Published lesson JSON supplies the reading/diferencia/sobres/serie projects.
 * Only technical stream/legacy fixtures use in-memory content. Preferences are
 * restored after every test, including drafts, attempts and course progress.
 */
@RunWith(AndroidJUnit4::class)
class CppCurricularLabsDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val snapshots = linkedMapOf<String, Map<String, *>>()
    private var originalPreferenceFiles = emptySet<String>()
    private var injectedFixtureActivityIdentity: Int? = null
    private var selectedLab: Pair<String, LabLocation>? = null

    @Before fun preserveUserState() {
        assertEquals("Usar -Plgp.curricularQa=true para aislar los datos del usuario",
            "com.learngraphicsprogramming.app.curricularqa", context.packageName)
        originalPreferenceFiles = preferenceFiles()
        (originalPreferenceFiles + KNOWN_PREFERENCES).forEach { name ->
            snapshots[name] = context.getSharedPreferences(name, Context.MODE_PRIVATE).all.mapValues { (_, value) ->
                if (value is Set<*>) value.toSet() else value
            }
        }
        val keyguard = context.getSystemService(Context.KEYGUARD_SERVICE) as KeyguardManager
        assertFalse("Unlock the phone before renderer acceptance checks", keyguard.isDeviceLocked)
        NarrationSettingsRepository.saveEnabled(context, false)
        assertTrue(context.getSharedPreferences(DRAFTS, Context.MODE_PRIVATE).edit().clear().commit())
        assertTrue(context.getSharedPreferences(ATTEMPTS, Context.MODE_PRIVATE).edit()
            .putBoolean("unlimited", false).putFloat("reserve_$ROUTE", 4f)
            .putLong("updated_at_$ROUTE", System.currentTimeMillis()).commit())
    }

    @After fun restoreUserState() {
        // JUnit también ejecuta After cuando Before rechaza un paquete no aislado.
        if (context.packageName != "com.learngraphicsprogramming.app.curricularqa") return
        (preferenceFiles() + snapshots.keys).forEach { name ->
            if (name !in originalPreferenceFiles) {
                assertTrue("Remove only preferences created by this test: $name", context.deleteSharedPreferences(name))
            } else {
                val editor = context.getSharedPreferences(name, Context.MODE_PRIVATE).edit().clear()
                snapshots.getValue(name).forEach { (key, value) ->
                    when (value) {
                        is String -> editor.putString(key, value)
                        is Int -> editor.putInt(key, value)
                        is Long -> editor.putLong(key, value)
                        is Float -> editor.putFloat(key, value)
                        is Boolean -> editor.putBoolean(key, value)
                        is Set<*> -> editor.putStringSet(key, value.filterIsInstance<String>().toSet())
                        else -> error("Unsupported preference value for $name/$key")
                    }
                }
                assertTrue("Restore user preferences: $name", editor.commit())
            }
        }
    }

    @Test fun authoredReadingLabConsumesFreshPreparedInputAndRetainsTheDraft() {
        withAsset(READING, LessonStatePosition(6, 2), { block ->
            block.optString("initialStandardInput") == "Rita 4" &&
                declaresOutput(block, "Rita 4", "Rita: 7") && declaresOutput(block, "Leo 2", "Leo: 5")
        }) { scenario, location ->
            runWithInput(scenario, "Rita 4", "Rita: 7")
            runWithInput(scenario, "Leo\n2\n", "Leo: 5")
            runWithInput(scenario, "  Rita\t4\n", "Rita: 7")
            runWithInput(scenario, "  Rita\t4\n", "Rita: 7")
            var source = ""
            scenario.onActivity { activity ->
                source = editor(activity).text.toString() + "\n// draft survives recreation\n"
                editor(activity).setText(source)
                input(activity).setText("Leo 2")
            }
            scenario.recreate()
            scenario.onActivity { activity ->
                assertEquals(location.page, field(activity, "currentPage"))
                assertEquals(source, editor(activity).text.toString())
                assertEquals("Leo 2", input(activity).text.toString())
            }
            assertSuccessful(run(scenario), "Leo: 5")
        }
    }

    @Test fun focusedResetRetainsExactAuthoredSourceAfterSettlingAndRecreation() {
        val lessonId = "cpp_fundamentals__programas_y_ejecucion__primer_programa_y_salida"
        withAsset(lessonId, LessonStatePosition(4, 2), { declaresOutput(it, "", "Caja recibida") }) { scenario, location ->
            val original = LessonRepository.loadLessonJson(context, ROUTE, lessonId).getJSONArray("pages")
                .getJSONObject(location.page).getJSONArray("blocks").getJSONObject(location.block)
            val code = original.getString("initialCode")
            val stdin = original.optString("initialStandardInput")
            val requestsInput = CppLabInputPolicy.requested(original, CppOutputCheck.fromBlock(original))
            val temporaryInput = "temporary input"
            // A task without stdin intentionally keeps an older hidden draft. Reset
            // restores authored input only when this activity actually requests it.
            val inputAfterReset = if (requestsInput) stdin else temporaryInput
            val observations = JSONArray()
            for (iteration in 0..3) {
                scenario.onActivity { activity ->
                    val source = editor(activity)
                    source.setText(code.replace("Zona inicial", "Caja recibida") + "\n// reset check $iteration\n")
                    assertEquals("Only curricular input metadata exposes the input field",
                        if (requestsInput) View.VISIBLE else View.GONE, input(activity).visibility)
                    input(activity).setText(temporaryInput)
                    source.requestFocus()
                    source.setSelection(source.length())
                    (activity.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager)
                        .showSoftInput(source, InputMethodManager.SHOW_IMPLICIT)
                }
                instrumentation.waitForIdleSync()
                scenario.onActivity { activity ->
                    // Startup/IME dismissal may legitimately retire a caret. Acquire
                    // focus on the current native instance immediately before Reset.
                    assertTrue("The actual editor can enter editing before Reset", editor(activity).requestFocus())
                    assertTrue("Reset is exercised with the actual editor focused", editor(activity).hasFocus())
                    tagged(activity, "cpp_reset").performClick()
                    assertEquals("Reset restores exact authored characters immediately", code, editor(activity).text.toString())
                    assertEquals("Reset respects authored input and preserves an unrelated hidden draft",
                        inputAfterReset, input(activity).text.toString())
                }
                repeat(5) {
                    SystemClock.sleep(100)
                    instrumentation.waitForIdleSync()
                    scenario.onActivity { activity ->
                        assertEquals("No delayed IME/rules edit may add whitespace after reset", code, editor(activity).text.toString())
                        assertEquals(inputAfterReset, input(activity).text.toString())
                    }
                }
                scenario.recreate()
                instrumentation.waitForIdleSync()
                scenario.onActivity { activity ->
                    assertEquals("Reopened source stays byte-exact", code, editor(activity).text.toString())
                    assertEquals(inputAfterReset, input(activity).text.toString())
                    val draft = LessonStateCoordinates.draftKey(context, ROUTE, lessonId, location.page, location.block)
                    assertEquals("The persisted draft has no added trailing spaces", code,
                        CppLessonSourceDraft.read(context.getSharedPreferences(DRAFTS, Context.MODE_PRIVATE), draft, "missing"))
                    observations.put(JSONObject().put("iteration", iteration).put("sourceSha256", sha256(code))
                        .put("characters", code.length).put("exactAfterSettlingAndRecreation", true)
                        .put("preparedInputRequested", requestsInput).put("resetInputContractPreserved", true)
                        .put("imeVisibleAfterRecreation", imeVisible(activity)))
                }
            }
            assertSuccessful(run(scenario), "Zona inicial")
            stabilityEvidence("focused-reset-exact.json", JSONObject().put("observations", observations)
                .put("newExecutionStdout", "Zona inicial").put("device", Build.MODEL))
        }
    }

    @Test fun pedidosWithAuthoredSkeletonRetainsAllFifteenAnswersWithoutBlocking() =
        checkPedidosPage(null, "pedidos-skeleton")

    @Test fun pedidosMappingsPrepareOnEnableAndReuseOnlyTheUnchangedVisiblePresentation() {
        val lessonId = "cpp_fundamentals__datos_propios_y_estado_valido__construir_y_comprobar_un_modelo_de_pedidos"
        val lesson = LessonRepository.loadLessonJson(context, ROUTE, lessonId)
        val locations = pedidosLocations(lessonId, lesson)
        val puzzle = blockAt(lesson, locations.puzzle)
        val mappingKey = locations.puzzle.block to false
        MainThreadProbe("pedidos-reading-mapping-reuse").use { probe ->
        withAsset(lessonId, LessonStatePosition(1, 2), { declaresPedidosOutput(it) }) { scenario, location ->
            assertEquals(locations.compiler, location)
            var cached: Any? = null
            fun cache(activity: LessonActivity): Map<*, *> {
                val pages = field(activity, "readingTextPages") as Map<*, *>
                val identity = LessonStateCoordinates.narrationPageIdentity(context, ROUTE, lessonId, location.page)
                val page = requireNotNull(pages[identity])
                val mappings = page.javaClass.getDeclaredField("puzzleMappings").apply { isAccessible = true }.get(page)
                return mappings.javaClass.getDeclaredField("current").apply { isAccessible = true }.get(mappings) as Map<*, *>
            }
            scenario.onActivity { assertTrue("Disabled narration does not perform expensive unused puzzle mappings", cache(it).isEmpty()) }
            NarrationSettingsRepository.saveEnabled(context, true)
            instrumentation.waitForIdleSync()
            scenario.onActivity { activity ->
                cached = requireNotNull(cache(activity)[mappingKey])
                val actual = cached!!.javaClass.getDeclaredField("mappings").apply { isAccessible = true }.get(cached) as Map<*, *>
                val originalInputs = dumpPedidosMappingInput(requireNotNull(cached), actual,
                    locations.puzzle.block).getJSONArray("views")
                assertTrue("Enabling the reader reconstructs actual word mappings from the original page", actual.isNotEmpty())
                val words = actual.values.map { it as com.learngraphicsprogramming.app.narration.NarrationWordMapping }
                val expectedTargets = (0 until originalInputs.length()).map { originalInputs.getJSONObject(it).getInt("target") }.toSet()
                assertEquals("The actual page has 24 original passive targets, with controls excluded", 24, expectedTargets.size)
                assertEquals("Every visible cell, including all technical rows, has proven word owners",
                    expectedTargets, words.flatMap { requireNotNull(it.targets) }.toSet())
                for (index in 0 until originalInputs.length()) {
                    val original = originalInputs.getJSONObject(index)
                    val target = original.getInt("target")
                    val owned = words.flatMap { word -> word.displayed.indices
                        .filter { word.targets!![it] == target }.flatMap { offset ->
                            word.displayed[offset].let { it.start until it.endExclusive }
                        } }.toSet()
                    assertEquals("All original glyphs in ${original.getString("text")} have real spoken provenance",
                        original.getString("text").indices.toSet(), owned)
                }
                fillPedidos(activity, puzzle, wrong = true)
            }
            instrumentation.waitForIdleSync()
            scenario.onActivity { activity ->
                assertSame("Ordinary answers do not change passive visible text or rebuild its verified mapping",
                    cached, cache(activity)[mappingKey])
            }
            NarrationSettingsRepository.saveEnabled(context, false)
            instrumentation.waitForIdleSync()
            NarrationSettingsRepository.saveEnabled(context, true)
            instrumentation.waitForIdleSync()
            scenario.onActivity { activity ->
                assertSame("Re-enabling preserves the exact current mapping, not another reader history",
                    cached, cache(activity)[mappingKey])
                val controller = field(activity, "narrationController") as com.learngraphicsprogramming.app.narration.LessonNarrationController
                assertFalse("Preparing display mappings does not start playback", controller.isPlaying)
            }
            probe.result.put("disabledMappingDeferred", true).put("enabledMappingPresent", true)
                .put("allFifteenOrdinaryChangesReuseCurrentMapping", true).put("reenableReuse", true)
                .put("playbackStarted", false).put("allTwentyFourTargetsMapped", true).put("allOriginalGlyphsMapped", true)
        }
        }
    }

    /** Native source ranges only, to reproduce expensive searches on the host.
     * Contains no validator, expected response, model data or user preference dump. */
    private fun dumpPedidosMappingInput(cached: Any, actual: Map<*, *>, blockIndex: Int): JSONObject {
        fun property(owner: Any, name: String): Any? = owner.javaClass.getDeclaredField(name)
            .apply { isAccessible = true }.get(owner)
        val key = requireNotNull(property(cached, "key"))
        val sources = property(key, "sources") as List<*>
        val views = JSONArray()
        sources.forEach { source ->
            source ?: return@forEach
            val ranges = property(source, "formulas") as List<*>
            views.put(JSONObject().put("target", property(source, "target")).put("text", property(source, "text"))
                .put("formulas", JSONArray().apply { ranges.forEach { range ->
                    range as com.learngraphicsprogramming.app.narration.ReadingTextRange
                    put(JSONArray().put(range.start).put(range.endExclusive))
                } }))
        }
        val segments = JSONObject()
        (property(key, "segments") as Map<*, *>).forEach { (segment, text) -> segments.put(segment.toString(), text) }
        val matched = JSONArray()
        actual.forEach { (segment, value) ->
            value as com.learngraphicsprogramming.app.narration.NarrationWordMapping
            matched.put(JSONObject().put("segment", segment).put("targets", JSONArray(value.targets)))
        }
        return JSONObject().put("block", blockIndex)
            .put("language", property(key, "language").toString()).put("feedback", property(key, "feedback"))
            .put("views", views).put("segments", segments).put("matched", matched)
            .also { stabilityEvidence("pedidos-mapping-input.json", it) }
    }

    @Test fun pedidosWithPreservedSeventyThreeLineDraftRetainsAllFifteenAnswersWithoutBlocking() {
        val file = File(requireNotNull(context.getExternalFilesDir(null)), "editor-stability-input/honor-pedidos-draft.cpp")
        assertTrue("Supply only the preserved isolated QA draft, never user preferences: $file", file.isFile)
        val draft = file.readText()
        assertEquals("This diagnostic uses the exact draft described in the pending ficha", 2513, draft.length)
        assertEquals(72, draft.count { it == '\n' })
        assertEquals("60abf07523286e700215cff07fa5055b4f26ee6ae88ba483149f21d349a9b789", sha256(draft))
        checkPedidosPage(draft, "pedidos-preserved-draft")
    }

    private fun checkPedidosPage(draft: String?, evidence: String) {
        val lessonId = "cpp_fundamentals__datos_propios_y_estado_valido__construir_y_comprobar_un_modelo_de_pedidos"
        val lesson = LessonRepository.loadLessonJson(context, ROUTE, lessonId)
        val locations = pedidosLocations(lessonId, lesson)
        val puzzle = blockAt(lesson, locations.puzzle)
        val authoredCode = blockAt(lesson, locations.compiler).getString("initialCode")
        val draftKey = LessonStateCoordinates.draftKey(context, ROUTE, lessonId,
            locations.compiler.page, locations.compiler.block)
        assertEquals("The reordered compiler keeps the original draft identity", "$ROUTE/$lessonId/1/2", draftKey)
        if (draft != null) assertTrue(context.getSharedPreferences(DRAFTS, Context.MODE_PRIVATE).edit()
            .putString("$draftKey/source", draft).commit())
        MainThreadProbe(evidence).use { probe ->
            withAsset(lessonId, LessonStatePosition(1, 2), { declaresPedidosOutput(it) }) { scenario, location ->
                assertEquals(locations.compiler, location)
                scenario.onActivity { activity ->
                    assertEquals("The requested real editor source is rendered", draft ?: authoredCode,
                        editor(activity).text.toString())
                    assertEquals("The actual page retains its one numeric table", 10,
                        descendants(content(activity)).filterIsInstance<EditText>()
                            .count { it.contentDescription?.toString()?.startsWith("Solicitud ") == true })
                    assertEquals(5, descendants(content(activity)).filterIsInstance<Spinner>()
                        .count { it.contentDescription?.toString()?.startsWith("Motivo de la solicitud ") == true })
                    editor(activity).requestFocus()
                    editor(activity).setSelection(editor(activity).text.indexOf("return 0").coerceAtLeast(0))
                    (activity.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager)
                        .showSoftInput(editor(activity), InputMethodManager.SHOW_IMPLICIT)
                    fillPedidos(activity, puzzle, wrong = true)
                }
                instrumentation.waitForIdleSync()
                scenario.onActivity { activity ->
                    descendants(content(activity)).single { it.tag == com.learngraphicsprogramming.app.math.MathPuzzleRenderer.CHECK_ACTION_TAG }.performClick()
                    assertTrue("An actual wrong numeric response is rejected", descendants(content(activity)).filterIsInstance<TextView>()
                        .any { it.text.toString().startsWith("✗") })
                    assertFalse("A wrong attempt does not show the solution", descendants(content(activity)).filterIsInstance<TextView>()
                        .any { it.text.toString().startsWith("✓") })
                    fillPedidos(activity, puzzle, wrong = false)
                }
                instrumentation.waitForIdleSync()
                scenario.onActivity { activity ->
                    descendants(content(activity)).single { it.tag == com.learngraphicsprogramming.app.math.MathPuzzleRenderer.CHECK_ACTION_TAG }.performClick()
                    assertTrue("The original fifteen expected responses are accepted", descendants(content(activity)).filterIsInstance<TextView>()
                        .any { it.text.toString().startsWith("✓") })
                }
                scenario.recreate()
                instrumentation.waitForIdleSync()
                scenario.onActivity { activity ->
                    verifyPedidos(activity, puzzle)
                    assertEquals("Reopening preserves this independent compiler draft",
                        draft ?: authoredCode, editor(activity).text.toString())
                    assertTrue(descendants(content(activity)).filterIsInstance<TextView>().any { it.text.toString().startsWith("✓") })
                }
                val expectedOutput = if (draft == null) "" else
                    "Rechazado:0\nAceptado:1\nAceptado:20\nRechazado:21\nCodigo rechazado\n" +
                        "1:0:0:0:5:1\n1:0:0:0:2:1\n1:1:1:3:20:1:1"
                assertSuccessful(run(scenario), expectedOutput)
                probe.result.put("allFifteenAnswersRestored", true).put("wrongRetryCorrect", true)
                    .put("sourceSha256", sha256(draft ?: authoredCode))
                    .put("sourceCharacters", (draft ?: authoredCode).length)
                    .put("nativeCompilerStdout", expectedOutput)
            }
        }
    }

    private fun fillPedidos(activity: LessonActivity, puzzle: JSONObject, wrong: Boolean) {
        val responses = puzzle.getJSONArray("responses")
        for (index in 0 until responses.length()) {
            val response = responses.getJSONObject(index)
            val fields = response.getJSONArray("fields")
            val validators = response.getJSONObject("validator").getJSONObject("fields")
            for (fieldIndex in 0 until fields.length()) {
                val field = fields.getJSONObject(fieldIndex)
                val expected = validators.getJSONObject(field.getString("id")).getString("expected")
                val control = descendants(content(activity)).single { (it is EditText || it is Spinner) &&
                    it.contentDescription?.toString() == field.getString("label") }
                if (control is EditText) control.setText(if (wrong && field.getString("id") == "resultado_1") "9" else expected)
                else {
                    control as Spinner
                    val options = field.getJSONArray("options")
                    control.setSelection((0 until options.length()).single { options.getJSONObject(it).getString("id") == expected } + 1)
                }
            }
        }
    }

    private fun verifyPedidos(activity: LessonActivity, puzzle: JSONObject) {
        val responses = puzzle.getJSONArray("responses")
        for (index in 0 until responses.length()) {
            val response = responses.getJSONObject(index)
            val fields = response.getJSONArray("fields")
            val validators = response.getJSONObject("validator").getJSONObject("fields")
            for (fieldIndex in 0 until fields.length()) {
                val field = fields.getJSONObject(fieldIndex)
                val expected = validators.getJSONObject(field.getString("id")).getString("expected")
                val control = descendants(content(activity)).single { (it is EditText || it is Spinner) &&
                    it.contentDescription?.toString() == field.getString("label") }
                val actual = if (control is EditText) control.text.toString() else {
                    control as Spinner
                    field.getJSONArray("options").getJSONObject(control.selectedItemPosition - 1).getString("id")
                }
                assertEquals("Original response ${field.getString("id")} survives reopening", expected, actual)
            }
        }
    }

    /** A diagnostic observer only: no dispatch, reader, compiler or layout behavior changes.
     * Its independent thread records the actual UI stack if a queued heartbeat is delayed. */
    private inner class MainThreadProbe(private val name: String) : java.io.Closeable {
        val result = JSONObject().put("device", Build.MODEL).put("sdk", Build.VERSION.SDK_INT)
        private val running = java.util.concurrent.atomic.AtomicBoolean(true)
        private val queuedAt = java.util.concurrent.atomic.AtomicLong(0)
        private val maxDelay = java.util.concurrent.atomic.AtomicLong(0)
        private val samples = java.util.concurrent.ConcurrentLinkedQueue<JSONObject>()
        private val handler = android.os.Handler(android.os.Looper.getMainLooper())
        private val thread = Thread({
            while (running.get()) {
                val start = SystemClock.uptimeMillis()
                if (queuedAt.compareAndSet(0, start)) handler.post {
                    val queued = queuedAt.getAndSet(0)
                    if (queued > 0) maxDelay.accumulateAndGet(SystemClock.uptimeMillis() - queued) { first, second -> maxOf(first, second) }
                }
                val queued = queuedAt.get()
                val delay = if (queued > 0) SystemClock.uptimeMillis() - queued else 0
                if (delay > 500 && samples.size < 30) {
                    samples.add(JSONObject().put("delayMs", delay).put("mainThreadStack", JSONArray(
                        android.os.Looper.getMainLooper().thread.stackTrace.map { it.toString() })))
                    flush()
                }
                SystemClock.sleep(100)
            }
        }, "qa-editor-main-observer").apply { isDaemon = true; start() }
        private fun flush() = stabilityEvidence("$name.json", JSONObject(result.toString())
            .put("maxHeartbeatDelayMs", maxDelay.get()).put("slowSamples", JSONArray(samples.toList())))
        override fun close() {
            running.set(false)
            thread.join(1000)
            flush()
            assertTrue("The actual UI heartbeat must precede Android's documented 5000 ms focus/input timeout; " +
                "observed ${maxDelay.get()} ms (original stacks preserved in $name.json)", maxDelay.get() < 5000)
        }
    }

    private fun sha256(value: String) = java.security.MessageDigest.getInstance("SHA-256")
        .digest(value.toByteArray(Charsets.UTF_8)).joinToString("") { "%02x".format(it.toInt() and 255) }

    private fun stabilityEvidence(name: String, value: JSONObject) {
        File(requireNotNull(context.getExternalFilesDir(null)), "editor-stability-current-qa").apply { mkdirs() }
            .resolve(name).writeText(value.toString(2))
    }

    @Test fun preparedInputHandlesEmptyMalformedUnicodeAndSeparatesAllOutputChannels() {
        withFixture(STREAM_SOURCE, "") { scenario ->
            runWithInput(scenario, "", "EMPTY")
            runWithInput(scenario, "muchos\n", "INVALID")
            runWithInput(scenario, "7\nlínea con espacios ñ\n", "7|línea con espacios ñ|EOF", "stream stderr\n")
            runWithInput(scenario, "8\ntexto\nextra\n", "8|texto|EXTRA", "stream stderr\n")
            runWithInput(scenario, "7\nlínea con espacios ñ\n", "7|línea con espacios ñ|EOF", "stream stderr\n")
            scenario.onActivity { editor(it).setText("int main() { return missing_curricular_identifier; }") }
            val invalid = run(scenario)
            assertEquals("COMPILATION", invalid.phase)
            assertTrue(invalid.compilation, invalid.compilation.contains("missing_curricular_identifier"))
            assertTrue("Source location survives rendering: ${invalid.compilation}",
                Regex("main\\.cpp:1:\\d+: error:").containsMatchIn(invalid.compilation))
            assertEquals("No program output is fabricated after failed compilation", "", invalid.stdout)
            assertEquals("Compiler diagnostics belong to the compilation pane", "", invalid.stderr)
            scenario.onActivity {
                editor(it).setText("#include <iostream>\nint main() { std::cout << \"out\"; std::cerr << \"err\"; return 3; }")
            }
            val exited = run(scenario)
            assertEquals(exited.status, exited.finishedStatus(3))
            assertEquals("", exited.compilation)
            assertEquals("out", exited.stdout)
            assertEquals("err", exited.stderr)
        }
    }

    @Test fun legacyInitialCodeStillRunsWithoutPreparedInputOrAProject() {
        withFixture("#include <iostream>\nint main() { std::cout << \"legacy single source\"; }") { scenario ->
            scenario.onActivity { activity ->
                assertEquals("", input(activity).text.toString())
                assertTrue(descendants(content(activity)).none { it.tag?.toString()?.startsWith("cpp_file_") == true })
            }
            assertSuccessful(run(scenario), "legacy single source")
        }
    }

    @Test fun authoredDiferenciaCompilesDistinctUnitsAndDetectsFreshHeaderAndImplementationEdits() {
        withProject(DIFERENCIA, "diferencia.h") { scenario, _ ->
            assertSuccessful(run(scenario), "7")
            editFile(scenario, "diferencia.cpp") { source ->
                assertTrue(source, source.contains("primero - segundo"))
                source.replace("primero - segundo", "primero + segundo")
            }
            assertSuccessful(run(scenario), "11")
            editFile(scenario, "main.cpp") { "#include \"diferencia.h\"\n$it" }
            assertSuccessful(run(scenario), "11")
            editFile(scenario, "diferencia.h") { it.replace("int diferencia(", "double diferencia(") }
            assertCompilationFailure(run(scenario), "diferencia.cpp", "error:")
            editFile(scenario, "diferencia.h") { it.replace("double diferencia(", "int diferencia(") }
            assertSuccessful(run(scenario), "11")
            enableSource(scenario, "diferencia.cpp", false)
            enableSource(scenario, "main.cpp", false)
            assertConfigurationFailure(scenario)
            enableSource(scenario, "main.cpp", true)
            assertLinkFailure(run(scenario), "diferencia")
            enableSource(scenario, "diferencia.cpp", true)
            editFile(scenario, "diferencia.cpp") { it.replace("int diferencia(", "double diferencia(") }
            assertCompilationFailure(run(scenario), "diferencia.cpp", "error:")
            editFile(scenario, "diferencia.cpp") { it.replace("double diferencia(", "int diferencia(") }
            editFile(scenario, "main.cpp") { it.replace("diferencia.h", "missing_curricular_header.h") }
            assertCompilationFailure(run(scenario), "main.cpp", "missing_curricular_header.h")
        }
    }

    @Test fun optionalClang21UsesTheSameRendererForPreparedInputAndProjectLinking() {
        assumeTrue("This check needs the executable optional Clang 21 split",
            CppCompiler.isOptionalClang21Available(context))
        withAsset(READING, { it.optString("initialStandardInput") == "Rita 4" }) { scenario, _ ->
            selectOptionalCompiler(scenario)
            runWithInput(scenario, "Rita 4", "Rita: 7")
            runWithInput(scenario, "Leo 2", "Leo: 5")
        }
        // Different draft identities: the compiler choice and input cannot cross lessons.
        withProject(DIFERENCIA, "diferencia.h") { scenario, _ ->
            selectOptionalCompiler(scenario)
            assertSuccessful(run(scenario), "7")
            enableSource(scenario, "diferencia.cpp", false)
            assertLinkFailure(run(scenario), "diferencia")
            enableSource(scenario, "diferencia.cpp", true)
            assertSuccessful(run(scenario), "7")
        }
    }

    @Test fun projectFilesSelectionInputAndEditsSurvivePageChangesAndRecreation() {
        withProject(DIFERENCIA, "diferencia.h") { scenario, location ->
            var header = ""
            var implementation = ""
            editFile(scenario, "diferencia.h") { (it + "\n// edited shared interface\n").also { text -> header = text } }
            editFile(scenario, "diferencia.cpp") {
                it.replace("primero - segundo", "primero + segundo").also { text -> implementation = text }
            }
            scenario.onActivity { input(it).setText("prepared draft\nñ") }
            scenario.onActivity { activity ->
                showPage(activity, if (location.page == 0) 1 else 0)
                showPage(activity, location.page)
                assertEquals(implementation, editor(activity).text.toString())
                assertEquals("prepared draft\nñ", input(activity).text.toString())
            }
            scenario.recreate()
            scenario.onActivity { activity ->
                assertEquals(location.page, field(activity, "currentPage"))
                assertEquals("The active file is restored", implementation, editor(activity).text.toString())
                assertEquals("prepared draft\nñ", input(activity).text.toString())
                assertEquals("Each file remains available", 3,
                    descendants(content(activity)).count { it.tag?.toString()?.startsWith("cpp_file_") == true })
            }
            assertEquals(header, readFile(scenario, "diferencia.h"))
            assertEquals(implementation, readFile(scenario, "diferencia.cpp"))
            assertSuccessful(run(scenario), "11")
        }
        withProject(INTERFACES, "sobres.h") { other, _ ->
            assertEquals("A different lesson owns a different input draft", "", currentInput(other))
            assertSuccessful(run(other), "Varios sobres: 3")
        }
    }

    @Test fun authoredSobresProjectChecksDomainCapacityAndDifferentFailureStages() {
        withProject(INTERFACES, "sobres.h") { scenario, _ ->
            assertSuccessful(run(scenario), "Varios sobres: 3")
            val main = readFile(scenario, "main.cpp")
            val implementation = readFile(scenario, "sobres.cpp")
            val header = readFile(scenario, "sobres.h")
            editFile(scenario, "main.cpp") { SOBRES_BOUNDARIES }
            assertSuccessful(run(scenario), "Sin tarjetas: 0\nUn sobre: 1\nVarios sobres: 2\nVarios sobres: 3\nVarios sobres: 10")
            editFile(scenario, "main.cpp") { main }
            editFile(scenario, "sobres.cpp") { implementation.replace("/ 12", "/ 20").replace("% 12", "% 20") }
            assertSuccessful(run(scenario), "Varios sobres: 2")
            editFile(scenario, "sobres.cpp") { implementation }
            editFile(scenario, "main.cpp") { main.replace("#include \"sobres.h\"", "") }
            assertCompilationFailure(run(scenario), "main.cpp", "descripcion_sobres")
            editFile(scenario, "main.cpp") { main }
            editFile(scenario, "sobres.h") { header.replace("int sobres_necesarios(int tarjetas);", "") }
            assertCompilationFailure(run(scenario), "main.cpp", "sobres_necesarios")
            editFile(scenario, "sobres.h") { header }
            enableSource(scenario, "sobres.cpp", false)
            assertLinkFailure(run(scenario), "sobres_necesarios")
            enableSource(scenario, "sobres.cpp", true)
            val bodyStart = implementation.indexOf("int sobres_necesarios(")
            val bodyEnd = implementation.indexOf("\n\nstd::string descripcion_sobres", bodyStart)
            assertTrue("The published implementation contains the ordinary function body", bodyStart >= 0 && bodyEnd > bodyStart)
            val body = implementation.substring(bodyStart, bodyEnd)
            assertTrue("The shared interface declares the same function", header.contains("int sobres_necesarios(int tarjetas);"))
            editFile(scenario, "sobres.h") { header.replace("int sobres_necesarios(int tarjetas);", body) }
            editFile(scenario, "sobres.cpp") { implementation.removeRange(bodyStart, bodyEnd) }
            val duplicate = run(scenario)
            assertEquals("LINK", duplicate.phase)
            assertTrue(duplicate.compilation, duplicate.compilation.contains("duplicate symbol", ignoreCase = true))
            assertTrue(duplicate.compilation, duplicate.compilation.contains("sobres_necesarios"))
            assertEquals("", duplicate.stdout)
            editFile(scenario, "sobres.h") { header }
            editFile(scenario, "sobres.cpp") { implementation }
            assertSuccessful(run(scenario), "Varios sobres: 3")
        }
    }

    @Test fun authoredSerieProjectTransfersTheInterfaceAndChecksBothDomainBoundaries() {
        withProject(INTERFACES, "serie.h") { scenario, _ ->
            assertSuccessful(run(scenario), "En curso: 2\nTerminada: 0\nTerminada: 0")
            editFile(scenario, "main.cpp") { SERIE_BOUNDARIES }
            assertSuccessful(run(scenario), "En curso: 2\nTerminada: 0\nTerminada: 0\nEn curso: 40\nTerminada: 0")
            editFile(scenario, "main.cpp") { SERIE_TRANSFER }
            assertSuccessful(run(scenario), "En curso: 8\nTerminada: 0")
        }
    }

    @Test fun virtualFileCreationRejectsEscapesAndDeletionRetainsAHeadersOnlyDraft() {
        withProject(DIFERENCIA, "diferencia.h") { scenario, location ->
            val main = readFile(scenario, "main.cpp")
            assertSuccessful(run(scenario), "7")
            scenario.onActivity { tagged(it, "cpp_add_file").performClick() }
            onView(withTagValue(equalTo("cpp_new_file_name"))).inRoot(isDialog())
                .perform(replaceText("../outside.cpp"), closeSoftKeyboard())
            onView(withId(android.R.id.button1)).inRoot(isDialog()).perform(click())
            onView(withTagValue(equalTo("cpp_new_file_name"))).inRoot(isDialog()).check { view, error ->
                if (error != null) throw error
                assertNotNull("A path outside the project remains rejected in the dialog", (view as EditText).error)
            }
            onView(withId(android.R.id.button2)).inRoot(isDialog()).perform(click())
            scenario.onActivity { activity ->
                assertEquals(3, fileCount(activity))
                assertTrue(descendants(content(activity)).none { it.tag == "cpp_file_../outside.cpp" })
            }
            createFile(scenario, "helper.h", "#ifndef CURRICULAR_HELPER_H\n#define CURRICULAR_HELPER_H\nint auxiliary(int value);\n#endif\n")
            createFile(scenario, "helper.cpp", "#include \"helper.h\"\nint auxiliary(int value) { return value * 2; }\n")
            scenario.onActivity { activity ->
                assertEquals(5, fileCount(activity))
                assertTrue("A newly created .cpp is selected for compilation",
                    (tagged(activity, "cpp_source_enabled") as CheckBox).isChecked)
            }
            editFile(scenario, "main.cpp") {
                "#include \"helper.h\"\n" + main.replace("diferencia(9, 2)", "auxiliary(diferencia(9, 2))")
            }
            assertSuccessful(run(scenario), "14")
            readFile(scenario, "helper.cpp")
            scenario.recreate()
            scenario.onActivity { activity ->
                assertEquals(location.page, field(activity, "currentPage"))
                assertEquals(5, fileCount(activity))
                assertEquals("helper.cpp", editor(activity).contentDescription.toString())
                assertTrue(editor(activity).text.toString().contains("value * 2"))
            }
            deleteFile(scenario, "helper.cpp")
            assertLinkFailure(run(scenario), "auxiliary")
            editFile(scenario, "main.cpp") { main }
            deleteFile(scenario, "helper.h")
            assertSuccessful(run(scenario), "7")
            deleteFile(scenario, "main.cpp")
            deleteFile(scenario, "diferencia.cpp")
            assertConfigurationFailure(scenario)
            scenario.recreate()
            scenario.onActivity { activity ->
                assertEquals("A headers-only project remains an editable saved draft", 1, fileCount(activity))
                assertEquals("diferencia.h", editor(activity).contentDescription.toString())
                assertTrue(editor(activity).text.toString().contains("int diferencia("))
            }
            assertConfigurationFailure(scenario)
        }
    }

    @Test fun multiFileFullscreenKeepsAnEditableViewportAndShortcutsAboveTheRealLandscapeIme() {
        withProject(DIFERENCIA, "diferencia.h") { scenario, _ ->
            var originalOrientation = ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
            var draft = ""
            var compactEditorHeight = 0
            try {
                scenario.onActivity { activity ->
                    originalOrientation = activity.requestedOrientation
                    val source = editor(activity)
                    draft = source.text.toString() + "\n// compact multi-file draft"
                    source.setText(draft)
                    hideIme(activity)
                    tagged(activity, "cpp_compiler_fullscreen").performClick()
                    activity.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
                }
                awaitUi(scenario, "The project enters landscape fullscreen before requesting the IME") { activity ->
                    activity.resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE &&
                        field(activity, "compilerFullscreenActive") == true && activity.hasWindowFocus() &&
                        !imeVisible(activity) && !editor(activity).isLayoutRequested
                }
                scenario.onActivity { activity ->
                    val source = editor(activity)
                    assertTrue("The established code input keeps keyboard focus integration", source.requestFocus())
                    source.setSelection(source.length())
                    (activity.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager)
                        .showSoftInput(source, InputMethodManager.SHOW_IMPLICIT)
                }
                awaitUi(scenario, "The real IME leaves a visible source line and active shortcuts") { activity ->
                    val source = editor(activity)
                    val toolbar = activity.findViewById<View>(R.id.programmingKeyboardBar)
                    val visibleSource = Rect()
                    imeVisible(activity) && source.hasFocus() && toolbar.isShown && toolbar.height > 0 &&
                        tagged(activity, "cpp_project_controls").visibility == View.GONE &&
                        !source.isLayoutRequested && !toolbar.isLayoutRequested &&
                        source.getGlobalVisibleRect(visibleSource) && visibleSource.height() >= source.lineHeight
                }
                scenario.onActivity { activity ->
                    val source = editor(activity)
                    val toolbar = activity.findViewById<View>(R.id.programmingKeyboardBar)
                    val sourceBounds = Rect().also { assertTrue(source.getGlobalVisibleRect(it)) }
                    val toolbarBounds = Rect().also { assertTrue(toolbar.getGlobalVisibleRect(it)) }
                    val frame = Rect().also(activity.window.decorView::getWindowVisibleDisplayFrame)
                    compactEditorHeight = source.height
                    assertSame("The source retains focus while compacting", source, activity.currentFocus)
                    assertTrue("A full source line remains visible: $sourceBounds", sourceBounds.height() >= source.lineHeight)
                    assertTrue("Source stays above the shortcut bar: source=$sourceBounds toolbar=$toolbarBounds",
                        sourceBounds.bottom <= toolbarBounds.top + 1)
                    assertTrue("Shortcut bar remains inside the real window viewport: toolbar=$toolbarBounds frame=$frame",
                        toolbarBounds.bottom <= frame.bottom + 1)
                    assertEquals(draft, source.text.toString())
                    val tab = descendants(activity.findViewById(R.id.programmingKeyboardBarContent))
                        .filterIsInstance<TextView>().single { it.text.toString() == activity.getString(R.string.lesson_tab_label) }
                    assertTrue("The existing TAB shortcut is available with the IME", tab.isShown && tab.isEnabled)
                    tab.performClick()
                    draft += KeyboardShortcutSettingsRepository.getTabText()
                    assertEquals("The shortcut edits this project's selected source", draft, source.text.toString())
                    assertTrue("Using a shortcut preserves editor focus", source.hasFocus())
                    hideIme(activity)
                }
                awaitUi(scenario, "Hiding the IME restores the project file controls and editor height") { activity ->
                    !imeVisible(activity) && tagged(activity, "cpp_project_controls").visibility == View.VISIBLE &&
                        !editor(activity).isLayoutRequested && editor(activity).height > compactEditorHeight
                }
                scenario.onActivity { activity ->
                    assertEquals(draft, editor(activity).text.toString())
                    tagged(activity, "cpp_compiler_fullscreen").performClick()
                }
                awaitUi(scenario, "Exiting fullscreen restores the authored project's normal controls") { activity ->
                    field(activity, "compilerFullscreenActive") == false &&
                        tagged(activity, "cpp_project_controls").isShown && tagged(activity, "cpp_run").isShown
                }
                scenario.onActivity { assertEquals(draft, editor(it).text.toString()) }
                assertSuccessful(run(scenario), "7")
            } finally {
                scenario.onActivity { activity ->
                    hideIme(activity)
                    activity.requestedOrientation = originalOrientation
                }
            }
        }
    }

    private data class LabLocation(val page: Int, val block: Int)
    private data class PedidosLocations(val compiler: LabLocation, val puzzle: LabLocation)
    private data class RunResult(val status: String, val phase: String, val compilation: String,
                                 val stdout: String, val stderr: String, val finishedStatuses: Map<Int, String>) {
        fun finishedStatus(exitCode: Int) = finishedStatuses.getValue(exitCode)
    }

    private fun withProject(lessonId: String, header: String,
                            test: (ActivityScenario<LessonActivity>, LabLocation) -> Unit) =
        withAsset(lessonId, { block ->
            val files = block.optJSONArray("projectFiles")
            files != null && (0 until files.length()).any { files.getJSONObject(it).optString("path") == header }
        }, test)

    private fun withAsset(lessonId: String, select: (JSONObject) -> Boolean,
                          test: (ActivityScenario<LessonActivity>, LabLocation) -> Unit) =
        withAsset(lessonId, null, select, test)

    private fun withAsset(lessonId: String, storage: LessonStatePosition?, select: (JSONObject) -> Boolean,
                          test: (ActivityScenario<LessonActivity>, LabLocation) -> Unit) {
        val lesson = LessonRepository.loadLessonJson(context, ROUTE, lessonId)
        val location = authoredLocation(lessonId, lesson, "cpp_compiler", storage, select)
        ProgressRepository.resetLesson(context, ROUTE, lessonId)
        ProgressRepository.saveCurrentPage(context, ROUTE, lessonId, location.page)
        selectedLab = lessonId to location
        try {
            ActivityScenario.launch<LessonActivity>(lessonIntent(lessonId)).use { scenario ->
                scenario.onActivity { activity ->
                    activity.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                    assertEquals("The published asset is loaded", lessonId, (field(activity, "lessonJson") as JSONObject).getString("id"))
                    assertEquals(location.page, field(activity, "currentPage"))
                    tagged(activity, "cpp_code_input")
                }
                instrumentation.waitForIdleSync()
                test(scenario, location)
            }
        } finally { selectedLab = null }
    }

    private fun authoredLocation(lessonId: String, lesson: JSONObject, type: String,
                                 storage: LessonStatePosition? = null,
                                 select: (JSONObject) -> Boolean): LabLocation {
        val locations = buildList {
            val pages = lesson.getJSONArray("pages")
            for (page in 0 until pages.length()) {
                val blocks = pages.getJSONObject(page).getJSONArray("blocks")
                for (block in 0 until blocks.length()) {
                    val candidate = blocks.getJSONObject(block)
                    if (candidate.optString("type") == type &&
                        (storage == null || LessonStateCoordinates.block(context, lessonId, page, block) == storage) &&
                        select(candidate)) add(LabLocation(page, block))
                }
            }
        }
        assertEquals("One authored $type matches $lessonId at historical identity $storage", 1, locations.size)
        return locations.single()
    }

    private fun blockAt(lesson: JSONObject, location: LabLocation) = lesson.getJSONArray("pages")
        .getJSONObject(location.page).getJSONArray("blocks").getJSONObject(location.block)

    /** Use the authored objective as well as its immutable storage identity. A nearby
     * prediction or a different program with the same prepared input is not this lab. */
    private fun declaresOutput(block: JSONObject, standardInput: String, expected: String): Boolean {
        val cases = block.optJSONObject("outputCheck")?.optJSONArray("cases") ?: return false
        return (0 until cases.length()).any { index ->
            val case = cases.getJSONObject(index)
            val outputs = case.optJSONArray("outputs") ?: return@any false
            case.optString("standardInput") == standardInput &&
                (0 until outputs.length()).any { outputs.getString(it) == expected }
        }
    }

    private fun declaresPedidosOutput(block: JSONObject) =
        declaresOutput(block, "", "1:7\n0:7\n1:1\n0:1\n0:7")

    private fun pedidosLocations(lessonId: String, lesson: JSONObject): PedidosLocations {
        val compiler = authoredLocation(lessonId, lesson, "cpp_compiler", LessonStatePosition(1, 2)) {
            declaresPedidosOutput(it)
        }
        val puzzle = authoredLocation(lessonId, lesson, "math_puzzle") {
            it.optString("id") == "${lessonId}__traza_de_solicitudes"
        }
        assertEquals("The original table and compiler remain on the same curricular page", compiler.page, puzzle.page)
        return PedidosLocations(compiler, puzzle)
    }

    private fun withFixture(source: String, preparedInput: String? = null,
                            test: (ActivityScenario<LessonActivity>) -> Unit) {
        assertTrue(context.getSharedPreferences(DRAFTS, Context.MODE_PRIVATE).edit().clear().commit())
        ProgressRepository.resetLesson(context, ROUTE, READING)
        val block = JSONObject().put("type", "cpp_compiler").put("initialCode", source)
        if (preparedInput != null) block.put("initialStandardInput", preparedInput)
        val lesson = JSONObject().put("id", READING).put("title", "Technical curricular runtime check")
            .put("pages", JSONArray().put(JSONObject().put("blocks", JSONArray().put(block))))
        val startupCompleted = WeakHashMap<LessonActivity, Long>()
        val observer = object : Application.ActivityLifecycleCallbacks {
            override fun onActivityCreated(activity: Activity, state: Bundle?) {
                if (activity is LessonActivity && activity.packageName == context.packageName)
                    activity.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
            }
            override fun onActivityStarted(activity: Activity) = Unit
            override fun onActivityResumed(activity: Activity) {
                if (Build.VERSION.SDK_INT < 29 && activity is LessonActivity)
                    startupCompleted[activity] = SystemClock.uptimeMillis()
            }
            override fun onActivityPostResumed(activity: Activity) {
                if (activity is LessonActivity) startupCompleted[activity] = SystemClock.uptimeMillis()
            }
            override fun onActivityPaused(activity: Activity) = Unit
            override fun onActivityStopped(activity: Activity) = Unit
            override fun onActivitySaveInstanceState(activity: Activity, state: Bundle) = Unit
            override fun onActivityDestroyed(activity: Activity) = Unit
        }
        val application = context.applicationContext as Application
        application.registerActivityLifecycleCallbacks(observer)
        try {
            ActivityScenario.launch<LessonActivity>(lessonIntent(READING)).use { scenario ->
                awaitOriginalReadingStartupAndInject(scenario, lesson, startupCompleted)
                instrumentation.waitForIdleSync()
                test(scenario)
            }
        } finally { application.unregisterActivityLifecycleCallbacks(observer) }
    }

    /** The asset read is synchronous; the Activity/window/configuration startup may still
     * settle or replace its instance. Only the prepared instance receives the fixture. */
    private fun awaitOriginalReadingStartupAndInject(scenario: ActivityScenario<LessonActivity>, lesson: JSONObject,
                                                     startupCompleted: Map<LessonActivity, Long>) {
        val originalPages = LessonRepository.loadLessonJson(context, ROUTE, READING).getJSONArray("pages").length()
        val deadline = SystemClock.uptimeMillis() + 12_000L
        var previousKey = ""
        var stableSince = 0L
        var samples = 0
        var diagnostic = ""
        while (SystemClock.uptimeMillis() < deadline) {
            instrumentation.waitForIdleSync()
            var injected = false
            scenario.onActivity { activity ->
                val original = field(activity, "lessonJson") as? JSONObject
                val root = content(activity)
                val configuration = activity.resources.configuration
                val resumedAt = startupCompleted[activity]
                val ready = resumedAt != null && !activity.isDestroyed && !activity.isFinishing &&
                    field(activity, "currentLessonId") == READING && original?.optString("id") == READING &&
                    original?.optJSONArray("pages")?.length() == originalPages &&
                    field(activity, "totalPages") == originalPages && field(activity, "currentPage") == 0 &&
                    (field(activity, "pageViewCache") as Map<*, *>).containsKey(0) &&
                    root.childCount > 0 && root.width > 0 && root.height > 0 && root.isAttachedToWindow &&
                    !root.isLayoutRequested && activity.window.decorView.hasWindowFocus()
                val key = "${System.identityHashCode(activity)}/${System.identityHashCode(original)}/" +
                    "${System.identityHashCode(root.getChildAt(0))}/${root.width}x${root.height}/" +
                    "${configuration.orientation}/${configuration.densityDpi}/${configuration.locales.toLanguageTags()}"
                val now = SystemClock.uptimeMillis()
                if (!ready || key != previousKey) {
                    stableSince = if (ready) now else 0L
                    samples = if (ready) 1 else 0
                } else samples++
                previousKey = key
                diagnostic = "$key ready=$ready, page=${field(activity, "currentPage")}, total=${field(activity, "totalPages")}, " +
                    "loaded=${original?.optString("id")}, focus=${activity.window.decorView.hasWindowFocus()}, postResume=$resumedAt"
                if (ready && samples >= 4 && now - stableSince >= 500L) {
                    require(injectedFixtureActivityIdentity == null) { "Inject the runtime fixture once; never repair a later loss by reinjection." }
                    val metadata = JSONObject().put("sourceLessonId", READING).put("originalPageCount", originalPages)
                        .put("activityIdentity", System.identityHashCode(activity)).put("postResumeUptime", resumedAt)
                        .put("stableStartupMillis", now - stableSince).put("stableSamples", samples)
                        .put("orientation", configuration.orientation).put("requestedOrientation", activity.requestedOrientation)
                        .put("contentWidth", root.width).put("contentHeight", root.height)
                    setField(activity, "lessonJson", lesson)
                    setField(activity, "totalPages", 1)
                    setField(activity, "currentPage", 0)
                    listOf("pageViewCache", "pageRequiredExercises", "pageCompletedExercises").forEach { name ->
                        (field(activity, name) as MutableMap<*, *>).clear()
                    }
                    invoke(activity, "renderCurrentPage")
                    injectedFixtureActivityIdentity = System.identityHashCode(activity)
                    val directory = runtimeDiagnosticDirectory()
                    File(directory, "prepared-runtime-fixture-${SystemClock.uptimeMillis()}.json").writeText(metadata.toString(2))
                    injected = true
                }
            }
            if (injected) return
            SystemClock.sleep(100L)
        }
        fail("The original reading lesson did not finish a stable focused startup before runtime QA injection: $diagnostic")
    }

    private fun runWithInput(scenario: ActivityScenario<LessonActivity>, text: String, expected: String,
                             expectedError: String = "") {
        scenario.onActivity { input(it).setText(text) }
        assertSuccessful(run(scenario), expected, expectedError)
    }

    private fun run(scenario: ActivityScenario<LessonActivity>): RunResult {
        var completedBefore = 0
        var reserveBefore = 0f
        scenario.onActivity { activity ->
            completedBefore = field(activity, "completedExercises") as Int
            reserveBefore = context.getSharedPreferences(ATTEMPTS, Context.MODE_PRIVATE).getFloat("reserve_$ROUTE", 0f)
            val button = tagged(activity, "cpp_run")
            assertTrue("The renderer exposes an enabled Run action", button.isEnabled)
            button.performClick()
            assertFalse("The real native job disables Run while working", button.isEnabled)
        }
        val deadline = SystemClock.uptimeMillis() + 120_000L
        var finished = false
        do {
            instrumentation.waitForIdleSync()
            scenario.onActivity { finished = tagged(it, "cpp_run").isEnabled }
            if (!finished) SystemClock.sleep(50L)
        } while (!finished && SystemClock.uptimeMillis() < deadline)
        var result: RunResult? = null
        scenario.onActivity { activity ->
            result = RunResult(text(activity, "cpp_status"), tagged(activity, "cpp_status").getTag(R.id.cpp_lab_build_phase)?.toString().orEmpty(),
                text(activity, "cpp_compilation_output"), text(activity, "cpp_stdout"), text(activity, "cpp_stderr"),
                listOf(0, 3).associateWith { activity.getString(
                    if (it == 0) R.string.cpp_runtime_finished else R.string.cpp_execution_failed, it) })
            assertEquals("A lab run cannot complete an evaluated exercise", completedBefore, field(activity, "completedExercises"))
            assertTrue("A compiler run cannot charge attempts", context.getSharedPreferences(ATTEMPTS, Context.MODE_PRIVATE)
                .getFloat("reserve_$ROUTE", 0f) >= reserveBefore)
        }
        assertTrue("Native compilation/execution completes within 120 seconds: $result", finished)
        return requireNotNull(result).also { actual ->
            assertFalse("Diagnostics identify virtual source files without internal job paths: ${actual.compilation}",
                actual.compilation.contains("cpp-jobs/"))
            assertFalse("Diagnostics cannot expose the private cache directory: ${actual.compilation}",
                actual.compilation.contains(context.cacheDir.absolutePath))
        }
    }

    private fun assertSuccessful(result: RunResult, expected: String, expectedError: String = "") {
        assertEquals("The program exits successfully: $result", result.finishedStatus(0), result.status)
        assertEquals("Compilation diagnostics are separate from stdout", "", result.compilation)
        assertEquals("Only the program writes stdout", expected, result.stdout.trimEnd())
        assertEquals("Program stderr remains independent", expectedError, result.stderr)
    }

    private fun assertCompilationFailure(result: RunResult, source: String, detail: String) {
        assertEquals(result.compilation, "COMPILATION", result.phase)
        assertTrue(result.compilation, result.compilation.contains(source))
        assertTrue(result.compilation, result.compilation.contains(detail))
        assertEquals("", result.stdout)
        assertEquals("", result.stderr)
    }

    private fun assertLinkFailure(result: RunResult, symbol: String) {
        assertEquals(result.compilation, "LINK", result.phase)
        assertTrue(result.compilation, result.compilation.contains("undefined symbol", ignoreCase = true))
        assertTrue(result.compilation, result.compilation.contains(symbol))
        assertEquals("", result.stdout)
        assertEquals("", result.stderr)
    }

    private fun assertConfigurationFailure(scenario: ActivityScenario<LessonActivity>) {
        scenario.onActivity { activity ->
            val completed = field(activity, "completedExercises")
            val reserve = context.getSharedPreferences(ATTEMPTS, Context.MODE_PRIVATE).getFloat("reserve_$ROUTE", 0f)
            val button = tagged(activity, "cpp_run")
            assertTrue(button.isEnabled)
            button.performClick()
            assertTrue("Invalid project configuration cannot start a native job", button.isEnabled)
            assertEquals("CONFIGURATION", tagged(activity, "cpp_status").getTag(R.id.cpp_lab_build_phase).toString())
            assertEquals(activity.getString(R.string.cpp_project_invalid), text(activity, "cpp_status"))
            assertEquals(activity.getString(R.string.cpp_project_invalid), text(activity, "cpp_compilation_output"))
            assertEquals("A previous successful output cannot survive a rejected build", "", text(activity, "cpp_stdout"))
            assertEquals("A rejected build has no program stderr", "", text(activity, "cpp_stderr"))
            assertEquals(completed, field(activity, "completedExercises"))
            assertTrue(context.getSharedPreferences(ATTEMPTS, Context.MODE_PRIVATE)
                .getFloat("reserve_$ROUTE", 0f) >= reserve)
        }
    }

    private fun selectOptionalCompiler(scenario: ActivityScenario<LessonActivity>) {
        scenario.onActivity { activity ->
            val selector = tagged(activity, "cpp_compiler_variant") as Spinner
            assertTrue("The executable optional split is offered by the curricular UI", selector.count >= 2)
            selector.setSelection(1)
        }
        instrumentation.waitForIdleSync()
    }

    private fun editFile(scenario: ActivityScenario<LessonActivity>, path: String, transform: (String) -> String) {
        scenario.onActivity { activity ->
            tagged(activity, "cpp_file_$path").performClick()
            editor(activity).setText(transform(editor(activity).text.toString()))
        }
        instrumentation.waitForIdleSync()
    }

    private fun readFile(scenario: ActivityScenario<LessonActivity>, path: String): String {
        var result = ""
        scenario.onActivity { activity ->
            tagged(activity, "cpp_file_$path").performClick()
            result = editor(activity).text.toString()
        }
        return result
    }

    private fun createFile(scenario: ActivityScenario<LessonActivity>, path: String, code: String) {
        scenario.onActivity { tagged(it, "cpp_add_file").performClick() }
        onView(withTagValue(equalTo("cpp_new_file_name"))).inRoot(isDialog())
            .perform(replaceText(path), closeSoftKeyboard())
        onView(withId(android.R.id.button1)).inRoot(isDialog()).perform(click())
        scenario.onActivity { activity ->
            assertEquals(path, editor(activity).contentDescription.toString())
            editor(activity).setText(code)
        }
    }

    private fun deleteFile(scenario: ActivityScenario<LessonActivity>, path: String) {
        scenario.onActivity { activity ->
            tagged(activity, "cpp_file_$path").performClick()
            tagged(activity, "cpp_delete_file").performClick()
            assertTrue("The selected virtual file is removed", descendants(content(activity)).none { it.tag == "cpp_file_$path" })
        }
    }

    private fun fileCount(activity: LessonActivity) = descendants(content(activity))
        .count { it.tag?.toString()?.startsWith("cpp_file_") == true }

    private fun currentInput(scenario: ActivityScenario<LessonActivity>): String {
        var result = ""
        scenario.onActivity { result = input(it).text.toString() }
        return result
    }

    private fun enableSource(scenario: ActivityScenario<LessonActivity>, path: String, enabled: Boolean) {
        scenario.onActivity { activity ->
            tagged(activity, "cpp_file_$path").performClick()
            val checkbox = tagged(activity, "cpp_source_enabled") as CheckBox
            if (checkbox.isChecked != enabled) checkbox.performClick()
            assertEquals("Compile only the selected translation units", enabled, checkbox.isChecked)
        }
    }

    private fun showPage(activity: LessonActivity, page: Int) {
        invoke(activity, "saveCurrentPageState")
        setField(activity, "currentPage", page)
        ProgressRepository.saveCurrentPage(context, ROUTE, field(activity, "currentLessonId") as String, page)
        invoke(activity, "renderCurrentPage")
    }

    private fun lessonIntent(lessonId: String): Intent {
        val parts = lessonId.split("__")
        return Intent(context, LessonActivity::class.java).putExtra("routeId", ROUTE)
            .putExtra("courseId", parts[0]).putExtra("unitId", parts[1]).putExtra("lessonId", lessonId)
            .putExtra("lessonNumber", 1).putExtra("lessonTotal", 1)
    }

    private fun preferenceFiles(): Set<String> = File(context.applicationInfo.dataDir, "shared_prefs")
        .listFiles().orEmpty().filter { it.name.endsWith(".xml") }.map { it.name.removeSuffix(".xml") }.toSet()
    private fun content(activity: LessonActivity): ViewGroup = activity.findViewById(R.id.lessonContent)
    private fun editor(activity: LessonActivity) = tagged(activity, "cpp_code_input") as EditText
    private fun input(activity: LessonActivity) = tagged(activity, "cpp_input") as EditText
    private fun text(activity: LessonActivity, tag: String) = (tagged(activity, tag) as TextView).text.toString()
    private fun tagged(activity: LessonActivity, tag: String): View {
        val page = content(activity)
        val lesson = field(activity, "lessonJson") as JSONObject
        val pageIndex = field(activity, "currentPage") as Int
        val blocks = lesson.getJSONArray("pages").getJSONObject(pageIndex).getJSONArray("blocks")
        val compilerBlocks = (0 until blocks.length()).filter {
            blocks.getJSONObject(it).optString("type") == "cpp_compiler"
        }
        val selected = selectedLab?.takeIf { it.first == lesson.optString("id") && it.second.page == pageIndex }?.second
        val compilerIndex = if (selected != null) compilerBlocks.indexOf(selected.block) else {
            assertEquals("A technical fixture without a selected identity has one compiler", 1, compilerBlocks.size)
            0
        }
        assertTrue("The selected historical lab is a displayed compiler", compilerIndex >= 0)
        val runs = descendants(page).filter { it.tag == "cpp_run" }
        assertEquals("Every displayed compiler owns its Run action", compilerBlocks.size, runs.size)
        fun cardIndex(run: View): Int {
            var runChild: View = run
            while (runChild.parent !== page) runChild = runChild.parent as? View
                ?: error("The compiler Run control is not inside the current lesson block")
            return (page.indexOfChild(runChild) - 1 downTo 0).first { index ->
                descendants(page.getChildAt(index)).any { it.tag == "cpp_code_input" && it is FreeScrollEditText }
            }
        }
        val start = cardIndex(runs[compilerIndex])
        val end = if (compilerIndex + 1 < runs.size) cardIndex(runs[compilerIndex + 1]) else page.childCount
        // The real renderer places each card, prepared input, Run, status and console
        // consecutively. Scope every tag to that selected compiler, including when
        // an independent compiler or prediction precedes it on the same page.
        val scope = if (tag == "cpp_code_input") descendants(page.getChildAt(start))
            else (start until end).flatMap { descendants(page.getChildAt(it)) }
        val matches = scope.filter { it.tag == tag }
        if (tag == "cpp_code_input") assertTrue("The selected editor is the actual compiler component",
            matches.singleOrNull() is FreeScrollEditText)
        if (matches.size != 1) diagnoseUnexpectedRuntimeControls(activity, tag, matches.size)
        return matches.single()
    }
    private fun runtimeDiagnosticDirectory() = File(requireNotNull(context.getExternalFilesDir(null)), "cpp-runtime-regression-diagnostics").apply { mkdirs() }
    private fun diagnoseUnexpectedRuntimeControls(activity: LessonActivity, tag: String, count: Int) {
        val name = "runtime-controls-${SystemClock.uptimeMillis()}-$tag"
        val lesson = field(activity, "lessonJson") as JSONObject
        val page = field(activity, "currentPage") as Int
        val blocks = lesson.optJSONArray("pages")?.optJSONObject(page)?.optJSONArray("blocks")
        val metadata = JSONObject().put("missingOrRepeatedTag", tag).put("matchCount", count)
            .put("activityIdentity", System.identityHashCode(activity))
            .put("injectedFixtureActivityIdentity", injectedFixtureActivityIdentity ?: JSONObject.NULL)
            .put("currentLessonId", field(activity, "currentLessonId")).put("loadedLessonId", lesson.optString("id"))
            .put("loadedLessonTitle", lesson.optString("title")).put("currentPage", page).put("totalPages", field(activity, "totalPages"))
            .put("exerciseFullscreenActive", field(activity, "exerciseFullscreenActive"))
            .put("compilerFullscreenActive", field(activity, "compilerFullscreenActive"))
            .put("orientation", activity.resources.configuration.orientation).put("requestedOrientation", activity.requestedOrientation)
            .put("lessonContentTags", JSONArray(descendants(content(activity)).mapNotNull { it.tag?.toString() }))
            .put("decorViewTags", JSONArray(descendants(activity.window.decorView).mapNotNull { it.tag?.toString() }))
            .put("blockTypes", JSONArray().apply { if (blocks != null)
                for (index in 0 until blocks.length()) put(blocks.optJSONObject(index)?.optString("type") ?: JSONObject.NULL) })
        runCatching {
            val screen = requireNotNull(instrumentation.uiAutomation.takeScreenshot())
            try { File(runtimeDiagnosticDirectory(), "$name.png").outputStream().use {
                check(screen.compress(Bitmap.CompressFormat.PNG, 100, it))
            } } finally { screen.recycle() }
            metadata.put("nativeScreenshot", "$name.png")
        }.onFailure { metadata.put("screenshotError", it.toString()) }
        runCatching { File(runtimeDiagnosticDirectory(), "$name.json").writeText(metadata.toString(2)) }
        println("CPP runtime native diagnostic: $name; expected one $tag, actual=$count")
    }
    private fun descendants(root: View): List<View> = buildList {
        add(root)
        if (root is ViewGroup) for (index in 0 until root.childCount) addAll(descendants(root.getChildAt(index)))
    }
    private fun imeVisible(activity: LessonActivity) = ViewCompat.getRootWindowInsets(activity.window.decorView)
        ?.isVisible(WindowInsetsCompat.Type.ime()) == true
    private fun hideIme(activity: LessonActivity) {
        (activity.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager)
            .hideSoftInputFromWindow(activity.window.decorView.windowToken, 0)
    }
    private fun awaitUi(scenario: ActivityScenario<LessonActivity>, label: String, ready: (LessonActivity) -> Boolean) {
        val deadline = SystemClock.uptimeMillis() + 15_000L
        var stableSamples = 0
        var diagnostic = ""
        do {
            instrumentation.waitForIdleSync()
            scenario.onActivity { activity ->
                val source = editor(activity)
                val toolbar = activity.findViewById<View>(R.id.programmingKeyboardBar)
                val controls = tagged(activity, "cpp_project_controls")
                diagnostic = "orientation=${activity.resources.configuration.orientation} ime=${imeVisible(activity)} " +
                    "editor=${source.width}x${source.height} focus=${source.hasFocus()} " +
                    "toolbar=${toolbar.visibility}/${toolbar.height} controls=${controls.visibility}"
                stableSamples = if (ready(activity)) stableSamples + 1 else 0
            }
            if (stableSamples >= 3) return
            SystemClock.sleep(50L)
        } while (SystemClock.uptimeMillis() < deadline)
        fail("$label; $diagnostic")
    }
    private fun field(activity: LessonActivity, name: String): Any? = LessonActivity::class.java.getDeclaredField(name)
        .apply { isAccessible = true }.get(activity)
    private fun setField(activity: LessonActivity, name: String, value: Any) = LessonActivity::class.java.getDeclaredField(name)
        .apply { isAccessible = true }.set(activity, value)
    private fun invoke(activity: LessonActivity, name: String) = LessonActivity::class.java.getDeclaredMethod(name)
        .apply { isAccessible = true }.invoke(activity)

    companion object {
        private const val ROUTE = "programming"
        private const val DRAFTS = "cpp_lesson_drafts"
        private const val ATTEMPTS = "attempt_reserve_seconds_test"
        private const val READING = "cpp_fundamentals__objetos_y_estado__leer_una_entrada_conocida"
        private const val DIFERENCIA = "cpp_fundamentals__funciones_y_contratos__declaraciones_y_firmas"
        private const val INTERFACES = "cpp_fundamentals__funciones_y_contratos__interfaces_cabeceras_y_programas_con_varios_archivos"
        private val KNOWN_PREFERENCES = setOf(DRAFTS, ATTEMPTS, "learning_progress", "navigation_state",
            "lesson_narration_settings", "app_settings", "ui_language_settings")
        private val STREAM_SOURCE = """
            #include <iostream>
            #include <limits>
            #include <string>
            int main() {
                int number = 0;
                if (!(std::cin >> number)) {
                    std::cout << (std::cin.eof() ? "EMPTY" : "INVALID");
                    return 0;
                }
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                std::string line, extra;
                if (!std::getline(std::cin, line)) return 4;
                bool eof = !std::getline(std::cin, extra);
                std::cout << number << '|' << line << '|' << (eof ? "EOF" : "EXTRA");
                std::cerr << "stream stderr\n";
                return 0;
            }
        """.trimIndent()
        private val SOBRES_BOUNDARIES = """
            #include "sobres.h"
            #include <iostream>
            #include <initializer_list>
            int main() {
                for (int tarjetas : {0, 12, 13, 25, 120}) {
                    std::cout << descripcion_sobres(tarjetas) << ": " << sobres_necesarios(tarjetas) << '\n';
                }
            }
        """.trimIndent()
        private val SERIE_BOUNDARIES = """
            #include "serie.h"
            #include <iostream>
            #include <initializer_list>
            #include <utility>
            int main() {
                for (auto caso : {std::pair{3, 5}, std::pair{0, 0}, std::pair{5, 5}, std::pair{0, 40}, std::pair{40, 40}}) {
                    std::cout << estado_serie(caso.first, caso.second) << ": "
                        << tareas_pendientes(caso.first, caso.second) << '\n';
                }
            }
        """.trimIndent()
        private val SERIE_TRANSFER = """
            #include "serie.h"
            #include <iostream>
            int main() {
                std::cout << estado_serie(0, 8) << ": " << tareas_pendientes(0, 8) << '\n';
                std::cout << estado_serie(8, 8) << ": " << tareas_pendientes(8, 8);
            }
        """.trimIndent()
    }
}
