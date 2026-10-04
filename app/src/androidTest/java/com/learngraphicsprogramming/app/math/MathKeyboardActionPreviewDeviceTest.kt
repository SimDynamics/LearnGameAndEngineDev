package com.learngraphicsprogramming.app.math

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.EditText
import android.widget.ImageButton
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.json.JSONArray
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

/** Exercises actual toolbar actions and Canvas previews; no separate formula rendering fixture. */
@RunWith(AndroidJUnit4::class)
class MathKeyboardActionPreviewDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val accent = Color.rgb(169, 233, 111)

    @Test fun structuralPreviewsExposeRealEmptySlotsAndDrawWithTheSharedLayout() = onMain {
        val editor = MathExpressionEditor(context)
        val toolbar = MathKeyboardView(context, accent)
        toolbar.bind(editor, listOf("FRACTION", "LIMIT", "MATRIX", "→", "+"))
        measure(toolbar)
        val fraction = preview(toolbar, "Insertar fracción")
        val fractionNode = fraction.previewExpression.children.single() as MathExpression.Fraction
        assertTrue(fractionNode.numerator.children.isEmpty())
        assertTrue(fractionNode.denominator.children.isEmpty())
        val numerator = requireNotNull(fraction.previewLayout.caretBounds(slot("numerator")))
        val denominator = requireNotNull(fraction.previewLayout.caretBounds(slot("denominator")))
        assertTrue("Fraction preview has separate stacked numerator and denominator", numerator.bottom <= denominator.top)

        val limit = preview(toolbar, "Insertar límite")
        val limitNode = limit.previewExpression.children.single() as MathExpression.Limit
        assertTrue(limitNode.condition.children.isEmpty())
        assertTrue(limitNode.body.children.isEmpty())
        assertNotNull("Limit condition uses the shared layout", limit.previewLayout.caretBounds(slot("condition")))

        val matrix = preview(toolbar, "Insertar matriz 2 por 2")
        val matrixNode = matrix.previewExpression.children.single() as MathExpression.Matrix
        assertEquals(2, matrixNode.rows.size)
        assertTrue(matrixNode.rows.all { it.size == 2 && it.all { cell -> cell.children.isEmpty() } })
        val cells = listOf("r0c0", "r0c1", "r1c0", "r1c1").map {
            requireNotNull(matrix.previewLayout.caretBounds(slot(it)))
        }
        assertTrue(cells[0].left < cells[1].left)
        assertTrue(cells[0].top < cells[2].top)
        assertEquals(cells[0].left, cells[2].left, 1f)
        assertEquals(cells[1].left, cells[3].left, 1f)

        listOf(fraction, limit, matrix).forEach { key ->
            assertEquals("Structural preview does not print a text approximation", "", key.text.toString())
            assertTrue(key.contentDescription.isNotBlank())
            assertTrue(key.previewLayout.width > 0 && key.previewLayout.height > 0)
            assertMathematicalInk(key)
        }
        listOf("→", "+").forEach { symbol ->
            val key = descendants(toolbar).filterIsInstance<Button>().single { it.text.toString() == symbol }
            assertFalse("Simple symbols retain ordinary text buttons", key is MathKeyboardActionPreviewView)
        }
    }

    @Test fun eachStructuralPreviewExecutesItsOriginalSemanticAction() = onMain {
        val shortcuts = listOf("FRACTION", "POWER", "SUBSCRIPT", "ROOT", "NTH_ROOT", "PARENTHESES",
            "ABSOLUTE_VALUE", "BRACKETS", "BRACES", "FUNCTION:sin", "FUNCTION:log", "INTEGRAL",
            "SUM", "PRODUCT", "LIMIT", "DERIVATIVE", "MATRIX", "CASES")
        shortcuts.forEach { shortcut ->
            val editor = MathExpressionEditor(context)
            val toolbar = MathKeyboardView(context, accent)
            val action = MathKeyboardAction.fromShortcut(shortcut)
            toolbar.bind(editor, listOf(shortcut))
            val key = preview(toolbar, action.contentDescription)
            assertTrue("Preview $shortcut retains its action", key.performClick())
            val node = editor.model.expression.children.single()
            when (shortcut) {
                "FRACTION" -> assertTrue(node is MathExpression.Fraction)
                "POWER" -> assertTrue(node is MathExpression.Scripts && node.upper != null && node.lower == null)
                "SUBSCRIPT" -> assertTrue(node is MathExpression.Scripts && node.lower != null && node.upper == null)
                "ROOT" -> assertTrue(node is MathExpression.Root && node.index == null)
                "NTH_ROOT" -> assertTrue(node is MathExpression.Root && node.index != null)
                "PARENTHESES" -> assertTrue(node is MathExpression.Group && node.delimiter == MathDelimiter.PARENTHESES)
                "ABSOLUTE_VALUE" -> assertTrue(node is MathExpression.Group && node.delimiter == MathDelimiter.ABSOLUTE)
                "BRACKETS" -> assertTrue(node is MathExpression.Group && node.delimiter == MathDelimiter.BRACKETS)
                "BRACES" -> assertTrue(node is MathExpression.Group && node.delimiter == MathDelimiter.BRACES)
                "FUNCTION:sin" -> assertTrue(node is MathExpression.Function && node.name == "sin")
                "FUNCTION:log" -> assertTrue(node is MathExpression.Function && node.name == "log")
                "INTEGRAL" -> assertTrue(node is MathExpression.LargeOperator && node.kind == MathLargeOperator.INTEGRAL)
                "SUM" -> assertTrue(node is MathExpression.LargeOperator && node.kind == MathLargeOperator.SUM)
                "PRODUCT" -> assertTrue(node is MathExpression.LargeOperator && node.kind == MathLargeOperator.PRODUCT)
                "LIMIT" -> assertTrue(node is MathExpression.Limit)
                "DERIVATIVE" -> assertTrue(node is MathExpression.Derivative)
                "MATRIX" -> assertTrue(node is MathExpression.Matrix)
                "CASES" -> assertTrue(node is MathExpression.Cases)
            }
            assertFalse("Placeholders are visual and never appear in the answer", editor.getLatex().contains('□'))
        }
    }

    @Test fun arrowInsertsARelationWhileSlotIconsMoveOnlyTheCursorAndStayPresent() = onMain {
        val editor = MathExpressionEditor(context)
        val toolbar = MathKeyboardView(context, accent)
        toolbar.bind(editor, listOf("FRACTION", "MATRIX", "→"))
        val previous = navigation(toolbar, "Slot anterior")
        val next = navigation(toolbar, "Slot siguiente")
        assertEquals(View.GONE, previous.visibility)
        assertEquals(View.GONE, next.visibility)
        val arrow = descendants(toolbar).filterIsInstance<Button>().single { it.text.toString() == "→" }
        arrow.performClick()
        assertEquals(MathExpression.Relation("→"), editor.model.expression.children.single())
        assertEquals(View.GONE, previous.visibility)
        assertEquals(View.GONE, next.visibility)

        assertTrue(editor.setLatex(""))
        preview(toolbar, "Insertar fracción").performClick()
        val fraction = editor.model.expression
        assertEquals("numerator", editor.model.cursor.path.last().slot)
        assertNavigationStable(toolbar, editor, previous, next)
        next.performClick()
        assertEquals("Leaving the empty numerator exposes its child boundary", MathCursor(offset = 1, level = MathCursorLevel.CHILD_BOUNDARY), editor.model.cursor)
        assertNull(editor.model.selectionRange)
        assertTrue("The whole fraction's boundary is still pending", next.isEnabled)
        assertEquals(fraction, editor.model.expression)
        assertNavigationStable(toolbar, editor, previous, next)
        next.performClick()
        assertEquals("The next declared child is the real denominator", MathCursor(listOf(MathCursorStep(0, "denominator")), 0), editor.model.cursor)
        assertEquals(MathCursorPresentation.Interior(MathContentScope.Slot), editor.model.cursorPresentation)
        assertSame(fraction, editor.model.expression)
        assertNavigationStable(toolbar, editor, previous, next)
        next.performClick()
        assertEquals(MathCursorPresentation.ChildBoundary(listOf(MathCursorStep(0, "denominator"))), editor.model.cursorPresentation)
        assertNavigationStable(toolbar, editor, previous, next)
        next.performClick()
        assertEquals(MathCursorPresentation.ParentBoundary(emptyList(), 0), editor.model.cursorPresentation)
        assertFalse("The maximum right position is the parent boundary", next.isEnabled)
        assertEquals(fraction, editor.model.expression)
        assertNavigationStable(toolbar, editor, previous, next)
        previous.performClick()
        assertEquals("LEFT returns across the immediately traversed denominator edge",
            MathCursorPresentation.ChildBoundary(listOf(MathCursorStep(0, "denominator"))), editor.model.cursorPresentation)
        editor.model.cursor = MathCursor(offset = 1)
        previous.performClick()
        assertEquals("Left from the parent first visits the numerator's RIGHT boundary", MathCursor(offset = 1, level = MathCursorLevel.CHILD_BOUNDARY), editor.model.cursor)
        assertEquals(MathCursorPresentation.ChildBoundary(listOf(MathCursorStep(0, "numerator")), true), editor.model.cursorPresentation)
        assertNull(editor.model.selectionRange)
        assertTrue("The numerator's content is still pending", previous.isEnabled)
        assertEquals(fraction, editor.model.expression)
        assertNavigationStable(toolbar, editor, previous, next)
        previous.performClick()
        assertEquals("numerator", editor.model.cursor.path.last().slot)
        assertEquals(fraction, editor.model.expression)
        assertNavigationStable(toolbar, editor, previous, next)
        previous.performClick()
        assertEquals(MathCursorPresentation.ChildBoundary(listOf(MathCursorStep(0, "denominator")), true), editor.model.cursorPresentation)
        assertNavigationStable(toolbar, editor, previous, next)
        previous.performClick()
        assertEquals("denominator", editor.model.cursor.path.last().slot)
        previous.performClick()
        assertEquals(MathCursor(), editor.model.cursor)
        assertFalse(previous.isEnabled)
        next.performClick()
        assertEquals("RIGHT from HOME reaches the nearest whole parent", MathCursor(offset = 1), editor.model.cursor)
        assertEquals(MathCursorPresentation.ParentBoundary(emptyList(), 0), editor.model.cursorPresentation)
        assertFalse(next.isEnabled)
        assertNavigationStable(toolbar, editor, previous, next)
        assertEquals(fraction, editor.model.expression)

        assertTrue(editor.setLatex(""))
        assertSame("Existing navigation controls survive clearing a structure", previous, navigation(toolbar, "Slot anterior"))
        assertEquals(View.GONE, previous.visibility)
        preview(toolbar, "Insertar matriz 2 por 2").performClick()
        val matrix = editor.model.expression
        val start = editor.model.cursor
        for (name in listOf("r0c0", "r0c1", "r1c0", "r1c1")) {
            assertEquals(MathCursor(listOf(MathCursorStep(0, name)), 0), editor.model.cursor)
            next.performClick()
            assertEquals(MathCursorPresentation.ChildBoundary(listOf(MathCursorStep(0, name)), true), editor.model.cursorPresentation)
            assertSame(matrix, editor.model.expression)
            next.performClick()
            assertNavigationStable(toolbar, editor, previous, next)
        }
        assertTrue(editor.model.cursor.path.isEmpty())
        assertEquals(MathCursorPresentation.ParentBoundary(emptyList(), 0), editor.model.cursorPresentation)
        assertFalse(next.isEnabled)
        assertNavigationStable(toolbar, editor, previous, next)
        editor.model.cursor = MathCursor(offset = 1)
        previous.performClick()
        for (name in listOf("r0c0", "r0c1", "r1c0", "r1c1")) {
            assertEquals(MathCursorLevel.CHILD_BOUNDARY, editor.model.navigationLevel)
            assertEquals(MathCursorPresentation.ChildBoundary(listOf(MathCursorStep(0, name)), true), editor.model.cursorPresentation)
            previous.performClick()
            assertEquals(name, editor.model.cursor.path.last().slot)
            assertEquals(matrix, editor.model.expression)
            assertNavigationStable(toolbar, editor, previous, next)
            if (name == "r0c0") assertEquals(start, editor.model.cursor)
            previous.performClick()
            assertEquals("Left from START continues to the next declared child RIGHT boundary or HOME",
                if (name == "r1c1") MathCursor(offset = 0) else MathCursor(offset = 1, level = MathCursorLevel.CHILD_BOUNDARY), editor.model.cursor)
            assertNull(editor.model.selectionRange)
            assertEquals(matrix, editor.model.expression)
            assertEquals("Only the final declared cell exhausts pending left states", name != "r1c1", previous.isEnabled)
            assertNavigationStable(toolbar, editor, previous, next)
        }
        assertTrue(editor.model.cursor.path.isEmpty())
        assertFalse(previous.isEnabled)
        assertNavigationStable(toolbar, editor, previous, next)
        assertEquals(matrix, editor.model.expression)
    }

    @Test fun previewsFollowTextScaleAccessibilityAndContextualShapeWithoutLeakingAnswers() = onMain {
        val editor = MathExpressionEditor(context)
        editor.configureExpected("\\begin{matrix}7&8&9\\end{matrix}")
        val toolbar = MathKeyboardView(context, accent)
        toolbar.bind(editor, listOf("MATRIX", "FRACTION", "+"))
        measure(toolbar)
        val first = preview(toolbar, "Insertar matriz 1 por 3")
        val node = first.previewExpression.children.single() as MathExpression.Matrix
        assertEquals(1, node.rows.size)
        assertEquals(3, node.rows.single().size)
        assertTrue("Expected values are absent from the toolbar template", node.rows.single().all { it.children.isEmpty() })
        val size = first.previewTextSizePx
        val width = first.previewLayout.width
        val height = first.previewLayout.height
        val expression = first.previewExpression
        editor.scaleMathText(1.5f)
        measure(toolbar)
        val scaled = preview(toolbar, "Insertar matriz 1 por 3")
        assertEquals(expression, scaled.previewExpression)
        assertEquals("Toolbar inherits lesson text scale", size * 1.5f, scaled.previewTextSizePx, .1f)
        assertTrue(scaled.previewLayout.width > width)
        assertTrue(scaled.previewLayout.height > height)
        assertTrue(scaled.width >= scaled.previewLayout.width)
        assertTrue(scaled.height >= scaled.previewLayout.height)
        assertTrue(scaled.width >= MathWidgetUi.dp(context, 48))
        assertTrue(scaled.height >= MathWidgetUi.dp(context, 48))
        // Android does not populate an AccessibilityNodeInfo for an unattached View.
        // The real IME test additionally checks native node info while its popup is attached.
        assertEquals("android.widget.Button", scaled.accessibilityClassName.toString())
        assertEquals("Insertar matriz 1 por 3", scaled.contentDescription.toString())
        assertTrue(scaled.isClickable)
        scaled.performClick()
        val inserted = editor.model.expression.children.single() as MathExpression.Matrix
        assertEquals(1, inserted.rows.size)
        assertEquals(3, inserted.rows.single().size)
        val answer = editor.getLatex()
        editor.configureExpected("\\begin{matrix}1&2\\\\3&4\\end{matrix}")
        assertSame("A contextual shape update retains the native preview Button", scaled,
            preview(toolbar, "Insertar matriz 2 por 2"))
        val changed = scaled.previewExpression.children.single() as MathExpression.Matrix
        assertEquals(2, changed.rows.size)
        assertTrue(changed.rows.all { row -> row.size == 2 && row.all { it.children.isEmpty() } })
        assertEquals("Updating a toolbar template cannot change the answer", answer, editor.getLatex())
    }

    @Test fun simpleModeKeepsLinearInsertionAndHasNoSlotNavigationIcons() = onMain {
        val editor = EditText(context)
        val toolbar = MathKeyboardView(context, accent)
        toolbar.bind(editor, "ALGEBRA", JSONArray().put("/").put("+"))
        assertTrue(descendants(toolbar).none { it is MathKeyboardActionPreviewView || it is ImageButton })
        val slash = descendants(toolbar).filterIsInstance<Button>().single { it.text.toString() == "/" }
        slash.performClick()
        assertEquals("/", editor.text.toString())
        editor.setSelection(editor.text.length)
        descendants(toolbar).filterIsInstance<Button>().single { it.text.toString() == "+" }.performClick()
        assertEquals("/+", editor.text.toString())
    }

    private fun assertNavigationStable(toolbar: MathKeyboardView, editor: MathExpressionEditor, previous: ImageButton, next: ImageButton) {
        measure(toolbar)
        assertSame(previous, navigation(toolbar, "Slot anterior"))
        assertSame(next, navigation(toolbar, "Slot siguiente"))
        assertEquals(View.VISIBLE, previous.visibility)
        assertEquals(View.VISIBLE, next.visibility)
        assertEquals(editor.model.canNavigatePrevious, previous.isEnabled)
        assertEquals(editor.model.canNavigateNext, next.isEnabled)
        assertNotNull(previous.drawable)
        assertNotNull(next.drawable)
        listOf(previous, next).forEach { icon ->
            assertTrue("Slot navigation retains a 48dp touch target", icon.width >= MathWidgetUi.dp(context, 48) &&
                icon.height >= MathWidgetUi.dp(context, 48))
        }
    }

    private fun preview(toolbar: MathKeyboardView, description: String) =
        descendants(toolbar).filterIsInstance<MathKeyboardActionPreviewView>().single { it.contentDescription.toString() == description }

    private fun navigation(toolbar: MathKeyboardView, description: String) =
        descendants(toolbar).filterIsInstance<ImageButton>().single { it.contentDescription.toString() == description }

    private fun slot(name: String) = MathCursor(listOf(MathCursorStep(0, name)), 0)

    private fun measure(view: View) {
        view.measure(View.MeasureSpec.makeMeasureSpec(MathWidgetUi.dp(context, 420),
            if (view is MathKeyboardView) View.MeasureSpec.EXACTLY else View.MeasureSpec.AT_MOST),
            View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED))
        view.layout(0, 0, view.measuredWidth, view.measuredHeight)
    }

    private fun assertMathematicalInk(key: MathKeyboardActionPreviewView) {
        key.background = null
        key.setTextColor(Color.WHITE)
        measure(key)
        val bitmap = Bitmap.createBitmap(key.width, key.height, Bitmap.Config.ARGB_8888)
        try {
            key.draw(Canvas(bitmap))
            var ink = 0
            for (y in 0 until bitmap.height) for (x in 0 until bitmap.width) {
                if (Color.alpha(bitmap.getPixel(x, y)) > 32) ink++
            }
            assertTrue("${key.contentDescription} draws mathematical ink without a text label", ink > 5)
        } finally { bitmap.recycle() }
    }

    private fun descendants(view: View): List<View> = listOf(view) +
        if (view is ViewGroup) (0 until view.childCount).flatMap { descendants(view.getChildAt(it)) } else emptyList()

    private fun onMain(body: () -> Unit) = instrumentation.runOnMainSync { body() }
}
