package com.learngraphicsprogramming.app.math

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.text.Spanned
import android.text.style.ReplacementSpan
import android.view.MotionEvent
import android.view.View
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import kotlin.math.ceil

/** Exercises native Canvas geometry and the shared lesson-span layout, including cursor slots. */
@RunWith(AndroidJUnit4::class)
class MathExpressionRenderingDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private fun parse(source: String): MathExpression.Sequence =
        (MathLatexParser.parse(source) as MathLatexParser.Result.Success).expression
    private fun paint(size: Float = 32f) = Paint(Paint.ANTI_ALIAS_FLAG).apply { textSize = size; color = Color.WHITE }

    @Test fun fractionsStackSlotsAndHitTestTheirOwnSequences() {
        val expression = parse("\\frac{x+1}{2}")
        val layout = MathExpressionLayout.create(expression, paint(), 32f, true)
        val numerator = MathCursor(listOf(MathCursorStep(0, "numerator")), 1)
        val denominator = MathCursor(listOf(MathCursorStep(0, "denominator")), 1)
        val top = layout.caretBounds(numerator)!!
        val bottom = layout.caretBounds(denominator)!!
        assertTrue("Numerator and denominator occupy separate rows", top.bottom < bottom.top)
        assertEquals(numerator, layout.hitTest(top.left, top.centerY()))
        assertEquals(denominator, layout.hitTest(bottom.left, bottom.centerY()))
        val scaled = MathExpressionLayout.create(expression, paint(), 48f, true)
        assertEquals(layout.width * 1.5f, scaled.width, 1f)
        assertEquals(layout.height * 1.5f, scaled.height, 1f)
    }

    @Test fun rootsScriptsLargeOperatorsMatricesAndCasesHaveNativeGeometry() {
        val expressions = listOf(
            "x^{2}_{1}", "\\sqrt[3]{\\frac{x+1}{2}}", "\\sin(x)+\\log_{2}(x)",
            "\\left|x+1\\right|", "\\int_{0}^{1}{x^{2}\\,dx}", "\\sum_{i=1}^{n}{i}",
            "\\prod_{i=1}^{n}{i}", "\\lim_{x\\to0}{x}",
            "\\frac{\\mathrm{d}^{2}x}{\\mathrm{d}x^{2}}",
            "\\begin{matrix}1&2\\\\3&4\\end{matrix}",
            "\\begin{cases}x&x>0\\\\-x&x\\le0\\end{cases}"
        )
        expressions.forEach { source ->
            val layout = MathExpressionLayout.create(parse(source), paint(), 32f)
            assertTrue(source, layout.width > 0f && layout.height > 0f && layout.width.isFinite() && layout.height.isFinite())
            val image = Bitmap.createBitmap(ceil(layout.width).toInt() + 16, ceil(layout.height).toInt() + 16, Bitmap.Config.ARGB_8888)
            layout.draw(Canvas(image), paint(), 8f, 8f - layout.ascent)
            val pixels = IntArray(image.width * image.height)
            image.getPixels(pixels, 0, image.width, 0, 0, image.width, image.height)
            assertTrue("The native layout draws ink for $source", pixels.count { Color.alpha(it) > 30 } > 20)
            image.recycle()
        }
        val matrix = MathExpressionLayout.create(parse(expressions[9]), paint(), 32f)
        fun cell(slot: String) = matrix.caretBounds(MathCursor(listOf(MathCursorStep(0, slot)), 0))!!
        assertTrue(cell("r0c0").centerY() < cell("r1c0").centerY())
        assertTrue(cell("r0c0").left < cell("r0c1").left)
        val integral = MathExpressionLayout.create(parse(expressions[4]), paint(), 32f)
        val upper = integral.caretBounds(MathCursor(listOf(MathCursorStep(0, "upper")), 0))!!
        val lower = integral.caretBounds(MathCursor(listOf(MathCursorStep(0, "lower")), 0))!!
        assertTrue(upper.centerY() < lower.centerY())
    }

    @Test fun lessonInlineDisplayAndInvalidLatexUseTheSharedLayout() {
        val source = "Usa \\(x^{2}+1\\) y después \\[\\frac{x+1}{2}\\] continúa."
        val rendered = MathTextRenderer.render(source) as Spanned
        assertTrue("Display mathematics separates paragraphs", rendered.toString().contains("\n\\frac{x+1}{2}\n"))
        val spans = rendered.getSpans(0, rendered.length, ReplacementSpan::class.java)
        assertEquals(2, spans.size)
        val invalid = MathTextRenderer.render("\\unknown{a}", "block") as Spanned
        assertEquals("\\unknown{a}", invalid.toString())
        assertTrue(invalid.getSpans(0, invalid.length, ReplacementSpan::class.java).single()
            .getSize(paint(), invalid, 0, invalid.length, null) > 0)
        val legacy = MathTextRenderer.parseExpression("1/2 + x²")
        assertTrue(legacy.children.any { it is MathExpression.Fraction })
        assertTrue(legacy.children.any { it is MathExpression.Scripts })
    }

    @Test fun legacyCoordinateSeparatorsRemainVisibleWithOrWithoutWhitespace() {
        for (source in listOf("(1,2)", "(1, 2)", "(−1,2)", "(−1, 2)")) {
            val coordinate = MathTextRenderer.parseExpression(source).children.single() as MathExpression.Group
            assertTrue("$source retains its coordinate separator", coordinate.content.children.contains(MathExpression.Symbol(",")))
            val numbers = coordinate.content.children.filterIsInstance<MathExpression.Number>().map { it.value }
            assertEquals("$source retains two coordinates", listOf("1", "2"), numbers)
            val layout = MathExpressionLayout.create(MathExpression.Sequence(listOf(coordinate)), paint(), 32f)
            assertTrue(layout.width > 0f && layout.height > 0f)
        }
        assertEquals("Decimal commas remain supported outside legacy coordinates",
            MathExpression.Sequence(listOf(MathExpression.Number("1.5"))), parse("1,5"))
    }

    @Test fun expressionViewScalesAndRoutesTouchesToStructuralSlots() {
        instrumentation.runOnMainSync {
            val view = MathExpressionView(instrumentation.targetContext).apply {
                expression = parse("\\frac{}{2}")
                showPlaceholders = true
                textSizePx = 32f
                setPadding(8, 8, 8, 8)
            }
            fun measure() {
                view.measure(View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED), View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED))
                view.layout(0, 0, view.measuredWidth, view.measuredHeight)
            }
            measure()
            val height = view.expressionLayout.height
            view.scaleMathText(1.5f)
            measure()
            assertEquals(height * 1.5f, view.expressionLayout.height, 1f)
            assertFalse("Placeholder boxes never become answer text", MathLatexSerializer.serialize(view.expression).contains("□"))
            val expected = MathCursor(listOf(MathCursorStep(0, "numerator")), 0)
            val caret = view.expressionLayout.caretBounds(expected)!!
            var requested: MathCursor? = null
            view.onCursorRequested = { requested = it }
            val x = view.expressionOriginX + caret.left
            val y = view.expressionBaseline + caret.centerY()
            MotionEvent.obtain(0, 0, MotionEvent.ACTION_DOWN, x, y, 0).also { view.onTouchEvent(it); it.recycle() }
            MotionEvent.obtain(0, 1, MotionEvent.ACTION_UP, x, y, 0).also { view.onTouchEvent(it); it.recycle() }
            assertEquals(expected, requested)
            assertFalse("Editing surfaces do not advertise edge-dismiss gestures", view.allowEdgeSwipeToDismiss)
        }
    }
}
