package com.learngraphicsprogramming.app.math

import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.Rect
import android.os.ParcelFileDescriptor
import android.os.SystemClock
import android.view.View
import android.view.ViewGroup
import android.view.inputmethod.InputMethodManager
import android.widget.Button
import android.widget.EditText
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.LessonActivity
import com.learngraphicsprogramming.app.LessonTextSizeSettingsRepository
import com.learngraphicsprogramming.app.ProgressRepository
import com.learngraphicsprogramming.app.R
import org.json.JSONArray
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/** Verifies the actual IME window and the measured toolbar, not a simulated keyboard rectangle. */
@RunWith(AndroidJUnit4::class)
class MathKeyboardImeDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val lesson = "math_course_01__arithmetic_fundamentals__math_puzzle_sandbox"
    private val snapshots = linkedMapOf<String, Map<String, *>>()

    @Before fun prepare() {
        for (command in listOf("input keyevent KEYCODE_WAKEUP", "wm dismiss-keyguard")) {
            ParcelFileDescriptor.AutoCloseInputStream(instrumentation.uiAutomation.executeShellCommand(command)).use { it.readBytes() }
        }
        listOf("learning_progress", "navigation_state", "lesson_text_size_settings").forEach {
            snapshots[it] = context.getSharedPreferences(it, Context.MODE_PRIVATE).all.toMap()
        }
        ProgressRepository.resetLesson(context, "math_physics", lesson)
        LessonTextSizeSettingsRepository.saveUsesGlobalScale(context, "math_course_01", true)
        LessonTextSizeSettingsRepository.saveGlobalScale(context, 100)
    }

    @After fun restore() {
        snapshots.forEach { (name, original) ->
            val preferences = context.getSharedPreferences(name, Context.MODE_PRIVATE)
            val editor = preferences.edit().clear()
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

    @Test fun toolbarFloatsAboveActualImeInputStaysVisibleAndButtonsAreSquare() {
        val intent = Intent(context, LessonActivity::class.java).putExtra("routeId", "math_physics")
            .putExtra("courseId", "math_course_01").putExtra("lessonId", lesson).putExtra("lessonNumber", 4)
        ActivityScenario.launch<LessonActivity>(intent).use { scenario ->
            lateinit var input: EditText
            lateinit var toolbar: View
            scenario.onActivity { activity ->
                val views = tree(activity.findViewById<View>(R.id.lessonContent))
                input = views.filterIsInstance<EditText>().last()
                // Retain the real View before it moves to the non-focusable popup window.
                toolbar = views.filterIsInstance<MathKeyboardView>().last()
                input.setText("12")
                input.requestFocus()
                (activity.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager)
                    .showSoftInput(input, InputMethodManager.SHOW_IMPLICIT)
            }
            var shown = false
            repeat(15) {
                if (!shown) {
                    SystemClock.sleep(200)
                    scenario.onActivity { activity ->
                        shown = ViewCompat.getRootWindowInsets(activity.window.decorView)
                            ?.isVisible(WindowInsetsCompat.Type.ime()) == true
                        if (!shown && activity.hasWindowFocus()) {
                            input.requestFocus()
                            (activity.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager)
                                .showSoftInput(input, InputMethodManager.SHOW_IMPLICIT)
                        }
                    }
                }
            }
            assertTrue("El teclado Android real debe abrirse", shown)
            instrumentation.waitForIdleSync()
            SystemClock.sleep(300)
            screenshot("sandbox-keyboard-ime-before-assert.png")
            scenario.onActivity { activity ->
                assertTrue(toolbar.isAttachedToWindow)
                assertEquals(View.VISIBLE, toolbar.visibility)
                val frame = Rect()
                activity.window.decorView.getWindowVisibleDisplayFrame(frame)
                val barBounds = Rect()
                val inputBounds = Rect()
                assertTrue(toolbar.getGlobalVisibleRect(barBounds))
                assertTrue(input.getGlobalVisibleRect(inputBounds))
                assertEquals("Campo completo visible", input.height, inputBounds.height())
                // Global visible rectangles are relative to each window. The popup and Activity
                // have different origins; compare their physical screen coordinates to the IME.
                val location = IntArray(2)
                toolbar.getLocationOnScreen(location)
                barBounds.set(location[0], location[1], location[0] + toolbar.width, location[1] + toolbar.height)
                input.getLocationOnScreen(location)
                inputBounds.set(location[0], location[1], location[0] + input.width, location[1] + input.height)
                assertEquals("La barra termina inmediatamente sobre IME", frame.bottom.toDouble(), barBounds.bottom.toDouble(), 3.0)
                assertTrue("Input elevado por la altura real de la barra", inputBounds.bottom <= barBounds.top - 4 * input.resources.displayMetrics.density)
                val keys = tree(toolbar).filterIsInstance<Button>()
                assertTrue(keys.isNotEmpty())
                assertTrue(keys.all { kotlin.math.abs(it.width - it.height) <= 2 })
                assertFalse(keys.any { it.text.toString().matches(Regex("[0-9]")) || it.text.toString() in listOf("π", "%", "≤", "≥") })
                assertEquals(0, input.selectionStart)
                assertEquals(input.text.length, input.selectionEnd)
            }
            screenshot("sandbox-keyboard-ime.png")
            scenario.onActivity { input.onEditorAction(android.view.inputmethod.EditorInfo.IME_ACTION_DONE) }
            SystemClock.sleep(500)
            instrumentation.waitForIdleSync()
            scenario.onActivity { activity ->
                val header = activity.findViewById<View>(R.id.textTopLesson).parent.parent as View
                assertEquals("La cabecera vuelve al terminar la edición", View.VISIBLE, header.visibility)
                assertFalse(input.hasFocus())
            }
        }
    }

    @Test fun declaredKeyboardSymbolsFilterIrrelevantKeysAndDigits() {
        instrumentation.runOnMainSync {
            val bar = MathKeyboardView(context, 0xff66cc55.toInt())
            bar.bind(EditText(context), "ALGEBRA", JSONArray("[\"+\",\"−\",\"×\",\"÷\",\"(\",\")\",\"5\",\"unknown\"]"))
            assertEquals(listOf("+", "−", "×", "÷", "(", ")", "⌫"), tree(bar).filterIsInstance<Button>().map { it.text.toString() })
        }
    }

    private fun tree(view: View): List<View> = listOf(view) + if (view is ViewGroup)
        (0 until view.childCount).flatMap { tree(view.getChildAt(it)) } else emptyList()

    private fun screenshot(name: String) {
        SystemClock.sleep(500)
        val bitmap = instrumentation.uiAutomation.takeScreenshot() ?: error("No se pudo capturar IME")
        if (android.os.Build.VERSION.SDK_INT >= 31) {
            ParcelFileDescriptor.AutoCloseInputStream(instrumentation.uiAutomation.executeShellCommand("mkdir -p /data/local/tmp/math-puzzle-smoke")).use { it.readBytes() }
            val streams = instrumentation.uiAutomation.executeShellCommandRw("dd of=/data/local/tmp/math-puzzle-smoke/$name")
            ParcelFileDescriptor.AutoCloseOutputStream(streams[1]).use { assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) }
            ParcelFileDescriptor.AutoCloseInputStream(streams[0]).use { it.readBytes() }
        }
        bitmap.recycle()
    }
}
