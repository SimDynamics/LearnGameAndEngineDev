package com.learngraphicsprogramming.app.math

import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import kotlin.math.hypot

class EuclideanDiagramTest {
    @Test fun declaredMembershipUsesStablePointAndObjectIds() {
        val model = EuclideanDiagram.parse(membership())
        assertTrue(model.contains("r", "A"))
        assertTrue(model.contains("s", "A"))
        assertTrue(model.contains("r", "B"))
        assertFalse(model.contains("s", "B"))
        assertFalse(model.contains("r", "C"))
        assertTrue(model.contains("s", "C"))
        assertEquals(listOf("A", "B", "C"), model.points.map { it.id })
        assertEquals(model, EuclideanDiagram.parse(JSONObject().put("config", membership())))
    }

    @Test fun multipleNamesOfOnePointDoNotBecomeDistinctPointsOrSupplyDirection() {
        val model = EuclideanDiagram.parse(aliases())
        assertEquals(1, model.points.size)
        assertEquals(listOf("A", "B"), model.points.single().labels)
        assertEquals(listOf("P"), model.objects[0].definingPointIds)
        assertTrue(model.contains("r", "P"))
        assertTrue(model.contains("s", "P"))
        assertNotEquals(model.objects[0].direction, model.objects[1].direction)
        rejects(aliases().apply {
            getJSONArray("points").put(JSONObject("""{"id":"Q","x":0,"y":0,"labels":["Q"]}"""))
        })
        rejects(aliases().apply { getJSONArray("objects").getJSONObject(0).put("direction", JSONObject("""{"x":0,"y":0}""")) })
    }

    @Test fun signedZeroIsOnePositionAndIsNormalizedInDirectionsToo() {
        val duplicate = JSONObject("""{"points":[{"id":"A","x":0.0,"y":0.0,"labels":["A"]},{"id":"B","x":-0.0,"y":-0.0,"labels":["B"]}],"objects":[]}""")
        rejects(duplicate)
        val spec = aliases().apply {
            getJSONArray("points").getJSONObject(0).put("x", -0.0).put("y", -0.0)
            getJSONArray("objects").getJSONObject(0).getJSONObject("direction").put("y", -0.0)
        }
        val model = EuclideanDiagram.parse(spec)
        assertEquals(0L, java.lang.Double.doubleToRawLongBits(model.points.single().position.x))
        assertEquals(0L, java.lang.Double.doubleToRawLongBits(model.points.single().position.y))
        assertEquals(0L, java.lang.Double.doubleToRawLongBits(model.objects[0].direction.y))
    }

    @Test fun invalidIdsReferencesTypesRangesAndLabelsCannotCreateAFalseDiagram() {
        rejects(membership().apply { getJSONArray("points").getJSONObject(0).put("id", "") })
        rejects(membership().apply { getJSONArray("points").getJSONObject(0).put("x", "0") })
        rejects(membership().apply { getJSONArray("points").getJSONObject(0).put("x", 1_000_001) })
        rejects(membership().apply { getJSONArray("points").getJSONObject(0).remove("y") })
        rejects(membership().apply { getJSONArray("points").getJSONObject(0).put("labels", "A") })
        rejects(membership().apply { getJSONArray("points").getJSONObject(0).getJSONArray("labels").put("A") })
        rejects(membership().apply { getJSONArray("objects").getJSONObject(0).getJSONArray("through").put(1, "unknown") })
        rejects(membership().apply { getJSONArray("objects").getJSONObject(0).put("kind", "circle") })
        rejects(membership().apply { getJSONArray("objects").getJSONObject(0).remove("contains") })
        rejects(membership().apply { getJSONArray("objects").getJSONObject(0).put("id", "A") })
        rejects(aliases().apply { getJSONArray("objects").getJSONObject(0).put("kind", "segment") })
    }

    @Test fun visibleIncidencesMustMatchDeclarationsIncludingRayAndSegmentLimits() {
        rejects(membership().apply { getJSONArray("objects").getJSONObject(0).getJSONArray("contains").put("C") })
        rejects(membership().apply { getJSONArray("objects").getJSONObject(0).getJSONArray("contains").remove(1) })
        val spec = limits("ray")
        spec.getJSONArray("points").put(JSONObject("""{"id":"C","x":-1,"y":0,"labels":["C"]}"""))
        assertFalse(EuclideanDiagram.parse(spec).contains("object", "C"))
        rejects(JSONObject(spec.toString()).apply { getJSONArray("objects").getJSONObject(0).getJSONArray("contains").put("C") })
        val segment = limits("segment")
        segment.getJSONArray("points").put(JSONObject("""{"id":"C","x":3,"y":0,"labels":["C"]}"""))
        assertFalse(EuclideanDiagram.parse(segment).contains("object", "C"))
        val line = limits("line")
        line.getJSONArray("points").put(JSONObject("""{"id":"C","x":3,"y":0,"labels":["C"]}"""))
        rejects(line)
        line.getJSONArray("objects").getJSONObject(0).getJSONArray("contains").put("C")
        assertTrue(EuclideanDiagram.parse(line).contains("object", "C"))
    }

    @Test fun twoDistinctLinesCannotShareTwoDistinctPoints() {
        val spec = membership()
        spec.getJSONArray("objects").getJSONObject(1)
            .put("through", org.json.JSONArray(listOf("B", "A")))
            .put("contains", org.json.JSONArray(listOf("A", "B")))
        rejects(spec)
    }

    @Test fun clippingUsesParametricRangesAndHandlesVerticalNegativeAndOutsideDirections() {
        val box = EuclideanRect(0.0, 0.0, 100.0, 80.0)
        val origin = EuclideanPosition(30.0, 40.0)
        val direction = EuclideanPosition(10.0, 0.0)
        assertEquals(EuclideanPosition(0.0, 40.0) to EuclideanPosition(100.0, 40.0), EuclideanDiagramLayout.clip(origin, direction, box, EuclideanObjectKind.LINE))
        assertEquals(origin to EuclideanPosition(100.0, 40.0), EuclideanDiagramLayout.clip(origin, direction, box, EuclideanObjectKind.RAY))
        assertEquals(origin to EuclideanPosition(40.0, 40.0), EuclideanDiagramLayout.clip(origin, direction, box, EuclideanObjectKind.SEGMENT))
        assertEquals(EuclideanPosition(30.0, 80.0) to EuclideanPosition(30.0, 0.0), EuclideanDiagramLayout.clip(origin, EuclideanPosition(0.0, -20.0), box, EuclideanObjectKind.LINE))
        assertNull(EuclideanDiagramLayout.clip(EuclideanPosition(-10.0, -10.0), EuclideanPosition(-1.0, 0.0), box, EuclideanObjectKind.RAY))
        assertNotNull(EuclideanDiagramLayout.clip(EuclideanPosition(-10.0, 40.0), direction, box, EuclideanObjectKind.LINE))
    }

    @Test fun lineRayAndSegmentHaveDifferentEndpointAndContinuationPrimitives() {
        val scenes = listOf("line", "ray", "segment").associateWith { kind ->
            EuclideanDiagramLayout.layout(EuclideanDiagram.parse(limits(kind)), 480.0, 330.0)
        }
        assertEquals(2, scenes.getValue("line").arrows.size)
        assertEquals(1, scenes.getValue("ray").arrows.size)
        assertEquals(0, scenes.getValue("segment").arrows.size)
        val a = scenes.getValue("segment").points.single { it.id == "A" }.center
        val b = scenes.getValue("segment").points.single { it.id == "B" }.center
        assertEquals(a, scenes.getValue("segment").paths.single().start)
        assertEquals(b, scenes.getValue("segment").paths.single().end)
        assertEquals(a, scenes.getValue("ray").paths.single().start)
        assertTrue(scenes.getValue("ray").paths.single().end.x > b.x)
        assertTrue(scenes.getValue("line").paths.single().start.x < a.x)
        assertTrue(scenes.getValue("line").paths.single().end.x > b.x)
        scenes.values.forEach { snapshot ->
            snapshot.arrows.forEach { arrow ->
                assertTrue(snapshot.viewport.contains(arrow.tip))
                assertTrue(snapshot.viewport.contains(arrow.wingA))
                assertTrue(snapshot.viewport.contains(arrow.wingB))
            }
        }
    }

    @Test fun aSmallDirectionInALargeSceneKeepsItsInfiniteLineAndArrowheads() {
        val spec = JSONObject("""{"points":[{"id":"P","x":0,"y":0,"labels":["P"]},{"id":"Q","x":1000000,"y":1000000,"labels":["Q"]}],"objects":[{"id":"r","kind":"line","origin":"P","direction":{"x":0.000000001,"y":0},"contains":["P"],"label":"r"}]}""")
        val small = EuclideanDiagramLayout.layout(EuclideanDiagram.parse(spec), 1080.0, 780.0)
        assertEquals(1, small.paths.size)
        assertEquals(2, small.arrows.size)
        assertEquals(small.plotBounds.left, small.paths.single().start.x, 1e-10)
        assertEquals(small.plotBounds.right, small.paths.single().end.x, 1e-10)
        spec.getJSONArray("objects").getJSONObject(0).getJSONObject("direction").put("x", 1.0)
        val unit = EuclideanDiagramLayout.layout(EuclideanDiagram.parse(spec), 1080.0, 780.0)
        assertEquals(unit.paths, small.paths)
        assertEquals(unit.arrows, small.arrows)
    }

    @Test fun threeSupportedScenesKeepCompleteSeparatedLabelsAtDifferentSizesAndScales() {
        for (spec in listOf(membership(), aliases(), limits("line"), limits("ray"), limits("segment"))) {
            for ((width, height) in listOf(320.0 to 240.0, 780.0 to 480.0)) {
                for (scale in listOf(1.0, 1.6)) {
                    val model = EuclideanDiagram.parse(spec)
                    val drawing = EuclideanDiagramLayout.layout(model, width, height, 1.0, 15.0 * scale)
                    val expected = model.points.flatMap { it.labels } + model.objects.mapNotNull { it.label }
                    assertEquals(expected, drawing.labels.map { it.text })
                    drawing.labels.forEach { label ->
                        assertEquals(label.text, label.lines.joinToString(""))
                        assertTrue(drawing.viewport.contains(EuclideanPosition(label.bounds.left, label.bounds.top)))
                        assertTrue(drawing.viewport.contains(EuclideanPosition(label.bounds.right, label.bounds.bottom)))
                        assertTrue(drawing.points.none { label.bounds.contains(it.center) })
                    }
                    for (a in drawing.labels.indices) for (b in a + 1 until drawing.labels.size) {
                        assertFalse("${drawing.labels[a].text}/${drawing.labels[b].text} at $width/$scale", drawing.labels[a].bounds.overlaps(drawing.labels[b].bounds))
                    }
                }
            }
        }
    }

    @Test fun positionsAreUniformlyFittedWithoutAxesCoordinatesOrAutomaticallyGeneratedNames() {
        val drawing = EuclideanDiagramLayout.layout(EuclideanDiagram.parse(membership()), 520.0, 350.0)
        val a = drawing.points.single { it.id == "A" }.center
        val b = drawing.points.single { it.id == "B" }.center
        val c = drawing.points.single { it.id == "C" }.center
        assertEquals(2.0, hypot(b.x - a.x, b.y - a.y) / hypot(c.x - a.x, c.y - a.y), 1e-10)
        assertEquals(listOf("A", "B", "C", "r", "s"), drawing.labels.map { it.text })
        assertEquals(2, drawing.paths.size)
        assertEquals(4, drawing.arrows.size)
        val unnamed = membership().apply { getJSONArray("points").getJSONObject(0).remove("labels") }
        assertFalse(EuclideanDiagramLayout.layout(EuclideanDiagram.parse(unnamed), 400.0, 320.0).labels.any { it.text == "A" })
    }

    @Test fun longLabelsWrapWithoutLosingAnyCharacters() {
        val spec = limits("segment")
        val text = "Punto cuya etiqueta conserva todos los caracteres sin puntos suspensivos"
        spec.getJSONArray("points").getJSONObject(0).put("labels", org.json.JSONArray(listOf(text)))
        val drawing = EuclideanDiagramLayout.layout(EuclideanDiagram.parse(spec), 420.0, 320.0)
        val label = drawing.labels.single { it.text == text }
        assertTrue(label.lines.size > 1)
        assertEquals(text, label.lines.joinToString(""))
        assertTrue(label.bounds.right < 420.0 && label.bounds.bottom < 320.0)
    }

    private fun rejects(spec: JSONObject) {
        try { EuclideanDiagram.parse(spec); fail("Invalid Euclidean configuration was accepted: $spec") }
        catch (_: IllegalArgumentException) { }
        catch (_: IllegalStateException) { }
    }
    private fun membership() = JSONObject("""{"type":"euclidean_diagram","points":[{"id":"A","x":0,"y":0,"labels":["A"]},{"id":"B","x":2,"y":0,"labels":["B"]},{"id":"C","x":0,"y":1,"labels":["C"]}],"objects":[{"id":"r","kind":"line","through":["A","B"],"label":"r","contains":["A","B"]},{"id":"s","kind":"line","through":["A","C"],"label":"s","contains":["A","C"]}]}""")
    private fun aliases() = JSONObject("""{"type":"euclidean_diagram","points":[{"id":"P","x":0,"y":0,"labels":["A","B"]}],"objects":[{"id":"r","kind":"line","origin":"P","direction":{"x":1,"y":0},"label":"r","contains":["P"]},{"id":"s","kind":"line","origin":"P","direction":{"x":1,"y":1},"label":"s","contains":["P"]}]}""")
    private fun limits(kind: String) = JSONObject("""{"type":"euclidean_diagram","points":[{"id":"A","x":0,"y":0,"labels":["A"]},{"id":"B","x":2,"y":0,"labels":["B"]}],"objects":[{"id":"object","kind":"$kind","through":["A","B"],"label":"r","contains":["A","B"]}]}""")
}
