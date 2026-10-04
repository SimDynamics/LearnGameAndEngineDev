package com.learngraphicsprogramming.app.math

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF
import android.view.View
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

/** Cursor geometry is a projection of structural state and never participates in navigation. */
@RunWith(AndroidJUnit4::class)
class MathStructuralCursorRenderingDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private fun parse(source: String) = (MathLatexParser.parse(source) as MathLatexParser.Result.Success).expression
    private fun layout(expression: MathExpression.Sequence, size: Float = 32f) =
        MathExpressionLayout.create(expression, Paint(Paint.ANTI_ALIAS_FLAG), size, true)

    @Test fun neutralCaretUsesTheCurrentSequenceWithoutAnExtraFormulaLevel() {
        val expression = parse("\\frac{\\sqrt{x}}{2}")
        val layout = layout(expression)
        val path = listOf(MathCursorStep(0, "numerator"))
        val scope = requireNotNull(layout.sequenceBounds(path))
        val cursor = MathCursor(path, 1)
        val caret = requireNotNull(layout.presentationCaretBounds(cursor, MathCursorPresentation.Neutral))
        assertEquals(requireNotNull(layout.caretBounds(cursor)).left, caret.left, .001f)
        assertTrue("Neutral feedback encloses this Sequence's own height", caret.top < scope.top && caret.bottom > scope.bottom)
        assertTrue("A nested neutral level does not borrow the whole formula height", caret.height() < layout.height)
        assertEquals(expression, parse("\\frac{\\sqrt{x}}{2}"))
    }

    @Test fun sharedParentCursorRendersTheAnchoredChildRatherThanItsSibling() {
        val expression = parse("x_{123}^{2}")
        val layout = layout(expression)
        val parentCursor = MathCursor(offset = 1)
        val upperPath = listOf(MathCursorStep(0, "upper"))
        val lowerPath = listOf(MathCursorStep(0, "lower"))
        val upperScope = requireNotNull(layout.sequenceBounds(upperPath))
        val lowerScope = requireNotNull(layout.sequenceBounds(lowerPath))
        val upper = requireNotNull(layout.presentationCaretBounds(parentCursor,
            MathCursorPresentation.ChildBoundary(upperPath, after = true)))
        val lower = requireNotNull(layout.presentationCaretBounds(parentCursor,
            MathCursorPresentation.ChildBoundary(lowerPath, after = true)))
        assertEquals(upperScope.top, upper.top, .001f)
        assertEquals(upperScope.bottom, upper.bottom, .001f)
        assertEquals(lowerScope.top, lower.top, .001f)
        assertEquals(lowerScope.bottom, lower.bottom, .001f)
        assertTrue("Superscript and subscript frontiers retain their own row", upper.centerY() < lower.centerY())
        assertTrue("The longer subscript has its own exterior edge", lower.left > upper.left)
        assertTrue(upper.left > upperScope.right)
        val startCursor = MathCursor(upperPath, 0)
        val start = requireNotNull(layout.presentationCaretBounds(startCursor, MathCursorPresentation.Interior()))
        assertEquals("START retains the ordinary interior caret", layout.caretBounds(startCursor), start)
        assertTrue("Only the right edge represents an exterior frontier", start.left < upper.left)
        assertThrows("An exterior-left state is invalid", IllegalArgumentException::class.java) {
            MathCursorPresentation.ChildBoundary(upperPath, after = false)
        }
        val neutral = requireNotNull(layout.presentationCaretBounds(parentCursor, MathCursorPresentation.Neutral))
        assertTrue("Sequence feedback is taller than either individual child", neutral.height() > upper.height() && neutral.height() > lower.height())
    }

    @Test fun emptySlotInteriorAndExteriorRemainDistinctAtBothTextScales() {
        val expression = parse("x^{}")
        val path = listOf(MathCursorStep(0, "upper"))
        fun geometry(size: Float): List<RectF> {
            val layout = layout(expression, size)
            val scope = requireNotNull(layout.sequenceBounds(path))
            val inside = requireNotNull(layout.presentationCaretBounds(MathCursor(path, 0), MathCursorPresentation.Interior()))
            val right = requireNotNull(layout.presentationCaretBounds(MathCursor(offset = 1), MathCursorPresentation.ChildBoundary(path, after = true)))
            assertTrue("Empty placeholders retain their actual box width", scope.width() > 0f)
            assertEquals(scope.centerX(), inside.left, .001f)
            assertEquals("An empty START keeps the normal interior caret", layout.caretBounds(MathCursor(path, 0)), inside)
            val exteriorGap = maxOf(1f, size * .05f)
            assertEquals(scope.right + exteriorGap, right.left, .001f)
            assertTrue(right.left > scope.right)
            assertTrue(inside.left < right.left)
            return listOf(scope, inside, right)
        }
        val normal = geometry(32f)
        val enlarged = geometry(48f)
        assertEquals(normal.first().width() * 1.5f, enlarged.first().width(), .01f)
        normal.zip(enlarged).forEach { (before, after) ->
            // Font measurement may round the enclosing base's position. The slot's
            // dimensions and each caret's own height still scale with the math size.
            assertEquals(before.height() * 1.5f, after.height(), .01f)
        }
    }

    @Test fun glyphTouchAndCaretLookupDependOnAstLocationRatherThanPresentationLevel() {
        val expression = parse("1234")
        val layout = layout(expression)
        val location = MathCursor()
        val glyph = requireNotNull(layout.textCaretBounds(location, 2))
        val target = layout.hitTestForEditing(glyph.left, glyph.centerY())
        assertEquals(emptyList<MathCursorStep>(), target.cursor.path)
        assertEquals(0, target.cursor.offset)
        assertEquals(2, target.leafTextOffset)
        for (level in MathCursorLevel.values()) {
            val cursor = location.copy(level = level)
            assertEquals(requireNotNull(layout.caretBounds(location)), requireNotNull(layout.caretBounds(cursor)))
            assertEquals(glyph, requireNotNull(layout.textCaretBounds(cursor, 2)))
        }
        layout.presentationCaretBounds(location, MathCursorPresentation.Neutral)
        layout.presentationCaretBounds(location, MathCursorPresentation.Interior())
        assertEquals("Presentation queries never change glyph hit-testing", target,
            layout.hitTestForEditing(glyph.left, glyph.centerY()))
    }

    @Test fun textEndsEnterAtomsWhileWhitespaceRetainsItsNeutralLocation() {
        val layout = layout(parse("1234"))
        val location = MathCursor()
        val start = requireNotNull(layout.textCaretBounds(location, 0))
        val end = requireNotNull(layout.textCaretBounds(location, 4))
        assertEquals(0, layout.hitTestForEditing(start.left, start.centerY()).leafTextOffset)
        assertEquals(4, layout.hitTestForEditing(end.left, end.centerY()).leafTextOffset)
        val whitespace = layout.hitTestForEditing(end.left + 8f, end.centerY())
        assertNull("Whitespace does not invent an atom editing target", whitespace.leafTextOffset)
        assertEquals(1, whitespace.cursor.offset)
        assertEquals("The legacy structural hit-test still returns the parent boundary", MathCursor(offset = 1),
            layout.hitTest(end.left, end.centerY()))
    }

    @Test fun interiorAtomGeometryDoesNotUseTheHeightOfUnrelatedSiblings() {
        val expression = MathExpression.Sequence(listOf(parse("\\frac{x}{2}").children.single(),
            MathExpression.Number("12"), MathExpression.Number("34")))
        val layout = layout(expression)
        val cursor = MathCursor(offset = 2)
        val inside = requireNotNull(layout.presentationCaretBounds(cursor,
            MathCursorPresentation.Interior(MathContentScope.Atom(1, 3))))
        val neutral = requireNotNull(layout.presentationCaretBounds(cursor, MathCursorPresentation.Neutral))
        val isolatedText = requireNotNull(layout(parse("1234")).caretBounds(MathCursor()))
        assertEquals("A split numeric atom retains its own text height", isolatedText.height(), inside.height(), .001f)
        assertTrue("The taller fraction sibling affects only the Sequence-level caret", neutral.height() > inside.height())
        assertEquals(requireNotNull(layout.caretBounds(cursor)).left, inside.left, .001f)
    }

    @Test fun parentBoundaryUsesTheWholeDirectParentAtRootAndNestedLevels() {
        val expression = parse("\\frac{\\sqrt{x}+12}{x_{1}^{2}}+34")
        val layout = layout(expression)
        val rootPath = emptyList<MathCursorStep>()
        val numeratorPath = listOf(MathCursorStep(0, "numerator"))
        val denominatorPath = listOf(MathCursorStep(0, "denominator"))
        val cases = listOf(rootPath to 0, rootPath to 2, numeratorPath to 0, numeratorPath to 2, denominatorPath to 0)
        for ((path, index) in cases) {
            val parent = requireNotNull(layout.childBounds(path, index))
            val edge = requireNotNull(layout.presentationCaretBounds(MathCursor(path, index + 1),
                MathCursorPresentation.ParentBoundary(path, index)))
            assertEquals(parent.right + 32f * .05f, edge.left, .001f)
            assertEquals(parent.top, edge.top, .001f)
            assertEquals(parent.bottom, edge.bottom, .001f)
        }
        val last = requireNotNull(layout.presentationCaretBounds(MathCursor(offset = 3),
            MathCursorPresentation.ParentBoundary(rootPath, 2)))
        assertTrue("A short final parent does not borrow the tall previous fraction's height", last.height() < layout.height)
        val scriptsParent = requireNotNull(layout.presentationCaretBounds(MathCursor(denominatorPath, 1),
            MathCursorPresentation.ParentBoundary(denominatorPath, 0)))
        val upperPath = denominatorPath + MathCursorStep(0, "upper")
        val upperEdge = requireNotNull(layout.presentationCaretBounds(MathCursor(denominatorPath, 1),
            MathCursorPresentation.ChildBoundary(upperPath)))
        assertTrue("Parent feedback includes the base and both scripts", scriptsParent.height() > upperEdge.height())
    }

    @Test fun nativeDrawingShowsDifferentLevelsWithoutRelayoutOrAstMutation() {
        instrumentation.runOnMainSync {
            val expression = parse("x_{12}^{23}")
            val model = MathExpressionModel(expression)
            val draft = model.saveDraft()
            val view = MathExpressionView(instrumentation.targetContext).apply {
                this.expression = expression
                textSizePx = 32f
                showPlaceholders = true
                caretColor = Color.MAGENTA
                setPadding(12, 12, 12, 12)
            }
            view.measure(View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED),
                View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED))
            view.layout(0, 0, view.measuredWidth, view.measuredHeight)
            val cachedLayout = view.expressionLayout
            val initialSize = view.width to view.height
            val upper = listOf(MathCursorStep(0, "upper"))
            fun draw(cursor: MathCursor, presentation: MathCursorPresentation): RectF {
                view.cursor = cursor
                view.cursorPresentation = presentation
                assertSame("Presentation only invalidates paint, preserving shared layout", cachedLayout, view.expressionLayout)
                assertFalse("Cursor feedback does not request layout", view.isLayoutRequested)
                val image = Bitmap.createBitmap(view.width, view.height, Bitmap.Config.ARGB_8888)
                try {
                    view.draw(Canvas(image))
                    val pixels = IntArray(image.width * image.height)
                    image.getPixels(pixels, 0, image.width, 0, 0, image.width, image.height)
                    val caretPixels = pixels.indices.filter { index ->
                        val color = pixels[index]
                        Color.alpha(color) > 100 && Color.red(color) > 180 && Color.blue(color) > 180 && Color.green(color) < 80
                    }
                    assertTrue("The native Canvas draws caret feedback", caretPixels.isNotEmpty())
                    return RectF(caretPixels.minOf { it % image.width }.toFloat(), caretPixels.minOf { it / image.width }.toFloat(),
                        caretPixels.maxOf { it % image.width }.toFloat(), caretPixels.maxOf { it / image.width }.toFloat())
                } finally { image.recycle() }
            }
            val start = draw(MathCursor(upper, 0), MathCursorPresentation.Interior())
            val interior = draw(MathCursor(upper, 1), MathCursorPresentation.Interior())
            val frontier = draw(MathCursor(offset = 1), MathCursorPresentation.ChildBoundary(upper, after = true))
            val neutral = draw(MathCursor(offset = 1), MathCursorPresentation.Neutral)
            assertTrue("Child frontier is visibly outside content", frontier.left > interior.left)
            assertTrue("START remains an interior caret without frontier caps", start.left < interior.left && start.width() < frontier.width())
            assertTrue("Neutral Sequence caret represents the larger current level", neutral.height() > frontier.height())
            assertEquals(initialSize, view.width to view.height)
            assertSame(expression, view.expression)
            assertEquals("Painting never changes model cursor, selection, trail or AST", draft, model.saveDraft())
        }
    }
}
