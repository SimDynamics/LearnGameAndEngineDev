package com.learngraphicsprogramming.app.math

import kotlin.math.*

/** Label clearance for two superposed figures. Only name rectangles move; never geometry.
 * The passive Euclidean layout and all existing lessons retain their original policy. */
internal object RigidAngleLabelLayout {
    fun clear(snapshot: EuclideanDrawingSnapshot, unit: Double): EuclideanDrawingSnapshot {
        val placed = mutableListOf<EuclideanDrawnLabel>()
        val markerRadius = 6.6*unit
        val arrowMargin = 1.8*unit
        fun hitsProtectedInk(bounds: EuclideanRect): Boolean {
            if (snapshot.points.any { point ->
                val dx = max(0.0, max(bounds.left-point.center.x, point.center.x-bounds.right))
                val dy = max(0.0, max(bounds.top-point.center.y, point.center.y-bounds.bottom))
                hypot(dx,dy) < markerRadius
            }) return true
            val expanded = EuclideanRect(bounds.left-arrowMargin,bounds.top-arrowMargin,bounds.right+arrowMargin,bounds.bottom+arrowMargin)
            return snapshot.arrows.any { arrow -> listOf(arrow.wingA,arrow.wingB).any { wing ->
                EuclideanDiagramLayout.clip(arrow.tip,wing-arrow.tip,expanded,EuclideanObjectKind.SEGMENT) != null
            } }
        }
        for (label in snapshot.labels) {
            val owner = snapshot.points.singleOrNull { it.id == label.ownerId }
            if (owner == null) { placed += label; continue }
            val width = label.bounds.width
            val height = label.bounds.height
            val edge = 2*unit
            fun rect(center: EuclideanPosition): EuclideanRect {
                val x = (center.x-width/2).coerceIn(edge,max(edge,snapshot.viewport.right-width-edge))
                val y = (center.y-height/2).coerceIn(edge,max(edge,snapshot.viewport.bottom-height-edge))
                return EuclideanRect(x,y,x+width,y+height)
            }
            fun available(bounds: EuclideanRect) = !hitsProtectedInk(bounds) && placed.none { previous ->
                EuclideanRect(bounds.left-unit,bounds.top-unit,bounds.right+unit,bounds.bottom+unit).overlaps(previous.bounds)
            }
            fun center(bounds: EuclideanRect) = EuclideanPosition((bounds.left+bounds.right)/2,(bounds.top+bounds.bottom)/2)
            fun associationDeficit(bounds: EuclideanRect): Double {
                val middle = center(bounds)
                val own = hypot(middle.x-owner.center.x,middle.y-owner.center.y)
                return snapshot.points.filter { it.id != owner.id && hypot(it.center.x-owner.center.x,it.center.y-owner.center.y) >= 12*unit }
                    .maxOfOrNull { point -> max(0.0, own+unit-hypot(middle.x-point.center.x,middle.y-point.center.y)) } ?: 0.0
            }
            if (available(label.bounds) && associationDeficit(label.bounds) == 0.0) { placed += label; continue }
            val local = buildList {
                add(label.bounds)
                for (gap in listOf(10.0,16.0,24.0,36.0,52.0)) for (index in 0 until 16) {
                    val angle = index*Math.PI/8
                    add(rect(owner.center+EuclideanPosition(cos(angle)*(width/2+gap*unit),sin(angle)*(height/2+gap*unit))))
                }
            }.distinct()
            var candidates = local.filter(::available)
            if (candidates.isEmpty()) {
                val step = max(8*unit,height*.6)
                val grid = mutableListOf<EuclideanRect>()
                var y = edge+height/2
                while (y <= snapshot.viewport.bottom-height/2-edge) {
                    var x = edge+width/2
                    while (x <= snapshot.viewport.right-width/2-edge) { val value = rect(EuclideanPosition(x,y)); if (available(value)) grid += value; x += step }
                    y += step
                }
                candidates = grid
            }
            val chosen = candidates.minWithOrNull(compareBy<EuclideanRect> { associationDeficit(it) }
                .thenBy { bounds -> snapshot.paths.count { path -> EuclideanDiagramLayout.clip(path.start,path.end-path.start,bounds,EuclideanObjectKind.SEGMENT) != null } }
                .thenBy { bounds -> val middle = center(bounds); hypot(middle.x-owner.center.x,middle.y-owner.center.y) })
                ?: label.bounds
            placed += label.copy(bounds = chosen)
        }
        return snapshot.copy(labels = placed)
    }
}
