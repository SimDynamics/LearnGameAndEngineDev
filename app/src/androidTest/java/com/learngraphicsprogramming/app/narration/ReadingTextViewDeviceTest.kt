package com.learngraphicsprogramming.app.narration

import android.content.res.ColorStateList
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Typeface
import android.text.SpannableString
import android.text.Spanned
import android.text.style.ForegroundColorSpan
import android.text.style.ReplacementSpan
import android.text.style.StyleSpan
import android.view.View
import android.view.ViewGroup
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.math.MathTextRenderer
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import kotlin.math.ceil
import kotlin.math.floor
import kotlin.math.max
import kotlin.math.min

/** Native Canvas checks: no preferences, activity, audio, curricular assets or device data change. */
@RunWith(AndroidJUnit4::class)
class ReadingTextViewDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext

    @Test fun wrappedProseContainsOriginalAndPendingWordsWithoutReplacingTextLayoutOrSpans() = onMain {
        for (background in listOf(Color.WHITE, Color.rgb(16, 17, 20))) {
            val originalColor = if (background == Color.WHITE) Color.rgb(20, 23, 27) else Color.rgb(233, 235, 240)
            val phrase = "Primera palabra leída, segunda palabra pendiente y otras palabras que continúan en varias líneas."
            val source = SpannableString(phrase).apply {
                setSpan(StyleSpan(Typeface.BOLD), 0, "Primera".length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                setSpan(ForegroundColorSpan(originalColor), 0, "Primera".length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            }
            val view = prose(source, background, originalColor)
            assertTrue("The paragraph wraps natively", view.lineCount > 1)
            val text = view.text
            val layout = view.layout
            val colors = view.textColors
            val spans = spanSnapshot(text)
            val geometry = geometry(view)
            val ordinary = render(view)
            val pendingStart = phrase.indexOf("segunda")
            val gray = LessonReadingColor.unread(background)
            assertTrue(LessonReadingColor.contrast(gray, background) >= LessonReadingColor.MIN_CONTRAST)

            view.setUnreadWordRanges(listOf(ReadingTextRange(pendingStart, phrase.length)), gray)
            val progressive = render(view)
            assertWordColor(progressive, view, phrase.indexOf("Primera"), "Primera".length, originalColor)
            assertWordColor(progressive, view, pendingStart, "segunda".length, gray)
            assertWordPixelsUnchanged(ordinary, progressive, view, 0, "Primera".length)
            assertSame("The curricular CharSequence is unchanged", text, view.text)
            assertSame("No layout is rebuilt", layout, view.layout)
            assertSame("The original ColorStateList remains installed", colors, view.textColors)
            assertEquals(spans, spanSnapshot(view.text))
            assertEquals(geometry, geometry(view))

            view.setUnreadWordRanges(null, gray)
            assertPixelsEqual("Disabling tracking restores every original pixel", ordinary, render(view))
            view.setUnreadWordRanges(emptyList(), gray)
            assertPixelsEqual("An empty pending set keeps the original drawing", ordinary, render(view))
        }
    }

    @Test fun bidiAndDisjointPendingRangesKeepUnheardSeekGapsGray() = onMain {
        val background = Color.rgb(16, 17, 20)
        val phrase = "uno dos tres שלום cuatro cinco seis"
        val view = prose(phrase, background, Color.WHITE)
        val gray = LessonReadingColor.unread(background)
        val gapStart = phrase.indexOf("dos")
        val replayedStart = phrase.indexOf("tres")
        val replayedEnd = replayedStart + "tres".length
        view.setUnreadWordRanges(listOf(
            ReadingTextRange(replayedEnd, phrase.length),
            ReadingTextRange(gapStart, replayedStart)
        ), gray)
        val progressive = render(view)
        assertWordColor(progressive, view, 0, "uno".length, Color.WHITE)
        assertWordColor(progressive, view, gapStart, "dos".length, gray)
        assertWordColor(progressive, view, replayedStart, "tres".length, Color.WHITE)
        assertWordColor(progressive, view, phrase.indexOf("שלום"), "שלום".length, gray)
    }

    @Test fun mathematicalReplacementSpansAndInteractiveTextKeepOrdinaryRendering() = onMain {
        val formula = ReadingTextView(context).apply {
            textSize = 24f
            setTextColor(Color.WHITE)
            setBackgroundColor(Color.rgb(16, 17, 20))
            MathTextRenderer.apply(this, "El resultado es \\(\\frac{1}{2}\\) y sigue.")
        }
        measure(formula)
        val text = formula.text
        val spans = spanSnapshot(text)
        assertTrue((text as Spanned).getSpans(0, text.length, ReplacementSpan::class.java).isNotEmpty())
        assertFalse(formula.safeSupportsWordReading)
        val original = render(formula)
        formula.setUnreadWordRanges(listOf(ReadingTextRange(0, text.length)), Color.GRAY)
        assertPixelsEqual("A formula is never split or recolored with unverified word offsets", original, render(formula))
        assertSame(text, formula.text)
        assertEquals(spans, spanSnapshot(formula.text))

        val interactive = prose("Este control conserva su apariencia.", Color.WHITE, Color.BLACK)
        interactive.isClickable = true
        assertFalse(interactive.safeSupportsWordReading)
        val ordinary = render(interactive)
        interactive.setUnreadWordRanges(listOf(ReadingTextRange(0, interactive.text.length)), Color.GRAY)
        assertPixelsEqual("Interactive views are excluded", ordinary, render(interactive))
    }

    @Test fun invalidOffsetsSurrogateSplitsAndChangedDisplayTextFailToOriginalPresentation() = onMain {
        val view = prose("A😀B siguen las palabras.", Color.WHITE, Color.BLACK)
        val original = render(view)
        for (range in listOf(ReadingTextRange(-1, 3), ReadingTextRange(2, 3),
            ReadingTextRange(0, view.text.length + 1), ReadingTextRange(4, 4))) {
            view.setUnreadWordRanges(listOf(range), Color.GRAY)
            assertPixelsEqual("Invalid display offsets must not invent partial-word evidence: $range", original, render(view))
        }
        view.setUnreadWordRanges(listOf(ReadingTextRange(0, view.text.length)), Color.GRAY)
        assertFalse(original.sameAs(render(view)))
        view.text = "Otro contenido ya cargado por la vista."
        measure(view)
        val afterDisplayChange = render(view)
        view.setUnreadWordRanges(null, Color.GRAY)
        assertPixelsEqual("Offsets from an earlier CharSequence are ignored", afterDisplayChange, render(view))
    }

    private fun prose(content: CharSequence, background: Int, color: Int) = ReadingTextView(context).apply {
        layoutParams = ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)
        text = content
        setTextColor(ColorStateList(arrayOf(intArrayOf(-android.R.attr.state_enabled), intArrayOf()), intArrayOf(color, color)))
        textSize = 25f
        setLineSpacing(0f, 1.25f)
        setPadding(19, 17, 13, 23)
        setBackgroundColor(background)
        measure(this)
        assertTrue(safeSupportsWordReading)
    }

    private fun measure(view: ReadingTextView) {
        view.measure(View.MeasureSpec.makeMeasureSpec(540, View.MeasureSpec.EXACTLY),
            View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED))
        view.layout(0, 0, view.measuredWidth, view.measuredHeight)
    }

    private fun render(view: ReadingTextView): Bitmap = Bitmap.createBitmap(view.width, view.height, Bitmap.Config.ARGB_8888)
        .also { view.draw(Canvas(it)) }

    private fun geometry(view: ReadingTextView): List<Any> = listOf(
        view.measuredWidth, view.measuredHeight, view.width, view.height, view.lineCount,
        view.paddingLeft, view.paddingTop, view.paddingRight, view.paddingBottom,
        view.textSize, view.textScaleX, view.typeface,
        (0 until view.lineCount).map { view.layout.getLineBaseline(it) }
    )

    private fun spanSnapshot(text: CharSequence): List<List<Any>> = (text as? Spanned)?.let { spanned ->
        spanned.getSpans(0, text.length, Any::class.java).map { span ->
            listOf(span, spanned.getSpanStart(span), spanned.getSpanEnd(span), spanned.getSpanFlags(span))
        }
    }.orEmpty()

    private fun wordBounds(view: ReadingTextView, start: Int, length: Int): IntArray {
        val layout = view.layout
        val line = layout.getLineForOffset(start)
        assertEquals("The sampled word must fit on one native line", line, layout.getLineForOffset(start + length - 1))
        val first = layout.getPrimaryHorizontal(start)
        val last = layout.getPrimaryHorizontal(start + length)
        return intArrayOf(
            floor(min(first, last) + view.compoundPaddingLeft).toInt().coerceIn(0, view.width - 1),
            (layout.getLineTop(line) + view.totalPaddingTop).coerceIn(0, view.height - 1),
            ceil(max(first, last) + view.compoundPaddingLeft).toInt().coerceIn(0, view.width),
            (layout.getLineBottom(line) + view.totalPaddingTop).coerceIn(0, view.height)
        )
    }

    private fun assertWordColor(bitmap: Bitmap, view: ReadingTextView, start: Int, length: Int, color: Int) {
        val bounds = wordBounds(view, start, length)
        var ink = 0
        for (y in bounds[1] until bounds[3]) for (x in bounds[0] until bounds[2]) {
            if (bitmap.getPixel(x, y) == color) ink++
        }
        assertTrue("Expected solid glyph pixels in ${view.text.subSequence(start, start + length)} with color ${Integer.toHexString(color)}", ink > 3)
    }

    private fun assertWordPixelsUnchanged(first: Bitmap, second: Bitmap, view: ReadingTextView, start: Int, length: Int) {
        val bounds = wordBounds(view, start, length)
        for (y in bounds[1] until bounds[3]) for (x in bounds[0] until bounds[2]) {
            assertEquals("Read glyphs retain their exact original pixels at $x,$y", first.getPixel(x, y), second.getPixel(x, y))
        }
    }

    private fun assertPixelsEqual(message: String, first: Bitmap, second: Bitmap) = assertTrue(message, first.sameAs(second))

    private fun onMain(action: () -> Unit) = instrumentation.runOnMainSync(action)
}
