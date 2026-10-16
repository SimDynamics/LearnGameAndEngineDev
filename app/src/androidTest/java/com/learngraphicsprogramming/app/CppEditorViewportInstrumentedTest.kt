package com.learngraphicsprogramming.app

import android.content.pm.ActivityInfo
import android.app.Activity
import android.app.Application
import android.content.Context
import android.content.res.Configuration
import android.graphics.Rect
import android.os.SystemClock
import android.os.Bundle
import android.os.ParcelFileDescriptor
import android.view.InputDevice
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.view.ViewConfiguration
import android.view.WindowManager
import android.view.inputmethod.InputMethodManager
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.TextView
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.doOnPreDraw
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import kotlin.math.abs
import kotlin.math.ceil
import org.junit.Assert.assertEquals
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.Before
import org.junit.After
import org.junit.runner.RunWith

/** Real standalone editor geometry and gestures; source samples live only in the Activity. */
@RunWith(AndroidJUnit4::class)
class CppEditorViewportInstrumentedTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val application = instrumentation.targetContext.applicationContext as Application
    private val savedStates = mutableListOf<Bundle>()
    private val screenGuard = object : Application.ActivityLifecycleCallbacks {
        override fun onActivityCreated(activity: Activity, state: Bundle?) {
            if (activity is CppPrototypeActivity) activity.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        }
        override fun onActivityStarted(activity: Activity) = Unit
        override fun onActivityResumed(activity: Activity) = Unit
        override fun onActivityPaused(activity: Activity) = Unit
        override fun onActivityStopped(activity: Activity) = Unit
        override fun onActivitySaveInstanceState(activity: Activity, state: Bundle) {
            if (activity is CppPrototypeActivity) savedStates += Bundle(state)
        }
        override fun onActivityDestroyed(activity: Activity) = Unit
    }

    @Before fun keepOnlyTheTestActivitiesAwake() {
        savedStates.clear()
        application.registerActivityLifecycleCallbacks(screenGuard)
        for (command in listOf("input keyevent KEYCODE_WAKEUP", "wm dismiss-keyguard")) {
            ParcelFileDescriptor.AutoCloseInputStream(instrumentation.uiAutomation.executeShellCommand(command)).use { it.readBytes() }
        }
    }

    @After fun releaseTestScreenGuard() {
        application.unregisterActivityLifecycleCallbacks(screenGuard)
    }
    private val longSource = (1..140).joinToString("\n") { line ->
        "int value_$line = $line; // " + "long_expression_token ".repeat(16)
    }

    @Test
    fun longCodeHasThreeDigitGutterAndWrappedContinuationRowsStayBlankAndAligned() = withEditor { scenario ->
        setSource(scenario)
        await(scenario, "Long source must expose both actual scroll ranges") { activity ->
            source(activity).layout?.lineCount == 140 && horizontal(activity).max > 0 && vertical(activity).max > 0 &&
                activity.findViewById<TextView>(R.id.lineNumbers).text.toString().split('\n').size == 140
        }
        scenario.onActivity { activity ->
            val input = source(activity)
            val gutter = activity.findViewById<TextView>(R.id.lineNumbers)
            val gutterScroll = activity.findViewById<ScrollView>(R.id.lineNumberScroll)
            assertEquals((1..140).map(Int::toString), gutter.text.toString().split('\n'))
            assertTrue("Line 140 must fit without clipping", gutterScroll.width >=
                ceil(gutter.paint.measureText("140").toDouble()).toInt() + gutter.paddingLeft + gutter.paddingRight)
            assertEquals("Only ten code rows occupy the normal editor",
                input.compoundPaddingTop + input.lineHeight * 10 + input.compoundPaddingBottom,
                activity.findViewById<View>(R.id.codeEditorBody).height)
            assertEquals(View.VISIBLE, horizontal(activity).visibility)
            assertEquals(View.VISIBLE, vertical(activity).visibility)
            activity.findViewById<View>(R.id.alignCodeButton).performClick()
        }
        await(scenario, "Wrapping must remove horizontal overflow and populate visual continuation rows") { activity ->
            val input = source(activity)
            val gutter = activity.findViewById<TextView>(R.id.lineNumbers)
            val rows = gutter.text.toString().split('\n')
            val visualCount = input.layout?.lineCount ?: 0
            horizontal(activity).max == 0 && input.scrollX == 0 && visualCount > 140 && rows.size == visualCount &&
                gutter.layout?.lineCount == visualCount
        }
        scenario.onActivity { activity ->
            val rows = activity.findViewById<TextView>(R.id.lineNumbers).text.toString().split('\n')
            assertTrue("Wrapped continuation rows must have no repeated logical number", rows.any(String::isEmpty))
            assertEquals((1..140).map(Int::toString), rows.filter(String::isNotEmpty))
            assertEquals(View.GONE, horizontal(activity).visibility)
            val input = source(activity)
            input.suppressCursorAutoScroll = true
            input.scrollTo(0, vertical(activity).max / 3)
        }
        await(scenario, "Gutter and vertical bar must follow the wrapped source") { activity ->
            val input = source(activity)
            input.scrollY > 0 && activity.findViewById<TextView>(R.id.lineNumbers).translationY == -input.scrollY.toFloat() &&
                vertical(activity).progress == input.scrollY
        }
        scenario.onActivity { activity -> assertGutterBaselineAlignment(activity) }
        // A later short program must reset stale bar ranges, not merely hide them.
        scenario.onActivity { activity -> source(activity).setText("int main() { return 0; }") }
        await(scenario, "A short wrapped program must clear both scroll ranges") { activity ->
            horizontal(activity).max == 0 && vertical(activity).max == 0 && source(activity).scrollX == 0 && source(activity).scrollY == 0
        }
    }

    @Test
    fun actualHorizontalAndVerticalDragsScrollCodeWithoutMovingTheSelectionOrOuterPage() = withEditor { scenario ->
        setSource(scenario)
        await(scenario, "Editor must have overflowing source before dragging") { activity ->
            horizontal(activity).max > 0 && vertical(activity).max > 0
        }
        val bounds = Rect()
        var outerScroll = 0
        scenario.onActivity { activity ->
            val input = source(activity)
            input.requestFocus()
            input.setSelection(4, 12)
            input.suppressCursorAutoScroll = true
            input.scrollTo(0, 0)
        }
        instrumentation.waitForIdleSync()
        scenario.onActivity { activity ->
            val input = source(activity)
            assertTrue("The real editor must be visible for injected touch", input.getGlobalVisibleRect(bounds))
            assertTrue(bounds.width() > 100 && bounds.height() > 100)
            outerScroll = activity.findViewById<ScrollView>(R.id.compilerScroll).scrollY
        }
        swipe(bounds.left + bounds.width() * 0.8f, bounds.top + bounds.height() * 0.4f,
            bounds.left + bounds.width() * 0.2f, bounds.top + bounds.height() * 0.4f)
        await(scenario, "A horizontal drag must move the actual source viewport") { source(it).scrollX > 0 }
        scenario.onActivity { activity ->
            assertSelection(activity, 4, 12)
            assertEquals(0, source(activity).scrollY)
            assertEquals(outerScroll, activity.findViewById<ScrollView>(R.id.compilerScroll).scrollY)
            assertTrue(source(activity).getGlobalVisibleRect(bounds))
        }
        swipe(bounds.left + bounds.width() * 0.5f, bounds.top + bounds.height() * 0.8f,
            bounds.left + bounds.width() * 0.5f, bounds.top + bounds.height() * 0.2f)
        await(scenario, "A vertical drag must scroll source and gutter together") { activity ->
            source(activity).scrollY > 0 && vertical(activity).progress == source(activity).scrollY &&
                activity.findViewById<TextView>(R.id.lineNumbers).translationY == -source(activity).scrollY.toFloat()
        }
        scenario.onActivity { activity ->
            assertSelection(activity, 4, 12)
            assertEquals(outerScroll, activity.findViewById<ScrollView>(R.id.compilerScroll).scrollY)
            assertGutterBaselineAlignment(activity)
        }
    }

    @Test
    fun realLongPressRetainsNativeWordSelectionWithoutPanningTheCode() = withEditor { scenario ->
        val code = List(3) { "int alpha = 1;" }.joinToString("\n")
        scenario.onActivity { activity ->
            source(activity).apply {
                setText(code)
                requestFocus()
                setSelection(0)
                scrollTo(0, 0)
            }
        }
        await(scenario, "Short source must complete layout before native word selection") { activity ->
            source(activity).layout?.lineCount == 3 && horizontal(activity).max == 0 && vertical(activity).max == 0
        }
        var wordX = 0f
        var wordY = 0f
        var outerScroll = 0
        scenario.onActivity { activity ->
            val input = source(activity)
            val layout = requireNotNull(input.layout)
            val wordStart = code.indexOf("alpha")
            val wordEnd = wordStart + "alpha".length
            val line = layout.getLineForOffset(wordStart)
            val location = IntArray(2)
            input.getLocationOnScreen(location)
            wordX = location[0] + input.totalPaddingLeft - input.scrollX +
                (layout.getPrimaryHorizontal(wordStart) + layout.getPrimaryHorizontal(wordEnd)) / 2f
            wordY = location[1] + input.totalPaddingTop - input.scrollY +
                (layout.getLineTop(line) + layout.getLineBottom(line)) / 2f
            val visible = Rect()
            assertTrue("The selected word must lie inside the actual visible editor",
                input.getGlobalVisibleRect(visible) && visible.contains(wordX.toInt(), wordY.toInt()))
            outerScroll = activity.findViewById<ScrollView>(R.id.compilerScroll).scrollY
        }
        longPress(wordX, wordY)
        await(scenario, "The native EditText long press must select the whole alpha word") { activity ->
            val input = source(activity)
            input.selectionStart == code.indexOf("alpha") && input.selectionEnd == code.indexOf("alpha") + 5
        }
        scenario.onActivity { activity ->
            assertEquals(code, source(activity).text.toString())
            assertEquals(0, source(activity).scrollX)
            assertEquals(0, source(activity).scrollY)
            assertEquals(outerScroll, activity.findViewById<ScrollView>(R.id.compilerScroll).scrollY)
            assertTrue("Native selection must leave the compiler open", !activity.isFinishing)
        }
        // ActivityScenario closes this temporary editor and its native ActionMode;
        // no copy action or clipboard write is needed for selection validation.
    }

    @Test
    fun fullscreenUsesTheViewportInBothOrientationsAndBackRestoresTheNormalPanels() = withEditor { scenario ->
        setSource(scenario)
        val originalVisibility = mutableMapOf<Int, Int>()
        var originalActivity: CppPrototypeActivity? = null
        scenario.onActivity { activity ->
            originalActivity = activity
            val content = activity.findViewById<ViewGroup>(R.id.compilerContent)
            for (index in 0 until content.childCount) {
                val child = content.getChildAt(index)
                if (child.id != R.id.codeCard) originalVisibility[index] = child.visibility
            }
            source(activity).setSelection(4, 12)
            activity.findViewById<View>(R.id.fullscreenCodeButton).performClick()
        }
        await(scenario, "Fullscreen card must fill its real viewport") { fullscreenFits(it) }
        scenario.onActivity { activity ->
            assertFullscreenPanelsHidden(activity)
            activity.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
        }
        await(scenario, "Landscape must resize the existing fullscreen editor") { activity ->
            activity.resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE && fullscreenFits(activity)
        }
        scenario.onActivity { activity ->
            assertSame("Handled rotation must preserve this Activity/editor", originalActivity, activity)
            assertEquals(longSource, source(activity).text.toString())
            assertSelection(activity, 4, 12)
            assertFullscreenPanelsHidden(activity)
            activity.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
        }
        await(scenario, "Portrait must also retain fullscreen geometry") { activity ->
            activity.resources.configuration.orientation == Configuration.ORIENTATION_PORTRAIT && fullscreenFits(activity)
        }
        scenario.onActivity { it.onBackPressedDispatcher.onBackPressed() }
        await(scenario, "Back must restore the normal editor without closing the compiler") { activity ->
            val content = activity.findViewById<ViewGroup>(R.id.compilerContent)
            !activity.isFinishing && originalVisibility.all { (index, visibility) -> content.getChildAt(index).visibility == visibility }
        }
        scenario.onActivity { activity ->
            assertEquals(longSource, source(activity).text.toString())
            assertSelection(activity, 4, 12)
            assertEquals(activity.getString(R.string.exercise_fullscreen),
                activity.findViewById<View>(R.id.fullscreenCodeButton).contentDescription.toString())
        }
    }

    @Test
    fun recreationRetainsSourceSelectionScrollAndFullscreen() = withEditor { scenario ->
        setSource(scenario)
        scenario.onActivity { it.findViewById<View>(R.id.fullscreenCodeButton).performClick() }
        await(scenario, "Fullscreen source must be ready before saving its viewport") { activity ->
            fullscreenFits(activity) && horizontal(activity).max > 0 && vertical(activity).max > 0
        }
        var savedX = 0
        var savedY = 0
        scenario.onActivity { activity ->
            val input = source(activity)
            input.setSelection(4, 12)
            input.suppressCursorAutoScroll = true
            savedX = minOf(120, horizontal(activity).max)
            savedY = minOf(240, vertical(activity).max)
            input.scrollTo(savedX, savedY)
        }
        await(scenario, "The saved source scroll must be visible before recreation") { activity ->
            source(activity).scrollX == savedX && source(activity).scrollY == savedY
        }
        scenario.recreate()
        scenario.onActivity {
            val saved = savedStates.last()
            assertEquals("The real lifecycle must save the panned X before restoration", savedX,
                saved.getInt("cpp_prototype_scroll_x"))
            assertEquals("The real lifecycle must save the panned Y before restoration", savedY,
                saved.getInt("cpp_prototype_scroll_y"))
            assertTrue("Manual-pan cursor suppression belongs to that same saved viewport",
                saved.getBoolean("cpp_prototype_manual_viewport"))
        }
        await(scenario, "Recreated editor must restore its actual saved viewport and selection") { activity ->
            val input = source(activity)
            input.showSoftInputOnFocus = false
            fullscreenFits(activity) && input.text.toString() == longSource &&
                input.selectionStart == 4 && input.selectionEnd == 12 && input.scrollX == savedX && input.scrollY == savedY
        }
        scenario.onActivity { activity ->
            assertFullscreenPanelsHidden(activity)
            assertGutterBaselineAlignment(activity)
            assertEquals(savedX, horizontal(activity).progress)
            assertEquals(savedY, vertical(activity).progress)
            assertTrue(source(activity).suppressCursorAutoScroll)
        }
        // A second real layout/insets pass exercises TextView's pre-draw cursor reveal,
        // rather than accepting a scrollTo value observed before the pending traversal.
        val drawn = CountDownLatch(1)
        scenario.onActivity { activity ->
            source(activity).doOnPreDraw { drawn.countDown() }
            source(activity).requestLayout()
            ViewCompat.requestApplyInsets(activity.findViewById(android.R.id.content))
        }
        assertTrue("The restored editor must actually traverse another native pre-draw",
            drawn.await(5, TimeUnit.SECONDS))
        instrumentation.waitForIdleSync()
        scenario.onActivity { activity ->
            assertEquals(savedX, source(activity).scrollX)
            assertEquals(savedY, source(activity).scrollY)
            assertSelection(activity, 4, 12)
            assertTrue(fullscreenFits(activity))
            assertFullscreenPanelsHidden(activity)
            assertGutterBaselineAlignment(activity)
        }
        scenario.recreate()
        await(scenario, "A second recreation must retain the same viewport, selection and fullscreen") { activity ->
            val input = source(activity)
            input.showSoftInputOnFocus = false
            fullscreenFits(activity) && input.text.toString() == longSource &&
                input.selectionStart == 4 && input.selectionEnd == 12 &&
                input.scrollX == savedX && input.scrollY == savedY &&
                horizontal(activity).progress == savedX && vertical(activity).progress == savedY
        }
    }

    @Test
    fun landscapeFullscreenWithRealImeRetainsManualPanWhenInsetsAreReapplied() = withEditor { scenario ->
        setSource(scenario)
        scenario.onActivity { activity ->
            activity.findViewById<View>(R.id.fullscreenCodeButton).performClick()
            activity.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
        }
        await(scenario, "Landscape fullscreen must finish its layout before opening the real IME") { activity ->
            activity.resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE && fullscreenFits(activity)
        }
        try {
            scenario.onActivity { activity ->
                val input = source(activity)
                input.showSoftInputOnFocus = true
                input.requestFocus()
                input.setSelection(4, 12)
                (activity.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager)
                    .showSoftInput(input, InputMethodManager.SHOW_IMPLICIT)
            }
            val bounds = Rect()
            await(scenario, "The real IME must resize landscape fullscreen while leaving the source visible") { activity ->
                ViewCompat.getRootWindowInsets(activity.findViewById(android.R.id.content))
                    ?.isVisible(WindowInsetsCompat.Type.ime()) == true && fullscreenFits(activity) &&
                    activity.findViewById<View>(R.id.codeHeader).visibility == View.GONE &&
                    horizontal(activity).max > 0 && vertical(activity).max > 0 &&
                    source(activity).getGlobalVisibleRect(bounds) && bounds.width() > 100 && bounds.height() > 40
            }
            scenario.onActivity { activity ->
                val input = source(activity)
                input.setSelection(4, 12)
                input.suppressCursorAutoScroll = true
                input.scrollTo(0, 0)
            }
            instrumentation.waitForIdleSync()
            scenario.onActivity { assertTrue(source(it).getGlobalVisibleRect(bounds)) }
            swipe(bounds.left + bounds.width() * 0.8f, bounds.top + bounds.height() * 0.4f,
                bounds.left + bounds.width() * 0.2f, bounds.top + bounds.height() * 0.4f)
            await(scenario, "Horizontal touch must pan away from the cursor with the IME open") {
                source(it).scrollX > 0
            }
            scenario.onActivity { assertTrue(source(it).getGlobalVisibleRect(bounds)) }
            swipe(bounds.left + bounds.width() * 0.5f, bounds.top + bounds.height() * 0.8f,
                bounds.left + bounds.width() * 0.5f, bounds.top + bounds.height() * 0.2f)
            await(scenario, "Vertical touch must pan away from the cursor with the IME open") {
                source(it).scrollX > 0 && source(it).scrollY > 0
            }
            var pannedX = 0
            var pannedY = 0
            var insetsApplied = false
            scenario.onActivity { activity ->
                val input = source(activity)
                assertSelection(activity, 4, 12)
                assertTrue(input.suppressCursorAutoScroll)
                pannedX = input.scrollX
                pannedY = input.scrollY
                val root = activity.findViewById<ViewGroup>(android.R.id.content).getChildAt(0)
                ViewCompat.requestApplyInsets(root)
                // Observe subsequent frames so the assertion cannot pass before
                // the requested inset dispatch has updated the Activity geometry.
                root.postOnAnimation { root.postOnAnimation { root.postOnAnimation { insetsApplied = true } } }
            }
            await(scenario, "Reapplying IME insets must preserve the manually chosen source viewport") { activity ->
                insetsApplied && source(activity).scrollX == pannedX && source(activity).scrollY == pannedY &&
                    source(activity).suppressCursorAutoScroll
            }
            scenario.onActivity { activity ->
                assertSelection(activity, 4, 12)
                assertEquals(pannedX, horizontal(activity).progress)
                assertEquals(pannedY, vertical(activity).progress)
                assertGutterBaselineAlignment(activity)
            }
        } finally {
            scenario.onActivity { activity ->
                val input = source(activity)
                input.showSoftInputOnFocus = false
                (activity.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager)
                    .hideSoftInputFromWindow(input.windowToken, 0)
            }
        }
    }

    private fun withEditor(test: (ActivityScenario<CppPrototypeActivity>) -> Unit) {
        ActivityScenario.launch(CppPrototypeActivity::class.java).use { scenario ->
            var originalOrientation = ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
            scenario.onActivity { activity ->
                originalOrientation = activity.requestedOrientation
                source(activity).showSoftInputOnFocus = false
            }
            try {
                await(scenario, "Standalone editor must complete its real layout") { activity ->
                    activity.hasWindowFocus() && source(activity).width > 0 && source(activity).height > 0 && source(activity).layout != null
                }
                test(scenario)
            } finally {
                scenario.onActivity { it.requestedOrientation = originalOrientation }
            }
        }
    }

    private fun setSource(scenario: ActivityScenario<CppPrototypeActivity>) = scenario.onActivity { activity ->
        source(activity).apply { setText(longSource); setSelection(4, 12) }
    }

    private fun source(activity: CppPrototypeActivity) = activity.findViewById<FreeScrollEditText>(R.id.sourceCode)
    private fun horizontal(activity: CppPrototypeActivity) = activity.findViewById<SeekBar>(R.id.editorHorizontalBar)
    private fun vertical(activity: CppPrototypeActivity) = activity.findViewById<SeekBar>(R.id.editorVerticalBar)

    private fun assertSelection(activity: CppPrototypeActivity, start: Int, end: Int) {
        assertEquals("Drag/lifecycle must retain selection start", start, source(activity).selectionStart)
        assertEquals("Drag/lifecycle must retain selection end", end, source(activity).selectionEnd)
    }

    private fun fullscreenFits(activity: CppPrototypeActivity): Boolean {
        val viewport = activity.findViewById<ScrollView>(R.id.compilerScroll)
        val card = activity.findViewById<View>(R.id.codeCard)
        val body = activity.findViewById<View>(R.id.codeEditorBody)
        return viewport.height > 0 && abs(card.height - viewport.height) <= 2 && card.width == viewport.width &&
            viewport.scrollY == 0 && body.height > 0 && source(activity).height == body.height
    }

    private fun assertFullscreenPanelsHidden(activity: CppPrototypeActivity) {
        val content = activity.findViewById<ViewGroup>(R.id.compilerContent)
        for (index in 0 until content.childCount) {
            val child = content.getChildAt(index)
            if (child.id != R.id.codeCard) assertEquals("Fullscreen must reclaim panel ${child.id}", View.GONE, child.visibility)
        }
    }

    private fun assertGutterBaselineAlignment(activity: CppPrototypeActivity) {
        val input = source(activity)
        val gutter = activity.findViewById<TextView>(R.id.lineNumbers)
        val gutterScroll = activity.findViewById<ScrollView>(R.id.lineNumberScroll)
        assertEquals("Gutter ScrollView is fixed; the label follows source translation", 0, gutterScroll.scrollY)
        val line = input.layout.getLineForVertical(input.scrollY)
        val sourceLocation = IntArray(2)
        val gutterLocation = IntArray(2)
        input.getLocationOnScreen(sourceLocation)
        gutter.getLocationOnScreen(gutterLocation)
        val sourceBaseline = sourceLocation[1] + input.paddingTop + input.layout.getLineBaseline(line) - input.scrollY
        val gutterBaseline = gutterLocation[1] + gutter.paddingTop + gutter.layout.getLineBaseline(line)
        assertTrue("Visible source/gutter baselines must align ($sourceBaseline / $gutterBaseline)",
            abs(sourceBaseline - gutterBaseline) <= 2)
    }

    private fun swipe(startX: Float, startY: Float, endX: Float, endY: Float) {
        val downTime = SystemClock.uptimeMillis()
        fun inject(action: Int, x: Float, y: Float) {
            val event = MotionEvent.obtain(downTime, SystemClock.uptimeMillis(), action, x, y, 0).apply {
                source = InputDevice.SOURCE_TOUCHSCREEN
            }
            try { assertTrue("The real touchscreen event must be delivered", instrumentation.uiAutomation.injectInputEvent(event, true)) }
            finally { event.recycle() }
        }
        inject(MotionEvent.ACTION_DOWN, startX, startY)
        for (step in 1..8) {
            SystemClock.sleep(16)
            inject(MotionEvent.ACTION_MOVE, startX + (endX - startX) * step / 8, startY + (endY - startY) * step / 8)
        }
        inject(MotionEvent.ACTION_UP, endX, endY)
        instrumentation.waitForIdleSync()
    }

    private fun longPress(x: Float, y: Float) {
        val downTime = SystemClock.uptimeMillis()
        fun inject(action: Int) {
            val event = MotionEvent.obtain(downTime, SystemClock.uptimeMillis(), action, x, y, 0)
                .apply { source = InputDevice.SOURCE_TOUCHSCREEN }
            try {
                assertTrue("The real long-press event must be delivered",
                    instrumentation.uiAutomation.injectInputEvent(event, true))
            } finally { event.recycle() }
        }
        inject(MotionEvent.ACTION_DOWN)
        SystemClock.sleep(ViewConfiguration.getLongPressTimeout().toLong() + 150)
        inject(MotionEvent.ACTION_UP)
        instrumentation.waitForIdleSync()
    }

    private fun await(scenario: ActivityScenario<CppPrototypeActivity>, message: String,
        condition: (CppPrototypeActivity) -> Boolean) {
        val deadline = SystemClock.uptimeMillis() + 10_000
        while (SystemClock.uptimeMillis() < deadline) {
            instrumentation.waitForIdleSync()
            var ready = false
            scenario.onActivity { ready = condition(it) }
            if (ready) return
            SystemClock.sleep(40) // Instrumentation thread only; never block the UI/layout thread.
        }
        scenario.onActivity { activity ->
            val input = source(activity)
            assertTrue("$message: orientation=${activity.resources.configuration.orientation}, " +
                "editor=${input.width}x${input.height}, scroll=${input.scrollX},${input.scrollY}, " +
                "range=${horizontal(activity).max},${vertical(activity).max}, selection=${input.selectionStart},${input.selectionEnd}",
                condition(activity))
        }
    }
}
