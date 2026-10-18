package com.learngraphicsprogramming.app.math

import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import java.io.File
import kotlin.math.hypot
import kotlin.math.max

/** The same point-ownership rule must also work when no auxiliary circle is drawn. */
class EuclideanRayLabelAssociationTest {
    @Test fun thePublishedThreeRayPanelKeepsEveryLetterWithItsOwnMarkAtAllFourProfiles() {
        val model = angleScenes().single { it.page == 2 }.diagram
        assertTrue(model.circles.isEmpty())
        assertEquals(listOf("S", "A", "C", "B", "D", "E"), model.points.map { it.id })
        for ((width, height, scale) in profiles) {
            for (density in listOf(1.0, 3.0)) {
                val drawing = EuclideanDiagramLayout.layout(model, width * density, height * density,
                    density, 15 * scale * density)
                assertPointOwnership(drawing, density, "three rays ${width}x$height font$scale density$density", entireBox = true)
            }
        }
    }

    @Test fun allFourPublishedAnglePanelsRetainLegibleLocalNamesIncludingTheirAuxiliaryCircles() {
        val scenes = angleScenes()
        assertEquals(listOf(1 to 1, 2 to 1, 3 to 1, 3 to 2), scenes.map { it.page to it.panel })
        for (scene in scenes) {
            for ((width, height, scale) in profiles) {
                val drawing = EuclideanDiagramLayout.layout(scene.diagram, width, height, 1.0, 15 * scale)
                assertPointOwnership(drawing, 1.0, "p${scene.page} panel${scene.panel} ${width}x$height font$scale")
                assertEquals(scene.diagram.points.flatMap { it.labels }, drawing.labels.map { it.text })
                assertEquals(scene.diagram.objects.size, drawing.paths.size)
                assertEquals(scene.diagram.objects.size, drawing.arrows.size)
                assertEquals(scene.diagram.circles.size, drawing.circles.size)
                assertEquals(scene.diagram.arcs.size, drawing.arcs.size)
            }
        }
    }

    @Test fun theThreeRaysKeepTheirCommonOriginIncidencesAndContinuationBeyondTheLastNamedPoint() {
        val model = angleScenes().single { it.page == 2 }.diagram
        for ((ray, expected) in mapOf("sa" to setOf("S", "A", "C"), "sb" to setOf("S", "B", "D"), "se" to setOf("S", "E"))) {
            model.points.forEach { assertEquals(it.id in expected, model.contains(ray, it.id)) }
        }
        for ((width, height, scale) in profiles) {
            val drawing = EuclideanDiagramLayout.layout(model, width, height, 1.0, 15 * scale)
            val points = drawing.points.associate { it.id to it.center }
            val origin = points.getValue("S")
            assertEquals(2.0, distance(origin, points.getValue("C")) / distance(origin, points.getValue("A")), 1e-10)
            assertEquals(2.0, distance(origin, points.getValue("D")) / distance(origin, points.getValue("B")), 1e-10)
            assertEquals(3, drawing.paths.size)
            assertEquals(3, drawing.arrows.size)
            for ((id, last) in mapOf("sa" to "C", "sb" to "D", "se" to "E")) {
                val path = drawing.paths.single { it.id == id }
                assertEquals(EuclideanObjectKind.RAY, path.kind)
                assertEquals(origin, path.start)
                assertFalse(path.startArrow)
                assertTrue(path.endArrow)
                assertTrue(distance(origin, path.end) > distance(origin, points.getValue(last)))
                val arrow = drawing.arrows.single { it.objectId == id }
                assertEquals(path.end, arrow.tip)
                assertTrue(drawing.viewport.contains(arrow.tip))
                assertTrue(drawing.viewport.contains(arrow.wingA))
                assertTrue(drawing.viewport.contains(arrow.wingB))
            }
        }
    }

    @Test fun unrelatedLineAndSegmentNamesRetainTheirEstablishedPositions() {
        for (kind in listOf("line", "segment")) {
            val spec = JSONObject("""{"points":[{"id":"A","x":0,"y":0,"labels":["A"]},{"id":"B","x":2,"y":0,"labels":["B"]}],"objects":[{"id":"object","kind":"$kind","through":["A","B"],"contains":["A","B"],"label":"r"}]}""")
            val drawing = EuclideanDiagramLayout.layout(EuclideanDiagram.parse(spec), 480.0, 330.0)
            assertEquals(listOf("A", "B", "r"), drawing.labels.map { it.text })
            val expectedLeft = listOf(133.33333333333334, 366.66666666666663,
                if (kind == "line") 342.4 else 301.3333333333333)
            drawing.labels.forEachIndexed { index, label ->
                assertEquals("$kind ${label.text}", expectedLeft[index], label.bounds.left, 1e-10)
                assertEquals(134.25, label.bounds.top, 1e-10)
                assertEquals(12.7, label.bounds.width, 1e-10)
                assertEquals(20.75, label.bounds.height, 1e-10)
            }
        }
    }

    @Test fun twoNamesOfOneRayOriginStillNameOnePositionWithoutHidingEitherName() {
        val model = EuclideanDiagram.parse(JSONObject("""{"points":[{"id":"P","x":0,"y":0,"labels":["S","Q"]}],"objects":[{"id":"r","kind":"ray","origin":"P","direction":{"x":1,"y":0},"contains":["P"]},{"id":"s","kind":"ray","origin":"P","direction":{"x":0,"y":1},"contains":["P"]}]}"""))
        for (textSize in listOf(15.0, 22.5)) {
            val drawing = EuclideanDiagramLayout.layout(model, 403.0, 863.0 / 3.0, 1.0, textSize)
            assertEquals(1, drawing.points.size)
            assertEquals(listOf("S", "Q"), drawing.labels.map { it.text })
            assertTrue(drawing.labels.all { it.ownerId == "P" })
            assertPointOwnership(drawing, 1.0, "origin aliases font$textSize")
        }
    }

    private data class Scene(val page: Int, val panel: Int, val diagram: EuclideanDiagram)
    private val profiles = listOf(
        Triple(344.0, 737.0 / 3.0, 1.0),
        Triple(360.0, 257.0, 1.0),
        Triple(403.0, 863.0 / 3.0, 1.5),
        Triple(704.0, 1508.0 / 3.0, 1.5)
    )

    private fun angleScenes(): List<Scene> {
        val relative = "src/main/assets/content/es/math_physics/math_geometria_plana/angulos_y_rectas/lesson_lados_vertice_y_region_de_un_angulo.json"
        val pages = JSONObject(sequenceOf(File(relative), File("app/$relative")).first { it.isFile }.readText(Charsets.UTF_8)).getJSONArray("pages")
        return buildList {
            for (pageIndex in 0 until pages.length()) {
                val blocks = pages.getJSONObject(pageIndex).getJSONArray("blocks")
                for (blockIndex in 0 until blocks.length()) {
                    val representations = blocks.getJSONObject(blockIndex).optJSONArray("representations") ?: continue
                    for (panelIndex in 0 until representations.length()) {
                        val representation = representations.getJSONObject(panelIndex)
                        if (representation.optString("type") == "euclidean_diagram") {
                            add(Scene(pageIndex + 1, panelIndex + 1, EuclideanDiagram.parse(representation)))
                        }
                    }
                }
            }
        }
    }

    private fun assertPointOwnership(drawing: EuclideanDrawingSnapshot, unit: Double, context: String, entireBox: Boolean = false) {
        drawing.labels.forEachIndexed { index, label ->
            val owner = drawing.points.single { it.id == label.ownerId }
            val middle = EuclideanPosition((label.bounds.left + label.bounds.right) / 2, (label.bounds.top + label.bounds.bottom) / 2)
            assertEquals(label.text, label.lines.joinToString(""))
            assertTrue("$context ${label.text} exceeds viewport", drawing.viewport.contains(EuclideanPosition(label.bounds.left, label.bounds.top)))
            assertTrue("$context ${label.text} exceeds viewport", drawing.viewport.contains(EuclideanPosition(label.bounds.right, label.bounds.bottom)))
            drawing.points.forEach { other ->
                assertTrue("$context ${label.text} obscures ${other.id}", distanceToRectangle(other.center, label.bounds) >= 6.6 * unit - 1e-7)
                if (owner.id != other.id) {
                    assertTrue("$context ${label.text} is ambiguous with ${other.id}",
                        distance(middle, owner.center) + 2 * unit <= distance(middle, other.center) + 1e-7)
                    if (entireBox) {
                        for ((x, y) in listOf(label.bounds.left to label.bounds.top, label.bounds.right to label.bounds.top,
                            label.bounds.left to label.bounds.bottom, label.bounds.right to label.bounds.bottom)) {
                            val corner = EuclideanPosition(x, y)
                            assertTrue("$context ${label.text} straddles ${other.id}", distance(corner, owner.center) <= distance(corner, other.center) + 1e-7)
                        }
                    }
                }
            }
            drawing.labels.drop(index + 1).forEach { other -> assertFalse("$context ${label.text} overlaps ${other.text}", label.bounds.overlaps(other.bounds)) }
        }
    }

    private fun distance(first: EuclideanPosition, second: EuclideanPosition) = hypot(first.x - second.x, first.y - second.y)
    private fun distanceToRectangle(point: EuclideanPosition, rect: EuclideanRect) = hypot(
        max(0.0, max(rect.left - point.x, point.x - rect.right)), max(0.0, max(rect.top - point.y, point.y - rect.bottom)))
}
