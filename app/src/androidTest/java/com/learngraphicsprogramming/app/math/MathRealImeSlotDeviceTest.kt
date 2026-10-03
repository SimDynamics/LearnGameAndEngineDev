package com.learngraphicsprogramming.app.math

import android.accessibilityservice.AccessibilityServiceInfo
import android.content.Context
import android.content.Intent
import android.content.pm.ActivityInfo
import android.content.res.Configuration
import android.graphics.Rect
import android.os.Parcel
import android.os.ParcelFileDescriptor
import android.os.SystemClock
import android.provider.Settings
import android.view.InputDevice
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.view.accessibility.AccessibilityNodeInfo
import android.view.accessibility.AccessibilityWindowInfo
import android.view.inputmethod.InputMethodManager
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.LessonActivity
import com.learngraphicsprogramming.app.LessonTextSizeSettingsRepository
import com.learngraphicsprogramming.app.ProgressRepository
import com.learngraphicsprogramming.app.R
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/** Screen taps on the installed Android keyboard exercise its actual composition protocol. */
@RunWith(AndroidJUnit4::class)
class MathRealImeSlotDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val automation get() = instrumentation.uiAutomation
    private val route = "math_physics"
    private val lesson = "math_course_01__arithmetic_fundamentals__math_puzzle_sandbox"
    private val snapshots = linkedMapOf<String, Map<String, *>>()
    private var originalServiceInfo: AccessibilityServiceInfo? = null

    @Before fun prepare() {
        for (command in listOf("input keyevent KEYCODE_WAKEUP", "wm dismiss-keyguard")) {
            ParcelFileDescriptor.AutoCloseInputStream(automation.executeShellCommand(command)).use { it.readBytes() }
        }
        listOf("learning_progress", "navigation_state", "lesson_text_size_settings").forEach {
            snapshots[it] = context.getSharedPreferences(it, Context.MODE_PRIVATE).all.toMap()
        }
        ProgressRepository.resetLesson(context, route, lesson)
        LessonTextSizeSettingsRepository.saveUsesGlobalScale(context, "math_course_01", true)
        LessonTextSizeSettingsRepository.saveGlobalScale(context, 100)
        originalServiceInfo = automation.serviceInfo
        val parcel = Parcel.obtain()
        try {
            requireNotNull(originalServiceInfo).writeToParcel(parcel, 0)
            parcel.setDataPosition(0)
            val updated = AccessibilityServiceInfo.CREATOR.createFromParcel(parcel)
            updated.flags = updated.flags or AccessibilityServiceInfo.FLAG_RETRIEVE_INTERACTIVE_WINDOWS
            automation.serviceInfo = updated
        } finally {
            parcel.recycle()
        }
    }

    @After fun restoreUserState() {
        originalServiceInfo?.let { automation.serviceInfo = it }
        snapshots.forEach { (name, original) ->
            val editor = context.getSharedPreferences(name, Context.MODE_PRIVATE).edit().clear()
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

    @Test fun installedImeTypesExactlyOnceIntoRepeatedlyTouchedLogarithmSlots() {
        ActivityScenario.launch<LessonActivity>(intent()).use { scenario ->
            scenario.onActivity { it.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_PORTRAIT }
            await(scenario, "The lesson reaches portrait orientation with window focus") {
                it.resources.configuration.orientation == Configuration.ORIENTATION_PORTRAIT && it.hasWindowFocus()
            }
            lateinit var editor: MathExpressionEditor
            lateinit var control: MathResponseControl
            scenario.onActivity { activity ->
                control = requireNotNull(MathInputControls.create(activity,
                    MathResponseSpec("real_ime_log", "math_expression_input",
                        JSONObject().put("inputMode", "structured").put("label", "Logaritmo"),
                        JSONObject().put("type", "latex").put("expected", "\\log_{23}{x1}")),
                    0xffa9e96f.toInt()) {})
                activity.findViewById<ViewGroup>(R.id.lessonContent).apply {
                    removeAllViews()
                    addView(control.view)
                }
                MathInputFocusOrder.configure(control.view)
                editor = tree(control.view).filterIsInstance<MathExpressionEditor>().single()
                editor.perform(MathKeyboardAction.Function("log"))
                assertEquals("base", editor.model.cursor.path.last().slot)
                assertTrue(editor.requestFocus())
                (activity.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager)
                    .showSoftInput(editor, InputMethodManager.SHOW_IMPLICIT)
            }
            await(scenario, "The installed Android IME opens for the structured editor") { activity ->
                if (activity.hasWindowFocus() && ViewCompat.getRootWindowInsets(activity.window.decorView)
                        ?.isVisible(WindowInsetsCompat.Type.ime()) != true) {
                    (activity.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager)
                        .showSoftInput(editor, InputMethodManager.SHOW_IMPLICIT)
                }
                activity.hasWindowFocus() && editor.hasFocus() && ViewCompat.getRootWindowInsets(activity.window.decorView)
                    ?.isVisible(WindowInsetsCompat.Type.ime()) == true
            }

            tapImeKey("2")
            awaitLatex(scenario, editor, "\\log_{2}{}")
            tapSlot(scenario, editor, "argument", 0)
            tapImeKey("x")
            awaitLatex(scenario, editor, "\\log_{2}{x}")
            tapSlot(scenario, editor, "base", 1)
            tapImeKey("3")
            awaitLatex(scenario, editor, "\\log_{23}{x}")
            tapSlot(scenario, editor, "argument", 1)
            tapImeKey("1")
            awaitLatex(scenario, editor, "\\log_{23}{x1}")
            scenario.onActivity { activity ->
                assertTrue("IME input never transfers focus to another control", editor.hasFocus())
                assertTrue(activity.hasWindowFocus())
                assertEquals(MathAnswer.Text("\\log_{23}{x1}"), control.answer())
                assertTrue(MathValidators.validate(JSONObject().put("type", "latex")
                    .put("expected", "\\log_{23}{x1}"), control.answer()).correct)
            }
        }
    }

    private fun tapSlot(scenario: ActivityScenario<LessonActivity>, editor: MathExpressionEditor, slot: String, offset: Int) {
        val target = MathCursor(listOf(MathCursorStep(0, slot)), offset)
        var x = 0f
        var y = 0f
        scenario.onActivity {
            val caret = requireNotNull(editor.expressionLayout.caretBounds(target))
            val location = IntArray(2).also(editor::getLocationOnScreen)
            x = location[0] + editor.expressionOriginX + caret.centerX()
            y = location[1] + editor.expressionBaseline + caret.centerY()
            val visible = Rect()
            assertTrue("Selected structural slot is visible above the IME", editor.getGlobalVisibleRect(visible))
            assertTrue("A screen tap must land on the visible editor", visible.contains(x.toInt(), y.toInt()))
        }
        tap(x, y)
        await(scenario, "A touch selects $slot at offset $offset") { editor.hasFocus() && editor.model.cursor == target }
    }

    private data class ImeNode(val text: String, val description: String, val bounds: Rect, val clickable: Boolean)

    private fun imeNodes(): List<ImeNode> {
        val result = mutableListOf<ImeNode>()
        fun collect(node: AccessibilityNodeInfo) {
            if (node.isVisibleToUser) {
                val bounds = Rect().also(node::getBoundsInScreen)
                if (!bounds.isEmpty) result += ImeNode(node.text?.toString().orEmpty(),
                    node.contentDescription?.toString().orEmpty(), bounds, node.isClickable)
            }
            for (index in 0 until node.childCount) {
                val child = node.getChild(index) ?: continue
                try { collect(child) } finally { child.recycle() }
            }
        }
        automation.windows.filter { it.type == AccessibilityWindowInfo.TYPE_INPUT_METHOD }.forEach { window ->
            val root = window.root ?: return@forEach
            try { collect(root) } finally { root.recycle() }
        }
        return result
    }

    /** Keys are resolved from the live IME tree; no hardware/input-text substitute is used. */
    private fun tapImeKey(key: String) {
        var nodes = emptyList<ImeNode>()
        var switched = false
        repeat(20) {
            nodes = imeNodes()
            val match = nodes.filter { it.text == key || it.description == key }
                .sortedByDescending { it.clickable }.firstOrNull()
            if (match != null) {
                tap(match.bounds.exactCenterX(), match.bounds.exactCenterY())
                return
            }
            if (!switched && it >= 3) {
                val modeLabels = if (key.single().isDigit())
                    setOf("123", "?123", "? 123", "Símbolos", "Números y símbolos", "Symbols", "Numbers and symbols")
                else setOf("ABC", "abc", "Letras", "Letters", "Alfabeto")
                val mode = nodes.firstOrNull { it.text in modeLabels || it.description in modeLabels }
                if (mode != null) {
                    tap(mode.bounds.exactCenterX(), mode.bounds.exactCenterY())
                    switched = true
                }
            }
            SystemClock.sleep(100)
        }
        val installedIme = Settings.Secure.getString(context.contentResolver, Settings.Secure.DEFAULT_INPUT_METHOD)
        fail("The installed IME ($installedIme) does not expose a visible accessible '$key' key. " +
            "This real-IME test requires accessible keyboard keys; labels: " +
            nodes.map { it.text.ifEmpty { it.description } }.filter { it.isNotBlank() }.distinct().joinToString())
    }

    private fun tap(x: Float, y: Float) {
        val downTime = SystemClock.uptimeMillis()
        val down = MotionEvent.obtain(downTime, downTime, MotionEvent.ACTION_DOWN, x, y, 0)
        down.source = InputDevice.SOURCE_TOUCHSCREEN
        try { assertTrue(automation.injectInputEvent(down, true)) } finally { down.recycle() }
        SystemClock.sleep(45)
        val up = MotionEvent.obtain(downTime, SystemClock.uptimeMillis(), MotionEvent.ACTION_UP, x, y, 0)
        up.source = InputDevice.SOURCE_TOUCHSCREEN
        try { assertTrue(automation.injectInputEvent(up, true)) } finally { up.recycle() }
        instrumentation.waitForIdleSync()
    }

    private fun awaitLatex(scenario: ActivityScenario<LessonActivity>, editor: MathExpressionEditor, expected: String) {
        await(scenario, "The actual IME produces $expected exactly once") { editor.getLatex() == expected && editor.hasFocus() }
        scenario.onActivity { assertEquals(expected, editor.getLatex()) }
    }

    private fun await(scenario: ActivityScenario<LessonActivity>, message: String, condition: (LessonActivity) -> Boolean) {
        var ready = false
        repeat(40) {
            if (!ready) {
                SystemClock.sleep(100)
                scenario.onActivity { ready = condition(it) }
            }
        }
        assertTrue(message, ready)
    }

    private fun tree(view: View): List<View> = listOf(view) + if (view is ViewGroup)
        (0 until view.childCount).flatMap { tree(view.getChildAt(it)) } else emptyList()

    private fun intent() = Intent(context, LessonActivity::class.java)
        .putExtra("routeId", route).putExtra("courseId", "math_course_01")
        .putExtra("unitId", "arithmetic_fundamentals").putExtra("lessonId", lesson).putExtra("lessonNumber", 4)
}
