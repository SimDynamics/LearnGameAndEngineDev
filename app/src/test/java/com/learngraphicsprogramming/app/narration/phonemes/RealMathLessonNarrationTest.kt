package com.learngraphicsprogramming.app.narration.phonemes

import com.learngraphicsprogramming.app.narration.LessonNarrationText
import com.learngraphicsprogramming.app.narration.NarrationLanguage
import com.learngraphicsprogramming.app.narration.NarrationSegments
import java.io.File
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class RealMathLessonNarrationTest {
    @Test fun everySentenceInMathCourseOneUnitOneLessonThreeIsAcceptedByKokoro() {
        val resources = File(System.getProperty("translation.resources") ?: "src/main/res")
        val assets = requireNotNull(resources.absoluteFile.parentFile).resolve("assets")
        val file = assets.resolve(
            "content/es/math_physics/math_course_01/numeros_y_operaciones/lesson_sumar_y_restar_enteros.json"
        )
        val lesson = JSONObject(file.readText())
        val unchanged = lesson.toString()
        val pages = lesson.getJSONArray("pages")
        for (page in 0 until pages.length()) {
            val texts = LessonNarrationText.page(lesson, page, NarrationLanguage.SPANISH)
            val sentences = NarrationSegments.sentences(texts)
            assertTrue("Math lesson 3 page ${page + 1} must contain spoken content", sentences.isNotEmpty())
            for (sentence in sentences) {
                val phones = try {
                    SpanishFluidG2P.phonemize(sentence)
                } catch (error: Exception) {
                    throw AssertionError("Math lesson 3 page ${page + 1}, sentence=\"$sentence\"", error)
                }
                assertTrue("Kokoro cannot synthesize an empty sentence", phones.isNotBlank())
            }
        }
        assertEquals("Speech normalization must never rewrite the course", unchanged, lesson.toString())
    }
}
