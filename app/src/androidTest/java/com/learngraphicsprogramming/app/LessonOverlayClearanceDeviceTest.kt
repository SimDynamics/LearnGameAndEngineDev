package com.learngraphicsprogramming.app

import android.graphics.Color
import android.graphics.Rect
import android.os.SystemClock
import android.view.Choreographer
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.view.ViewTreeObserver
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.learngraphicsprogramming.app.math.MathGeometryQaSupport
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import kotlin.math.roundToInt

/** Native layout lifecycle coverage, independent of narration playback and curricular data. */
@RunWith(AndroidJUnit4::class)
class LessonOverlayClearanceDeviceTest : MathGeometryQaSupport() {
    private class Scene(activity: LessonActivity) : AutoCloseable {
        val host = FrameLayout(activity).apply { setBackgroundColor(Color.DKGRAY) }
        val scroll = ScrollView(activity).apply {
            isFillViewport = true
            setPadding(9, 11, 13, 0)
        }
        val wrapper = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(3, 5, 7, BASELINE_BOTTOM)
        }
        val finalLine = TextView(activity).apply { text = "Última línea completa de la guía técnica QA." }
        val controls = TextView(activity).apply {
            text = "Controles QA"
            setTextColor(Color.WHITE)
            setBackgroundColor(Color.rgb(40, 45, 50))
            gravity = Gravity.CENTER
            visibility = View.GONE
        }
        val gap = (8f * activity.resources.displayMetrics.density).roundToInt()
        val clearance: LessonOverlayClearance
        private val root = activity.findViewById<FrameLayout>(android.R.id.content)

        init {
            wrapper.addView(View(activity), LinearLayout.LayoutParams(1, 6_000))
            wrapper.addView(finalLine, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT))
            scroll.addView(wrapper, FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT))
            host.addView(scroll, FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT))
            host.addView(controls, FrameLayout.LayoutParams(300, 100))
            clearance = LessonOverlayClearance(scroll) { controls }
            root.addView(host, FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT))
        }

        fun positionControls(top: Int, height: Int = 100) {
            controls.layoutParams = (controls.layoutParams as FrameLayout.LayoutParams).apply {
                this.height = height
                leftMargin = 20
                topMargin = top
            }
            controls.visibility = View.VISIBLE
        }

        override fun close() {
            clearance.close()
            root.removeView(host)
        }
    }

    @Test fun measuredReservationTracksControlsWithoutAccumulationOrAutomaticScrolling() {
        val fixture = fixture(qaBlock())
        withFixture(fixture) { scenario ->
            lateinit var scene: Scene
            scenario.onActivity { scene = Scene(it) }
            try {
                awaitScene(scenario, scene) { it.wrapper.paddingBottom == BASELINE_BOTTOM }
                scenario.onActivity {
                    scene.positionControls(scene.scroll.height - 180)
                    scene.scroll.scrollTo(0, 100)
                }
                awaitScene(scenario, scene) { it.wrapper.paddingBottom == BASELINE_BOTTOM + 180 + it.gap }
                repeat(4) {
                    scenario.onActivity { scene.host.requestLayout(); scene.controls.invalidate() }
                    awaitScene(scenario, scene) { it.wrapper.paddingBottom == BASELINE_BOTTOM + 180 + it.gap }
                    scenario.onActivity { assertEquals("Padding changes never auto-scroll the lesson", 100, scene.scroll.scrollY) }
                }
                scenario.onActivity { scene.positionControls(scene.scroll.height - 280, 150) }
                awaitScene(scenario, scene) { it.wrapper.paddingBottom == BASELINE_BOTTOM + 280 + it.gap }

                // Resize the real native viewport; the control strip stays in place.
                scenario.onActivity {
                    scene.scroll.layoutParams = (scene.scroll.layoutParams as FrameLayout.LayoutParams).apply {
                        height = scene.host.height - 40
                    }
                }
                awaitScene(scenario, scene) { it.wrapper.paddingBottom == BASELINE_BOTTOM + 240 + it.gap }
                scenario.onActivity {
                    val viewport = Rect()
                    val overlay = Rect()
                    assertTrue(scene.scroll.getGlobalVisibleRect(viewport))
                    assertTrue(scene.controls.getGlobalVisibleRect(overlay))
                    assertEquals(240, viewport.bottom - overlay.top)
                    // Use the measured finite native range, never Int.MAX_VALUE.
                    val maxScroll = (scene.wrapper.height -
                        (scene.scroll.height - scene.scroll.paddingTop - scene.scroll.paddingBottom)).coerceAtLeast(0)
                    scene.scroll.scrollTo(0, maxScroll)
                }
                instrumentation.waitForIdleSync()
                scenario.onActivity {
                    val last = Rect()
                    val overlay = Rect()
                    assertFalse(scene.scroll.canScrollVertically(1))
                    assertTrue(scene.finalLine.getGlobalVisibleRect(last))
                    assertTrue(scene.controls.getGlobalVisibleRect(overlay))
                    assertEquals("The final line is entirely visible", scene.finalLine.height, last.height())
                    assertTrue("The complete final line can reach above the unchanged strip", last.bottom + scene.gap <= overlay.top)
                    scene.controls.visibility = View.GONE
                }
                awaitScene(scenario, scene) { it.wrapper.paddingBottom == BASELINE_BOTTOM }
                scenario.onActivity { scene.positionControls(scene.host.height + 20) }
                awaitScene(scenario, scene) { it.wrapper.paddingBottom == BASELINE_BOTTOM }
            } finally {
                scenario.onActivity { scene.close() }
            }
        }
    }

    @Test fun keyboardPaddingRemainsOwnedByTheKeyboardAndDisposeRestoresOnlyTheContentTail() {
        val fixture = fixture(qaBlock())
        withFixture(fixture) { scenario ->
            lateinit var scene: Scene
            scenario.onActivity { scene = Scene(it) }
            try {
                awaitScene(scenario, scene) { it.wrapper.paddingBottom == BASELINE_BOTTOM }
                scenario.onActivity { scene.positionControls(scene.scroll.height - 180) }
                awaitScene(scenario, scene) { it.wrapper.paddingBottom == BASELINE_BOTTOM + 180 + it.gap }
                scenario.onActivity { scene.scroll.setPadding(9, 11, 13, 60) }
                awaitScene(scenario, scene) { it.wrapper.paddingBottom == BASELINE_BOTTOM + 120 + it.gap }
                scenario.onActivity {
                    assertEquals(listOf(9, 11, 13, 60), listOf(scene.scroll.paddingLeft,
                        scene.scroll.paddingTop, scene.scroll.paddingRight, scene.scroll.paddingBottom))
                    assertEquals(listOf(3, 5, 7), listOf(scene.wrapper.paddingLeft,
                        scene.wrapper.paddingTop, scene.wrapper.paddingRight))
                    scene.clearance.close()
                    assertEquals(BASELINE_BOTTOM, scene.wrapper.paddingBottom)
                    assertEquals(60, scene.scroll.paddingBottom)
                    scene.wrapper.setPadding(3, 5, 7, 29)
                    scene.positionControls(scene.scroll.height - 280)
                }
                awaitScene(scenario, scene) { it.wrapper.paddingBottom == 29 }
                scenario.onActivity {
                    assertEquals("Disposed listeners never rewrite the host's later padding", 29, scene.wrapper.paddingBottom)
                    assertEquals(60, scene.scroll.paddingBottom)
                }
            } finally {
                scenario.onActivity { scene.close() }
            }
        }
    }

    @Test fun translatingControlsAcrossFramesKeepsDrawingIntermediateWindowStates() {
        val fixture = fixture(qaBlock())
        withFixture(fixture) { scenario ->
            lateinit var scene: Scene
            scenario.onActivity { scene = Scene(it) }
            val finished = CountDownLatch(1)
            val drawnTicks = linkedSetOf<Int>()
            val steps = 16
            var tick = 0
            var watching = false
            val drawListener = ViewTreeObserver.OnDrawListener {
                if (tick in 1..steps) drawnTicks += tick
            }
            val translate = object : Choreographer.FrameCallback {
                override fun doFrame(frameTimeNanos: Long) {
                    tick++
                    // Dragging changes translation each scheduled frame, not margins
                    // in separate settled layouts. No physical FPS claim is made here.
                    scene.controls.translationY = -4f * tick
                    scene.host.invalidate()
                    if (tick < steps) Choreographer.getInstance().postFrameCallback(this)
                    else finished.countDown()
                }
            }
            try {
                awaitScene(scenario, scene) { it.wrapper.paddingBottom == BASELINE_BOTTOM }
                scenario.onActivity {
                    scene.positionControls(scene.scroll.height - 180)
                    scene.scroll.scrollTo(0, 100)
                }
                awaitScene(scenario, scene) { it.wrapper.paddingBottom == BASELINE_BOTTOM + 180 + it.gap }
                scenario.onActivity {
                    scene.host.viewTreeObserver.addOnDrawListener(drawListener)
                    watching = true
                    Choreographer.getInstance().postFrameCallback(translate)
                }
                assertTrue("The finite native translation sequence completed", finished.await(6, TimeUnit.SECONDS))
                instrumentation.waitForIdleSync()
                scenario.onActivity {
                    val intermediate = drawnTicks.filter { it in 1 until steps }
                    saveMetadata("overlay-continuous-translation-window-draws", JSONObject()
                        .put("scheduledTranslationTicks", steps).put("drawnTicks", JSONArray(drawnTicks.toList()))
                        .put("intermediateDrawnTicks", JSONArray(intermediate))
                        .put("nativeWindowDrawCallbacks", true).put("physicalFrameRateMeasurement", false))
                    assertTrue("Window must draw multiple changing states while controls move; drawn ticks=$drawnTicks",
                        intermediate.size >= 4)
                    assertEquals("Translation never auto-scrolls the lesson", 100, scene.scroll.scrollY)
                }
                awaitScene(scenario, scene) {
                    it.wrapper.paddingBottom == BASELINE_BOTTOM + 180 + steps * 4 + it.gap
                }
            } finally {
                scenario.onActivity {
                    Choreographer.getInstance().removeFrameCallback(translate)
                    if (watching && scene.host.viewTreeObserver.isAlive)
                        scene.host.viewTreeObserver.removeOnDrawListener(drawListener)
                    scene.close()
                }
            }
        }
    }

    private fun awaitScene(scenario: ActivityScenario<LessonActivity>, scene: Scene, ready: (Scene) -> Boolean) {
        val deadline = SystemClock.uptimeMillis() + 6_000
        var diagnostic = "not measured"
        var stable = 0
        while (SystemClock.uptimeMillis() < deadline) {
            instrumentation.waitForIdleSync()
            scenario.onActivity {
                val measured = scene.host.isAttachedToWindow && scene.host.width > 0 && scene.scroll.height > 0 &&
                    !scene.host.isLayoutRequested && !scene.scroll.isLayoutRequested && !scene.wrapper.isLayoutRequested
                stable = if (measured && ready(scene)) stable + 1 else 0
                diagnostic = "host=${scene.host.width}x${scene.host.height}, viewport=${scene.scroll.width}x${scene.scroll.height}, " +
                    "tail=${scene.wrapper.paddingBottom}, scrollPadding=${scene.scroll.paddingBottom}, " +
                    "controls=${scene.controls.visibility}/${scene.controls.top}..${scene.controls.bottom}"
            }
            if (stable >= 3) return
            SystemClock.sleep(35)
        }
        fail("Native overlay clearance did not settle: $diagnostic")
    }

    private fun qaBlock() = JSONObject().put("type", "math_puzzle").put("schemaVersion", 1)
        .put("id", "qa_overlay_clearance").put("prompt", "Verificación aislada de presentación.")
        .put("representations", JSONArray()).put("tools", JSONArray())
        .put("responses", JSONArray().put(JSONObject().put("id", "qa_overlay_clearance__choice")
            .put("type", "math_multiple_choice_single").put("options", JSONArray()
                .put(JSONObject().put("id", "correct").put("label", "A"))
                .put(JSONObject().put("id", "wrong").put("label", "B")))
            .put("validator", JSONObject().put("type", "choice").put("expected", JSONArray(listOf("correct"))))))
        .put("feedback", JSONObject().put("correct", "Verificación correcta.").put("incorrect", "Revisa la selección."))

    companion object {
        private const val BASELINE_BOTTOM = 17
    }
}
