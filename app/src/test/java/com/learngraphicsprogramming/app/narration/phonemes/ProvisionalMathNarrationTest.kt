package com.learngraphicsprogramming.app.narration.phonemes

import com.learngraphicsprogramming.app.narration.LessonNarrationText
import com.learngraphicsprogramming.app.narration.NarrationLanguage
import com.learngraphicsprogramming.app.narration.NarrationSegments
import java.io.File
import org.json.JSONObject
import org.junit.Assert.assertTrue
import org.junit.Test

/** Every authored provisional math page must be narratable without falling back to Android TTS. */
class ProvisionalMathNarrationTest {
    private val source = File(System.getProperty("translation.resources", "src/main/res"))
        .parentFile.resolve("assets/content/es/math_physics")

    @Test fun everyProvisionalMathLessonSentenceIsAcceptedBySpanishKokoro() {
        val lessons = source.walkTopDown().filter {
            it.isFile && it.name.startsWith("lesson_qa_step_") && it.extension == "json"
        }.toList()
        assertTrue("No provisional math lessons were found", lessons.isNotEmpty())
        for (file in lessons) {
            val json = JSONObject(file.readText())
            val pages = json.getJSONArray("pages")
            for (page in 0 until pages.length()) {
                val extracted = LessonNarrationText.page(json, page, NarrationLanguage.SPANISH)
                val sentences = NarrationSegments.sentences(extracted)
                assertTrue("${file.name}: empty narration page $page", sentences.isNotEmpty())
                for (sentence in sentences) {
                    val phones = try {
                        SpanishFluidG2P.phonemize(sentence)
                    } catch (error: Exception) {
                        throw AssertionError(
                            "${file.path}, page ${page + 1}, text=\"$sentence\" was rejected", error
                        )
                    }
                    assertTrue("${file.name}: empty phonemes for $sentence", phones.isNotBlank())
                }
            }
        }
    }
}
