package com.learngraphicsprogramming.app

import android.app.Activity
import android.app.Application
import android.app.KeyguardManager
import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.os.SystemClock
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.Button
import android.widget.EditText
import android.widget.Spinner
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

/** The six already checked desktop cases of revised U2L9, now through Android controls.
 * The complete, unchanged lesson is copied in memory under one exclusive QA identity.
 * Relaunches rebuild the real renderer from persisted QA answers/drafts; no mutable
 * repository, source asset, student answer, installed model or compiler is replaced. */
@RunWith(AndroidJUnit4::class)
class CppPreparedInputRevisionDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val snapshots = linkedMapOf<String, Map<String, *>>()
    private var originalFiles = emptySet<String>()
    private var preserved = false
    private val session = UUID.randomUUID().toString()
    private val fixtureId = "cpp_fundamentals__objetos_y_estado__prepared_revision_qa_${session.replace("-", "")}"
    private val guard = object : Application.ActivityLifecycleCallbacks {
        override fun onActivityCreated(activity: Activity, state: Bundle?) {
            if (activity is LessonActivity && activity.packageName == context.packageName)
                activity.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        }
        override fun onActivityStarted(activity: Activity) = Unit
        override fun onActivityResumed(activity: Activity) = Unit
        override fun onActivityPaused(activity: Activity) = Unit
        override fun onActivityStopped(activity: Activity) = Unit
        override fun onActivitySaveInstanceState(activity: Activity, state: Bundle) = Unit
        override fun onActivityDestroyed(activity: Activity) = Unit
    }

    @Before fun isolateAndPreserveEveryQaPreference() {
        assertTrue("Only an isolated QA package is allowed", context.packageName in QA_PACKAGES)
        assertEquals(context.packageName, BuildConfig.APPLICATION_ID)
        assertTrue("These native checks require the existing default Clang 23 toolchain", BuildConfig.LGP_CLANG23_ENABLED)
        assertTrue("Use LDPlayer, never Work's Honor", android.os.Build.SUPPORTED_ABIS.any { it == "x86" || it == "x86_64" })
        val keyguard = context.getSystemService(KeyguardManager::class.java)
        assertFalse("Wake and unlock LDPlayer first", keyguard.isDeviceLocked || keyguard.isKeyguardLocked)
        originalFiles = preferenceFiles()
        (originalFiles + setOf("learning_progress", "cpp_lesson_drafts", "narration_settings", "app_settings")).forEach { name ->
            snapshots[name] = context.getSharedPreferences(name, Context.MODE_PRIVATE).all.mapValues { (_, value) ->
                if (value is Set<*>) value.toSet() else value
            }
        }
        preserved = true
        (context.applicationContext as Application).registerActivityLifecycleCallbacks(guard)
        NarrationSettingsRepository.saveEnabled(context, false)
        PremiumRepository.setVerifiedPremium(context)
        assertTrue(AttemptRepository.getState(context, ROUTE).unlimited)
    }

    @After fun restoreEveryOriginalQaPreference() {
        if (!preserved) return
        try {
            instrumentation.waitForIdleSync()
            (preferenceFiles() + snapshots.keys).forEach { name ->
                if (name !in originalFiles) {
                    context.deleteSharedPreferences(name)
                } else {
                    val preferences = context.getSharedPreferences(name, Context.MODE_PRIVATE)
                    val editor = preferences.edit().clear()
                    snapshots.getValue(name).forEach { (key, value) -> when (value) {
                        is String -> editor.putString(key, value)
                        is Int -> editor.putInt(key, value)
                        is Long -> editor.putLong(key, value)
                        is Float -> editor.putFloat(key, value)
                        is Boolean -> editor.putBoolean(key, value)
                        is Set<*> -> editor.putStringSet(key, value.filterIsInstance<String>().toSet())
                        else -> error("Unsupported QA preference $name/$key")
                    } }
                    assertTrue(editor.commit())
                    assertEquals("Restore all pre-existing QA values in $name", snapshots.getValue(name), preferences.all)
                }
            }
            assertEquals(originalFiles, preferenceFiles())
        } finally {
            (context.applicationContext as Application).unregisterActivityLifecycleCallbacks(guard)
        }
    }

    @Test fun sixPublishedCasesUseRealClang23PreparedInputAndIndependentPersistedLabDrafts() {
        val source = source()
        val sourceHash = sha(source.toString())
        val results = JSONArray()
        withLesson(7) { scenario ->
            scenario.onActivity { activity ->
                assertEquals("Rita 4", input(activity).text.toString())
                assertEquals(block(source, 7, 3).getString("initialCode"), editor(activity).text.toString())
                assertEquals("Clang 23 remains the default", 0, (tagged(activity, "cpp_compiler_variant") as Spinner).selectedItemPosition)
            }
            for (case in CASES) {
                val exercise = block(source, case.page, case.block)
                assertEquals("Keep the published exercise ID", "$LESSON__${case.id}", exercise.getString("id"))
                val code = program(exercise)
                assertEquals("Reuse exactly the PC-approved source, including authored comments", case.sourceHash, sha(code))
                scenario.onActivity { activity ->
                    editor(activity).setText(code)
                    input(activity).setText(case.stdin)
                }
                results.put(run(scenario, case.expected).put("case", case.id).put("sourceSha256", sha(code)))
            }
            // Repeating the same source/input must restart the closed stdin at byte zero.
            results.put(run(scenario, CASES.last().expected).put("case", "repeat_last_closed_stdin"))
            scenario.onActivity { activity ->
                editor(activity).setText(block(source, 7, 3).getString("initialCode"))
                input(activity).setText("Leo\n2\n")
                showPage(activity, 10)
                assertEquals(block(source, 10, 4).getString("initialCode"), editor(activity).text.toString())
                assertEquals("Iris 4 2", input(activity).text.toString())
                editor(activity).setText(block(source, 11, 2).getString("code"))
                input(activity).setText("Iris\t4\n2")
            }
            results.put(run(scenario, "Iris: 22").put("case", "three_inputs_reference"))
            scenario.onActivity { activity ->
                showPage(activity, 7)
                assertEquals(block(source, 7, 3).getString("initialCode"), editor(activity).text.toString())
                assertEquals("Leo\n2\n", input(activity).text.toString())
                showPage(activity, 10)
                assertEquals("Iris\t4\n2", input(activity).text.toString())
            }
        }
        // A new Activity and freshly built views recover both independent persisted drafts.
        withLesson(10) { scenario ->
            scenario.onActivity { activity ->
                assertEquals(block(source, 11, 2).getString("code"), editor(activity).text.toString())
                assertEquals("Iris\t4\n2", input(activity).text.toString())
            }
            results.put(run(scenario, "Iris: 22").put("case", "three_inputs_after_reopen"))
            scenario.onActivity { activity ->
                showPage(activity, 7)
                assertEquals(block(source, 7, 3).getString("initialCode"), editor(activity).text.toString())
                assertEquals("Leo\n2\n", input(activity).text.toString())
            }
            results.put(run(scenario, "Leo: 5").put("case", "two_inputs_after_reopen"))
        }
        assertEquals("The installed original lesson is unchanged", sourceHash, sha(source().toString()))
        evidence("clang23-prepared-cases", JSONObject().put("sourceLessonId", LESSON)
            .put("sourceLessonSha256", sourceHash).put("qaLessonId", fixtureId).put("runs", results)
            .put("reopenedBothIndependentDrafts", true).put("desktopEvidenceReused", "u2l9-cpp/resultados.json"))
    }

    @Test fun fiveExactPredictionControlsAndNewDragGuidanceRetryAndReopenWithoutChangingAnswers() {
        val source = source()
        val results = JSONArray()
        for (case in CASES.filter { it.page != 9 }) {
            val exercise = block(source, case.page, case.block)
            assertEquals("exact", exercise.getString("outputComparison"))
            assertEquals(case.expected, exercise.getJSONArray("answers").getString(0))
            withLesson(case.page) { scenario -> scenario.onActivity { activity ->
                val controls = prediction(activity, source, case)
                controls.input.setText(case.expected + "\n")
                controls.check.performClick()
                assertState(case, case.expected + "\n", false)
                assertTrue(controls.feedback.text.toString(), controls.feedback.text.toString().contains(exercise.getString("reviewFeedback")))
                assertFalse("No solution explanation before success", controls.feedback.text.toString().contains(exercise.getString("explanation")))
                results.put(answerEvidence(activity, case, "incorrect", controls.feedback))
            } }
            withLesson(case.page) { scenario -> scenario.onActivity { activity ->
                val controls = prediction(activity, source, case)
                assertEquals(case.expected + "\n", controls.input.text.toString())
                assertTrue("The reviewed failure restores", controls.feedback.text.toString().contains(exercise.getString("reviewFeedback")))
                assertTrue(controls.input.isEnabled && controls.check.isEnabled)
                controls.input.setText(case.expected)
                controls.check.performClick()
                assertState(case, case.expected, true)
                assertTrue(controls.feedback.text.toString(), controls.feedback.text.toString().contains(exercise.getString("explanation")))
                assertFalse(controls.input.isEnabled || controls.check.isEnabled)
                results.put(answerEvidence(activity, case, "correct", controls.feedback))
            } }
            withLesson(case.page) { scenario -> scenario.onActivity { activity ->
                val controls = prediction(activity, source, case)
                assertEquals(case.expected, controls.input.text.toString())
                assertState(case, case.expected, true)
                assertFalse(controls.input.isEnabled || controls.check.isEnabled)
                assertTrue("The whole success explanation restores", controls.feedback.text.toString().contains(exercise.getString("explanation")))
                results.put(answerEvidence(activity, case, "correct_reopened", controls.feedback))
            } }
        }
        val drag = CASES.single { it.page == 9 }
        val exercise = block(source, drag.page, drag.block)
        val choices = strings(exercise.getJSONArray("choices"))
        val order = integers(exercise.getJSONArray("correctOrder"))
        val wrong = order.toMutableList().apply { val first = this[0]; this[0] = this[1]; this[1] = first }
        withLesson(9) { scenario ->
            chooseTokens(scenario, choices, wrong)
            scenario.onActivity { activity ->
                val controls = dragControls(activity)
                assertTrue(controls.first.isEnabled)
                controls.first.performClick()
                assertEquals(wrong, integers(saved(drag).getJSONArray("slots")))
                assertFalse(saved(drag).getBoolean("correct"))
                assertTrue(controls.second.text.toString().contains(exercise.getString("reviewFeedback")))
                assertFalse(controls.second.text.toString().contains(exercise.getString("explanation")))
                results.put(answerEvidence(activity, drag, "incorrect", controls.second))
            }
        }
        withLesson(9) { scenario ->
            scenario.onActivity { activity ->
                assertEquals(wrong, integers(saved(drag).getJSONArray("slots")))
                assertTrue(dragControls(activity).second.text.toString().contains(exercise.getString("reviewFeedback")))
            }
            // Tap occupied tokens back to the palette, then fill the actual slots.
            chooseTokens(scenario, choices, wrong)
            chooseTokens(scenario, choices, order)
            scenario.onActivity { activity ->
                val controls = dragControls(activity)
                controls.first.performClick()
                assertEquals(order, integers(saved(drag).getJSONArray("slots")))
                assertTrue(saved(drag).getBoolean("correct"))
                assertFalse(controls.first.isEnabled)
                assertTrue(controls.second.text.toString().contains(exercise.getString("explanation")))
                results.put(answerEvidence(activity, drag, "correct", controls.second))
            }
        }
        withLesson(9) { scenario -> scenario.onActivity { activity ->
            assertEquals(order, integers(saved(drag).getJSONArray("slots")))
            assertTrue(saved(drag).getBoolean("correct"))
            assertFalse(dragControls(activity).first.isEnabled)
            assertTrue(dragControls(activity).second.text.toString().contains(exercise.getString("explanation")))
            results.put(answerEvidence(activity, drag, "correct_reopened", dragControls(activity).second))
        } }
        evidence("prediction-drag-controls", JSONObject().put("sourceLessonId", LESSON)
            .put("sourceLessonSha256", sha(source.toString())).put("qaLessonId", fixtureId).put("states", results))
    }

    private fun withLesson(page: Int, body: (ActivityScenario<LessonActivity>) -> Unit) {
        val original = source()
        val fixture = JSONObject(original.toString()).put("id", fixtureId)
        ProgressRepository.saveCurrentPage(context, ROUTE, LESSON, 0)
        val intent = Intent(context, LessonActivity::class.java).putExtra("routeId", ROUTE)
            .putExtra("courseId", "cpp_fundamentals").putExtra("unitId", "objetos_y_estado")
            .putExtra("lessonId", LESSON).putExtra("lessonNumber", 9).putExtra("lessonTotal", 10)
        ActivityScenario.launch<LessonActivity>(intent).use { scenario ->
            var previous = 0
            var stableSince = 0L
            await("The original U2L9 must finish a stable startup before unique QA injection") { done -> scenario.onActivity { activity ->
                val loaded = field(activity, "lessonJson") as? JSONObject
                val ready = field(activity, "currentLessonId") == LESSON && loaded?.optString("id") == LESSON &&
                    content(activity).width > 0 && !content(activity).isLayoutRequested && activity.window.decorView.hasWindowFocus()
                val key = System.identityHashCode(activity) xor System.identityHashCode(loaded)
                if (!ready || previous != key) { previous = key; stableSince = if (ready) SystemClock.uptimeMillis() else 0L }
                done(ready && SystemClock.uptimeMillis() - stableSince >= 500L)
            } }
            scenario.onActivity { activity ->
                setField(activity, "currentLessonId", fixtureId)
                setField(activity, "lessonJson", fixture)
                setField(activity, "totalPages", fixture.getJSONArray("pages").length())
                setField(activity, "currentPage", page - 1)
                listOf("pageViewCache", "pageRequiredExercises", "pageCompletedExercises").forEach {
                    (field(activity, it) as MutableMap<*, *>).clear()
                }
                invoke(activity, "renderCurrentPage")
            }
            await("The original-coordinate QA page must lay out") { done -> scenario.onActivity { activity ->
                assertEquals("Never silently reinject a lost QA fixture", fixtureId, field(activity, "currentLessonId"))
                done(content(activity).width > 0 && !content(activity).isLayoutRequested)
            } }
            body(scenario)
        }
        assertEquals("No source curricular data changes", original.toString(), source().toString())
    }

    private fun run(scenario: ActivityScenario<LessonActivity>, expected: String): JSONObject {
        lateinit var owner: LessonActivity
        lateinit var button: View
        lateinit var status: TextView
        lateinit var compiler: TextView
        lateinit var stdout: TextView
        lateinit var stderr: TextView
        var before = 0
        var code = ""
        var stdin = ""
        var attemptsBefore = ""
        scenario.onActivity { activity ->
            owner = activity
            button = tagged(activity, "cpp_run")
            status = tagged(activity, "cpp_status") as TextView
            compiler = tagged(activity, "cpp_compilation_output") as TextView
            stdout = tagged(activity, "cpp_stdout") as TextView
            stderr = tagged(activity, "cpp_stderr") as TextView
            code = editor(activity).text.toString()
            stdin = input(activity).text.toString()
            before = field(activity, "completedExercises") as Int
            attemptsBefore = (field(activity, "exerciseAttempts") as ExerciseAttemptLedger).save().toString()
            assertEquals(0, (tagged(activity, "cpp_compiler_variant") as Spinner).selectedItemPosition)
            assertTrue(button.isAttachedToWindow && button.isEnabled)
            button.performClick()
            assertFalse("A real native job starts", button.isEnabled)
            assertEquals("BUILDING", status.getTag(R.id.cpp_lab_build_phase))
        }
        await("The same native Clang 23 job must finish", 120_000L) { done -> scenario.onActivity { activity ->
            assertSame("Never accept a different Activity's output", owner, activity)
            assertSame(button, tagged(activity, "cpp_run"))
            assertSame(status, tagged(activity, "cpp_status"))
            assertEquals(code, editor(activity).text.toString())
            assertEquals(stdin, input(activity).text.toString())
            done(button.isEnabled)
        } }
        var result = JSONObject()
        scenario.onActivity { activity ->
            assertEquals(activity.getString(R.string.cpp_runtime_finished, 0), status.text.toString())
            assertEquals("No build diagnostics", "", compiler.text.toString())
            assertEquals("Exact stdout, including LF and absence of final LF", expected, stdout.text.toString())
            assertEquals("Independent program stderr", "", stderr.text.toString())
            assertEquals("Labs never complete evaluated puzzles", before, field(activity, "completedExercises"))
            assertEquals("Native lab runs do not record any evaluated answer", attemptsBefore,
                (field(activity, "exerciseAttempts") as ExerciseAttemptLedger).save().toString())
            assertTrue(AttemptRepository.getState(context, ROUTE).unlimited)
            result = JSONObject().put("status", status.text.toString()).put("phase", status.getTag(R.id.cpp_lab_build_phase))
                .put("compiler", "Clang23/default").put("standardInput", stdin).put("sourceSha256", sha(code))
                .put("stdout", stdout.text.toString()).put("stderr", stderr.text.toString()).put("diagnostics", compiler.text.toString())
                .put("activityIdentity", System.identityHashCode(activity))
        }
        return result
    }

    private fun prediction(activity: LessonActivity, lesson: JSONObject, case: Case): PredictionControls {
        val blocks = lesson.getJSONArray("pages").getJSONObject(case.page - 1).getJSONArray("blocks")
        val indices = (0 until blocks.length()).filter { blocks.getJSONObject(it).optString("type") == "output_prediction" }
        val fields = descendants(content(activity)).filterIsInstance<EditText>().filter {
            it.hint?.toString() == activity.getString(R.string.exercise_expected_output)
        }
        assertEquals("Select the intended prediction, not another C++ input on the same page", indices.size, fields.size)
        val response = fields[indices.indexOf(case.block - 1)]
        val card = directChild(activity, response)
        val offset = content(activity).indexOfChild(card)
        return PredictionControls(response, content(activity).getChildAt(offset + 1) as Button,
            content(activity).getChildAt(offset + 2) as TextView)
    }

    private fun dragControls(activity: LessonActivity): Pair<Button, TextView> {
        val check = descendants(content(activity)).filterIsInstance<Button>().single {
            it.text.toString() in setOf(activity.getString(R.string.exercise_check), activity.getString(R.string.exercise_correct))
        }
        val offset = content(activity).indexOfChild(directChild(activity, check))
        return check to content(activity).getChildAt(offset + 1) as TextView
    }

    private fun chooseTokens(scenario: ActivityScenario<LessonActivity>, choices: List<String>, order: List<Int>) {
        for (index in order) {
            scenario.onActivity { activity ->
                val token = descendants(content(activity)).filterIsInstance<TextView>().single {
                    it.isClickable && it.text.toString() == choices[index]
                }
                assertTrue(token.performClick())
            }
            // The existing token animation saves the real slot identity after 350 ms.
            // Waiting for that UI operation is not a speech timing approximation.
            SystemClock.sleep(400L)
            instrumentation.waitForIdleSync()
        }
    }

    private fun answerEvidence(activity: LessonActivity, case: Case, stage: String, feedback: TextView) =
        JSONObject().put("page", case.page).put("block", case.block).put("sourceExerciseId", "$LESSON__${case.id}")
            .put("stage", stage).put("state", saved(case)).put("visibleFeedback", feedback.text.toString())
            .put("activityIdentity", System.identityHashCode(activity))
    private fun assertState(case: Case, text: String, correct: Boolean) {
        assertEquals(text, saved(case).getString("answer"))
        assertEquals(correct, saved(case).getBoolean("correct"))
    }
    private fun saved(case: Case) = requireNotNull(ProgressRepository.getExerciseState(context, fixtureId, case.page - 1, case.block - 1))
    private fun program(block: JSONObject): String {
        if (block.optString("type") != "drag_fill_code") return block.getString("code")
        val order = integers(block.getJSONArray("correctOrder"))
        val choices = strings(block.getJSONArray("choices"))
        var index = 0
        return Regex("\\[\\]").replace(block.getString("template")) { choices[order[index++]] }.also { assertEquals(order.size, index) }
    }
    private fun source() = JSONObject(context.assets.open(ASSET).bufferedReader().use { it.readText() })
    private fun block(lesson: JSONObject, page: Int, index: Int) = lesson.getJSONArray("pages").getJSONObject(page - 1).getJSONArray("blocks").getJSONObject(index - 1)
    private fun showPage(activity: LessonActivity, page: Int) {
        invoke(activity, "saveCurrentPageState")
        setField(activity, "currentPage", page - 1)
        ProgressRepository.saveCurrentPage(context, ROUTE, fixtureId, page - 1)
        invoke(activity, "renderCurrentPage")
    }
    private fun evidence(name: String, value: JSONObject) {
        val directory = File(requireNotNull(context.getExternalFilesDir(null)), "cpp-prepared-input-revision-qa/$session").apply { mkdirs() }
        File(directory, "$name.json").writeText(value.put("package", context.packageName).put("abis", JSONArray(android.os.Build.SUPPORTED_ABIS.toList())).toString(2))
    }
    private fun editor(activity: LessonActivity) = descendants(content(activity)).filterIsInstance<FreeScrollEditText>().single { it.tag == "cpp_code_input" }
    private fun input(activity: LessonActivity) = tagged(activity, "cpp_input") as EditText
    private fun tagged(activity: LessonActivity, tag: String) = descendants(content(activity)).single { it.tag == tag }
    private fun directChild(activity: LessonActivity, view: View): View {
        var result = view
        while (result.parent !== content(activity)) result = result.parent as View
        return result
    }
    private fun content(activity: LessonActivity): ViewGroup = activity.findViewById(R.id.lessonContent)
    private fun descendants(view: View): List<View> = buildList { add(view); if (view is ViewGroup) repeat(view.childCount) { addAll(descendants(view.getChildAt(it))) } }
    private fun field(activity: LessonActivity, name: String): Any? = LessonActivity::class.java.getDeclaredField(name).apply { isAccessible = true }.get(activity)
    private fun setField(activity: LessonActivity, name: String, value: Any) = LessonActivity::class.java.getDeclaredField(name).apply { isAccessible = true }.set(activity, value)
    private fun invoke(activity: LessonActivity, name: String) = LessonActivity::class.java.getDeclaredMethod(name).apply { isAccessible = true }.invoke(activity)
    private fun preferenceFiles() = File(context.applicationInfo.dataDir, "shared_prefs").listFiles().orEmpty().filter { it.name.endsWith(".xml") }.map { it.name.removeSuffix(".xml") }.toSet()
    private fun strings(array: JSONArray) = List(array.length()) { array.getString(it) }
    private fun integers(array: JSONArray) = List(array.length()) { array.getInt(it) }
    private fun sha(text: String) = MessageDigest.getInstance("SHA-256").digest(text.toByteArray(Charsets.UTF_8)).joinToString("") { "%02x".format(it) }
    private fun await(reason: String, timeout: Long = 12_000L, predicate: ((Boolean) -> Unit) -> Unit) {
        val deadline = SystemClock.uptimeMillis() + timeout
        while (SystemClock.uptimeMillis() < deadline) {
            instrumentation.waitForIdleSync()
            var done = false
            predicate { done = it }
            if (done) return
            SystemClock.sleep(50L)
        }
        fail(reason)
    }
    private data class PredictionControls(val input: EditText, val check: Button, val feedback: TextView)
    private data class Case(val page: Int, val block: Int, val id: String, val stdin: String, val expected: String, val sourceHash: String)
    companion object {
        private val QA_PACKAGES = setOf("com.learngraphicsprogramming.app.realdownloadsqa", "com.learngraphicsprogramming.app.curricularqa")
        private const val ROUTE = "programming"
        private const val LESSON = "cpp_fundamentals__objetos_y_estado__leer_una_entrada_conocida"
        private const val LESSON__ = LESSON + "__"
        private const val ASSET = "content/es/programming/cpp_fundamentals/objetos_y_estado/lesson_leer_una_entrada_conocida.json"
        // Oracles and hashes from Work's six approved PC cases, read without rerunning or editing its QA.
        private val CASES = listOf(
            Case(7, 5, "etapas_lectura_y_actualizacion", "Rita 4", "Rita 4\nRita: 7", "faf178dd7a0c557de42d9fa045eec7d9af0493bd4bfd4d166d9460dbb3dd15a6"),
            Case(9, 6, "funciones_lectura_procesamiento_presentacion", "Rita 4", "Rita: 7", "f2329694c3726f067f7ad89ddf491542f915b75275f2215197ce44ef8c043ac5"),
            Case(11, 6, "registrar_tres_datos_y_total", "Iris 4 2", "Iris 4 2 22", "5c1293d4be1aae7fe3d3d0fa5241923a67107244de941117399828689208045b"),
            Case(13, 3, "distinguir_regla_cuatro", "Nora 2 1", "Nora: 9", "5463de9f558891430ce394fe54b3e465c64c1d4476a79717bcd8c5b9753b1b04"),
            Case(13, 4, "cero_paquetes_regla_cuatro", "Vega 0 3", "Vega: 3", "5463de9f558891430ce394fe54b3e465c64c1d4476a79717bcd8c5b9753b1b04"),
            Case(13, 5, "maximo_regla_cuatro", "Orion 20 3", "Orion: 83", "5463de9f558891430ce394fe54b3e465c64c1d4476a79717bcd8c5b9753b1b04"))
    }
}
