package com.learngraphicsprogramming.app.math

import android.graphics.Paint
import android.os.SystemClock
import android.view.MotionEvent
import android.view.View
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class MathParentBoundaryTouchDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private fun parse(source: String) = (MathLatexParser.parse(source) as MathLatexParser.Result.Success).expression
    private fun layout(expression: MathExpression.Sequence, size: Float = 32f) =
        MathExpressionLayout.create(expression, Paint(Paint.ANTI_ALIAS_FLAG), size, true)

    @Test fun horizontalSpaceAfterAllWrittenContentAlwaysFindsTheLastRootParentAtEveryHeight() {
        val sources = listOf("x^{2}", "12", "\\frac{1}{2}", "\\sqrt[3]{x}",
            "\\int_{0}^{1}{x}+x_{1}^{2}", "\\begin{matrix}1&2\\\\3&4\\end{matrix}+x^{2}")
        for (source in sources) {
            val expression = parse(source)
            val layout = layout(expression)
            val written = requireNotNull(layout.sequenceBounds(emptyList()))
            val expected = MathCursor(offset = expression.children.size)
            val xs = listOf(written.right + .01f, written.right + 10f, written.right + 1_000f)
            val ys = listOf(written.top - 1_000f, written.top - 1f, written.centerY(), written.bottom + 1f, written.bottom + 1_000f)
            for (x in xs) for (y in ys) {
                assertEquals("$source at ($x,$y)", expected, layout.hitTest(x, y))
                val target = layout.hitTestForEditing(x, y)
                assertEquals("Editing hit-test follows the same horizontal exterior rule", expected, target.cursor)
                assertNull("The parent edge never fabricates an interior text offset", target.leafTextOffset)
            }
        }
    }

    @Test fun emptyLayoutHasOnlyHomeForEveryTouchPositionAndTextScale() {
        for (size in listOf(16f, 32f, 48f)) {
            val layout = layout(MathExpression.Sequence(), size)
            assertEquals(0f, layout.width, .001f)
            for (x in listOf(-1_000f, -1f, 0f, 1f, 1_000f)) for (y in listOf(-1_000f, 0f, 1_000f)) {
                assertEquals(MathCursor(), layout.hitTest(x, y))
                assertEquals(MathExpressionTouchTarget(MathCursor()), layout.hitTestForEditing(x, y))
            }
        }
    }

    @Test fun editorTapAfterWrittenContentPreservesTheLastParentAndWholeParentBackspace() = onMain {
        val source = "\\frac{1}{2}+x_{1}^{2}"
        for (relativeY in listOf(-1_000f, -80f, 0f, 80f, 1_000f)) {
            val editor = editor(source)
            val original = editor.model.expression
            val lower = listOf(MathCursorStep(2, "lower"))
            editor.model.cursor = MathCursor(lower, 1)
            editor.model.select(lower, 0, 1)
            val written = requireNotNull(editor.expressionLayout.sequenceBounds(emptyList()))
            assertTrue("The field's empty area is larger than the written content", editor.width > editor.expressionOriginX + written.right + 10f)
            tap(editor, editor.expressionOriginX + written.right + 10f, editor.expressionBaseline + relativeY)
            assertSame("Touch does not mutate the AST", original, editor.model.expression)
            assertEquals(MathCursor(offset = 3), editor.model.cursor)
            assertEquals(MathCursorLevel.NEUTRAL, editor.model.navigationLevel)
            assertEquals(MathCursorPresentation.ParentBoundary(emptyList(), 2), editor.cursorPresentation)
            assertNull(editor.model.selectionRange)
            assertNull(editor.model.editingScope)
            editor.perform(MathKeyboardAction.Backspace)
            assertEquals("Exterior Backspace removes the entire final parent, including both scripts",
                MathExpression.Sequence(original.children.dropLast(1)), editor.model.expression)
        }
    }

    @Test fun emptyEditorTapsStayAtHomeAndNumericInkStillEntersItsChild() = onMain {
        for (x in listOf(-100f, 0f, 250f, 1_000f)) for (y in listOf(-1_000f, 0f, 1_000f)) {
            val editor = editor("")
            val original = editor.model.expression
            tap(editor, x, y)
            assertEquals(MathCursor(), editor.model.cursor)
            assertEquals(MathCursorPresentation.Neutral, editor.cursorPresentation)
            assertNull(editor.model.editingScope)
            editor.perform(MathKeyboardAction.Backspace)
            assertSame("Empty HOME Backspace leaves the expression untouched", original, editor.model.expression)
        }
        val editor = editor("x^{12}")
        val upper = listOf(MathCursorStep(0, "upper"))
        val glyph = requireNotNull(editor.expressionLayout.textCaretBounds(MathCursor(upper, 0), 1))
        tap(editor, editor.expressionOriginX + glyph.left, editor.expressionBaseline + glyph.centerY())
        assertEquals(upper, editor.model.cursor.path)
        assertEquals(MathCursorLevel.INTERIOR, editor.model.navigationLevel)
        assertNull(editor.model.selectionRange)
        editor.perform(MathKeyboardAction.Backspace)
        assertEquals("Touching the numeric ink retains exact character editing", parse("x^{2}"), editor.model.expression)
    }

    private fun editor(source: String) = MathExpressionEditor(context).also {
        assertTrue(it.setLatex(source))
        it.measure(View.MeasureSpec.makeMeasureSpec(800, View.MeasureSpec.EXACTLY),
            View.MeasureSpec.makeMeasureSpec(300, View.MeasureSpec.EXACTLY))
        it.layout(0, 0, it.measuredWidth, it.measuredHeight)
    }

    private fun tap(editor: MathExpressionEditor, x: Float, y: Float) {
        val now = SystemClock.uptimeMillis()
        val down = MotionEvent.obtain(now, now, MotionEvent.ACTION_DOWN, x, y, 0)
        val up = MotionEvent.obtain(now, now + 20, MotionEvent.ACTION_UP, x, y, 0)
        try { editor.dispatchTouchEvent(down); editor.dispatchTouchEvent(up) } finally { down.recycle(); up.recycle() }
    }

    private fun onMain(block: () -> Unit) = instrumentation.runOnMainSync(block)
}
