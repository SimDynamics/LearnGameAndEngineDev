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
import android.widget.CheckBox
import android.widget.ImageButton
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.learngraphicsprogramming.app.R
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.hypot
import kotlin.math.max
import kotlin.math.min
import kotlin.math.sin
import kotlin.math.sqrt

/** Only QA-owned, in-memory representations use the extended Euclidean renderer. No lesson,
 * answer, index, physical-device configuration or production preference is edited. */
@RunWith(AndroidJUnit4::class)
class EuclideanCircularDiagramDeviceTest : MathGeometryQaSupport("com.learngraphicsprogramming.app.realdownloadsqa") {
    private val background: Int get() = context.getColor(R.color.lesson_canvas)
    @Before fun requireLdPlayerArchitecture() {
        assertTrue("Run this suite only on the explicitly selected LDPlayer; never Honor",
            Build.SUPPORTED_ABIS.any { it == "x86" || it == "x86_64" })
    }

    @Test fun nativeCircularPrimitivesKeepUniformGeometryPaintAndFullLabelsAtThreeSizes() {
        instrumentation.runOnMainSync {
            for ((name, spec) in scenes()) {
                for ((widthDp, heightDp, scale) in listOf(Triple(280, 210, 1f), Triple(600, 360, 1f), Triple(320, 250, 1.5f))) {
                    val rendered = MathRepresentationRenderer.render(context, spec, ACCENT)
                    val visual = descendants(rendered).filterIsInstance<MathEuclideanDiagramView>().single()
                    visual.scaleMathText(scale)
                    measure(visual, widthDp, heightDp)
                    val metadata = assertNativeGeometry(visual).put("scene", name).put("widthDp", widthDp)
                        .put("heightDp", heightDp).put("textScale", scale).put("fixtureOnly", true)
                    if (scale > 1f) {
                        val bitmap = draw(visual)
                        try { saveBitmap(bitmap, "circular-$name-large-canvas.png") } finally { bitmap.recycle() }
                        saveMetadata("circular-$name-large-canvas", metadata)
                    }
                }
            }
        }
    }

    @Test fun longCircularLabelWrapsWithRealPaintWithoutDroppingCharactersAtAllThreeSizes() {
        instrumentation.runOnMainSync {
            val spec = representation("""{"points":[{"id":"O","x":0,"y":0,"labels":["O"]}],"circles":[{"id":"c","center":"O","radius":2,"contains":[],"label":"Circunferencia de referencia sin clasificación anticipada"}]}""")
            for ((widthDp, heightDp, scale) in listOf(Triple(280, 210, 1f), Triple(600, 360, 1f), Triple(320, 250, 1.5f))) {
                val visual = descendants(MathRepresentationRenderer.render(context, spec, ACCENT))
                    .filterIsInstance<MathEuclideanDiagramView>().single()
                visual.scaleMathText(scale)
                measure(visual, widthDp, heightDp)
                val metadata = assertNativeGeometry(visual).put("scene", "long-label")
                    .put("widthDp", widthDp).put("heightDp", heightDp).put("textScale", scale).put("fixtureOnly", true)
                val label = visual.drawingSnapshot().labels.single { it.ownerId == "c" }
                assertTrue("A long authored circle name really wraps at the measured native width", label.lines.size > 1)
                if (scale > 1f) {
                    val bitmap = draw(visual)
                    try { saveBitmap(bitmap, "circular-long-label-large-canvas.png") } finally { bitmap.recycle() }
                    saveMetadata("circular-long-label-large-canvas", metadata)
                }
            }
        }
    }

    @Test fun sevenScenesUseActualExpandedPanelInPortraitAndLandscapeWithLargeType() {
        for ((name, representation) in scenes()) {
            val fixture = fixture(choiceBlock(name, representation))
            withFixture(fixture) { scenario ->
                for ((orientation, label) in listOf(Configuration.ORIENTATION_PORTRAIT to "portrait",
                    Configuration.ORIENTATION_LANDSCAPE to "landscape")) {
                    orient(scenario, fixture, orientation)
                    lateinit var visual: MathEuclideanDiagramView
                    lateinit var inlineParent: ViewGroup
                    scenario.onActivity { activity ->
                        visual = descendants(content(activity)).filterIsInstance<MathEuclideanDiagramView>().single()
                        inlineParent = visual.parent as ViewGroup
                        if (orientation == Configuration.ORIENTATION_LANDSCAPE) visual.scaleMathText(1.5f)
                        val wrapper = visual.parent.parent as ViewGroup
                        scrollVisible(activity, wrapper)
                        descendants(wrapper).filterIsInstance<ImageButton>().single { it.tag == "math_expand_representation" }.performClick()
                    }
                    awaitWindow(visual, inlineParent, true)
                    lateinit var metadata: JSONObject
                    instrumentation.runOnMainSync {
                        assertTrue("Expanded panel reuses the same native renderer", visual.isAttachedToWindow)
                        metadata = assertNativeGeometry(visual).put("scene", name).put("orientation", label)
                            .put("textScale", if (orientation == Configuration.ORIENTATION_LANDSCAPE) 1.5 else 1.0)
                            .put("fixtureFingerprint", fixture.fingerprint).put("fixtureOnly", true)
                    }
                    capture("circular-$name-$label", { visual }, metadata)
                    instrumentation.runOnMainSync {
                        descendants(visual.rootView).filterIsInstance<ImageButton>().single { it.tag == "math_exit_representation" }.performClick()
                    }
                    awaitWindow(visual, inlineParent, false)
                }
            }
        }
    }

    @Test fun diameterRadiiAndSecantUseTheSameScaleAndKeepExplicitMembershipAndTwoArrows() {
        instrumentation.runOnMainSync {
            val diameter = native(scenes().single { it.first == "diameter" }.second)
            val snapshot = diameter.drawingSnapshot()
            val circle = snapshot.circles.single()
            val path = snapshot.paths.single()
            assertEquals("A complete diameter has exactly twice the drawn radius", 2 * circle.radius,
                distance(path.start, path.end), 0.00001)
            assertEquals("The declared center lies halfway along the diameter", circle.center.x, (path.start.x + path.end.x) / 2, 0.00001)
            assertEquals(circle.center.y, (path.start.y + path.end.y) / 2, 0.00001)

            val radii = native(scenes().single { it.first == "radii" }.second).drawingSnapshot()
            radii.paths.forEach { assertEquals("Each radius has the same drawing scale in all directions",
                radii.circles.single().radius, distance(it.start, it.end), 0.00001) }

            val secant = native(scenes().single { it.first == "chord_secant" }.second)
            val crossing = secant.drawingSnapshot()
            val straight = crossing.paths.single { it.kind == EuclideanObjectKind.LINE }
            assertEquals(2, crossing.arrows.count { it.objectId == straight.id })
            assertTrue(secant.diagram.contains("c", "A") && secant.diagram.contains("c", "B"))
            assertFalse(secant.diagram.contains("c", "P"))
            assertFalse(secant.diagram.contains("c", "O"))
            assertTrue(listOf("A", "B", "P", "Q").all { secant.diagram.contains("secant", it) })
            assertEquals(0, crossing.arrows.count { it.objectId == "chord" })
            assertNativeGeometry(secant)
        }
    }

    @Test fun filledDiskPaintsItsInteriorAndKeepsBoundaryInteriorAndExteriorMarksDistinct() {
        instrumentation.runOnMainSync {
            val view = native(scenes().single { it.first == "disk" }.second)
            val snapshot = view.drawingSnapshot()
            val circle = snapshot.circles.single()
            assertTrue(circle.filled)
            assertTrue(view.diagram.contains("c", "A"))
            assertFalse(view.diagram.contains("c", "O"))
            assertFalse(view.diagram.contains("c", "I"))
            assertFalse(view.diagram.contains("c", "E"))
            val image = draw(view)
            try {
                val density = context.resources.displayMetrics.density
                fun unobscured(point: EuclideanPosition): Boolean = point.x >= 1 && point.y >= 1 &&
                    point.x < image.width - 1 && point.y < image.height - 1 &&
                    snapshot.labels.none { point.x >= it.bounds.left - 2 * density && point.x <= it.bounds.right + 2 * density &&
                        point.y >= it.bounds.top - 2 * density && point.y <= it.bounds.bottom + 2 * density } &&
                    snapshot.points.none { distance(point, it.center) <= 8 * density }
                val innerSamples = listOf(.3, .45, .6, .75).flatMap { fraction ->
                    (0 until 360 step 10).map { polar(circle.center, circle.radius * fraction, it.toDouble()) }
                }.filter(::unobscured)
                val outerSamples = (0 until 360 step 10).map { polar(circle.center, circle.radius * 1.15, it.toDouble()) }
                    .filter(::unobscured)
                assertTrue("Actual label/mark masks still leave substantial disk interior to verify", innerSamples.size >= 30)
                assertTrue("Actual label/mark masks leave exterior samples to verify", outerSamples.size >= 10)
                val fill = Color.rgb((Color.red(ACCENT) * 42 + Color.red(background) * 213 + 127) / 255,
                    (Color.green(ACCENT) * 42 + Color.green(background) * 213 + 127) / 255,
                    (Color.blue(ACCENT) * 42 + Color.blue(background) * 213 + 127) / 255)
                innerSamples.forEach { point ->
                    val actual = image.getPixel(point.x.toInt(), point.y.toInt())
                    assertTrue("Every unobscured interior sample has the actual translucent disk fill",
                        abs(Color.red(actual) - Color.red(fill)) <= 1 && abs(Color.green(actual) - Color.green(fill)) <= 1 &&
                            abs(Color.blue(actual) - Color.blue(fill)) <= 1)
                }
                outerSamples.forEach { point -> assertEquals("The authored fill never covers the exterior", background,
                    image.getPixel(point.x.toInt(), point.y.toInt())) }
                saveMetadata("circular-disk-fill-samples", JSONObject().put("interiorSamples", innerSamples.size)
                    .put("exteriorSamples", outerSamples.size).put("expectedFillArgb", fill)
                    .put("excludedOcclusions", "Exact measured labels and visible point masks"))
                snapshot.points.forEach { assertColorNear(image, it.center, Color.WHITE, 2, "Visible mark ${it.id}") }
            } finally { image.recycle() }
            val labels = view.contentDescription.toString()
            assertFalse("Accessibility must not announce the answer by invented classifications",
                listOf("interior", "exterior", "frontera", "pertenece").any { labels.contains(it, true) })
        }
    }

    @Test fun arcsWithTheSameEndpointsFollowTheirOwnIntermediatePointAndNeverPaintAChordOrSector() {
        instrumentation.runOnMainSync {
            val view = native(scenes().single { it.first == "arcs" }.second)
            val snapshot = view.drawingSnapshot()
            val arcs = snapshot.arcs
            assertEquals(2, arcs.size)
            assertEquals("Same endpoints remain distinct arc objects", setOf("upper", "lower"), arcs.map { it.id }.toSet())
            assertEquals(-arcs[0].sweepDegrees, arcs[1].sweepDegrees, .00001)
            assertEquals(180.0, abs(arcs[0].sweepDegrees), .00001)
            assertTrue(view.diagram.contains("upper", "B"))
            assertFalse(view.diagram.contains("upper", "D"))
            assertTrue(view.diagram.contains("lower", "D"))
            assertFalse(view.diagram.contains("lower", "B"))
            assertTrue(snapshot.paths.isEmpty())
            val image = draw(view)
            try {
                // Sample away from the authored center mark and its label. A drawArc with
                // useCenter=true or a straight endpoint chord would paint these locations.
                val circle = snapshot.circles.single()
                listOf(EuclideanPosition(circle.center.x - circle.radius * .55, circle.center.y),
                    EuclideanPosition(circle.center.x + circle.radius * .55, circle.center.y)).forEach {
                    assertEquals("An arc never invents a diameter/chord or filled sector", background,
                        image.getPixel(it.x.toInt(), it.y.toInt()))
                }
            } finally { image.recycle() }
            assertNativeGeometry(view)
        }
    }

    @Test fun copiedUnindexedDraftKeepsItsResponsesAndRestoresDraftWrongAndCorrectStatesWithInjectedDiagram() {
        val source = context.assets.open("content/es/math_physics/math_geometria_plana/fundamentos_geometricos/lesson_circunferencia_circulo_y_radios.json")
            .bufferedReader(Charsets.UTF_8).use { JSONObject(it.readText()) }
        val blocks = source.getJSONArray("pages").getJSONObject(0).getJSONArray("blocks")
        val original = (0 until blocks.length()).map { blocks.getJSONObject(it) }.single { it.optString("type") == "math_puzzle" }
        val responsesBefore = original.getJSONArray("responses").toString()
        val options = original.getJSONArray("responses").getJSONObject(0).getJSONArray("options")
        val labelsById = (0 until options.length()).map { options.getJSONObject(it) }.associate { it.getString("id") to it.getString("label") }
        fun choice(activity: com.learngraphicsprogramming.app.LessonActivity, id: String): CheckBox =
            descendants(content(activity)).filterIsInstance<CheckBox>().single { it.contentDescription.toString() == labelsById.getValue(id) }
        val copy = JSONObject(original.toString()).put("representations", JSONArray().put(draftCircle()))
        val fixture = fixture(copy)
        assertEquals("QA adds only an in-memory diagram, not new response IDs or answers", responsesBefore, fixture.block.getJSONArray("responses").toString())
        withFixture(fixture) { scenario ->
            scenario.onActivity { activity ->
                val choices = descendants(content(activity)).filterIsInstance<CheckBox>()
                assertEquals(4, choices.size)
                choice(activity, "a").performClick()
                assertState(activity, fixture, false, MathAnswer.Choice(setOf("a")))
            }
            scenario.recreate()
            scenario.onActivity { inject(it, fixture) }
            awaitLayout(scenario, fixture)
            scenario.onActivity { activity ->
                assertState(activity, fixture, false, MathAnswer.Choice(setOf("a")))
                assertTrue(choice(activity, "a").isChecked)
                check(activity).performClick()
                assertState(activity, fixture, false, MathAnswer.Choice(setOf("a")))
            }
            scenario.recreate()
            scenario.onActivity { inject(it, fixture) }
            awaitLayout(scenario, fixture)
            scenario.onActivity { activity ->
                assertState(activity, fixture, false, MathAnswer.Choice(setOf("a")))
                choice(activity, "b").performClick()
                check(activity).performClick()
                assertState(activity, fixture, true, MathAnswer.Choice(setOf("a", "b")))
            }
            scenario.recreate()
            scenario.onActivity { inject(it, fixture) }
            awaitLayout(scenario, fixture)
            scenario.onActivity { activity ->
                assertState(activity, fixture, true, MathAnswer.Choice(setOf("a", "b")))
                assertTrue(descendants(content(activity)).filterIsInstance<CheckBox>().all { !it.isEnabled })
                assertNativeGeometry(descendants(content(activity)).filterIsInstance<MathEuclideanDiagramView>().single())
            }
        }
        assertEquals("The asset and its original answer contract remain untouched", responsesBefore, original.getJSONArray("responses").toString())
    }

    private fun assertNativeGeometry(view: MathEuclideanDiagramView): JSONObject {
        val nativeAccent = MathEuclideanDiagramView::class.java.getDeclaredField("accent").apply { isAccessible = true }.getInt(view)
        val drawing = view.drawingSnapshot()
        val expectedLabels = view.diagram.points.flatMap { it.labels } + view.diagram.objects.mapNotNull { it.label } +
            view.diagram.circles.mapNotNull { it.label } + view.diagram.arcs.mapNotNull { it.label }
        assertEquals("Only authored labels appear; no axes, ticks, measurements or answers are generated",
            expectedLabels.sorted(), drawing.labels.map { it.text }.sorted())
        val paint = Paint(Paint.ANTI_ALIAS_FLAG)
        val density = context.resources.displayMetrics.density.toDouble()
        val metadata = JSONObject().put("width", view.width).put("height", view.height)
        val labels = JSONArray()
        drawing.labels.forEach { label ->
            assertEquals(label.text, label.lines.joinToString(""))
            assertTrue("Complete label ${label.text} stays inside the native viewport", label.bounds.left >= 0 && label.bounds.top >= 0 &&
                label.bounds.right <= view.width && label.bounds.bottom <= view.height)
            // Resolve text size from the same actual Canvas Paint used for this snapshot.
            val nativePaint = MathEuclideanDiagramView::class.java.getDeclaredField("paint").apply { isAccessible = true }.get(view) as Paint
            paint.textSize = nativePaint.textSize
            assertEquals("Real Paint glyph widths fit the complete authored label", label.lines.maxOf { paint.measureText(it) }.toDouble() + 4 * density,
                label.bounds.width, .05)
            labels.put(JSONObject().put("ownerId", label.ownerId).put("text", label.text)
                .put("bounds", JSONArray(listOf(label.bounds.left, label.bounds.top, label.bounds.right, label.bounds.bottom))))
        }
        for (a in drawing.labels.indices) for (b in a + 1 until drawing.labels.size) {
            assertFalse("Two complete labels must not overlap: ${drawing.labels[a].text}/${drawing.labels[b].text}",
                drawing.labels[a].bounds.overlaps(drawing.labels[b].bounds))
        }
        val image = draw(view)
        try {
            drawing.labels.forEach { label ->
                val glyphs = (max(0, label.bounds.top.toInt()) until min(image.height, label.bounds.bottom.toInt())).sumOf { y ->
                    (max(0, label.bounds.left.toInt()) until min(image.width, label.bounds.right.toInt())).count { x ->
                        val color = image.getPixel(x, y)
                        Color.red(color) > 210 && Color.green(color) > 210 && Color.blue(color) > 210
                    }
                }
                assertTrue("Native Canvas really paints the full label ${label.text}", glyphs > 3)
            }
            drawing.circles.forEach { circle ->
                assertTrue(circle.radius > 0)
                assertTrue(circle.center.x - circle.radius >= 0 && circle.center.x + circle.radius <= view.width)
                assertTrue(circle.center.y - circle.radius >= 0 && circle.center.y + circle.radius <= view.height)
                assertTrue("Circle ink follows one radius on both pixel axes; labels may mask isolated points",
                    listOf(15.0, 45.0, 75.0, 105.0, 135.0, 165.0, 195.0, 225.0, 255.0, 285.0, 315.0, 345.0)
                        .count { colorNear(image, polar(circle.center, circle.radius, it), nativeAccent, 3) } >= 6)
            }
            drawing.arcs.forEach { arc ->
                assertTrue("The actual highlighted arc follows its directed sweep",
                    listOf(.15, .30, .55, .75, .90).count { fraction -> colorNear(image,
                        polar(arc.center, arc.radius, arc.startDegrees + arc.sweepDegrees * fraction), nativeAccent, 3) } >= 3)
            }
            drawing.arrows.forEach { arrow ->
                assertColorNear(image, (arrow.tip + arrow.wingA) * .5, nativeAccent, 3, "Native secant continuation arrow")
                assertColorNear(image, (arrow.tip + arrow.wingB) * .5, nativeAccent, 3, "Native secant continuation arrow")
            }
            val inkTolerance = 7 * density
            for (y in 0 until image.height step 5) for (x in 0 until image.width step 5) {
                if (image.getPixel(x, y) == background) continue
                val pixel = EuclideanPosition(x.toDouble(), y.toDouble())
                val authoredInk = drawing.labels.any { x >= kotlin.math.floor(it.bounds.left) - 1 && x <= kotlin.math.ceil(it.bounds.right) + 1 &&
                    y >= kotlin.math.floor(it.bounds.top) - 1 && y <= kotlin.math.ceil(it.bounds.bottom) + 1 } ||
                    drawing.points.any { distance(pixel, it.center) <= inkTolerance } ||
                    drawing.paths.any { segmentDistance(pixel, it.start, it.end) <= inkTolerance } ||
                    drawing.arrows.any { segmentDistance(pixel, it.tip, it.wingA) <= inkTolerance || segmentDistance(pixel, it.tip, it.wingB) <= inkTolerance } ||
                    drawing.circles.any { if (it.filled) distance(pixel, it.center) <= it.radius + inkTolerance
                        else abs(distance(pixel, it.center) - it.radius) <= inkTolerance }
                assertTrue("All native ink belongs to authored geometry; no axes, grid, coordinates or scales at $x,$y", authoredInk)
            }
            assertEquals("Euclidean circles never introduce the Cartesian grid", 0, countColor(image, Color.rgb(51, 56, 65)))
        } finally { image.recycle() }
        return metadata.put("labels", labels).put("circles", JSONArray(drawing.circles.map {
            JSONObject().put("id", it.id).put("center", JSONArray(listOf(it.center.x, it.center.y))).put("radiusPixels", it.radius).put("filled", it.filled)
        })).put("arcs", JSONArray(drawing.arcs.map {
            JSONObject().put("id", it.id).put("circleId", it.circleId).put("startDegrees", it.startDegrees).put("sweepDegrees", it.sweepDegrees)
        })).put("pathCount", drawing.paths.size).put("arrowCount", drawing.arrows.size).put("axesGridCoordinatesVisible", false)
    }

    private fun scenes(): List<Pair<String, JSONObject>> {
        val root = sqrt(3.0)
        return listOf(
            "radii" to representation("""{"points":[{"id":"O","x":0,"y":0,"labels":["O"]},{"id":"A","x":2,"y":0,"labels":["A"]},{"id":"B","x":0,"y":2,"labels":["B"]},{"id":"C","x":-2,"y":0,"labels":["C"]},{"id":"D","x":0,"y":-2,"labels":["D"]}],"objects":[{"id":"OA","kind":"segment","through":["O","A"],"contains":["O","A"]},{"id":"OB","kind":"segment","through":["O","B"],"contains":["O","B"]},{"id":"OC","kind":"segment","through":["O","C"],"contains":["O","C"]},{"id":"OD","kind":"segment","through":["O","D"],"contains":["O","D"]}],"circles":[{"id":"c","center":"O","radius":2,"contains":["A","B","C","D"],"label":"c"}]}"""),
            "diameter" to representation("""{"points":[{"id":"O","x":0,"y":0,"labels":["O"]},{"id":"A","x":-2,"y":0,"labels":["A"]},{"id":"B","x":2,"y":0,"labels":["B"]}],"objects":[{"id":"diameter","kind":"segment","through":["A","B"],"contains":["A","O","B"],"label":"d"}],"circles":[{"id":"c","center":"O","radius":2,"contains":["A","B"],"label":"c"}]}"""),
            "chord_secant" to representation("""{"points":[{"id":"O","x":0,"y":0,"labels":["O"]},{"id":"A","x":${-root},"y":1,"labels":["A"]},{"id":"B","x":$root,"y":1,"labels":["B"]},{"id":"P","x":-3,"y":1,"labels":["P"]},{"id":"Q","x":3,"y":1,"labels":["Q"]}],"objects":[{"id":"chord","kind":"segment","through":["A","B"],"contains":["A","B"],"label":"q"},{"id":"secant","kind":"line","through":["P","Q"],"contains":["A","B","P","Q"],"label":"s"}],"circles":[{"id":"c","center":"O","radius":2,"contains":["A","B"],"label":"c"}]}"""),
            "disk" to representation("""{"points":[{"id":"O","x":0,"y":0,"labels":["O"]},{"id":"A","x":2,"y":0,"labels":["A"]},{"id":"I","x":0.5,"y":0.5,"labels":["I"]},{"id":"E","x":3,"y":0,"labels":["E"]}],"circles":[{"id":"c","center":"O","radius":2,"contains":["A"],"filled":true,"label":"c"}]}"""),
            "arcs" to representation("""{"points":[{"id":"O","x":0,"y":0,"labels":["O"]},{"id":"A","x":2,"y":0,"labels":["A"]},{"id":"B","x":0,"y":2,"labels":["B"]},{"id":"C","x":-2,"y":0,"labels":["C"]},{"id":"D","x":0,"y":-2,"labels":["D"]}],"circles":[{"id":"c","center":"O","radius":2,"contains":["A","B","C","D"],"label":"c"}],"arcs":[{"id":"upper","circle":"c","through":["A","B","C"],"contains":["A","B","C"],"label":"u"},{"id":"lower","circle":"c","through":["A","D","C"],"contains":["A","D","C"],"label":"v"}]}"""),
            "congruent" to representation("""{"points":[{"id":"O","x":-3,"y":0,"labels":["O"]},{"id":"P","x":3,"y":0,"labels":["P"]},{"id":"A","x":-1,"y":0,"labels":["A"]},{"id":"B","x":5,"y":0,"labels":["B"]}],"objects":[{"id":"OA","kind":"segment","through":["O","A"],"contains":["O","A"]},{"id":"PB","kind":"segment","through":["P","B"],"contains":["P","B"]}],"circles":[{"id":"c","center":"O","radius":2,"contains":["A"],"label":"c"},{"id":"d","center":"P","radius":2,"contains":["B"],"label":"d"}]}"""),
            "translated" to representation("""{"points":[{"id":"H","x":100,"y":200,"labels":["H"]},{"id":"A","x":100.01,"y":200,"labels":["A"]},{"id":"B","x":100,"y":200.01,"labels":["B"]}],"objects":[{"id":"HA","kind":"segment","through":["H","A"],"contains":["H","A"]},{"id":"HB","kind":"segment","through":["H","B"],"contains":["H","B"]}],"circles":[{"id":"c","center":"H","radius":0.01,"contains":["A","B"],"label":"c"}]}"""))
    }

    private fun draftCircle(): JSONObject = representation("""{"points":[{"id":"O","x":0,"y":0,"labels":["O"]},{"id":"A","x":${5.0 / 3},"y":0,"labels":["A"]},{"id":"B","x":0,"y":${5.0 / 3},"labels":["B"]},{"id":"C","x":-1.5,"y":0,"labels":["C"]},{"id":"D","x":0,"y":-1.75,"labels":["D"]}],"circles":[{"id":"c","center":"O","radius":${5.0 / 3},"contains":["A","B"],"label":"c"}]}""")
    private fun representation(config: String): JSONObject {
        val content = JSONObject(config)
        if (!content.has("objects")) content.put("objects", JSONArray())
        return JSONObject().put("type", "euclidean_diagram").put("config", content)
    }
    private fun choiceBlock(name: String, representation: JSONObject) = JSONObject().put("type", "math_puzzle").put("schemaVersion", 1)
        .put("id", "qa_circular_$name").put("prompt", "Verificación técnica aislada de objetos geométricos declarados.")
        .put("representations", JSONArray().put(representation)).put("tools", JSONArray()).put("responses", JSONArray().put(JSONObject()
            .put("id", "qa_circular_${name}__choice").put("type", "math_multiple_choice_single")
            .put("options", JSONArray().put(JSONObject().put("id", "yes").put("label", "Conservar los datos declarados."))
                .put(JSONObject().put("id", "no").put("label", "Inferir respuestas midiendo píxeles.")))
            .put("validator", JSONObject().put("type", "choice").put("expected", JSONArray(listOf("yes"))))))
        .put("feedback", JSONObject().put("correct", "Datos conservados.").put("incorrect", "Revisa los datos declarados."))

    private fun native(spec: JSONObject): MathEuclideanDiagramView = descendants(MathRepresentationRenderer.render(context, spec, ACCENT))
        .filterIsInstance<MathEuclideanDiagramView>().single().also { measure(it, 400, 300) }
    private fun measure(view: View, widthDp: Int, heightDp: Int) {
        val density = context.resources.displayMetrics.density
        val width = (widthDp * density).toInt()
        val height = (heightDp * density).toInt()
        view.measure(View.MeasureSpec.makeMeasureSpec(width, View.MeasureSpec.EXACTLY), View.MeasureSpec.makeMeasureSpec(height, View.MeasureSpec.EXACTLY))
        view.layout(0, 0, width, height)
    }
    private fun awaitWindow(visual: MathEuclideanDiagramView, inlineParent: ViewGroup, expanded: Boolean) {
        val deadline = SystemClock.uptimeMillis() + 8_000
        var count = 0
        var previous = ""
        while (SystemClock.uptimeMillis() < deadline) {
            instrumentation.waitForIdleSync()
            instrumentation.runOnMainSync {
                val bounds = Rect()
                val parent = visual.parent as? ViewGroup
                val correct = if (expanded) parent?.tag == "math_expanded_visual_container" else parent === inlineParent
                val ready = correct && visual.width > 0 && visual.height > 0 && !visual.isLayoutRequested &&
                    visual.hasWindowFocus() && visual.getGlobalVisibleRect(bounds)
                val state = "${visual.width}x${visual.height}/$bounds/${parent?.tag}"
                count = if (ready) { if (state == previous) count + 1 else 1 } else 0
                previous = state
            }
            if (count >= 3) return
            SystemClock.sleep(50)
        }
        fail("The actual expanded native diagram window did not settle (expanded=$expanded)")
    }
    private fun polar(center: EuclideanPosition, radius: Double, degrees: Double): EuclideanPosition {
        val radians = Math.toRadians(degrees)
        return EuclideanPosition(center.x + radius * cos(radians), center.y + radius * sin(radians))
    }
    private fun countColor(image: Bitmap, color: Int): Int = (0 until image.height).sumOf { y -> (0 until image.width).count { x -> image.getPixel(x, y) == color } }
    private fun distance(first: EuclideanPosition, second: EuclideanPosition) = hypot(first.x - second.x, first.y - second.y)
    private fun segmentDistance(point: EuclideanPosition, first: EuclideanPosition, second: EuclideanPosition): Double {
        val direction = second - first
        val t = (((point.x - first.x) * direction.x + (point.y - first.y) * direction.y) /
            (direction.x * direction.x + direction.y * direction.y)).coerceIn(0.0, 1.0)
        return distance(point, first + direction * t)
    }
    private fun assertColorNear(image: Bitmap, point: EuclideanPosition, color: Int, tolerance: Int, message: String) = assertTrue(message, colorNear(image, point, color, tolerance))
    private fun colorNear(image: Bitmap, point: EuclideanPosition, color: Int, tolerance: Int): Boolean =
        (max(0, point.y.toInt() - tolerance)..min(image.height - 1, point.y.toInt() + tolerance)).any { y ->
            (max(0, point.x.toInt() - tolerance)..min(image.width - 1, point.x.toInt() + tolerance)).any { x ->
                val pixel = image.getPixel(x, y)
                abs(Color.red(pixel) - Color.red(color)) < 25 && abs(Color.green(pixel) - Color.green(color)) < 25 && abs(Color.blue(pixel) - Color.blue(color)) < 25
            }
        }

    companion object {
        private val ACCENT = Color.rgb(255, 166, 70)
    }
}
