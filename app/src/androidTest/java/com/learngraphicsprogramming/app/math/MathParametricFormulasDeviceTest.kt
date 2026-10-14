package com.learngraphicsprogramming.app.math

import android.app.Activity
import android.app.Application
import android.app.KeyguardManager
import android.content.Context
import android.content.Intent
import android.content.pm.ActivityInfo
import android.content.res.Configuration
import android.graphics.Bitmap
import android.os.Bundle
import android.os.SystemClock
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.Button
import android.widget.CheckBox
import android.widget.EditText
import android.widget.Spinner
import android.widget.TextView
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.AttemptRepository
import com.learngraphicsprogramming.app.BuildConfig
import com.learngraphicsprogramming.app.ExerciseAttemptLedger
import com.learngraphicsprogramming.app.LessonActivity
import com.learngraphicsprogramming.app.LessonRepository
import com.learngraphicsprogramming.app.PremiumRepository
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
import java.util.UUID

/** Published parametric controls in LessonActivity, isolated from student identities and data.
 * The original authored page/block coordinates and complete blocks are kept in each fixture.
 * All existing QA preferences are restored, including the source lesson's launch preferences.
 */
@RunWith(AndroidJUnit4::class)
class MathParametricFormulasDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val snapshots = linkedMapOf<String, Map<String, *>>()
    private var originalPreferenceFiles = emptySet<String>()
    private var preferencesCaptured = false
    private var screenGuardRegistered = false
    private val screenGuard = object : Application.ActivityLifecycleCallbacks {
        override fun onActivityCreated(activity: Activity, state: Bundle?) {
            if (activity is LessonActivity && activity.packageName == QA_PACKAGE)
                activity.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        }
        override fun onActivityStarted(activity: Activity) = Unit
        override fun onActivityResumed(activity: Activity) = Unit
        override fun onActivityPaused(activity: Activity) = Unit
        override fun onActivityStopped(activity: Activity) = Unit
        override fun onActivitySaveInstanceState(activity: Activity, state: Bundle) = Unit
        override fun onActivityDestroyed(activity: Activity) = Unit
    }

    @Before fun requireQaAndPreserveAllItsPreferences() {
        assertEquals("Use -Plgp.curricularQa=true, never the student's app", QA_PACKAGE, context.packageName)
        assertEquals(QA_PACKAGE, BuildConfig.APPLICATION_ID)
        val keyguard = context.getSystemService(Context.KEYGUARD_SERVICE) as KeyguardManager
        assertFalse("Unlock the test device", keyguard.isDeviceLocked)
        assertFalse("Dismiss the lock screen", keyguard.isKeyguardLocked)
        originalPreferenceFiles = preferenceFiles()
        (originalPreferenceFiles + PREFS).forEach { name ->
            snapshots[name] = context.getSharedPreferences(name, Context.MODE_PRIVATE).all.mapValues { (_, value) ->
                if (value is Set<*>) value.toSet() else value
            }
        }
        preferencesCaptured = true
        (context.applicationContext as Application).registerActivityLifecycleCallbacks(screenGuard)
        screenGuardRegistered = true
        NarrationSettingsRepository.saveEnabled(context, false)
        PremiumRepository.setVerifiedPremium(context)
    }

    @After fun restoreAllQaPreferences() {
        if (context.packageName != QA_PACKAGE || BuildConfig.APPLICATION_ID != QA_PACKAGE || !preferencesCaptured) return
        try {
            instrumentation.waitForIdleSync()
            (preferenceFiles() + snapshots.keys).forEach { name ->
                if (name !in originalPreferenceFiles) {
                    assertTrue("Delete only a QA preference created by the fixture: $name", context.deleteSharedPreferences(name))
                } else {
                    val original = snapshots.getValue(name)
                    val preferences = context.getSharedPreferences(name, Context.MODE_PRIVATE)
                    val editor = preferences.edit().clear()
                    original.forEach { (key, value) ->
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
                    assertTrue("Restore $name", editor.commit())
                    assertEquals("Restore every original QA value, including progress", original, preferences.all)
                }
            }
            assertEquals("Restore the original preference files", originalPreferenceFiles, preferenceFiles())
        } finally {
            if (screenGuardRegistered) {
                (context.applicationContext as Application).unregisterActivityLifecycleCallbacks(screenGuard)
                screenGuardRegistered = false
            }
        }
    }

    @Test fun allSevenPublishedExpectedAnswersAndIndependentEquivalentFormsPassRealChecks() {
        withPublishedFixture(CONSTRUCTION) { scenario, fixture ->
            assertContract(fixture, CONSTRUCTION_EXPECTED)
            for (answers in CONSTRUCTION_VARIANTS) scenario.onActivity { activity ->
                reset(activity)
                setAnswers(activity, answers)
                check(activity).performClick()
                assertSavedExpressions(activity, fixture, answers, true)
            }
        }
        withPublishedFixture(TRANSFER) { scenario, fixture ->
            assertContract(fixture, TRANSFER_EXPECTED)
            for (answers in TRANSFER_VARIANTS) scenario.onActivity { activity ->
                reset(activity)
                setAnswers(activity, answers)
                check(activity).performClick()
                assertSavedExpressions(activity, fixture, answers, true)
            }
        }
    }

    @Test fun completeValueAndDomainErrorsAreEvaluableAndRejectedByThePublishedControls() {
        withPublishedFixture(CONSTRUCTION) { scenario, fixture ->
            for (wrong in listOf("(d*u+b*v)/D", "(a*v-c*u)/D", "(d*u-b*v)/(a*d+b*c)",
                "d*(d*u-b*v)/(d*D)", "b*(d*u-b*v)/(b*D)")) scenario.onActivity { activity ->
                val answers = CONSTRUCTION_EXPECTED.toMutableList().apply { set(3, wrong) }
                val result = MathValidators.validate(fixture.responses.getJSONObject(3).getJSONObject("validator"), MathAnswer.Text(wrong))
                assertTrue("A complete error is an evaluable answer: $wrong", result.evaluable)
                assertFalse("No cancellation can silently add d!=0 or b!=0: $wrong", result.correct)
                setAnswers(activity, answers)
                check(activity).performClick()
                assertSavedExpressions(activity, fixture, answers, false)
            }
            scenario.onActivity { activity ->
                setAnswers(activity, CONSTRUCTION_EXPECTED)
                check(activity).performClick()
                assertSavedExpressions(activity, fixture, CONSTRUCTION_EXPECTED, true)
            }
        }
        withPublishedFixture(TRANSFER) { scenario, fixture ->
            for (answers in listOf(
                listOf("(d*u+d*v-b*u-b*v)/D", TRANSFER_EXPECTED[1]),
                listOf(TRANSFER_EXPECTED[0], "(a*u-a*v-c*u+c*v)/D"),
                TRANSFER_EXPECTED.reversed()
            )) scenario.onActivity { activity ->
                setAnswers(activity, answers)
                check(activity).performClick()
                assertSavedExpressions(activity, fixture, answers, false)
            }
        }
    }

    @Test fun syntaxFunctionsAndResourceLimitsPermitRetryWithoutIncorrectHistoryOrPenalty() {
        PremiumRepository.setVerifiedFree(context)
        seedReserve(12)
        withPublishedFixture(CONSTRUCTION) { scenario, fixture ->
            for (unsupported in listOf("(d*u-b*v)/(", "sin(a)", "a+".repeat(2_100) + "a", "(a+b)^1000")) {
                scenario.onActivity { activity ->
                    // An evaluable wrong earlier response must not charge a mixed technical submission.
                    val answers = CONSTRUCTION_EXPECTED.toMutableList().apply { set(0, "a*d+b*c"); set(3, unsupported) }
                    setAnswers(activity, answers)
                    val draftBeforeCheck = saved(activity, fixture).toString()
                    val historyBeforeCheck = ledger(activity).save().toString()
                    val result = MathValidators.validate(fixture.responses.getJSONObject(3).getJSONObject("validator"), MathAnswer.Text(unsupported))
                    assertFalse("Unsupported syntax or limits are technical: $unsupported", result.evaluable)
                    assertFalse(result.correct)
                    assertTrue(check(activity).isEnabled)
                    check(activity).performClick()
                    check(activity).performClick()
                    assertEquals("Keep only the unchanged draft, no evaluated-incorrect write", draftBeforeCheck, saved(activity, fixture).toString())
                    assertEquals("A technical failure cannot enter the wrong-answer history", historyBeforeCheck, ledger(activity).save().toString())
                    assertEquals("Technical retries do not consume the QA reserve", 12, AttemptRepository.getState(context, ROUTE).available)
                    assertEquals(0, field(activity, "completedExercises"))
                    assertEquals(answers, inputs(activity).map { it.text.toString() })
                    assertTrue("Technical submissions keep the fields editable", inputs(activity).all { it.isEnabled })
                    val messages = descendants(card(activity)).filterIsInstance<TextView>().map { it.text.toString() }
                    assertFalse("Do not present the curricular incorrect message for a technical failure",
                        messages.any { it.startsWith("✗") })
                    assertTrue("A localized technical message is displayed", messages.last().isNotBlank())
                }
            }
            scenario.onActivity { activity ->
                val wrong = CONSTRUCTION_EXPECTED.toMutableList().apply { set(3, "d*(d*u-b*v)/(d*D)") }
                setAnswers(activity, wrong)
                check(activity).performClick()
                check(activity).performClick()
                assertEquals("A complete domain error charges exactly once", 11, AttemptRepository.getState(context, ROUTE).available)
                assertEquals("One recorded evaluated-wrong identity", 1, ledger(activity).save().getJSONArray("exercises").length())
                assertSavedExpressions(activity, fixture, wrong, false)
                setAnswers(activity, CONSTRUCTION_EXPECTED)
                check(activity).performClick()
                assertEquals("The supported retry succeeds without another charge", 11, AttemptRepository.getState(context, ROUTE).available)
                assertSavedExpressions(activity, fixture, CONSTRUCTION_EXPECTED, true)
            }
        }
    }

    @Test fun longCorrectAndIncorrectExpressionsRestoreAcrossReconstructionRotationAndRecreation() {
        withPublishedFixture(TRANSFER) { scenario, fixture ->
            val incorrect = listOf("((d*(u+v)+b*(u-v))*D)/(D*D)", "((a*(u-v)-c*(u+v))*D)/(D*D)")
            scenario.onActivity { activity ->
                setAnswers(activity, incorrect)
                check(activity).performClick()
                assertSavedExpressions(activity, fixture, incorrect, false)
                injectFixture(activity, fixture)
                assertSavedExpressions(activity, fixture, incorrect, false)
            }
            orient(scenario, fixture, Configuration.ORIENTATION_LANDSCAPE)
            scenario.onActivity { assertSavedExpressions(it, fixture, incorrect, false) }
            scenario.recreate()
            awaitFixture(scenario, fixture)
            scenario.onActivity { activity ->
                assertSavedExpressions(activity, fixture, incorrect, false)
                setAnswers(activity, LONG_TRANSFER)
                check(activity).performClick()
                assertSavedExpressions(activity, fixture, LONG_TRANSFER, true)
            }
            orient(scenario, fixture, Configuration.ORIENTATION_PORTRAIT)
            scenario.onActivity { assertSavedExpressions(it, fixture, LONG_TRANSFER, true) }
            captureStableScreen("portrait-transfer-correct", scenario, fixture)
            orient(scenario, fixture, Configuration.ORIENTATION_LANDSCAPE)
            scenario.onActivity { assertSavedExpressions(it, fixture, LONG_TRANSFER, true) }
            captureStableScreen("landscape-transfer-correct", scenario, fixture)
            scenario.recreate()
            awaitFixture(scenario, fixture)
            scenario.onActivity { activity ->
                assertSavedExpressions(activity, fixture, LONG_TRANSFER, true)
                assertEquals("Completion keeps the original authored zero-based coordinate", 1, field(activity, "completedExercises"))
            }
        }
    }

    @Test fun allNinePreviouslyPublishedCompatiblePuzzlesStillAcceptAndRestoreTheirControls() {
        for (id in ORIGINAL_PUZZLES) withPublishedFixture(id) { scenario, fixture ->
            scenario.onActivity { fillCompatibleExpected(it, fixture) }
            // Spinner selection callbacks are delivered after layout; use the settled real
            // controls before invoking Check, rather than relying on a synchronous adapter.
            instrumentation.waitForIdleSync()
            scenario.onActivity { activity ->
                check(activity).performClick()
                assertTrue("The original compatible puzzle still succeeds: $id", saved(activity, fixture).getBoolean("correct"))
                assertTrue(MathPuzzleState.isCurrentCorrect(fixture.block, saved(activity, fixture)))
                assertEquals(1, field(activity, "completedExercises"))
                injectFixture(activity, fixture)
                assertTrue("Restore an original puzzle's correct answer: $id", saved(activity, fixture).getBoolean("correct"))
                assertFalse(check(activity).isEnabled)
                assertEquals(1, field(activity, "completedExercises"))
            }
        }
    }

    private fun assertContract(fixture: Fixture, expected: List<String>) {
        assertEquals(expected.size, fixture.responses.length())
        expected.forEachIndexed { index, value ->
            val response = fixture.responses.getJSONObject(index)
            assertEquals("math_expression_input", response.getString("type"))
            assertEquals("simple", response.getString("inputMode"))
            assertEquals("ALGEBRA", response.getString("keyboard"))
            val validator = response.getJSONObject("validator")
            assertEquals("parametric_rational_expression", validator.getString("type"))
            assertEquals(value, validator.getString("expected"))
            assertEquals(listOf("a", "b", "c", "d", "u", "v"), strings(validator.getJSONArray("parameters")))
            assertEquals("a*d-b*c", validator.getJSONObject("definitions").getString("D"))
            assertEquals(listOf("D"), strings(validator.getJSONArray("nonzero")))
            MathValidators.validateSpec(validator)
        }
    }

    private fun withPublishedFixture(blockId: String, test: (ActivityScenario<LessonActivity>, Fixture) -> Unit) {
        val source = LessonRepository.loadLessonJson(context, ROUTE, SOURCE_LESSON)
        val pages = source.getJSONArray("pages")
        val locations = (0 until pages.length()).flatMap { page ->
            val blocks = pages.getJSONObject(page).getJSONArray("blocks")
            (0 until blocks.length()).mapNotNull { block ->
                blocks.getJSONObject(block).takeIf { it.optString("id") == blockId }?.let { Triple(page, block, it) }
            }
        }
        assertEquals("The published asset contains exactly one requested curricular block: $blockId", 1, locations.size)
        val (page, blockIndex, published) = locations.single()
        val block = JSONObject(published.toString())
        val id = "math_algebra_elemental__sistemas_lineales__parametric_qa_${UUID.randomUUID().toString().replace("-", "") }"
        val fixturePages = JSONArray().apply {
            repeat(pages.length()) { p -> put(JSONObject().put("blocks", JSONArray().apply {
                if (p == page) {
                    repeat(blockIndex) { put(JSONObject().put("type", "paragraph").put("text", "")) }
                    put(block)
                } else put(JSONObject().put("type", "paragraph").put("text", ""))
            })) }
        }
        val fixture = Fixture(id, page, blockIndex, block,
            JSONObject().put("id", id).put("title", source.getString("title")).put("pages", fixturePages))
        val original = block.toString()
        ProgressRepository.saveCurrentPage(context, ROUTE, SOURCE_LESSON, page)
        val intent = Intent(context, LessonActivity::class.java).putExtra("routeId", ROUTE)
            .putExtra("courseId", "math_algebra_elemental").putExtra("unitId", "sistemas_lineales")
            .putExtra("lessonId", SOURCE_LESSON).putExtra("lessonNumber", 1).putExtra("lessonTotal", 1)
        ActivityScenario.launch<LessonActivity>(intent).use { scenario ->
            scenario.onActivity { injectFixture(it, fixture) }
            awaitFixture(scenario, fixture)
            test(scenario, fixture)
        }
        assertEquals("Checks and lifecycle events never change the published block", original, block.toString())
        val after = LessonRepository.loadLessonJson(context, ROUTE, SOURCE_LESSON).getJSONArray("pages")
            .getJSONObject(page).getJSONArray("blocks").getJSONObject(blockIndex)
        assertEquals("The packaged curricular block remains identical", original, after.toString())
    }

    private fun injectFixture(activity: LessonActivity, fixture: Fixture) {
        assertEquals(QA_PACKAGE, activity.packageName)
        assertEquals(ROUTE, field(activity, "currentRouteId"))
        setField(activity, "currentLessonId", fixture.id)
        setField(activity, "lessonJson", JSONObject(fixture.lesson.toString()))
        setField(activity, "totalPages", fixture.lesson.getJSONArray("pages").length())
        setField(activity, "currentPage", fixture.page)
        listOf("pageViewCache", "pageRequiredExercises", "pageCompletedExercises").forEach { mapField(activity, it).clear() }
        invoke(activity, "renderCurrentPage")
        assertEquals("One published puzzle in the isolated lesson page", 1, field(activity, "requiredExercises"))
        assertEquals(1, descendants(content(activity)).count { it.tag == MathPuzzleRenderer.INTERACTION_TAG })
    }

    private fun awaitFixture(scenario: ActivityScenario<LessonActivity>, fixture: Fixture, orientation: Int? = null) {
        val deadline = SystemClock.uptimeMillis() + 5_000L
        while (SystemClock.uptimeMillis() < deadline) {
            instrumentation.waitForIdleSync()
            var ready = false
            scenario.onActivity { activity ->
                if (orientation == null || activity.resources.configuration.orientation == orientation) {
                    if (field(activity, "currentLessonId") != fixture.id) injectFixture(activity, fixture)
                    ready = card(activity).width > 0 && check(activity).isAttachedToWindow
                }
            }
            if (ready) return
            SystemClock.sleep(50)
        }
        fail("The curricular fixture did not complete native layout")
    }

    private fun orient(scenario: ActivityScenario<LessonActivity>, fixture: Fixture, orientation: Int) {
        scenario.onActivity { it.requestedOrientation = if (orientation == Configuration.ORIENTATION_PORTRAIT)
            ActivityInfo.SCREEN_ORIENTATION_PORTRAIT else ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE }
        awaitFixture(scenario, fixture, orientation)
    }

    private fun assertSavedExpressions(activity: LessonActivity, fixture: Fixture, answers: List<String>, correct: Boolean) {
        val state = saved(activity, fixture)
        assertEquals(MathPuzzleState.fingerprint(fixture.block), state.getString("contentFingerprint"))
        assertEquals(correct, state.getBoolean("correct"))
        assertEquals(correct, MathPuzzleState.isCurrentCorrect(fixture.block, state))
        assertEquals(answers, inputs(activity).map { it.text.toString() })
        fixture.responses.let { responses -> answers.forEachIndexed { index, expected ->
            assertEquals(MathAnswer.Text(expected), MathAnswerJson.decode(state.getJSONObject("responses")
                .getJSONObject(responses.getJSONObject(index).getString("id"))))
        } }
        assertEquals(if (correct) 1 else 0, field(activity, "completedExercises"))
        assertTrue(inputs(activity).all { it.isEnabled == !correct })
        assertEquals(!correct, check(activity).isEnabled)
        assertTrue(context.getSharedPreferences(PROGRESS, Context.MODE_PRIVATE)
            .contains("exercise_${fixture.id}_page_${fixture.page}_block_${fixture.blockIndex}"))
    }

    private fun fillCompatibleExpected(activity: LessonActivity, fixture: Fixture) {
        var inputIndex = 0
        var choiceIndex = 0
        var spinnerIndex = 0
        val edits = inputs(activity)
        val choices = descendants(card(activity)).filterIsInstance<CheckBox>()
        val spinners = descendants(card(activity)).filterIsInstance<Spinner>()
        for (i in 0 until fixture.responses.length()) {
            val response = fixture.responses.getJSONObject(i)
            val validator = response.getJSONObject("validator")
            when (validator.getString("type")) {
                "numeric" -> edits[inputIndex++].setText(validator.getString("expected"))
                "fields" -> {
                    val fields = response.getJSONArray("fields")
                    for (j in 0 until fields.length()) {
                        val field = fields.getJSONObject(j)
                        val value = validator.getJSONObject("fields").getJSONObject(field.getString("id")).getString("expected")
                        if (field.getString("type") == "dropdown") {
                            val options = field.getJSONArray("options")
                            val index = (0 until options.length()).single { options.getJSONObject(it).getString("id") == value }
                            spinners[spinnerIndex++].setSelection(index + 1) // The existing first row is the empty prompt.
                        } else edits[inputIndex++].setText(value)
                    }
                }
                "choice" -> {
                    val expected = strings(validator.getJSONArray("expected"))
                    val options = response.getJSONArray("options")
                    for (j in 0 until options.length()) {
                        val option = options.getJSONObject(j)
                        val checkbox = choices[choiceIndex++]
                        if (option.getString("id") in expected) checkbox.performClick()
                    }
                }
                else -> error("Unanticipated original compatible control in ${fixture.block.getString("id")}")
            }
        }
    }

    private fun captureStableScreen(name: String, scenario: ActivityScenario<LessonActivity>, fixture: Fixture) {
        scenario.onActivity { activity ->
            inputs(activity).forEach { it.clearFocus() }
            activity.currentFocus?.clearFocus()
            val ime = activity.getSystemService(Context.INPUT_METHOD_SERVICE) as android.view.inputmethod.InputMethodManager
            ime.hideSoftInputFromWindow(activity.window.decorView.windowToken, 0)
        }
        instrumentation.waitForIdleSync()
        // The source page can restore a scroll position below this shorter QA fixture.
        // Center the two real answer fields and wait until both fit the visible viewport.
        val viewportDeadline = SystemClock.uptimeMillis() + 5_000L
        var fieldsVisible = false
        while (SystemClock.uptimeMillis() < viewportDeadline) {
            scenario.onActivity { activity ->
                val scroll = activity.findViewById<android.widget.ScrollView>(R.id.lessonScroll)
                val fields = inputs(activity)
                assertEquals("Capture the transfer puzzle's two actual expression inputs", 2, fields.size)
                val union = android.graphics.Rect()
                fields.forEach { edit ->
                    val bounds = android.graphics.Rect()
                    edit.getDrawingRect(bounds)
                    scroll.offsetDescendantRectToMyCoords(edit, bounds)
                    union.union(bounds)
                }
                assertTrue("The two answer fields fit in this orientation", union.height() <= scroll.height)
                scroll.scrollTo(0, ((union.top + union.bottom - scroll.height) / 2).coerceAtLeast(0))
            }
            instrumentation.waitForIdleSync()
            scenario.onActivity { activity ->
                val scroll = activity.findViewById<android.widget.ScrollView>(R.id.lessonScroll)
                val viewport = android.graphics.Rect()
                fieldsVisible = scroll.getGlobalVisibleRect(viewport) && inputs(activity).all { edit ->
                    val bounds = android.graphics.Rect()
                    edit.getGlobalVisibleRect(bounds) && bounds.width() == edit.width &&
                        bounds.height() == edit.height && viewport.contains(bounds)
                }
            }
            if (fieldsVisible) break
            SystemClock.sleep(50)
        }
        assertTrue("Both real expression fields are fully visible before native capture", fieldsVisible)
        val deadline = SystemClock.uptimeMillis() + 6_000L
        var bitmap: Bitmap? = null
        var previousScreen: Bitmap? = null
        var comparisonViewport: Bitmap? = null
        var stabilityRegion: android.graphics.Rect? = null
        var identical = 0
        var samples = 0
        try {
            while (SystemClock.uptimeMillis() < deadline) {
                val next = requireNotNull(instrumentation.uiAutomation.takeScreenshot())
                val region = android.graphics.Rect()
                scenario.onActivity { activity ->
                    assertTrue(activity.findViewById<android.widget.ScrollView>(R.id.lessonScroll).getGlobalVisibleRect(region))
                }
                assertTrue("The comparison viewport is inside the native screen",
                    android.graphics.Rect(0, 0, next.width, next.height).contains(region))
                assertTrue("The lesson viewport excludes the changing status bar", region.top > 0)
                // Status-bar time/indicators do not describe stability of the lesson content.
                // Only this comparison is cropped; the evidence remains the original screen.
                val nextViewport = Bitmap.createBitmap(next, region.left, region.top, region.width(), region.height())
                samples++
                identical = if (stabilityRegion == region && comparisonViewport?.sameAs(nextViewport) == true)
                    identical + 1 else 0
                previousScreen?.recycle()
                previousScreen = bitmap
                bitmap = next
                comparisonViewport?.recycle()
                comparisonViewport = nextViewport
                stabilityRegion = android.graphics.Rect(region)
                if (identical >= 3) break
                SystemClock.sleep(250)
            }
            if (identical < 3) {
                listOf("last" to bitmap, "previous" to previousScreen).forEach { (label, screen) ->
                    screen?.let { File(evidenceDirectory(), "$name-unstable-$label.png").outputStream().use { output ->
                        assertTrue(it.compress(Bitmap.CompressFormat.PNG, 100, output))
                    } }
                }
                val region = requireNotNull(stabilityRegion)
                File(evidenceDirectory(), "$name-unstable.json").writeText(JSONObject()
                    .put("sampledFrames", samples).put("stableConsecutiveFrames", identical + 1)
                    .put("stabilityRegion", JSONObject().put("view", "lessonScroll")
                        .put("left", region.left).put("top", region.top)
                        .put("right", region.right).put("bottom", region.bottom))
                    .toString(2))
            }
            assertTrue("Capture a settled native screen after rotation", identical >= 3)
            val region = requireNotNull(stabilityRegion)
            val evidence = JSONObject().put("page", fixture.page + 1).put("block", fixture.blockIndex + 1)
                .put("puzzle", fixture.block.getString("id")).put("fingerprint", MathPuzzleState.fingerprint(fixture.block))
                .put("stableConsecutiveFrames", identical + 1).put("sampledFrames", samples)
                .put("samplingIntervalMillis", 250)
                .put("stabilityRegion", JSONObject().put("view", "lessonScroll").put("excludesStatusBar", true)
                    .put("left", region.left).put("top", region.top).put("right", region.right).put("bottom", region.bottom))
            scenario.onActivity { activity ->
                assertTrue("The tested LessonActivity owns focus", activity.hasWindowFocus())
                val scroll = activity.findViewById<android.widget.ScrollView>(R.id.lessonScroll)
                val viewport = android.graphics.Rect()
                assertTrue(scroll.getGlobalVisibleRect(viewport))
                val screen = requireNotNull(bitmap)
                val screenBounds = android.graphics.Rect(0, 0, screen.width, screen.height)
                val fields = JSONArray()
                inputs(activity).forEachIndexed { index, edit ->
                    val bounds = android.graphics.Rect()
                    assertTrue("Answer ${index + 1} is visible in $name", edit.getGlobalVisibleRect(bounds))
                    assertEquals("The complete answer field width is visible", edit.width, bounds.width())
                    assertEquals("The complete answer field height is visible", edit.height, bounds.height())
                    assertTrue("The field is wholly inside the lesson viewport", viewport.contains(bounds))
                    assertTrue("The field is wholly inside the captured native screen", screenBounds.contains(bounds))
                    fields.put(JSONObject().put("id", fixture.responses.getJSONObject(index).getString("id"))
                        .put("expression", edit.text.toString()).put("fullyVisible", true)
                        .put("left", bounds.left).put("top", bounds.top)
                        .put("right", bounds.right).put("bottom", bounds.bottom)
                        .put("width", edit.width).put("height", edit.height))
                }
                evidence.put("answers", saved(activity, fixture).getJSONObject("responses"))
                    .put("orientation", activity.resources.configuration.orientation)
                    .put("fields", fields).put("scrollY", scroll.scrollY)
                    .put("viewport", JSONObject().put("left", viewport.left).put("top", viewport.top)
                        .put("right", viewport.right).put("bottom", viewport.bottom))
            }
            File(evidenceDirectory(), "$name.json").writeText(evidence.toString(2))
            File(evidenceDirectory(), "$name.png").outputStream().use {
                assertTrue(requireNotNull(bitmap).compress(Bitmap.CompressFormat.PNG, 100, it))
            }
        } finally {
            bitmap?.recycle()
            previousScreen?.recycle()
            comparisonViewport?.recycle()
        }
    }

    private fun seedReserve(available: Int) {
        assertTrue(context.getSharedPreferences("attempt_reserve_seconds_test", Context.MODE_PRIVATE).edit()
            .putFloat("reserve_$ROUTE", available.toFloat()).putLong("updated_at_$ROUTE", System.currentTimeMillis())
            .putLong("recovery_interval_$ROUTE", AttemptRepository.RECOVERY_MILLIS).commit())
    }
    private fun setAnswers(activity: LessonActivity, values: List<String>) {
        val inputs = inputs(activity)
        assertEquals(values.size, inputs.size)
        values.forEachIndexed { index, value -> inputs[index].setText(value) }
    }
    private fun reset(activity: LessonActivity) {
        descendants(card(activity)).single { it.contentDescription?.toString() == activity.getString(R.string.math_ui_restart) }.performClick()
    }
    private fun saved(activity: LessonActivity, fixture: Fixture) = requireNotNull(
        ProgressRepository.getExerciseState(activity, fixture.id, fixture.page, fixture.blockIndex))
    private fun inputs(activity: LessonActivity) = descendants(card(activity)).filterIsInstance<EditText>()
    private fun check(activity: LessonActivity) = descendants(card(activity)).filterIsInstance<Button>()
        .single { it.tag == MathPuzzleRenderer.CHECK_ACTION_TAG }
    private fun card(activity: LessonActivity) = descendants(content(activity)).single { it.tag == MathPuzzleRenderer.INTERACTION_TAG }
    private fun content(activity: LessonActivity): ViewGroup = activity.findViewById(R.id.lessonContent)
    private fun ledger(activity: LessonActivity) = field(activity, "exerciseAttempts") as ExerciseAttemptLedger
    private fun descendants(root: View): List<View> = listOf(root) + if (root is ViewGroup)
        (0 until root.childCount).flatMap { descendants(root.getChildAt(it)) } else emptyList()
    private fun strings(array: JSONArray) = (0 until array.length()).map { array.getString(it) }
    private fun preferenceFiles() = File(context.applicationInfo.dataDir, "shared_prefs").listFiles().orEmpty()
        .filter { it.name.endsWith(".xml") }.map { it.name.removeSuffix(".xml") }.toSet()
    private fun evidenceDirectory() = File(requireNotNull(context.getExternalFilesDir(null)), "math-parametric-formulas-qa").apply { mkdirs() }
    private fun field(activity: LessonActivity, name: String): Any? = LessonActivity::class.java.getDeclaredField(name)
        .apply { isAccessible = true }.get(activity)
    private fun setField(activity: LessonActivity, name: String, value: Any) = LessonActivity::class.java.getDeclaredField(name)
        .apply { isAccessible = true }.set(activity, value)
    @Suppress("UNCHECKED_CAST")
    private fun mapField(activity: LessonActivity, name: String) = field(activity, name) as MutableMap<Int, Any>
    private fun invoke(activity: LessonActivity, name: String) = LessonActivity::class.java.getDeclaredMethod(name)
        .apply { isAccessible = true }.invoke(activity)
    private data class Fixture(val id: String, val page: Int, val blockIndex: Int, val block: JSONObject, val lesson: JSONObject) {
        val responses: JSONArray get() = block.getJSONArray("responses")
    }

    companion object {
        private const val QA_PACKAGE = "com.learngraphicsprogramming.app.curricularqa"
        private const val ROUTE = "math_physics"
        private const val PROGRESS = "learning_progress"
        private const val SOURCE_LESSON = "math_algebra_elemental__sistemas_lineales__formulas_con_datos_y_una_condicion_no_nula"
        private const val CONSTRUCTION = "construir_el_criterio_con_datos"
        private const val TRANSFER = "trasladar_datos_sin_perder_la_condicion_no_nula"
        private val CONSTRUCTION_EXPECTED = listOf("a*d-b*c", "d*u-b*v", "a*v-c*u", "(d*u-b*v)/D", "(a*v-c*u)/D")
        private val TRANSFER_EXPECTED = listOf("(d*(u+v)-b*(u-v))/D", "(a*(u-v)-c*(u+v))/D")
        private val CONSTRUCTION_VARIANTS = listOf(CONSTRUCTION_EXPECTED,
            listOf("d*a-c*b", "u*d-v*b", "v*a-u*c", "(b*v-d*u)/(b*c-a*d)", "(c*u-a*v)/(b*c-a*d)"),
            listOf("D", "d*u-b*v", "a*v-c*u", "d*u/D-b*v/D", "a*v/D-c*u/D"),
            listOf("a*d+(-b*c)", "(d*u-b*v)*(D/D)", "(a*v-c*u)*(D/D)", "((d*u-b*v)*D)/(D*D)", "((a*v-c*u)*D)/(D*D)"))
        private val TRANSFER_VARIANTS = listOf(TRANSFER_EXPECTED,
            listOf("(d*u+d*v-b*u+b*v)/D", "(a*u-a*v-c*u-c*v)/D"),
            listOf("(b*(u-v)-d*(u+v))/(b*c-a*d)", "(c*(u+v)-a*(u-v))/(b*c-a*d)"),
            listOf("d*(u+v)/D-b*(u-v)/D", "a*(u-v)/D-c*(u+v)/D"))
        private val LONG_TRANSFER = listOf("((d*(u+v)-b*(u-v))*D)/(D*D)", "((a*(u-v)-c*(u+v))*D)/(D*D)")
        private val ORIGINAL_PUZZLES = listOf("leer_y_calcular_la_condicion_con_datos", "aplicar_las_dos_cancelaciones_a_datos_fijos",
            "distinguir_la_prueba_general_de_las_comprobaciones", "usar_la_receta_cuando_d_es_cero", "usar_la_receta_cuando_b_es_cero",
            "resolver_con_una_diferencia_negativa", "resolver_con_datos_fraccionarios", "justificar_el_homogeneo_sin_borrar_condiciones",
            "revisar_originales_cuando_la_receta_no_se_define")
        private val PREFS = listOf(PROGRESS, "attempt_reserve_seconds_test", "navigation_state", "lesson_narration_settings",
            "math_development_mode", "app_settings", "appearance_settings", "ui_language_settings", "premium_state", "lesson_text_size_settings")
    }
}
