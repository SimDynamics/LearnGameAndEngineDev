package com.learngraphicsprogramming.app.math

import android.app.KeyguardManager
import android.content.Context
import android.content.Intent
import android.os.SystemClock
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.Button
import android.widget.CompoundButton
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.Spinner
import android.widget.TextView
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.BuildConfig
import com.learngraphicsprogramming.app.LessonActivity
import com.learngraphicsprogramming.app.ProgressRepository
import com.learngraphicsprogramming.app.R
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

/** Real published puzzles and native controls, always under the independent curricular QA
 * package. Fresh lesson identities protect published progress; every QA preference is restored. */
@RunWith(AndroidJUnit4::class)
class MathSelectedOptionFeedbackDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val snapshots = linkedMapOf<String, Map<String, *>>()
    private var captured = false
    private val session = UUID.randomUUID().toString()

    @Before fun requireIsolatedPackageAndPreservePreferences() {
        assertEquals(QA_PACKAGE, context.packageName)
        assertEquals(QA_PACKAGE, BuildConfig.APPLICATION_ID)
        val keyguard = context.getSystemService(Context.KEYGUARD_SERVICE) as KeyguardManager
        assertFalse("Unlock the device before this curricular check", keyguard.isDeviceLocked)
        assertFalse(keyguard.isKeyguardLocked)
        (preferenceFiles() + listOf("learning_progress", "navigation_state", "lesson_narration_settings",
            "math_development_mode", "lesson_text_size_settings", "ui_language_settings")).forEach { name ->
            snapshots[name] = context.getSharedPreferences(name, Context.MODE_PRIVATE).all.mapValues { (_, value) ->
                if (value is Set<*>) value.toSet() else value
            }
        }
        captured = true
        NarrationSettingsRepository.saveEnabled(context, false)
    }

    @After fun restorePreferencesWithoutRemovingAnyFile() {
        if (!captured || context.packageName != QA_PACKAGE) return
        instrumentation.waitForIdleSync()
        (preferenceFiles() + snapshots.keys).forEach { name ->
            val original = snapshots[name].orEmpty()
            val prefs = context.getSharedPreferences(name, Context.MODE_PRIVATE)
            val editor = prefs.edit().clear()
            original.forEach { (key, value) ->
                when (value) {
                    is String -> editor.putString(key, value)
                    is Int -> editor.putInt(key, value)
                    is Long -> editor.putLong(key, value)
                    is Float -> editor.putFloat(key, value)
                    is Boolean -> editor.putBoolean(key, value)
                    is Set<*> -> editor.putStringSet(key, value.filterIsInstance<String>().toSet())
                    else -> error("Unsupported QA preference value")
                }
            }
            assertTrue(editor.commit())
            assertEquals("Preserve every pre-existing value; retain new empty QA files", original, prefs.all)
        }
    }

    @Test fun everyAvailableMathCourseWithChoicesGivesOnlySelectedGuidance() {
        val representatives = linkedMapOf<String, Pair<String, JSONObject>>()
        val indexedPaths = linkedSetOf<String>()
        for (language in listOf("es", "en", "fr")) {
            val indexed = indexedLessonPaths(language)
            indexedPaths.addAll(indexed)
            // Retained developmental courses still require feedback review; prefer their
            // actual index whenever it exists and mark a non-indexed source explicitly.
            (indexed + allExistingLessonPaths("content/$language/math_physics").filterNot { it in indexed }).forEach { path ->
                val lesson = JSONObject(context.assets.open(path).bufferedReader().use { it.readText() })
                val pages = lesson.optJSONArray("pages") ?: return@forEach
                for (p in 0 until pages.length()) {
                    val blocks = pages.getJSONObject(p).getJSONArray("blocks")
                    for (b in 0 until blocks.length()) {
                        val block = blocks.getJSONObject(b)
                        if (!MathPuzzleTypes.isExercise(block.optString("type"))) continue
                        val spec = MathPuzzleParser.parse(block)
                        if (spec.responses.size != 1 || !MathPuzzleFeedback.isChoice(spec.responses.single())) continue
                        val course = path.split('/')[3]
                        representatives.putIfAbsent("$language/$course", path to JSONObject(block.toString()))
                    }
                }
            }
        }
        assertTrue("Available mathematics must be exercised", representatives.isNotEmpty())
        assertTrue("Cover the two published Spanish courses and the retained geometry draft", representatives.keys
            .containsAll(listOf("es/math_course_01", "es/math_algebra_elemental", "es/math_geometria_plana")))
        representatives.forEach { (course, source) ->
            val (path, block) = source
            val response = MathPuzzleParser.parse(block).responses.single()
            val options = response.config.getJSONArray("options")
            val expected = strings(response.validator.getJSONArray("expected")).toSet()
            val wrong = (0 until options.length()).map { options.getJSONObject(it) }
                .first { it.getString("id") !in expected }
            assertTrue("Every authored selected option must have reviewed guidance: $path", wrong.has("reviewFeedback"))
            withRenderer(block) { activity, renderer, id ->
                select(renderer, response, setOf(wrong.getString("id")))
                check(renderer).performClick()
                assertFalse(renderer.isCorrect)
                assertFalse(saved(id).getBoolean("correct"))
                val visibleFeedback = feedback(renderer).text.toString()
                assertTrue(visibleFeedback.contains(MathTextRenderer.render(wrong.getString("reviewFeedback")).toString()))
                assertFalse("Never display a technical response ID", visibleFeedback.contains(response.id))
                val global = MathPuzzleParser.parse(block).incorrectFeedback
                if (global.isNotBlank() && global != wrong.getString("reviewFeedback"))
                    assertFalse("Legacy global explanations cannot classify absent choices", visibleFeedback.contains(MathTextRenderer.render(global).toString()))
                for (i in 0 until options.length()) {
                    val option = options.getJSONObject(i)
                    val hint = option.optString("reviewFeedback")
                    if (option.getString("id") != wrong.getString("id") && hint.isNotBlank() && hint != wrong.getString("reviewFeedback"))
                        assertFalse("An unselected option's review is not shown", visibleFeedback.contains(MathTextRenderer.render(hint).toString()))
                }
                if (course.startsWith("es/")) saveNativeFeedback(activity, renderer, course.replace('/', '-'),
                    JSONObject().put("sourcePath", path).put("indexed", path in indexedPaths).put("puzzleId", block.optString("id"))
                        .put("responseId", response.id).put("selected", JSONArray().put(wrong.getString("id")))
                        .put("visibleFeedback", visibleFeedback).put("correct", false))
                select(renderer, response, expected)
                check(renderer).performClick()
                assertTrue(renderer.isCorrect)
                assertTrue(MathPuzzleState.isCurrentCorrect(block, saved(id)))
                val accepted = feedback(renderer).text.toString()
                if (MathPuzzleParser.parse(block).correctFeedback.isNotBlank())
                    assertTrue(accepted.contains(MathTextRenderer.render(MathPuzzleParser.parse(block).correctFeedback).toString()))
            }
        }
        saveMetadata("available-course-coverage", JSONObject().put("representatives", JSONArray(representatives.keys))
            .put("count", representatives.size).put("method", "actual native Check controls; one wrong submission and independent correct retry per available language/course; indexed lessons preferred and retained geometry draft identified"))
    }

    @Test fun multipleSelectionDoesNotClassifyASelectedCorrectOptionOrRevealTheAbsentOne() {
        val block = choiceBlock(multi = true)
        val response = MathPuzzleParser.parse(block).responses.single()
        withRenderer(block) { activity, renderer, id ->
            select(renderer, response, setOf("compatible", "incompatible"))
            check(renderer).performClick()
            val text = feedback(renderer).text.toString()
            assertFalse(renderer.isCorrect)
            assertTrue(text.contains("Reconsidera qué cantidad se está comparando."))
            assertTrue(text.contains("Examina las unidades de la medición elegida."))
            assertFalse(text.contains("Mensaje del inciso no elegido"))
            assertFalse(text.contains("LEGACY reveals"))
            assertFalse(text.contains(response.id))
            assertEquals(MathAnswer.Choice(setOf("compatible", "incompatible")),
                MathAnswerJson.decode(saved(id).getJSONObject("responses").getJSONObject(response.id)))
            saveNativeFeedback(activity, renderer, "multiple-neutral-guidance", JSONObject().put("visibleFeedback", text))
            select(renderer, response, setOf("compatible", "additional"))
            check(renderer).performClick()
            assertTrue(renderer.isCorrect)
            assertTrue(MathPuzzleState.isCurrentCorrect(block, saved(id)))
        }
    }

    @Test fun legacyAndMixedPuzzlesUseSafeGuidanceAndKeepCorrectFeedbackForFullSuccess() {
        val block = choiceBlock()
        val choice = block.getJSONArray("responses").getJSONObject(0)
        val options = choice.getJSONArray("options")
        for (i in 0 until options.length()) options.getJSONObject(i).remove("reviewFeedback")
        block.getJSONArray("responses").put(JSONObject().put("id", "interno__cantidad")
            .put("type", "math_numeric_input").put("validator", JSONObject().put("type", "numeric").put("expected", "2")))
        val response = MathPuzzleParser.parse(block).responses.first()
        withRenderer(block) { activity, renderer, id ->
            select(renderer, response, setOf("incompatible"))
            descendants(renderer.view).filterIsInstance<EditText>().single().setText("3")
            check(renderer).performClick()
            val text = feedback(renderer).text.toString()
            assertTrue(text.contains(activity.getString(R.string.math_ui_review_selected_option)))
            assertFalse(text.contains("LEGACY reveals"))
            assertFalse(text.contains("Conclusión completa"))
            assertFalse(text.contains("interno__cantidad"))
            assertFalse(text.contains(response.id))
            assertFalse(saved(id).getBoolean("correct"))
            select(renderer, response, setOf("compatible"))
            descendants(renderer.view).filterIsInstance<EditText>().single().setText("2")
            check(renderer).performClick()
            assertTrue(renderer.isCorrect)
            assertTrue(feedback(renderer).text.toString().contains("Conclusión completa"))
        }
    }

    @Test fun addingReviewsRestoresBothOldCorrectProgressAndAnOldIncorrectSelection() {
        val enhanced = choiceBlock()
        val legacy = JSONObject(enhanced.toString())
        val options = legacy.getJSONArray("responses").getJSONObject(0).getJSONArray("options")
        for (i in 0 until options.length()) options.getJSONObject(i).remove("reviewFeedback")
        val oldFingerprint = MessageDigest.getInstance("SHA-256").digest(legacy.toString().toByteArray(Charsets.UTF_8))
            .joinToString("") { "%02x".format(it) }
        assertEquals(oldFingerprint, MathPuzzleState.fingerprint(enhanced))
        val response = MathPuzzleParser.parse(enhanced).responses.single()
        for (wasCorrect in listOf(true, false)) {
            val selection = setOf(if (wasCorrect) "compatible" else "incompatible")
            val oldState = JSONObject().put("schemaVersion", 1).put("correct", wasCorrect)
                .put("contentFingerprint", oldFingerprint).put("responses", JSONObject()
                    .put(response.id, MathAnswerJson.encode(MathAnswer.Choice(selection))))
            withRenderer(enhanced, oldState) { _, renderer, id ->
                assertEquals(wasCorrect, renderer.isCorrect)
                assertEquals(selection, descendants(renderer.view).filterIsInstance<CompoundButton>()
                    .mapIndexedNotNull { index, button -> if (button.isChecked) response.config.getJSONArray("options")
                        .getJSONObject(index).getString("id") else null }.toSet())
                assertEquals(oldFingerprint, saved(id).getString("contentFingerprint"))
                assertEquals(wasCorrect, MathPuzzleState.isCurrentCorrect(enhanced, saved(id)))
                if (!wasCorrect) {
                    select(renderer, response, setOf("compatible"))
                    check(renderer).performClick()
                    assertTrue(renderer.isCorrect)
                } else assertFalse(check(renderer).isEnabled)
            }
        }
    }

    @Test fun technicalFieldFailuresUseHumanNumberedLabelsAndAllowARepair() {
        val block = JSONObject().put("type", "math_puzzle").put("schemaVersion", 1).put("id", "technical_qa")
            .put("prompt", "Escribe una cantidad numérica.").put("responses", JSONArray().put(JSONObject()
                .put("id", "respuesta__tecnica").put("type", "math_multi_input")
                .put("fields", JSONArray().put(JSONObject().put("id", "campo__interno").put("type", "numeric")))
                .put("validator", JSONObject().put("type", "fields").put("fields", JSONObject()
                    .put("campo__interno", JSONObject().put("type", "numeric").put("expected", "2"))))))
            .put("feedback", JSONObject().put("correct", "Cantidad revisada.").put("incorrect", "Conserva el significado de la cantidad."))
        withRenderer(block) { activity, renderer, id ->
            val edit = descendants(renderer.view).filterIsInstance<EditText>().single()
            edit.setText("(")
            check(renderer).performClick()
            val text = feedback(renderer).text.toString()
            assertTrue(text.contains(activity.getString(R.string.math_ui_response, 1)))
            assertTrue(text.contains(activity.getString(R.string.math_ui_field, 1)))
            assertFalse(text.contains("respuesta__tecnica"))
            assertFalse(text.contains("campo__interno"))
            assertFalse(text.contains("Conserva el significado"))
            assertFalse(renderer.isCorrect)
            edit.setText("2")
            check(renderer).performClick()
            assertTrue(MathPuzzleState.isCurrentCorrect(block, saved(id)))
        }
    }

    @Test fun nonChoicePuzzlesKeepTheirExistingConceptualIncorrectFeedback() {
        val block = JSONObject().put("type", "math_puzzle").put("prompt", "Completa una cantidad.")
            .put("responses", JSONArray().put(JSONObject().put("id", "interno__respuesta")
                .put("type", "math_numeric_input").put("validator", JSONObject().put("type", "numeric").put("expected", "2"))))
            .put("feedback", JSONObject().put("correct", "Cantidad comprobada.").put("incorrect", "Reconsidera el significado de la unidad."))
        withRenderer(block) { _, renderer, _ ->
            descendants(renderer.view).filterIsInstance<EditText>().single().setText("3")
            check(renderer).performClick()
            assertTrue(feedback(renderer).text.toString().contains("Reconsidera el significado de la unidad."))
            assertFalse(feedback(renderer).text.toString().contains("interno__respuesta"))
        }
    }

    @Test fun allPublishedMathChoiceFingerprintsRetainPreRevisionIdentity() {
        val baseline = JSONObject(instrumentation.context.assets.open("math-feedback-baseline.json").bufferedReader().use { it.readText() })
        assertEquals("e5a7ecab3ec0509b6e1dad4c6e9e69265e29e4c7", baseline.getString("head"))
        val entries = baseline.getJSONArray("entries")
        assertEquals("Cover every pre-revision selection block, including retained non-indexed fixtures",
            baseline.getInt("expectedBlocks"), entries.length())
        assertEquals(339, baseline.getInt("choiceResponseCount"))
        var choiceResponseCount = 0
        var dropdownFieldsCount = 0
        for (index in 0 until entries.length()) {
            val entry = entries.getJSONObject(index)
            val path = entry.getString("file")
            val oldBlock = entry.optJSONObject("oldBlock") ?: JSONObject(entry.getString("oldBlock"))
            val actual = JSONObject(context.assets.open(path).bufferedReader().use { it.readText() })
                .getJSONArray("pages").getJSONObject(entry.getInt("page0")).getJSONArray("blocks").getJSONObject(entry.getInt("block0"))
            val oldFingerprint = MessageDigest.getInstance("SHA-256").digest(oldBlock.toString().toByteArray(Charsets.UTF_8))
                .joinToString("") { "%02x".format(it) }
            assertEquals("Previously saved Android states keep their exact identity: $path#$index",
                oldFingerprint, MathPuzzleState.fingerprint(actual))
            assertEquals("Only selected-option reviews may differ; all IDs, answers and evaluators stay identical",
                oldBlock.toString(), MathPuzzleState.withoutReviewFeedback(actual).toString())
            val oldResponses = MathPuzzleParser.parse(oldBlock).responses
            val newResponses = MathPuzzleParser.parse(actual).responses
            choiceResponseCount += oldResponses.count(MathPuzzleFeedback::isChoice)
            dropdownFieldsCount += oldResponses.sumOf { response -> response.config.optJSONArray("fields")?.let { fields ->
                (0 until fields.length()).count { fields.getJSONObject(it).optString("type") == "dropdown" }
            } ?: 0 }
            assertEquals(oldResponses.map { it.id }, newResponses.map { it.id })
            oldResponses.zip(newResponses).forEach { (old, updated) ->
                assertEquals(old.type, updated.type)
                assertEquals(old.validator.toString(), updated.validator.toString())
            }
        }
        assertEquals("The composed block has two choice responses; neither may be omitted", 339, choiceResponseCount)
        assertTrue("Real dropdown fields must participate in the legacy identity guard", dropdownFieldsCount > 0)
        assertEquals(baseline.getInt("dropdownFieldsCount"), dropdownFieldsCount)
        saveMetadata("all-real-legacy-fingerprints", JSONObject().put("count", entries.length())
            .put("choiceResponseCount", choiceResponseCount).put("dropdownFieldsCount", dropdownFieldsCount)
            .put("baselineHead", baseline.getString("head"))
            .put("method", "direct SHA-256 of each original Android JSONObject versus the current presentation-normalized fingerprint"))
    }

    @Test fun publishedDropdownSelectionsKeepFeedbackPrivateAndRestorePreviousStates() {
        val sources = listOf(
            "content/es/math_physics/math_algebra_elemental/desigualdades_lineales/lesson_rangos_y_condiciones_simultaneas.json" to "leer_cotas_e_inclusion",
            "content/es/math_physics/math_geometria_plana/fundamentos_geometricos/lesson_del_trazo_a_la_figura_geometrica.json" to "conservar_el_significado_de_nombres_y_datos"
        )
        val baseline = JSONObject(instrumentation.context.assets.open("math-feedback-baseline.json").bufferedReader().use { it.readText() })
            .getJSONArray("entries")
        sources.forEach { (path, puzzleId) ->
            val lesson = JSONObject(context.assets.open(path).bufferedReader().use { it.readText() })
            val block = lesson.getJSONArray("pages").let { pages ->
                (0 until pages.length()).flatMap { page -> pages.getJSONObject(page).getJSONArray("blocks").let { blocks ->
                    (0 until blocks.length()).map { blocks.getJSONObject(it) }
                } }.single { it.optString("id") == puzzleId }
            }
            val legacy = (0 until baseline.length()).map { baseline.getJSONObject(it) }.single { entry ->
                entry.getString("file") == path && (entry.optJSONObject("oldBlock") ?: JSONObject(entry.getString("oldBlock")))
                    .optString("id") == puzzleId
            }.let { it.optJSONObject("oldBlock") ?: JSONObject(it.getString("oldBlock")) }
            val oldFingerprint = MessageDigest.getInstance("SHA-256").digest(legacy.toString().toByteArray(Charsets.UTF_8))
                .joinToString("") { "%02x".format(it) }
            assertEquals(oldFingerprint, MathPuzzleState.fingerprint(block))
            val response = MathPuzzleParser.parse(block).responses.single()
            val fields = response.config.getJSONArray("fields").let { array -> (0 until array.length()).map { array.getJSONObject(it) } }
            val expected = fields.filter { it.optString("type") != "fixed" }.associate { field ->
                field.getString("id") to response.validator.getJSONObject("fields").getJSONObject(field.getString("id")).get("expected").toString()
            }
            val dropdowns = fields.filter { it.optString("type") == "dropdown" }
            val changedField = dropdowns.first()
            val wrongOption = changedField.getJSONArray("options").let { options ->
                (0 until options.length()).map { options.getJSONObject(it) }.first { it.getString("id") != expected.getValue(changedField.getString("id")) }
            }
            val incorrect = expected + (changedField.getString("id") to wrongOption.getString("id"))
            fun oldState(correct: Boolean, values: Map<String, String>) = JSONObject().put("schemaVersion", 1)
                .put("correct", correct).put("contentFingerprint", oldFingerprint).put("responses", JSONObject()
                    .put(response.id, MathAnswerJson.encode(MathAnswer.Fields(values))))
            withRenderer(block, oldState(false, incorrect)) { activity, renderer, id ->
                assertFalse(renderer.isCorrect)
                assertEquals(MathAnswer.Fields(incorrect), MathAnswerJson.decode(saved(id).getJSONObject("responses").getJSONObject(response.id)))
                check(renderer).performClick()
                assertDropdownReviewsOnly(renderer, response, dropdowns, incorrect)
                assertFalse(feedback(renderer).text.toString().contains(MathTextRenderer.render(MathPuzzleParser.parse(block).incorrectFeedback).toString()))
                assertFalse(saved(id).getBoolean("correct"))
                saveNativeFeedback(activity, renderer, "dropdown-$puzzleId", JSONObject().put("sourcePath", path)
                    .put("puzzleId", puzzleId).put("visibleFeedback", feedback(renderer).text.toString())
                    .put("selectedValues", JSONObject(incorrect)).put("oldFingerprint", oldFingerprint))
                val numeric = fields.firstOrNull { it.optString("type", "numeric") in setOf("numeric", "expression") }
                if (numeric != null) {
                    val mixed = expected + (numeric.getString("id") to "99")
                    fillFields(renderer, response, mixed)
                    check(renderer).performClick()
                    assertFalse(renderer.isCorrect)
                    assertDropdownReviewsOnly(renderer, response, dropdowns, mixed)
                    assertTrue("Keep the independent numeric field diagnostic", feedback(renderer).text.toString()
                        .contains(numeric.getString("label")))
                    saveNativeFeedback(activity, renderer, "dropdown-mixed-numeric-$puzzleId", JSONObject()
                        .put("sourcePath", path).put("visibleFeedback", feedback(renderer).text.toString()))
                }
                fillFields(renderer, response, expected)
                check(renderer).performClick()
                assertTrue(renderer.isCorrect)
                assertTrue(MathPuzzleState.isCurrentCorrect(block, saved(id)))
            }
            withRenderer(block, oldState(true, expected)) { _, renderer, id ->
                assertTrue("Previously completed dropdown puzzles stay completed", renderer.isCorrect)
                assertFalse(check(renderer).isEnabled)
                assertEquals(oldFingerprint, saved(id).getString("contentFingerprint"))
                assertTrue(MathPuzzleState.isCurrentCorrect(block, saved(id)))
            }
        }
    }

    private fun assertDropdownReviewsOnly(renderer: MathPuzzleRenderer, response: MathResponseSpec,
                                         dropdowns: List<JSONObject>, values: Map<String, String>) {
        val actual = feedback(renderer).text.toString()
        val selectedReviews = dropdowns.map { field ->
            val options = field.getJSONArray("options")
            (0 until options.length()).map { options.getJSONObject(it) }
                .single { it.getString("id") == values.getValue(field.getString("id")) }.getString("reviewFeedback")
        }
        selectedReviews.forEach { assertTrue(actual.contains(MathTextRenderer.render(it).toString())) }
        dropdowns.forEach { field ->
            val options = field.getJSONArray("options")
            for (index in 0 until options.length()) {
                val option = options.getJSONObject(index)
                val review = option.getString("reviewFeedback")
                if (option.getString("id") != values.getValue(field.getString("id")) && review !in selectedReviews)
                    assertFalse("Do not reveal an unselected dropdown alternative", actual.contains(MathTextRenderer.render(review).toString()))
            }
        }
        assertFalse(actual.contains(response.id))
    }

    private fun fillFields(renderer: MathPuzzleRenderer, response: MathResponseSpec, values: Map<String, String>) {
        val fields = response.config.getJSONArray("fields").let { array -> (0 until array.length()).map { array.getJSONObject(it) } }
        val spinners = descendants(renderer.view).filterIsInstance<Spinner>()
        val edits = descendants(renderer.view).filterIsInstance<EditText>()
        var spinnerIndex = 0
        var editIndex = 0
        fields.forEach { field ->
            when (field.optString("type", "numeric")) {
                "dropdown" -> {
                    val options = field.getJSONArray("options")
                    val index = (0 until options.length()).single { options.getJSONObject(it).getString("id") == values.getValue(field.getString("id")) }
                    spinners[spinnerIndex++].setSelection(index + 1)
                }
                "fixed" -> Unit
                else -> edits[editIndex++].setText(values.getValue(field.getString("id")))
            }
        }
        assertEquals(spinners.size, spinnerIndex)
        assertEquals(edits.size, editIndex)
    }

    private fun withRenderer(block: JSONObject, oldState: JSONObject? = null,
                             assertions: (LessonActivity, MathPuzzleRenderer, String) -> Unit) {
        val lessonId = "math_feedback__native_qa_${UUID.randomUUID().toString().replace("-", "") }"
        if (oldState != null) ProgressRepository.saveExerciseState(context, "math_physics", lessonId, 0, 0, oldState)
        val before = block.toString()
        val intent = Intent(context, LessonActivity::class.java).putExtra("routeId", "math_physics")
            .putExtra("courseId", "math_course_01").putExtra("unitId", "estructura_y_divisibilidad")
            .putExtra("lessonId", "math_course_01__estructura_y_divisibilidad__divisibilidad_y_primos")
            .putExtra("lessonNumber", 1).putExtra("lessonTotal", 1)
        ActivityScenario.launch<LessonActivity>(intent).use { scenario ->
            // Prepare the native fixture after the actual window has bounds and focus.
            // The wait stays off the UI thread and does not change curricular assertions.
            instrumentation.waitForIdleSync()
            val deadline = SystemClock.uptimeMillis() + 10_000L
            var ready = false
            do {
                scenario.onActivity { activity ->
                    val content = activity.findViewById<LinearLayout>(R.id.lessonContent)
                    ready = content.width > 0 && content.height > 0 && activity.hasWindowFocus()
                }
                if (!ready) SystemClock.sleep(50L)
            } while (!ready && SystemClock.uptimeMillis() < deadline)
            assertTrue("The native curricular viewport must be focused and measured", ready)
            scenario.onActivity { activity ->
                activity.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                val content = activity.findViewById<LinearLayout>(R.id.lessonContent)
                content.removeAllViews()
                val renderer = MathPuzzleRenderer(activity, "math_physics", lessonId, 0, 0, JSONObject(block.toString())) { }
                content.addView(renderer.view)
                assertions(activity, renderer, lessonId)
            }
        }
        assertEquals("Feedback presentation never rewrites published blocks", before, block.toString())
    }

    private fun select(renderer: MathPuzzleRenderer, response: MathResponseSpec, desired: Set<String>) {
        val options = response.config.getJSONArray("options")
        val buttons = descendants(renderer.view).filterIsInstance<CompoundButton>()
        assertEquals(options.length(), buttons.size)
        // Clearing first makes single-choice switches independent of visual insertion order.
        buttons.forEach { if (it.isChecked) it.isChecked = false }
        for (index in 0 until options.length()) if (options.getJSONObject(index).getString("id") in desired)
            buttons[index].isChecked = true
    }
    private fun check(renderer: MathPuzzleRenderer) = descendants(renderer.view).filterIsInstance<Button>()
        .single { it.tag == MathPuzzleRenderer.CHECK_ACTION_TAG }
    private fun feedback(renderer: MathPuzzleRenderer) = renderer.view.getChildAt(renderer.view.childCount - 1) as TextView
    private fun saved(id: String) = requireNotNull(ProgressRepository.getExerciseState(context, id, 0, 0))
    private fun descendants(view: View): List<View> = listOf(view) + if (view is ViewGroup)
        (0 until view.childCount).flatMap { descendants(view.getChildAt(it)) } else emptyList()
    private fun strings(array: JSONArray) = (0 until array.length()).map { array.getString(it) }
    private fun indexedLessonPaths(language: String): List<String> {
        val route = "content/$language/math_physics"
        val catalog = JSONArray(context.assets.open("$route/courses.json").bufferedReader().use { it.readText() })
        val paths = mutableListOf<String>()
        for (courseIndex in 0 until catalog.length()) {
            val course = catalog.getJSONObject(courseIndex).getString("id")
            // The complete route catalog also names future courses without authored assets.
            if ("course_$course.json" !in context.assets.list("$route/$course").orEmpty()) continue
            val metadata = JSONObject(context.assets.open("$route/$course/course_$course.json").bufferedReader().use { it.readText() })
            val units = metadata.getJSONArray("units")
            for (unitIndex in 0 until units.length()) {
                val unit = units.getJSONObject(unitIndex).getString("id").removePrefix("${course}__")
                if ("unit_$unit.json" !in context.assets.list("$route/$course/$unit").orEmpty()) continue
                val definition = JSONObject(context.assets.open("$route/$course/$unit/unit_$unit.json").bufferedReader().use { it.readText() })
                val lessons = definition.getJSONArray("lessons")
                for (lessonIndex in 0 until lessons.length()) {
                    val lesson = lessons.getJSONObject(lessonIndex).getString("id").substringAfterLast("__")
                    // Partial translations use the ordinary source-language fallback; the
                    // Spanish representative already covers that same published lesson.
                    if ("lesson_$lesson.json" in context.assets.list("$route/$course/$unit").orEmpty())
                        paths += "$route/$course/$unit/lesson_$lesson.json"
                }
            }
        }
        return paths
    }
    private fun allExistingLessonPaths(path: String): List<String> = context.assets.list(path).orEmpty().flatMap { name ->
        val child = "$path/$name"
        if (name.endsWith(".json")) {
            if (name.startsWith("lesson_")) listOf(child) else emptyList()
        } else allExistingLessonPaths(child)
    }
    private fun preferenceFiles() = File(context.applicationInfo.dataDir, "shared_prefs").listFiles().orEmpty()
        .filter { it.name.endsWith(".xml") }.map { it.name.removeSuffix(".xml") }.toSet()

    private fun saveNativeFeedback(activity: LessonActivity, renderer: MathPuzzleRenderer, name: String, metadata: JSONObject) {
        // Draw the actual Android feedback TextView at its existing lesson viewport width.
        // This is native view evidence, explicitly not a full-window screenshot or a test
        // of unrelated reader controls. The published option/validator fields are untouched.
        val feedback = feedback(renderer)
        val content = activity.findViewById<LinearLayout>(R.id.lessonContent)
        val width = content.width - content.paddingLeft - content.paddingRight -
            renderer.view.paddingLeft - renderer.view.paddingRight
        assertTrue("The actual lesson viewport must already have native bounds", width > 0)
        feedback.measure(View.MeasureSpec.makeMeasureSpec(width, View.MeasureSpec.EXACTLY),
            View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED))
        feedback.layout(0, 0, width, feedback.measuredHeight)
        assertTrue(feedback.height > 0)
        val bitmap = Bitmap.createBitmap(width, feedback.height, Bitmap.Config.ARGB_8888)
        try {
            val canvas = Canvas(bitmap)
            canvas.drawColor(Color.rgb(28, 31, 36))
            feedback.draw(canvas)
            File(evidenceDirectory(), "$name.png").outputStream().use {
                assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it))
            }
        } finally { bitmap.recycle() }
        saveMetadata(name, metadata.put("nativeText", feedback.text.toString()).put("package", context.packageName)
            .put("captureKind", "actual Android feedback TextView drawn at the original lesson viewport width")
            .put("width", width).put("height", feedback.height).put("lineCount", feedback.layout.lineCount)
            .put("png", "$name.png").put("pngSha256", MessageDigest.getInstance("SHA-256")
                .digest(File(evidenceDirectory(), "$name.png").readBytes()).joinToString("") { "%02x".format(it) }))
    }
    private fun saveMetadata(name: String, value: JSONObject) {
        File(evidenceDirectory(), "$name.json").writeText(value.toString(2))
    }
    private fun evidenceDirectory() = File(context.filesDir, "math-selected-feedback-qa/$session").apply { mkdirs() }
    private fun choiceBlock(multi: Boolean = false) = JSONObject().put("type", "math_puzzle").put("schemaVersion", 1)
        .put("id", "feedback_selection_qa").put("prompt", "Compara las cantidades y justifica la selección.")
        .put("responses", JSONArray().put(JSONObject().put("id", "contar_o_medir__eleccion")
            .put("type", if (multi) "math_multiple_choice_multi" else "math_multiple_choice_single")
            .put("options", JSONArray().put(JSONObject().put("id", "compatible").put("label", "Cantidad en unidades compatibles")
                .put("reviewFeedback", "Reconsidera qué cantidad se está comparando."))
                .put(JSONObject().put("id", "incompatible").put("label", "Cantidad en otra unidad")
                    .put("reviewFeedback", "Examina las unidades de la medición elegida."))
                .put(JSONObject().put("id", "additional").put("label", "Otra cantidad compatible")
                    .put("reviewFeedback", "Mensaje del inciso no elegido.")))
            .put("validator", JSONObject().put("type", "choice").put("expected", JSONArray().put("compatible")
                .apply { if (multi) put("additional") }))))
        .put("feedback", JSONObject().put("correct", "Conclusión completa.")
            .put("incorrect", "LEGACY reveals: compatible and additional are correct; incompatible is not."))

    companion object { private const val QA_PACKAGE = "com.learngraphicsprogramming.app.curricularqa" }
}
