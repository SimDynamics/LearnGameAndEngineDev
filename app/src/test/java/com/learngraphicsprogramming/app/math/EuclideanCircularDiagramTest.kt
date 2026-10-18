package com.learngraphicsprogramming.app.math

import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.hypot
import kotlin.math.sin
import kotlin.math.sqrt

/** Circular scenes exercise the same ID declarations and pure layout as the native Canvas. */
class EuclideanCircularDiagramTest {
    @Test fun optionalCircularArraysPreserveTheExistingLinearContract() {
        val legacy = JSONObject("""{"points":[{"id":"A","x":0,"y":0,"labels":["A"]},{"id":"B","x":2,"y":0,"labels":["B"]}],"objects":[{"id":"s","kind":"segment","through":["A","B"],"contains":["A","B"]}]}""")
        val original = EuclideanDiagram.parse(legacy)
        val explicit = EuclideanDiagram.parse(copy(legacy).put("circles", JSONArray()).put("arcs", JSONArray()))
        assertEquals(original, explicit)
        assertTrue(original.circles.isEmpty())
        assertTrue(original.arcs.isEmpty())
        assertEquals(
            EuclideanDiagramLayout.layout(original, 480.0, 320.0),
            EuclideanDiagramLayout.layout(explicit, 480.0, 320.0)
        )
        assertEquals(original, EuclideanDiagram.parse(JSONObject().put("config", legacy)))
    }

    @Test fun aFilledDiskDoesNotDeclareItsInteriorOrCenterToBeOnTheCircumference() {
        val boundary = EuclideanDiagram.parse(relations())
        val disk = EuclideanDiagram.parse(relations().apply { getJSONArray("circles").getJSONObject(0).put("filled", true) })
        assertFalse(boundary.circles.single().filled)
        assertTrue(disk.circles.single().filled)
        assertEquals("O", disk.circles.single().centerPointId)
        assertEquals(EuclideanPosition(0.0, 0.0), disk.circles.single().center)
        assertEquals(2.0, disk.circles.single().radius, 0.0)
        for (id in listOf("A", "B", "C", "D")) {
            assertTrue(boundary.contains("k", id))
            assertTrue(disk.contains("k", id))
        }
        for (id in listOf("O", "I", "E")) {
            assertFalse(boundary.contains("k", id))
            assertFalse(disk.contains("k", id))
        }
        assertEquals(boundary.circles.single().contains, disk.circles.single().contains)
        assertEquals(disk, EuclideanDiagram.parse(JSONObject().put("config", relations().apply {
            getJSONArray("circles").getJSONObject(0).put("filled", true)
        })))
    }

    @Test fun radiiMustBeFinitePositiveNumbersWithinThePublishedInternalRange() {
        for (radius in listOf<Any>(0, -2, 0.0000000001, 1_000_001, "2", JSONObject.NULL)) {
            rejects(circleOnly().apply { getJSONArray("circles").getJSONObject(0).put("radius", radius) })
        }
        rejects(circleOnly().apply { getJSONArray("circles").getJSONObject(0).remove("radius") })
        for (radius in listOf(1e-9, 1.0, 1_000_000.0)) {
            assertEquals(radius, EuclideanDiagram.parse(circleOnly(radius)).circles.single().radius, 0.0)
        }
    }

    @Test fun circlesRejectContradictoryIncidencesAndUnknownReferences() {
        rejects(relations().apply { getJSONArray("circles").getJSONObject(0).put("center", "absent") })
        rejects(relations().apply { getJSONArray("circles").getJSONObject(0).remove("center") })
        rejects(relations().apply { getJSONArray("circles").getJSONObject(0).remove("contains") })
        rejects(relations().apply { getJSONArray("circles").getJSONObject(0).getJSONArray("contains").put("O") })
        rejects(relations().apply { getJSONArray("circles").getJSONObject(0).getJSONArray("contains").put("I") })
        rejects(relations().apply { getJSONArray("circles").getJSONObject(0).getJSONArray("contains").put("E") })
        rejects(relations().apply { getJSONArray("circles").getJSONObject(0).getJSONArray("contains").put("unknown") })
        rejects(relations().apply { getJSONArray("circles").getJSONObject(0).getJSONArray("contains").remove(0) })
        rejects(relations().apply { getJSONArray("circles").getJSONObject(0).getJSONArray("contains").put("A") })
        rejects(relations().apply { getJSONArray("circles").getJSONObject(0).put("radius", 3.0) })
        rejects(relations().apply { getJSONArray("circles").getJSONObject(0).put("filled", "true") })
    }

    @Test fun pointsLinesCirclesAndArcsShareOneStableIdNamespace() {
        rejects(relations().apply { getJSONArray("circles").getJSONObject(0).put("id", "O") })
        rejects(relations().apply { getJSONArray("circles").getJSONObject(0).put("id", "radius") })
        rejects(relations().apply { getJSONArray("circles").put(copy(getJSONArray("circles").getJSONObject(0))) })
        rejects(semicircles().apply { getJSONArray("arcs").getJSONObject(0).put("id", "A") })
        rejects(semicircles().apply { getJSONArray("arcs").getJSONObject(0).put("id", "k") })
        rejects(semicircles().apply { getJSONArray("arcs").getJSONObject(1).put("id", "upper") })
        rejects(circleOnly().apply { getJSONArray("circles").getJSONObject(0).put("id", "not an ID") })
        rejects(semicircles().apply { getJSONArray("arcs").getJSONObject(0).put("label", "\n") })
        rejects(circleOnly().apply { getJSONArray("circles").getJSONObject(0).put("label", "") })
        rejects(circleOnly().apply { getJSONArray("circles").put(copy(getJSONArray("circles").getJSONObject(0)).put("id", "another")) })
    }

    @Test fun unknownMembershipQueriesCannotSilentlyInventGeometry() {
        val model = EuclideanDiagram.parse(semicircles())
        rejectsOperation { model.contains("absent", "A") }
        rejectsOperation { model.contains("k", "absent") }
        rejectsOperation { model.contains("upper", "absent") }
    }

    @Test fun circularArrayLimitsAndTypesAreValidatedBeforeDrawing() {
        rejects(circleOnly().put("circles", "k"))
        rejects(semicircles().put("arcs", JSONObject()))
        rejects(circleOnly().apply {
            val circles = getJSONArray("circles")
            repeat(8) { circles.put(copy(circles.getJSONObject(0)).put("id", "c$it")) }
        })
        rejects(semicircles().apply {
            val arcs = getJSONArray("arcs")
            repeat(15) { arcs.put(copy(arcs.getJSONObject(0)).put("id", "a$it")) }
        })
    }

    @Test fun equalRadiiAndADiameterUseOneScaleWhileAChordAndASecantKeepTheirOwnLimits() {
        val model = EuclideanDiagram.parse(relations())
        val drawing = EuclideanDiagramLayout.layout(model, 780.0, 480.0)
        val circle = drawing.circles.single()
        val points = drawing.points.associate { it.id to it.center }
        for (id in listOf("A", "B", "C", "D")) {
            assertEquals(circle.radius, distance(points.getValue("O"), points.getValue(id)), 1e-8)
        }
        assertEquals(2 * circle.radius, distance(points.getValue("A"), points.getValue("C")), 1e-8)
        val radius = drawing.paths.single { it.id == "radius" }
        assertPointEquals(points.getValue("O"), radius.start)
        assertPointEquals(points.getValue("A"), radius.end)
        val diameter = drawing.paths.single { it.id == "diameter" }
        assertPointEquals(points.getValue("A"), diameter.start)
        assertPointEquals(points.getValue("C"), diameter.end)
        assertTrue(model.contains("diameter", "O"))
        val chord = drawing.paths.single { it.id == "chord" }
        assertPointEquals(points.getValue("A"), chord.start)
        assertPointEquals(points.getValue("B"), chord.end)
        assertFalse(model.contains("chord", "O"))
        assertEquals(sqrt(2.0) * circle.radius, distance(chord.start, chord.end), 1e-8)
        val secant = drawing.paths.single { it.id == "secant" }
        assertTrue(secant.start.x > points.getValue("A").x)
        assertTrue(secant.end.x < points.getValue("C").x)
        assertEquals(2, drawing.arrows.count { it.objectId == "secant" })
        assertFalse(drawing.arrows.any { it.objectId in setOf("radius", "diameter", "chord") })
    }

    @Test fun aThirdPointSelectsEachComplementarySemicircleIncludingItsEndpoints() {
        val model = EuclideanDiagram.parse(semicircles())
        assertEquals(listOf("A", "B", "C"), model.arcs.single { it.id == "upper" }.throughPointIds)
        for (id in listOf("A", "C")) {
            assertTrue(model.contains("upper", id))
            assertTrue(model.contains("lower", id))
        }
        assertTrue(model.contains("upper", "B"))
        assertFalse(model.contains("upper", "D"))
        assertTrue(model.contains("lower", "D"))
        assertFalse(model.contains("lower", "B"))
        assertFalse(model.contains("upper", "O"))
        val drawing = EuclideanDiagramLayout.layout(model, 520.0, 360.0)
        val upper = drawing.arcs.single { it.id == "upper" }
        val lower = drawing.arcs.single { it.id == "lower" }
        assertEquals(180.0, abs(upper.sweepDegrees), 1e-8)
        assertEquals(180.0, abs(lower.sweepDegrees), 1e-8)
        assertTrue(upper.sweepDegrees * lower.sweepDegrees < 0)
        for (arc in drawing.arcs) {
            assertEquals("k", arc.circleId)
            assertPointEquals(drawing.points.single { it.id == "A" }.center, pointOn(arc, 0.0))
            assertPointEquals(drawing.points.single { it.id == "C" }.center, pointOn(arc, 1.0))
        }
        assertPointEquals(drawing.points.single { it.id == "B" }.center, pointOn(upper, .5))
        assertPointEquals(drawing.points.single { it.id == "D" }.center, pointOn(lower, .5))
    }

    @Test fun aNonDiameterArcCanSelectTheLongRouteWithoutInventingAngularLabels() {
        val spec = semicircles().apply {
            getJSONArray("points").put(point("P", sqrt(2.0), -sqrt(2.0)))
            getJSONArray("circles").getJSONObject(0).getJSONArray("contains").put("P")
            put("arcs", JSONArray().put(arc("major", listOf("A", "B", "D"), listOf("A", "B", "C", "D")))
                .put(arc("minor", listOf("A", "P", "D"), listOf("A", "P", "D"))))
        }
        val model = EuclideanDiagram.parse(spec)
        assertTrue(model.contains("major", "C"))
        assertFalse(model.contains("major", "P"))
        assertTrue(model.contains("minor", "P"))
        assertFalse(model.contains("minor", "C"))
        val drawing = EuclideanDiagramLayout.layout(model, 520.0, 360.0)
        assertEquals(270.0, abs(drawing.arcs.single { it.id == "major" }.sweepDegrees), 1e-8)
        assertEquals(90.0, abs(drawing.arcs.single { it.id == "minor" }.sweepDegrees), 1e-8)
        assertEquals(listOf("O", "A", "B", "C", "D", "P", "k", "major", "minor"), drawing.labels.map { it.text })
    }

    @Test fun arcsRejectUnknownRepeatedOffBoundaryOrContradictoryPoints() {
        rejects(semicircles().apply { getJSONArray("arcs").getJSONObject(0).put("circle", "absent") })
        rejects(semicircles().apply { getJSONArray("arcs").getJSONObject(0).put("through", JSONArray(listOf("A", "B"))) })
        rejects(semicircles().apply { getJSONArray("arcs").getJSONObject(0).put("through", JSONArray(listOf("A", "A", "C"))) })
        rejects(semicircles().apply { getJSONArray("arcs").getJSONObject(0).put("through", JSONArray(listOf("A", "O", "C"))) })
        rejects(semicircles().apply { getJSONArray("arcs").getJSONObject(0).put("through", JSONArray(listOf("A", "unknown", "C"))) })
        rejects(semicircles().apply { getJSONArray("arcs").getJSONObject(0).remove("contains") })
        rejects(semicircles().apply { getJSONArray("arcs").getJSONObject(0).getJSONArray("contains").put("D") })
        rejects(semicircles().apply { getJSONArray("arcs").getJSONObject(0).getJSONArray("contains").remove(1) })
        rejects(semicircles().apply { remove("circles") })
    }

    @Test fun circlesStayCircularAndIncidentAtPortraitLandscapeAndLargerTextSizes() {
        val model = EuclideanDiagram.parse(semicircles())
        for ((width, height) in listOf(320.0 to 360.0, 780.0 to 420.0)) {
            for (scale in listOf(1.0, 1.5)) {
                val drawing = EuclideanDiagramLayout.layout(model, width, height, 1.0, 15.0 * scale)
                val circle = drawing.circles.single()
                val points = drawing.points.associate { it.id to it.center }
                assertEquals(circle.center, points.getValue("O"))
                assertEquals(circle.radius, points.getValue("A").x - circle.center.x, 1e-8)
                assertEquals(circle.radius, circle.center.y - points.getValue("B").y, 1e-8)
                assertEquals(circle.radius, circle.center.x - points.getValue("C").x, 1e-8)
                assertEquals(circle.radius, points.getValue("D").y - circle.center.y, 1e-8)
                assertCircleInside(drawing, circle)
                drawing.arcs.forEach {
                    assertEquals(circle.center, it.center)
                    assertEquals(circle.radius, it.radius, 0.0)
                }
            }
        }
    }

    @Test fun circleExtentsParticipateInFittingEvenWhenOnlyTheirCentersAreDeclaredPoints() {
        val spec = JSONObject().put("points", JSONArray().put(point("O", -10.0, 5.0)).put(point("Q", 20.0, -10.0)))
            .put("objects", JSONArray())
            .put("circles", JSONArray().put(circle("first", "O", 2.0, emptyList())).put(circle("second", "Q", 6.0, emptyList())))
        for ((width, height) in listOf(320.0 to 440.0, 780.0 to 400.0)) {
            val drawing = EuclideanDiagramLayout.layout(EuclideanDiagram.parse(spec), width, height)
            drawing.circles.forEach { assertCircleInside(drawing, it) }
            assertEquals(3.0, drawing.circles[1].radius / drawing.circles[0].radius, 1e-10)
            assertEquals(drawing.points[0].center, drawing.circles[0].center)
            assertEquals(drawing.points[1].center, drawing.circles[1].center)
        }
    }

    @Test fun congruentCirclesAtDifferentCentersShareTheSameRadiusAndScale() {
        val spec = JSONObject().put("points", JSONArray().put(point("O", -4.0, 0.0)).put(point("Q", 4.0, 0.0))
            .put(point("A", -2.0, 0.0)).put(point("B", 6.0, 0.0)))
            .put("objects", JSONArray())
            .put("circles", JSONArray().put(circle("first", "O", 2.0, listOf("A")))
                .put(circle("second", "Q", 2.0, listOf("B"))))
        val model = EuclideanDiagram.parse(spec)
        assertTrue(model.contains("first", "A"))
        assertFalse(model.contains("second", "A"))
        assertTrue(model.contains("second", "B"))
        for ((width, height) in listOf(320.0 to 440.0, 780.0 to 400.0)) {
            val drawing = EuclideanDiagramLayout.layout(model, width, height)
            val first = drawing.circles[0]
            val second = drawing.circles[1]
            assertEquals(first.radius, second.radius, 0.0)
            assertEquals(4 * first.radius, distance(first.center, second.center), 1e-8)
            drawing.circles.forEach { assertCircleInside(drawing, it) }
            for ((circle, pointId) in listOf(first to "A", second to "B")) {
                assertEquals(circle.radius, distance(circle.center, drawing.points.single { it.id == pointId }.center), 1e-8)
            }
        }
    }

    @Test fun verySmallAndLargeCirclesRemainVisibleAndDoNotInheritAFictitiousUnitSpan() {
        val radii = listOf(1e-9, 1e-6, 1.0, 1_000_000.0)
        val drawings = radii.map { radius ->
            EuclideanDiagramLayout.layout(EuclideanDiagram.parse(circleOnly(radius)), 480.0, 320.0)
        }
        drawings.forEach { drawing ->
            val circle = drawing.circles.single()
            assertTrue(circle.radius.isFinite())
            assertTrue("An isolated positive circle must fit visibly, not collapse to a dot", circle.radius > 20)
            assertCircleInside(drawing, circle)
            assertEquals(drawing.points.single().center, circle.center)
        }
        drawings.drop(1).forEach { assertEquals(drawings.first().circles.single().radius, it.circles.single().radius, 1e-7) }
        val tiny = circleOnly(1e-9).apply { getJSONArray("points").put(point("A", 1e-9, 0.0)); getJSONArray("circles").getJSONObject(0).put("contains", JSONArray(listOf("A"))) }
        assertTrue(EuclideanDiagram.parse(tiny).contains("k", "A"))
        assertFalse(EuclideanDiagram.parse(tiny).contains("k", "O"))
        rejects(copy(tiny).apply { getJSONArray("circles").getJSONObject(0).put("contains", JSONArray()) })
    }

    @Test fun labelsKeepEveryDeclaredCharacterWithoutCoveringPointsOrGivingAutomaticClassifications() {
        val text = "Circunferencia de referencia sin clasificación anticipada"
        val spec = circleOnly(2.0).apply {
            getJSONArray("circles").getJSONObject(0).put("label", text)
            getJSONArray("points").getJSONObject(0).put("labels", JSONArray(listOf("O")))
        }
        for ((width, height) in listOf(420.0 to 360.0, 780.0 to 420.0)) {
            for (scale in listOf(1.0, 1.5)) {
                val drawing = EuclideanDiagramLayout.layout(EuclideanDiagram.parse(spec), width, height, 1.0, 15.0 * scale)
                assertEquals(listOf("O", text), drawing.labels.map { it.text })
                for (label in drawing.labels) {
                    assertEquals(label.text, label.lines.joinToString(""))
                    assertTrue(drawing.viewport.contains(EuclideanPosition(label.bounds.left, label.bounds.top)))
                    assertTrue(drawing.viewport.contains(EuclideanPosition(label.bounds.right, label.bounds.bottom)))
                    assertFalse(drawing.points.any { label.bounds.contains(it.center) })
                }
                assertFalse("Labels overlap at $width x $height, font scale $scale: ${drawing.labels.map { it.bounds }}",
                    drawing.labels[0].bounds.overlaps(drawing.labels[1].bounds))
            }
        }
        val unnamed = circleOnly(2.0).apply {
            getJSONArray("circles").getJSONObject(0).remove("label")
            getJSONArray("points").getJSONObject(0).remove("labels")
        }
        assertTrue(EuclideanDiagramLayout.layout(EuclideanDiagram.parse(unnamed), 420.0, 320.0).labels.isEmpty())
    }

    @Test fun theCenterLabelStaysLocallyAnchoredAmongFourRadiiAtLargeTextSize() {
        // The same four-radius scene used by the native visual regression. Avoiding
        // a trace must not move the point name into an unrelated viewport corner.
        val spec = JSONObject("""{"points":[{"id":"O","x":0,"y":0,"labels":["O"]},{"id":"A","x":2,"y":0,"labels":["A"]},{"id":"B","x":0,"y":2,"labels":["B"]},{"id":"C","x":-2,"y":0,"labels":["C"]},{"id":"D","x":0,"y":-2,"labels":["D"]}],"objects":[{"id":"OA","kind":"segment","through":["O","A"],"contains":["O","A"]},{"id":"OB","kind":"segment","through":["O","B"],"contains":["O","B"]},{"id":"OC","kind":"segment","through":["O","C"],"contains":["O","C"]},{"id":"OD","kind":"segment","through":["O","D"],"contains":["O","D"]}],"circles":[{"id":"c","center":"O","radius":2,"contains":["A","B","C","D"],"label":"c"}]}""")
        val model = EuclideanDiagram.parse(spec)
        for ((width, height) in listOf(280.0 to 210.0, 600.0 to 360.0, 320.0 to 250.0)) {
            for (textSize in listOf(15.0, 22.5)) {
                val drawing = EuclideanDiagramLayout.layout(model, width, height, 1.0, textSize)
                val center = drawing.points.single { it.id == "O" }.center
                val label = drawing.labels.single { it.ownerId == "O" }
                val dx = maxOf(label.bounds.left - center.x, center.x - label.bounds.right, 0.0)
                val dy = maxOf(label.bounds.top - center.y, center.y - label.bounds.bottom, 0.0)
                val gap = maxOf(10.0, textSize * .55)
                assertEquals("O", label.text)
                assertFalse(label.bounds.contains(center))
                assertTrue("Center label lost its local anchor at $width x $height, text size $textSize: ${label.bounds}",
                    hypot(dx, dy) <= sqrt(2.0) * gap + 1e-8)
            }
        }
    }

    private fun relations() = JSONObject("""{"points":[{"id":"O","x":0,"y":0,"labels":["O"]},{"id":"A","x":2,"y":0,"labels":["A"]},{"id":"B","x":0,"y":2,"labels":["B"]},{"id":"C","x":-2,"y":0,"labels":["C"]},{"id":"D","x":0,"y":-2,"labels":["D"]},{"id":"I","x":0.5,"y":0,"labels":["I"]},{"id":"E","x":3,"y":0,"labels":["E"]}],"objects":[{"id":"radius","kind":"segment","through":["O","A"],"contains":["O","I","A"]},{"id":"diameter","kind":"segment","through":["A","C"],"contains":["A","I","O","C"]},{"id":"chord","kind":"segment","through":["A","B"],"contains":["A","B"]},{"id":"secant","kind":"line","through":["A","C"],"contains":["A","I","O","C","E"]}],"circles":[{"id":"k","center":"O","radius":2,"contains":["A","B","C","D"]}]}""")

    private fun semicircles() = JSONObject().put("points", JSONArray()
        .put(point("O", 0.0, 0.0)).put(point("A", 2.0, 0.0)).put(point("B", 0.0, 2.0))
        .put(point("C", -2.0, 0.0)).put(point("D", 0.0, -2.0)))
        .put("objects", JSONArray()).put("circles", JSONArray().put(circle("k", "O", 2.0, listOf("A", "B", "C", "D"))))
        .put("arcs", JSONArray().put(arc("upper", listOf("A", "B", "C"), listOf("A", "B", "C")))
            .put(arc("lower", listOf("A", "D", "C"), listOf("A", "D", "C"))))

    private fun circleOnly(radius: Double = 2.0) = JSONObject().put("points", JSONArray().put(point("O", 0.0, 0.0)))
        .put("objects", JSONArray()).put("circles", JSONArray().put(circle("k", "O", radius, emptyList())))
    private fun point(id: String, x: Double, y: Double) = JSONObject().put("id", id).put("x", x).put("y", y).put("labels", JSONArray(listOf(id)))
    private fun circle(id: String, center: String, radius: Double, contains: List<String>) = JSONObject().put("id", id)
        .put("center", center).put("radius", radius).put("contains", JSONArray(contains)).put("label", id)
    private fun arc(id: String, through: List<String>, contains: List<String>) = JSONObject().put("id", id)
        .put("circle", "k").put("through", JSONArray(through)).put("contains", JSONArray(contains)).put("label", id)
    private fun copy(value: JSONObject) = JSONObject(value.toString())
    private fun distance(a: EuclideanPosition, b: EuclideanPosition) = hypot(a.x - b.x, a.y - b.y)
    private fun pointOn(arc: EuclideanDrawnArc, fraction: Double): EuclideanPosition {
        val angle = Math.toRadians(arc.startDegrees + arc.sweepDegrees * fraction)
        return arc.center + EuclideanPosition(cos(angle), sin(angle)) * arc.radius
    }
    private fun assertPointEquals(expected: EuclideanPosition, actual: EuclideanPosition) {
        assertEquals(expected.x, actual.x, 1e-8)
        assertEquals(expected.y, actual.y, 1e-8)
    }
    private fun assertCircleInside(drawing: EuclideanDrawingSnapshot, circle: EuclideanDrawnCircle) {
        val epsilon = 1e-8
        assertTrue(circle.center.x - circle.radius >= drawing.plotBounds.left - epsilon)
        assertTrue(circle.center.x + circle.radius <= drawing.plotBounds.right + epsilon)
        assertTrue(circle.center.y - circle.radius >= drawing.plotBounds.top - epsilon)
        assertTrue(circle.center.y + circle.radius <= drawing.plotBounds.bottom + epsilon)
    }
    private fun rejects(spec: JSONObject) = rejectsOperation { EuclideanDiagram.parse(spec) }
    private fun rejectsOperation(operation: () -> Unit) {
        try { operation(); fail("Invalid Euclidean circular configuration or query was accepted") }
        catch (_: IllegalArgumentException) { }
        catch (_: IllegalStateException) { }
    }
}
