package com.learngraphicsprogramming.app

import android.app.KeyguardManager
import android.content.Context
import android.content.Intent
import android.graphics.PointF
import android.graphics.Rect
import android.os.SystemClock
import android.view.InputDevice
import android.view.MotionEvent
import android.view.View
import android.view.ViewConfiguration
import android.view.ViewGroup
import android.view.WindowManager
import android.view.inputmethod.InputMethodManager
import android.widget.EditText
import android.widget.ScrollView
import android.widget.SeekBar
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.narration.NarrationSettingsRepository
import java.io.File
import java.util.UUID
import org.json.JSONArray
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/** Window-delivered touch against the real lesson editor, parent ScrollView and page navigation. */
@RunWith(AndroidJUnit4::class)
class CppEditorEmptySpaceGesturesDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val snapshots = linkedMapOf<String, Map<String, *>>()
    private var originalFiles = emptySet<String>()
    private var captured = false

    private enum class Axis { X, Y }
    private enum class Overflow(val x: Boolean, val y: Boolean) {
        NONE(false, false), X(true, false), Y(false, true), XY(true, true);

        fun owns(axis: Axis) = if (axis == Axis.X) x else y
        fun code(): String = buildList {
            add("int alpha = 1;")
            if (x) add("// " + "long_expression_token ".repeat(90))
            if (y) repeat(26) { add("int n$it = $it;") }
        }.joinToString("\n")
    }

    @Before fun preserveOnlyTheIsolatedQaState() {
        assertEquals("com.learngraphicsprogramming.app.realdownloadsqa", context.packageName)
        assertEquals(context.packageName, BuildConfig.APPLICATION_ID)
        val lock = context.getSystemService(KeyguardManager::class.java)
        assertFalse("Unlock the test device before injecting real touch", lock.isDeviceLocked || lock.isKeyguardLocked)
        originalFiles = preferenceFiles()
        originalFiles.forEach { name ->
            snapshots[name] = context.getSharedPreferences(name, Context.MODE_PRIVATE).all.mapValues { (_, value) ->
                if (value is Set<*>) value.toSet() else value
            }
        }
        captured = true
        NarrationSettingsRepository.saveEnabled(context, false)
    }

    @After fun restoreAllQaPreferences() {
        if (!captured) return
        instrumentation.waitForIdleSync()
        (preferenceFiles() + snapshots.keys).forEach { name ->
            if (name !in originalFiles) assertTrue(context.deleteSharedPreferences(name))
            else {
                val target = context.getSharedPreferences(name, Context.MODE_PRIVATE)
                val editor = target.edit().clear()
                snapshots.getValue(name).forEach { (key, value) ->
                    when (value) {
                        is String -> editor.putString(key, value)
                        is Int -> editor.putInt(key, value)
                        is Long -> editor.putLong(key, value)
                        is Float -> editor.putFloat(key, value)
                        is Boolean -> editor.putBoolean(key, value)
                        is Set<*> -> editor.putStringSet(key, value.filterIsInstance<String>().toSet())
                        else -> error("Unsupported preference $name/$key")
                    }
                }
                assertTrue(editor.commit())
                assertEquals(snapshots[name], target.all)
            }
        }
        assertEquals(originalFiles, preferenceFiles())
    }

    @Test fun emptySpaceRoutesEachAxisByItsActualRangeForImmediateAndHeldDrags() = withLesson { scenario ->
        for (overflow in Overflow.values()) {
            for (held in listOf(false, true)) {
                for (axis in Axis.values()) {
                    checkEmptyDrag(scenario, overflow, axis, held, edge = false, belowLastLine = false)
                    if (overflow.owns(axis)) {
                        checkEmptyDrag(scenario, overflow, axis, held, edge = true, belowLastLine = false)
                    }
                }
            }
        }
    }

    @Test fun spaceBelowTheLastLineAlsoDelegatesAfterHoldingWithoutMovingTheCaret() = withLesson { scenario ->
        for (overflow in listOf(Overflow.NONE, Overflow.X)) {
            for (held in listOf(false, true)) {
                for (axis in Axis.values()) {
                    checkEmptyDrag(scenario, overflow, axis, held, edge = false, belowLastLine = true)
                }
            }
        }
    }

    @Test fun longPressOverGlyphsStillSelectsTheNativeWordAndDoesNotNavigateOrPan() = withLesson { scenario ->
        prepare(scenario, Overflow.NONE)
        var target = PointF()
        var outer = 0
        scenario.onActivity { activity ->
            val input = source(activity)
            val layout = requireNotNull(input.layout)
            val start = input.text.toString().indexOf("alpha")
            val origin = IntArray(2).also(input::getLocationOnScreen)
            target = PointF(origin[0] + input.totalPaddingLeft +
                (layout.getPrimaryHorizontal(start) + layout.getPrimaryHorizontal(start + 5)) / 2,
                origin[1] + input.totalPaddingTop + (layout.getLineTop(0) + layout.getLineBottom(0)) / 2f)
            outer = scroll(activity).scrollY
        }
        touch(target, target, held = true, move = false)
        await(scenario, "A held touch over glyphs must retain native word selection") { activity ->
            val input = source(activity)
            input.selectionStart == 4 && input.selectionEnd == 9
        }
        scenario.onActivity { activity ->
            assertEquals(1, field(activity, "currentPage"))
            assertEquals(outer, scroll(activity).scrollY)
            assertEquals(0, source(activity).scrollX)
            assertEquals(0, source(activity).scrollY)
            assertEquals(Overflow.NONE.code(), source(activity).text.toString())
        }
    }

    @Test fun actualScrollBarsRetainControlOwnershipAndLeaveLessonAndInputIntact() = withLesson { scenario ->
        prepare(scenario, Overflow.XY)
        for (axis in Axis.values()) {
            var from = PointF()
            var to = PointF()
            var outer = 0
            scenario.onActivity { activity ->
                val input = source(activity)
                val bar = if (axis == Axis.X) horizontal(input) else vertical(input)
                val bounds = Rect()
                assertTrue("The actual $axis bar is visible", bar.getGlobalVisibleRect(bounds))
                outer = scroll(activity).scrollY
                if (axis == Axis.X) {
                    from = PointF(bounds.left + bounds.width() * .25f, bounds.exactCenterY())
                    to = PointF(bounds.left + bounds.width() * .75f, bounds.exactCenterY())
                } else {
                    from = PointF(bounds.exactCenterX(), bounds.top + bounds.height() * .25f)
                    to = PointF(bounds.exactCenterX(), bounds.top + bounds.height() * .75f)
                }
            }
            touch(from, to, held = true)
            await(scenario, "The real $axis scroll bar pans the editor") { activity ->
                if (axis == Axis.X) source(activity).scrollX > 0 else source(activity).scrollY > 0
            }
            scenario.onActivity { activity ->
                assertEquals(1, field(activity, "currentPage"))
                assertEquals(outer, scroll(activity).scrollY)
                assertEquals(Overflow.XY.code(), source(activity).text.toString())
                assertEquals("fixture input", (tagged(activity, "cpp_input") as EditText).text.toString())
                assertTrue(tagged(activity, "cpp_run").isEnabled)
                assertTrue(tagged(activity, "cpp_compiler_fullscreen").isShown)
                assertEquals(source(activity).scrollX, horizontal(source(activity)).progress)
                assertEquals(source(activity).scrollY, vertical(source(activity)).progress)
            }
        }
    }

    @Test fun emptyTapAfterPanningStillPlacesTheCaretCancelIsHarmlessAndFullscreenNeverChangesPages() = withLesson { scenario ->
        prepare(scenario, Overflow.X)
        var point = PointF()
        scenario.onActivity { activity ->
            val input = source(activity)
            input.suppressCursorAutoScroll = true
            input.scrollTo(horizontal(input).max, 0)
            point = emptyPoint(input, below = false)
        }
        touch(point, point, held = false, move = false)
        await(scenario, "An empty tap replays normal caret behavior after manual panning") { activity ->
            val input = source(activity)
            input.hasFocus() && input.selectionStart == Overflow.X.code().indexOf('\n') &&
                input.selectionEnd == input.selectionStart && !input.suppressCursorAutoScroll
        }
        var caret = 0
        scenario.onActivity { activity ->
            caret = source(activity).selectionStart
            point = emptyPoint(source(activity), below = false)
        }
        touch(point, point, held = true, move = false, cancel = true)
        scenario.onActivity { activity ->
            assertEquals(Overflow.X.code(), source(activity).text.toString())
            assertEquals(caret, source(activity).selectionStart)
            assertEquals(caret, source(activity).selectionEnd)
            assertEquals(1, field(activity, "currentPage"))
        }
        prepare(scenario, Overflow.NONE)
        lateinit var selected: FreeScrollEditText
        var finish = PointF()
        scenario.onActivity { activity ->
            selected = source(activity)
            selected.setSelection(4, 9)
            point = emptyPoint(selected, below = true)
            finish = PointF(point.x - selected.width * .58f, point.y)
        }
        touch(point, finish, held = true)
        await(scenario, "An existing selection cannot veto navigation that began in empty space") {
            field(it, "currentPage") == 2
        }
        scenario.onActivity {
            assertEquals(4, selected.selectionStart)
            assertEquals(9, selected.selectionEnd)
            assertEquals(Overflow.NONE.code(), selected.text.toString())
        }
        prepare(scenario, Overflow.NONE)
        scenario.onActivity { tagged(it, "cpp_compiler_fullscreen").performClick() }
        await(scenario, "The original fullscreen viewport is active") { activity ->
            field(activity, "compilerFullscreenActive") == true && source(activity).height > 0
        }
        scenario.onActivity { activity ->
            val input = source(activity)
            point = emptyPoint(input, below = true)
            finish = PointF(point.x - input.width * .58f, point.y)
        }
        touch(point, finish, held = true)
        scenario.onActivity { activity ->
            assertEquals(1, field(activity, "currentPage"))
            assertEquals(true, field(activity, "compilerFullscreenActive"))
            assertEquals(Overflow.NONE.code(), source(activity).text.toString())
            activity.onBackPressedDispatcher.onBackPressed()
        }
        await(scenario, "Leaving fullscreen restores the original controls") { activity ->
            field(activity, "compilerFullscreenActive") == false && tagged(activity, "cpp_run").isShown
        }
    }

    private fun checkEmptyDrag(scenario: ActivityScenario<LessonActivity>, overflow: Overflow, axis: Axis,
                               held: Boolean, edge: Boolean, belowLastLine: Boolean) {
        prepare(scenario, overflow)
        var from = PointF()
        var to = PointF()
        var outer = 0
        var initialX = 0
        var initialY = 0
        lateinit var touched: FreeScrollEditText
        scenario.onActivity { activity ->
            touched = source(activity)
            if (edge) {
                touched.suppressCursorAutoScroll = true
                touched.scrollTo(if (axis == Axis.X) horizontal(touched).max else 0,
                    if (axis == Axis.Y) vertical(touched).max else 0)
            }
            initialX = touched.scrollX
            initialY = touched.scrollY
            outer = scroll(activity).scrollY
            from = emptyPoint(touched, belowLastLine)
            val density = activity.resources.displayMetrics.density
            val distance = if (axis == Axis.X) maxOf(80 * density, touched.width * .58f)
                else minOf(touched.height * .45f, 120 * density)
            to = PointF(from.x - if (axis == Axis.X) distance else 0f,
                from.y - if (axis == Axis.Y) distance else 0f)
        }
        val label = "$overflow/$axis held=$held edge=$edge below=$belowLastLine"
        touch(from, to, held)
        await(scenario, "Empty-space routing: $label") { activity ->
            if (!overflow.owns(axis)) {
                if (axis == Axis.X) field(activity, "currentPage") == 2
                else field(activity, "currentPage") == 1 && scroll(activity).scrollY > outer + 2
            } else if (edge) field(activity, "currentPage") == 1 && scroll(activity).scrollY == outer
            else field(activity, "currentPage") == 1 &&
                (if (axis == Axis.X) touched.scrollX > initialX else touched.scrollY > initialY)
        }
        scenario.onActivity { activity ->
            assertEquals("Source remains unchanged: $label", overflow.code(), touched.text.toString())
            assertEquals("The empty drag preserves the caret: $label", 0, touched.selectionStart)
            assertEquals("The empty drag preserves the caret: $label", 0, touched.selectionEnd)
            if (overflow.owns(axis)) {
                assertEquals("An editor-owned axis cannot scroll the lesson: $label", outer, scroll(activity).scrollY)
                if (edge) {
                    assertEquals(initialX, touched.scrollX)
                    assertEquals(initialY, touched.scrollY)
                }
                assertEquals(touched.scrollX, horizontal(touched).progress)
                assertEquals(touched.scrollY, vertical(touched).progress)
            }
        }
    }

    private fun prepare(scenario: ActivityScenario<LessonActivity>, overflow: Overflow) {
        scenario.onActivity { activity ->
            if (field(activity, "currentPage") != 1) {
                setField(activity, "currentPage", 1)
                invoke(activity, "renderCurrentPage")
            }
            val input = source(activity)
            input.showSoftInputOnFocus = false
            (activity.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager)
                .hideSoftInputFromWindow(input.windowToken, 0)
            input.requestFocus()
            input.setText(overflow.code())
            input.setSelection(0)
            input.suppressCursorAutoScroll = true
            input.scrollTo(0, 0)
        }
        await(scenario, "The actual editor exposes the requested $overflow ranges") { activity ->
            val input = source(activity)
            !input.isLayoutRequested && !(input.parent as View).isLayoutRequested &&
                input.layout?.lineCount == overflow.code().count { it == '\n' } + 1 &&
                (horizontal(input).max > 0) == overflow.x && (vertical(input).max > 0) == overflow.y
        }
        scenario.onActivity { activity ->
            val input = source(activity)
            val location = IntArray(2).also(input::getLocationOnScreen)
            val viewport = IntArray(2).also(scroll(activity)::getLocationOnScreen)
            scroll(activity).scrollBy(0, location[1] + input.height / 2 - viewport[1] - scroll(activity).height / 2)
        }
        await(scenario, "The complete editor must be visible before real touch") { activity ->
            val input = source(activity)
            val visible = Rect()
            input.getGlobalVisibleRect(visible) && visible.height() == input.height && visible.width() > input.width * .8f
        }
    }

    private fun emptyPoint(input: FreeScrollEditText, below: Boolean): PointF {
        val layout = requireNotNull(input.layout)
        val location = IntArray(2).also(input::getLocationOnScreen)
        val localX = input.width * .82f
        val localY = if (below) {
            val lastBottom = input.totalPaddingTop + layout.height - input.scrollY
            // TextView.totalPaddingBottom includes unused vertical gravity space;
            // compound padding describes the real editor viewport below the glyphs.
            val viewportBottom = input.height - input.compoundPaddingBottom
            assertTrue("Actual blank area required: height=${input.height}, layoutHeight=${layout.height}, " +
                "lastBottom=$lastBottom, viewportBottom=$viewportBottom, lineHeight=${input.lineHeight}, " +
                "compoundBottom=${input.compoundPaddingBottom}, totalBottom=${input.totalPaddingBottom}, scrollY=${input.scrollY}",
                lastBottom + input.lineHeight / 2f < viewportBottom)
            (lastBottom + viewportBottom) / 2f
        } else {
            val line = layout.getLineForVertical(input.scrollY + input.lineHeight / 2)
            assertTrue("The touch must begin beyond actual glyphs, including at the scroll edge",
                localX + input.scrollX - input.totalPaddingLeft > maxOf(layout.getLineLeft(line), layout.getLineRight(line)) + 2)
            input.totalPaddingTop + (layout.getLineTop(line) + layout.getLineBottom(line)) / 2f - input.scrollY
        }
        val result = PointF(location[0] + localX, location[1] + localY)
        val visible = Rect()
        assertTrue(input.getGlobalVisibleRect(visible) && visible.contains(result.x.toInt(), result.y.toInt()))
        return result
    }

    private fun withLesson(test: (ActivityScenario<LessonActivity>) -> Unit) {
        val id = "cpp_fundamentals__programas_y_ejecucion__empty_drag_${UUID.randomUUID().toString().replace("-", "")}"
        val fixture = JSONObject().put("id", id).put("title", "Isolated editor gestures")
            .put("pages", JSONArray().apply {
                repeat(3) { page -> put(JSONObject().put("blocks", JSONArray()
                    .put(JSONObject().put("type", "paragraph").put("text", "Gesture page $page"))
                    .put(JSONObject().put("type", "cpp_compiler").put("initialCode", Overflow.NONE.code())
                        .put("initialStandardInput", "fixture input"))
                    .put(JSONObject().put("type", "paragraph").put("text", "Additional lesson reading. ".repeat(240))))) }
            })
        ProgressRepository.saveCurrentPage(context, "programming", SOURCE, 0)
        val intent = Intent(context, LessonActivity::class.java).putExtra("routeId", "programming")
            .putExtra("courseId", "cpp_fundamentals").putExtra("unitId", "programas_y_ejecucion")
            .putExtra("lessonId", SOURCE).putExtra("lessonNumber", 1).putExtra("lessonTotal", 1)
        ActivityScenario.launch<LessonActivity>(intent).use { scenario ->
            await(scenario, "The original lesson finishes startup before one-time QA injection") { activity ->
                activity.hasWindowFocus() && (field(activity, "lessonJson") as? JSONObject)?.optString("id") == SOURCE
            }
            scenario.onActivity { activity ->
                activity.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                setField(activity, "currentLessonId", id)
                setField(activity, "lessonJson", fixture)
                setField(activity, "totalPages", 3)
                setField(activity, "currentPage", 1)
                setField(activity, "swipeNavigationEnabled", true)
                for (name in listOf("pageViewCache", "pageRequiredExercises", "pageCompletedExercises")) {
                    (field(activity, name) as MutableMap<*, *>).clear()
                }
                invoke(activity, "renderCurrentPage")
                LessonActivity::class.java.getDeclaredMethod("setCurrentCourseWrap", Boolean::class.javaPrimitiveType)
                    .apply { isAccessible = true }.invoke(activity, false)
            }
            instrumentation.waitForIdleSync()
            test(scenario)
        }
    }

    private fun touch(start: PointF, finish: PointF, held: Boolean, move: Boolean = true, cancel: Boolean = false) {
        val down = SystemClock.uptimeMillis()
        fun inject(action: Int, point: PointF) {
            val event = MotionEvent.obtain(down, SystemClock.uptimeMillis(), action, point.x, point.y, 0)
                .apply { source = InputDevice.SOURCE_TOUCHSCREEN }
            try { assertTrue("The real window receives touch", instrumentation.uiAutomation.injectInputEvent(event, true)) }
            finally { event.recycle() }
        }
        inject(MotionEvent.ACTION_DOWN, start)
        if (held) SystemClock.sleep(ViewConfiguration.getLongPressTimeout().toLong() + 120)
        if (move) {
            for (step in 1..10) {
                SystemClock.sleep(16)
                inject(MotionEvent.ACTION_MOVE, PointF(start.x + (finish.x - start.x) * step / 10,
                    start.y + (finish.y - start.y) * step / 10))
            }
            // End at rest so a parent fling cannot contaminate the next independent case.
            SystemClock.sleep(200)
            inject(MotionEvent.ACTION_MOVE, finish)
        }
        inject(if (cancel) MotionEvent.ACTION_CANCEL else MotionEvent.ACTION_UP, finish)
        instrumentation.waitForIdleSync()
    }

    private fun source(activity: LessonActivity) = tagged(activity, "cpp_code_input") as FreeScrollEditText
    private fun scroll(activity: LessonActivity) = activity.findViewById<ScrollView>(R.id.lessonScroll)
    private fun vertical(input: FreeScrollEditText) = descendants(input.parent as View).filterIsInstance<SeekBar>().single()
    private fun horizontal(input: FreeScrollEditText): SeekBar {
        val vertical = vertical(input)
        return descendants(input.parent.parent.parent as View).filterIsInstance<SeekBar>().single { it !== vertical }
    }
    private fun tagged(activity: LessonActivity, tag: String) = descendants(activity.window.decorView).single { it.tag == tag }
    private fun descendants(view: View): List<View> = buildList {
        add(view)
        if (view is ViewGroup) for (index in 0 until view.childCount) addAll(descendants(view.getChildAt(index)))
    }
    private fun field(activity: LessonActivity, name: String): Any? = LessonActivity::class.java
        .getDeclaredField(name).apply { isAccessible = true }.get(activity)
    private fun setField(activity: LessonActivity, name: String, value: Any) = LessonActivity::class.java
        .getDeclaredField(name).apply { isAccessible = true }.set(activity, value)
    private fun invoke(activity: LessonActivity, name: String) = LessonActivity::class.java
        .getDeclaredMethod(name).apply { isAccessible = true }.invoke(activity)
    private fun preferenceFiles() = File(context.applicationInfo.dataDir, "shared_prefs").listFiles().orEmpty()
        .filter { it.extension == "xml" }.map { it.nameWithoutExtension }.toSet()
    private fun await(scenario: ActivityScenario<LessonActivity>, label: String, ready: (LessonActivity) -> Boolean) {
        val deadline = SystemClock.uptimeMillis() + 12_000
        do {
            instrumentation.waitForIdleSync()
            var success = false
            scenario.onActivity { success = ready(it) }
            if (success) return
            SystemClock.sleep(50)
        } while (SystemClock.uptimeMillis() < deadline)
        fail(label)
    }

    companion object {
        private const val SOURCE = "cpp_fundamentals__programas_y_ejecucion__primer_programa_y_salida"
    }
}
