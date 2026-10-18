package com.learngraphicsprogramming.app.math

import android.content.res.Configuration
import android.graphics.Bitmap
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Rect
import android.os.Build
import android.os.SystemClock
import android.view.View
import android.view.ViewGroup
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
import kotlin.math.ceil
import kotlin.math.floor
import kotlin.math.hypot
import kotlin.math.max
import kotlin.math.min

/** Real Work scenes, original answers and all six pages, copied only into fresh QA identities.
 * The four tests cover six panels x two profiles x inline/expanded = 24 native views. */
@RunWith(AndroidJUnit4::class)
class MathArcLabelAssociationDeviceTest : MathGeometryQaSupport("com.learngraphicsprogramming.app.realdownloadsqa") {
    @Before fun requireLdPlayerWithOriginalVisibleReaderControls() {
        assertTrue("Only the explicitly selected LDPlayer; never Work's Honor",
            Build.SUPPORTED_ABIS.any { it == "x86" || it == "x86_64" })
        NarrationSettingsRepository.saveControlsVisible(context, true)
        NarrationSettingsRepository.savePosition(context, 1f, .85f)
        assertFalse(NarrationSettingsRepository.get(context).enabled)
    }

    @Test fun pageOneBothAlignmentPanelsKeepEveryNameWithItsOwnMark() = verifyPage(1, 6, 2)
    @Test fun pageTwoBothDifferencePanelsKeepEveryNameWithItsOwnMark() = verifyPage(2, 5, 2)
    @Test fun pageThreeRepeatedArcKeepsCenterAndEveryNamedEndWithItsOwnMark() = verifyPage(3, 5, 1)
    @Test fun pageFiveRationalFactorKeepsCenterAndEveryNamedEndWithItsOwnMark() = verifyPage(5, 5, 1)

    private fun verifyPage(page: Int, block: Int, panelCount: Int) {
        val source = context.assets.open(ASSET).bufferedReader(Charsets.UTF_8).use { JSONObject(it.readText()) }
        assertEquals("The current unfinished source still has six pages", 6, source.getJSONArray("pages").length())
        val original = source.getJSONArray("pages").getJSONObject(page - 1).getJSONArray("blocks").getJSONObject(block - 1)
        val fixture = fixture(original, page - 1, block - 1)
        fixture.lesson.put("pages", JSONArray(source.getJSONArray("pages").toString()))
        val sourceBefore = source.toString()
        withFixture(fixture) { scenario ->
            val portrait = Triple(Configuration.ORIENTATION_PORTRAIT, 100, "portrait-100")
            val landscape = Triple(Configuration.ORIENTATION_LANDSCAPE, 150, "landscape-150")
            // Reproduce each originally rejected profile before an accepted one can stop
            // the baseline run for a different presentation issue.
            val profiles = if (page == 1 || page == 2) listOf(landscape, portrait) else listOf(portrait, landscape)
            for ((orientation, scale, profile) in profiles) {
                LessonTextSizeSettingsRepository.saveGlobalScale(context, scale)
                orient(scenario, fixture, orientation)
                scenario.onActivity { inject(it, fixture) }
                awaitLayout(scenario, fixture, orientation)
                settle(scenario)
                for (panel in 0 until panelCount) {
                    val prefix = "arc-association-p$page-b$block-panel${panel + 1}-$profile"
                    lateinit var visual: MathEuclideanDiagramView
                    lateinit var inlineParent: ViewGroup
                    lateinit var root: View
                    scenario.onActivity { activity ->
                        assertUnanswered(activity, fixture)
                        val visuals = descendants(content(activity)).filterIsInstance<MathEuclideanDiagramView>()
                        assertEquals(panelCount, visuals.size)
                        visual = visuals[panel]
                        inlineParent = visual.parent as ViewGroup
                        root = activity.findViewById(android.R.id.content)
                        scrollVisible(activity, visual.parent.parent as View)
                    }
                    settle(scenario)
                    fun sceneMetadata(expanded: Boolean) = associationMetadata(visual).put("profile", profile).put("expanded", expanded)
                        .put("page", page).put("block", block).put("panel", panel + 1).put("qaLessonId", fixture.id)
                        .put("totalPages", fixture.lesson.getJSONArray("pages").length())
                    fun verifyInline() {
                        scenario.onActivity { scrollVisible(it, visual.parent.parent as View) }
                        settle(scenario)
                        lateinit var metadata: JSONObject
                        scenario.onActivity { metadata = sceneMetadata(false) }
                        // Capture before every assertion: rejected originals remain evidence.
                        capture("$prefix-inline-initial", { root }, metadata)
                        assertAssociation(visual, metadata)
                        captureInlineCoverage(scenario, visual, root, prefix, metadata)
                    }
                    fun verifyExpanded() {
                        scenario.onActivity { activity ->
                            val wrapper = visual.parent.parent as View
                            scrollVisible(activity, wrapper)
                            descendants(wrapper).filterIsInstance<ImageButton>().single { it.tag == "math_expand_representation" }.performClick()
                        }
                        awaitWindow(visual, inlineParent, true)
                        lateinit var metadata: JSONObject
                        instrumentation.runOnMainSync { metadata = sceneMetadata(true) }
                        capture("$prefix-expanded", { visual }, metadata)
                        assertAssociation(visual, metadata)
                        assertExpandedGlyphs(visual, metadata, "$prefix-expanded")
                        instrumentation.runOnMainSync { descendants(visual.rootView).filterIsInstance<ImageButton>()
                            .single { it.tag == "math_exit_representation" }.performClick() }
                        awaitWindow(visual, inlineParent, false)
                    }
                    if ((page == 1 || page == 2) && orientation == Configuration.ORIENTATION_LANDSCAPE) {
                        verifyExpanded()
                        verifyInline()
                    } else {
                        verifyInline()
                        verifyExpanded()
                    }
                    scenario.onActivity { assertUnanswered(it, fixture) }
                }
            }
        }
        assertEquals("No source text, figure, ID, evaluator or response is rewritten", sourceBefore, source.toString())
    }

    private fun associationMetadata(view: MathEuclideanDiagramView): JSONObject {
        val drawing = view.drawingSnapshot()
        val paint = MathEuclideanDiagramView::class.java.getDeclaredField("paint").apply { isAccessible = true }.get(view) as Paint
        val names = JSONArray()
        for (label in drawing.labels) {
            val owner = drawing.points.single { it.id == label.ownerId }
            val middle = EuclideanPosition((label.bounds.left + label.bounds.right) / 2, (label.bounds.top + label.bounds.bottom) / 2)
            val other = drawing.points.filter { it.id != owner.id }.minBy { distance(middle, it.center) }
            val nearestEdge = drawing.points.filter { it.id != owner.id }.minBy { pointToRect(it.center, label.bounds) }
            val actualInk = JSONArray()
            val actualCorners = JSONArray()
            val density = view.resources.displayMetrics.density.toDouble()
            label.lines.forEachIndexed { index, text ->
                val nativeInk = Rect()
                paint.getTextBounds(text, 0, text.length, nativeInk)
                val x = label.bounds.left + 2 * density
                val baseline = label.bounds.top + density + label.ascent + index * label.lineHeight
                val ink = EuclideanRect(x + nativeInk.left, baseline + nativeInk.top, x + nativeInk.right, baseline + nativeInk.bottom)
                actualInk.put(rectangle(ink))
                for (corner in listOf(EuclideanPosition(ink.left, ink.top), EuclideanPosition(ink.right, ink.top),
                    EuclideanPosition(ink.left, ink.bottom), EuclideanPosition(ink.right, ink.bottom))) {
                    val closestOther = drawing.points.filter { it.id != owner.id }.minBy { distance(corner, it.center) }
                    actualCorners.put(JSONObject().put("position", position(corner)).put("ownDistance", distance(corner, owner.center))
                        .put("nearestOtherId", closestOther.id).put("nearestOtherDistance", distance(corner, closestOther.center)))
                }
            }
            names.put(JSONObject().put("ownerId", label.ownerId).put("text", label.text)
                .put("ownMark", position(owner.center)).put("labelBounds", rectangle(label.bounds))
                .put("ownCenterDistance", distance(middle, owner.center)).put("nearestOtherId", other.id)
                .put("nearestOtherCenterDistance", distance(middle, other.center))
                .put("ownEdgeDistance", pointToRect(owner.center, label.bounds))
                .put("nearestOtherEdgeId", nearestEdge.id).put("nearestOtherEdgeDistance", pointToRect(nearestEdge.center, label.bounds))
                .put("nativeMeasuredWidth", label.lines.maxOf { paint.measureText(it) }.toDouble())
                .put("nativeGlyphInkBounds", actualInk).put("nativeGlyphInkCorners", actualCorners))
        }
        return JSONObject().put("width", view.width).put("height", view.height).put("pointNames", names)
            .put("nativePaintTextSize", paint.textSize.toDouble()).put("density", view.resources.displayMetrics.density.toDouble())
            .put("declaredPointIds", JSONArray(view.diagram.points.map { it.id }))
            .put("source", ASSET).put("checkPressed", false)
    }

    private fun assertAssociation(view: MathEuclideanDiagramView, metadata: JSONObject) {
        instrumentation.runOnMainSync {
            val drawing = view.drawingSnapshot()
            val expectedNames = view.diagram.points.flatMap { it.labels }.sorted()
            assertEquals("Every original point name remains present, with no added labels", expectedNames, drawing.labels.map { it.text }.sorted())
            val density = view.resources.displayMetrics.density.toDouble()
            val gap = max(10 * density, metadata.getDouble("nativePaintTextSize") * .55)
            drawing.labels.forEachIndexed { index, label ->
                val details = metadata.getJSONArray("pointNames").getJSONObject(index)
                assertEquals(label.text, label.lines.joinToString(""))
                assertEquals("Full text uses actual native Paint", details.getDouble("nativeMeasuredWidth") + 4 * density, label.bounds.width, .1)
                assertTrue("Full authored label stays inside the native panel", label.bounds.left >= 0 && label.bounds.top >= 0 &&
                    label.bounds.right <= view.width && label.bounds.bottom <= view.height)
                val halfDiagonal = hypot(label.bounds.width, label.bounds.height) / 2
                assertTrue("Name remains local to its own mark, not an arbitrary free corner: $details",
                    details.getDouble("ownCenterDistance") <= 2 * gap + halfDiagonal)
                assertTrue("Its own geometric mark must be the unambiguous nearest mark, including unnamed neighbours: $details",
                    details.getDouble("ownCenterDistance") + 2 * density < details.getDouble("nearestOtherCenterDistance"))
                assertTrue("The complete letter must not extend toward a different mark: $details",
                    details.getDouble("ownEdgeDistance") + density < details.getDouble("nearestOtherEdgeDistance"))
                val corners = details.getJSONArray("nativeGlyphInkCorners")
                for (cornerIndex in 0 until corners.length()) {
                    val corner = corners.getJSONObject(cornerIndex)
                    assertTrue("Every corner of the native glyph ink, not its reserved line box, belongs nearest to its own mark: ${label.text}/$corner",
                        corner.getDouble("ownDistance") + .5 * density < corner.getDouble("nearestOtherDistance"))
                }
                drawing.points.forEach { assertTrue("A label may not hide the original white mark ${it.id}",
                    pointToRect(it.center, label.bounds) >= 5.6 * density - .1) }
            }
            for (a in drawing.labels.indices) for (b in a + 1 until drawing.labels.size)
                assertFalse("Complete point names must not overlap", drawing.labels[a].bounds.overlaps(drawing.labels[b].bounds))
        }
    }

    private fun captureInlineCoverage(scenario: ActivityScenario<LessonActivity>, visual: MathEuclideanDiagramView,
                                      root: View, prefix: String, initial: JSONObject) {
        lateinit var scroll: ScrollView
        lateinit var controls: View
        val labelsCovered = mutableSetOf<String>()
        val marksCovered = mutableSetOf<String>()
        val intervals = mutableListOf<Pair<Double, Double>>()
        val frames = JSONArray()
        var inkTop = 0.0
        var inkBottom = 0.0
        scenario.onActivity { activity ->
            scroll = activity.findViewById(R.id.lessonScroll)
            controls = activity.findViewById(R.id.lessonNarrationControls)
            assertTrue("Keep the original reader controls visible", controls.isShown)
            val drawing = visual.drawingSnapshot()
            val margin = 6 * visual.resources.displayMetrics.density.toDouble()
            inkTop = min(drawing.circles.minOf { it.center.y - it.radius - margin }, drawing.labels.minOf { it.bounds.top })
            inkBottom = max(drawing.circles.maxOf { it.center.y + it.radius + margin }, drawing.labels.maxOf { it.bounds.bottom })
        }
        fun completeHeight(): Boolean {
            var end = inkTop
            for ((from, to) in intervals.sortedBy { it.first }) { if (from > end + 1) return false; end = max(end, to) }
            return end >= inkBottom - 1
        }
        fun record(name: String, metadata: JSONObject, screenshot: Boolean) {
            val labelRects = linkedMapOf<String, Rect>()
            val pointRects = linkedMapOf<String, Rect>()
            val nativeLabels = JSONArray()
            val nativePoints = JSONArray()
            scenario.onActivity {
                val origin = onScreen(visual)
                val viewport = onScreen(scroll).apply { top += scroll.paddingTop; bottom -= scroll.paddingBottom }
                val bar = onScreen(controls)
                val safeBottom = min(viewport.bottom, bar.top)
                val from = max(inkTop, (viewport.top - origin.top).toDouble())
                val to = min(inkBottom, (safeBottom - origin.top).toDouble())
                if (from <= to) intervals += from to to
                metadata.put("viewDisplayBounds", rect(origin)).put("viewport", rect(viewport)).put("controls", rect(bar))
                    .put("scrollY", scroll.scrollY).put("finiteLegalMax", legalEnd(scroll))
                    .put("requiredInkLocalY", JSONArray(listOf(inkTop, inkBottom)))
                    .put("unobstructedInkLocalY", JSONArray(listOf(from, to)))
                val drawing = visual.drawingSnapshot()
                val markRadius = 4 * visual.resources.displayMetrics.density.toDouble()
                for (label in drawing.labels) {
                    val bounds = toDisplay(label.bounds, origin)
                    val ownMark = drawing.points.single { it.id == label.ownerId }.center
                    val ownMarkBounds = toDisplay(EuclideanRect(ownMark.x - markRadius, ownMark.y - markRadius,
                        ownMark.x + markRadius, ownMark.y + markRadius), origin)
                    val ownMarkVisible = viewport.contains(ownMarkBounds) && !Rect.intersects(ownMarkBounds, bar)
                    val visible = viewport.contains(bounds) && !Rect.intersects(bounds, bar) && ownMarkVisible
                    nativeLabels.put(JSONObject().put("ownerId", label.ownerId).put("text", label.text)
                        .put("displayBounds", rect(bounds)).put("ownMarkDisplayBounds", rect(ownMarkBounds))
                        .put("ownMarkVisibleInSameFrame", ownMarkVisible).put("completelyVisibleWithoutReader", visible))
                    if (visible) labelRects[label.ownerId] = bounds
                }
                val radius = 4 * visual.resources.displayMetrics.density.toDouble()
                for (point in drawing.points) {
                    val bounds = toDisplay(EuclideanRect(point.center.x - radius, point.center.y - radius,
                        point.center.x + radius, point.center.y + radius), origin)
                    val visible = viewport.contains(bounds) && !Rect.intersects(bounds, bar)
                    nativePoints.put(JSONObject().put("id", point.id).put("displayBounds", rect(bounds)).put("fullyVisible", visible))
                    if (visible) pointRects[point.id] = bounds
                }
                metadata.put("nativeLabels", nativeLabels).put("nativeMarks", nativePoints)
                assertTrue("Only finite legal scrolling is used", scroll.scrollY in 0..legalEnd(scroll))
            }
            if (screenshot) capture(name, { root }, metadata)
            val bitmap = requireNotNull(instrumentation.uiAutomation.takeScreenshot())
            try {
                for (index in 0 until nativeLabels.length()) {
                    val label = nativeLabels.getJSONObject(index)
                    val bounds = labelRects[label.getString("ownerId")] ?: continue
                    val white = whitePixels(bitmap, bounds)
                    label.put("nativeWhiteGlyphPixels", white)
                    assertTrue("Native screenshot really paints the full name ${label.getString("text")}", white > 8)
                }
                for (index in 0 until nativePoints.length()) {
                    val point = nativePoints.getJSONObject(index)
                    val bounds = pointRects[point.getString("id")] ?: continue
                    val white = whitePixels(bitmap, bounds)
                    point.put("nativeWhiteMarkPixels", white)
                    assertTrue("Native screenshot retains visible mark ${point.getString("id")}", white > 8)
                }
            } finally { bitmap.recycle() }
            labelsCovered += labelRects.keys
            marksCovered += pointRects.keys
            frames.put("$name.png")
            saveMetadata(name, metadata)
        }
        record("$prefix-inline-initial", initial, false)
        val allNames = visual.diagram.points.filter { it.labels.isNotEmpty() }.map { it.id }.toSet()
        val allMarks = visual.diagram.points.map { it.id }.toSet()
        if (labelsCovered != allNames || marksCovered != allMarks || !completeHeight()) {
            for (edge in listOf("top", "bottom")) {
                scenario.onActivity {
                    val viewport = onScreen(scroll).apply { top += scroll.paddingTop; bottom -= scroll.paddingBottom }
                    val margin = 4 * visual.resources.displayMetrics.density
                    val origin = onScreen(visual)
                    val target = if (edge == "top") scroll.scrollY + origin.top + inkTop - viewport.top - margin
                        else scroll.scrollY + origin.top + inkBottom - min(viewport.bottom, onScreen(controls).top) + margin
                    scroll.scrollTo(0, target.toInt().coerceIn(0, legalEnd(scroll)))
                }
                settle(scenario)
                lateinit var metadata: JSONObject
                scenario.onActivity { metadata = associationMetadata(visual).put("inlineEdge", edge) }
                record("$prefix-inline-$edge", metadata, true)
            }
        }
        assertEquals("Each original name and its own white mark are fully visible together in an unobstructed native frame", allNames, labelsCovered)
        assertEquals("Each original mark, named or unnamed, is fully visible in a native frame", allMarks, marksCovered)
        assertTrue("Legal native frames cover the full height of the original curves and labels: $intervals", completeHeight())
        saveMetadata("$prefix-inline-coverage", JSONObject().put("frames", frames).put("visibleNameIds", JSONArray(labelsCovered.sorted()))
            .put("visibleMarkIds", JSONArray(marksCovered.sorted())).put("requiredInkLocalY", JSONArray(listOf(inkTop, inkBottom)))
            .put("coveredLocalYIntervals", JSONArray().apply { intervals.forEach { put(JSONArray(listOf(it.first, it.second))) } })
            .put("allNamesMarksAndCurveHeightCovered", true))
    }

    private fun assertExpandedGlyphs(visual: MathEuclideanDiagramView, metadata: JSONObject, name: String) {
        lateinit var origin: Rect
        lateinit var drawing: EuclideanDrawingSnapshot
        instrumentation.runOnMainSync { origin = onScreen(visual); drawing = visual.drawingSnapshot() }
        val screenshot = requireNotNull(instrumentation.uiAutomation.takeScreenshot())
        val labels = JSONArray()
        val marks = JSONArray()
        try {
            for (label in drawing.labels) {
                val bounds = toDisplay(label.bounds, origin)
                assertTrue("Expanded panel fully contains each point name", origin.contains(bounds))
                val white = whitePixels(screenshot, bounds)
                labels.put(JSONObject().put("ownerId", label.ownerId).put("displayBounds", rect(bounds)).put("nativeWhiteGlyphPixels", white))
                assertTrue("Expanded native screenshot actually paints each complete letter", white > 8)
            }
            val radius = 4 * visual.resources.displayMetrics.density.toDouble()
            for (point in drawing.points) {
                val bounds = toDisplay(EuclideanRect(point.center.x - radius, point.center.y - radius,
                    point.center.x + radius, point.center.y + radius), origin)
                assertTrue("Expanded panel fully contains every original white mark", origin.contains(bounds))
                val white = whitePixels(screenshot, bounds)
                marks.put(JSONObject().put("id", point.id).put("displayBounds", rect(bounds)).put("nativeWhiteMarkPixels", white))
                assertTrue("Expanded native screenshot retains every original white point", white > 8)
            }
        } finally { screenshot.recycle() }
        metadata.put("nativeLabelGlyphs", labels).put("nativeWhiteMarks", marks)
        saveMetadata(name, metadata)
    }

    private fun assertUnanswered(activity: LessonActivity, fixture: Fixture) {
        assertEquals("No exercise is completed during this presentation test", 0, field(activity, "completedExercises"))
        val action = check(activity)
        assertEquals("The unused action retains its original Comprobar label", activity.getString(R.string.math_ui_check), action.text.toString())
        // Empty fields legitimately disable Comprobar. Submission is established from
        // the unchanged draft and empty feedback, not from whether the button is enabled.
        val host = action.parent as ViewGroup
        assertTrue("No verdict or review has been shown", (host.getChildAt(host.childCount - 1) as TextView).text.isBlank())
        assertTrue("All original text responses remain empty", descendants(host).filterIsInstance<EditText>().all { it.text.isNullOrBlank() })
        assertTrue("All original dropdowns retain the unselected placeholder", descendants(host).filterIsInstance<Spinner>()
            .all { it.selectedItemPosition == 0 })
        val saved = com.learngraphicsprogramming.app.ProgressRepository.getExerciseState(activity, fixture.id, fixture.page, fixture.index)
        assertFalse("No accepted response or verdict is manufactured", saved?.optBoolean("correct", false) == true)
        saved?.optJSONObject("responses")?.let { responses ->
            for (key in responses.keys()) {
                val answer = MathAnswerJson.decode(responses.getJSONObject(key))
                assertTrue("Any legitimately initialized draft retains only empty fields", answer is MathAnswer.Fields && answer.values.values.all { it.isBlank() })
            }
        }
    }

    private fun settle(scenario: ActivityScenario<LessonActivity>) {
        val deadline = SystemClock.uptimeMillis() + 6_000
        var prior = ""
        var stable = 0
        while (SystemClock.uptimeMillis() < deadline) {
            instrumentation.waitForIdleSync()
            scenario.onActivity { activity ->
                val scroll = activity.findViewById<ScrollView>(R.id.lessonScroll)
                val controls = activity.findViewById<View>(R.id.lessonNarrationControls)
                val next = "${scroll.width}/${scroll.height}/${scroll.getChildAt(0).height}/${scroll.paddingBottom}/${scroll.scrollY}/${onScreen(controls)}"
                stable = if (!scroll.isLayoutRequested && !scroll.getChildAt(0).isLayoutRequested && next == prior) stable + 1 else 0
                prior = next
            }
            if (stable >= 4) return
            SystemClock.sleep(100)
        }
        fail("Native lesson bounds did not settle: $prior")
    }

    private fun awaitWindow(visual: MathEuclideanDiagramView, inlineParent: ViewGroup, expanded: Boolean) {
        val deadline = SystemClock.uptimeMillis() + 6_000
        var stable = 0
        while (SystemClock.uptimeMillis() < deadline) {
            instrumentation.waitForIdleSync()
            instrumentation.runOnMainSync {
                val parentCorrect = if (expanded) (visual.parent as? View)?.tag == "math_expanded_visual_container" else visual.parent === inlineParent
                stable = if (parentCorrect && visual.hasWindowFocus() && visual.width > 0 && visual.height > 0 && !visual.isLayoutRequested) stable + 1 else 0
            }
            if (stable >= 3) return
            SystemClock.sleep(75)
        }
        fail("Native diagram window did not settle (expanded=$expanded)")
    }
    private fun legalEnd(scroll: ScrollView): Int {
        val child = scroll.getChildAt(0)
        val margins = child.layoutParams as? ViewGroup.MarginLayoutParams
        return (child.height + (margins?.topMargin ?: 0) + (margins?.bottomMargin ?: 0) - scroll.height + scroll.paddingTop + scroll.paddingBottom).coerceAtLeast(0)
    }
    private fun whitePixels(bitmap: Bitmap, bounds: Rect): Int {
        var count = 0
        for (y in max(0, bounds.top) until min(bitmap.height, bounds.bottom)) for (x in max(0, bounds.left) until min(bitmap.width, bounds.right)) {
            val color = bitmap.getPixel(x, y)
            if (Color.red(color) > 210 && Color.green(color) > 210 && Color.blue(color) > 210) count++
        }
        return count
    }
    private fun pointToRect(point: EuclideanPosition, bounds: EuclideanRect) = hypot(max(0.0, max(bounds.left - point.x, point.x - bounds.right)),
        max(0.0, max(bounds.top - point.y, point.y - bounds.bottom)))
    private fun distance(a: EuclideanPosition, b: EuclideanPosition) = hypot(a.x - b.x, a.y - b.y)
    private fun onScreen(view: View): Rect = IntArray(2).let { view.getLocationOnScreen(it); Rect(it[0], it[1], it[0] + view.width, it[1] + view.height) }
    private fun toDisplay(bounds: EuclideanRect, origin: Rect) = Rect(origin.left + floor(bounds.left).toInt(), origin.top + floor(bounds.top).toInt(),
        origin.left + ceil(bounds.right).toInt(), origin.top + ceil(bounds.bottom).toInt())
    private fun rect(value: Rect) = JSONArray(listOf(value.left, value.top, value.right, value.bottom))
    private fun rectangle(value: EuclideanRect) = JSONArray(listOf(value.left, value.top, value.right, value.bottom))
    private fun position(value: EuclideanPosition) = JSONArray(listOf(value.x, value.y))
    companion object {
        private const val ASSET = "content/es/math_physics/math_geometria_plana/fundamentos_geometricos/lesson_comparar_y_operar_con_arcos.json"
    }
}
