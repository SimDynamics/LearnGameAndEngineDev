package com.learngraphicsprogramming.app.math

import android.content.Context
import android.content.Intent
import android.graphics.Color
import android.os.ParcelFileDescriptor
import android.os.SystemClock
import android.view.InputDevice
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.view.ViewConfiguration
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.LessonActivity
import com.learngraphicsprogramming.app.R
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class MathFullscreenGesturesDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val snapshots = linkedMapOf<String, Map<String, *>>()

    @Before fun preservePreferencesAndWakeDevice() {
        listOf("navigation_state", "learning_progress").forEach {
            snapshots[it] = context.getSharedPreferences(it, Context.MODE_PRIVATE).all.toMap()
        }
        listOf("input keyevent KEYCODE_WAKEUP", "wm dismiss-keyguard").forEach { command ->
            ParcelFileDescriptor.AutoCloseInputStream(instrumentation.uiAutomation.executeShellCommand(command)).use { it.readBytes() }
        }
    }

    @After fun restorePreferences() {
        snapshots.forEach { (name, original) ->
            val preferences = context.getSharedPreferences(name, Context.MODE_PRIVATE)
            val editor = preferences.edit()
            (preferences.all.keys - original.keys).forEach(editor::remove)
            original.forEach { (key, value) ->
                when (value) {
                    is String -> editor.putString(key, value)
                    is Int -> editor.putInt(key, value)
                    is Long -> editor.putLong(key, value)
                    is Float -> editor.putFloat(key, value)
                    is Boolean -> editor.putBoolean(key, value)
                    is Set<*> -> editor.putStringSet(key, value.filterIsInstance<String>().toSet())
                }
            }
            assertTrue(editor.commit())
        }
    }

    @Test fun staticRepresentationDoubleTapEntersAndExitsFullscreen() {
        lateinit var visual: MathCartesianView
        lateinit var holder: ViewGroup
        ActivityScenario.launch<LessonActivity>(intent()).use { scenario ->
            scenario.onActivity { activity ->
                visual = MathCartesianView(activity, JSONObject(), Color.GREEN)
                show(activity, visual)
                holder = visual.parent as ViewGroup
            }
            instrumentation.waitForIdleSync()
            scenario.onActivity { doubleTap(holder) }
            instrumentation.waitForIdleSync()
            scenario.onActivity {
                assertNotSame(holder, visual.parent)
                assertTrue(descendants(visual.rootView).any { it.tag == "math_exit_representation" && it.isShown })
                val viewport = visual.parent as MathZoomViewport
                assertEquals(1f, viewport.zoomScale, 0f)
                assertFalse(descendants(visual.rootView).single { it.tag == "math_reset_representation_zoom" }.isShown)
                doubleTap(viewport)
            }
            instrumentation.waitForIdleSync()
            scenario.onActivity { assertSame("Doble toque devuelve la misma View", holder, visual.parent) }
        }
    }

    @Test fun pinchRetainsZoomAfterReleasePanAndResetAppearsOnlyWhenEnlarged() {
        lateinit var visual: MathCartesianView
        lateinit var holder: ViewGroup
        ActivityScenario.launch<LessonActivity>(intent()).use { scenario ->
            scenario.onActivity { activity ->
                visual = MathCartesianView(activity, JSONObject(), Color.GREEN)
                val wrapped = show(activity, visual)
                holder = visual.parent as ViewGroup
                descendants(wrapped).single { it.tag == "math_expand_representation" }.performClick()
            }
            instrumentation.waitForIdleSync()
            scenario.onActivity {
                val viewport = visual.parent as MathZoomViewport
                val reset = descendants(visual.rootView).single { it.tag == "math_reset_representation_zoom" }
                assertEquals(View.GONE, reset.visibility)
                val outward = pinch(viewport)
                assertTrue("El nivel permanece al soltar ambos dedos: scale=${viewport.zoomScale}, viewport=${viewport.width}x${viewport.height}, $outward",
                    viewport.zoomScale > 1.25f)
                assertEquals(viewport.zoomScale, visual.scaleX, 0f)
                assertTrue(reset.isShown)
                assertEquals("Control abajo a la izquierda", 0, reset.left)
                val retainedScale = viewport.zoomScale
                drag(viewport, viewport.width / 2f, viewport.height / 2f, 40f, 30f)
                assertEquals(retainedScale, viewport.zoomScale, 0f)
                assertTrue("La representación ampliada permite desplazar el viewport", visual.translationX != 0f || visual.translationY != 0f)
                val inward = pinch(viewport, inward = true)
                assertTrue("Pinch-in conserva una ampliación reducida al soltar: before=$retainedScale, after=${viewport.zoomScale}, $inward",
                    viewport.zoomScale > 1f && viewport.zoomScale < retainedScale)
                assertEquals(viewport.zoomScale, visual.scaleX, 0f)
                reset.performClick()
                assertEquals(1f, viewport.zoomScale, 0f)
                assertEquals(0f, visual.translationX, 0f)
                assertEquals(0f, visual.translationY, 0f)
                assertEquals(View.GONE, reset.visibility)
                descendants(visual.rootView).single { it.tag == "math_exit_representation" }.performClick()
                assertSame(holder, visual.parent)
                assertEquals(1f, visual.scaleX, 0f)
            }
        }
    }

    @Test fun interactiveDiagramKeepsTapOwnershipWhilePinchStillZooms() {
        lateinit var graph: MathCartesianView
        lateinit var holder: ViewGroup
        lateinit var wrapped: View
        ActivityScenario.launch<LessonActivity>(intent()).use { scenario ->
            scenario.onActivity { activity ->
                graph = MathCartesianView(activity, JSONObject(), Color.GREEN, true)
                wrapped = show(activity, graph)
                holder = graph.parent as ViewGroup
            }
            instrumentation.waitForIdleSync()
            scenario.onActivity {
                doubleTap(holder)
                assertSame("Los taps de un diagrama editable no abren fullscreen", holder, graph.parent)
                descendants(wrapped).single { it.tag == "math_expand_representation" }.performClick()
            }
            instrumentation.waitForIdleSync()
            scenario.onActivity {
                val points = graph.points()
                val viewport = graph.parent as MathZoomViewport
                pinch(viewport)
                assertTrue(viewport.zoomScale > 1f)
                assertEquals("El gesto con dos dedos no arrastra puntos", points, graph.points())
                descendants(graph.rootView).single { it.tag == "math_exit_representation" }.performClick()
            }
        }
    }

    private fun show(activity: LessonActivity, visual: View): View {
        val wrapped = MathExpandableRepresentation.wrap(activity, visual, Color.GREEN)
        activity.findViewById<ViewGroup>(R.id.lessonContent).apply { removeAllViews(); addView(wrapped) }
        return wrapped
    }

    private fun intent() = Intent(context, LessonActivity::class.java)
        .putExtra("routeId", "math_physics").putExtra("courseId", "math_course_01")
        .putExtra("unitId", "math_course_01__arithmetic_fundamentals")
        .putExtra("lessonId", "math_course_01__arithmetic_fundamentals__numbers_operations").putExtra("lessonNumber", 1)

    private fun doubleTap(target: View) {
        val start = SystemClock.uptimeMillis()
        for (tap in 0..1) {
            val down = start + tap * 100
            listOf(MotionEvent.ACTION_DOWN, MotionEvent.ACTION_UP).forEachIndexed { index, action ->
                val event = MotionEvent.obtain(down, down + index * 30, action, target.width / 2f, target.height / 2f, 0)
                try { target.dispatchTouchEvent(event) } finally { event.recycle() }
            }
        }
    }

    private fun drag(target: View, x: Float, y: Float, dx: Float, dy: Float) {
        val down = SystemClock.uptimeMillis()
        listOf(MotionEvent.ACTION_DOWN, MotionEvent.ACTION_MOVE, MotionEvent.ACTION_UP).forEachIndexed { index, action ->
            val event = MotionEvent.obtain(down, down + index * 30, action,
                x + if (index == 0) 0f else dx, y + if (index == 0) 0f else dy, 0)
            try { target.dispatchTouchEvent(event) } finally { event.recycle() }
        }
    }

    private fun pinch(target: View, inward: Boolean = false): String {
        val down = SystemClock.uptimeMillis()
        val centerX = target.width / 2f
        val centerY = target.height / 2f
        // These pointers travel horizontally, so their usable span is the width even in
        // landscape. Begin above Android's minimum scaling span, then cross its touch slop
        // with enough movement remaining to exercise scaling after onScaleBegin.
        val configuration = ViewConfiguration.get(target.context)
        val minimumSpan = if (android.os.Build.VERSION.SDK_INT >= 29) configuration.scaledMinimumScalingSpan.toFloat()
            else 48f * target.resources.displayMetrics.density
        val slop = configuration.scaledTouchSlop * 2f
        val lowSpan = maxOf(minimumSpan + slop * 2, target.width * .3f)
        val highSpan = minOf(target.width * .9f, lowSpan * 2.3f)
        assertTrue("Viewport demasiado estrecho para un pinch real: width=${target.width}, minimumSpan=$minimumSpan, slop=$slop",
            highSpan > lowSpan + slop * 2)
        val startSpan = if (inward) highSpan else lowSpan
        val endSpan = if (inward) maxOf(lowSpan, highSpan * .75f) else highSpan
        var step = 0
        fun event(action: Int, count: Int, span: Float) {
            val properties = Array(count) { index -> MotionEvent.PointerProperties().apply { id = index; toolType = MotionEvent.TOOL_TYPE_FINGER } }
            val coordinates = Array(count) { index -> MotionEvent.PointerCoords().apply {
                x = centerX + if (index == 0) -span / 2 else span / 2
                y = centerY
                pressure = 1f
                size = 1f
            } }
            val motion = MotionEvent.obtain(down, down + step++ * 30, action, count, properties, coordinates,
                0, 0, 1f, 1f, 0, 0, InputDevice.SOURCE_TOUCHSCREEN, 0)
            try { target.dispatchTouchEvent(motion) } finally { motion.recycle() }
        }
        event(MotionEvent.ACTION_DOWN, 1, startSpan)
        event(MotionEvent.ACTION_POINTER_DOWN or (1 shl MotionEvent.ACTION_POINTER_INDEX_SHIFT), 2, startSpan)
        for (index in 0..12) event(MotionEvent.ACTION_MOVE, 2, startSpan + (endSpan - startSpan) * index / 12)
        event(MotionEvent.ACTION_POINTER_UP or (1 shl MotionEvent.ACTION_POINTER_INDEX_SHIFT), 2, endSpan)
        event(MotionEvent.ACTION_UP, 1, endSpan)
        return "span=$startSpan->$endSpan, minimumSpan=$minimumSpan, slop=$slop"
    }

    private fun descendants(view: View): List<View> = listOf(view) +
        if (view is ViewGroup) (0 until view.childCount).flatMap { descendants(view.getChildAt(it)) } else emptyList()
}
