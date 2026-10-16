package com.learngraphicsprogramming.app

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Typeface
import android.text.SpannableString
import android.text.Spanned
import android.text.style.ForegroundColorSpan
import android.text.style.MetricAffectingSpan
import android.text.style.ReplacementSpan
import android.text.style.StyleSpan
import android.view.View
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.math.MathTextRenderer
import com.learngraphicsprogramming.app.narration.LessonReadingColor
import com.learngraphicsprogramming.app.narration.NarrationLanguage
import com.learngraphicsprogramming.app.narration.ProgrammingTechnicalRanges
import com.learngraphicsprogramming.app.narration.ReadingTextRange
import com.learngraphicsprogramming.app.narration.ReadingTextView
import com.learngraphicsprogramming.app.narration.TechnicalDelimiterSpan
import java.io.File
import kotlin.math.ceil
import kotlin.math.floor
import kotlin.math.max
import kotlin.math.min
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

/** Real LessonTechnicalText integration over native layouts; no content, audio or preferences mutate. */
@RunWith(AndroidJUnit4::class)
class TechnicalTextIntegrationDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val dark = Color.rgb(16, 17, 20)

    @Test fun mathematicalParagraphsAndTitlesKeepTheirFormulaSpansGeometryAndOrdinaryInk() = onMain {
        for ((surface, ink, theme) in listOf(Triple(dark, Color.WHITE, "dark"), Triple(Color.WHITE, Color.BLACK, "light"))) {
            for (heading in listOf(false, true)) {
                val source = if (heading) "Proporción \\(\\frac{3}{5}\\) y x²" else
                    "La fraction \\(\\frac{3}{5}\\) reste lisible et x² conserve sa forme."
                val content = SpannableString(MathTextRenderer.render(source)).apply {
                    setSpan(StyleSpan(Typeface.ITALIC), 0, 2, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                }
                val view = view(content, surface, ink, heading)
                val originalText = view.text
                val beforeSpans = spans(originalText)
                val beforeGeometry = geometry(view)
                val before = render(view)
                val formulas = MathTextRenderer.formulaRanges(originalText)
                assertTrue("The real renderer must have produced formula replacement spans", formulas.isNotEmpty())
                assertTrue((originalText as Spanned).getSpans(0, originalText.length, ForegroundColorSpan::class.java).isEmpty())

                LessonTechnicalText.apply(view, mathematics = true, language = NarrationLanguage.FRENCH)
                measure(view)
                val decorated = render(view)
                assertSame(originalText, view.text)
                assertEquals(originalText.toString(), view.text.toString())
                assertEquals(beforeSpans, spans(view.text))
                assertEquals(beforeGeometry, geometry(view))
                assertEquals(formulas, MathTextRenderer.formulaRanges(view.text))
                assertTrue((view.text as Spanned).getSpans(0, view.text.length, ForegroundColorSpan::class.java).isEmpty())
                assertFalse("The subtle mathematical underlay must actually draw", before.sameAs(decorated))
                assertTrue("Ordinary mathematical ink remains its original color", countColor(decorated, ink) > 10)
                assertFalse("Formula drawing retains its existing conservative word-tracking behavior", view.safeSupportsWordReading)
                view.setUnreadWordRanges(listOf(ReadingTextRange(0, view.text.length)), Color.GRAY)
                assertTrue("Unsupported formula spans must not acquire guessed visual word times", decorated.sameAs(render(view)))
                save(decorated, "math-${if (heading) "title" else "paragraph"}-$theme.png")
            }
        }
    }

    @Test fun cppProseUsesExactlyTheConsolePaletteAndRestoresItAfterPendingAndRewind() = onMain {
        val source = "La ligne #include <iostream> prépare main. std::cout << \"Bonjour\"; int valeur = 42; return 0;"
        val existingStyle = StyleSpan(Typeface.ITALIC)
        val existingForeground = ForegroundColorSpan(Color.rgb(209, 215, 223))
        val content = SpannableString(source).apply {
            setSpan(existingStyle, 0, 2, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            setSpan(existingForeground, 0, 2, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        }
        val view = view(content, dark, Color.WHITE)
        val beforeGeometry = geometry(view)
        val beforeMetricSpans = (view.text as Spanned).getSpans(0, source.length, MetricAffectingSpan::class.java).toList()
        val analysis = ProgrammingTechnicalRanges.analyze(source, NarrationLanguage.FRENCH)
        assertTrue(analysis.fragments.isNotEmpty())
        assertTrue(analysis.wrappers.isEmpty())

        LessonTechnicalText.apply(view, mathematics = false, language = NarrationLanguage.FRENCH)
        measure(view)
        val once = spans(view.text).map { it.drop(1) }
        LessonTechnicalText.apply(view, mathematics = false, language = NarrationLanguage.FRENCH)
        measure(view)
        assertEquals("Registering a view again must not accumulate presentation spans", once, spans(view.text).map { it.drop(1) })
        assertEquals(source, view.text.toString())
        assertEquals(beforeGeometry, geometry(view))
        val styled = view.text as Spanned
        assertEquals(0, styled.getSpanStart(existingStyle))
        assertEquals(2, styled.getSpanEnd(existingStyle))
        assertEquals(0, styled.getSpanStart(existingForeground))
        assertEquals(2, styled.getSpanEnd(existingForeground))
        assertEquals(beforeMetricSpans, styled.getSpans(0, source.length, MetricAffectingSpan::class.java).toList())
        val expectedColors = mutableSetOf<Int>()
        for (fragment in analysis.fragments) {
            val console = CppSyntaxHighlighter.highlight(source.substring(fragment.start, fragment.endExclusive))
            for (span in console.getSpans(0, console.length, ForegroundColorSpan::class.java)) {
                val start = fragment.start + console.getSpanStart(span)
                val end = fragment.start + console.getSpanEnd(span)
                expectedColors += span.foregroundColor
                assertTrue("The real console palette must be reused at source offsets $start..$end",
                    styled.getSpans(start, end, ForegroundColorSpan::class.java).any {
                        styled.getSpanStart(it) == start && styled.getSpanEnd(it) == end && it.foregroundColor == span.foregroundColor
                    })
            }
        }
        assertTrue("Exercise actual preprocessor, identifier, string and literal colors", expectedColors.size >= 4)
        assertTrue(view.safeSupportsWordReading)
        val originalText = view.text
        val originalLayout = view.layout
        val originalSpans = spans(view.text)
        val decorated = render(view)
        val gray = LessonReadingColor.unread(view.effectiveReadingSurface(dark))
        val std = source.indexOf("std::cout")
        view.setUnreadWordRanges(listOf(ReadingTextRange(0, source.length)), gray)
        val pending = render(view)
        assertInk(pending, view, std, 3, gray)
        expectedColors.forEach { assertEquals("Pending ink dominates every syntax color", 0, countColor(pending, it)) }
        view.setUnreadWordRanges(listOf(ReadingTextRange(std + "std::cout".length, source.length)), gray)
        val progressed = render(view)
        assertPixels(decorated, progressed, bounds(view, std, "std::cout".length))
        view.setUnreadWordRanges(listOf(ReadingTextRange(std, source.length)), gray)
        val rewound = render(view)
        assertInk(rewound, view, std, 3, gray)
        view.setUnreadWordRanges(emptyList(), gray)
        assertTrue(decorated.sameAs(render(view)))
        assertSame(originalText, view.text)
        assertSame(originalLayout, view.layout)
        assertEquals(originalSpans, spans(view.text))
        assertEquals(beforeGeometry, geometry(view))
        save(decorated, "cpp-read-dark.png")
        save(pending, "cpp-pending-dark.png")
        save(rewound, "cpp-rewound-dark.png")
    }

    @Test fun trustedBackticksDisappearOnlyVisuallyAndKeepTheirOriginalOffsetsAndWordTracking() = onMain {
        val source = "Lis `std::cout` puis `return 0;`."
        val view = view(source, dark, Color.WHITE)
        val ordinaryHeight = view.height
        LessonTechnicalText.apply(view, mathematics = false, language = NarrationLanguage.FRENCH)
        measure(view)
        assertEquals(source, view.text.toString())
        assertEquals(source.length, view.text.length)
        assertEquals(ordinaryHeight, view.height)
        val styled = view.text as Spanned
        val delimiters = styled.getSpans(0, source.length, TechnicalDelimiterSpan::class.java)
        assertEquals(4, delimiters.size)
        assertEquals(4, styled.getSpans(0, source.length, ReplacementSpan::class.java).size)
        delimiters.forEach { delimiter ->
            val start = styled.getSpanStart(delimiter)
            val end = styled.getSpanEnd(delimiter)
            assertEquals("`", source.substring(start, end))
            assertEquals(view.layout.getPrimaryHorizontal(start), view.layout.getPrimaryHorizontal(end), .01f)
        }
        val withoutWrappers = this.view(source.replace("`", ""), dark, Color.WHITE)
        assertEquals(withoutWrappers.height, view.height)
        assertEquals(withoutWrappers.layout.getLineWidth(0), view.layout.getLineWidth(0), .01f)
        assertTrue(view.safeSupportsWordReading)
        val start = source.indexOf("std::cout")
        val before = render(view)
        val originalSpans = spans(view.text)
        val gray = LessonReadingColor.unread(view.effectiveReadingSurface(dark))
        view.setUnreadWordRanges(listOf(ReadingTextRange(start, start + "std::cout".length)), gray)
        assertInk(render(view), view, start, 3, gray)
        view.setUnreadWordRanges(null, gray)
        assertTrue(before.sameAs(render(view)))
        assertEquals(originalSpans, spans(view.text))
        save(before, "cpp-inline-delimiters-dark.png")
    }

    @Test fun explicitTextModeAndOrdinaryProseAcquireNoMathDecorationOrCppSyntax() = onMain {
        for ((source, mode) in listOf(
            "Texte x² et \\(\\frac{1}{2}\\), sans interprétation." to "text",
            "Une explication ordinaire et std::cout." to "auto"
        )) {
            val view = view(MathTextRenderer.render(source, mode), dark, Color.WHITE)
            val before = render(view)
            val original = view.text
            val beforeGeometry = geometry(view)
            val beforeSpans = spans(view.text)
            LessonTechnicalText.apply(view, mathematics = true, language = NarrationLanguage.FRENCH, mode = mode)
            measure(view)
            assertSame(original, view.text)
            assertEquals(beforeGeometry, geometry(view))
            assertEquals(beforeSpans, spans(view.text))
            assertTrue("No guessed mathematics or C++ colors in plain text", before.sameAs(render(view)))
        }
    }

    @Test fun mathematicalFormattingHidesOnlyOuterBackticksAndPreservesFormulaOwnedGlyphs() = onMain {
        val source = "Calcule `x²` puis \\(\\frac{3}{5}\\)."
        val view = view(MathTextRenderer.render(source), dark, Color.WHITE)
        val original = view.text
        val formulas = MathTextRenderer.formulaRanges(original)
        val originalReplacements = (original as Spanned).getSpans(0, original.length, ReplacementSpan::class.java).toList()
        LessonTechnicalText.apply(view, mathematics = true, language = NarrationLanguage.FRENCH)
        measure(view)
        assertEquals(original.toString(), view.text.toString())
        assertEquals(formulas, MathTextRenderer.formulaRanges(view.text))
        val styled = view.text as Spanned
        originalReplacements.forEach { assertEquals(original.getSpanStart(it), styled.getSpanStart(it)) }
        assertEquals(2, styled.getSpans(0, styled.length, TechnicalDelimiterSpan::class.java).size)
        assertTrue(styled.getSpans(0, styled.length, ForegroundColorSpan::class.java).isEmpty())
        styled.getSpans(0, styled.length, TechnicalDelimiterSpan::class.java).forEach {
            assertEquals(view.layout.getPrimaryHorizontal(styled.getSpanStart(it)),
                view.layout.getPrimaryHorizontal(styled.getSpanEnd(it)), .01f)
        }
        save(render(view), "math-formatting-dark.png")
    }

    private fun view(content: CharSequence, surface: Int, ink: Int, heading: Boolean = false) = ReadingTextView(context).apply {
        layoutParams = android.view.ViewGroup.LayoutParams(android.view.ViewGroup.LayoutParams.MATCH_PARENT,
            android.view.ViewGroup.LayoutParams.WRAP_CONTENT)
        text = content
        textSize = if (heading) 32f else 25f
        if (heading) typeface = Typeface.DEFAULT_BOLD
        setTextColor(ink)
        setBackgroundColor(surface)
        setPadding(19, 17, 13, 23)
        measure(this)
    }

    private fun measure(view: ReadingTextView) {
        view.measure(View.MeasureSpec.makeMeasureSpec(1600, View.MeasureSpec.EXACTLY),
            View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED))
        view.layout(0, 0, view.measuredWidth, view.measuredHeight)
    }

    private fun render(view: ReadingTextView) = Bitmap.createBitmap(view.width, view.height, Bitmap.Config.ARGB_8888)
        .also { view.draw(Canvas(it)) }

    private fun geometry(view: ReadingTextView): List<Any> = listOf(view.width, view.height, view.lineCount,
        view.paddingLeft, view.paddingTop, view.paddingRight, view.paddingBottom, view.textSize, view.textScaleX,
        (0 until view.lineCount).map { listOf(view.layout.getLineBaseline(it), view.layout.getLineWidth(it)) })

    private fun spans(text: CharSequence): List<List<Any>> = (text as? Spanned)?.let { value ->
        value.getSpans(0, value.length, Any::class.java).map { listOf(it, value.getSpanStart(it), value.getSpanEnd(it), value.getSpanFlags(it)) }
    }.orEmpty()

    private fun bounds(view: ReadingTextView, start: Int, length: Int): IntArray {
        val line = view.layout.getLineForOffset(start)
        assertEquals(line, view.layout.getLineForOffset(start + length - 1))
        val first = view.layout.getPrimaryHorizontal(start)
        val last = view.layout.getPrimaryHorizontal(start + length)
        return intArrayOf(floor(min(first, last) + view.compoundPaddingLeft).toInt(),
            view.layout.getLineTop(line) + view.totalPaddingTop,
            ceil(max(first, last) + view.compoundPaddingLeft).toInt(), view.layout.getLineBottom(line) + view.totalPaddingTop)
    }

    private fun countColor(bitmap: Bitmap, color: Int): Int {
        var count = 0
        for (y in 0 until bitmap.height) for (x in 0 until bitmap.width) if (bitmap.getPixel(x, y) == color) count++
        return count
    }

    private fun assertInk(bitmap: Bitmap, view: ReadingTextView, start: Int, length: Int, color: Int) {
        val region = bounds(view, start, length)
        var count = 0
        for (y in region[1] until region[3]) for (x in region[0] until region[2]) if (bitmap.getPixel(x, y) == color) count++
        assertTrue("Expected actual ink for ${view.text.subSequence(start, start + length)}", count > 3)
    }

    private fun assertPixels(first: Bitmap, second: Bitmap, region: IntArray) {
        for (y in region[1] until region[3]) for (x in region[0] until region[2])
            assertEquals(first.getPixel(x, y), second.getPixel(x, y))
    }

    private fun save(bitmap: Bitmap, name: String) {
        val directory = File(requireNotNull(context.getExternalFilesDir(null)), "technical-text-integration").apply { check(isDirectory || mkdir()) }
        File(directory, name).outputStream().use { check(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) }
    }

    private fun onMain(action: () -> Unit) {
        assertEquals("Only the isolated QA application may run these integration tests",
            "com.learngraphicsprogramming.app.realdownloadsqa", BuildConfig.APPLICATION_ID)
        instrumentation.runOnMainSync(action)
    }
}
