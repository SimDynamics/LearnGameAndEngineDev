package com.learngraphicsprogramming.app.narration

import android.graphics.Bitmap
import android.graphics.Canvas
import android.text.Spanned
import android.text.style.ForegroundColorSpan
import android.view.View
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.BuildConfig
import com.learngraphicsprogramming.app.LessonRepository
import com.learngraphicsprogramming.app.LessonTechnicalText
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

/** Android extraction and rendering only; real neural playback has its own isolated test. */
@RunWith(AndroidJUnit4::class)
class SilentInlineCodeHonorDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext

    @Test fun currentCorrectFeedbackKeepsItsGateAndReadsTheOperatorWithoutDelimiterAnnouncements() {
        requireQa()
        val lesson = targetLesson()
        val original = lesson.toString()
        val block = targetBlock(lesson)
        assertEquals("literal_y_operador", block.getString("id"))
        assertTrue(block.getString("explanation").contains("`<<`"))
        for (kokoro in listOf(false, true)) {
            val script = LessonNarrationScript.page(lesson, 4, NarrationLanguage.SPANISH, kokoro)
            val gate = script.gates.single { it.blockIndex == 1 }
            val correct = script.texts.subList(gate.start, gate.endExclusive).joinToString(" ")
            val spoken = ManualEnglishSpeech.strip(correct)
            assertTrue(spoken, spoken.startsWith("Correcto."))
            assertTrue(spoken, spoken.contains("El primer hueco contiene doble menor que, el operador"))
            assertFalse(spoken, spoken.contains("fin de código"))
            assertFalse(spoken, spoken.contains("código, doble menor que"))
            assertFalse(spoken, spoken.contains("código doble menor que"))
            assertFalse("This authored explanation has no spoken code boundary labels", spoken.contains("código"))
            assertTrue("An explanation remains behind its original success gate",
                script.origins.subList(gate.start, gate.endExclusive).all { it.feedback && it.blockIndex == 1 })
            assertFalse("Normal page narration cannot disclose the success explanation",
                LessonNarrationText.page(lesson, 4, NarrationLanguage.SPANISH, kokoro)
                    .joinToString(" ").contains("El primer hueco contiene"))
        }
        assertEquals("No lesson text, answer or ID changes to silence markup", original, lesson.toString())
    }

    @Test fun normalParagraphAndIncorrectGuidanceShareSilentInlineMarkup() {
        requireQa()
        val normal = "El operador `<<` inserta el texto."
        val wrong = "✗ Incorrecto. Reconsidera la función de `<<` antes de reintentar."
        for (kokoro in listOf(false, true)) {
            val ordinary = ManualEnglishSpeech.strip(LessonNarrationText.page(paragraph(normal), 0,
                NarrationLanguage.SPANISH, kokoro).joinToString(" "))
            assertEquals("El operador doble menor que inserta el texto.", ordinary)
            val guidance = ManualEnglishSpeech.strip(IncorrectFeedbackNarration.prepare(listOf(wrong),
                NarrationLanguage.SPANISH, kokoro).joinToString(" "))
            assertEquals("Incorrecto. Reconsidera la función de doble menor que antes de reintentar.", guidance)
            assertFalse(guidance.contains("Correcto."))
            assertFalse(guidance.contains("código"))
        }
        val adjoining = SpanishTechnicalSpeech.prepare("Antes`<<`después, `>>`; `!=`.").text
        assertEquals("Antes doble menor que después, doble mayor que; distinto de.", adjoining)
        assertEquals("código, doble menor que, fin de código",
            SpanishTechnicalSpeech.prepare("```<<```").text)
    }

    @Test fun silentMarkupKeepsOperatorOwnershipForWordReadingOnAndroid() {
        requireQa()
        val source = "Antes `<<` después."
        val pieces = mutableListOf<Triple<Int, Int, String>>()
        val prepared = SpanishTechnicalSpeech.prepareForReading(source) { start, end, spoken ->
            pieces += Triple(start, end, spoken)
        }
        assertEquals(SpanishTechnicalSpeech.prepareForKokoro(source), prepared)
        assertEquals("Antes doble menor que después.", ManualEnglishSpeech.strip(prepared.text))
        assertTrue("The spoken operator belongs only to its original two glyphs", pieces.any {
            source.substring(it.first, it.second) == "<<" && it.third.trim() == "doble menor que"
        })
        val wrappers = source.indices.filter { source[it] == '`' }
        assertTrue("Neither silent delimiter acquires a spoken code label", pieces.filter { piece ->
            (piece.first until piece.second).any { it in wrappers }
        }.all { it.third.isBlank() })
        val segments = NarrationSegments.sentences(listOf(prepared.text)).withIndex().associate { it.index to it.value }
        val mapping = NarrationWordMappings.forBlock(0, source, segments)
        assertEquals(segments.keys, mapping.keys)
        assertEquals("All original glyph offsets remain accounted for", source.indices.toSet(),
            mapping.values.flatMap { it.displayed.flatMap { range -> range.start until range.endExclusive } }.toSet())
        assertTrue("Reading must retain several local owners instead of one paragraph-wide range",
            mapping.values.flatMap { it.displayed }.distinct().size >= 3)
    }

    @Test fun nativeCorrectFeedbackKeepsItsTechnicalColorAndSourceOffsets() {
        requireQa()
        val source = "Correcto.\n" + targetBlock(targetLesson()).getString("explanation")
        instrumentation.runOnMainSync {
            val view = ReadingTextView(context)
            view.text = source
            LessonTechnicalText.apply(view, false, NarrationLanguage.SPANISH)
            val styled = view.text as Spanned
            assertEquals(source, styled.toString())
            val start = source.indexOf("<<")
            assertTrue("The original operator keeps technical color", styled.getSpans(start, start + 2,
                ForegroundColorSpan::class.java).isNotEmpty())
            val delimiters = styled.getSpans(0, styled.length, TechnicalDelimiterSpan::class.java)
            assertEquals(source.count { it == '`' }, delimiters.size)
            delimiters.forEach { span ->
                val from = styled.getSpanStart(span)
                val to = styled.getSpanEnd(span)
                assertEquals("`", source.substring(from, to))
                assertEquals(0, span.getSize(view.paint, styled, from, to, null))
            }
            view.measure(View.MeasureSpec.makeMeasureSpec(900, View.MeasureSpec.EXACTLY),
                View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED))
            view.layout(0, 0, view.measuredWidth, view.measuredHeight)
            assertTrue(view.height > 0)
            val bitmap = Bitmap.createBitmap(view.width, view.height, Bitmap.Config.ARGB_8888)
            try { view.draw(Canvas(bitmap)) } finally { bitmap.recycle() }
            assertEquals("Drawing cannot remove source markers or shift the operator", start,
                view.text.toString().indexOf("<<"))
        }
    }

    private fun requireQa() {
        assertEquals("Use only the isolated curricular APK", QA_PACKAGE, context.packageName)
        assertEquals(QA_PACKAGE, BuildConfig.APPLICATION_ID)
    }
    private fun targetLesson() = LessonRepository.loadLessonJson(context, "programming", LESSON_ID)
    private fun targetBlock(lesson: JSONObject) = lesson.getJSONArray("pages").getJSONObject(4)
        .getJSONArray("blocks").getJSONObject(1)
    private fun paragraph(text: String) = JSONObject().put("pages", JSONArray().put(JSONObject()
        .put("blocks", JSONArray().put(JSONObject().put("type", "paragraph").put("text", text)))))

    companion object {
        private const val QA_PACKAGE = "com.learngraphicsprogramming.app.curricularqa"
        private const val LESSON_ID = "cpp_fundamentals__programas_y_ejecucion__texto_secuencia_y_formato"
    }
}
