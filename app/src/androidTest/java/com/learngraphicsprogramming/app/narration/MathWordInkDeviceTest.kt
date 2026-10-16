package com.learngraphicsprogramming.app.narration

import android.content.res.Configuration
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import android.text.SpannableString
import android.text.Spanned
import android.text.style.ReplacementSpan
import android.view.View
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.LessonTechnicalText
import com.learngraphicsprogramming.app.R
import com.learngraphicsprogramming.app.math.MathExpressionLayout
import com.learngraphicsprogramming.app.math.MathTextRenderer
import java.io.File
import java.util.Locale
import kotlin.math.ceil
import kotlin.math.floor
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

/** Native glyph geometry and real published scripts, without synthesis or preference changes. */
@RunWith(AndroidJUnit4::class)
class MathWordInkDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val dark = Color.rgb(16, 17, 20)

    @Test fun knownFlatFormulaChangesInkPerSourceAtomWithoutTouchingNativeLayout() = onMain {
        for ((surface, normal) in listOf(dark to Color.WHITE, Color.WHITE to Color.BLACK)) {
            val view = view(MathTextRenderer.render("\\(6 × 100\\)"), surface, normal)
            val text = view.text
            val nativeLayout = view.layout
            val spans = spans(text)
            val dimensions = geometry(view)
            assertTrue(view.safeSupportsWordReading)
            val formula = MathTextRenderer.formulaRanges(text).single()
            val expression = MathTextRenderer.parseExpression(text.subSequence(formula.first, formula.last + 1).toString())
            val mathematical = MathExpressionLayout.create(expression, view.paint, view.paint.textSize)
            val origin = formulaOrigin(view, formula)
            fun leaf(index: Int) = requireNotNull(mathematical.childBounds(emptyList(), index)).apply {
                offset(origin.first, origin.second)
            }
            val six = leaf(0)
            val multiplication = leaf(1)
            val hundred = leaf(2)
            val original = render(view)
            val gray = LessonReadingColor.unread(surface)

            view.setUnreadWordRanges(listOf(ReadingTextRange(text.toString().indexOf('×'), text.length)), gray)
            val partial = render(view)
            assertPixelsEqual("The already read 6 retains its original ink", original, partial, six)
            assertInk(partial, multiplication, gray)
            assertInk(partial, hundred, gray)
            assertEquals("The pending product is not prematurely white", 0, countColor(partial, hundred, normal))
            save(partial, "math-flat-progress-${if (surface == dark) "dark" else "light"}.png")

            view.setUnreadWordRanges(listOf(ReadingTextRange(0, text.length)), gray)
            assertInk(render(view), six, gray)
            view.setUnreadWordRanges(emptyList(), gray)
            assertTrue(original.sameAs(render(view)))
            assertSame(text, view.text)
            assertSame(nativeLayout, view.layout)
            assertEquals(spans, spans(view.text))
            assertEquals(dimensions, geometry(view))
        }
    }

    @Test fun proseAroundSimpleFormulasContinuesWordTrackingForwardAndBack() = onMain {
        val view = view(MathTextRenderer.render("Primera palabra \\(3047=3000+0+40+7\\). Después siguen palabras pendientes."))
        assertTrue(view.safeSupportsWordReading)
        val original = render(view)
        val nativeLayout = view.layout
        val text = view.text
        val snapshot = spans(text)
        val formula = MathTextRenderer.formulaRanges(text).single()
        val gray = LessonReadingColor.unread(dark)
        val firstWord = nativeBounds(view, 0, "Primera".length)
        val pendingTail = text.toString().indexOf("Después")
        val tailBounds = nativeBounds(view, pendingTail, pendingTail + "Después".length)

        view.setUnreadWordRanges(listOf(ReadingTextRange(formula.first, text.length)), gray)
        val during = render(view)
        assertPixelsEqual("An inline formula does not disable the surrounding read words", original, during, firstWord)
        assertInk(during, tailBounds, gray)
        assertInk(during, nativeBounds(view, formula.first, formula.last + 1), gray)
        save(during, "math-prose-inline-progress.png")

        view.setUnreadWordRanges(listOf(ReadingTextRange(formula.last + 1, text.length)), gray)
        assertPixelsEqual("Completing the formula restores its original glyphs", original, render(view),
            nativeBounds(view, formula.first, formula.last + 1))
        view.setUnreadWordRanges(listOf(ReadingTextRange(0, text.length)), gray)
        assertInk(render(view), firstWord, gray)
        view.setUnreadWordRanges(null, gray)
        assertTrue(original.sameAs(render(view)))
        assertSame(nativeLayout, view.layout)
        assertSame(text, view.text)
        assertEquals(snapshot, spans(view.text))
    }

    @Test fun complexFormulaIsAtomicWhileSurroundingWordsStillTrack() = onMain {
        val view = view(MathTextRenderer.render("Antes \\(\\frac{1}{2}\\) después."))
        assertTrue(view.safeSupportsWordReading)
        val text = view.text
        val nativeLayout = view.layout
        val originalSpans = spans(text)
        val original = render(view)
        val formula = MathTextRenderer.formulaRanges(text).single()
        val bounds = nativeBounds(view, formula.first, formula.last + 1)
        val gray = LessonReadingColor.unread(dark)
        val interior = formula.first + text.subSequence(formula.first, formula.last + 1).toString().indexOf('1')

        view.setUnreadWordRanges(listOf(ReadingTextRange(interior, text.length)), gray)
        val pending = render(view)
        assertInk(pending, bounds, gray)
        assertEquals("Complex fractions remain one conservative unit, including both numerator and denominator",
            0, countColor(pending, bounds, Color.WHITE))
        assertPixelsEqual("Prose preceding the complex formula still tracks independently", original, pending,
            nativeBounds(view, 0, "Antes".length))
        view.setUnreadWordRanges(listOf(ReadingTextRange(formula.last + 1, text.length)), gray)
        assertPixelsEqual("A completed fraction retains all of its original typesetting", original, render(view), bounds)
        assertSame(text, view.text)
        assertSame(nativeLayout, view.layout)
        assertEquals(originalSpans, spans(view.text))
    }

    @Test fun unknownReplacementSpansRetainOriginalConservativeSafety() = onMain {
        val source = SpannableString("Texto X y palabras.").apply {
            setSpan(object : ReplacementSpan() {
                override fun getSize(paint: Paint, text: CharSequence, start: Int, end: Int,
                                     fm: Paint.FontMetricsInt?): Int = ceil(paint.measureText("X")).toInt()
                override fun draw(canvas: Canvas, text: CharSequence, start: Int, end: Int, x: Float,
                                  top: Int, y: Int, bottom: Int, paint: Paint) {
                    canvas.drawText("X", x, y.toFloat(), paint)
                }
            }, 6, 7, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        }
        val view = view(source)
        val original = render(view)
        assertFalse(view.safeSupportsWordReading)
        view.setUnreadWordRanges(listOf(ReadingTextRange(0, view.text.length)), LessonReadingColor.unread(dark))
        assertTrue("Unknown non-text drawing cannot gain invented word geometry", original.sameAs(render(view)))
    }

    @Test fun publishedNumberPositionParagraphMapsEveryRealSpokenSegmentAndOriginalGlyph() = onMain {
        val lesson = lesson()
        val untouched = lesson.toString()
        val blocks = lesson.getJSONArray("pages").getJSONObject(1).getJSONArray("blocks")
        val index = (0 until blocks.length()).single {
            blocks.getJSONObject(it).optString("text").startsWith("En nuestro sistema de numeración")
        }
        val source = blocks.getJSONObject(index).getString("text")
        val displayed = MathTextRenderer.render(source)
        assertTrue(MathTextRenderer.formulaRanges(displayed).size >= 2)
        val segments = segments(lesson, index, feedback = false)
        val mapping = NarrationWordMappings.forBlock(index, displayed, segments, NarrationLanguage.SPANISH)
        assertEquals("All actual spoken sentences retain their original displayed source", segments.keys, mapping.keys)
        assertMapping(displayed, segments, mapping)
        val equation = displayed.toString().indexOf("3047=3000+0+40+7")
        assertTrue(equation >= 0)
        val equationRanges = mapping.values.flatMap { it.displayed }.filter {
            it.start < equation + "3047=3000+0+40+7".length && it.endExclusive > equation
        }
        assertTrue("The flat decomposition is not one whole-paragraph owner", equationRanges.distinct().size >= 5)
        assertEquals(untouched, lesson.toString())
    }

    @Test fun publishedMathSuccessFeedbackMapsBothComparedExpressionsWithoutWholeViewOwners() = onMain {
        val lesson = lesson()
        val untouched = lesson.toString()
        val blocks = lesson.getJSONArray("pages").getJSONObject(1).getJSONArray("blocks")
        val spanish = context.createConfigurationContext(Configuration(context.resources.configuration).apply {
            setLocale(Locale("es"))
        })
        for (id in listOf("valor_posicional", "descomponer_ceros")) {
            val index = (0 until blocks.length()).single { blocks.getJSONObject(it).optString("id") == id }
            val explanation = blocks.getJSONObject(index).getJSONObject("feedback").getString("correct")
            val source = spanish.getString(R.string.math_ui_correct_heading) + "\n" + explanation
            val displayed = MathTextRenderer.render(source)
            val segments = segments(lesson, index, feedback = true)
            val mapping = NarrationPuzzleWordMappings.forViews(index, listOf(91 to displayed), segments,
                NarrationLanguage.SPANISH, feedback = true)
            assertEquals("All actual successful feedback segments map for $id", segments.keys, mapping.keys)
            assertTrue(mapping.values.all { it.targets!!.all { target -> target == 91 } })
            assertMapping(displayed, segments, mapping)
        }
        assertEquals(untouched, lesson.toString())
    }

    @Test fun semanticUnderlayCoversWholeExpressionsAcrossDifferentTypesettingBoundaries() = onMain {
        for ((source, expression) in listOf(
            "El 6 está en las centenas y aporta 6 × 100 = 600." to "6 × 100 = 600",
            "5006 = 5000 + 0 + 0 + 6. Los dos ceros mantienen centenas y decenas." to "5006 = 5000 + 0 + 0 + 6",
            "Compara 6 × 100 ≤ 700 y observa el resultado." to "6 × 100 ≤ 700"
        )) {
            val view = view(MathTextRenderer.render(source), dark, Color.rgb(180, 220, 190))
            val text = view.text
            val nativeLayout = view.layout
            val originalSpans = spans(text)
            val originalGeometry = geometry(view)
            LessonTechnicalText.apply(view, mathematics = true, language = NarrationLanguage.SPANISH)
            val decorated = render(view)
            val color = LessonReadingColor.composite((view.currentTextColor and 0x00ffffff) or (16 shl 24), dark)
            val start = text.toString().indexOf(expression)
            assertTrue(start >= 0)
            // Check each visible non-whitespace source atom's actual native cell, including the
            // relation and the result that used to fall outside the mathematical decoration.
            expression.indices.filter { !expression[it].isWhitespace() }.forEach { offset ->
                val cell = nativeBounds(view, start + offset, start + offset + 1)
                if (cell.width() > 1f) assertTrue("Decoration covers '${expression[offset]}' in $expression",
                    countCompositeColor(decorated, cell, color) > 0)
            }
            assertSame(text, view.text)
            assertSame(nativeLayout, view.layout)
            assertEquals(originalSpans, spans(view.text))
            assertEquals(originalGeometry, geometry(view))
            save(decorated, "math-complete-underlay-${if (source.startsWith("El")) "product" else if (source.startsWith("5006")) "decomposition" else "comparison"}.png")
        }
    }

    private fun lesson() = JSONObject(context.assets.open(
        "content/es/math_physics/math_course_01/numeros_y_operaciones/lesson_magnitudes_y_numeracion.json"
    ).bufferedReader().use { it.readText() })

    private fun segments(lesson: JSONObject, block: Int, feedback: Boolean): Map<Int, String> {
        val script = LessonNarrationScript.page(lesson, 1, NarrationLanguage.SPANISH, forKokoro = true)
        val parts = NarrationSegments.sentences(script.texts)
        val plan = requireNotNull(script.readingPlan(NarrationEngine.KOKORO))
        return parts.indices.filter { plan.origins[it] == NarrationTextOrigin(block, feedback) }
            .associateWith { parts[it] }.also { assertTrue(it.isNotEmpty()) }
    }

    private fun assertMapping(displayed: CharSequence, segments: Map<Int, String>,
                              mapping: Map<Int, NarrationWordMapping>) {
        val ranges = mapping.values.flatMap { it.displayed }
        assertEquals("Every original displayed character has a spoken owner", displayed.indices.toSet(),
            ranges.flatMap { it.start until it.endExclusive }.toSet())
        assertTrue("No single normalized term owns the entire view", ranges.none {
            it.start == 0 && it.endExclusive == displayed.length
        })
        mapping.forEach { (segment, value) ->
            assertEquals(NarrationSpokenWords.technicalAtoms(segments.getValue(segment), NarrationLanguage.SPANISH)
                .map { ReadingTextRange(it.start, it.endExclusive) }, value.spoken)
            assertEquals(value.spoken.size, value.displayed.size)
        }
    }

    private fun view(source: CharSequence, surface: Int = dark, ink: Int = Color.WHITE) = ReadingTextView(context).apply {
        text = source
        textSize = 28f
        setTextColor(ink)
        setBackgroundColor(surface)
        setPadding(19, 17, 13, 23)
        measure(View.MeasureSpec.makeMeasureSpec(1600, View.MeasureSpec.EXACTLY),
            View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED))
        layout(0, 0, measuredWidth, measuredHeight)
    }

    private fun nativeBounds(view: ReadingTextView, start: Int, end: Int): RectF {
        val path = Path()
        view.layout.getSelectionPath(start, end, path)
        return RectF().apply {
            path.computeBounds(this, true)
            offset(view.compoundPaddingLeft.toFloat(), view.totalPaddingTop.toFloat())
        }
    }

    private fun formulaOrigin(view: ReadingTextView, formula: IntRange): Pair<Float, Float> {
        val bounds = nativeBounds(view, formula.first, formula.last + 1)
        return bounds.left to (view.layout.getLineBaseline(view.layout.getLineForOffset(formula.first)) +
            view.totalPaddingTop).toFloat()
    }

    private fun render(view: ReadingTextView) = Bitmap.createBitmap(view.width, view.height, Bitmap.Config.ARGB_8888)
        .also { view.draw(Canvas(it)) }

    private fun pixels(bitmap: Bitmap, region: RectF, action: (Int, Int) -> Unit) {
        for (y in floor(region.top).toInt().coerceAtLeast(0) until ceil(region.bottom).toInt().coerceAtMost(bitmap.height))
            for (x in floor(region.left).toInt().coerceAtLeast(0) until ceil(region.right).toInt().coerceAtMost(bitmap.width)) action(x, y)
    }

    private fun countColor(bitmap: Bitmap, region: RectF, color: Int): Int {
        var count = 0
        pixels(bitmap, region) { x, y -> if (bitmap.getPixel(x, y) == color) count++ }
        return count
    }

    private fun countCompositeColor(bitmap: Bitmap, region: RectF, color: Int): Int {
        var count = 0
        pixels(bitmap, region) { x, y ->
            val actual = bitmap.getPixel(x, y)
            // Canvas may round the translucent underlay's channels instead of truncating them.
            if (kotlin.math.abs(Color.red(actual) - Color.red(color)) <= 1 &&
                kotlin.math.abs(Color.green(actual) - Color.green(color)) <= 1 &&
                kotlin.math.abs(Color.blue(actual) - Color.blue(color)) <= 1) count++
        }
        return count
    }

    private fun assertInk(bitmap: Bitmap, region: RectF, color: Int) =
        assertTrue("Expected actual glyph ink in $region", countColor(bitmap, region, color) > 3)

    private fun assertPixelsEqual(message: String, original: Bitmap, actual: Bitmap, region: RectF) {
        pixels(original, region) { x, y -> assertEquals("$message at $x,$y", original.getPixel(x, y), actual.getPixel(x, y)) }
    }

    private fun spans(text: CharSequence): List<List<Any>> = (text as? Spanned)?.let { content ->
        content.getSpans(0, content.length, Any::class.java).map {
            listOf(it, content.getSpanStart(it), content.getSpanEnd(it), content.getSpanFlags(it))
        }
    }.orEmpty()

    private fun geometry(view: ReadingTextView): List<Any> = listOf(view.width, view.height, view.lineCount,
        view.textSize, view.textScaleX, (0 until view.lineCount).map { listOf(view.layout.getLineBaseline(it), view.layout.getLineWidth(it)) })

    private fun save(bitmap: Bitmap, name: String) {
        val directory = File(requireNotNull(context.getExternalFilesDir(null)), "math-word-ink").apply { check(isDirectory || mkdir()) }
        File(directory, name).outputStream().use { check(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) }
    }

    private fun onMain(action: () -> Unit) = instrumentation.runOnMainSync(action)
}
