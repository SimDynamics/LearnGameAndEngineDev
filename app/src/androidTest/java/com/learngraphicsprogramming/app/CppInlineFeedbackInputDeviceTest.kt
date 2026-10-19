package com.learngraphicsprogramming.app

import android.app.Activity
import android.app.Application
import android.app.KeyguardManager
import android.content.Context
import android.content.Intent
import android.graphics.Rect
import android.os.Bundle
import android.os.SystemClock
import android.text.InputType
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.view.inputmethod.EditorInfo
import android.widget.Button
import android.widget.EditText
import android.widget.ScrollView
import android.widget.TextView
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.narration.IncorrectFeedbackNarration
import com.learngraphicsprogramming.app.narration.LessonNarrationController
import com.learngraphicsprogramming.app.narration.LessonNarrationScript
import com.learngraphicsprogramming.app.narration.LessonNarrationText
import com.learngraphicsprogramming.app.narration.NarrationLanguage
import com.learngraphicsprogramming.app.narration.NarrationSettingsRepository
import com.learngraphicsprogramming.app.narration.ReadingTextView
import java.io.File
import java.security.MessageDigest
import java.util.UUID
import org.json.JSONArray
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/** Exclusive QA IDs and in-memory optional feedback; no source lesson or student data is authored.
 * InputConnection coverage verifies Android's literal-field contract, not Honor's software IME. */
@RunWith(AndroidJUnit4::class)
class CppInlineFeedbackInputDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val preferences = linkedMapOf<String, Map<String, *>>()
    private var originalFiles = emptySet<String>()
    private var originalLocale = ""
    private var preserved = false
    private val session = UUID.randomUUID().toString()
    private val guard = object : Application.ActivityLifecycleCallbacks {
        override fun onActivityCreated(activity: Activity, state: Bundle?) {
            if (activity is LessonActivity) activity.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        }
        override fun onActivityStarted(activity: Activity) = Unit
        override fun onActivityResumed(activity: Activity) = Unit
        override fun onActivityPaused(activity: Activity) = Unit
        override fun onActivityStopped(activity: Activity) = Unit
        override fun onActivitySaveInstanceState(activity: Activity, state: Bundle) = Unit
        override fun onActivityDestroyed(activity: Activity) = Unit
    }

    @Before fun preserveQaStateAndRequireLdplayer() {
        assertTrue("Use an isolated QA package", context.packageName in setOf(
            "com.learngraphicsprogramming.app.realdownloadsqa", "com.learngraphicsprogramming.app.curricularqa"))
        assertEquals(context.packageName, BuildConfig.APPLICATION_ID)
        assertTrue("Only LDPlayer is authorized; never use Work's Honor", android.os.Build.SUPPORTED_ABIS.any {
            it == "x86" || it == "x86_64"
        })
        val lock = context.getSystemService(KeyguardManager::class.java)
        assertFalse("Unlock LDPlayer before running the native tests", lock.isDeviceLocked || lock.isKeyguardLocked)
        originalLocale = UiLanguageSettings.selectedTag(context)
        originalFiles = preferenceFiles()
        (originalFiles + KNOWN_PREFERENCES).forEach { name ->
            preferences[name] = context.getSharedPreferences(name, Context.MODE_PRIVATE).all.mapValues { (_, value) ->
                if (value is Set<*>) value.toSet() else value
            }
        }
        preserved = true
        (context.applicationContext as Application).registerActivityLifecycleCallbacks(guard)
        instrumentation.runOnMainSync { UiLanguageSettings.select(context, "es") }
        NarrationSettingsRepository.saveEnabled(context, false)
        PremiumRepository.setVerifiedPremium(context)
    }

    @After fun restoreEveryOriginalPreference() {
        if (!preserved) return
        instrumentation.waitForIdleSync()
        instrumentation.runOnMainSync { UiLanguageSettings.select(context, originalLocale) }
        (preferenceFiles() + preferences.keys).forEach { name ->
            if (name !in originalFiles) context.deleteSharedPreferences(name)
            else {
                val edit = context.getSharedPreferences(name, Context.MODE_PRIVATE).edit().clear()
                preferences.getValue(name).forEach { (key, value) -> when (value) {
                    is String -> edit.putString(key, value)
                    is Int -> edit.putInt(key, value)
                    is Long -> edit.putLong(key, value)
                    is Float -> edit.putFloat(key, value)
                    is Boolean -> edit.putBoolean(key, value)
                    is Set<*> -> edit.putStringSet(key, value.filterIsInstance<String>().toSet())
                    else -> error("Unexpected preference $name/$key")
                } }
                assertTrue(edit.commit())
                assertEquals(preferences.getValue(name), context.getSharedPreferences(name, Context.MODE_PRIVATE).all)
            }
        }
        assertEquals(originalFiles, preferenceFiles())
        (context.applicationContext as Application).unregisterActivityLifecycleCallbacks(guard)
    }

    @Test fun ninePublishedInlineActivitiesKeepTheirHolesWhileAuthoredGuidanceAndCompleteSuccessRestore() {
        for ((number, location) in LOCATIONS.withIndex()) {
            val original = fixture(location)
            val explanation = if (number == 0) source(original.path).getJSONArray("pages").getJSONObject(8)
                .getJSONArray("blocks").getJSONObject(2).getString("text") else
                "Explicación QA posterior completa. El resultado depende de los datos y de la operación elegida."
            val guide = if (number == 0) "Revisa los tipos de los operandos antes de considerar el destino del cociente." else
                "Revisa el papel de cada hueco antes de cambiar el fragmento."
            val fixture = enhance(original) { it.put("reviewFeedback", guide).put("explanation", explanation) }
            val correct = strings(fixture.block.getJSONArray("answers"))
            val wrong = correct.toMutableList().apply { this[0] = if (number == 0) "int" else "incorrecto_qa" }
            withFixture(fixture) { scenario -> scenario.onActivity { activity ->
                writeInline(activity, wrong)
                assertEquals("Editing clears any previous review", "", feedback(activity).text.toString())
                check(activity).performClick()
                assertFalse(saved(fixture).getBoolean("correct"))
                assertEquals(wrong, strings(saved(fixture).getJSONArray("answers")))
                assertTrue(CppExerciseFeedback.wasReviewed(saved(fixture), JSONArray(wrong)))
                val visible = feedback(activity).text.toString()
                assertTrue(visible, visible.contains(guide))
                assertFalse("A conceptual failure must not blame code spacing", visible.contains(activity.getString(R.string.exercise_wrong_spacing)))
                assertFalse("Success-only explanation never appears on failure", visible.contains(explanation))
                assertTrue(feedback(activity) is ReadingTextView)
                assertFalse("Disabled narration does not start playback", playing(activity))
                val spoken = IncorrectFeedbackNarration.prepare(listOf(visible), NarrationLanguage.SPANISH, true)
                assertTrue("The whole visible review is accepted by the existing speech frontend", spoken.isNotEmpty())
                val script = LessonNarrationScript.page(fixture.lesson, fixture.page, NarrationLanguage.SPANISH)
                assertFalse("Incorrect guidance is absent from continuous page narration", script.texts.any { it.contains(guide) })
                val gate = script.gates.single { it.blockIndex == fixture.index }
                val expected = LessonNarrationText.page(paragraphPage(explanation), 0, NarrationLanguage.SPANISH)
                assertEquals("Success speech keeps the complete original explanation", expected,
                    script.texts.subList(gate.start + 1, gate.endExclusive))
            }; if (number == 0) capture(scenario, "inline-reparto-incorrect", fixture) }
            withFixture(fixture) { scenario -> scenario.onActivity { activity ->
                assertEquals(wrong, inputs(activity).map { it.text.toString() })
                assertTrue(feedback(activity).text.toString().contains(guide))
                assertFalse("Restoration is silent", playing(activity))
                writeInline(activity, correct)
                assertEquals("", feedback(activity).text.toString())
                assertFalse("Editing clears the previous reviewed-answer identity", saved(fixture).has(CppExerciseFeedback.REVIEWED_ANSWER_IDENTITY))
                check(activity).performClick()
                assertTrue(saved(fixture).getBoolean("correct"))
                assertEquals(correct, strings(saved(fixture).getJSONArray("answers")))
                assertTrue("Full explanation appears only after success", feedback(activity).text.toString().contains(explanation))
                assertFalse(check(activity).isEnabled)
                assertTrue(inputs(activity).none { it.isEnabled })
            }; if (number == 0) capture(scenario, "inline-reparto-success", fixture) }
            withFixture(fixture) { scenario -> scenario.onActivity { activity ->
                assertEquals(correct, inputs(activity).map { it.text.toString() })
                assertTrue(saved(fixture).getBoolean("correct"))
                assertTrue(feedback(activity).text.toString().contains(explanation))
                assertFalse(playing(activity))
            } }
            assertEquals(original.block.getJSONArray("answers").toString(), fixture.block.getJSONArray("answers").toString())
            assertEquals(original.block.opt("answerAlternatives")?.toString(), fixture.block.opt("answerAlternatives")?.toString())
        }
    }

    @Test fun legacyInlineDraftsStayUnreviewedAndMalformedOptionalFieldsCannotCreditAnAttempt() {
        val legacy = fixture(LOCATIONS[1])
        val answer = JSONArray(listOf("int", "incorrecto_qa"))
        ProgressRepository.saveExerciseState(context, ROUTE, legacy.id, legacy.page, legacy.index,
            JSONObject().put("answers", answer).put("correct", false))
        withFixture(legacy) { scenario -> scenario.onActivity { activity ->
            assertEquals(strings(answer), inputs(activity).map { it.text.toString() })
            assertEquals("A historical unreviewed draft never acquires a submission", "", feedback(activity).text.toString())
            check(activity).performClick()
            assertFalse(saved(legacy).getBoolean("correct"))
            assertTrue(feedback(activity).text.toString().contains(activity.getString(R.string.cpp_review_fill)))
            assertTrue("Absent metadata retains the legacy status", feedback(activity).text.toString().startsWith(activity.getString(R.string.exercise_wrong_spacing)))
            writeInline(activity, strings(legacy.block.getJSONArray("answers")))
            check(activity).performClick()
            assertTrue(saved(legacy).getBoolean("correct"))
            assertEquals("Legacy success text stays identical", activity.getString(R.string.lesson_correct_feedback), feedback(activity).text.toString())
        } }
        for (property in listOf("reviewFeedback", "explanation")) {
            val malformed = enhance(fixture(LOCATIONS[1])) { it.put(property, JSONArray().put("not text")) }
            withFixture(malformed) { scenario -> scenario.onActivity { activity ->
                writeInline(activity, strings(malformed.block.getJSONArray("answers")))
                check(activity).performClick()
                assertNull("Malformed optional content must not charge or save an evaluated attempt",
                    ProgressRepository.getExerciseState(context, malformed.id, malformed.page, malformed.index))
                assertEquals(0, field(activity, "completedExercises"))
                assertTrue(check(activity).isEnabled)
                assertEquals("", feedback(activity).text.toString())
            } }
        }
    }

    @Test fun exactPublishedPredictionUsesLiteralImeFlagsAndPreservesDeliberateSpacesAndAccents() {
        val fixture = fixture(Triple("programas_y_ejecucion/lesson_fuente_compilacion_y_diagnostico.json", 4, 6))
        assertEquals("prediccion_revision_inicial", fixture.block.getString("id"))
        for ((index, literal) in listOf("Revision inicial ", "Revisión inicial", "Revision inicial").withIndex()) {
            withFixture(fixture) { scenario -> scenario.onActivity { activity ->
                val input = inputs(activity).single()
                assertLiteralIme(input, multiline = false)
                commitLiteral(input, literal)
                assertEquals("Android's InputConnection must preserve every voluntary character", literal, input.text.toString())
                check(activity).performClick()
                assertEquals(literal, saved(fixture).getString("answer"))
                assertEquals(index == 2, saved(fixture).getBoolean("correct"))
            }; capture(scenario, "prediction-literal-$index", fixture) }
            withFixture(fixture) { scenario -> scenario.onActivity { activity ->
                assertEquals(literal, inputs(activity).single().text.toString())
                assertEquals(index == 2, saved(fixture).getBoolean("correct"))
            } }
        }
        File(evidence(), "prediction-ime-limit.json").writeText(JSONObject()
            .put("device", "LDPlayer").put("tested", "Real Android EditText/InputConnection flags, voluntary text, validation and reopening")
            .put("notTested", "Honor software keyboard autocorrection; InputConnection insertion is not a substitute")
            .toString(2))
    }

    @Test fun multilinePredictionsKeepNewlinesBoundaryWhitespaceAndExactRestoration() {
        val literal = " A\nB \t"
        val fixture = enhance(fixture(Triple("programas_y_ejecucion/lesson_fuente_compilacion_y_diagnostico.json", 4, 6))) {
            it.put("answers", JSONArray().put(literal)).put("code", "#include <iostream>\nint main() { std::cout << \" A\\nB \\t\"; }")
        }
        withFixture(fixture) { scenario -> scenario.onActivity { activity ->
            val input = inputs(activity).single()
            assertLiteralIme(input, multiline = true)
            commitLiteral(input, literal)
            assertEquals(literal, input.text.toString())
            assertFalse("Enter still belongs to multiline editing", PuzzleAnswerSubmit.isSingleLine(input))
            check(activity).performClick()
            assertEquals(literal, saved(fixture).getString("answer"))
            assertTrue(saved(fixture).getBoolean("correct"))
        }; capture(scenario, "prediction-multiline-exact", fixture) }
        withFixture(fixture) { scenario -> scenario.onActivity { activity ->
            assertEquals(literal, inputs(activity).single().text.toString())
            assertFalse(check(activity).isEnabled)
        } }
    }

    private data class Fixture(val id: String, val path: String, val sourceId: String, val sourceSha: String,
                               val page: Int, val index: Int, val block: JSONObject, val lesson: JSONObject)
    private fun source(path: String) = JSONObject(context.assets.open("content/es/programming/cpp_fundamentals/$path")
        .bufferedReader().use { it.readText() })
    private fun fixture(location: Triple<String, Int, Int>): Fixture {
        val (path, page, blockNumber) = location
        val source = source(path)
        val block = JSONObject(source.getJSONArray("pages").getJSONObject(page - 1).getJSONArray("blocks").getJSONObject(blockNumber - 1).toString())
        val id = "cpp_fundamentals__objetos_y_estado__inline_feedback_qa_${UUID.randomUUID().toString().replace("-", "")}"
        val pages = JSONArray().apply { repeat(source.getJSONArray("pages").length()) { index ->
            put(JSONObject().put("blocks", JSONArray().apply {
                if (index == page - 1) {
                    repeat(blockNumber - 1) { put(JSONObject().put("type", "paragraph").put("text", "Comprobación aislada")) }
                    put(JSONObject(block.toString()))
                } else put(JSONObject().put("type", "paragraph").put("text", "Comprobación aislada"))
            }))
        } }
        return Fixture(id, path, source.getString("id"), sha(source.toString()), page - 1, blockNumber - 1, block,
            JSONObject().put("id", id).put("title", source.getString("title")).put("pages", pages))
    }
    private fun enhance(original: Fixture, edit: (JSONObject) -> Unit): Fixture {
        val block = JSONObject(original.block.toString()).also(edit)
        val lesson = JSONObject(original.lesson.toString())
        lesson.getJSONArray("pages").getJSONObject(original.page).getJSONArray("blocks").put(original.index, block)
        return original.copy(block = block, lesson = lesson)
    }
    private fun withFixture(fixture: Fixture, body: (ActivityScenario<LessonActivity>) -> Unit) {
        ProgressRepository.saveCurrentPage(context, ROUTE, fixture.sourceId, 0)
        val intent = Intent(context, LessonActivity::class.java).putExtra("routeId", ROUTE)
            .putExtra("courseId", "cpp_fundamentals").putExtra("unitId", fixture.sourceId.removePrefix("cpp_fundamentals__").substringBefore("__"))
            .putExtra("lessonId", fixture.sourceId).putExtra("lessonNumber", 1).putExtra("lessonTotal", 1)
        ActivityScenario.launch<LessonActivity>(intent).use { scenario ->
            var sourceInstance = 0
            var stableSince = 0L
            await("The unchanged source lesson must load before injecting exclusive QA identities") { ready -> scenario.onActivity { activity ->
                val loaded = field(activity, "lessonJson") as? JSONObject
                val loadedSource = field(activity, "currentLessonId") == fixture.sourceId && loaded?.optString("id") == fixture.sourceId &&
                    content(activity).childCount > 0 && activity.window.decorView.hasWindowFocus() &&
                    !content(activity).isLayoutRequested && content(activity).width > 0
                val instance = System.identityHashCode(activity) xor System.identityHashCode(loaded)
                if (!loadedSource || instance != sourceInstance) {
                    sourceInstance = instance
                    stableSince = if (loadedSource) SystemClock.uptimeMillis() else 0L
                }
                ready(loadedSource && SystemClock.uptimeMillis() - stableSince >= 500L)
            } }
            scenario.onActivity { activity ->
                setField(activity, "currentLessonId", fixture.id)
                setField(activity, "lessonJson", JSONObject(fixture.lesson.toString()))
                setField(activity, "totalPages", fixture.lesson.getJSONArray("pages").length())
                setField(activity, "currentPage", fixture.page)
                listOf("pageViewCache", "pageRequiredExercises", "pageCompletedExercises").forEach {
                    (field(activity, it) as MutableMap<*, *>).clear()
                }
                LessonActivity::class.java.getDeclaredMethod("renderCurrentPage").apply { isAccessible = true }.invoke(activity)
            }
            await("The real inline/prediction view must finish layout") { ready -> scenario.onActivity { activity ->
                assertEquals(fixture.id, field(activity, "currentLessonId"))
                ready(content(activity).width > 0 && !content(activity).isLayoutRequested && inputs(activity).isNotEmpty())
            } }
            body(scenario)
        }
        assertEquals("Source content, IDs, answers and later explanation are untouched", fixture.sourceSha, sha(source(fixture.path).toString()))
    }
    private fun assertLiteralIme(input: EditText, multiline: Boolean) {
        val info = EditorInfo()
        assertNotNull(input.onCreateInputConnection(info))
        for (type in listOf(input.inputType, info.inputType)) {
            assertEquals(InputType.TYPE_CLASS_TEXT, type and InputType.TYPE_MASK_CLASS)
            assertEquals(InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD, type and InputType.TYPE_MASK_VARIATION)
            assertTrue(type and InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS != 0)
            assertEquals(0, type and InputType.TYPE_TEXT_FLAG_AUTO_CORRECT)
            assertEquals(multiline, type and InputType.TYPE_TEXT_FLAG_MULTI_LINE != 0)
        }
        assertFalse("Literal technical input stays visible", input.transformationMethod is android.text.method.PasswordTransformationMethod)
        if (!multiline) assertTrue(PuzzleAnswerSubmit.isSingleLine(input))
    }
    private fun commitLiteral(input: EditText, literal: String) {
        input.requestFocus()
        input.selectAll()
        assertTrue(requireNotNull(input.onCreateInputConnection(EditorInfo())).commitText(literal, 1))
    }
    private fun writeInline(activity: LessonActivity, values: List<String>) {
        assertEquals(values.size, inputs(activity).size)
        values.forEachIndexed { index, value -> inputs(activity)[index].setText(value) }
    }
    private fun playing(activity: LessonActivity) = (field(activity, "narrationController") as? LessonNarrationController)?.isPlaying == true
    private fun saved(fixture: Fixture) = requireNotNull(ProgressRepository.getExerciseState(context, fixture.id, fixture.page, fixture.index))
    private fun inputs(activity: LessonActivity) = descendants(content(activity)).filterIsInstance<EditText>()
    private fun feedback(activity: LessonActivity) = content(activity).getChildAt(content(activity).childCount - 1) as TextView
    private fun check(activity: LessonActivity) = descendants(content(activity)).filterIsInstance<Button>().single {
        it.text.toString() in listOf(activity.getString(R.string.exercise_check), activity.getString(R.string.exercise_correct))
    }
    private fun paragraphPage(text: String) = JSONObject().put("pages", JSONArray().put(JSONObject().put("blocks", JSONArray()
        .put(JSONObject().put("type", "paragraph").put("text", text)))))
    private fun content(activity: LessonActivity): ViewGroup = activity.findViewById(R.id.lessonContent)
    private fun descendants(view: View): List<View> = buildList {
        add(view); if (view is ViewGroup) repeat(view.childCount) { addAll(descendants(view.getChildAt(it))) }
    }
    private fun strings(array: JSONArray) = List(array.length()) { array.getString(it) }
    private fun field(activity: LessonActivity, name: String): Any? = LessonActivity::class.java.getDeclaredField(name).apply { isAccessible = true }.get(activity)
    private fun setField(activity: LessonActivity, name: String, value: Any) = LessonActivity::class.java.getDeclaredField(name).apply { isAccessible = true }.set(activity, value)
    private fun preferenceFiles() = File(context.applicationInfo.dataDir, "shared_prefs").listFiles().orEmpty()
        .filter { it.name.endsWith(".xml") }.map { it.name.removeSuffix(".xml") }.toSet()
    private fun sha(text: String) = MessageDigest.getInstance("SHA-256").digest(text.toByteArray()).joinToString("") { "%02x".format(it) }
    private fun await(reason: String, condition: ((Boolean) -> Unit) -> Unit) {
        val until = SystemClock.uptimeMillis() + 12_000L
        while (SystemClock.uptimeMillis() < until) {
            instrumentation.waitForIdleSync()
            var okay = false
            condition { okay = it }
            if (okay) return
            SystemClock.sleep(40L)
        }
        fail(reason)
    }
    private fun evidence() = File(requireNotNull(context.getExternalFilesDir(null)), "cpp-inline-feedback-input-qa/$session").apply { mkdirs() }
    private fun capture(scenario: ActivityScenario<LessonActivity>, name: String, fixture: Fixture) {
        var originalInputs = emptyList<Triple<String, Int, Int>>()
        // Framing is a real legal lesson scroll, not a detached view rendering.
        // Clear the editing caret/IME so a blinking cursor cannot masquerade as
        // an unstable capture. The reader bar remains at its real position.
        scenario.onActivity { activity ->
            originalInputs = inputs(activity).map { Triple(it.text.toString(), it.selectionStart, it.selectionEnd) }
            activity.currentFocus?.clearFocus()
            (activity.getSystemService(Context.INPUT_METHOD_SERVICE) as android.view.inputmethod.InputMethodManager)
                .hideSoftInputFromWindow(activity.window.decorView.windowToken, 0)
        }
        await("Feedback and viewport must finish their real layout before framing") { ready -> scenario.onActivity { activity ->
            val target = feedback(activity)
            val scroll = activity.findViewById<ScrollView>(R.id.lessonScroll)
            ready(target.width > 0 && target.height > 0 && target.layout != null &&
                !target.isLayoutRequested && !scroll.isLayoutRequested && scroll.height > 0)
        } }
        fun rectangle(rect: Rect) = JSONArray(listOf(rect.left, rect.top, rect.right, rect.bottom))
        fun visibleBounds(view: View): Rect {
            val rect = Rect()
            if (!view.isShown || !view.getGlobalVisibleRect(rect)) return Rect()
            val origin = IntArray(2)
            view.rootView.getLocationOnScreen(origin)
            rect.offset(origin[0], origin[1])
            return rect
        }
        fun safeViewport(activity: LessonActivity): Rect {
            val scroll = activity.findViewById<ScrollView>(R.id.lessonScroll)
            val viewport = visibleBounds(scroll)
            val overlay = activity.findViewById<View>(R.id.lessonNarrationControls)
            val reader = overlay?.takeIf { it.alpha > 0f }?.let(::visibleBounds) ?: Rect()
            if (!reader.isEmpty && Rect.intersects(viewport, reader)) {
                viewport.bottom = (reader.top - (8 * activity.resources.displayMetrics.density).toInt()).coerceAtLeast(viewport.top)
            }
            assertTrue("At least one actual text line must fit above the floating reader", viewport.height() > 0)
            return viewport
        }
        fun captureText(label: String, target: (LessonActivity) -> TextView): JSONObject {
            var required = emptySet<Int>()
            scenario.onActivity { activity ->
                val view = target(activity)
                val layout = requireNotNull(view.layout)
                required = (0 until layout.lineCount).filter { line ->
                    view.text.subSequence(layout.getLineStart(line), layout.getLineEnd(line)).any { !it.isWhitespace() }
                }.toSet()
            }
            assertTrue("Evidence must contain real nonempty visible text", required.isNotEmpty())
            val covered = mutableSetOf<Int>()
            val frames = JSONArray()
            while (!covered.containsAll(required)) {
                val next = required.first { it !in covered }
                var desired = 0
                var scrollLimit = 0
                var bitmap: android.graphics.Bitmap? = null
                var previousCrop: android.graphics.Bitmap? = null
                var stable = 0
                var sampled = 0
                var frame = JSONObject()
                var visibleLines = emptySet<Int>()
                val deadline = SystemClock.uptimeMillis() + 8_000L
                try {
                    while (SystemClock.uptimeMillis() < deadline && stable < 4) {
                        instrumentation.waitForIdleSync()
                        var bounds = Rect()
                        var layoutReady = false
                        scenario.onActivity { activity ->
                            val view = target(activity)
                            val scroll = activity.findViewById<ScrollView>(R.id.lessonScroll)
                            // Rendering and overlay-clearance posts may still complete
                            // after the first scroll. Reassert the same finite legal
                            // position, then require four stable native screenshots.
                            bounds = safeViewport(activity)
                            val before = IntArray(2).also(view::getLocationOnScreen)
                            val scrollPosition = IntArray(2).also(scroll::getLocationOnScreen)
                            val body = requireNotNull(scroll.getChildAt(0))
                            val margin = (body.layoutParams as? ViewGroup.MarginLayoutParams)?.let { it.topMargin + it.bottomMargin } ?: 0
                            scrollLimit = (body.height + margin + scroll.paddingTop + scroll.paddingBottom - scroll.height).coerceAtLeast(0)
                            val contentTop = before[1] - scrollPosition[1] + scroll.scrollY
                            val lineTop = view.totalPaddingTop + requireNotNull(view.layout).getLineTop(next) - view.scrollY
                            desired = (contentTop + lineTop - (bounds.top - scrollPosition[1]) -
                                (8 * activity.resources.displayMetrics.density).toInt()).coerceIn(0, scrollLimit)
                            if (scroll.scrollY != desired) scroll.scrollTo(0, desired)
                            layoutReady = activity.hasWindowFocus() && !scroll.isLayoutRequested &&
                                !view.isLayoutRequested && scroll.scrollY == desired
                            val position = IntArray(2).also(view::getLocationOnScreen)
                            val layout = requireNotNull(view.layout)
                            val lineBounds = JSONArray()
                            visibleLines = required.filter { line ->
                                val left = kotlin.math.floor(position[0] + view.totalPaddingLeft + layout.getLineLeft(line) - view.scrollX).toInt()
                                val right = kotlin.math.ceil(position[0] + view.totalPaddingLeft + layout.getLineRight(line) - view.scrollX).toInt()
                                val rect = Rect(left, position[1] + view.totalPaddingTop + layout.getLineTop(line) - view.scrollY,
                                    right.coerceAtLeast(left + 1), position[1] + view.totalPaddingTop + layout.getLineBottom(line) - view.scrollY)
                                val visible = bounds.contains(rect)
                                lineBounds.put(JSONObject().put("line", line).put("start", layout.getLineStart(line))
                                    .put("endExclusive", layout.getLineEnd(line)).put("bounds", rectangle(rect)).put("fullyVisible", visible))
                                visible
                            }.toSet()
                            frame = JSONObject().put("scrollY", scroll.scrollY).put("maxLegalScrollY", scrollLimit)
                                .put("safeViewport", rectangle(bounds)).put("textViewBounds", rectangle(Rect(position[0], position[1],
                                    position[0] + view.width, position[1] + view.height))).put("lines", lineBounds)
                                .put("fullyVisibleLines", JSONArray(visibleLines.sorted()))
                        }
                        val shot = requireNotNull(instrumentation.uiAutomation.takeScreenshot())
                        assertTrue("The evidence viewport is inside the actual display", bounds.left >= 0 && bounds.top >= 0 &&
                            bounds.right <= shot.width && bounds.bottom <= shot.height)
                        val crop = android.graphics.Bitmap.createBitmap(shot, bounds.left, bounds.top, bounds.width(), bounds.height())
                        stable = if (layoutReady && next in visibleLines && previousCrop?.sameAs(crop) == true) stable + 1
                            else if (layoutReady && next in visibleLines) 1 else 0
                        previousCrop?.recycle()
                        previousCrop = crop
                        bitmap?.recycle()
                        bitmap = shot
                        sampled++
                        if (stable < 4) SystemClock.sleep(100L)
                    }
                    assertTrue("The requested text line must be unobstructed in four stable native frames: $label/$next", stable >= 4)
                    assertTrue("Each legal frame must add real text coverage", visibleLines.any { it !in covered })
                    val fileName = if (frames.length() == 0) "$label.png" else "$label-frame-${frames.length() + 1}.png"
                    File(evidence(), fileName).outputStream().use { output ->
                        assertTrue(requireNotNull(bitmap).compress(android.graphics.Bitmap.CompressFormat.PNG, 100, output))
                    }
                    frame.put("file", fileName).put("stableConsecutiveFrames", stable).put("sampledFrames", sampled)
                        .put("samplingIntervalMillis", 100).put("boundsCoordinateSpace", "displayPixels")
                    frames.put(frame)
                    covered += visibleLines
                    assertTrue("Finite coverage cannot require more frames than native text lines", frames.length() <= required.size)
                } finally { previousCrop?.recycle(); bitmap?.recycle() }
            }
            assertEquals("All nonblank native text lines are fully visible without the reader covering them", required, covered)
            return JSONObject().put("frames", frames).put("requiredLines", JSONArray(required.sorted()))
                .put("coveredLines", JSONArray(covered.sorted())).put("allNonblankLinesCovered", true)
                .put("floatingReaderHiddenOrMoved", false)
        }
        val feedbackFrames = captureText(name, ::feedback)
        val answerFrames = if (fixture.block.getString("type") == "output_prediction") {
            captureText("$name-answer") { inputs(it).single() }
        } else null
        scenario.onActivity { activity ->
            assertEquals("Evidence framing never changes text, cursor or selections", originalInputs,
                inputs(activity).map { Triple(it.text.toString(), it.selectionStart, it.selectionEnd) })
            File(evidence(), "$name.json").writeText(JSONObject().put("source", fixture.path).put("page", fixture.page + 1)
                .put("block", fixture.index + 1).put("qaLesson", fixture.id).put("package", context.packageName)
                .put("feedback", feedback(activity).text.toString()).put("saved", saved(fixture))
                .put("feedbackCoverage", feedbackFrames).put("answerCoverage", answerFrames)
                .put("kind", "Real LDPlayer window screenshot; optional authored feedback exists only in the in-memory QA copy")
                .toString(2))
        }
    }
    companion object {
        private const val ROUTE = "programming"
        private val LOCATIONS = listOf(
            Triple("objetos_y_estado/lesson_enteros_y_aproximaciones.json", 8, 7),
            Triple("objetos_y_estado/lesson_enteros_y_aproximaciones.json", 4, 4),
            Triple("objetos_y_estado/lesson_declarar_e_inicializar.json", 3, 5),
            Triple("objetos_y_estado/lesson_estado_asignacion_y_copia.json", 6, 5),
            Triple("objetos_y_estado/lesson_nombres_con_intencion.json", 4, 5),
            Triple("objetos_y_estado/lesson_conversiones_y_deduccion.json", 4, 6),
            Triple("objetos_y_estado/lesson_cadenas_y_texto.json", 4, 5),
            Triple("objetos_y_estado/lesson_caracteres_y_valores_logicos.json", 3, 4),
            Triple("programas_y_ejecucion/lesson_texto_secuencia_y_formato.json", 5, 2))
        private val KNOWN_PREFERENCES = setOf("learning_progress", "navigation_state", "lesson_narration_settings", "attempt_reserve_seconds_test",
            "cpp_lesson_drafts", "app_settings", "appearance_settings", "ui_language_settings", "premium_state")
    }
}
