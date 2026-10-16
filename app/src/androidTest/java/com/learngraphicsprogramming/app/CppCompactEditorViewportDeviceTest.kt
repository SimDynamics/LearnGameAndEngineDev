package com.learngraphicsprogramming.app

import android.graphics.Typeface
import android.text.InputType
import android.view.ContextThemeWrapper
import android.view.Gravity
import android.view.View
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.TextView
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

/** Actual Android Layout measurements; the fixture never opens a compiler or changes a draft. */
@RunWith(AndroidJUnit4::class)
class CppCompactEditorViewportDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val app = instrumentation.targetContext

    @Test fun lessonRowsFitContentAndReuseAnExistingTrailingEditingRow() = onMain {
        val fixture = Fixture(compact = true)
        try {
            fixture.source.setText("int valeur = 1;")
            fixture.settle()
            fixture.assertRows(4)
            assertEquals(0, fixture.vertical.max)
            val eight = (1..8).joinToString("\n") { "// ligne $it" }
            fixture.source.setText(eight)
            fixture.source.setSelection(3, 6)
            fixture.settle()
            assertEquals(8, fixture.source.layout.lineCount)
            fixture.assertRows(9)
            assertEquals(eight, fixture.source.text.toString())
            assertEquals(3, fixture.source.selectionStart)
            assertEquals(6, fixture.source.selectionEnd)
            assertEquals(0, fixture.vertical.max)
            fixture.source.setText("$eight\n")
            fixture.settle()
            assertEquals(9, fixture.source.layout.lineCount)
            fixture.assertRows(9)
            assertEquals("$eight\n", fixture.source.text.toString())
        } finally { fixture.controller.dispose() }
    }

    @Test fun actualWrappedRowsAndLongSourcesReachTheExistingCapAndKeepNativePanning() = onMain {
        val fixture = Fixture(compact = true)
        try {
            val longLine = "int valeur = 1; // " + "mot_technique ".repeat(40)
            fixture.source.setText(longLine)
            fixture.source.setSelection(longLine.length)
            fixture.controller.setWrapEnabled(false)
            fixture.settle()
            assertEquals(1, fixture.source.layout.lineCount)
            fixture.assertRows(4)
            assertTrue(fixture.horizontal.max > 0)
            fixture.controller.setWrapEnabled(true)
            fixture.settle()
            assertTrue("Use real visual continuation rows rather than newline count", fixture.source.layout.lineCount > 10)
            fixture.assertRows(10)
            assertEquals(0, fixture.horizontal.max)
            assertTrue(fixture.vertical.max > 0)
            fixture.source.scrollTo(0, fixture.vertical.max)
            fixture.controller.updateVerticalBar()
            assertEquals(fixture.vertical.max, fixture.source.scrollY)
            assertEquals(fixture.source.scrollY, fixture.vertical.progress)
            assertEquals(longLine, fixture.source.text.toString())
            assertEquals(longLine.length, fixture.source.selectionEnd)

            fixture.source.setText("int court = 1;")
            fixture.settle()
            fixture.assertRows(4)
            assertEquals(0, fixture.vertical.max)
            assertEquals(0, fixture.source.scrollY)
        } finally { fixture.controller.dispose() }
    }

    @Test fun theDefaultMainLabAndFullscreenKeepTheirExistingHeightPolicies() = onMain {
        val mainLab = Fixture(compact = false)
        val lesson = Fixture(compact = true)
        try {
            mainLab.source.setText("int valeur = 1;")
            mainLab.settle()
            mainLab.assertRows(10)
            lesson.source.setText("int valeur = 1;")
            lesson.settle()
            lesson.assertRows(4)
            lesson.fullscreen = true
            val fullscreenHeight = lesson.dp(420)
            lesson.body.layoutParams = lesson.body.layoutParams.apply { height = fullscreenHeight }
            lesson.settle()
            assertEquals("The fullscreen owner retains its exact viewport height", fullscreenHeight, lesson.body.height)
            lesson.fullscreen = false
            lesson.settle()
            lesson.assertRows(4)
        } finally {
            mainLab.controller.dispose()
            lesson.controller.dispose()
        }
    }

    private inner class Fixture(compact: Boolean) {
        private val context = ContextThemeWrapper(app, app.applicationInfo.theme)
        private val root = LinearLayout(context).apply { orientation = LinearLayout.VERTICAL }
        val body = LinearLayout(context).apply {
            orientation = LinearLayout.HORIZONTAL
            isBaselineAligned = false
            layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT)
        }
        private val numbers = TextView(context).apply {
            text = "1"
            textSize = 18f
            typeface = Typeface.MONOSPACE
            includeFontPadding = false
            setPadding(0, dp(16), dp(3), dp(16))
        }
        private val gutter = ScrollView(context).apply {
            layoutParams = LinearLayout.LayoutParams(dp(18), LinearLayout.LayoutParams.MATCH_PARENT)
            addView(numbers, FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.WRAP_CONTENT))
        }
        val source = FreeScrollEditText(context).apply {
            textSize = 18f
            typeface = Typeface.MONOSPACE
            includeFontPadding = false
            gravity = Gravity.TOP or Gravity.START
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_MULTI_LINE or InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
            setPadding(dp(10), dp(16), dp(16), dp(16))
            // Mirror the lesson's legacy minimum; the exact body viewport must still win.
            minHeight = dp(220)
            setHorizontallyScrolling(true)
        }
        val vertical = SeekBar(context)
        val horizontal = SeekBar(context)
        var fullscreen = false
        val controller: CppEditorViewportController

        init {
            val frame = FrameLayout(context).apply {
                layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, 1f)
                addView(source, FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT))
                addView(vertical, FrameLayout.LayoutParams(dp(28), dp(100), Gravity.END))
            }
            body.addView(gutter)
            body.addView(frame)
            root.addView(body)
            root.addView(horizontal, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, dp(28)))
            controller = CppEditorViewportController(source, numbers, gutter, body, vertical, horizontal,
                isFullscreen = { fullscreen }, isKeyboardVisible = { false }, compactNormalContent = compact)
        }

        fun settle() {
            repeat(4) {
                measure()
                controller.updateNormalHeight()
                measure()
                controller.updateHorizontalBar()
                controller.updateVerticalBar()
            }
        }

        fun assertRows(rows: Int) {
            assertEquals(source.compoundPaddingTop + source.lineHeight * rows + source.compoundPaddingBottom, body.height)
            assertEquals(body.height, source.height)
        }

        private fun measure() {
            root.measure(View.MeasureSpec.makeMeasureSpec(dp(340), View.MeasureSpec.EXACTLY),
                View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED))
            root.layout(0, 0, root.measuredWidth, root.measuredHeight)
        }

        fun dp(value: Int) = (value * app.resources.displayMetrics.density).toInt()
    }

    private fun onMain(action: () -> Unit) {
        assertEquals("Only the isolated QA application may run the fixture", "com.learngraphicsprogramming.app.realdownloadsqa", BuildConfig.APPLICATION_ID)
        instrumentation.runOnMainSync(action)
    }
}
