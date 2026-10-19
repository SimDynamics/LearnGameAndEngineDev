package com.learngraphicsprogramming.app.math

import android.content.res.Configuration
import android.graphics.Color
import android.os.SystemClock
import android.view.InputDevice
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.learngraphicsprogramming.app.LessonActivity
import com.learngraphicsprogramming.app.AppearanceSettingsRepository
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import kotlin.math.*

/** Fresh QA-only blocks exercise free production, never replace any curricular response.
 * Translation/rotation events go through Android's real input pipeline in LDPlayer. */
@RunWith(AndroidJUnit4::class)
class MathRigidAngleCopyDeviceTest : MathGeometryQaSupport() {
    @Test fun freeRigidProductionInBothOrientationsRejectsOppositeRayThenAcceptsAndRestores() {
        for (orientation in listOf(Configuration.ORIENTATION_PORTRAIT, Configuration.ORIENTATION_LANDSCAPE)) {
            val fixture = fixture(block())
            withFixture(fixture) { scenario ->
                orient(scenario, fixture, orientation)
                val visual = expand(scenario)
                capture("rigid-angle-initial-$orientation", { visual }, metadata(visual))
                val fixedBefore = visual.drawingSnapshot().paths.filter { it.id.startsWith("reference:") }
                moveTo(scenario, visual, 0.0, 0.0)
                rotateTo(scenario, visual, Math.PI)
                assertEquals(RigidAngleCondition.DIRECTION, visual.spec.evaluate(visual.placement))
                assertEquals("The reference never follows a copy gesture", fixedBefore, visual.drawingSnapshot().paths.filter { it.id.startsWith("reference:") })
                capture("rigid-angle-opposite-direction-$orientation", { visual }, metadata(visual))
                closeExpanded(visual)
                scenario.onActivity { activity ->
                    check(activity).performClick()
                    assertFalse(state(activity, fixture).getBoolean("correct"))
                    assertTrue(check(activity).isEnabled)
                }
                val again = expand(scenario)
                // A normal accepted gesture is not exactly snapped to the mathematical solution.
                moveTo(scenario, again, .5*again.spec.positionTolerance, 0.0)
                rotateTo(scenario, again, .5*again.spec.directionTolerance)
                assertEquals(RigidAngleCondition.SATISFIED, again.spec.evaluate(again.placement))
                assertTrue(abs(again.placement.x) > .01)
                assertTrue(abs(again.placement.rotation) > .001)
                assertGeometryAndNativeInk(again)
                capture("rigid-angle-accepted-unsnapped-$orientation", { again }, metadata(again))
                val answer = MathAnswer.RigidAngle(again.placement)
                closeExpanded(again)
                scenario.onActivity { activity -> check(activity).performClick(); assertState(activity, fixture, true, answer) }
                scenario.recreate()
                awaitLayout(scenario, fixture, orientation)
                scenario.onActivity { activity ->
                    val restored = visual(activity)
                    assertEquals(answer.placement, restored.placement)
                    assertFalse(restored.editable)
                    assertState(activity, fixture, true, answer)
                }
                val restored = expand(scenario)
                capture("rigid-angle-correct-restored-$orientation", { restored }, metadata(restored))
                closeExpanded(restored)
            }
        }
    }

    @Test fun partialDraftSurvivesRecreationAndReopeningAndLongDragRemainsRecoverable() {
        val fixture = fixture(block())
        var saved: RigidAnglePlacement? = null
        withFixture(fixture) { scenario ->
            val visual = expand(scenario)
            moveTo(scenario, visual, 1.7, -1.3)
            rotateTo(scenario, visual, -.55)
            saved = visual.placement
            assertEquals(RigidAngleCondition.VERTEX, visual.spec.evaluate(requireNotNull(saved)))
            capture("rigid-angle-partial-draft", { visual }, metadata(visual))
            closeExpanded(visual)
            scenario.recreate()
            awaitLayout(scenario, fixture)
            scenario.onActivity { activity ->
                val restored = visual(activity)
                assertEquals(saved, restored.placement)
                assertEquals(MathAnswer.RigidAngle(requireNotNull(saved)), MathAnswerJson.decode(state(activity, fixture)
                    .getJSONObject("responseDrafts").getJSONObject("copy")))
                assertFalse(state(activity, fixture).getBoolean("correct"))
            }
        }
        withFixture(fixture) { scenario ->
            scenario.onActivity { assertEquals(saved, visual(it).placement) }
            val visual = expand(scenario)
            mode(scenario, visual, MathRigidAngleCopyView.Mode.MOVE_COPY)
            // The gesture starts inside the real surface and travels to a physical edge.
            val start = displayPoint(visual, EuclideanPosition(visual.placement.x, visual.placement.y))
            val end = screenPoint(visual, visual.width-2.0, visual.height-2.0)
            drag(start, end)
            instrumentation.runOnMainSync {
                val copy = visual.spec.copy.moved(visual.placement, "copy:")
                for (point in copy.points) {
                    val pixel = visual.project(point.position)
                    assertTrue("Long drag keeps each mark visible: $pixel", pixel.x >= 0 && pixel.x <= visual.width && pixel.y >= 0 && pixel.y <= visual.height)
                }
            }
            assertGeometryAndNativeInk(visual)
            capture("rigid-angle-long-drag-recoverable", { visual }, metadata(visual))
            instrumentation.runOnMainSync {
                descendants(visual.rootView).single { it.tag == "rigid_angle_reset" }.performClick()
                assertEquals(visual.spec.initial, visual.placement)
            }
            capture("rigid-angle-reset-initial", { visual }, metadata(visual))
            closeExpanded(visual)
            scenario.onActivity { activity -> assertFalse(state(activity, fixture).getBoolean("correct")); assertFalse(check(activity).isEnabled) }
        }
    }

    @Test fun obliqueReferenceAndReversedExplicitCorrespondenceAreProducedByFreeGestures() {
        for (case in listOf("oblique", "opposite-interior-reordered-sides")) {
            val source = block()
            val validator = source.getJSONArray("responses").getJSONObject(0).getJSONObject("validator")
            var target = RigidAnglePlacement(-2.0, 1.0, .8)
            if (case == "oblique") {
                val points = validator.getJSONObject("reference").getJSONObject("diagram").getJSONArray("points")
                for (index in 0 until points.length()) {
                    val point = points.getJSONObject(index)
                    val x = point.getDouble("x")
                    val y = point.getDouble("y")
                    point.put("x", -2+x*cos(.8)-y*sin(.8)).put("y", 1+x*sin(.8)+y*cos(.8))
                }
                validator.getJSONObject("initial").put("rotation", -.4)
            } else {
                source.put("prompt", "La copia conserva su región inicialmente inferior. Haz corresponder OA con VD y OB con VC mediante un movimiento rígido, sin reflejarla.")
                val copy = validator.getJSONObject("copy")
                copy.put("sides", JSONArray(listOf("second", "first")))
                copy.getJSONObject("diagram").getJSONArray("points").getJSONObject(2).put("x", 0).put("y", -3)
                validator.getJSONObject("initial").put("rotation", 0)
                target = RigidAnglePlacement(0.0, 0.0, Math.PI/2)
            }
            val fixture = fixture(source)
            withFixture(fixture) { scenario ->
                orient(scenario, fixture, Configuration.ORIENTATION_LANDSCAPE)
                val visual = expand(scenario)
                capture("rigid-angle-$case-initial", { visual }, metadata(visual))
                moveTo(scenario, visual, target.x, target.y)
                rotateTo(scenario, visual, target.rotation)
                assertEquals("The explicit correspondence, not the initial screen orientation, decides validity", RigidAngleCondition.SATISFIED,
                    visual.spec.evaluate(visual.placement))
                assertGeometryAndNativeInk(visual)
                capture("rigid-angle-$case-produced", { visual }, metadata(visual))
                val answer = MathAnswer.RigidAngle(visual.placement)
                closeExpanded(visual)
                scenario.onActivity { activity -> check(activity).performClick(); assertState(activity, fixture, true, answer) }
            }
        }
    }

    @Test fun inclusionAndViewportNavigationUseTheSameCopyWithoutChangingItsShapeOrAnswer() {
        for (relation in listOf("copy_inside_reference", "reference_inside_copy")) {
            val fixture = fixture(block(relation))
            withFixture(fixture) { scenario ->
                val visual = expand(scenario)
                moveTo(scenario, visual, 0.0, 0.0)
                rotateTo(scenario, visual, 0.0)
                assertEquals(RigidAngleCondition.SATISFIED, visual.spec.evaluate(visual.placement))
                val answer = visual.placement
                val geometry = visual.drawingSnapshot()
                mode(scenario, visual, MathRigidAngleCopyView.Mode.VIEW)
                val panel = visual.parent as View
                val viewport = panel.parent as MathZoomViewport
                instrumentation.runOnMainSync { pinch(viewport) }
                instrumentation.waitForIdleSync()
                assertTrue("Existing viewport enlarged the same panel", viewport.zoomScale > 1f)
                assertEquals(answer, visual.placement)
                assertEquals(geometry, visual.drawingSnapshot())
                instrumentation.runOnMainSync {
                    val before = panel.translationX to panel.translationY
                    localDrag(viewport, viewport.width*.5f, viewport.height*.5f, viewport.width*.1f, viewport.height*.1f)
                    assertTrue(before != (panel.translationX to panel.translationY))
                    assertEquals(answer, visual.placement)
                    viewport.resetZoom()
                    assertEquals(1f, viewport.zoomScale, 0f)
                }
                capture("rigid-angle-$relation-viewport-reset", { visual }, metadata(visual))
                assertGeometryAndNativeInk(visual)
                closeExpanded(visual)
                scenario.onActivity { activity -> check(activity).performClick(); assertState(activity, fixture, true, MathAnswer.RigidAngle(answer)) }
            }
        }
    }

    private fun visual(activity: LessonActivity) = descendants(content(activity)).filterIsInstance<MathRigidAngleCopyView>().single()
    private fun expand(scenario: ActivityScenario<LessonActivity>): MathRigidAngleCopyView {
        lateinit var result: MathRigidAngleCopyView
        scenario.onActivity { activity ->
            result = visual(activity)
            val wrapper = result.parent.parent.parent as ViewGroup
            scrollVisible(activity, wrapper)
            descendants(wrapper).single { it.tag == "math_expand_representation" }.performClick()
        }
        val deadline = SystemClock.uptimeMillis()+8_000
        while (SystemClock.uptimeMillis() < deadline) {
            instrumentation.waitForIdleSync()
            var ready = false
            instrumentation.runOnMainSync { ready = result.hasWindowFocus() && result.width > 0 && result.height > 0 && !result.isLayoutRequested }
            if (ready) return result
            SystemClock.sleep(50)
        }
        fail("The native expanded copy surface did not become ready")
        return result
    }
    private fun closeExpanded(visual: MathRigidAngleCopyView) {
        instrumentation.runOnMainSync { descendants(visual.rootView).single { it.tag == "math_exit_representation" }.performClick() }
        instrumentation.waitForIdleSync()
    }
    private fun mode(scenario: ActivityScenario<LessonActivity>, visual: MathRigidAngleCopyView, mode: MathRigidAngleCopyView.Mode) {
        instrumentation.runOnMainSync { descendants(visual.rootView).single { it.tag == "rigid_angle_mode_$mode" }.performClick() }
        instrumentation.waitForIdleSync()
        assertEquals(mode, visual.mode)
    }
    private fun moveTo(scenario: ActivityScenario<LessonActivity>, visual: MathRigidAngleCopyView, x: Double, y: Double) {
        mode(scenario, visual, MathRigidAngleCopyView.Mode.MOVE_COPY)
        drag(displayPoint(visual, EuclideanPosition(visual.placement.x, visual.placement.y)), displayPoint(visual, EuclideanPosition(x, y)))
    }
    private fun rotateTo(scenario: ActivityScenario<LessonActivity>, visual: MathRigidAngleCopyView, rotation: Double) {
        mode(scenario, visual, MathRigidAngleCopyView.Mode.ROTATE_COPY)
        val center = displayPoint(visual, EuclideanPosition(visual.placement.x, visual.placement.y))
        var r = 0.0
        instrumentation.runOnMainSync {
            r = min(visual.width, visual.height)*.28
        }
        val delta = rotation-visual.placement.rotation
        val points = (0..30).map { index -> center + EuclideanPosition(cos(delta*index/30), -sin(delta*index/30))*r }
        gesture(points)
    }
    private fun displayPoint(visual: MathRigidAngleCopyView, point: EuclideanPosition): EuclideanPosition {
        var result = EuclideanPosition(0.0, 0.0)
        instrumentation.runOnMainSync { val pixel = visual.project(point); result = screenPoint(visual, pixel.x, pixel.y) }
        return result
    }
    private fun screenPoint(view: View, x: Double, y: Double): EuclideanPosition {
        val origin = IntArray(2)
        view.getLocationOnScreen(origin)
        return EuclideanPosition(origin[0]+x, origin[1]+y)
    }
    private fun drag(start: EuclideanPosition, end: EuclideanPosition) = gesture((0..16).map { index -> start+(end-start)*(index/16.0) })
    private fun gesture(points: List<EuclideanPosition>) {
        val down = SystemClock.uptimeMillis()
        points.forEachIndexed { index, point ->
            val action = if (index == 0) MotionEvent.ACTION_DOWN else MotionEvent.ACTION_MOVE
            val event = MotionEvent.obtain(down, SystemClock.uptimeMillis(), action, point.x.toFloat(), point.y.toFloat(), 0)
            event.source = InputDevice.SOURCE_TOUCHSCREEN
            try { assertTrue("Android accepted the actual touch event", instrumentation.uiAutomation.injectInputEvent(event, true)) } finally { event.recycle() }
            SystemClock.sleep(16)
        }
        val last = points.last()
        val up = MotionEvent.obtain(down, SystemClock.uptimeMillis(), MotionEvent.ACTION_UP, last.x.toFloat(), last.y.toFloat(), 0).apply { source = InputDevice.SOURCE_TOUCHSCREEN }
        try { assertTrue(instrumentation.uiAutomation.injectInputEvent(up, true)) } finally { up.recycle() }
        instrumentation.waitForIdleSync()
        SystemClock.sleep(350) // Separate deliberate gestures from the existing double-tap command.
    }
    private fun localDrag(target: View, x: Float, y: Float, dx: Float, dy: Float) {
        val down = SystemClock.uptimeMillis()
        listOf(MotionEvent.ACTION_DOWN, MotionEvent.ACTION_MOVE, MotionEvent.ACTION_UP).forEachIndexed { index, action ->
            val event = MotionEvent.obtain(down, down+index*30, action, x+if(index==0) 0f else dx, y+if(index==0) 0f else dy, 0)
            try { target.dispatchTouchEvent(event) } finally { event.recycle() }
        }
    }
    private fun pinch(target: MathZoomViewport) {
        val down = SystemClock.uptimeMillis()
        val low = max(100*context.resources.displayMetrics.density, target.width*.35f)
        val high = target.width*.9f
        assertTrue(high > low)
        var step = 0
        fun event(action: Int, count: Int, span: Float) {
            val properties = Array(count) { index -> MotionEvent.PointerProperties().apply { id = index; toolType = MotionEvent.TOOL_TYPE_FINGER } }
            val coords = Array(count) { index -> MotionEvent.PointerCoords().apply { x = target.width/2f+if(index==0) -span/2 else span/2; y = target.height/2f; pressure = 1f; size = 1f } }
            val motion = MotionEvent.obtain(down, down+step++*30, action, count, properties, coords, 0, 0, 1f, 1f, 0, 0, InputDevice.SOURCE_TOUCHSCREEN, 0)
            try { target.dispatchTouchEvent(motion) } finally { motion.recycle() }
        }
        event(MotionEvent.ACTION_DOWN, 1, low)
        event(MotionEvent.ACTION_POINTER_DOWN or (1 shl MotionEvent.ACTION_POINTER_INDEX_SHIFT), 2, low)
        for (index in 0..16) event(MotionEvent.ACTION_MOVE, 2, low+(high-low)*index/16)
        event(MotionEvent.ACTION_POINTER_UP or (1 shl MotionEvent.ACTION_POINTER_INDEX_SHIFT), 2, high)
        event(MotionEvent.ACTION_UP, 1, high)
    }
    private fun assertGeometryAndNativeInk(view: MathRigidAngleCopyView) {
        instrumentation.runOnMainSync {
            val drawing = view.drawingSnapshot()
            assertEquals(4, drawing.paths.size)
            assertEquals(4, drawing.arrows.size)
            assertTrue(drawing.paths.all { !it.startArrow && it.endArrow })
            assertEquals(setOf("O", "A", "B", "V", "C", "D"), drawing.labels.map { it.text }.toSet())
            assertTrue(drawing.labels.all { it.bounds.left >= 0 && it.bounds.top >= 0 && it.bounds.right <= view.width && it.bounds.bottom <= view.height })
            val bitmap = draw(view)
            try {
                assertTrue("Native Canvas contains reference, copy and readable names", (0 until bitmap.height step 2).sumOf { y ->
                    (0 until bitmap.width step 2).count { x -> val c = bitmap.getPixel(x,y); Color.red(c)>180 || Color.green(c)>180 || Color.blue(c)>180 }
                } > 100)
                val accent = AppearanceSettingsRepository.getRouteAccentColor(context, ROUTE)
                val copy = Color.rgb(255,190,95)
                fun sameColor(value: Int, expected: Int) = abs(Color.red(value)-Color.red(expected)) < 14 &&
                    abs(Color.green(value)-Color.green(expected)) < 14 && abs(Color.blue(value)-Color.blue(expected)) < 14
                val unit = view.resources.displayMetrics.density
                for (label in drawing.labels) {
                    for (point in drawing.points) {
                        val dx = max(0.0,max(label.bounds.left-point.center.x,point.center.x-label.bounds.right))
                        val dy = max(0.0,max(label.bounds.top-point.center.y,point.center.y-label.bounds.bottom))
                        assertTrue("Name ${label.text} cannot cover marker ${point.id}",hypot(dx,dy) >= 6.6*unit-.01)
                    }
                    val expected = if(label.ownerId.startsWith("copy:")) copy else Color.WHITE
                    var ink = 0
                    for (y in label.bounds.top.toInt().coerceAtLeast(0)..label.bounds.bottom.toInt().coerceAtMost(bitmap.height-1))
                        for (x in label.bounds.left.toInt().coerceAtLeast(0)..label.bounds.right.toInt().coerceAtMost(bitmap.width-1))
                            if (sameColor(bitmap.getPixel(x,y),expected)) ink++
                    assertTrue("The complete original name still has native ink: ${label.text}",ink > 8)
                }
                for (point in drawing.points) {
                    // Two geometric points may intentionally be superposed within the
                    // placement tolerance. Check full marker disks whenever they are distinct.
                    if (drawing.points.any { it.id != point.id && hypot(it.center.x-point.center.x,it.center.y-point.center.y) < 9*unit }) continue
                    val expected = if(point.id.startsWith("copy:")) copy else Color.WHITE
                    val radius = (3*unit).toDouble()
                    for (y in floor(point.center.y-radius).toInt()..ceil(point.center.y+radius).toInt())
                        for (x in floor(point.center.x-radius).toInt()..ceil(point.center.x+radius).toInt())
                            if (hypot(x-point.center.x,y-point.center.y) <= radius)
                                assertTrue("The whole inner marker disk remains visible: ${point.id} at $x,$y",sameColor(bitmap.getPixel(x,y),expected))
                }
                fun nearStroke(point: EuclideanPosition): Boolean {
                    val radius = (3*view.resources.displayMetrics.density).roundToInt().coerceAtLeast(2)
                    for (y in (point.y.roundToInt()-radius).coerceAtLeast(0)..(point.y.roundToInt()+radius).coerceAtMost(bitmap.height-1))
                        for (x in (point.x.roundToInt()-radius).coerceAtLeast(0)..(point.x.roundToInt()+radius).coerceAtMost(bitmap.width-1)) {
                            val value = bitmap.getPixel(x,y)
                            // Superposed rays can share the same pixels; either genuine stroke
                            // colour proves that the complete geometric arrow remains visible.
                            if (sameColor(value,accent) || sameColor(value,copy)) return true
                        }
                    return false
                }
                for (arrow in drawing.arrows) {
                    assertTrue("Native ray tip remains visible: ${arrow.objectId}", nearStroke(arrow.tip))
                    for (wing in listOf(arrow.wingA,arrow.wingB))
                        assertTrue("Both native arrow wings remain visible: ${arrow.objectId}", nearStroke(arrow.tip+(wing-arrow.tip)*.65))
                }
            } finally { bitmap.recycle() }
        }
    }
    private fun metadata(view: MathRigidAngleCopyView) = JSONObject().put("fixtureOnly", true)
        .put("placement", MathAnswerJson.encode(MathAnswer.RigidAngle(view.placement)))
        .put("condition", view.spec.evaluate(view.placement).name).put("relation", view.spec.relation)
        .put("nativeFreeGesture", true).put("operationalPositionTolerance", view.spec.positionTolerance)
        .put("operationalDirectionTolerance", view.spec.directionTolerance)
    private fun block(relation: String = "congruent"): JSONObject {
        fun figure(vertex: String, first: String, second: String, d1: Double, d2: Double, sweep: Double): JSONObject {
            fun point(id: String, x: Double, y: Double) = JSONObject().put("id", id).put("x", x).put("y", y).put("labels", JSONArray(listOf(id)))
            fun ray(id: String, endpoint: String) = JSONObject().put("id", id).put("kind", "ray").put("through", JSONArray(listOf(vertex, endpoint))).put("contains", JSONArray(listOf(vertex, endpoint)))
            return JSONObject().put("vertex", vertex).put("sides", JSONArray(listOf("first", "second"))).put("region", "counterclockwise")
                .put("diagram", JSONObject().put("points", JSONArray().put(point(vertex,0.0,0.0)).put(point(first,d1,0.0)).put(point(second,cos(sweep)*d2,sin(sweep)*d2)))
                    .put("objects", JSONArray().put(ray("first",first)).put(ray("second",second))))
        }
        val sweep = when (relation) { "copy_inside_reference" -> .7; "reference_inside_copy" -> 2.0; else -> Math.PI/2 }
        val validator = JSONObject().put("type", "rigid_angle_copy").put("reference", figure("O","A","B",4.0,5.0,Math.PI/2))
            .put("copy", figure("V","C","D",6.0,3.0,sweep)).put("initial", JSONObject().put("x",5.0).put("y",4.0).put("rotation",.6))
            .put("relation",relation).put("positionTolerance",.25).put("directionTolerance",.03)
        return JSONObject().put("type","math_puzzle").put("schemaVersion",1).put("id","free_copy")
            .put("prompt","Traslada y gira libremente la copia. Conserva la correspondencia OA con VC y OB con VD y las regiones elegidas.")
            .put("responses",JSONArray().put(JSONObject().put("id","copy").put("type","math_rigid_angle_copy").put("validator",validator)))
            .put("feedback",JSONObject().put("correct","La colocación satisface las condiciones con la tolerancia operativa declarada.").put("incorrect","Revisa la condición indicada y vuelve a mover o girar la copia."))
    }
}
