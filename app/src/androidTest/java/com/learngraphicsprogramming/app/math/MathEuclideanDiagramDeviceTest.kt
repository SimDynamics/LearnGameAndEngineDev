package com.learngraphicsprogramming.app.math

import android.content.res.Configuration
import android.graphics.Bitmap
import android.graphics.Color
import android.graphics.Rect
import android.os.SystemClock
import android.view.View
import android.view.ViewGroup
import android.widget.ImageButton
import android.widget.RadioButton
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.learngraphicsprogramming.app.ProgressRepository
import com.learngraphicsprogramming.app.R
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import kotlin.math.abs
import kotlin.math.hypot
import kotlin.math.max
import kotlin.math.min

/** The future curricular scenes are isolated QA data, never assets or indexed lessons.
 * Native Canvas pixels are checked against the exact geometry consumed by onDraw; full
 * screenshots then verify reading in the actual mathematical activity and expanded panel. */
@RunWith(AndroidJUnit4::class)
class MathEuclideanDiagramDeviceTest : MathGeometryQaSupport() {
    private val background: Int get() = context.getColor(R.color.lesson_canvas)
    @Test fun threeRequiredScenesDrawNativeLabelsAndLimitsWithoutCoordinatesInBothOrientations() {
        for ((sceneName, representations) in scenes()) {
            val fixture = fixture(block(sceneName, representations))
            withFixture(fixture) { scenario ->
                for ((orientation, orientationName) in listOf(Configuration.ORIENTATION_PORTRAIT to "portrait",
                    Configuration.ORIENTATION_LANDSCAPE to "landscape")) {
                    orient(scenario, fixture, orientation)
                    for (index in representations.indices) {
                        var visual: MathEuclideanDiagramView? = null
                        var inlineParent: ViewGroup? = null
                        scenario.onActivity { activity ->
                            visual = descendants(content(activity)).filterIsInstance<MathEuclideanDiagramView>()[index]
                            inlineParent = requireNotNull(visual).parent as ViewGroup
                            val wrapper = requireNotNull(visual).parent.parent as ViewGroup
                            scrollVisible(activity, wrapper)
                        }
                        instrumentation.waitForIdleSync()
                        scenario.onActivity {
                            val wrapper = requireNotNull(visual).parent.parent as ViewGroup
                            descendants(wrapper).filterIsInstance<ImageButton>().single { it.tag == "math_expand_representation" }.performClick()
                        }
                        instrumentation.waitForIdleSync()
                        awaitExpandedWindowState(requireNotNull(visual), requireNotNull(inlineParent), true,
                            "$sceneName-$index-$orientationName-open")
                        var metadata = JSONObject()
                        instrumentation.runOnMainSync {
                            assertTrue("The same native Euclidean view moved into the actual expanded panel", requireNotNull(visual).isAttachedToWindow)
                            assertTrue(requireNotNull(visual).hasWindowFocus())
                            metadata = assertGeometryAndPaint(requireNotNull(visual))
                                .put("scene", sceneName).put("representationIndex", index).put("orientation", orientationName)
                                .put("fixtureFingerprint", fixture.fingerprint).put("fixtureOnly", true)
                        }
                        capture("euclidean-$sceneName-$index-$orientationName", { requireNotNull(visual) }, metadata)
                        instrumentation.runOnMainSync {
                            descendants(requireNotNull(visual).rootView).filterIsInstance<ImageButton>()
                                .single { it.tag == "math_exit_representation" }.performClick()
                        }
                        instrumentation.waitForIdleSync()
                        awaitExpandedWindowState(requireNotNull(visual), requireNotNull(inlineParent), false,
                            "$sceneName-$index-$orientationName-close")
                    }
                }
            }
        }
    }

    /** IdleSync alone does not await WindowManager's focus/insets traversal of a dialog. */
    private fun awaitExpandedWindowState(visual: MathEuclideanDiagramView, inlineParent: ViewGroup,
                                         expanded: Boolean, label: String) {
        val deadline = SystemClock.uptimeMillis() + 8_000L
        var consecutive = 0
        var previousState = ""
        var diagnostic = ""
        while (SystemClock.uptimeMillis() < deadline) {
            instrumentation.waitForIdleSync()
            instrumentation.runOnMainSync {
                val parent = visual.parent as? ViewGroup
                val bounds = Rect()
                val visible = visual.getGlobalVisibleRect(bounds)
                val correctParent = if (expanded) parent?.tag == "math_expanded_visual_container" else parent === inlineParent
                val measured = visual.width > 0 && visual.height > 0 && !visual.isLayoutRequested && parent?.isLayoutRequested == false
                val state = "${visual.width}x${visual.height}/$bounds/${parent?.javaClass?.name}/${parent?.tag}"
                val ready = correctParent && measured && visual.isAttachedToWindow && visual.hasWindowFocus() && visible
                consecutive = if (ready) { if (state == previousState) consecutive + 1 else 1 } else 0
                previousState = state
                diagnostic = "$state attached=${visual.isAttachedToWindow}, focus=${visual.hasWindowFocus()}, visible=$visible, " +
                    "correctParent=$correctParent, layoutRequested=${visual.isLayoutRequested}"
            }
            if (consecutive >= 3) return
            SystemClock.sleep(50L)
        }
        fail("The native representation window did not stabilize after $label: $diagnostic")
    }

    @Test fun readingChoicesRejectDistractorsAcceptExplicitRelationshipsAndRestoreCorrectAndWrongState() {
        for ((sceneName, representations) in scenes()) {
            val fixture = fixture(block(sceneName, representations))
            withFixture(fixture) { scenario ->
                scenario.onActivity { activity ->
                    val choices = descendants(content(activity)).filterIsInstance<RadioButton>()
                    assertEquals(2, choices.size)
                    choices[1].performClick()
                    check(activity).performClick()
                    assertState(activity, fixture, false, MathAnswer.Choice(setOf("wrong")))
                }
                scenario.recreate()
                scenario.onActivity { inject(it, fixture) }
                awaitLayout(scenario, fixture)
                scenario.onActivity { activity ->
                    assertState(activity, fixture, false, MathAnswer.Choice(setOf("wrong")))
                    assertTrue(descendants(content(activity)).filterIsInstance<RadioButton>()[1].isChecked)
                    descendants(content(activity)).filterIsInstance<RadioButton>()[0].performClick()
                    check(activity).performClick()
                    assertState(activity, fixture, true, MathAnswer.Choice(setOf("correct")))
                }
                scenario.recreate()
                scenario.onActivity { inject(it, fixture) }
                awaitLayout(scenario, fixture)
                scenario.onActivity { activity ->
                    assertState(activity, fixture, true, MathAnswer.Choice(setOf("correct")))
                    assertTrue(descendants(content(activity)).filterIsInstance<RadioButton>()[0].isChecked)
                    assertTrue(descendants(content(activity)).filterIsInstance<RadioButton>().all { !it.isEnabled })
                    // Restoration preserves object IDs and declared, rather than pixel-inferred,
                    // incidence. No new evaluator or coordinate editor is introduced.
                    val diagrams = descendants(content(activity)).filterIsInstance<MathEuclideanDiagramView>()
                    when (sceneName) {
                        "membership" -> {
                            val diagram = diagrams.single().diagram
                            assertTrue(diagram.contains("r", "B"))
                            assertFalse(diagram.contains("s", "B"))
                            assertTrue(diagram.contains("r", "A") && diagram.contains("s", "A"))
                            assertTrue(diagram.contains("s", "C") && !diagram.contains("r", "C"))
                        }
                        "names" -> {
                            assertEquals(2, diagrams[0].diagram.points.size)
                            assertEquals(1, diagrams[1].diagram.points.size)
                            assertEquals(listOf("A", "B", "P"), diagrams[1].diagram.points.single().labels)
                            assertEquals(2, diagrams[1].diagram.objects.size)
                        }
                        "limits" -> assertEquals(listOf(EuclideanObjectKind.LINE, EuclideanObjectKind.RAY, EuclideanObjectKind.SEGMENT),
                            diagrams.map { it.diagram.objects.single().kind })
                    }
                }
            }
        }
    }

    @Test fun componentKeepsGeometryAndFullLabelsAtNarrowWideAndLargeTextSizes() {
        instrumentation.runOnMainSync {
            for ((scene, representations) in scenes()) for ((index, representation) in representations.withIndex()) {
                for ((widthDp, heightDp, scale) in listOf(Triple(280, 210, 1f), Triple(600, 360, 1f), Triple(320, 250, 1.5f))) {
                    val rendered = MathRepresentationRenderer.render(context, representation, ACCENT)
                    val visual = descendants(rendered).filterIsInstance<MathEuclideanDiagramView>().single()
                    visual.scaleMathText(scale)
                    val density = context.resources.displayMetrics.density
                    val width = (widthDp * density).toInt()
                    val height = (heightDp * density).toInt()
                    visual.measure(View.MeasureSpec.makeMeasureSpec(width, View.MeasureSpec.EXACTLY),
                        View.MeasureSpec.makeMeasureSpec(height, View.MeasureSpec.EXACTLY))
                    visual.layout(0, 0, width, height)
                    val metadata = assertGeometryAndPaint(visual).put("scene", scene).put("representationIndex", index)
                        .put("widthDp", widthDp).put("heightDp", heightDp).put("textScale", scale)
                    if (scale > 1f) {
                        val image = draw(visual)
                        try { saveBitmap(image, "euclidean-$scene-$index-large-canvas.png") } finally { image.recycle() }
                        saveMetadata("euclidean-$scene-$index-large-canvas", metadata)
                    }
                }
            }
        }
    }

    @Test fun existingGeometricTemplatesAndCartesianDefaultsStillPaintTheirOriginalFeatures() {
        instrumentation.runOnMainSync {
            val template = MathRepresentationRenderer.render(context,
                JSONObject("""{"type":"geometry_2d_diagram","shape":"rectangle","labels":["ABCD"]}"""), ACCENT)
            assertTrue(descendants(template).none { it is MathEuclideanDiagramView || it is MathCartesianView })
            val templateVisual = descendants(template).single { it.javaClass.simpleName == "GeometryDiagramView" }
            measureStandalone(templateVisual)
            val templateImage = draw(templateVisual)
            try { assertTrue("The existing rectangle still paints its four sides", countColor(templateImage, ACCENT) > 100) }
            finally { templateImage.recycle() }

            val oldGeometry = MathRepresentationRenderer.render(context,
                JSONObject("""{"type":"geometry_2d_diagram","points":[{"x":-2,"y":-1},{"x":2,"y":-1},{"x":0,"y":2}]}"""), ACCENT)
            val cartesian = descendants(oldGeometry).filterIsInstance<MathCartesianView>().single()
            assertEquals("polygon", cartesian.mode)
            assertEquals(1.0, cartesian.snap, 0.0)
            assertEquals(listOf(MathPoint(-2.0, -1.0), MathPoint(2.0, -1.0), MathPoint(0.0, 2.0)), cartesian.points())
            measureStandalone(cartesian)
            val oldImage = draw(cartesian)
            try {
                val axesColor = Color.rgb(192, 201, 211)
                val gridColor = Color.rgb(51, 56, 65)
                assertTrue("Legacy Cartesian geometry still paints a grid", countColor(oldImage, gridColor) > 100)
                assertTrue("Legacy Cartesian geometry still paints axes/ticks", countColor(oldImage, axesColor) > 100)
                saveBitmap(oldImage, "legacy-cartesian-defaults.png")
            } finally { oldImage.recycle() }
            val directGraph = MathRepresentationRenderer.render(context,
                JSONObject("""{"type":"math_coordinate_graph","points":[{"x":0,"y":0}]}"""), ACCENT)
            assertEquals("point", descendants(directGraph).filterIsInstance<MathCartesianView>().single().mode)
            assertTrue(descendants(directGraph).none { it is MathEuclideanDiagramView })
        }
    }

    private fun assertGeometryAndPaint(view: MathEuclideanDiagramView): JSONObject {
        assertTrue(view.width > 0 && view.height > 0)
        val accent = MathEuclideanDiagramView::class.java.getDeclaredField("accent").apply { isAccessible = true }.getInt(view)
        val drawing = view.drawingSnapshot()
        val expectedTexts = view.diagram.points.flatMap { it.labels } + view.diagram.objects.mapNotNull { it.label }
        assertEquals("Only authored names are drawn; no coordinates, numeric scale or axis labels are appended",
            expectedTexts.sorted(), drawing.labels.map { it.text }.sorted())
        assertTrue(expectedTexts.none { it.contains(Regex("[0-9]")) })
        val labels = JSONArray()
        for (label in drawing.labels) {
            assertEquals("The native wrapped label never drops characters", label.text, label.lines.joinToString(""))
            assertTrue("Complete label left bound", label.bounds.left >= 0)
            assertTrue("Complete label top bound", label.bounds.top >= 0)
            assertTrue("Complete label right bound", label.bounds.right <= view.width)
            assertTrue("Complete label bottom bound", label.bounds.bottom <= view.height)
            labels.put(JSONObject().put("ownerId", label.ownerId).put("text", label.text).put("lines", JSONArray(label.lines))
                .put("bounds", JSONArray(listOf(label.bounds.left, label.bounds.top, label.bounds.right, label.bounds.bottom))))
        }
        for (i in drawing.labels.indices) for (j in i + 1 until drawing.labels.size) {
            val a = drawing.labels[i].bounds
            val b = drawing.labels[j].bounds
            assertFalse("Names ${drawing.labels[i].text}/${drawing.labels[j].text} never overlap illegibly",
                a.left < b.right && a.right > b.left && a.top < b.bottom && a.bottom > b.top)
        }
        val paths = JSONArray()
        drawing.paths.forEach { path ->
            val expectedArrows = when (path.kind) { EuclideanObjectKind.LINE -> 2; EuclideanObjectKind.RAY -> 1; EuclideanObjectKind.SEGMENT -> 0 }
            assertEquals("Infinite directions are distinguished from finite endpoints", expectedArrows,
                drawing.arrows.count { it.objectId == path.id })
            assertEquals(path.kind == EuclideanObjectKind.LINE, path.startArrow)
            assertEquals(path.kind != EuclideanObjectKind.SEGMENT, path.endArrow)
            val objectSpec = view.diagram.objects.single { it.id == path.id }
            if (path.kind != EuclideanObjectKind.LINE) {
                val origin = drawing.points.single { it.id == objectSpec.definingPointIds.first() }.center
                assertEquals(origin.x, path.start.x, 0.01)
                assertEquals(origin.y, path.start.y, 0.01)
            }
            if (path.kind == EuclideanObjectKind.SEGMENT) {
                val endpoint = drawing.points.single { it.id == objectSpec.definingPointIds[1] }.center
                assertEquals(endpoint.x, path.end.x, 0.01)
                assertEquals(endpoint.y, path.end.y, 0.01)
            }
            if (path.kind == EuclideanObjectKind.LINE) assertTrue("A line extends beyond all defining marks",
                drawing.points.filter { point -> view.diagram.contains(path.id, point.id) }
                    .all { distance(it.center, path.start) > 2 && distance(it.center, path.end) > 2 })
            paths.put(JSONObject().put("id", path.id).put("kind", path.kind.name)
                .put("start", JSONArray(listOf(path.start.x, path.start.y))).put("end", JSONArray(listOf(path.end.x, path.end.y)))
                .put("arrows", expectedArrows))
        }
        val image = draw(view)
        try {
            drawing.labels.forEach { label ->
                val paintedGlyphs = (max(0, label.bounds.top.toInt()) until min(image.height, label.bounds.bottom.toInt())).sumOf { y ->
                    (max(0, label.bounds.left.toInt()) until min(image.width, label.bounds.right.toInt())).count { x ->
                        val pixel = image.getPixel(x, y)
                        Color.red(pixel) > 210 && Color.green(pixel) > 210 && Color.blue(pixel) > 210
                    }
                }
                assertTrue("The complete native label ${label.text} actually paints visible glyphs", paintedGlyphs > 3)
            }
            drawing.points.forEach { point -> assertColorNear(image, point.center, Color.WHITE, 2, "Native point mark ${point.id}") }
            drawing.paths.forEach { path ->
                // Try several interior locations because points and protected labels legitimately
                // mask small portions of the line. At least one location must paint real ink.
                assertTrue("Native Canvas paints the ${path.kind} ${path.id}", listOf(.17, .33, .51, .73, .88).any { t ->
                    colorNear(image, EuclideanPosition(path.start.x + (path.end.x - path.start.x) * t,
                        path.start.y + (path.end.y - path.start.y) * t), accent, 3)
                })
            }
            drawing.arrows.forEach { arrow ->
                val first = EuclideanPosition((arrow.tip.x + arrow.wingA.x) / 2, (arrow.tip.y + arrow.wingA.y) / 2)
                val second = EuclideanPosition((arrow.tip.x + arrow.wingB.x) / 2, (arrow.tip.y + arrow.wingB.y) / 2)
                assertColorNear(image, first, accent, 3, "First painted continuation arrow wing")
                assertColorNear(image, second, accent, 3, "Second painted continuation arrow wing")
            }
            // Every non-background pixel must belong to an authored path, arrow, mark or label.
            // Thus an added axis/grid/numeric scale would fail even if its text were inaccessible.
            val tolerance = 7 * context.resources.displayMetrics.density
            for (y in 0 until image.height step 5) for (x in 0 until image.width step 5) {
                val color = image.getPixel(x, y)
                if (color == background) continue
                val pixel = EuclideanPosition(x.toDouble(), y.toDouble())
                // Canvas rasterizes subpixel label boxes into integer pixel squares and
                // antialiases their boundary. Attribute that one-pixel fringe to its exact
                // authored box, rather than requiring the sampled pixel's integer corner
                // to lie inside a fractional bound. The rest of the viewport stays strict.
                val inAuthoredGeometry = drawing.labels.any { x >= kotlin.math.floor(it.bounds.left) - 1 && x <= kotlin.math.ceil(it.bounds.right) + 1 &&
                    y >= kotlin.math.floor(it.bounds.top) - 1 && y <= kotlin.math.ceil(it.bounds.bottom) + 1 } ||
                    drawing.points.any { distance(pixel, it.center) <= tolerance } ||
                    drawing.paths.any { segmentDistance(pixel, it.start, it.end) <= tolerance } ||
                    drawing.arrows.any { segmentDistance(pixel, it.tip, it.wingA) <= tolerance || segmentDistance(pixel, it.tip, it.wingB) <= tolerance }
                if (!inAuthoredGeometry) {
                    val diagnosticName = "euclidean-unexpected-${image.width}x${image.height}-$x-$y"
                    saveBitmap(image, "$diagnosticName.png")
                    saveMetadata(diagnosticName, JSONObject().put("unexpectedX", x).put("unexpectedY", y)
                        .put("actualColorArgb", color).put("expectedBackgroundArgb", background)
                        .put("labels", labels).put("paths", paths).put("width", image.width).put("height", image.height))
                }
                assertTrue("No non-authored axes, grid, coordinates or scale paint at $x,$y", inAuthoredGeometry)
            }
            assertEquals("Euclidean views never paint the legacy grid color", 0, countColor(image, Color.rgb(51, 56, 65)))
            return JSONObject().put("width", view.width).put("height", view.height).put("labels", labels).put("paths", paths)
                .put("points", JSONArray(drawing.points.map { JSONObject().put("id", it.id).put("position", JSONArray(listOf(it.center.x, it.center.y))) }))
                .put("axesGridCoordinatesVisible", false).put("nativeAccentPixels", countColor(image, accent))
        } finally { image.recycle() }
    }

    private fun scenes(): List<Pair<String, List<JSONObject>>> = listOf(
        "membership" to listOf(representation("""{"points":[{"id":"A","x":0,"y":0,"labels":["A"]},{"id":"B","x":2,"y":0,"labels":["B"]},{"id":"C","x":0,"y":1,"labels":["C"]}],"objects":[{"id":"r","kind":"line","through":["A","B"],"label":"r","contains":["A","B"]},{"id":"s","kind":"line","through":["A","C"],"label":"s","contains":["A","C"]}]}""")),
        "names" to listOf(
            representation("""{"points":[{"id":"A","x":-1,"y":0,"labels":["A"]},{"id":"B","x":1,"y":0,"labels":["B"]}],"objects":[{"id":"r","kind":"line","through":["A","B"],"label":"r","contains":["A","B"]}]}"""),
            representation("""{"points":[{"id":"P","x":0,"y":0,"labels":["A","B","P"]}],"objects":[{"id":"r","kind":"line","origin":"P","direction":{"x":1,"y":0},"label":"r","contains":["P"]},{"id":"s","kind":"line","origin":"P","direction":{"x":1,"y":1},"label":"s","contains":["P"]}]}""")),
        "limits" to listOf("line", "ray", "segment").map { kind -> representation("""{"points":[{"id":"A","x":-1,"y":0,"labels":["A"]},{"id":"B","x":1,"y":0,"labels":["B"]}],"objects":[{"id":"object","kind":"$kind","through":["A","B"],"label":"${when (kind) { "line" -> "r"; "ray" -> "AB"; else -> "AB" }}","contains":["A","B"]}]}""") }
    )
    private fun representation(config: String) = JSONObject().put("type", "euclidean_diagram").put("config", JSONObject(config))
    private fun block(scene: String, representations: List<JSONObject>): JSONObject {
        val correct = when (scene) {
            "membership" -> "B pertenece a r y no pertenece a s."
            "names" -> "A y B distintos fijan una recta; los nombres A y B del mismo P no fijan una dirección única."
            else -> "La recta tiene dos prolongaciones, la semirrecta conserva su origen y el segmento tiene dos extremos."
        }
        val wrong = when (scene) {
            "membership" -> "B pertenece a s y no pertenece a r."
            "names" -> "Contar dos nombres siempre equivale a contar dos posiciones distintas."
            else -> "El borde visible da dos extremos a los tres objetos."
        }
        return JSONObject().put("type", "math_puzzle").put("schemaVersion", 1).put("id", "qa_euclidean_$scene")
            .put("prompt", "Verificación técnica de lectura del dibujo y sus condiciones declaradas.")
            .put("representations", JSONArray(representations)).put("tools", JSONArray())
            .put("responses", JSONArray().put(JSONObject().put("id", "qa_euclidean_${scene}__choice")
                .put("type", "math_multiple_choice_single").put("options", JSONArray()
                    .put(JSONObject().put("id", "correct").put("label", correct))
                    .put(JSONObject().put("id", "wrong").put("label", wrong)))
                .put("validator", JSONObject().put("type", "choice").put("expected", JSONArray(listOf("correct"))))))
            .put("feedback", JSONObject().put("correct", "Condiciones conservadas.").put("incorrect", "Revisa las condiciones declaradas."))
    }

    private fun measureStandalone(view: View) {
        val density = context.resources.displayMetrics.density
        val width = (360 * density).toInt()
        val height = (300 * density).toInt()
        view.measure(View.MeasureSpec.makeMeasureSpec(width, View.MeasureSpec.EXACTLY), View.MeasureSpec.makeMeasureSpec(height, View.MeasureSpec.EXACTLY))
        view.layout(0, 0, width, height)
    }
    private fun countColor(bitmap: Bitmap, color: Int): Int = (0 until bitmap.height).sumOf { y -> (0 until bitmap.width).count { x -> bitmap.getPixel(x, y) == color } }
    private fun assertColorNear(image: Bitmap, position: EuclideanPosition, color: Int, tolerance: Int, message: String) =
        assertTrue(message, colorNear(image, position, color, tolerance))
    private fun colorNear(image: Bitmap, position: EuclideanPosition, color: Int, tolerance: Int): Boolean =
        (max(0, position.y.toInt() - tolerance)..min(image.height - 1, position.y.toInt() + tolerance)).any { y ->
            (max(0, position.x.toInt() - tolerance)..min(image.width - 1, position.x.toInt() + tolerance)).any { x ->
                val pixel = image.getPixel(x, y)
                abs(Color.red(pixel) - Color.red(color)) < 25 && abs(Color.green(pixel) - Color.green(color)) < 25 && abs(Color.blue(pixel) - Color.blue(color)) < 25
            }
        }
    private fun distance(first: EuclideanPosition, second: EuclideanPosition) = hypot(first.x - second.x, first.y - second.y)
    private fun segmentDistance(point: EuclideanPosition, first: EuclideanPosition, second: EuclideanPosition): Double {
        val dx = second.x - first.x
        val dy = second.y - first.y
        val t = (((point.x - first.x) * dx + (point.y - first.y) * dy) / (dx * dx + dy * dy)).coerceIn(0.0, 1.0)
        return distance(point, EuclideanPosition(first.x + t * dx, first.y + t * dy))
    }

    companion object {
        private val ACCENT = Color.rgb(255, 166, 70)
    }
}
