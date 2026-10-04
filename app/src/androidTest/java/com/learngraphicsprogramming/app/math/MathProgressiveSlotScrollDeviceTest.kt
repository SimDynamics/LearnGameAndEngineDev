package com.learngraphicsprogramming.app.math

import android.content.Context
import android.content.Intent
import android.content.pm.ActivityInfo
import android.content.res.Configuration
import android.graphics.Bitmap
import android.graphics.Rect
import android.os.ParcelFileDescriptor
import android.os.SystemClock
import android.view.InputDevice
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.view.inputmethod.InputMethodManager
import android.widget.ScrollView
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.LessonActivity
import com.learngraphicsprogramming.app.LessonTextSizeSettingsRepository
import com.learngraphicsprogramming.app.ProgressRepository
import com.learngraphicsprogramming.app.R
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/** Real lesson scroll policy remains independent of cursor/selection-only slot navigation. */
@RunWith(AndroidJUnit4::class)
class MathProgressiveSlotScrollDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val route = "math_physics"
    private val lesson = "math_course_01__arithmetic_fundamentals__math_puzzle_sandbox"
    private val snapshots = linkedMapOf<String, Map<String, *>>()

    @Before fun prepare() {
        for (command in listOf("input keyevent KEYCODE_WAKEUP", "wm dismiss-keyguard")) {
            ParcelFileDescriptor.AutoCloseInputStream(instrumentation.uiAutomation.executeShellCommand(command)).use { it.readBytes() }
        }
        listOf("learning_progress", "navigation_state", "lesson_text_size_settings").forEach {
            snapshots[it] = context.getSharedPreferences(it, Context.MODE_PRIVATE).all.toMap()
        }
        ProgressRepository.resetLesson(context, route, lesson)
        LessonTextSizeSettingsRepository.saveUsesGlobalScale(context, "math_course_01", true)
        LessonTextSizeSettingsRepository.saveGlobalScale(context, 100)
    }

    @After fun restoreUserState() {
        snapshots.forEach { (name, original) ->
            val preferences = context.getSharedPreferences(name, Context.MODE_PRIVATE).edit().clear()
            original.forEach { (key, value) ->
                when (value) {
                    is String -> preferences.putString(key, value)
                    is Int -> preferences.putInt(key, value)
                    is Long -> preferences.putLong(key, value)
                    is Float -> preferences.putFloat(key, value)
                    is Boolean -> preferences.putBoolean(key, value)
                    is Set<*> -> preferences.putStringSet(key, value.filterIsInstance<String>().toSet())
                }
            }
            assertTrue(preferences.commit())
        }
    }

    @Test fun lastFieldAndCheckAreRevealedOnceWhileNavigationAndVerticalDragKeepManualScroll() {
        ActivityScenario.launch<LessonActivity>(intent()).use { scenario ->
            scenario.onActivity { it.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_PORTRAIT }
            await(scenario, "Portrait lesson window settles") {
                it.resources.configuration.orientation == Configuration.ORIENTATION_PORTRAIT && it.hasWindowFocus()
            }
            lateinit var editor: MathExpressionEditor
            lateinit var toolbar: MathKeyboardView
            lateinit var check: View
            lateinit var scroll: ScrollView
            scenario.onActivity { activity ->
                val content = activity.findViewById<ViewGroup>(R.id.lessonContent)
                val fields = tree(content).filterIsInstance<MathExpressionEditor>()
                assertEquals("Use the existing eight structured inputs on sandbox page 17", 8, fields.size)
                editor = fields.last()
                toolbar = tree(content).filterIsInstance<MathKeyboardView>().last()
                check = tree(content).single { it.tag == MathPuzzleRenderer.CHECK_ACTION_TAG }
                scroll = generateSequence(editor.parent) { it.parent }.filterIsInstance<ScrollView>().first()
                assertTrue(editor.setLatex("x_{12}^{23}"))
                editor.model.cursor = MathCursor(listOf(MathCursorStep(0, "lower")), 0)
            }
            instrumentation.waitForIdleSync()
            // The user scrolls to this last response before touching it. Settle the changed
            // expression's layout and reproduce that entry, rather than remotely focusing an
            // off-screen descendant while its new layout and the IME resize are both pending.
            scenario.onActivity {
                scroll.scrollTo(0, scroll.getChildAt(0).height)
                assertTrue("The last field is visible before opening the keyboard", entirelyVisible(editor))
            }
            instrumentation.waitForIdleSync()
            scenario.onActivity { activity ->
                assertTrue(editor.requestFocus())
                (activity.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager)
                    .showSoftInput(editor, InputMethodManager.SHOW_IMPLICIT)
            }
            await(scenario, "Last field and its Comprobar are visible above the actual IME toolbar",
                diagnostic = { activity ->
                    val frame = Rect().also { activity.window.decorView.getWindowVisibleDisplayFrame(it) }
                    fun describe(view: View): String {
                        val visible = Rect()
                        val intersects = view.getGlobalVisibleRect(visible)
                        return "screen=${screenBounds(view)}, visible=$visible/$intersects, size=${view.width}x${view.height}, " +
                            "shown=${view.isShown}, attached=${view.isAttachedToWindow}, focus=${view.hasFocus()}"
                    }
                    "frame=$frame, ime=${ViewCompat.getRootWindowInsets(activity.window.decorView)?.isVisible(WindowInsetsCompat.Type.ime())}, " +
                        "editor={${describe(editor)}}, check={${describe(check)}}, toolbar={${describe(toolbar)}}, " +
                        "scroll={${describe(scroll)}; y=${scroll.scrollY}; bottomPadding=${scroll.paddingBottom}; " +
                        "childHeight=${scroll.getChildAt(0).height}; canScrollDown=${scroll.canScrollVertically(1)}}"
                }) { activity ->
                ViewCompat.getRootWindowInsets(activity.window.decorView)?.isVisible(WindowInsetsCompat.Type.ime()) == true &&
                    editor.hasFocus() && toolbar.isShown && entirelyVisible(editor) && entirelyVisible(check) &&
                    screenBounds(editor).bottom <= screenBounds(toolbar).top &&
                    screenBounds(check).bottom <= screenBounds(toolbar).top
            }
            instrumentation.waitForIdleSync()
            SystemClock.sleep(250)
            var revealedY = 0
            lateinit var originalExpression: MathExpression.Sequence
            scenario.onActivity {
                revealedY = scroll.scrollY
                assertTrue("The last field needed a real initial reveal", revealedY > MathWidgetUi.dp(context, 64))
                originalExpression = editor.model.expression
            }
            repeat(3) {
                scenario.onActivity {
                    val path = listOf(MathCursorStep(0, "lower"))
                    editor.onCursorRequested?.invoke(MathCursor(path, requireNotNull(editor.model.sequenceAt(path)).children.size))
                    editor.perform(MathKeyboardAction.PreviousSlot)
                    assertNotNull("Left from END creates a genuine mathematical selection", editor.model.selectionRange)
                }
                instrumentation.waitForIdleSync()
                scenario.onActivity {
                    assertEquals("Selection-only navigation does not repeat the special scroll", revealedY, scroll.scrollY)
                    editor.perform(MathKeyboardAction.PreviousSlot)
                    assertNull(editor.model.selectionRange)
                    assertEquals(0, editor.model.cursor.offset)
                }
                instrumentation.waitForIdleSync()
                scenario.onActivity {
                    assertEquals("Collapsing the selection does not repeat the special scroll", revealedY, scroll.scrollY)
                    assertEquals(originalExpression, editor.model.expression)
                }
            }

            var beforeDrag = MathCursor()
            var startX = 0f
            var startY = 0f
            scenario.onActivity {
                beforeDrag = editor.model.cursor
                val bounds = screenBounds(editor)
                startX = bounds.exactCenterX()
                startY = bounds.exactCenterY()
                assertTrue(entirelyVisible(editor))
            }
            dragDown(startX, startY, MathWidgetUi.dp(context, 80).toFloat())
            await(scenario, "A vertical drag starting on the editor scrolls the lesson") {
                scroll.scrollY < revealedY - MathWidgetUi.dp(context, 16)
            }
            var manualY = 0
            scenario.onActivity {
                manualY = scroll.scrollY
                assertEquals("Dragging does not commit the pending touch cursor", beforeDrag, editor.model.cursor)
                assertNull("Dragging never selects slot contents", editor.model.selectionRange)
                assertEquals(originalExpression, editor.model.expression)
                assertTrue(editor.hasFocus())
                MathKeyboardHost.current(it)?.refreshPosition()
            }
            instrumentation.waitForIdleSync()
            SystemClock.sleep(250)
            scenario.onActivity {
                assertEquals("Toolbar refresh respects the user's manual scroll", manualY, scroll.scrollY)
                val path = listOf(MathCursorStep(0, "lower"))
                editor.onCursorRequested?.invoke(MathCursor(path, requireNotNull(editor.model.sequenceAt(path)).children.size))
                editor.perform(MathKeyboardAction.PreviousSlot)
                assertNotNull(editor.model.selectionRange)
            }
            instrumentation.waitForIdleSync()
            scenario.onActivity {
                assertEquals("Navigation after a drag respects the user's manual scroll", manualY, scroll.scrollY)
                assertEquals(originalExpression, editor.model.expression)
            }
        }
    }

    private fun dragDown(x: Float, y: Float, distance: Float) {
        val started = SystemClock.uptimeMillis()
        fun send(action: Int, currentY: Float) {
            val event = MotionEvent.obtain(started, SystemClock.uptimeMillis(), action, x, currentY, 0)
            event.source = InputDevice.SOURCE_TOUCHSCREEN
            try { assertTrue(instrumentation.uiAutomation.injectInputEvent(event, true)) } finally { event.recycle() }
        }
        send(MotionEvent.ACTION_DOWN, y)
        repeat(8) { step ->
            SystemClock.sleep(30)
            send(MotionEvent.ACTION_MOVE, y + distance * (step + 1) / 8f)
        }
        // Keep the final samples stationary so this checks dragging rather than inertial fling.
        repeat(3) {
            SystemClock.sleep(80)
            send(MotionEvent.ACTION_MOVE, y + distance)
        }
        send(MotionEvent.ACTION_UP, y + distance)
        instrumentation.waitForIdleSync()
    }

    private fun entirelyVisible(view: View): Boolean {
        val visible = Rect()
        return view.getGlobalVisibleRect(visible) && visible.height() == view.height
    }

    private fun screenBounds(view: View): Rect {
        val location = IntArray(2)
        view.getLocationOnScreen(location)
        return Rect(location[0], location[1], location[0] + view.width, location[1] + view.height)
    }

    private fun await(scenario: ActivityScenario<LessonActivity>, message: String,
        diagnostic: ((LessonActivity) -> String)? = null, ready: (LessonActivity) -> Boolean) {
        var fulfilled = false
        repeat(40) {
            if (!fulfilled) {
                SystemClock.sleep(100)
                scenario.onActivity { fulfilled = ready(it) }
            }
        }
        if (!fulfilled) {
            var detail = ""
            scenario.onActivity { detail = diagnostic?.invoke(it).orEmpty() }
            captureScreenshot("progressive-slot-scroll-initial.png")
            fail("$message; $detail")
        }
    }

    private fun captureScreenshot(name: String) {
        val bitmap = instrumentation.uiAutomation.takeScreenshot() ?: return
        try {
            if (android.os.Build.VERSION.SDK_INT >= 31) {
                ParcelFileDescriptor.AutoCloseInputStream(instrumentation.uiAutomation.executeShellCommand(
                    "mkdir -p /data/local/tmp/math-puzzle-smoke")).use { it.readBytes() }
                val streams = instrumentation.uiAutomation.executeShellCommandRw(
                    "dd of=/data/local/tmp/math-puzzle-smoke/$name")
                ParcelFileDescriptor.AutoCloseOutputStream(streams[1]).use { bitmap.compress(Bitmap.CompressFormat.PNG, 100, it) }
                ParcelFileDescriptor.AutoCloseInputStream(streams[0]).use { it.readBytes() }
            }
        } finally { bitmap.recycle() }
    }

    private fun tree(view: View): List<View> = listOf(view) + if (view is ViewGroup)
        (0 until view.childCount).flatMap { tree(view.getChildAt(it)) } else emptyList()

    private fun intent() = Intent(context, LessonActivity::class.java)
        .putExtra("routeId", route).putExtra("courseId", "math_course_01")
        .putExtra("unitId", "arithmetic_fundamentals").putExtra("lessonId", lesson).putExtra("lessonNumber", 4)
}
