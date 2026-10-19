package com.learngraphicsprogramming.app.math

import org.json.JSONObject
import kotlin.math.*

/** A rigid motion, never a list of independently movable vertices. Rotation is internal only. */
data class RigidAnglePlacement(val x: Double, val y: Double, val rotation: Double) {
    init { require(listOf(x, y, rotation).all { it.isFinite() }) }
    fun rotate(vector: EuclideanPosition) = EuclideanPosition(
        vector.x * cos(rotation) - vector.y * sin(rotation),
        vector.x * sin(rotation) + vector.y * cos(rotation))
}

data class RigidAngleFigure(val diagram: EuclideanDiagram, val vertexId: String,
    val sideIds: List<String>, val counterclockwise: Boolean) {
    val vertex get() = diagram.points.single { it.id == vertexId }.position
    val sides get() = sideIds.map { id -> diagram.objects.single { it.id == id } }
    val sweep get() = directedAngle(sides[0].direction, sides[1].direction, counterclockwise)
    val radius get() = diagram.points.maxOf { point -> hypot(point.position.x-vertex.x, point.position.y-vertex.y) }

    fun moved(placement: RigidAnglePlacement, prefix: String): EuclideanDiagram {
        fun point(position: EuclideanPosition) = EuclideanPosition(placement.x, placement.y) + placement.rotate(position - vertex)
        return EuclideanDiagram(diagram.points.map { it.copy(id = prefix + it.id, position = point(it.position)) },
            diagram.objects.map { it.copy(id = prefix + it.id, origin = point(it.origin), direction = placement.rotate(it.direction),
                definingPointIds = it.definingPointIds.map { id -> prefix + id }, contains = it.contains.map { id -> prefix + id }.toSet()) })
    }

    companion object {
        fun parse(raw: JSONObject): RigidAngleFigure {
            val diagram = EuclideanDiagram.parse(raw.getJSONObject("diagram"))
            val vertex = raw.getString("vertex")
            require(diagram.points.any { it.id == vertex }) { "Unknown angle vertex" }
            val sides = raw.getJSONArray("sides").let { array ->
                require(array.length() == 2)
                (0..1).map { array.getString(it) }
            }
            require(sides.distinct().size == 2 && diagram.objects.size == 2 && diagram.circles.isEmpty() && diagram.arcs.isEmpty()) {
                "A rigid angle requires exactly its two rays"
            }
            require(diagram.objects.all { it.id in sides && it.kind == EuclideanObjectKind.RAY && it.origin == diagram.points.single { p -> p.id == vertex }.position }) {
                "Both complete rays must start at the declared vertex"
            }
            val region = raw.getString("region")
            require(region in setOf("counterclockwise", "clockwise"))
            val figure = RigidAngleFigure(diagram, vertex, sides, region == "counterclockwise")
            require(figure.sweep > 1e-6 && figure.sweep < Math.PI - 1e-6) { "Initial support is limited to ordinary nondegenerate regions" }
            return figure
        }
    }
}

/** No naming-point coincidence is required: only the vertex, directed rays and selected region. */
data class RigidAngleCopySpec(val reference: RigidAngleFigure, val copy: RigidAngleFigure,
    val initial: RigidAnglePlacement, val relation: String, val positionTolerance: Double, val directionTolerance: Double) {
    val referencePlacement get() = RigidAnglePlacement(reference.vertex.x, reference.vertex.y, 0.0)
    fun evaluate(placement: RigidAnglePlacement): RigidAngleCondition {
        if (hypot(placement.x - reference.vertex.x, placement.y - reference.vertex.y) > positionTolerance) return RigidAngleCondition.VERTEX
        val referenceSide = reference.sides[0].direction
        val copySide = placement.rotate(copy.sides[0].direction)
        val alignment = abs(signedAngle(referenceSide, copySide))
        if (abs(alignment-Math.PI) <= directionTolerance) return RigidAngleCondition.DIRECTION
        if (alignment > directionTolerance) return RigidAngleCondition.FIRST_SIDE
        if (reference.counterclockwise != copy.counterclockwise) return RigidAngleCondition.REGION
        val movedSecond = placement.rotate(copy.sides[1].direction)
        val refSecond = reference.sides[1].direction
        val difference = copy.sweep - reference.sweep
        val coincident = dot(movedSecond, refSecond) > 0 && abs(signedAngle(refSecond, movedSecond)) <= directionTolerance
        return when (relation) {
            "congruent" -> if (coincident) RigidAngleCondition.SATISFIED else RigidAngleCondition.SECOND_SIDE
            "copy_inside_reference" -> if (difference <= directionTolerance) RigidAngleCondition.SATISFIED else RigidAngleCondition.INCLUSION
            "reference_inside_copy" -> if (difference >= -directionTolerance) RigidAngleCondition.SATISFIED else RigidAngleCondition.INCLUSION
            else -> error("Invalid rigid angle relation")
        }
    }

    companion object {
        fun parse(raw: JSONObject): RigidAngleCopySpec {
            val reference = RigidAngleFigure.parse(raw.getJSONObject("reference"))
            val copy = RigidAngleFigure.parse(raw.getJSONObject("copy"))
            val initial = raw.getJSONObject("initial").let { RigidAnglePlacement(it.getDouble("x"), it.getDouble("y"), it.getDouble("rotation")) }
            val relation = raw.getString("relation")
            require(relation in setOf("congruent", "copy_inside_reference", "reference_inside_copy"))
            val position = raw.getDouble("positionTolerance")
            val direction = raw.getDouble("directionTolerance")
            require(position.isFinite() && position > 0 && direction.isFinite() && direction > 0 && direction < .1) {
                "Declare finite positive operational tolerances, with directionTolerance below 0.1 radian"
            }
            require(reference.counterclockwise == copy.counterclockwise) { "A rigid rotation cannot reverse the selected region with fixed side correspondence" }
            val difference = copy.sweep-reference.sweep
            require(when (relation) {
                "congruent" -> abs(difference) <= 1e-9
                "copy_inside_reference" -> difference <= 1e-9
                else -> difference >= -1e-9
            }) { "The declared rigid-copy goal must be geometrically attainable; operational tolerance does not repair an impossible task" }
            require((reference.diagram.points + copy.diagram.points).all { abs(it.position.x) <= 1e9 && abs(it.position.y) <= 1e9 } &&
                listOf(initial.x, initial.y).all { abs(it) <= 1e9 }) { "Internal placement must remain in the supported finite drawing range" }
            return RigidAngleCopySpec(reference, copy, initial, relation, position, direction)
        }
    }
}

enum class RigidAngleCondition { SATISFIED, VERTEX, DIRECTION, FIRST_SIDE, REGION, SECOND_SIDE, INCLUSION }
internal fun dot(a: EuclideanPosition, b: EuclideanPosition) = a.x * b.x + a.y * b.y
internal fun signedAngle(a: EuclideanPosition, b: EuclideanPosition) = atan2(a.x * b.y - a.y * b.x, dot(a, b))
internal fun directedAngle(a: EuclideanPosition, b: EuclideanPosition, counterclockwise: Boolean): Double {
    val angle = signedAngle(a, b) * if (counterclockwise) 1 else -1
    return (angle + 2 * Math.PI) % (2 * Math.PI)
}
