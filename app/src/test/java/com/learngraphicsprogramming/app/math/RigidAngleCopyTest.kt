package com.learngraphicsprogramming.app.math

import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import kotlin.math.*

class RigidAngleCopyTest {
    @Test fun congruenceUsesCompleteDirectedRaysNotTheNamingPointDistances() {
        val model = RigidAngleCopySpec.parse(spec())
        val answer = RigidAnglePlacement(1.0, 2.0, -.7)
        assertEquals(RigidAngleCondition.SATISFIED, model.evaluate(answer))
        val transformed = model.copy.moved(answer, "copy:")
        assertEquals(model.reference.vertex, transformed.points.first().position)
        // Naming points intentionally have different distances on both sides.
        assertNotEquals(model.reference.diagram.points[1].position, transformed.points[1].position)
        assertTrue(MathValidators.validate(spec(), MathAnswer.RigidAngle(answer)).correct)
    }

    @Test fun rejectsWrongVertexOppositeSenseNoncollinearityAndOppositeRegionsWithSpecificGuidance() {
        val model = RigidAngleCopySpec.parse(spec())
        assertEquals(RigidAngleCondition.VERTEX, model.evaluate(RigidAnglePlacement(2.0, 2.0, -.7)))
        assertEquals(RigidAngleCondition.DIRECTION, model.evaluate(RigidAnglePlacement(1.0, 2.0, Math.PI-.7)))
        assertEquals(RigidAngleCondition.FIRST_SIDE, model.evaluate(RigidAnglePlacement(1.0, 2.0, -.4)))
        assertEquals(RigidAngleCondition.FIRST_SIDE, model.evaluate(RigidAnglePlacement(1.0, 2.0, Math.PI/2-.7)))
        val opposite = model.copy(copy = model.copy.copy(counterclockwise = !model.copy.counterclockwise))
        assertEquals(RigidAngleCondition.REGION, opposite.evaluate(RigidAnglePlacement(1.0, 2.0, -.7)))
        for (placement in listOf(RigidAnglePlacement(2.0, 2.0, -.7), RigidAnglePlacement(1.0, 2.0, Math.PI-.7))) {
            val result = MathValidators.validate(spec(), MathAnswer.RigidAngle(placement))
            assertFalse(result.correct)
            assertTrue(result.evaluable)
            assertNotNull(result.uiMessage)
        }
    }

    @Test fun anInitiallyOppositeInteriorCanBeCongruentWithExplicitlyReversedSideCorrespondence() {
        val raw = spec().put("reference", figureJson("O", "A", "B", 0.0, 0.0, 0.0, Math.PI/2, 4.0, 5.0))
        val moving = figureJson("V", "C", "D", 0.0, 0.0, 0.0, -Math.PI/2, 6.0, 3.0)
            .put("sides", JSONArray(listOf("second", "first")))
        // The selected lower quadrant is unchanged: from the lower ray to the right ray
        // counterclockwise names the same region previously described clockwise.
        raw.put("copy", moving)
        val model = RigidAngleCopySpec.parse(raw)
        assertEquals(RigidAngleCondition.FIRST_SIDE, model.evaluate(RigidAnglePlacement(0.0, 0.0, 0.0)))
        assertEquals(RigidAngleCondition.SATISFIED, model.evaluate(RigidAnglePlacement(0.0, 0.0, Math.PI/2)))
        assertEquals(listOf("second", "first"), model.copy.sideIds)
    }

    @Test fun inclusionKeepsCorrespondenceAndDirectionOfContainment() {
        val reference = RigidAngleCopySpec.parse(spec())
        val small = reference.copy(copy = figure("V", "C", "D", 0.0, 0.0, .7, .4, 2.0, 7.0), relation = "copy_inside_reference")
        val aligned = RigidAnglePlacement(1.0, 2.0, -.7)
        assertEquals(RigidAngleCondition.SATISFIED, small.evaluate(aligned))
        assertEquals(RigidAngleCondition.INCLUSION, small.copy(relation = "reference_inside_copy").evaluate(aligned))
        assertEquals(RigidAngleCondition.SECOND_SIDE, small.copy(relation = "congruent").evaluate(aligned))
        val large = reference.copy(copy = figure("V", "C", "D", 0.0, 0.0, .7, 1.4, 9.0, 3.0), relation = "reference_inside_copy")
        assertEquals(RigidAngleCondition.SATISFIED, large.evaluate(aligned))
        assertEquals(RigidAngleCondition.INCLUSION, large.copy(relation = "copy_inside_reference").evaluate(aligned))
    }

    @Test fun independentOrientationsTranslationsAndCompleteTurnsHaveTheSameGeometricResult() {
        for (orientation in listOf(-2.5, -1.0, 0.0, .8, 2.6)) {
            val model = RigidAngleCopySpec.parse(spec()).copy(reference = figure("O", "A", "B", -4.0, 7.0, orientation, 1.0, 3.0, 6.0))
            for (turns in -3..3) assertEquals(RigidAngleCondition.SATISFIED,
                model.evaluate(RigidAnglePlacement(-4.0, 7.0, orientation-.7+turns*2*Math.PI)))
        }
    }

    @Test fun rigidMotionsPreserveEveryDistanceNameIncidenceAndSelectedRegion() {
        val figure = RigidAngleCopySpec.parse(spec()).copy
        for (angle in listOf(-2.0, 0.0, 1.7, 6.3)) {
            val moved = figure.moved(RigidAnglePlacement(-8.0, 3.0, angle), "copy:")
            for (i in figure.diagram.points.indices) for (j in figure.diagram.points.indices) {
                fun distance(points: List<EuclideanPoint>) = (points[i].position-points[j].position).let { hypot(it.x, it.y) }
                assertEquals(distance(figure.diagram.points), distance(moved.points), 1e-10)
            }
            assertEquals(figure.diagram.points.map { it.labels }, moved.points.map { it.labels })
            assertTrue(moved.objects.all { it.kind == EuclideanObjectKind.RAY && it.contains.all { id -> id.startsWith("copy:") } })
            assertEquals(figure.sweep, directedAngle(moved.objects[0].direction, moved.objects[1].direction, figure.counterclockwise), 1e-10)
        }
        // IDs and even identical naming-point records are local to each figure. Their
        // extent must always be measured from their own vertex, never by record membership.
        val shared = spec().put("reference", figureJson("O", "A", "B", 0.0, 0.0, 0.0, Math.PI/2, 4.0, 5.0))
        val copied = JSONObject(shared.getJSONObject("reference").toString())
        copied.getJSONObject("diagram").getJSONArray("points").getJSONObject(0).put("x", -4.0).put("y", -4.0)
        val localIds = RigidAngleCopySpec.parse(shared.put("copy", copied).put("relation", "copy_inside_reference"))
        assertEquals(5.0, localIds.reference.radius, 1e-10)
        assertEquals(sqrt(97.0), localIds.copy.radius, 1e-10)
        assertEquals(localIds.reference.diagram.points.map { it.id }, localIds.copy.diagram.points.map { it.id })
    }

    @Test fun explicitOperationalToleranceDoesNotTurnOppositeRaysIntoTheSameRay() {
        val model = RigidAngleCopySpec.parse(spec())
        assertEquals(RigidAngleCondition.SATISFIED, model.evaluate(RigidAnglePlacement(1.09, 2.0, -.7)))
        assertEquals(RigidAngleCondition.VERTEX, model.evaluate(RigidAnglePlacement(1.11, 2.0, -.7)))
        assertEquals(RigidAngleCondition.SATISFIED, model.evaluate(RigidAnglePlacement(1.0, 2.0, -.7+.009)))
        assertEquals(RigidAngleCondition.FIRST_SIDE, model.evaluate(RigidAnglePlacement(1.0, 2.0, -.7+.011)))
        assertEquals(RigidAngleCondition.DIRECTION, model.evaluate(RigidAnglePlacement(1.0, 2.0, Math.PI-.7)))
    }

    @Test fun partialAndCorrectPlacementsRoundTripThroughExistingAnswerAndProgressContracts() {
        val block = block()
        val parsed = MathPuzzleParser.parse(block).responses.single()
        assertEquals("math_rigid_angle_copy", parsed.type)
        for (placement in listOf(RigidAnglePlacement(-5.0, 9.0, 2.3), RigidAnglePlacement(1.0, 2.0, -.7))) {
            val answer = MathAnswer.RigidAngle(placement)
            assertEquals(answer, MathAnswerJson.decode(MathAnswerJson.encode(answer)))
            val state = JSONObject().put("correct", true).put("contentFingerprint", MathPuzzleState.fingerprint(block))
                .put("responses", JSONObject().put(parsed.id, MathAnswerJson.encode(answer)))
            assertEquals(placement.x == 1.0, MathPuzzleState.isCurrentCorrect(block, state))
        }
        assertNull(MathAnswerJson.decode(JSONObject().put("type", "rigid_angle").put("x", "NaN").put("y", 0).put("rotation", 0)))
    }

    @Test fun malformedContractsAndWrongTypedAnswersAreTechnicalFailuresNotStudentMistakes() {
        val opposite = spec().also { it.getJSONObject("copy").put("region", "clockwise") }
        val unequal = spec().put("copy", figureJson("V", "C", "D", 0.0, 0.0, .7, .5, 4.0, 5.0))
        val impossibleInside = spec().put("relation", "copy_inside_reference").put("copy", figureJson("V", "C", "D", 0.0, 0.0, .7, 1.4, 4.0, 5.0))
        val impossibleOutside = spec().put("relation", "reference_inside_copy").put("copy", figureJson("V", "C", "D", 0.0, 0.0, .7, .4, 4.0, 5.0))
        for (bad in listOf(spec().put("directionTolerance", -1), spec().put("relation", "any"), spec().put("positionTolerance", 0), opposite, unequal, impossibleInside, impossibleOutside)) {
            try { MathValidators.validateSpec(bad); fail("Invalid opt-in contract must be rejected") } catch (_: IllegalArgumentException) { }
            assertFalse(MathValidators.validate(bad, MathAnswer.RigidAngle(RigidAnglePlacement(0.0, 0.0, 0.0))).evaluable)
        }
        assertFalse(MathValidators.validate(spec(), MathAnswer.Graph(emptyList())).evaluable)
        for (sweep in listOf(0.0, Math.PI, 4.0)) {
            val raw = spec().put("copy", figureJson("V", "C", "D", 0.0, 0.0, 0.0, sweep, 4.0, 5.0))
            try { RigidAngleCopySpec.parse(raw); fail("Degenerate or reflex initial regions are not supported") } catch (_: IllegalArgumentException) { }
        }
    }

    @Test fun fixedViewportDoesNotMoveTheReferenceWhileTheCopyMovesOrRotates() {
        val spec = RigidAngleCopySpec.parse(spec())
        val bounds = EuclideanRect(-8.0, -8.0, 15.0, 15.0)
        fun draw(placement: RigidAnglePlacement): EuclideanDrawingSnapshot {
            val first = spec.reference.moved(spec.referencePlacement, "r:")
            val second = spec.copy.moved(placement, "c:")
            return EuclideanDiagramLayout.layoutInBounds(EuclideanDiagram(first.points+second.points, first.objects+second.objects),
                720.0, 540.0, bounds = bounds)
        }
        val before = draw(spec.initial)
        val after = draw(RigidAnglePlacement(-4.0, -1.0, 2.0))
        assertEquals(before.points.filter { it.id.startsWith("r:") }, after.points.filter { it.id.startsWith("r:") })
        assertEquals(before.paths.filter { it.id.startsWith("r:") }, after.paths.filter { it.id.startsWith("r:") })
        assertEquals(4, before.arrows.size)
        assertTrue(before.paths.all { !it.startArrow && it.endArrow })
    }

    companion object {
        fun spec(): JSONObject = JSONObject().put("type", "rigid_angle_copy")
            .put("reference", figureJson("O", "A", "B", 1.0, 2.0, 0.0, 1.0, 4.0, 5.0))
            .put("copy", figureJson("V", "C", "D", 0.0, 0.0, .7, 1.0, 6.0, 3.0))
            .put("initial", JSONObject().put("x", 7).put("y", 6).put("rotation", .2))
            .put("relation", "congruent").put("positionTolerance", .1).put("directionTolerance", .01)
        fun block(): JSONObject = JSONObject().put("type", "math_rigid_angle_copy").put("id", "copy")
            .put("prompt", "Traslada y gira la copia conservando la región y la correspondencia de lados.").put("validator", spec())
        private fun figure(vertex: String, a: String, b: String, x: Double, y: Double, orientation: Double, sweep: Double, firstDistance: Double, secondDistance: Double) =
            RigidAngleFigure.parse(figureJson(vertex, a, b, x, y, orientation, sweep, firstDistance, secondDistance))
        private fun figureJson(vertex: String, a: String, b: String, x: Double, y: Double, orientation: Double, sweep: Double, firstDistance: Double, secondDistance: Double): JSONObject {
            fun point(id: String, angle: Double, distance: Double) = JSONObject().put("id", id).put("x", x+cos(angle)*distance).put("y", y+sin(angle)*distance).put("labels", JSONArray(listOf(id)))
            fun ray(id: String, endpoint: String) = JSONObject().put("id", id).put("kind", "ray").put("through", JSONArray(listOf(vertex, endpoint))).put("contains", JSONArray(listOf(vertex, endpoint)))
            return JSONObject().put("vertex", vertex).put("sides", JSONArray(listOf("first", "second"))).put("region", "counterclockwise")
                .put("diagram", JSONObject().put("points", JSONArray().put(point(vertex, 0.0, 0.0)).put(point(a, orientation, firstDistance)).put(point(b, orientation+sweep, secondDistance)))
                    .put("objects", JSONArray().put(ray("first", a)).put(ray("second", b))))
        }
    }
}
