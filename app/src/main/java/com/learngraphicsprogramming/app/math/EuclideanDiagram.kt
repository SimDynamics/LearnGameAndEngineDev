package com.learngraphicsprogramming.app.math

import org.json.JSONArray
import org.json.JSONObject
import kotlin.math.abs
import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.hypot
import kotlin.math.max
import kotlin.math.min
import kotlin.math.sin

/** Internal positions only arrange the drawing; membership always comes from declared IDs. */
data class EuclideanPosition(val x: Double, val y: Double) {
    operator fun plus(other: EuclideanPosition) = EuclideanPosition(x + other.x, y + other.y)
    operator fun minus(other: EuclideanPosition) = EuclideanPosition(x - other.x, y - other.y)
    operator fun times(factor: Double) = EuclideanPosition(x * factor, y * factor)
}

enum class EuclideanObjectKind { LINE, RAY, SEGMENT }
data class EuclideanPoint(val id: String, val position: EuclideanPosition, val labels: List<String>)
data class EuclideanObject(
    val id: String,
    val kind: EuclideanObjectKind,
    val origin: EuclideanPosition,
    val direction: EuclideanPosition,
    val definingPointIds: List<String>,
    val contains: Set<String>,
    val label: String?
)
data class EuclideanCircle(
    val id: String,
    val centerPointId: String,
    val center: EuclideanPosition,
    val radius: Double,
    val contains: Set<String>,
    val filled: Boolean = false,
    val label: String? = null
)
data class EuclideanArc(
    val id: String,
    val circleId: String,
    val throughPointIds: List<String>,
    val contains: Set<String>,
    val label: String? = null
)

/** One point with several labels represents several names of the same ideal position.
 * Distinct point IDs must have distinct positions. No labels/coordinates are generated. */
data class EuclideanDiagram(
    val points: List<EuclideanPoint>,
    val objects: List<EuclideanObject>,
    val circles: List<EuclideanCircle> = emptyList(),
    val arcs: List<EuclideanArc> = emptyList()
) {
    fun contains(objectId: String, pointId: String): Boolean {
        require(points.any { it.id == pointId }) { "Unknown Euclidean point: $pointId" }
        return (objects.singleOrNull { it.id == objectId }?.contains
            ?: circles.singleOrNull { it.id == objectId }?.contains
            ?: arcs.singleOrNull { it.id == objectId }?.contains)?.contains(pointId)
            ?: throw IllegalArgumentException("Unknown Euclidean object: $objectId")
    }

    companion object {
        fun parse(spec: JSONObject): EuclideanDiagram {
            val config = spec.optJSONObject("config") ?: spec
            val rawPoints = array(config, "points", 1, 32)
            val pointIds = mutableSetOf<String>()
            val nameOwners = mutableSetOf<String>()
            val points = (0 until rawPoints.length()).map { index ->
                val raw = rawPoints.optJSONObject(index) ?: error("Euclidean points must be objects")
                val id = identifier(raw, "id")
                require(pointIds.add(id)) { "Duplicate Euclidean point ID: $id" }
                val position = EuclideanPosition(number(raw, "x"), number(raw, "y"))
                val labels = if (raw.has("labels")) strings(array(raw, "labels", 0, 8), "labels", false) else emptyList()
                require(labels.all { nameOwners.add(it) }) { "A point name must identify one position; use labels on the same point for aliases" }
                EuclideanPoint(id, position, labels)
            }
            for (a in points.indices) for (b in a + 1 until points.size) {
                require(points[a].position != points[b].position) { "Coincident names belong to one point with multiple labels" }
            }
            val pointMap = points.associateBy { it.id }
            val rawObjects = array(config, "objects", 0, 32)
            val objectIds = mutableSetOf<String>()
            val objects = (0 until rawObjects.length()).map { index ->
                val raw = rawObjects.optJSONObject(index) ?: error("Euclidean objects must be objects")
                val id = identifier(raw, "id")
                require(id !in pointIds && objectIds.add(id)) { "Duplicate Euclidean ID: $id" }
                val kind = when (raw.opt("kind")) {
                    "line" -> EuclideanObjectKind.LINE
                    "ray" -> EuclideanObjectKind.RAY
                    "segment" -> EuclideanObjectKind.SEGMENT
                    else -> error("Euclidean kind must be line, ray or segment")
                }
                val defined: List<String>
                val origin: EuclideanPosition
                val direction: EuclideanPosition
                if (raw.has("through")) {
                    require(!raw.has("origin") && !raw.has("direction")) { "Choose through or origin/direction" }
                    defined = strings(array(raw, "through", 2, 2), "through", true)
                    require(defined.distinct().size == 2) { "Two distinct points are required" }
                    origin = pointMap[defined[0]]?.position ?: error("Unknown Euclidean point")
                    direction = (pointMap[defined[1]]?.position ?: error("Unknown Euclidean point")) - origin
                } else {
                    require(kind != EuclideanObjectKind.SEGMENT) { "A segment requires through with its two endpoints" }
                    val originId = identifier(raw, "origin")
                    defined = listOf(originId)
                    origin = pointMap[originId]?.position ?: error("Unknown Euclidean origin")
                    val vector = raw.optJSONObject("direction") ?: error("Euclidean direction must be an object")
                    direction = EuclideanPosition(number(vector, "x"), number(vector, "y"))
                }
                require(hypot(direction.x, direction.y) >= 1e-9) { "A Euclidean direction must be nonzero" }
                val contains = strings(array(raw, "contains", 1, 32), "contains", true).toSet()
                require(contains.all { it in pointMap } && contains.containsAll(defined)) { "Membership must name known points and include defining points" }
                val label = if (raw.has("label")) label(raw.opt("label")) else null
                val obj = EuclideanObject(id, kind, origin, direction, defined, contains, label)
                points.forEach { point ->
                    require(isOnObject(point.position, obj) == (point.id in contains)) {
                        "Declared membership and internal drawing disagree for ${point.id} on $id"
                    }
                }
                obj
            }
            for (a in objects.indices) for (b in a + 1 until objects.size) {
                val first = objects[a]
                val second = objects[b]
                if (first.kind == EuclideanObjectKind.LINE && second.kind == EuclideanObjectKind.LINE) {
                    require(!sameLine(first, second)) { "Distinct line IDs cannot name the same line" }
                }
            }
            val allIds = (pointIds + objectIds).toMutableSet()
            val rawCircles = optionalArray(config, "circles", 8)
            val circles = (0 until rawCircles.length()).map { index ->
                val raw = rawCircles.optJSONObject(index) ?: error("Euclidean circles must be objects")
                val id = identifier(raw, "id")
                require(allIds.add(id)) { "Duplicate Euclidean ID: $id" }
                val centerId = identifier(raw, "center")
                val center = pointMap[centerId]?.position ?: error("Unknown Euclidean circle center")
                val radius = number(raw, "radius")
                require(radius >= 1e-9) { "A Euclidean radius must be positive and at least 1e-9" }
                val contains = strings(array(raw, "contains", 0, 32), "contains", true).toSet()
                require(contains.all { it in pointMap }) { "Membership must name known points" }
                val filled = if (raw.has("filled")) {
                    val value = raw.opt("filled")
                    require(value is Boolean) { "Euclidean filled must be a boolean" }
                    value
                } else false
                val circle = EuclideanCircle(id, centerId, center, radius, contains, filled,
                    if (raw.has("label")) label(raw.opt("label")) else null)
                points.forEach { point ->
                    require(isOnCircle(point.position, circle) == (point.id in contains)) {
                        "Declared boundary membership and internal drawing disagree for ${point.id} on $id"
                    }
                }
                circle
            }
            for (a in circles.indices) for (b in a + 1 until circles.size) {
                val first = circles[a]
                val second = circles[b]
                val tolerance = max(first.radius, second.radius) * 1e-8
                require(hypot(first.center.x - second.center.x, first.center.y - second.center.y) > tolerance ||
                    abs(first.radius - second.radius) > tolerance) { "Distinct circle IDs cannot name the same circumference" }
            }
            val circleMap = circles.associateBy { it.id }
            val rawArcs = optionalArray(config, "arcs", 16)
            val arcs = (0 until rawArcs.length()).map { index ->
                val raw = rawArcs.optJSONObject(index) ?: error("Euclidean arcs must be objects")
                val id = identifier(raw, "id")
                require(allIds.add(id)) { "Duplicate Euclidean ID: $id" }
                val circleId = identifier(raw, "circle")
                val circle = circleMap[circleId] ?: error("Unknown Euclidean arc circle")
                val through = strings(array(raw, "through", 3, 3), "through", true)
                require(through.distinct().size == 3 && through.all { it in circle.contains }) {
                    "An arc requires three distinct points on its circumference"
                }
                val contains = strings(array(raw, "contains", 3, 32), "contains", true).toSet()
                require(contains.all { it in pointMap } && contains.containsAll(through)) {
                    "Arc membership must name known points and include defining points"
                }
                val arc = EuclideanArc(id, circleId, through, contains,
                    if (raw.has("label")) label(raw.opt("label")) else null)
                val angles = arcAngles(arc, circle, pointMap)
                val start = canvasAngle(pointMap.getValue(through[0]).position, circle.center)
                val middle = normalizedDegrees(canvasAngle(pointMap.getValue(through[1]).position, circle.center) - start)
                val end = normalizedDegrees(canvasAngle(pointMap.getValue(through[2]).position, circle.center) - start)
                require(middle > 1e-10 && end > 1e-10 && abs(middle - end) > 1e-10) {
                    "An arc requires three distinct directions from its center"
                }
                points.forEach { point ->
                    require((isOnCircle(point.position, circle) && angleOnArc(canvasAngle(point.position, circle.center), angles.first, angles.second)) ==
                        (point.id in contains)) { "Declared arc membership and internal drawing disagree for ${point.id} on $id" }
                }
                arc
            }
            return EuclideanDiagram(points, objects, circles, arcs)
        }

        private fun optionalArray(raw: JSONObject, key: String, maximum: Int): JSONArray =
            if (raw.has(key)) array(raw, key, 0, maximum) else JSONArray()

        private fun array(raw: JSONObject, key: String, minimum: Int, maximum: Int): JSONArray {
            val result = raw.optJSONArray(key) ?: error("Euclidean $key must be an array")
            require(result.length() in minimum..maximum) { "Euclidean $key size is outside $minimum..$maximum" }
            return result
        }
        private fun identifier(raw: JSONObject, key: String): String {
            val id = raw.opt(key)
            require(id is String && id.matches(Regex("[A-Za-z][A-Za-z0-9_-]{0,63}"))) { "Invalid Euclidean $key" }
            return id
        }
        private fun label(value: Any?): String {
            require(value is String && value.isNotBlank() && value.length <= 128 && value.none { it.isISOControl() }) { "Invalid Euclidean label" }
            return value
        }
        private fun strings(raw: JSONArray, name: String, ids: Boolean): List<String> = (0 until raw.length()).map { index ->
            val value = raw.opt(index)
            if (ids) {
                require(value is String && value.matches(Regex("[A-Za-z][A-Za-z0-9_-]{0,63}"))) { "Invalid Euclidean $name ID" }
                value
            } else label(value)
        }.also { require(it.distinct().size == it.size) { "Duplicate Euclidean $name" } }

        private fun number(raw: JSONObject, key: String): Double {
            val value = raw.opt(key)
            require(value is Number && value.toDouble().isFinite() && abs(value.toDouble()) <= 1_000_000) { "Euclidean $key must be a finite internal number" }
            // IEEE signed zero is one geometric position/direction component.
            return value.toDouble().let { if (it == 0.0) 0.0 else it }
        }
        private fun isOnObject(point: EuclideanPosition, obj: EuclideanObject): Boolean {
            val delta = point - obj.origin
            val length = hypot(obj.direction.x, obj.direction.y)
            // Layout consistency, not a validator or an inferred curricular relationship.
            if (abs(delta.x * obj.direction.y - delta.y * obj.direction.x) / length > 1e-8) return false
            val parameter = (delta.x * obj.direction.x + delta.y * obj.direction.y) / (length * length)
            return when (obj.kind) {
                EuclideanObjectKind.LINE -> true
                EuclideanObjectKind.RAY -> parameter >= -1e-8
                EuclideanObjectKind.SEGMENT -> parameter >= -1e-8 && parameter <= 1.0 + 1e-8
            }
        }
        private fun sameLine(first: EuclideanObject, second: EuclideanObject): Boolean {
            val cross = first.direction.x * second.direction.y - first.direction.y * second.direction.x
            return abs(cross) / (hypot(first.direction.x, first.direction.y) * hypot(second.direction.x, second.direction.y)) < 1e-12 &&
                isOnObject(second.origin, first)
        }
        private fun isOnCircle(point: EuclideanPosition, circle: EuclideanCircle): Boolean =
            abs(hypot(point.x - circle.center.x, point.y - circle.center.y) - circle.radius) <= circle.radius * 1e-8
    }
}

/** Clockwise Canvas angles are internal drawing data, never displayed measurements. */
private fun canvasAngle(point: EuclideanPosition, center: EuclideanPosition): Double =
    normalizedDegrees(Math.toDegrees(atan2(-(point.y - center.y), point.x - center.x)))
private fun normalizedDegrees(angle: Double): Double = ((angle % 360.0) + 360.0) % 360.0
private fun arcAngles(arc: EuclideanArc, circle: EuclideanCircle, points: Map<String, EuclideanPoint>): Pair<Double, Double> {
    val start = canvasAngle(points.getValue(arc.throughPointIds[0]).position, circle.center)
    val middle = normalizedDegrees(canvasAngle(points.getValue(arc.throughPointIds[1]).position, circle.center) - start)
    val end = normalizedDegrees(canvasAngle(points.getValue(arc.throughPointIds[2]).position, circle.center) - start)
    return start to if (middle < end) end else end - 360.0
}
private fun angleOnArc(angle: Double, start: Double, sweep: Double): Boolean {
    val distance = if (sweep >= 0) normalizedDegrees(angle - start) else normalizedDegrees(start - angle)
    // An exact start must also survive normalization around the 360-degree seam.
    return distance <= abs(sweep) + 1e-7 || distance >= 360.0 - 1e-7
}

data class EuclideanRect(val left: Double, val top: Double, val right: Double, val bottom: Double) {
    val width get() = right - left
    val height get() = bottom - top
    fun contains(point: EuclideanPosition) = point.x >= left && point.x <= right && point.y >= top && point.y <= bottom
    fun overlaps(other: EuclideanRect) = left < other.right && right > other.left && top < other.bottom && bottom > other.top
}
data class EuclideanDrawnPoint(val id: String, val center: EuclideanPosition)
data class EuclideanDrawnObject(val id: String, val kind: EuclideanObjectKind, val start: EuclideanPosition, val end: EuclideanPosition, val startArrow: Boolean, val endArrow: Boolean)
data class EuclideanDrawnCircle(val id: String, val center: EuclideanPosition, val radius: Double, val filled: Boolean)
data class EuclideanDrawnArc(val id: String, val circleId: String, val center: EuclideanPosition, val radius: Double, val startDegrees: Double, val sweepDegrees: Double)
data class EuclideanArrow(val objectId: String, val tip: EuclideanPosition, val wingA: EuclideanPosition, val wingB: EuclideanPosition)
data class EuclideanLabelMetrics(val lines: List<String>, val width: Double, val lineHeight: Double, val ascent: Double)
data class EuclideanDrawnLabel(val ownerId: String, val text: String, val lines: List<String>, val bounds: EuclideanRect, val lineHeight: Double, val ascent: Double)
data class EuclideanDrawingSnapshot(
    val viewport: EuclideanRect,
    val plotBounds: EuclideanRect,
    val points: List<EuclideanDrawnPoint>,
    val paths: List<EuclideanDrawnObject>,
    val arrows: List<EuclideanArrow>,
    val labels: List<EuclideanDrawnLabel>,
    val circles: List<EuclideanDrawnCircle> = emptyList(),
    val arcs: List<EuclideanDrawnArc> = emptyList()
)

/** A pure pixel-space layout: uniform transform, parametric clipping and shared draw primitives.
 * The caller supplies font metrics, so Canvas and its inspectable snapshot use the same layout. */
object EuclideanDiagramLayout {
    fun layout(diagram: EuclideanDiagram, width: Double, height: Double, unit: Double = 1.0, textSize: Double = 15.0,
        measure: (String, Double) -> EuclideanLabelMetrics = { text, maximum -> approximateLabel(text, maximum, textSize) }
    ): EuclideanDrawingSnapshot {
        require(width.isFinite() && height.isFinite() && width > 0 && height > 0 && unit > 0 && unit.isFinite() && textSize > 0 && textSize.isFinite())
        val edge = min(12 * unit, min(width, height) * .08)
        val viewport = EuclideanRect(0.0, 0.0, width, height)
        val padding = min(max(30 * unit, textSize * 1.8), min(width, height) * .24)
        val plot = EuclideanRect(padding, padding, width - padding, height - padding)
        val minX = min(diagram.points.minOf { it.position.x }, diagram.circles.minOfOrNull { it.center.x - it.radius } ?: Double.POSITIVE_INFINITY)
        val maxX = max(diagram.points.maxOf { it.position.x }, diagram.circles.maxOfOrNull { it.center.x + it.radius } ?: Double.NEGATIVE_INFINITY)
        val minY = min(diagram.points.minOf { it.position.y }, diagram.circles.minOfOrNull { it.center.y - it.radius } ?: Double.POSITIVE_INFINITY)
        val maxY = max(diagram.points.maxOf { it.position.y }, diagram.circles.maxOfOrNull { it.center.y + it.radius } ?: Double.NEGATIVE_INFINITY)
        // Retain the established point/line layout exactly. Circular scenes also fit a tiny
        // declared radius instead of silently reducing it to an invisible one-unit scene.
        val minimumSpan = if (diagram.circles.isEmpty()) 1.0 else 1e-9
        val xSpan = max(maxX - minX, minimumSpan)
        val ySpan = max(maxY - minY, minimumSpan)
        val factor = min(plot.width / (xSpan * 1.8), plot.height / (ySpan * 1.8))
        val worldCenter = EuclideanPosition((minX + maxX) / 2, (minY + maxY) / 2)
        val canvasCenter = EuclideanPosition(width / 2, height / 2)
        fun project(position: EuclideanPosition): EuclideanPosition = canvasCenter + EuclideanPosition((position.x - worldCenter.x) * factor, -(position.y - worldCenter.y) * factor)
        val points = diagram.points.map { EuclideanDrawnPoint(it.id, project(it.position)) }
        val paths = diagram.objects.mapNotNull { obj ->
            val origin = project(obj.origin)
            val vector = if (obj.kind == EuclideanObjectKind.SEGMENT) obj.direction else
                obj.direction * (1 / hypot(obj.direction.x, obj.direction.y))
            val direction = EuclideanPosition(vector.x * factor, -vector.y * factor)
            clip(origin, direction, plot, obj.kind)?.let { (start, end) ->
                EuclideanDrawnObject(obj.id, obj.kind, start, end, obj.kind == EuclideanObjectKind.LINE, obj.kind != EuclideanObjectKind.SEGMENT)
            }
        }
        val arrows = paths.flatMap { obj ->
            val delta = obj.end - obj.start
            val length = hypot(delta.x, delta.y)
            val direction = delta * (1 / length)
            val size = min(10 * unit, length * .2)
            buildList {
                if (obj.startArrow) add(arrow(obj.id, obj.start, direction * -1.0, size))
                if (obj.endArrow) add(arrow(obj.id, obj.end, direction, size))
            }
        }
        val circles = diagram.circles.map { EuclideanDrawnCircle(it.id, project(it.center), it.radius * factor, it.filled) }
        val circleMap = diagram.circles.associateBy { it.id }
        val pointMap = diagram.points.associateBy { it.id }
        val arcs = diagram.arcs.map { arc ->
            val circle = circleMap.getValue(arc.circleId)
            val angles = arcAngles(arc, circle, pointMap)
            EuclideanDrawnArc(arc.id, circle.id, project(circle.center), circle.radius * factor, angles.first, angles.second)
        }
        val labels = mutableListOf<EuclideanDrawnLabel>()
        fun addLabel(ownerId: String, text: String, anchor: EuclideanPosition, allowEdgeCandidates: Boolean = false) {
            val metrics = measure(text, min(width * .43, width - 2 * edge))
            require(metrics.lines.isNotEmpty() && metrics.width.isFinite() && metrics.width >= 0 && metrics.lineHeight.isFinite() && metrics.lineHeight > 0)
            val labelWidth = metrics.width + 4 * unit
            val labelHeight = metrics.lines.size * metrics.lineHeight + 2 * unit
            val gap = max(10 * unit, textSize * .55)
            val candidates = listOf(
                EuclideanPosition(anchor.x + gap, anchor.y - labelHeight - gap),
                EuclideanPosition(anchor.x + gap, anchor.y + gap),
                EuclideanPosition(anchor.x - labelWidth - gap, anchor.y - labelHeight - gap),
                EuclideanPosition(anchor.x - labelWidth - gap, anchor.y + gap),
                EuclideanPosition(anchor.x - labelWidth / 2, anchor.y - labelHeight - gap),
                EuclideanPosition(anchor.x - labelWidth / 2, anchor.y + gap),
                EuclideanPosition(anchor.x + gap, anchor.y - labelHeight / 2),
                EuclideanPosition(anchor.x - labelWidth - gap, anchor.y - labelHeight / 2)
            ) + if (!allowEdgeCandidates) emptyList() else listOf(
                EuclideanPosition(edge, edge),
                EuclideanPosition(width - labelWidth - edge, edge),
                EuclideanPosition(edge, height - labelHeight - edge),
                EuclideanPosition(width - labelWidth - edge, height - labelHeight - edge),
                EuclideanPosition((width - labelWidth) / 2, edge),
                EuclideanPosition((width - labelWidth) / 2, height - labelHeight - edge),
                EuclideanPosition(edge, (height - labelHeight) / 2),
                EuclideanPosition(width - labelWidth - edge, (height - labelHeight) / 2)
            )
            fun rect(position: EuclideanPosition): EuclideanRect {
                val x = position.x.coerceIn(edge, max(edge, width - labelWidth - edge))
                val y = position.y.coerceIn(edge, max(edge, height - labelHeight - edge))
                return EuclideanRect(x, y, x + labelWidth, y + labelHeight)
            }
            val selected = candidates.map(::rect).minBy { candidate ->
                val labelHits = labels.count { candidate.overlaps(it.bounds) }
                val pointHits = points.count { candidate.contains(it.center) }
                val pathHits = paths.count { crosses(it.start, it.end, candidate) }
                val circleHits = circles.count { circumferenceCrosses(it.center, it.radius, candidate) }
                val arcHits = arcs.count { arcCrosses(it, candidate) }
                labelHits * 10000 + pointHits * 1000 + (pathHits + circleHits + arcHits) * 100
            }
            labels.add(EuclideanDrawnLabel(ownerId, text, metrics.lines, selected, metrics.lineHeight, metrics.ascent))
        }
        diagram.points.forEach { point ->
            val anchor = points.single { it.id == point.id }.center
            point.labels.forEach { addLabel(point.id, it, anchor) }
        }
        diagram.objects.forEach { obj ->
            val path = paths.singleOrNull { it.id == obj.id }
            if (obj.label != null && path != null) addLabel(obj.id, obj.label, path.start + (path.end - path.start) * .72)
        }
        diagram.circles.forEach { circle ->
            if (circle.label != null) {
                val drawn = circles.single { it.id == circle.id }
                addLabel(circle.id, circle.label, drawn.center + EuclideanPosition(drawn.radius * .7071067811865476, -drawn.radius * .7071067811865476), allowEdgeCandidates = true)
            }
        }
        diagram.arcs.forEach { arc ->
            if (arc.label != null) {
                val drawn = arcs.single { it.id == arc.id }
                val middle = Math.toRadians(drawn.startDegrees + drawn.sweepDegrees * .5)
                addLabel(arc.id, arc.label, drawn.center + EuclideanPosition(cos(middle) * drawn.radius, sin(middle) * drawn.radius), allowEdgeCandidates = true)
            }
        }
        return EuclideanDrawingSnapshot(viewport, plot, points, paths, arrows, labels, circles, arcs)
    }

    fun clip(origin: EuclideanPosition, direction: EuclideanPosition, box: EuclideanRect, kind: EuclideanObjectKind): Pair<EuclideanPosition, EuclideanPosition>? {
        if (!origin.x.isFinite() || !origin.y.isFinite() || !direction.x.isFinite() || !direction.y.isFinite()) return null
        var lower = if (kind == EuclideanObjectKind.LINE) Double.NEGATIVE_INFINITY else 0.0
        var upper = if (kind == EuclideanObjectKind.SEGMENT) 1.0 else Double.POSITIVE_INFINITY
        for ((coordinate, delta, minimum, maximum) in listOf(doubleArrayOf(origin.x, direction.x, box.left, box.right), doubleArrayOf(origin.y, direction.y, box.top, box.bottom))) {
            if (delta == 0.0) {
                if (coordinate < minimum || coordinate > maximum) return null
            } else {
                val first = (minimum - coordinate) / delta
                val second = (maximum - coordinate) / delta
                lower = max(lower, min(first, second))
                upper = min(upper, max(first, second))
                if (upper <= lower) return null
            }
        }
        if (!lower.isFinite() || !upper.isFinite() || upper <= lower) return null
        return origin + direction * lower to origin + direction * upper
    }

    private fun crosses(start: EuclideanPosition, end: EuclideanPosition, box: EuclideanRect) = clip(start, end - start, box, EuclideanObjectKind.SEGMENT) != null
    private fun circumferenceCrosses(center: EuclideanPosition, radius: Double, box: EuclideanRect): Boolean {
        val nearest = EuclideanPosition(center.x.coerceIn(box.left, box.right), center.y.coerceIn(box.top, box.bottom))
        val minimum = hypot(nearest.x - center.x, nearest.y - center.y)
        val maximum = max(max(hypot(box.left - center.x, box.top - center.y), hypot(box.right - center.x, box.top - center.y)),
            max(hypot(box.left - center.x, box.bottom - center.y), hypot(box.right - center.x, box.bottom - center.y)))
        return minimum <= radius && maximum >= radius
    }
    private fun arcCrosses(arc: EuclideanDrawnArc, box: EuclideanRect): Boolean {
        if (!circumferenceCrosses(arc.center, arc.radius, box)) return false
        // Used only to rank label candidates; all actual drawing uses the exact shared arc.
        val divisions = max(1, (abs(arc.sweepDegrees) / 5.0).toInt())
        fun sample(index: Int): EuclideanPosition {
            val angle = Math.toRadians(arc.startDegrees + arc.sweepDegrees * index / divisions)
            return arc.center + EuclideanPosition(cos(angle) * arc.radius, sin(angle) * arc.radius)
        }
        return (0 until divisions).any { index -> crosses(sample(index), sample(index + 1), box) }
    }
    private fun arrow(id: String, tip: EuclideanPosition, direction: EuclideanPosition, size: Double): EuclideanArrow {
        val base = tip - direction * size
        val normal = EuclideanPosition(-direction.y, direction.x) * (size * .48)
        return EuclideanArrow(id, tip, base + normal, base - normal)
    }
    private fun approximateLabel(text: String, maximum: Double, textSize: Double): EuclideanLabelMetrics {
        val capacity = max(1, (maximum / (textSize * .58)).toInt())
        val lines = text.chunked(capacity)
        return EuclideanLabelMetrics(lines, min(maximum, lines.maxOf { it.length } * textSize * .58), textSize * 1.25, textSize * .82)
    }
}
