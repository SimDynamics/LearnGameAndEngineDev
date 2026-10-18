package com.learngraphicsprogramming.app.math

import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import java.io.File
import kotlin.math.hypot
import kotlin.math.max

/** Point ownership is distinct from text fitting or avoiding another label. */
class EuclideanPointLabelAssociationTest {
    @Test fun allSixPublishedArcPanelsAssociateEveryNameWithItsOwnMarkAtTheRequiredProfiles() {
        val scenes = publishedArcScenes()
        assertEquals(listOf(1 to 1, 1 to 2, 2 to 1, 2 to 2, 3 to 1, 5 to 1), scenes.map { it.page to it.panel })
        for (scene in scenes) {
            for ((width, height, scale) in listOf(
                Triple(344.0, 737.0 / 3.0, 1.0),
                Triple(360.0, 257.0, 1.0),
                Triple(403.0, 863.0 / 3.0, 1.5),
                Triple(704.0, 1508.0 / 3.0, 1.5)
            )) {
                for (density in listOf(1.0, 2.75)) {
                    val drawing = EuclideanDiagramLayout.layout(scene.diagram, width * density, height * density,
                        density, 15.0 * scale * density)
                    assertEveryPointLabelHasOneClearOwner(drawing, density,
                        "p${scene.page} panel${scene.panel}, ${width}x$height, text$scale, density$density")
                }
            }
        }
    }

    @Test fun spaciousPanelsAlsoKeepTheWholeProtectedNameRectangleInsideItsOwnersRegion() {
        // The JVM metrics reserve a full line box. Native tests separately inspect
        // actual Paint glyph bounds at the four required display profiles above.
        // A dense scene need not have room for the entire ascent/descent reservation.
        for (scene in publishedArcScenes()) {
            for ((width, height) in listOf(504.0 to 360.0, 720.0 to 720.0 / 1.4)) {
                for (textSize in listOf(15.0, 22.5)) {
                    val drawing = EuclideanDiagramLayout.layout(scene.diagram, width, height, 1.0, textSize)
                    assertEveryPointLabelHasOneClearOwner(drawing, 1.0,
                        "spacious p${scene.page} panel${scene.panel}, ${width}x$height, text$textSize", checkEntireBox = true)
                }
            }
        }
    }

    @Test fun unnamedBoundaryMarksAlsoParticipateInTheAssociationCheck() {
        val scenes = publishedArcScenes().filter { it.page == 3 || it.page == 5 }
        assertEquals(listOf(10, 8), scenes.map { it.diagram.points.size })
        assertEquals(listOf(6, 4), scenes.map { it.diagram.points.count { point -> point.labels.isEmpty() } })
        scenes.forEach { scene ->
            val drawing = EuclideanDiagramLayout.layout(scene.diagram, 280.0, 200.0, 1.0, 22.5)
            assertEveryPointLabelHasOneClearOwner(drawing, 1.0, "small p${scene.page}")
            assertEquals(scene.diagram.points.filter { it.labels.isNotEmpty() }.map { it.id }, drawing.labels.map { it.ownerId })
        }
    }

    @Test fun multipleNamesOfOnePointStayReadableAndRetainTheSameGeometricOwner() {
        val config = JSONObject("""{"points":[{"id":"O","x":0,"y":0,"labels":["O","Q"]},{"id":"A","x":2,"y":0,"labels":["A"]},{"id":"B","x":0,"y":2,"labels":["B"]}],"objects":[],"circles":[{"id":"c","center":"O","radius":2,"contains":["A","B"]}]}""")
        val model = EuclideanDiagram.parse(config)
        for (textSize in listOf(15.0, 22.5)) {
            val drawing = EuclideanDiagramLayout.layout(model, 392.0, 280.0, 1.0, textSize)
            assertEveryPointLabelHasOneClearOwner(drawing, 1.0, "point aliases, text$textSize")
            assertEquals(listOf("O", "Q"), drawing.labels.filter { it.ownerId == "O" }.map { it.text })
            assertEquals(3, drawing.points.size)
        }
    }

    private data class Scene(val page: Int, val panel: Int, val diagram: EuclideanDiagram)

    private fun publishedArcScenes(): List<Scene> {
        val relative = "src/main/assets/content/es/math_physics/math_geometria_plana/fundamentos_geometricos/lesson_comparar_y_operar_con_arcos.json"
        val asset = sequenceOf(File(relative), File("app/$relative")).first { it.isFile }
        val pages = JSONObject(asset.readText(Charsets.UTF_8)).getJSONArray("pages")
        return buildList {
            for (pageIndex in 0 until pages.length()) {
                val blocks = pages.getJSONObject(pageIndex).getJSONArray("blocks")
                for (blockIndex in 0 until blocks.length()) {
                    val representations = blocks.getJSONObject(blockIndex).optJSONArray("representations") ?: continue
                    for (panelIndex in 0 until representations.length()) {
                        val config = representations.getJSONObject(panelIndex)
                        if (config.optString("type") == "euclidean_diagram") {
                            add(Scene(pageIndex + 1, panelIndex + 1, EuclideanDiagram.parse(config)))
                        }
                    }
                }
            }
        }
    }

    private fun assertEveryPointLabelHasOneClearOwner(drawing: EuclideanDrawingSnapshot, unit: Double, context: String,
        checkEntireBox: Boolean = false) {
        drawing.labels.forEachIndexed { index, label ->
            val owner = drawing.points.single { it.id == label.ownerId }
            val middle = EuclideanPosition((label.bounds.left + label.bounds.right) / 2, (label.bounds.top + label.bounds.bottom) / 2)
            val ownDistance = distance(middle, owner.center)
            assertEquals(label.text, label.lines.joinToString(""))
            assertTrue("$context ${label.text} exceeds viewport", drawing.viewport.contains(EuclideanPosition(label.bounds.left, label.bounds.top)))
            assertTrue("$context ${label.text} exceeds viewport", drawing.viewport.contains(EuclideanPosition(label.bounds.right, label.bounds.bottom)))
            drawing.points.forEach { point ->
                assertTrue("$context ${label.text} obscures marker ${point.id}", distanceToRectangle(point.center, label.bounds) >= 6.6 * unit - 1e-7)
                if (point.id != owner.id) {
                    assertTrue("$context ${label.text} is not clearly nearer ${owner.id} than ${point.id}: own=$ownDistance, other=${distance(middle, point.center)}, box=${label.bounds}",
                        ownDistance + 2 * unit <= distance(middle, point.center) + 1e-7)
                    if (checkEntireBox) {
                        for ((x, y) in listOf(label.bounds.left to label.bounds.top, label.bounds.right to label.bounds.top,
                            label.bounds.left to label.bounds.bottom, label.bounds.right to label.bounds.bottom)) {
                            val corner = EuclideanPosition(x, y)
                            assertTrue("$context ${label.text} straddles the ownership boundary with ${point.id}: box=${label.bounds}",
                                distance(corner, owner.center) <= distance(corner, point.center) + 1e-7)
                        }
                    }
                }
            }
            drawing.labels.drop(index + 1).forEach { other ->
                assertFalse("$context ${label.text} overlaps ${other.text}", label.bounds.overlaps(other.bounds))
            }
        }
    }

    private fun distance(first: EuclideanPosition, second: EuclideanPosition) = hypot(first.x - second.x, first.y - second.y)
    private fun distanceToRectangle(point: EuclideanPosition, rect: EuclideanRect) = hypot(
        max(0.0, max(rect.left - point.x, point.x - rect.right)),
        max(0.0, max(rect.top - point.y, point.y - rect.bottom)))
}
