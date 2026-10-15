package com.learngraphicsprogramming.app.narration

import java.io.File
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

/** All available French lessons exercise the same runtime extraction, without content metadata. */
class MixedFrenchCurriculumTest {
    @Test fun everyAvailableFrenchLessonKeepsItsContentAndSentenceBoundaries() {
        val resources = File(System.getProperty("translation.resources") ?: "src/main/res")
        val root = resources.absoluteFile.parentFile.resolve("assets/content/fr")
        val lessons = root.walkTopDown().filter { it.isFile && it.name.startsWith("lesson_") && it.extension == "json" }.toList()
        assertTrue("The existing French corpus must be exercised", lessons.isNotEmpty())
        var pages = 0
        var sentences = 0
        var selected = 0
        for (file in lessons) {
            val lesson = JSONObject(file.readText())
            val original = lesson.toString()
            repeat(lesson.getJSONArray("pages").length()) { page ->
                val location = "${file.relativeTo(root)} page ${page + 1}"
                val plain = LessonNarrationText.page(lesson, page, NarrationLanguage.FRENCH)
                val prepared = LessonNarrationText.page(lesson, page, NarrationLanguage.FRENCH, forKokoro = true)
                assertEquals(location, plain, prepared.map(ManualEnglishSpeech::strip))
                val split = NarrationSegments.sentences(prepared)
                assertEquals(location, NarrationSegments.sentences(plain), split.map(ManualEnglishSpeech::strip))
                assertEquals(location, NarrationSegments.prepare(plain, 250), NarrationSegments.prepare(prepared, 250))
                selected += split.sumOf { ManualEnglishSpeech.spans(it).size }
                pages++
                sentences += split.size
            }
            assertEquals("Source JSON remains unchanged: ${file.name}", original, lesson.toString())
        }
        assertTrue(sentences > 0)
        println("FRENCH_MANUAL_COMPATIBILITY lessons=${lessons.size} pages=$pages sentences=$sentences selected=$selected")
    }
}
