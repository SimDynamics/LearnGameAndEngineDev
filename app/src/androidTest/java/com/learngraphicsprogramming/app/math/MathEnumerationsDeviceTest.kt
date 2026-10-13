package com.learngraphicsprogramming.app.math

import android.app.Activity
import android.app.Application
import android.app.KeyguardManager
import android.content.Context
import android.content.Intent
import android.content.pm.ActivityInfo
import android.content.res.Configuration
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Rect
import android.os.Bundle
import android.os.SystemClock
import android.text.Spanned
import android.text.style.ReplacementSpan
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.Button
import android.widget.RadioButton
import android.widget.ScrollView
import android.widget.TextView
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.AttemptRepository
import com.learngraphicsprogramming.app.BuildConfig
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

/** Native presentation and lifecycle checks for the unchanged published dos_criterios block.
 * Every answer uses a unique QA lesson identity; the normal app is rejected before reading
 * preferences and every QA preference is restored, including the real lesson's launch drafts.
 */
@RunWith(AndroidJUnit4::class)
class MathEnumerationsDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val snapshots = linkedMapOf<String, Map<String, *>>()
    private var originalPreferenceFiles = emptySet<String>()
    private var preferencesCaptured = false
    private var screenGuardRegistered = false
    private val screenGuard = object : Application.ActivityLifecycleCallbacks {
        override fun onActivityCreated(activity: Activity, state: Bundle?) {
            if (activity is LessonActivity && activity.packageName == QA_PACKAGE) {
                activity.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
            }
        }
        override fun onActivityStarted(activity: Activity) = Unit
        override fun onActivityResumed(activity: Activity) = Unit
        override fun onActivityPaused(activity: Activity) = Unit
        override fun onActivityStopped(activity: Activity) = Unit
        override fun onActivitySaveInstanceState(activity: Activity, state: Bundle) = Unit
        override fun onActivityDestroyed(activity: Activity) = Unit
    }

    @Before fun requireQaAndPreserveAllItsPreferences() {
        assertEquals("Use -Plgp.curricularQa=true; never run this class against the normal app",
            QA_PACKAGE, context.packageName)
        assertEquals(QA_PACKAGE, BuildConfig.APPLICATION_ID)
        val keyguard = context.getSystemService(Context.KEYGUARD_SERVICE) as KeyguardManager
        assertFalse("Unlock the test device before presentation verification", keyguard.isDeviceLocked)
        assertFalse("Dismiss the visual lock screen before presentation verification", keyguard.isKeyguardLocked)
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
        // Presentation/answer checks must not exhaust a pre-existing QA reserve.
        PremiumRepository.setVerifiedPremium(context)
        assertTrue(AttemptRepository.getState(context, ROUTE).unlimited)
    }

    @After fun restoreAllQaPreferences() {
        if (context.packageName != QA_PACKAGE || BuildConfig.APPLICATION_ID != QA_PACKAGE || !preferencesCaptured) return
        try {
            instrumentation.waitForIdleSync()
            (preferenceFiles() + snapshots.keys).forEach { name ->
                if (name !in originalPreferenceFiles) {
                    assertTrue("Delete only a preference file created in curricularqa by this test: $name",
                        context.deleteSharedPreferences(name))
                    return@forEach
                }
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
                        else -> error("Unsupported preference value for $name/$key: $value")
                    }
                }
                assertTrue("Restore $name", editor.commit())
                assertEquals("Restore $name including all original progress and answers", original, preferences.all)
            }
            assertEquals("Restore the original QA preference files", originalPreferenceFiles, preferenceFiles())
        } finally {
            if (screenGuardRegistered) {
                (context.applicationContext as Application).unregisterActivityLifecycleCallbacks(screenGuard)
                screenGuardRegistered = false
            }
        }
    }

    @Test fun publishedListsPaintLikeSpacedProseInPortraitAndLandscape() {
        withPublishedFixture { scenario, fixture ->
            for ((orientation, name) in listOf(Configuration.ORIENTATION_PORTRAIT to "portrait",
                Configuration.ORIENTATION_LANDSCAPE to "landscape")) {
                orientAndRestoreFixture(scenario, fixture, orientation)
                scenario.onActivity { activity ->
                    optionLabels(activity).forEachIndexed { index, label ->
                        val expected = LABELS[index]
                        assertEquals(expected, label.text.toString())
                        assertEquals("Enumerations use ordinary text, not a mathematical replacement", 0,
                            replacementSpans(label.text).size)
                        assertEquals("Each short published list fits on one line", 1, label.layout.lineCount)
                        val spaced = referenceLabel(label, expected)
                        val compacted = referenceLabel(label, expected.replace(" ", ""))
                        val actualPixels = draw(label)
                        val spacedPixels = draw(spaced)
                        val compactedPixels = draw(compacted)
                        try {
                            assertEquals("Native measurement retains the prose spacing",
                                spaced.layout.getLineWidth(0), label.layout.getLineWidth(0), .01f)
                            assertTrue("Spaces have visible advance, unlike a compacted algebraic-looking list",
                                label.layout.getLineWidth(0) > compacted.layout.getLineWidth(0) + label.paint.measureText(" "))
                            assertTrue("The actual option pixels match ordinary spaced TextView drawing",
                                actualPixels.sameAs(spacedPixels))
                            assertFalse("The actual option drawing is distinguishable from the unspaced list",
                                actualPixels.sameAs(compactedPixels))
                            saveBitmap(actualPixels, "$name-label-${OPTION_IDS[index]}.png")
                        } finally {
                            actualPixels.recycle(); spacedPixels.recycle(); compactedPixels.recycle()
                        }
                    }
                }
                captureVisibleOptions(scenario, name, orientation)
            }
        }
    }

    @Test fun authenticEquationAndFractionKeepMathematicalSpansAndDrawnFractionBar() {
        withPublishedFixture { scenario, _ ->
            scenario.onActivity { activity ->
                for (formula in listOf("y = 2x", "1/4")) {
                    val view = MathTextView(activity).apply { setMathContent(formula) }
                    assertEquals(formula, view.text.toString())
                    assertEquals("An authentic formula remains typeset in auto mode", 1,
                        replacementSpans(view.text).size)
                }
                assertEquals("Unspaced algebra remains a short whole expression", 1,
                    replacementSpans(MathTextRenderer.render("6y12")).size)
                for (mode in listOf("inline", "block")) {
                    assertEquals("An explicit math mode remains authoritative", 1,
                        replacementSpans(MathTextRenderer.render("6 y 12", mode)).size)
                }
                for (explicit in listOf("\\(6 y 12\\)", "\$6 y 12\$")) {
                    assertEquals("Explicit mathematical delimiters remain authoritative", 1,
                        replacementSpans(MathTextRenderer.render(explicit)).size)
                }
                val mixed = "Compara y = 2x con 1/4."
                val mixedRendered = MathTextRenderer.render(mixed)
                assertEquals(mixed, mixedRendered.toString())
                assertEquals("Authentic formulas inside prose remain separate mathematical spans", 2,
                    replacementSpans(mixedRendered).size)
                assertEquals("Text mode still preserves an explicitly textual formula", 0,
                    replacementSpans(MathTextRenderer.render("y = 2x", "text")).size)
                val rendered = MathTextRenderer.render("1/4") as Spanned
                val span = replacementSpans(rendered).single()
                val paint = Paint(Paint.ANTI_ALIAS_FLAG).apply { textSize = 32f; color = Color.WHITE }
                val ordinaryMetrics = paint.fontMetricsInt
                val fractionMetrics = paint.fontMetricsInt
                val width = span.getSize(paint, rendered, 0, rendered.length, fractionMetrics)
                assertTrue("The fraction stacks its numerator and denominator",
                    fractionMetrics.descent - fractionMetrics.ascent > ordinaryMetrics.descent - ordinaryMetrics.ascent)
                val image = Bitmap.createBitmap(120, 120, Bitmap.Config.ARGB_8888)
                try {
                    span.draw(Canvas(image), rendered, 0, rendered.length, 8f, 0, 70, 120, paint)
                    val barY = (70 - paint.textSize * .29f).toInt()
                    val widestInk = (barY - 1..barY + 2).maxOf { y ->
                        (8 until 8 + width).count { x -> Color.alpha(image.getPixel(x, y)) > 30 }
                    }
                    assertTrue("A horizontal fraction bar is drawn, not merely the slash characters",
                        widestInk >= width * .7f)
                    saveBitmap(image, "formula-quarter-native.png")
                } finally { image.recycle() }
            }
        }
    }

    @Test fun unchangedChoiceRejectsAAndCAcceptsBAndRestoresItsRawSelection() {
        withPublishedFixture { scenario, fixture ->
            for ((option, correct) in listOf("a" to false, "c" to false, "b" to true)) {
                scenario.onActivity { activity ->
                    val index = OPTION_IDS.indexOf(option)
                    val button = optionButtons(activity)[index]
                    assertTrue("The actual option control is attached and editable", button.isAttachedToWindow && button.isEnabled)
                    button.performClick()
                    val check = checkButton(activity)
                    assertTrue("The actual math Check listener is attached and enabled", check.isAttachedToWindow && check.isEnabled)
                    check.performClick()
                    assertSavedSelection(activity, fixture, option, correct)
                }
            }
            scenario.recreate()
            scenario.onActivity { injectFixture(it, fixture) }
            awaitFixtureLayout(scenario, fixture)
            scenario.onActivity { activity -> assertSavedSelection(activity, fixture, "b", true) }
        }
    }

    @Test fun preCorrectionFingerprintAndCorrectStateRemainRecognizedAfterRecreation() {
        withPublishedFixture(preSavedCorrect = true) { scenario, fixture ->
            assertEquals("The published block keeps its pre-correction fingerprint",
                PRE_CORRECTION_FINGERPRINT, MathPuzzleState.fingerprint(fixture.block))
            scenario.onActivity { activity ->
                assertSavedSelection(activity, fixture, "b", true)
                assertEquals(0, optionLabels(activity).sumOf { replacementSpans(it.text).size })
            }
            scenario.recreate()
            scenario.onActivity { injectFixture(it, fixture) }
            awaitFixtureLayout(scenario, fixture)
            scenario.onActivity { activity ->
                assertSavedSelection(activity, fixture, "b", true)
                assertEquals("Existing completion uses the same zero-based coordinates",
                    1, field(activity, "completedExercises"))
            }
        }
    }

    private fun withPublishedFixture(preSavedCorrect: Boolean = false,
                                     test: (ActivityScenario<LessonActivity>, Fixture) -> Unit) {
        val source = LessonRepository.loadLessonJson(context, ROUTE, SOURCE_LESSON)
        val block = JSONObject(source.getJSONArray("pages").getJSONObject(PAGE)
            .getJSONArray("blocks").getJSONObject(BLOCK).toString())
        assertEquals("dos_criterios", block.getString("id"))
        assertEquals(PRE_CORRECTION_FINGERPRINT, MathPuzzleState.fingerprint(block))
        val response = block.getJSONArray("responses").getJSONObject(0)
        assertEquals(RESPONSE_ID, response.getString("id"))
        assertEquals("math_multiple_choice_single", response.getString("type"))
        assertEquals(listOf("b"), strings(response.getJSONObject("validator").getJSONArray("expected")))
        val options = response.getJSONArray("options")
        assertEquals(OPTION_IDS, (0 until options.length()).map { options.getJSONObject(it).getString("id") })
        assertEquals(LABELS, (0 until options.length()).map { options.getJSONObject(it).getString("label") })
        assertTrue((0 until options.length()).none { options.getJSONObject(it).has("content") })
        val fixtureId = "math_course_01__estructura_y_divisibilidad__enumeration_device_${UUID.randomUUID().toString().replace("-", "") }"
        val pages = JSONArray().apply {
            repeat(source.getJSONArray("pages").length()) { pageIndex ->
                put(JSONObject().put("blocks", JSONArray().apply {
                    if (pageIndex == PAGE) {
                        // Preserve the authored block coordinate; other exercises are omitted only
                        // from this in-memory QA lesson, never from the published assets.
                        repeat(BLOCK) { put(JSONObject().put("type", "paragraph").put("text", "")) }
                        put(block)
                    } else put(JSONObject().put("type", "paragraph").put("text", ""))
                }))
            }
        }
        val fixture = Fixture(fixtureId, block,
            JSONObject().put("id", fixtureId).put("title", source.getString("title")).put("pages", pages))
        val before = block.toString()
        if (preSavedCorrect) {
            val legacy = JSONObject().put("schemaVersion", 1).put("contentFingerprint", PRE_CORRECTION_FINGERPRINT)
                .put("correct", true).put("responses", JSONObject().put(RESPONSE_ID,
                    MathAnswerJson.encode(MathAnswer.Choice(setOf("b"))))).put("tools", JSONObject())
            assertTrue("A previously saved correct answer belongs to the unchanged published block",
                MathPuzzleState.isCurrentCorrect(block, legacy))
            ProgressRepository.saveExerciseState(context, ROUTE, fixtureId, PAGE, BLOCK, legacy)
        }
        // Use the real mathematical route initialization. Its ordinary launch preferences/drafts
        // are included in the complete @Before/@After snapshot; all interactions below use fixtureId.
        ProgressRepository.saveCurrentPage(context, ROUTE, SOURCE_LESSON, PAGE)
        val intent = Intent(context, LessonActivity::class.java).putExtra("routeId", ROUTE)
            .putExtra("courseId", "math_course_01").putExtra("unitId", "estructura_y_divisibilidad")
            .putExtra("lessonId", SOURCE_LESSON).putExtra("lessonNumber", 1).putExtra("lessonTotal", 1)
        ActivityScenario.launch<LessonActivity>(intent).use { scenario ->
            scenario.onActivity { injectFixture(it, fixture) }
            awaitFixtureLayout(scenario, fixture)
            test(scenario, fixture)
        }
        assertEquals("Rendering and interacting do not modify the copied published block", before, block.toString())
        val publishedAgain = LessonRepository.loadLessonJson(context, ROUTE, SOURCE_LESSON)
            .getJSONArray("pages").getJSONObject(PAGE).getJSONArray("blocks").getJSONObject(BLOCK)
        assertEquals("No content or state-contract migration is required", before, publishedAgain.toString())
    }

    private fun injectFixture(activity: LessonActivity, fixture: Fixture) {
        assertEquals(QA_PACKAGE, activity.packageName)
        assertEquals(ROUTE, field(activity, "currentRouteId"))
        setField(activity, "currentLessonId", fixture.id)
        setField(activity, "lessonJson", JSONObject(fixture.lesson.toString()))
        setField(activity, "totalPages", fixture.lesson.getJSONArray("pages").length())
        setField(activity, "currentPage", PAGE)
        listOf("pageViewCache", "pageRequiredExercises", "pageCompletedExercises").forEach {
            mapField(activity, it).clear()
        }
        invoke(activity, "renderCurrentPage")
        assertEquals(1, field(activity, "requiredExercises"))
        assertEquals(3, optionButtons(activity).size)
        assertEquals(LABELS, optionLabels(activity).map { it.text.toString() })
    }

    private fun awaitFixtureLayout(scenario: ActivityScenario<LessonActivity>, fixture: Fixture,
                                   orientation: Int? = null) {
        val deadline = SystemClock.uptimeMillis() + 5_000L
        while (SystemClock.uptimeMillis() < deadline) {
            instrumentation.waitForIdleSync()
            var ready = false
            scenario.onActivity { activity ->
                if (orientation == null || activity.resources.configuration.orientation == orientation) {
                    if (field(activity, "currentLessonId") != fixture.id) injectFixture(activity, fixture)
                    ready = optionLabels(activity).all { it.width > 0 && it.height > 0 && it.layout != null }
                }
            }
            if (ready) return
            SystemClock.sleep(50)
        }
        fail("The native mathematical fixture did not finish layout in the requested orientation")
    }

    private fun orientAndRestoreFixture(scenario: ActivityScenario<LessonActivity>, fixture: Fixture, orientation: Int) {
        scenario.onActivity { activity ->
            activity.requestedOrientation = if (orientation == Configuration.ORIENTATION_PORTRAIT)
                ActivityInfo.SCREEN_ORIENTATION_PORTRAIT else ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
        }
        awaitFixtureLayout(scenario, fixture, orientation)
    }

    private fun captureVisibleOptions(scenario: ActivityScenario<LessonActivity>, name: String, orientation: Int) {
        scenario.onActivity { activity ->
            val scroll = activity.findViewById<ScrollView>(R.id.lessonScroll)
            val first = optionLabels(activity).first()
            val bounds = Rect()
            first.getDrawingRect(bounds)
            scroll.offsetDescendantRectToMyCoords(first, bounds)
            scroll.scrollTo(0, (bounds.top - 8 * activity.resources.displayMetrics.density).toInt().coerceAtLeast(0))
        }
        instrumentation.waitForIdleSync()
        // Idle application layout can precede the system's rotation animation. Require four
        // identical native screen frames, sampled 250 ms apart, before saving visual evidence.
        val stabilityDeadline = SystemClock.uptimeMillis() + 5_000L
        var stableComparisons = 0
        var sampledFrames = 0
        var screen: Bitmap? = null
        try {
            while (SystemClock.uptimeMillis() < stabilityDeadline) {
                val next = requireNotNull(instrumentation.uiAutomation.takeScreenshot())
                sampledFrames++
                val previous = screen
                stableComparisons = if (previous != null && previous.sameAs(next)) stableComparisons + 1 else 0
                previous?.recycle()
                screen = next
                if (stableComparisons >= 3) break
                SystemClock.sleep(250)
            }
            assertTrue("Wait for the native $name rotation/scroll animation to settle before capture",
                stableComparisons >= 3)
            val visibility = JSONObject().put("orientation", name).put("page", PAGE + 1).put("block", BLOCK + 1)
                .put("fingerprint", PRE_CORRECTION_FINGERPRINT)
                .put("stableConsecutiveFrames", stableComparisons + 1).put("sampledFrames", sampledFrames)
                .put("samplingIntervalMillis", 250)
            scenario.onActivity { activity ->
                assertTrue("LessonActivity must own focus for the native screenshot", activity.hasWindowFocus())
                assertEquals(orientation, activity.resources.configuration.orientation)
                val labels = JSONArray()
                optionLabels(activity).forEachIndexed { index, label ->
                    val bounds = Rect()
                    assertTrue("Every published option is visible in $name", label.getGlobalVisibleRect(bounds))
                    assertEquals("The entire option label is visible in $name", label.height, bounds.height())
                    assertEquals(label.width, bounds.width())
                    labels.put(JSONObject().put("id", OPTION_IDS[index]).put("label", label.text.toString())
                        .put("replacementSpans", replacementSpans(label.text).size)
                        .put("left", bounds.left).put("top", bounds.top).put("right", bounds.right).put("bottom", bounds.bottom))
                }
                visibility.put("labels", labels)
            }
            val settledScreen = requireNotNull(screen)
            assertEquals(orientation == Configuration.ORIENTATION_PORTRAIT, settledScreen.height > settledScreen.width)
            saveBitmap(settledScreen, "$name-options.png")
            File(evidenceDirectory(), "$name-visibility.json").writeText(visibility.toString(2))
        } finally { screen?.recycle() }
    }

    private fun referenceLabel(actual: TextView, source: String): TextView = TextView(actual.context).apply {
        text = source
        paint.set(actual.paint)
        setTextColor(actual.currentTextColor)
        gravity = actual.gravity
        includeFontPadding = actual.includeFontPadding
        textDirection = actual.textDirection
        layoutDirection = actual.layoutDirection
        breakStrategy = actual.breakStrategy
        hyphenationFrequency = actual.hyphenationFrequency
        ellipsize = actual.ellipsize
        maxLines = actual.maxLines
        setLineSpacing(actual.lineSpacingExtra, actual.lineSpacingMultiplier)
        setPadding(actual.paddingLeft, actual.paddingTop, actual.paddingRight, actual.paddingBottom)
        measure(View.MeasureSpec.makeMeasureSpec(actual.width, View.MeasureSpec.EXACTLY),
            View.MeasureSpec.makeMeasureSpec(actual.height, View.MeasureSpec.EXACTLY))
        layout(0, 0, actual.width, actual.height)
    }

    private fun assertSavedSelection(activity: LessonActivity, fixture: Fixture, option: String, correct: Boolean) {
        val saved = requireNotNull(ProgressRepository.getExerciseState(activity, fixture.id, PAGE, BLOCK))
        assertEquals(PRE_CORRECTION_FINGERPRINT, saved.getString("contentFingerprint"))
        assertEquals(correct, saved.getBoolean("correct"))
        assertEquals(MathAnswer.Choice(setOf(option)), MathAnswerJson.decode(saved.getJSONObject("responses").getJSONObject(RESPONSE_ID)))
        assertEquals(correct, MathPuzzleState.isCurrentCorrect(fixture.block, saved))
        assertEquals(if (correct) 1 else 0, field(activity, "completedExercises"))
        assertEquals(listOf(option), optionButtons(activity).filter { it.isChecked }.map { OPTION_IDS[optionButtons(activity).indexOf(it)] })
        assertTrue(optionButtons(activity).all { it.isEnabled == !correct })
        assertEquals(!correct, checkButton(activity).isEnabled)
        assertTrue("The state retains the original zero-based page/block coordinates",
            context.getSharedPreferences(PROGRESS, Context.MODE_PRIVATE)
                .contains("exercise_${fixture.id}_page_${PAGE}_block_${BLOCK}"))
    }

    private fun optionButtons(activity: LessonActivity) = descendants(content(activity)).filterIsInstance<RadioButton>()
    private fun optionLabels(activity: LessonActivity): List<TextView> = LABELS.map { expected ->
        descendants(content(activity)).filterIsInstance<TextView>().single { it !is RadioButton && it.text.toString() == expected }
    }
    private fun checkButton(activity: LessonActivity) = descendants(content(activity)).filterIsInstance<Button>()
        .single { it.tag == MathPuzzleRenderer.CHECK_ACTION_TAG }
    private fun content(activity: LessonActivity): ViewGroup = activity.findViewById(R.id.lessonContent)
    private fun descendants(root: View): List<View> = listOf(root) + if (root is ViewGroup)
        (0 until root.childCount).flatMap { descendants(root.getChildAt(it)) } else emptyList()
    private fun replacementSpans(text: CharSequence) = if (text is Spanned)
        text.getSpans(0, text.length, ReplacementSpan::class.java).toList() else emptyList()
    private fun strings(array: JSONArray) = (0 until array.length()).map { array.getString(it) }
    private fun draw(view: View): Bitmap = Bitmap.createBitmap(view.width, view.height, Bitmap.Config.ARGB_8888).apply {
        view.draw(Canvas(this))
    }
    private fun evidenceDirectory() = File(requireNotNull(context.getExternalFilesDir(null)), "math-enumerations-qa").apply { mkdirs() }
    private fun saveBitmap(bitmap: Bitmap, name: String) {
        val target = File(evidenceDirectory(), name)
        target.outputStream().use { assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) }
        android.util.Log.i("MathEnumerationsQa", "Native evidence: ${target.absolutePath}")
    }
    private fun preferenceFiles() = File(context.applicationInfo.dataDir, "shared_prefs").listFiles().orEmpty()
        .filter { it.name.endsWith(".xml") }.map { it.name.removeSuffix(".xml") }.toSet()
    private fun field(activity: LessonActivity, name: String): Any? = LessonActivity::class.java.getDeclaredField(name)
        .apply { isAccessible = true }.get(activity)
    private fun setField(activity: LessonActivity, name: String, value: Any) = LessonActivity::class.java.getDeclaredField(name)
        .apply { isAccessible = true }.set(activity, value)
    @Suppress("UNCHECKED_CAST")
    private fun mapField(activity: LessonActivity, name: String) = field(activity, name) as MutableMap<Int, Any>
    private fun invoke(activity: LessonActivity, name: String) = LessonActivity::class.java.getDeclaredMethod(name)
        .apply { isAccessible = true }.invoke(activity)

    private data class Fixture(val id: String, val block: JSONObject, val lesson: JSONObject)

    companion object {
        private const val QA_PACKAGE = "com.learngraphicsprogramming.app.curricularqa"
        private const val ROUTE = "math_physics"
        private const val PROGRESS = "learning_progress"
        private const val SOURCE_LESSON = "math_course_01__estructura_y_divisibilidad__divisibilidad_y_primos"
        private const val PAGE = 5
        private const val BLOCK = 3
        private const val RESPONSE_ID = "dos_criterios__eleccion"
        private const val PRE_CORRECTION_FINGERPRINT = "f4c3637d2efd2dba068e855a555a08b40dd8b25834d2aa43a152b5063f464168"
        private val OPTION_IDS = listOf("a", "b", "c")
        private val LABELS = listOf("3, 6, 9 y 12", "6 y 12", "2, 4, 8 y 10")
        private val PREFS = listOf(PROGRESS, "attempt_reserve_seconds_test", "navigation_state", "lesson_narration_settings",
            "math_development_mode", "app_settings", "appearance_settings", "ui_language_settings", "premium_state",
            "lesson_text_size_settings")
    }
}
