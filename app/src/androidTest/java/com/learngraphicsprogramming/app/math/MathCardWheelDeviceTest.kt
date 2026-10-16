package com.learngraphicsprogramming.app.math

import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.Rect
import android.os.SystemClock
import android.view.DragEvent
import android.view.InputDevice
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.LessonActivity
import com.learngraphicsprogramming.app.ProgressRepository
import com.learngraphicsprogramming.app.R
import com.learngraphicsprogramming.app.UiLanguageSettings
import com.learngraphicsprogramming.app.narration.NarrationSettingsRepository
import java.io.File
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/** The actual Mathematics 1 / lesson 1 / page 3 ordering exercise, unchanged. */
@RunWith(AndroidJUnit4::class)
class MathCardWheelDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val lesson = "math_course_01__numeros_y_operaciones__magnitudes_y_numeracion"
    private val original = linkedMapOf<String, Map<String, *>>()
    private var locale = ""

    @Before fun preserveQaPreferences() {
        assertTrue("Use only the isolated QA application", context.packageName in setOf(
            "com.learngraphicsprogramming.app.realdownloadsqa", "com.learngraphicsprogramming.app.curricularqa"))
        assertTrue("Use LDPlayer, never Work's phone", android.os.Build.MODEL.contains("V2307A", true) ||
            android.os.Build.FINGERPRINT.contains("generic", true) || android.os.Build.HARDWARE.contains("ranchu", true))
        listOf("learning_progress", "navigation_state", "lesson_narration_settings", "ui_language_settings").forEach { name ->
            original[name] = context.getSharedPreferences(name, Context.MODE_PRIVATE).all.mapValues { (_, value) ->
                if (value is Set<*>) value.toSet() else value
            }
        }
        locale = UiLanguageSettings.selectedTag(context)
        instrumentation.runOnMainSync { UiLanguageSettings.select(context, "es") }
        NarrationSettingsRepository.saveEnabled(context, false)
        ProgressRepository.resetLesson(context, "math_physics", lesson)
        ProgressRepository.saveCurrentPage(context, "math_physics", lesson, 2)
    }

    @After fun restoreQaPreferences() {
        instrumentation.runOnMainSync { UiLanguageSettings.select(context, locale) }
        original.forEach { (name, values) ->
            val editor = context.getSharedPreferences(name, Context.MODE_PRIVATE).edit().clear()
            values.forEach { (key, value) -> when (value) {
                is String -> editor.putString(key, value)
                is Int -> editor.putInt(key, value)
                is Long -> editor.putLong(key, value)
                is Float -> editor.putFloat(key, value)
                is Boolean -> editor.putBoolean(key, value)
                is Set<*> -> editor.putStringSet(key, value.filterIsInstance<String>().toSet())
                else -> error("Unsupported preference")
            } }
            assertTrue(editor.commit())
            assertEquals(values, context.getSharedPreferences(name, Context.MODE_PRIVATE).all)
        }
    }

    @Test fun nativeWheelAndSubTwentyMillisecondSwipesCannotDragOrSelectPublishedCards() = withPublishedOrdering { scenario ->
        lateinit var card: View
        var started = 0
        val location = FloatArray(2)
        scenario.onActivity { activity ->
            val cards = cards(activity)
            assertEquals(listOf("d", "c", "b", "a"), order(cards.first()))
            card = cards.last()
            val root = activity.findViewById<ViewGroup>(android.R.id.content)
            root.setOnDragListener { _, event ->
                if (event.action == DragEvent.ACTION_DRAG_STARTED) started++
                true
            }
            for (amount in listOf(1f, -1f, 2f, -2f)) {
                send(card, MotionEvent.ACTION_SCROLL, 1_000, 1_010, 2f, 2f, mouse = true, wheel = amount)
                assertEquals(listOf("d", "c", "b", "a"), order(card))
                assertEquals(1f, card.alpha, .001f)
            }
            reveal(card)
        }
        instrumentation.waitForIdleSync()
        scenario.onActivity { screenPoint(card, location) }
        // InputDispatcher owns an actual down pointer before startDragAndDrop is evaluated.
        // LDPlayer's wheel translation is a rapid touchscreen gesture with no mouse marker.
        val down = SystemClock.uptimeMillis()
        var pointerOpen = false
        try {
            pointer(down, down, MotionEvent.ACTION_DOWN, location[0], location[1])
            pointerOpen = true
            pointer(down, down + 10, MotionEvent.ACTION_MOVE, location[0], location[1] - 70f)
            instrumentation.waitForIdleSync()
            scenario.onActivity {
                assertEquals("A wheel-like swipe must never arm startDragAndDrop", 1f, card.alpha, .001f)
            }
            pointer(down, down + 300, MotionEvent.ACTION_MOVE, location[0], location[1] - 100f)
            pointer(down, down + 310, MotionEvent.ACTION_UP, location[0], location[1] - 100f)
            pointerOpen = false
            instrumentation.waitForIdleSync()
            scenario.onActivity {
                assertEquals(1f, card.alpha, .001f)
                assertEquals("A wheel must not perform the tap-to-reorder action either",
                    listOf("d", "c", "b", "a"), order(card))
                // A native mouse MOVE without a pressed primary button is not a drag.
                send(card, MotionEvent.ACTION_MOVE, 3_000, 3_020, 25f, 50f, mouse = true)
                assertEquals("An orphan mouse MOVE must not start a native drag", 1f, card.alpha, .001f)
                send(card, MotionEvent.ACTION_CANCEL, 3_000, 3_030, 25f, 50f, mouse = true)
                assertEquals(0, started)
            }
        } finally {
            try {
                if (pointerOpen) pointer(down, maxOf(down + 311, SystemClock.uptimeMillis()),
                    MotionEvent.ACTION_CANCEL, location[0], location[1])
            } finally {
                scenario.onActivity { activity ->
                    card.cancelDragAndDrop()
                    activity.findViewById<ViewGroup>(android.R.id.content).setOnDragListener(null)
                }
            }
        }
    }

    @Test fun twentyFiveMillisecondTouchStartsNativeDragAndOrdinaryCardTapsStillReorder() = withPublishedOrdering { scenario ->
        lateinit var dragged: View
        var started = 0
        val source = FloatArray(2)
        val destination = FloatArray(2)
        scenario.onActivity { activity ->
            val root = activity.findViewById<ViewGroup>(android.R.id.content)
            root.setOnDragListener { _, event ->
                if (event.action == DragEvent.ACTION_DRAG_STARTED) started++
                true
            }
            dragged = cards(activity).last()
            reveal(dragged.parent as View)
        }
        instrumentation.waitForIdleSync()
        scenario.onActivity { activity ->
            screenPoint(dragged, source)
            screenPoint(cards(activity).first(), destination)
        }
        val down = SystemClock.uptimeMillis()
        pointer(down, down, MotionEvent.ACTION_DOWN, source[0], source[1])
        SystemClock.sleep(25)
        try {
            for (step in 1..12) {
                val fraction = step / 12f
                pointer(down, SystemClock.uptimeMillis(), MotionEvent.ACTION_MOVE,
                    source[0] + (destination[0] - source[0]) * fraction,
                    source[1] + (destination[1] - source[1]) * fraction)
                SystemClock.sleep(24)
            }
            pointer(down, SystemClock.uptimeMillis(), MotionEvent.ACTION_UP, destination[0], destination[1])
            instrumentation.waitForIdleSync()
            scenario.onActivity { activity ->
                assertTrue("Android actually dispatched a native drag, not a test callback", started > 0)
                assertEquals("Held touch actually moves the original card to the destination",
                    listOf("a", "d", "c", "b"), order(dragged))
                assertEquals(4, cards(activity).size)
            }
        } finally {
            scenario.onActivity { activity ->
                dragged.cancelDragAndDrop()
                activity.findViewById<ViewGroup>(android.R.id.content).setOnDragListener(null)
            }
        }
        instrumentation.waitForIdleSync()
        scenario.onActivity { activity ->
            for (id in listOf("a", "b", "c", "d")) {
                val card = cards(activity).single { cardId(it) == id }
                val down = SystemClock.uptimeMillis()
                send(card, MotionEvent.ACTION_DOWN, down, down, 3f, 3f)
                send(card, MotionEvent.ACTION_UP, down, down + 25, 3f, 3f)
            }
            assertEquals("Normal taps preserve the existing successive-slot ordering",
                listOf("a", "b", "c", "d"), order(cards(activity).first()))
            assertTrue(cards(activity).all { it.alpha == 1f })
        }
    }

    private fun withPublishedOrdering(block: (ActivityScenario<LessonActivity>) -> Unit) {
        val intent = Intent(context, LessonActivity::class.java)
            .putExtra("routeId", "math_physics").putExtra("courseId", "math_course_01")
            .putExtra("unitId", "numeros_y_operaciones").putExtra("lessonId", lesson)
            .putExtra("lessonNumber", 1)
        ActivityScenario.launch<LessonActivity>(intent).use { scenario ->
            scenario.onActivity { activity ->
                activity.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                assertEquals("3 de 5", activity.findViewById<android.widget.TextView>(R.id.textProgress).text.toString())
                assertEquals(4, cards(activity).size)
            }
            instrumentation.waitForIdleSync()
            block(scenario)
            val screenshot = instrumentation.uiAutomation.takeScreenshot()
            if (screenshot != null) {
                val destination = File(context.getExternalFilesDir(null), "math-card-wheel-qa").apply { mkdirs() }
                File(destination, "published-page-three.png").outputStream().use { screenshot.compress(Bitmap.CompressFormat.PNG, 100, it) }
                screenshot.recycle()
            }
        }
    }

    private fun cards(activity: LessonActivity): List<View> = tree(activity.findViewById(R.id.lessonContent))
        .filter { it is MathTouchCard && it.contentDescription?.startsWith("Ordenar tarjeta ") == true }
    private fun cardId(card: View) = card.contentDescription.toString().substringAfter("Ordenar tarjeta ").substringBefore(':')
    private fun order(card: View): List<String> = (card.parent as ViewGroup).let { parent ->
        (0 until parent.childCount).map { cardId(parent.getChildAt(it)) }
    }
    private fun tree(view: View): List<View> = listOf(view) + if (view is ViewGroup)
        (0 until view.childCount).flatMap { tree(view.getChildAt(it)) } else emptyList()

    private fun reveal(view: View) {
        assertTrue(view.width > 0 && view.height > 0)
        view.requestRectangleOnScreen(Rect(0, 0, view.width, view.height), true)
    }
    private fun screenPoint(view: View, destination: FloatArray) {
        val bounds = Rect()
        assertTrue("The actual card must be visible before injection", view.getGlobalVisibleRect(bounds))
        assertTrue(bounds.width() > 0 && bounds.height() > 0)
        destination[0] = bounds.left + bounds.width() * .8f
        destination[1] = bounds.exactCenterY()
    }
    private fun pointer(down: Long, time: Long, action: Int, x: Float, y: Float) {
        val event = MotionEvent.obtain(down, time, action, x, y, 0).apply { source = InputDevice.SOURCE_TOUCHSCREEN }
        try { assertTrue("The actual Android pointer is injected", instrumentation.uiAutomation.injectInputEvent(event, true)) }
        finally { event.recycle() }
    }

    private fun send(target: View, action: Int, down: Long, time: Long, x: Float, y: Float,
                     mouse: Boolean = false, primary: Boolean = false, wheel: Float = 0f) {
        val properties = MotionEvent.PointerProperties().apply {
            id = 0
            toolType = if (mouse) MotionEvent.TOOL_TYPE_MOUSE else MotionEvent.TOOL_TYPE_FINGER
        }
        val coordinates = MotionEvent.PointerCoords().apply {
            this.x = x; this.y = y; pressure = 1f; size = 1f
            setAxisValue(MotionEvent.AXIS_VSCROLL, wheel)
        }
        val event = MotionEvent.obtain(down, time, action, 1, arrayOf(properties), arrayOf(coordinates),
            0, if (primary) MotionEvent.BUTTON_PRIMARY else 0, 1f, 1f, 0, 0,
            if (mouse) InputDevice.SOURCE_MOUSE else InputDevice.SOURCE_TOUCHSCREEN, 0)
        try {
            if (action == MotionEvent.ACTION_SCROLL) target.dispatchGenericMotionEvent(event)
            else target.dispatchTouchEvent(event)
        } finally { event.recycle() }
    }
}
