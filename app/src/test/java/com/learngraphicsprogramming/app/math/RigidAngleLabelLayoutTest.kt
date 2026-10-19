package com.learngraphicsprogramming.app.math

import org.junit.Assert.*
import org.junit.Test
import kotlin.math.*

class RigidAngleLabelLayoutTest {
    @Test fun superposedNamesNeverCoverAnyMarkerOrArrowAndLeaveTheGeometryUntouched() {
        val raw = RigidAngleCopyTest.spec().put("relation","reference_inside_copy")
        raw.getJSONObject("copy").getJSONObject("diagram").getJSONArray("points").getJSONObject(2)
            .put("x",3*cos(2.7)).put("y",3*sin(2.7))
        val spec = RigidAngleCopySpec.parse(raw)
        for ((width,height) in listOf(360.0 to 213.0,316.0 to 174.0,720.0 to 540.0)) for (scale in listOf(1.0,1.5)) {
            val reference = spec.reference.moved(spec.referencePlacement,"reference:")
            val copy = spec.copy.moved(RigidAnglePlacement(1.00001,2.00001,-.7),"copy:")
            val initial = EuclideanDiagramLayout.layoutInBounds(EuclideanDiagram(reference.points+copy.points,reference.objects+copy.objects),
                width,height,1.0,15*scale,EuclideanRect(-8.0,-8.0,16.0,16.0))
            val cleared = RigidAngleLabelLayout.clear(initial,1.0)
            assertEquals(initial.points,cleared.points)
            assertEquals(initial.paths,cleared.paths)
            assertEquals(initial.arrows,cleared.arrows)
            assertEquals(initial.labels.map { it.ownerId to it.text },cleared.labels.map { it.ownerId to it.text })
            assertEquals(initial.labels.map { it.lines },cleared.labels.map { it.lines })
            for ((index,label) in cleared.labels.withIndex()) {
                assertTrue(label.bounds.left >= 0 && label.bounds.top >= 0 && label.bounds.right <= width && label.bounds.bottom <= height)
                assertTrue(cleared.labels.take(index).none { it.bounds.overlaps(label.bounds) })
                for (point in cleared.points) {
                    val dx = max(0.0,max(label.bounds.left-point.center.x,point.center.x-label.bounds.right))
                    val dy = max(0.0,max(label.bounds.top-point.center.y,point.center.y-label.bounds.bottom))
                    assertTrue("${label.text} must clear ${point.id}",hypot(dx,dy) >= 6.6)
                }
                val arrowBox = EuclideanRect(label.bounds.left-1.8,label.bounds.top-1.8,label.bounds.right+1.8,label.bounds.bottom+1.8)
                for (arrow in cleared.arrows) for (wing in listOf(arrow.wingA,arrow.wingB))
                    assertNull("${label.text} must clear both wings of ${arrow.objectId}",
                        EuclideanDiagramLayout.clip(arrow.tip,wing-arrow.tip,arrowBox,EuclideanObjectKind.SEGMENT))
            }
        }
    }
}
