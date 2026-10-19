package com.learngraphicsprogramming.app

import android.content.Intent
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Typeface
import android.text.SpannableString
import android.text.Spanned
import android.text.style.ForegroundColorSpan
import android.text.style.StyleSpan
import android.view.View
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.learngraphicsprogramming.app.math.MathGeometryQaSupport
import com.learngraphicsprogramming.app.narration.LessonReadingColor
import com.learngraphicsprogramming.app.narration.LessonReadingTextColors
import com.learngraphicsprogramming.app.narration.NarrationLanguage
import com.learngraphicsprogramming.app.narration.ReadingBlockState
import com.learngraphicsprogramming.app.narration.ReadingTextRange
import com.learngraphicsprogramming.app.narration.ReadingTextView
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.util.UUID
import kotlin.math.ceil
import kotlin.math.floor

/** QA-only: exact source characters, existing syntax spans and native word mask together.
 * The inherited fixture restores every QA preference; no audio or user package is used. */
@RunWith(AndroidJUnit4::class)
class LessonParagraphEmphasisDeviceTest : MathGeometryQaSupport("com.learngraphicsprogramming.app.realdownloadsqa") {
    @Test fun boldAndSyntaxKeepTheirIdentityThroughForwardBackwardAndPausedWordMasks() = onMain {
        for (language in NarrationLanguage.entries) for (surface in listOf(Color.rgb(16, 16, 16), Color.WHITE)) {
            val ordinaryInk = if (surface == Color.WHITE) Color.BLACK else Color.WHITE
            val source = "Comprueba std::cout y return 0; después compara el resultado."
            val originalItalic = StyleSpan(Typeface.ITALIC)
            val input = SpannableString(source).apply {
                setSpan(originalItalic, source.indexOf("después"), source.indexOf("compara") - 1,
                    Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            }
            val view = ReadingTextView(context).apply { text = input; textSize = 18f; setTextColor(ordinaryInk); setBackgroundColor(surface) }
            LessonParagraphEmphasis.apply(view, JSONArray().put(entry("Comprueba std::cout")))
            LessonTechnicalText.apply(view, false, language)
            measure(view)
            val text = view.text as Spanned
            assertEquals(source, text.toString())
            assertTrue(text.getSpans(0, text.length, StyleSpan::class.java).any { it === originalItalic })
            assertEquals(1, text.getSpans(0, text.length, StyleSpan::class.java).count { it.style == Typeface.BOLD })
            assertTrue("Existing C++ palette remains active", text.getSpans(0, text.length, ForegroundColorSpan::class.java).isNotEmpty())
            assertTrue(view.safeSupportsWordReading)
            val originalLayout = view.layout
            val originalSpans = snapshot(text)
            val width = view.width
            val height = view.height
            val original = render(view)
            val gray = LessonReadingColor.unread(surface)
            // Use the production presentation itself: technical ink may have the console's
            // dark backing even on a light page and needs its own accessible pending ink.
            val semanticSurface = view.readingInk.effectiveReadingSurface(surface)
            val semanticGray = LessonReadingColor.unread(semanticSurface)
            assertTrue(LessonReadingColor.contrast(gray, surface) >= LessonReadingColor.MIN_CONTRAST)
            assertTrue(LessonReadingColor.contrast(semanticGray, semanticSurface) >= LessonReadingColor.MIN_CONTRAST)
            val reading = LessonReadingTextColors().apply { register(0, view) }
            fun pending(from: Int) = reading.show(mapOf(0 to ReadingBlockState.CURRENT), enabled = true,
                unreadWords = mapOf(0 to listOf(ReadingTextRange(from, source.length))))
            pending(0)
            val pending = render(view)
            assertFalse(original.sameAs(pending))
            val split = source.indexOf("std::cout")
            pending(split)
            val progressive = render(view)
            assertInk(progressive, view, 0, "Comprueba".length, ordinaryInk)
            assertInk(progressive, view, split, "std::cout".length, semanticGray)
            save(progressive, "${language.tag}-${if (surface == Color.WHITE) "light" else "dark"}-progressive.png")
            // The existing playback-position mask can return to an earlier word unchanged.
            pending(0)
            assertTrue(pending.sameAs(render(view)))
            pending(split)
            assertTrue(progressive.sameAs(render(view)))
            reading.show(mapOf(0 to ReadingBlockState.READ), enabled = true)
            assertTrue("Completion restores the exact decorated ink", original.sameAs(render(view)))
            assertSame(text, view.text)
            assertSame(originalLayout, view.layout)
            assertEquals(originalSpans, snapshot(view.text as Spanned))
            assertEquals(width, view.width)
            assertEquals(height, view.height)
            reading.clear()
        }
    }

    @Test fun missingMetadataKeepsLegacyTextObjectAndReapplyingChangesOnlyOwnedBoldSpans() = onMain {
        val source = SpannableString("main conserva su estilo y return 0;").apply {
            setSpan(StyleSpan(Typeface.ITALIC), 0, 4, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            setSpan(ForegroundColorSpan(Color.CYAN), 0, 4, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        }
        val view = ReadingTextView(context).apply { text = source }
        val legacy = view.text
        LessonParagraphEmphasis.apply(view, null)
        assertSame("Old paragraphs never acquire a copy or a style", legacy, view.text)
        val initial = snapshot(legacy as Spanned)
        repeat(3) { LessonParagraphEmphasis.apply(view, JSONArray().put(entry("return 0;"))) }
        assertEquals(1, (view.text as Spanned).getSpans(0, source.length, StyleSpan::class.java).count { it.style == Typeface.BOLD })
        LessonParagraphEmphasis.apply(view, null)
        assertEquals(source.toString(), view.text.toString())
        assertEquals(initial, snapshot(view.text as Spanned))
    }

    @Test fun realProgrammingParagraphRendererAcceptsExplicitEmphasisWithoutChangingItsLesson() {
        val asset = "content/es/programming/cpp_fundamentals/programas_y_ejecucion/lesson_primer_programa_y_salida.json"
        val sourceText = context.assets.open(asset).bufferedReader().use { it.readText() }
        val lesson = JSONObject(sourceText)
        val qaId = "cpp_fundamentals__programas_y_ejecucion__emphasis_qa_${UUID.randomUUID().toString().replace("-", "")}"
        lesson.put("id", qaId)
        val paragraph = lesson.getJSONArray("pages").getJSONObject(0).getJSONArray("blocks").getJSONObject(3)
        val originalParagraph = paragraph.getString("text")
        paragraph.put("emphasis", JSONArray().put(entry("Una biblioteca")).put(entry("std::cout").put("occurrence", 1))
            .put(entry("informar al sistema y mostrar un mensaje son acciones diferentes")))
        val originalAnswers = lesson.getJSONArray("pages").getJSONObject(0).getJSONArray("blocks").getJSONObject(4).toString()
        val intent = Intent(context, LessonActivity::class.java).putExtra("routeId", "programming")
            .putExtra("courseId", "cpp_fundamentals").putExtra("unitId", "cpp_fundamentals__programas_y_ejecucion")
            .putExtra("lessonId", "cpp_fundamentals__programas_y_ejecucion__primer_programa_y_salida")
            .putExtra("lessonNumber", 2).putExtra("lessonTotal", 5)
        ActivityScenario.launch<LessonActivity>(intent).use { scenario ->
            scenario.onActivity { activity ->
                setField(activity, "currentLessonId", qaId)
                setField(activity, "lessonJson", lesson)
                setField(activity, "currentPage", 0)
                listOf("pageViewCache", "pageRequiredExercises", "pageCompletedExercises").forEach { name ->
                    (field(activity, name) as MutableMap<*, *>).clear()
                }
                LessonActivity::class.java.getDeclaredMethod("renderCurrentPage").apply { isAccessible = true }.invoke(activity)
            }
            instrumentation.waitForIdleSync()
            scenario.onActivity { activity ->
                val view = descendants(activity.findViewById(android.R.id.content)).filterIsInstance<ReadingTextView>()
                    .single { it.text.toString() == originalParagraph }
                val spanned = view.text as Spanned
                val bold = spanned.getSpans(0, spanned.length, StyleSpan::class.java).filter { it.style == Typeface.BOLD }
                assertEquals(3, bold.size)
                assertEquals(LessonParagraphEmphasis.ranges(originalParagraph, paragraph.opt("emphasis")), bold.map {
                    LessonParagraphEmphasis.Range(spanned.getSpanStart(it), spanned.getSpanEnd(it))
                }.sortedBy { it.start })
                assertTrue(spanned.getSpans(0, spanned.length, ForegroundColorSpan::class.java).isNotEmpty())
                assertTrue(view.safeSupportsWordReading)
                assertEquals(originalAnswers, lesson.getJSONArray("pages").getJSONObject(0).getJSONArray("blocks").getJSONObject(4).toString())
                assertEquals(1, field(activity, "requiredExercises"))
                assertEquals(qaId, field(activity, "currentLessonId"))
            }
        }
        assertEquals("The packaged curricular asset is never edited", sourceText,
            context.assets.open(asset).bufferedReader().use { it.readText() })
    }

    private fun entry(text: String) = JSONObject().put("text", text)
    private fun onMain(action: () -> Unit) = instrumentation.runOnMainSync(action)
    private fun measure(view: ReadingTextView) {
        view.measure(View.MeasureSpec.makeMeasureSpec(1080, View.MeasureSpec.EXACTLY),
            View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED))
        view.layout(0, 0, view.measuredWidth, view.measuredHeight)
    }
    private fun render(view: ReadingTextView): Bitmap = Bitmap.createBitmap(view.width, view.height, Bitmap.Config.ARGB_8888)
        .also { view.draw(Canvas(it)) }
    private fun snapshot(content: Spanned) = content.getSpans(0, content.length, Any::class.java).map {
        listOf(it, content.getSpanStart(it), content.getSpanEnd(it), content.getSpanFlags(it))
    }
    private fun assertInk(bitmap: Bitmap, view: ReadingTextView, start: Int, length: Int, color: Int) {
        val layout = view.layout
        val line = layout.getLineForOffset(start)
        assertEquals(line, layout.getLineForOffset(start + length - 1))
        val left = floor(layout.getPrimaryHorizontal(start).toDouble()).toInt().coerceAtLeast(0)
        val right = ceil(layout.getPrimaryHorizontal(start + length).toDouble()).toInt().coerceAtMost(bitmap.width)
        var count = 0
        for (y in layout.getLineTop(line) until layout.getLineBottom(line).coerceAtMost(bitmap.height)) {
            for (x in left until right) if (bitmap.getPixel(x, y) == color) count++
        }
        assertTrue("Expected native glyph ink for ${view.text.subSequence(start, start + length)}", count > 3)
    }
    private fun save(bitmap: Bitmap, name: String) {
        val directory = File(requireNotNull(context.getExternalFilesDir(null)), "paragraph-emphasis-qa").apply { mkdirs() }
        File(directory, name).outputStream().use { check(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) }
    }
}
