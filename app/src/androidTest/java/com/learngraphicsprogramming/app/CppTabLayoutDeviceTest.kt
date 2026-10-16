package com.learngraphicsprogramming.app

import android.graphics.Color
import android.graphics.Typeface
import android.text.InputType
import android.text.Spanned
import android.text.style.ForegroundColorSpan
import android.util.TypedValue
import android.view.ContextThemeWrapper
import android.view.Gravity
import android.view.View
import android.widget.FrameLayout
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

/** Native Android Layout and source offsets, without substituting spaces for TAB characters. */
@RunWith(AndroidJUnit4::class)
class CppTabLayoutDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()

    @Test fun realTabsUseFourColumnStopsIncludingAfterExactBoundariesAtEveryFontSize() = onMain {
        val input = editor()
        val source = "abcd\tX\n\tabcd\tX\n\t\t\t \tX"
        input.setText(source)
        for (size in listOf(12f, 18f, 24f, 31f)) {
            input.setTextSize(TypedValue.COMPLEX_UNIT_PX, size)
            settle(input)
            val cell = input.paint.measureText(" ")
            val layout = requireNotNull(input.layout)
            val expected = listOf(8, 4, 12, 4, 8, 12, 16)
            val offsets = source.indices.filter { source[it] == '\t' }
            assertEquals(expected.size, offsets.size)
            offsets.zip(expected).forEach { (offset, column) ->
                assertEquals("TAB after offset $offset at ${size}px", column * cell,
                    layout.getPrimaryHorizontal(offset + 1), 1.1f)
            }
            assertEquals(source, input.text.toString())
        }
    }

    @Test fun wrappedLateTabsKeepTheOriginalSourceInsideTheNativeViewport() = onMain {
        val input = editor()
        input.setTextSize(TypedValue.COMPLEX_UNIT_PX, 24f)
        input.setHorizontallyScrolling(false)
        val source = "\tint value = 1; // " + "long_token ".repeat(32) + "\tX\n\t\treturn 0;"
        input.setText(source)
        settle(input, width = 280)
        val layout = requireNotNull(input.layout)
        assertTrue(layout.lineCount > 3)
        source.indices.filter { source[it] == '\t' }.forEach { offset ->
            val position = layout.getPrimaryHorizontal(offset + 1)
            assertTrue("Soft-wrap cannot expand a late TAB beyond its viewport: $position",
                position >= 0f && position <= layout.width + 1f)
        }
        assertEquals(source, input.text.toString())
    }

    @Test fun tabGeometryPreservesColorsSelectionAndEditsAcrossParagraphChanges() = onMain {
        val input = editor()
        val source = "int\tvalue;\n\treturn 0;"
        input.setText(source)
        val editable = requireNotNull(input.text)
        val color = ForegroundColorSpan(Color.CYAN)
        editable.setSpan(color, 0, 3, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        input.setSelection(1, 6)
        settle(input)
        input.setTextSize(TypedValue.COMPLEX_UNIT_PX, 27f)
        settle(input)
        assertSame(editable, input.text)
        assertEquals(0, editable.getSpanStart(color))
        assertEquals(3, editable.getSpanEnd(color))
        assertEquals(1, input.selectionStart)
        assertEquals(6, input.selectionEnd)
        assertEquals(source, editable.toString())
        editable.insert(0, "\t\n")
        settle(input)
        assertEquals("\t\n$source", editable.toString())
        assertEquals(2, editable.getSpanStart(color))
        assertEquals(5, editable.getSpanEnd(color))
        assertEquals(3, input.selectionStart)
        assertEquals(8, input.selectionEnd)
        editable.clear()
        settle(input)
        assertEquals("", editable.toString())
    }

    private fun editor(): FreeScrollEditText {
        val context = ContextThemeWrapper(instrumentation.targetContext,
            instrumentation.targetContext.applicationInfo.theme)
        val input = FreeScrollEditText(context).apply {
            typeface = Typeface.MONOSPACE
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_MULTI_LINE or InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
            gravity = Gravity.TOP or Gravity.START
            setPadding(0, 0, 0, 0)
            setHorizontallyScrolling(true)
        }
        // Mirror the real editor host. TextView's native edit/resize path needs
        // parent LayoutParams after its initial Layout has already been measured.
        FrameLayout(context).addView(input, FrameLayout.LayoutParams(
            FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT))
        return input
    }

    private fun settle(input: FreeScrollEditText, width: Int = 700) {
        val host = input.parent as FrameLayout
        repeat(3) {
            host.measure(View.MeasureSpec.makeMeasureSpec(width, View.MeasureSpec.EXACTLY),
                View.MeasureSpec.makeMeasureSpec(700, View.MeasureSpec.EXACTLY))
            host.layout(0, 0, host.measuredWidth, host.measuredHeight)
        }
    }

    private fun onMain(action: () -> Unit) {
        assertEquals("Use the isolated QA application", "com.learngraphicsprogramming.app.realdownloadsqa", BuildConfig.APPLICATION_ID)
        instrumentation.runOnMainSync(action)
    }
}
