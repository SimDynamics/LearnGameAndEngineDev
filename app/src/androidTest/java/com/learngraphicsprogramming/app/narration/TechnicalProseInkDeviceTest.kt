package com.learngraphicsprogramming.app.narration

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Typeface
import android.text.SpannableString
import android.text.Spanned
import android.text.style.BackgroundColorSpan
import android.text.style.ForegroundColorSpan
import android.text.style.ReplacementSpan
import android.text.style.StyleSpan
import android.text.style.TypefaceSpan
import android.view.View
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.CppSyntaxHighlighter
import com.learngraphicsprogramming.app.LessonTechnicalText
import com.learngraphicsprogramming.app.math.MathTextRenderer
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import kotlin.math.ceil
import kotlin.math.floor
import kotlin.math.max
import kotlin.math.min

/** Exercises only native drawing; no Activity, curricular file, preference or audio is changed. */
@RunWith(AndroidJUnit4::class)
class TechnicalProseInkDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext

    @Test fun realConsoleSyntaxColorsYieldToPendingInkAndReturnExactlyAfterReadingOrRewind() = onMain {
        val source = "#include <iostream>\nint main() { return 42; }\nstd::cout << \"Hola\";\n// comentario"
        val highlighted = CppSyntaxHighlighter.highlight(source)
        val view = view(highlighted, Color.rgb(16, 17, 20), Color.WHITE)
        val original = render(view)
        val text = view.text
        val layout = view.layout
        val spans = spans(text)
        val geometry = geometry(view)
        val gray = LessonReadingColor.unread(Color.rgb(16, 17, 20))
        val samples = listOf(
            "#include" to Color.rgb(92, 156, 231),
            "iostream" to Color.rgb(122, 195, 141),
            "main" to Color.rgb(171, 141, 227),
            "return" to Color.rgb(231, 155, 186),
            "42" to Color.rgb(224, 151, 109),
            "std" to Color.rgb(171, 141, 227),
            "Hola" to Color.rgb(122, 195, 141),
            "comentario" to Color.rgb(172, 172, 172)
        )
        for ((word, color) in samples) assertInk(original, view, source.indexOf(word), word.length, color)

        view.setUnreadWordRanges(listOf(ReadingTextRange(0, source.length)), gray)
        val pending = render(view)
        for ((word, color) in samples) {
            assertInk(pending, view, source.indexOf(word), word.length, gray)
            assertEquals("Pending ink dominates console syntax for $word", 0,
                countColor(pending, bounds(view, source.indexOf(word), word.length), color))
        }

        val cout = source.indexOf("std::cout")
        val tail = listOf(ReadingTextRange(cout, source.length))
        view.setUnreadWordRanges(tail, gray)
        val partial = render(view)
        assertPixels(original, partial, bounds(view, source.indexOf("return"), "return".length))
        assertInk(partial, view, cout, "std".length, gray)
        view.setUnreadWordRanges(tail, gray)
        assertTrue("A repeated checkpoint does not change the drawing", partial.sameAs(render(view)))

        view.setUnreadWordRanges(listOf(ReadingTextRange(source.indexOf("return"), source.length)), gray)
        val rewound = render(view)
        assertInk(rewound, view, source.indexOf("return"), "return".length, gray)
        assertPixels(original, rewound, bounds(view, source.indexOf("main"), "main".length))

        view.setUnreadWordRanges(emptyList(), gray)
        assertTrue("Every syntax color returns without replacing the text", original.sameAs(render(view)))
        assertSame(text, view.text)
        assertSame(layout, view.layout)
        assertEquals(spans, spans(view.text))
        assertEquals(geometry, geometry(view))
    }

    @Test fun overlappingIdentificationForegroundStyleAndTypefaceSpansRemainImmutableInBothThemes() = onMain {
        val source = "Texte std::cout puis x et la suite."
        val start = source.indexOf("std::cout")
        val end = start + "std::cout".length
        val accent = Color.rgb(171, 141, 227)
        for ((surface, ordinary) in listOf(Color.WHITE to Color.BLACK,
            Color.rgb(16, 17, 20) to Color.WHITE)) {
            val text = SpannableString(source).apply {
                setSpan(ForegroundColorSpan(accent), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                setSpan(TypefaceSpan("monospace"), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                setSpan(StyleSpan(Typeface.BOLD), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            }
            val view = view(text, surface, ordinary)
            val original = render(view)
            val content = view.text
            val spans = spans(content)
            val geometry = geometry(view)
            val gray = LessonReadingColor.unread(surface)
            assertTrue(LessonReadingColor.contrast(gray, surface) >= LessonReadingColor.MIN_CONTRAST)
            view.setUnreadWordRanges(listOf(ReadingTextRange(start, source.length)), gray)
            val pending = render(view)
            assertInk(pending, view, start, "std".length, gray)
            assertEquals(0, countColor(pending, bounds(view, start, "std".length), accent))
            assertPixels(original, pending, bounds(view, 0, "Texte".length))
            view.setUnreadWordRanges(listOf(ReadingTextRange(source.indexOf("puis"), source.length)), gray)
            assertPixels(original, render(view), bounds(view, start, "std::cout".length))
            view.setUnreadWordRanges(null, gray)
            assertTrue(original.sameAs(render(view)))
            assertSame(content, view.text)
            assertEquals(spans, spans(view.text))
            assertEquals(geometry, geometry(view))
        }
    }

    @Test fun backgroundsStayConservativeWhileTrustedFractionsTrackTheirExistingSingleOwner() = onMain {
        val backgroundText = SpannableString("Texte std::cout et suite.").apply {
            setSpan(BackgroundColorSpan(Color.rgb(42, 38, 57)), 6, 15, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            setSpan(ForegroundColorSpan(Color.rgb(171, 141, 227)), 6, 15, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        }
        val background = view(backgroundText, Color.rgb(16, 17, 20), Color.WHITE, wordSafe = false)
        for (view in listOf(background)) {
            assertFalse("Unsupported drawing cannot acquire guessed word ranges", view.safeSupportsWordReading)
            val original = render(view)
            val content = view.text
            val spans = spans(content)
            val geometry = geometry(view)
            view.setUnreadWordRanges(listOf(ReadingTextRange(0, content.length)), Color.GRAY)
            assertTrue(original.sameAs(render(view)))
            view.setUnreadWordRanges(null, Color.GRAY)
            assertTrue(original.sameAs(render(view)))
            assertSame(content, view.text)
            assertEquals(spans, spans(view.text))
            assertEquals(geometry, geometry(view))
        }

        // Since 14753ea trusted formula spans supply their actual AST glyph bounds.
        // A structural fraction remains one conservative owner, never guessed word atoms.
        val formula = view("", Color.rgb(16, 17, 20), Color.WHITE, wordSafe = false)
        MathTextRenderer.apply(formula, "La fraction \\(\\frac{1}{2}\\) reste lisible.")
        measure(formula)
        val content = formula.text as Spanned
        val replacement = content.getSpans(0, content.length, ReplacementSpan::class.java).single()
        assertTrue(replacement is ReadingReplacementSpan)
        assertTrue(formula.safeSupportsWordReading)
        val start = content.getSpanStart(replacement)
        val end = content.getSpanEnd(replacement)
        val original = render(formula)
        val source = content.toString()
        val layout = formula.layout
        val spans = spans(content)
        val geometry = geometry(formula)
        val gray = LessonReadingColor.unread(Color.rgb(16, 17, 20))
        val full = listOf(ReadingTextRange(0, content.length))
        formula.setUnreadWordRanges(full, gray)
        val pending = render(formula)
        assertInk(pending, formula, 0, "La".length, gray)
        assertInk(pending, formula, start, end - start, gray)
        formula.setUnreadWordRanges(listOf(ReadingTextRange(start, start + 1)), gray)
        val fractionPending = render(formula)
        assertInk(fractionPending, formula, start, end - start, gray)
        assertPixels(original, fractionPending, bounds(formula, 0, "La".length))
        formula.setUnreadWordRanges(listOf(ReadingTextRange(end - 1, end)), gray)
        assertTrue("Every source position in a complex fraction owns the same actual glyphs",
            fractionPending.sameAs(render(formula)))
        formula.setUnreadWordRanges(listOf(ReadingTextRange(end, content.length)), gray)
        assertPixels(original, render(formula), bounds(formula, start, end - start))
        formula.setUnreadWordRanges(full, gray)
        assertTrue("Rewind restores pending prose and fraction without changing the AST", pending.sameAs(render(formula)))
        formula.setUnreadWordRanges(emptyList(), gray)
        assertTrue(original.sameAs(render(formula)))
        assertSame(content, formula.text)
        assertSame(layout, formula.layout)
        assertEquals(source, formula.text.toString())
        assertEquals(spans, spans(formula.text))
        assertEquals(geometry, geometry(formula))
    }

    @Test fun semanticMathUnderlayStaysVisibleWhileOnlyPendingGlyphInkChanges() = onMain {
        val surface = Color.rgb(16, 17, 20)
        val source = "Le résultat x + 1 reste lisible."
        val start = source.indexOf("x + 1")
        val end = start + "x + 1".length
        val accent = Color.rgb(122, 195, 141)
        val content = SpannableString(source).apply {
            setSpan(ForegroundColorSpan(accent), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        }
        val view = view(content, surface, Color.WHITE)
        val original = render(view)
        val text = view.text
        val layout = view.layout
        val spans = spans(text)
        val geometry = geometry(view)
        view.setSemanticBackgroundRanges(listOf(ReadingTextRange(start, end)), Color.argb(28, 69, 142, 255))
        val decorated = render(view)
        assertFalse(original.sameAs(decorated))
        assertPixels(original, decorated, bounds(view, 0, "Le".length))
        val mathBounds = bounds(view, start, end - start)
        val backgroundPixel = decorated.getPixel((mathBounds[0] + mathBounds[2]) / 2, mathBounds[1])
        assertNotEquals(surface, backgroundPixel)
        val gray = LessonReadingColor.unread(surface)
        view.setUnreadWordRanges(listOf(ReadingTextRange(start, source.length)), gray)
        val pending = render(view)
        assertInk(pending, view, start, 1, gray)
        assertEquals("The math underlay is independent of pending glyph recoloring", backgroundPixel,
            pending.getPixel((mathBounds[0] + mathBounds[2]) / 2, mathBounds[1]))
        assertTrue(countColor(pending, mathBounds, backgroundPixel) > 10)
        view.setUnreadWordRanges(emptyList(), gray)
        assertTrue("Read math recovers its original accent over the same underlay", decorated.sameAs(render(view)))
        assertSame(text, view.text)
        assertSame(layout, view.layout)
        assertEquals(spans, spans(view.text))
        assertEquals(geometry, geometry(view))

        view.text = "Un autre contenu remplace le paragraphe."
        measure(view)
        val changed = render(view)
        view.setSemanticBackgroundRanges(emptyList(), Color.TRANSPARENT)
        assertTrue("The previous paragraph cannot leave a stale decorative range", changed.sameAs(render(view)))
    }

    @Test fun trustedCodeDelimitersHideOnlyTheirDrawingAndKeepWordTrackingSourceOffsets() = onMain {
        val source = "Lis `std::cout` puis continue."
        val open = source.indexOf('`')
        val close = source.lastIndexOf('`')
        val delimiter = TechnicalDelimiterSpan()
        val metrics = Paint.FontMetricsInt().apply { top = -40; ascent = -30; descent = 10; bottom = 15; leading = 4 }
        assertEquals(0, delimiter.getSize(Paint(), source, open, open + 1, metrics))
        assertEquals(listOf(-40, -30, 10, 15, 4), listOf(metrics.top, metrics.ascent, metrics.descent, metrics.bottom, metrics.leading))
        val view = view(source, Color.rgb(16, 17, 20), Color.WHITE)
        LessonTechnicalText.apply(view, mathematics = false, language = NarrationLanguage.FRENCH)
        measure(view)
        assertTrue(view.safeSupportsWordReading)
        val replacements = (view.text as Spanned).getSpans(0, source.length, ReplacementSpan::class.java)
        assertEquals(2, replacements.size)
        assertTrue(replacements.all { it is TechnicalDelimiterSpan })
        assertEquals(source, view.text.toString())
        assertEquals(view.layout.getPrimaryHorizontal(open), view.layout.getPrimaryHorizontal(open + 1), .01f)
        assertEquals(view.layout.getPrimaryHorizontal(close), view.layout.getPrimaryHorizontal(close + 1), .01f)
        val withoutDelimiters = this.view("Lis std::cout puis continue.", Color.rgb(16, 17, 20), Color.WHITE)
        assertEquals(withoutDelimiters.height, view.height)
        assertEquals(withoutDelimiters.layout.getLineWidth(0), view.layout.getLineWidth(0), .01f)
        val original = render(view)
        val text = view.text
        val layout = view.layout
        val spans = spans(text)
        val geometry = geometry(view)
        val gray = LessonReadingColor.unread(Color.rgb(16, 17, 20))
        view.setUnreadWordRanges(listOf(ReadingTextRange(open, source.length)), gray)
        assertInk(render(view), view, open + 1, 3, gray)
        view.setUnreadWordRanges(listOf(ReadingTextRange(close + 1, source.length)), gray)
        assertPixels(original, render(view), bounds(view, open + 1, "std::cout".length))
        view.setUnreadWordRanges(listOf(ReadingTextRange(open, source.length)), gray)
        assertInk(render(view), view, open + 1, 3, gray)
        view.setUnreadWordRanges(null, gray)
        assertTrue(original.sameAs(render(view)))
        assertSame(text, view.text)
        assertSame(layout, view.layout)
        assertEquals(spans, spans(view.text))
        assertEquals(geometry, geometry(view))
    }

    @Test fun silentSpanishInlineOperatorRetainsItsNativeGeometryColorAndVisibleSource() = onMain {
        val source = "Lee el operador `<<` y continúa."
        val operator = source.indexOf("<<")
        val open = operator - 1
        val close = operator + 2
        val view = view(source, Color.rgb(16, 17, 20), Color.WHITE)
        LessonTechnicalText.apply(view, mathematics = false, language = NarrationLanguage.SPANISH)
        measure(view)
        val original = render(view)
        val text = view.text
        val layout = view.layout
        val spans = spans(text)
        val geometry = geometry(view)
        val replacements = (text as Spanned).getSpans(0, source.length, TechnicalDelimiterSpan::class.java)
        assertEquals(2, replacements.size)
        assertEquals(source, text.toString())
        assertEquals(layout.getPrimaryHorizontal(open), layout.getPrimaryHorizontal(operator), .01f)
        assertEquals(layout.getPrimaryHorizontal(close), layout.getPrimaryHorizontal(close + 1), .01f)
        val plain = this.view(source.replace("`", ""), Color.rgb(16, 17, 20), Color.WHITE)
        assertEquals(plain.height, view.height)
        assertEquals(plain.layout.getLineWidth(0), layout.getLineWidth(0), .01f)
        val prepared = requireNotNull(NarrationSourceProjection.spanish(text.toString(), "auto"))
        assertEquals("Lee el operador doble menor que y continúa.", ManualEnglishSpeech.strip(prepared.text))
        val operatorOwners = prepared.owners.filter { it.start >= operator && it.endExclusive <= close && it.start < it.endExclusive }
        assertTrue("The spoken operator still owns its two visible glyphs", operatorOwners.isNotEmpty())
        assertTrue(operatorOwners.all { it.start == operator && it.endExclusive == close })
        val gray = LessonReadingColor.unread(Color.rgb(16, 17, 20))
        view.setUnreadWordRanges(listOf(ReadingTextRange(operator, close)), gray)
        assertInk(render(view), view, operator, 2, gray)
        view.setUnreadWordRanges(null, gray)
        assertTrue(original.sameAs(render(view)))
        assertSame(text, view.text)
        assertSame(layout, view.layout)
        assertEquals(spans, spans(view.text))
        assertEquals(geometry, geometry(view))
    }

    @Test fun missingWordAlignmentKeepsSyntaxColoredProsePendingUntilReadOrDisabled() = onMain {
        val surface = Color.rgb(16, 17, 20)
        val source = "Lis std::cout puis continue."
        val code = "std::cout"
        val start = source.indexOf(code)
        val syntax = CppSyntaxHighlighter.highlight(code)
        val content = SpannableString(source).apply {
            syntax.getSpans(0, syntax.length, ForegroundColorSpan::class.java).forEach { span ->
                setSpan(span, start + syntax.getSpanStart(span), start + syntax.getSpanEnd(span), syntax.getSpanFlags(span))
            }
        }
        val view = view(content, surface, Color.WHITE)
        val original = render(view)
        val text = view.text
        val layout = view.layout
        val spans = spans(text)
        val geometry = geometry(view)
        val reading = LessonReadingTextColors().apply { register(0, view) }
        val gray = LessonReadingColor.unread(surface)
        for (state in listOf(ReadingBlockState.UNREAD, ReadingBlockState.CURRENT)) {
            reading.show(mapOf(0 to state), enabled = true, unreadWords = emptyMap())
            val pending = render(view)
            assertInk(pending, view, 0, "Lis".length, gray)
            assertInk(pending, view, start, "std".length, gray)
            assertEquals("A missing alignment cannot expose original syntax ink in $state", 0,
                countColor(pending, bounds(view, start, "std".length), Color.rgb(171, 141, 227)))
            reading.show(mapOf(0 to state), enabled = false, unreadWords = emptyMap())
            assertTrue("Disabling restores the syntax palette without changing content", original.sameAs(render(view)))
        }
        reading.show(mapOf(0 to ReadingBlockState.UNREAD), enabled = true, unreadWords = emptyMap())
        reading.show(mapOf(0 to ReadingBlockState.READ), enabled = true, unreadWords = emptyMap())
        assertTrue("A read block recovers every original syntax pixel", original.sameAs(render(view)))
        assertSame(text, view.text)
        assertSame(layout, view.layout)
        assertEquals(spans, spans(view.text))
        assertEquals(geometry, geometry(view))
        reading.clear()
    }

    @Test fun lightProseAndDarkCppCapsuleUseSeparateAccessiblePendingInkAndRestoreSyntaxExactly() = onMain {
        val source = "Lis `std::cout` puis continue."
        val code = source.indexOf("std::cout")
        val tail = source.indexOf("puis")
        val view = view(source, Color.WHITE, Color.BLACK)
        LessonTechnicalText.apply(view, mathematics = false, language = NarrationLanguage.FRENCH)
        measure(view)
        assertTrue(view.safeSupportsWordReading)
        val semanticSurface = view.effectiveReadingSurface(Color.WHITE)
        assertEquals("The original console palette has its readable dark backing in a light lesson",
            Color.rgb(31, 35, 48), semanticSurface)
        val baseGray = LessonReadingColor.unread(Color.WHITE)
        val semanticGray = LessonReadingColor.unread(semanticSurface)
        assertNotEquals(baseGray, semanticGray)
        assertTrue(LessonReadingColor.contrast(baseGray, Color.WHITE) >= LessonReadingColor.MIN_CONTRAST)
        assertTrue(LessonReadingColor.contrast(semanticGray, semanticSurface) >= LessonReadingColor.MIN_CONTRAST)
        val original = render(view)
        val text = view.text
        val layout = view.layout
        val spans = spans(text)
        val geometry = geometry(view)
        val reading = LessonReadingTextColors().apply { register(0, view) }
        val full = mapOf(0 to listOf(ReadingTextRange(0, source.length)))
        reading.show(mapOf(0 to ReadingBlockState.CURRENT), enabled = true, unreadWords = full)
        val pending = render(view)
        assertInk(pending, view, 0, "Lis".length, baseGray)
        assertInk(pending, view, code, "std".length, semanticGray)
        assertInk(pending, view, tail, "puis".length, baseGray)
        assertEquals(0, countColor(pending, bounds(view, code, "std".length), baseGray))
        // Compare with ordinary prose through its own pending-ink path, without a technical capsule.
        // Direct gray text has different compositing and cannot identify incidental antialias colors.
        val proseReference = this.view(source, Color.WHITE, Color.BLACK)
        val proseReading = LessonReadingTextColors().apply { register(0, proseReference) }
        proseReading.show(mapOf(0 to ReadingBlockState.CURRENT), enabled = true, unreadWords = full)
        val proseBounds = bounds(view, 0, "Lis".length)
        assertEquals(view.layout.getLineBaseline(0), proseReference.layout.getLineBaseline(0))
        assertEquals(view.layout.getPrimaryHorizontal(0).toBits(), proseReference.layout.getPrimaryHorizontal(0).toBits())
        assertEquals(view.layout.getPrimaryHorizontal("Lis".length).toBits(),
            proseReference.layout.getPrimaryHorizontal("Lis".length).toBits())
        assertArrayEquals(proseBounds, bounds(proseReference, 0, "Lis".length))
        assertPixels(render(proseReference), pending, proseBounds)
        proseReading.clear()
        val codeBounds = bounds(view, code, "std".length)
        assertEquals("Gray glyph filters cannot recolor the console backing", semanticSurface,
            pending.getPixel((codeBounds[0] + codeBounds[2]) / 2, codeBounds[1]))

        reading.show(mapOf(0 to ReadingBlockState.CURRENT), enabled = true,
            unreadWords = mapOf(0 to listOf(ReadingTextRange(tail, source.length))))
        val partial = render(view)
        assertPixels(original, partial, bounds(view, code, "std::cout".length))
        assertPixels(original, partial, bounds(view, 0, "Lis".length))
        assertInk(partial, view, tail, "puis".length, baseGray)
        reading.show(mapOf(0 to ReadingBlockState.CURRENT), enabled = true, unreadWords = full)
        assertTrue("Rewinding restores both pending surfaces consistently", pending.sameAs(render(view)))
        reading.show(mapOf(0 to ReadingBlockState.READ), enabled = true, unreadWords = emptyMap())
        assertTrue("Read syntax keeps every original console palette pixel", original.sameAs(render(view)))
        reading.show(mapOf(0 to ReadingBlockState.CURRENT), enabled = true, unreadWords = full)
        reading.show(mapOf(0 to ReadingBlockState.CURRENT), enabled = false)
        assertTrue(original.sameAs(render(view)))
        assertSame(text, view.text)
        assertSame(layout, view.layout)
        assertEquals(spans, spans(view.text))
        assertEquals(geometry, geometry(view))
        reading.clear()
    }

    @Test fun ordinaryAndExplicitTechnicalSemicolonsKeepDifferentIdentificationWhileBothTrackReading() = onMain {
        val source = "Primero ; después sigue. No añadas ; a cualquier línea."
        val ordinary = source.indexOf(';')
        val technical = source.lastIndexOf(';')
        val surface = Color.rgb(16, 17, 20)
        val view = view(source, surface, Color.WHITE)
        LessonTechnicalText.apply(view, mathematics = false, language = NarrationLanguage.SPANISH)
        measure(view)
        val content = view.text
        val styled = content as Spanned
        val semantic = semanticRanges(view)
        assertFalse("Ordinary clause punctuation cannot acquire the code underlay",
            semantic.any { it.start <= ordinary && it.endExclusive > ordinary })
        assertTrue("An explicit symbol mention keeps its existing code identification",
            semantic.any { it.start <= technical && it.endExclusive > technical })
        assertTrue(styled.getSpans(ordinary, ordinary + 1, ForegroundColorSpan::class.java).isEmpty())
        assertTrue(styled.getSpans(technical, technical + 1, ForegroundColorSpan::class.java).isNotEmpty())
        assertEquals(source, content.toString())
        assertTrue(view.safeSupportsWordReading)
        val original = render(view)
        val layout = view.layout
        val spans = spans(content)
        val geometry = geometry(view)
        val gray = LessonReadingColor.unread(surface)
        val full = listOf(ReadingTextRange(0, source.length))
        view.setUnreadWordRanges(full, gray)
        val pending = render(view)
        assertInk(pending, view, ordinary, 1, gray)
        assertInk(pending, view, technical, 1, gray)
        view.setUnreadWordRanges(listOf(ReadingTextRange(ordinary + 1, source.length)), gray)
        val partial = render(view)
        assertPixels(original, partial, bounds(view, ordinary, 1))
        assertInk(partial, view, technical, 1, gray)
        view.setUnreadWordRanges(full, gray)
        assertTrue("A rewind shades both punctuation owners again", pending.sameAs(render(view)))
        view.setUnreadWordRanges(emptyList(), gray)
        assertTrue(original.sameAs(render(view)))
        assertSame(content, view.text)
        assertSame(layout, view.layout)
        assertEquals(spans, spans(view.text))
        assertEquals(geometry, geometry(view))
        assertEquals(semantic, semanticRanges(view))
    }

    @Test fun renderedFileNameWithMathCharactersKeepsOnlyTheAdjacentRealEquationUnderlaidAndTracked() = onMain {
        val source = "El archivo src/6+3=9 conserva su nombre; la suma 2 + 3 = 5 es correcta."
        val fileStart = source.indexOf("src/6+3=9")
        val fileEnd = fileStart + "src/6+3=9".length
        val mathStart = source.indexOf("2 + 3 = 5")
        val mathEnd = mathStart + "2 + 3 = 5".length
        val surface = Color.rgb(16, 17, 20)
        val view = view(source, surface, Color.WHITE)
        MathTextRenderer.apply(view, source)
        LessonTechnicalText.apply(view, mathematics = true, language = NarrationLanguage.SPANISH)
        measure(view)
        assertEquals(source, view.text.toString())
        val semantic = semanticRanges(view)
        assertFalse("Filename characters must not become a mathematical underlay",
            semantic.any { it.start < fileEnd && it.endExclusive > fileStart })
        for (position in mathStart until mathEnd) {
            assertTrue("The real equation must retain complete semantic ownership at $position",
                semantic.any { it.start <= position && it.endExclusive > position })
        }
        assertFalse("The native formula renderer must not typeset a filename suffix as an equation",
            MathTextRenderer.formulaRanges(view.text).any { it.first < fileEnd && it.last >= fileStart })
        assertTrue(MathTextRenderer.formulaRanges(view.text).any { it.first >= mathStart && it.last < mathEnd })
        assertTrue(view.safeSupportsWordReading)
        val content = view.text
        val original = render(view)
        val layout = view.layout
        val spans = spans(content)
        val geometry = geometry(view)
        val gray = LessonReadingColor.unread(surface)
        val full = listOf(ReadingTextRange(0, source.length))
        view.setUnreadWordRanges(full, gray)
        val pending = render(view)
        assertInk(pending, view, fileStart, 3, gray)
        view.setUnreadWordRanges(listOf(ReadingTextRange(mathStart, source.length)), gray)
        val partial = render(view)
        assertPixels(original, partial, bounds(view, fileStart, "src/6+3=9".length))
        view.setUnreadWordRanges(full, gray)
        assertTrue("Rewinding keeps the semantic decoration independent of pending ink", pending.sameAs(render(view)))
        view.setUnreadWordRanges(emptyList(), gray)
        assertTrue(original.sameAs(render(view)))
        assertSame(content, view.text)
        assertSame(layout, view.layout)
        assertEquals(spans, spans(view.text))
        assertEquals(geometry, geometry(view))
        assertEquals(semantic, semanticRanges(view))
    }

    @Suppress("UNCHECKED_CAST")
    private fun semanticRanges(view: ReadingTextView): List<ReadingTextRange> =
        (ReadingTextInk::class.java.getDeclaredField("semanticBackgroundRanges").apply {
            isAccessible = true
        }.get(view.readingInk) as List<ReadingTextRange>).toList()

    private fun view(text: CharSequence, surface: Int, color: Int, wordSafe: Boolean = true) = ReadingTextView(context).apply {
        layoutParams = android.view.ViewGroup.LayoutParams(android.view.ViewGroup.LayoutParams.MATCH_PARENT,
            android.view.ViewGroup.LayoutParams.WRAP_CONTENT)
        this.text = text
        textSize = 25f
        setTextColor(color)
        setBackgroundColor(surface)
        setPadding(19, 17, 13, 23)
        measure(this)
        assertEquals(wordSafe, safeSupportsWordReading)
    }

    private fun measure(view: ReadingTextView) {
        view.measure(View.MeasureSpec.makeMeasureSpec(1400, View.MeasureSpec.EXACTLY),
            View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED))
        view.layout(0, 0, view.measuredWidth, view.measuredHeight)
    }

    private fun render(view: ReadingTextView) = Bitmap.createBitmap(view.width, view.height, Bitmap.Config.ARGB_8888)
        .also { view.draw(Canvas(it)) }

    private fun geometry(view: ReadingTextView): List<Any> = listOf(view.width, view.height, view.lineCount,
        view.paddingLeft, view.paddingTop, view.paddingRight, view.paddingBottom, view.textSize, view.textScaleX,
        (0 until view.lineCount).map { view.layout.getLineBaseline(it) })

    private fun spans(text: CharSequence): List<List<Any>> = (text as? Spanned)?.let { spanned ->
        spanned.getSpans(0, text.length, Any::class.java).map {
            listOf(it, spanned.getSpanStart(it), spanned.getSpanEnd(it), spanned.getSpanFlags(it))
        }
    }.orEmpty()

    private fun bounds(view: ReadingTextView, start: Int, length: Int): IntArray {
        val layout = view.layout
        val line = layout.getLineForOffset(start)
        assertEquals(line, layout.getLineForOffset(start + length - 1))
        val first = layout.getPrimaryHorizontal(start)
        val last = layout.getPrimaryHorizontal(start + length)
        return intArrayOf(floor(min(first, last) + view.compoundPaddingLeft).toInt(),
            layout.getLineTop(line) + view.totalPaddingTop,
            ceil(max(first, last) + view.compoundPaddingLeft).toInt(), layout.getLineBottom(line) + view.totalPaddingTop)
    }

    private fun countColor(bitmap: Bitmap, bounds: IntArray, color: Int): Int {
        var count = 0
        for (y in bounds[1] until bounds[3]) for (x in bounds[0] until bounds[2]) if (bitmap.getPixel(x, y) == color) count++
        return count
    }

    private fun assertInk(bitmap: Bitmap, view: ReadingTextView, start: Int, length: Int, color: Int) {
        assertTrue("Expected ink in ${view.text.subSequence(start, start + length)} with ${Integer.toHexString(color)}",
            countColor(bitmap, bounds(view, start, length), color) > 3)
    }

    private fun assertPixels(first: Bitmap, second: Bitmap, bounds: IntArray) {
        for (y in bounds[1] until bounds[3]) for (x in bounds[0] until bounds[2]) assertEquals(first.getPixel(x, y), second.getPixel(x, y))
    }

    private fun onMain(action: () -> Unit) = instrumentation.runOnMainSync(action)
}
