package com.learngraphicsprogramming.app.math

import android.content.res.Configuration
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Rect
import android.os.Build
import android.os.SystemClock
import android.view.View
import android.view.ViewGroup
import android.widget.CheckBox
import android.widget.EditText
import android.widget.ImageButton
import android.widget.ScrollView
import android.widget.Spinner
import android.widget.TextView
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.learngraphicsprogramming.app.LessonActivity
import com.learngraphicsprogramming.app.LessonTextSizeSettingsRepository
import com.learngraphicsprogramming.app.R
import com.learngraphicsprogramming.app.narration.NarrationSettingsRepository
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import kotlin.math.abs
import kotlin.math.ceil
import kotlin.math.hypot
import kotlin.math.max
import kotlin.math.min

/** Exact current curricular pages are copied into the QA application's memory. No content,
 * answer, student preference, model, audio, or physical-device configuration is changed. */
@RunWith(AndroidJUnit4::class)
class MathCurricularPresentationDeviceTest : MathGeometryQaSupport("com.learngraphicsprogramming.app.realdownloadsqa") {
    @Before fun requireLdPlayerAndVisibleOriginalReaderBar() {
        assertTrue("Run only on the explicitly selected LDPlayer, never Work's Honor",
            Build.SUPPORTED_ABIS.any { it == "x86" || it == "x86_64" })
        // This is the existing floating reader's default placement. Disabled narration keeps
        // this presentation-only test from synthesizing or playing anything.
        NarrationSettingsRepository.saveControlsVisible(context, true)
        NarrationSettingsRepository.savePosition(context, 1f, .85f)
    }

    @Test fun actualPageEightFullCorrectExplanationRemainsReadableAboveFloatingReaderAtFiniteLegalEnd() {
        verifyFeedback("circumference-p8-correct", circularPage(8, 3), mapOf(
            "congruentes" to "si", "misma_parte" to "no", "diametro_u" to "26/8"), true)
    }

    @Test fun actualIncorrectAndCorrectGuidesAndPreviousSegmentRegressionRemainReadableAtBothProfiles() {
        verifyFeedback("circumference-p4-incorrect", circularPage(4, 4), choices = setOf("a"), correct = false)
        verifyFeedback("circumference-p5-correct", circularPage(5, 4), mapOf("op" to "radio",
            "ab" to "diametro", "pb" to "cuerda", "recta_s" to "secante", "diametro_u" to "11/5"), true)
        verifyFeedback("circumference-p6-correct", circularPage(6, 3), mapOf("x_c" to "interior",
            "x_d" to "exterior", "y_d" to "frontera", "o_d" to "interior"), true)
        // The historical page contains two puzzles, while the shared isolation helper
        // intentionally hosts one. This regression uses the unchanged affected card.
        verifyFeedback("segments-p5-correct-card", copiedPage(SEGMENTS, 5, 5, preserveWholePage = false), mapOf("unidad_w" to "0,7"), true)
    }

    @Test fun actualConcentricCircleNamesStayLocallyAssociatedInBothProfilesAndExpandedNativePanel() {
        val fixture = circularPage(6, 3)
        withFixture(fixture) { scenario ->
            profiles(scenario, fixture) { profile ->
                lateinit var visual: MathEuclideanDiagramView
                lateinit var inlineParent: ViewGroup
                lateinit var root: View
                scenario.onActivity { activity ->
                    root = activity.findViewById(android.R.id.content)
                    visual = descendants(content(activity)).filterIsInstance<MathEuclideanDiagramView>().single()
                    inlineParent = visual.parent as ViewGroup
                    scrollVisible(activity, visual.parent.parent as View)
                }
                settle(scenario)
                lateinit var metadata: JSONObject
                scenario.onActivity { metadata = curveMetadata(visual).put("profile", profile).put("expanded", false) }
                capture("work-concentric-$profile-inline", { root }, metadata)
                instrumentation.runOnMainSync { assertCurveAssociation(visual, metadata) }
                captureInlineCircleCoverage(scenario, visual, root, profile, metadata)
                scenario.onActivity {
                    descendants(visual.parent.parent as View).filterIsInstance<ImageButton>()
                        .single { it.tag == "math_expand_representation" }.performClick()
                }
                awaitExpanded(visual, inlineParent, true)
                instrumentation.runOnMainSync { metadata = curveMetadata(visual).put("profile", profile).put("expanded", true) }
                capture("work-concentric-$profile-expanded", { visual }, metadata)
                instrumentation.runOnMainSync {
                    assertCurveAssociation(visual, metadata)
                    descendants(visual.rootView).filterIsInstance<ImageButton>()
                        .single { it.tag == "math_exit_representation" }.performClick()
                }
                awaitExpanded(visual, inlineParent, false)
            }
        }
    }

    private fun captureInlineCircleCoverage(scenario: ActivityScenario<LessonActivity>, visual: MathEuclideanDiagramView,
                                            root: View, profile: String, initialMetadata: JSONObject) {
        val coveredLabels = mutableSetOf<String>()
        val intervals = mutableListOf<Pair<Double, Double>>()
        val frameNames = JSONArray()
        var circleTop = 0.0
        var circleBottom = 0.0
        lateinit var scroll: ScrollView
        lateinit var controls: View
        scenario.onActivity { activity ->
            scroll = activity.findViewById(R.id.lessonScroll)
            controls = activity.findViewById(R.id.lessonNarrationControls)
            val drawing = visual.drawingSnapshot()
            val strokeMargin = 2 * visual.resources.displayMetrics.density.toDouble()
            circleTop = drawing.circles.minOf { it.center.y - it.radius - strokeMargin }
            circleBottom = drawing.circles.maxOf { it.center.y + it.radius + strokeMargin }
        }
        fun record(name: String, metadata: JSONObject, captureImage: Boolean) {
            val labelBounds = linkedMapOf<String, Rect>()
            val labelMetadata = JSONArray()
            scenario.onActivity {
                val origin = onScreen(visual)
                val viewport = onScreen(scroll).apply { top += scroll.paddingTop; bottom -= scroll.paddingBottom }
                val bar = onScreen(controls)
                val safeBottom = min(viewport.bottom, bar.top)
                val visibleTop = max(circleTop, (viewport.top - origin.top).toDouble())
                val visibleBottom = min(circleBottom, (safeBottom - origin.top).toDouble())
                if (visibleTop <= visibleBottom) intervals += visibleTop to visibleBottom
                metadata.put("viewDisplayBounds", rect(origin)).put("viewport", rect(viewport)).put("controls", rect(bar))
                    .put("scrollY", scroll.scrollY).put("finiteLegalMax", legalEnd(scroll))
                    .put("circleInkLocalY", JSONArray(listOf(circleTop, circleBottom)))
                    .put("unobstructedCircleLocalY", JSONArray(listOf(visibleTop, visibleBottom)))
                visual.drawingSnapshot().labels.filter { it.ownerId in setOf("c", "d") }.forEach { label ->
                    val bounds = Rect(origin.left + kotlin.math.floor(label.bounds.left).toInt(),
                        origin.top + kotlin.math.floor(label.bounds.top).toInt(),
                        origin.left + ceil(label.bounds.right).toInt(), origin.top + ceil(label.bounds.bottom).toInt())
                    val visible = viewport.contains(bounds) && !Rect.intersects(bounds, bar)
                    labelMetadata.put(JSONObject().put("ownerId", label.ownerId).put("displayBounds", rect(bounds))
                        .put("completelyVisibleWithoutReader", visible))
                    if (visible) labelBounds[label.ownerId] = bounds
                }
                metadata.put("nativeCircleLabels", labelMetadata)
                assertTrue("Native inline diagram keeps a finite legal scroll offset", scroll.scrollY in 0..legalEnd(scroll))
            }
            if (captureImage) capture(name, { root }, metadata)
            val bitmap = requireNotNull(instrumentation.uiAutomation.takeScreenshot())
            try {
                for (index in 0 until labelMetadata.length()) {
                    val evidence = labelMetadata.getJSONObject(index)
                    val bounds = labelBounds[evidence.getString("ownerId")] ?: continue
                    var glyphs = 0
                    for (y in max(0, bounds.top) until min(bitmap.height, bounds.bottom))
                        for (x in max(0, bounds.left) until min(bitmap.width, bounds.right)) {
                            val pixel = bitmap.getPixel(x, y)
                            if (Color.red(pixel) > 210 && Color.green(pixel) > 210 && Color.blue(pixel) > 210) glyphs++
                        }
                    evidence.put("nativeWhiteGlyphPixels", glyphs)
                    assertTrue("Actual native screenshot paints complete circle label ${evidence.getString("ownerId")}", glyphs > 8)
                }
            } finally { bitmap.recycle() }
            coveredLabels += labelBounds.keys
            frameNames.put("$name.png")
            // The initial full screenshot remains the original diagnostic framing; only
            // add its measured viewport/scroll/glyph evidence rather than disguising it.
            saveMetadata(name, metadata)
        }
        fun completeCurveHeight(): Boolean {
            var bottom = circleTop
            for ((from, to) in intervals.sortedBy { it.first }) {
                if (from > bottom + 1) return false
                bottom = max(bottom, to)
            }
            return bottom >= circleBottom - 1
        }
        record("work-concentric-$profile-inline", initialMetadata, false)
        if (coveredLabels != setOf("c", "d") || !completeCurveHeight()) {
            for (edge in listOf("top", "bottom")) {
                scenario.onActivity {
                    val viewport = onScreen(scroll).apply { top += scroll.paddingTop; bottom -= scroll.paddingBottom }
                    val margin = 4 * visual.resources.displayMetrics.density
                    val origin = onScreen(visual)
                    val desired = if (edge == "top") scroll.scrollY + origin.top + circleTop - viewport.top - margin
                        else scroll.scrollY + origin.top + circleBottom - min(viewport.bottom, onScreen(controls).top) + margin
                    scroll.scrollTo(0, desired.toInt().coerceIn(0, legalEnd(scroll)))
                }
                settle(scenario)
                lateinit var metadata: JSONObject
                scenario.onActivity { metadata = curveMetadata(visual).put("profile", profile).put("expanded", false).put("circleEdge", edge) }
                record("work-concentric-$profile-inline-circle-$edge", metadata, true)
            }
        }
        assertEquals("Every inline circle label is fully readable without the floating controls in at least one native frame",
            setOf("c", "d"), coveredLabels)
        assertTrue("The entire vertical ink extent of both circles is covered by legal frames above the reader: $intervals",
            completeCurveHeight())
        saveMetadata("work-concentric-$profile-inline-coverage", JSONObject().put("frames", frameNames)
            .put("fullyVisibleLabelIds", JSONArray(coveredLabels.sorted())).put("circleInkLocalY", JSONArray(listOf(circleTop, circleBottom)))
            .put("unobstructedLocalYIntervals", JSONArray().apply { intervals.forEach { put(JSONArray(listOf(it.first, it.second))) } })
            .put("entireCircleHeightVisibleInLegalFrames", true))
    }

    private fun circularPage(page: Int, block: Int) = copiedPage(CIRCUMFERENCE, page, block)

    private fun copiedPage(asset: String, page: Int, block: Int, preserveWholePage: Boolean = true): Fixture {
        val source = context.assets.open(asset).bufferedReader(Charsets.UTF_8).use { JSONObject(it.readText()) }
        val originalPage = source.getJSONArray("pages").getJSONObject(page - 1)
        val original = originalPage.getJSONArray("blocks").getJSONObject(block - 1)
        assertEquals("math_puzzle", original.getString("type"))
        val base = fixture(original, page - 1, block - 1)
        // Keep all real pages as well: page eight must remain eight of nine, with the
        // ordinary Continue control, rather than becoming a truncated final page.
        if (preserveWholePage) base.lesson.put("pages", JSONArray(source.getJSONArray("pages").toString()))
        assertEquals(original.toString(), base.lesson.getJSONArray("pages").getJSONObject(page - 1)
            .getJSONArray("blocks").getJSONObject(block - 1).toString())
        return base
    }

    private fun verifyFeedback(name: String, fixture: Fixture, answers: Map<String, String> = emptyMap(),
                               correct: Boolean, choices: Set<String> = emptySet()) {
        withFixture(fixture) { scenario ->
            var savedResponses = ""
            scenario.onActivity { activity ->
                val response = fixture.block.getJSONArray("responses").getJSONObject(0)
                if (choices.isNotEmpty()) {
                    val options = response.getJSONArray("options")
                    for (index in 0 until options.length()) {
                        val option = options.getJSONObject(index)
                        if (option.getString("id") in choices) descendants(content(activity)).filterIsInstance<CheckBox>()
                            .single { it.contentDescription.toString() == option.getString("label") }.performClick()
                    }
                } else {
                    val fields = response.getJSONArray("fields")
                    for (index in 0 until fields.length()) {
                        val field = fields.getJSONObject(index)
                        val value = answers[field.getString("id")] ?: continue
                        val label = field.getString("label")
                        when (field.optString("type")) {
                            "dropdown" -> {
                                val options = field.getJSONArray("options")
                                val position = (0 until options.length()).single { options.getJSONObject(it).getString("id") == value } + 1
                                descendants(content(activity)).filterIsInstance<Spinner>()
                                    .single { it.contentDescription.toString() == label }.setSelection(position)
                            }
                            else -> descendants(content(activity)).filterIsInstance<EditText>()
                                .single { it.contentDescription.toString() == label }.setText(value)
                        }
                    }
                }
            }
            settle(scenario)
            scenario.onActivity { activity ->
                check(activity).performClick()
                assertEquals("The real unchanged validator accepts/rejects the same answer", correct,
                    state(activity, fixture).getBoolean("correct"))
                savedResponses = state(activity, fixture).getJSONObject("responses").toString()
                val host = check(activity).parent as ViewGroup
                val feedback = host.getChildAt(host.childCount - 1) as TextView
                assertTrue("The real complete explanation is present", feedback.text.isNotBlank())
            }
            profiles(scenario, fixture) { profile ->
                // Recreation restores the accepted or incorrect explanation from the existing
                // saved state; incorrect guides intentionally need a new explicit Comprobar.
                scenario.onActivity { activity -> if (!correct) check(activity).performClick() }
                settle(scenario)
                scenario.onActivity { activity -> assertEquals("Rotation/text scaling preserves the exact submitted response, including equivalent fractions",
                    savedResponses, state(activity, fixture).getJSONObject("responses").toString()) }
                captureFeedbackEnds(scenario, fixture, "$name-$profile", correct)
            }
        }
    }

    private fun profiles(scenario: ActivityScenario<LessonActivity>, fixture: Fixture, body: (String) -> Unit) {
        for ((orientation, scale, profile) in listOf(Triple(Configuration.ORIENTATION_PORTRAIT, 100, "portrait-100"),
            Triple(Configuration.ORIENTATION_LANDSCAPE, 150, "landscape-150"))) {
            LessonTextSizeSettingsRepository.saveGlobalScale(context, scale)
            orient(scenario, fixture, orientation)
            // Apply the actual lesson text-size path even when the first requested
            // orientation was already active and did not trigger an Android recreation.
            scenario.onActivity { inject(it, fixture) }
            awaitLayout(scenario, fixture, orientation)
            settle(scenario)
            body(profile)
        }
    }

    private fun captureFeedbackEnds(scenario: ActivityScenario<LessonActivity>, fixture: Fixture, name: String, correct: Boolean) {
        lateinit var root: View
        lateinit var scroll: ScrollView
        lateinit var feedback: TextView
        lateinit var controls: View
        scenario.onActivity { activity ->
            root = activity.findViewById(android.R.id.content)
            scroll = activity.findViewById(R.id.lessonScroll)
            controls = activity.findViewById(R.id.lessonNarrationControls)
            val host = check(activity).parent as ViewGroup
            feedback = host.getChildAt(host.childCount - 1) as TextView
            assertEquals(correct, state(activity, fixture).getBoolean("correct"))
            assertTrue("The actual floating controls remain available", controls.isShown)
            val feedbackY = onScreen(feedback).top - onScreen(scroll).top + scroll.scrollY
            scroll.scrollTo(0, (feedbackY - scroll.paddingTop).coerceIn(0, legalEnd(scroll)))
        }
        settle(scenario)
        var first = JSONObject()
        scenario.onActivity { first = feedbackMetadata(scroll, feedback, controls).put("edge", "first") }
        capture("work-$name-first", { root }, first)
        val coveredLines = assertVisibleGlyphLine(feedback, scroll, controls, 0, first, "$name-first").toMutableSet()
        val coverageFrames = JSONArray().put("work-$name-first.png")
        scenario.onActivity { scroll.scrollTo(0, legalEnd(scroll)) }
        settle(scenario)
        var maximum = JSONObject()
        scenario.onActivity {
            maximum = feedbackMetadata(scroll, feedback, controls).put("edge", "finite-legal-end")
            assertFalse("Reached the actual finite legal end; never overscroll with Int.MAX_VALUE", scroll.canScrollVertically(1))
            assertEquals(legalEnd(scroll), scroll.scrollY)
        }
        capture("work-$name-finite-legal-end", { root }, maximum)
        scenario.onActivity {
            // Continue and reserved trailing space are also part of the real page. At its
            // legal end a tall preceding guide may already have scrolled past the top.
            // Measure an earlier legal frame for its final line instead of requiring that
            // every preceding item stay visible at the end of the entire page.
            val viewport = onScreen(scroll).apply { top += scroll.paddingTop; bottom -= scroll.paddingBottom }
            val margin = (4 * scroll.resources.displayMetrics.density).toInt()
            val bottomAboveControls = min(viewport.bottom, onScreen(controls).top) - margin
            val measuredOffset = scroll.scrollY + onScreen(feedback).bottom - bottomAboveControls
            scroll.scrollTo(0, measuredOffset.coerceIn(0, legalEnd(scroll)))
        }
        settle(scenario)
        var last = JSONObject()
        scenario.onActivity { last = feedbackMetadata(scroll, feedback, controls).put("edge", "last") }
        capture("work-$name-last", { root }, last)
        coveredLines += assertVisibleGlyphLine(feedback, scroll, controls, feedback.lineCount - 1, last, "$name-last")
        coverageFrames.put("work-$name-last.png")
        scenario.onActivity {
            assertTrue("The reading frame is a measured finite legal offset", scroll.scrollY in 0..legalEnd(scroll))
            val viewport = onScreen(scroll).apply { top += scroll.paddingTop; bottom -= scroll.paddingBottom }
            val bar = onScreen(controls)
            val margin = (4 * scroll.resources.displayMetrics.density).toInt()
            val unobstructedHeight = min(viewport.bottom, bar.top) - margin - viewport.top
            if (feedback.height <= unobstructedHeight) {
                val bounds = onScreen(feedback)
                assertTrue("A guide that fits must be readable in one native frame, above the controls: feedback=$bounds bar=$bar viewport=$viewport",
                    viewport.contains(bounds) && !Rect.intersects(bounds, bar))
            }
        }
        var middleFrame = 0
        while (coveredLines.size < feedback.lineCount) {
            val firstMissing = (0 until feedback.lineCount).first { it !in coveredLines }
            scenario.onActivity {
                val viewportTop = onScreen(scroll).top + scroll.paddingTop
                val measuredOffset = scroll.scrollY + glyphLineBounds(feedback, firstMissing).top - viewportTop
                scroll.scrollTo(0, measuredOffset.coerceIn(0, legalEnd(scroll)))
            }
            settle(scenario)
            middleFrame++
            val frameName = "$name-middle-$middleFrame"
            var middle = JSONObject()
            scenario.onActivity { middle = feedbackMetadata(scroll, feedback, controls).put("edge", "middle").put("targetLine", firstMissing) }
            capture("work-$frameName", { root }, middle)
            val newlyVisible = assertVisibleGlyphLine(feedback, scroll, controls, firstMissing, middle, frameName)
            assertTrue("Each additional legal frame must expose previously unreadable guide lines", newlyVisible.any { it !in coveredLines })
            coveredLines += newlyVisible
            coverageFrames.put("work-$frameName.png")
            assertTrue("Coverage requires no more frames than native lines", middleFrame <= feedback.lineCount)
        }
        assertEquals("Every native explanation line has a readable, unobstructed screenshot frame",
            (0 until feedback.lineCount).toSet(), coveredLines)
        saveMetadata("work-$name-line-coverage", JSONObject().put("nativeLineCount", feedback.lineCount)
            .put("coveredLineIndices", JSONArray(coveredLines.sorted())).put("frames", coverageFrames)
            .put("allLinesVisibleInLegalFrames", true))
    }

    private fun feedbackMetadata(scroll: ScrollView, feedback: TextView, controls: View) = JSONObject()
        .put("scrollY", scroll.scrollY).put("finiteLegalMax", legalEnd(scroll)).put("canScrollFurther", scroll.canScrollVertically(1))
        .put("scroll", rect(onScreen(scroll))).put("padding", JSONArray(listOf(scroll.paddingLeft, scroll.paddingTop,
            scroll.paddingRight, scroll.paddingBottom))).put("childHeight", scroll.getChildAt(0).height)
        .put("feedback", rect(onScreen(feedback))).put("controls", rect(onScreen(controls)))
        .put("feedbackLineCount", feedback.lineCount).put("completeVisibleFeedback", feedback.text.toString())
        .put("source", "Actual current asset copied in memory; original lesson UI, validator, floating controls, and legal ScrollView bounds")

    private fun assertVisibleGlyphLine(text: TextView, scroll: ScrollView, controls: View, line: Int, metadata: JSONObject, name: String): Set<Int> {
        lateinit var glyphBounds: Rect
        lateinit var viewport: Rect
        lateinit var bar: Rect
        val visibleLines = linkedMapOf<Int, Pair<Rect, Boolean>>()
        instrumentation.runOnMainSync {
            val layout = requireNotNull(text.layout)
            glyphBounds = glyphLineBounds(text, line)
            viewport = onScreen(scroll).apply { top += scroll.paddingTop; bottom -= scroll.paddingBottom }
            bar = onScreen(controls)
            for (index in 0 until text.lineCount) {
                val bounds = glyphLineBounds(text, index)
                if (viewport.contains(bounds) && !Rect.intersects(bounds, bar)) visibleLines[index] = bounds to
                    text.text.subSequence(layout.getLineStart(index), layout.getLineEnd(index)).any { !it.isWhitespace() }
            }
        }
        metadata.put("glyphLine", line).put("glyphBounds", rect(glyphBounds))
        val image = requireNotNull(instrumentation.uiAutomation.takeScreenshot())
        try {
            fun brightGlyphs(bounds: Rect): Int {
                var bright = 0
                for (y in max(0, bounds.top) until min(image.height, bounds.bottom))
                    for (x in max(0, bounds.left) until min(image.width, bounds.right)) {
                    val c = image.getPixel(x, y)
                    if (max(Color.red(c), max(Color.green(c), Color.blue(c))) > 135) bright++
                }
                return bright
            }
            val bright = brightGlyphs(glyphBounds)
            val visibleEvidence = JSONArray()
            visibleLines.forEach { (index, boundsAndInk) -> visibleEvidence.put(JSONObject().put("line", index)
                .put("bounds", rect(boundsAndInk.first)).put("expectsGlyphs", boundsAndInk.second)
                .put("nativeBrightGlyphPixels", brightGlyphs(boundsAndInk.first))) }
            metadata.put("nativeBrightGlyphPixels", bright).put("unobstructedLines", visibleEvidence)
            saveMetadata("work-$name", metadata)
            assertTrue("Complete native line $line is within the legal viewport: $glyphBounds / $viewport", viewport.contains(glyphBounds))
            assertFalse("The floating reader must not cover the actual glyph line: glyphs=$glyphBounds controls=$bar", Rect.intersects(glyphBounds, bar))
            for (index in 0 until visibleEvidence.length()) {
                val evidence = visibleEvidence.getJSONObject(index)
                if (evidence.getBoolean("expectsGlyphs")) assertTrue("Actual native screenshot contains rendered glyphs for line ${evidence.getInt("line")}",
                    evidence.getInt("nativeBrightGlyphPixels") > 8)
            }
            return visibleLines.keys.toSet()
        } finally { image.recycle() }
    }

    private fun glyphLineBounds(text: TextView, line: Int): Rect {
        val origin = onScreen(text)
        val layout = requireNotNull(text.layout)
        val left = origin.left + text.compoundPaddingLeft + layout.getLineLeft(line).toInt()
        return Rect(left, origin.top + text.compoundPaddingTop + layout.getLineTop(line),
            max(left + 1, origin.left + text.compoundPaddingLeft + ceil(layout.getLineRight(line).toDouble()).toInt()),
            origin.top + text.compoundPaddingTop + layout.getLineBottom(line))
    }

    private fun curveMetadata(view: MathEuclideanDiagramView): JSONObject {
        val drawing = view.drawingSnapshot()
        val labels = JSONArray()
        drawing.circles.forEach { circle ->
            val label = drawing.labels.single { it.ownerId == circle.id }
            val center = EuclideanPosition((label.bounds.left + label.bounds.right) / 2, (label.bounds.top + label.bounds.bottom) / 2)
            val radius = hypot(center.x - circle.center.x, center.y - circle.center.y)
            labels.put(JSONObject().put("id", circle.id).put("text", label.text).put("radius", circle.radius)
                .put("center", JSONArray(listOf(circle.center.x, circle.center.y))).put("bounds", JSONArray(listOf(
                    label.bounds.left, label.bounds.top, label.bounds.right, label.bounds.bottom)))
                .put("distanceToOwnCurve", abs(radius - circle.radius)).put("distanceToOtherCurve",
                    drawing.circles.filter { it.id != circle.id }.minOf { abs(radius - it.radius) }))
        }
        return JSONObject().put("labels", labels).put("width", view.width).put("height", view.height)
            .put("sourcePuzzle", "recuperar_la_posicion_al_cambiar_el_radio").put("sameNativeView", true)
    }

    private fun assertCurveAssociation(view: MathEuclideanDiagramView, metadata: JSONObject) {
        val drawing = view.drawingSnapshot()
        assertEquals(setOf("c", "d"), drawing.circles.map { it.id }.toSet())
        val nativePaint = MathEuclideanDiagramView::class.java.getDeclaredField("paint").apply { isAccessible = true }.get(view) as Paint
        val density = view.resources.displayMetrics.density.toDouble()
        drawing.circles.forEachIndexed { index, circle ->
            val label = drawing.labels.single { it.ownerId == circle.id }
            val distances = metadata.getJSONArray("labels").getJSONObject(index)
            assertEquals(circle.id, label.text)
            assertEquals(label.text, label.lines.joinToString(""))
            assertEquals("Full label bounds are measured with native Paint", nativePaint.measureText(label.text).toDouble() + 4 * density, label.bounds.width, .1)
            assertTrue("${circle.id} is near its own curve, not a global scene corner: $distances",
                distances.getDouble("distanceToOwnCurve") <= max(12 * density, max(label.bounds.height, label.bounds.width) * .8))
            assertTrue("${circle.id} unambiguously associates with its own concentric curve: $distances",
                distances.getDouble("distanceToOwnCurve") + density < distances.getDouble("distanceToOtherCurve"))
        }
        for (a in drawing.labels.indices) for (b in a + 1 until drawing.labels.size)
            assertFalse("Names must not overlap", drawing.labels[a].bounds.overlaps(drawing.labels[b].bounds))
    }

    private fun awaitExpanded(view: MathEuclideanDiagramView, inlineParent: ViewGroup, expanded: Boolean) {
        val deadline = SystemClock.uptimeMillis() + 6_000
        while (SystemClock.uptimeMillis() < deadline) {
            instrumentation.waitForIdleSync()
            var ready = false
            instrumentation.runOnMainSync { ready = view.hasWindowFocus() && view.width > 0 && view.height > 0 && !view.isLayoutRequested &&
                if (expanded) (view.parent as? View)?.tag == "math_expanded_visual_container" else view.parent === inlineParent }
            if (ready) return
            SystemClock.sleep(50)
        }
        fail("Native expanded representation did not settle (expanded=$expanded)")
    }

    private fun settle(scenario: ActivityScenario<LessonActivity>) {
        val deadline = SystemClock.uptimeMillis() + 6_000
        var previous = ""
        var count = 0
        while (SystemClock.uptimeMillis() < deadline) {
            instrumentation.waitForIdleSync()
            scenario.onActivity { activity ->
                val scroll = activity.findViewById<ScrollView>(R.id.lessonScroll)
                val bar = activity.findViewById<View>(R.id.lessonNarrationControls)
                val current = "${scroll.width}/${scroll.height}/${scroll.getChildAt(0).height}/${scroll.paddingBottom}/${scroll.scrollY}/${onScreen(bar)}"
                count = if (!scroll.isLayoutRequested && !scroll.getChildAt(0).isLayoutRequested && current == previous) count + 1 else 0
                previous = current
            }
            if (count >= 4) return
            SystemClock.sleep(100)
        }
        fail("Actual lesson dimensions/reader bar did not stabilize: $previous")
    }

    private fun legalEnd(scroll: ScrollView): Int {
        val child = scroll.getChildAt(0)
        val margins = child.layoutParams as? ViewGroup.MarginLayoutParams
        return (child.height + (margins?.topMargin ?: 0) + (margins?.bottomMargin ?: 0) -
            (scroll.height - scroll.paddingTop - scroll.paddingBottom)).coerceAtLeast(0)
    }
    private fun onScreen(view: View): Rect = IntArray(2).let { view.getLocationOnScreen(it); Rect(it[0], it[1], it[0] + view.width, it[1] + view.height) }
    private fun rect(value: Rect) = JSONArray(listOf(value.left, value.top, value.right, value.bottom))

    companion object {
        private const val ROOT = "content/es/math_physics/math_geometria_plana/fundamentos_geometricos/"
        private const val CIRCUMFERENCE = ROOT + "lesson_circunferencia_circulo_y_radios.json"
        private const val SEGMENTS = ROOT + "lesson_comparar_segmentos_y_elegir_una_unidad.json"
    }
}
