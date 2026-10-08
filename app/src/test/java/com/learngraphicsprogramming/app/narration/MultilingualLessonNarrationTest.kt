package com.learngraphicsprogramming.app.narration

import java.io.File
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class MultilingualLessonNarrationTest {
    @Test fun selectedLanguageLocalizesAnnouncementsAndStructuredMathOnly() {
        val lesson = JSONObject("""{"title":"Visible title","pages":[{"blocks":[
          {"type":"paragraph","text":"Visible prose: 3/4."},
          {"type":"code","language":"cpp","code":"SECRET_CODE"},
          {"type":"cpp_compiler","initialCode":"SECRET_SOURCE"},
          {"type":"math_multiple_choice_single","prompt":"Visible prompt","options":[
            {"label":"x²","explanation":"Visible explanation"}],"validator":{"expected":"SECRET_ANSWER"},
            "explanation":"HIDDEN_AFTER_VALIDATION"},
          {"type":"math_equation_fill_blanks","fields":[{"id":"input","type":"numeric"}],
           "segments":[{"text":"2 + "},{"field":"input"},{"text":" = 5"}],
           "validator":{"expected":3},"feedback":{"correct":"HIDDEN_FEEDBACK"}}
        ]}]}""")
        val before = lesson.toString()
        val english = LessonNarrationText.page(lesson, 0, NarrationLanguage.AMERICAN_ENGLISH)
        val french = LessonNarrationText.page(lesson, 0, NarrationLanguage.FRENCH)
        assertEquals(listOf("Visible title", "Visible prose: three quarters.", "Example of C plus plus code.",
            "C plus plus code editor.", "Visible prompt", "Option 1.", "ex squared", "Visible explanation",
            "two plus blank equals five"), english)
        assertEquals(listOf("Visible title", "Visible prose: trois quarts.", "Exemple de code C plus plus.",
            "Éditeur de code C plus plus.", "Visible prompt", "Option 1.", "ix au carré", "Visible explanation",
            "deux plus case vide est égal à cinq"), french)
        for (spoken in listOf(english, french)) {
            assertFalse(spoken.joinToString(" ").contains("SECRET"))
            assertFalse(spoken.joinToString(" ").contains("HIDDEN"))
        }
        assertEquals(before, lesson.toString())
    }

    @Test fun lessonMetadataIsResolvedBeforeContentExtractionAndPageMetadataWins() {
        val lesson = JSONObject("""{"metadata":{"language":"en-GB"},"pages":[
          {"blocks":[{"type":"math_puzzle","prompt":"Count","representations":[{"type":"math_formula","text":"101"}]}]},
          {"metadata":{"locale":"fr-FR"},"blocks":[{"type":"math_puzzle","prompt":"Compte","representations":[{"type":"math_formula","text":"101"}]}]}
        ]}""")
        val selected = NarrationLanguage.SPANISH
        val first = NarrationLanguage.forLesson(lesson, 0, selected)
        val second = NarrationLanguage.forLesson(lesson, 1, selected)
        assertEquals(NarrationLanguage.BRITISH_ENGLISH, first)
        assertEquals(NarrationLanguage.FRENCH, second)
        assertEquals(listOf("Count", "one hundred and one"), LessonNarrationText.page(lesson, 0, first))
        assertEquals(listOf("Compte", "cent un"), LessonNarrationText.page(lesson, 1, second))
    }

    @Test fun allRealCoursePagesRemainBoundedAndDoNotExposeRawFormulaSyntaxInAnyLanguage() {
        val assets = listOf(File("src/main/assets"), File("app/src/main/assets")).first { it.isDirectory }
        val course = File(assets, "content/es/math_physics/math_course_01")
        val lessons = course.walkTopDown().filter { it.isFile && it.name.startsWith("lesson_") && it.extension == "json" }.toList()
        assertTrue(lessons.size > 20)
        for (file in lessons) {
            val lesson = JSONObject(file.readText())
            val before = lesson.toString()
            for (language in NarrationLanguage.entries) {
                for (index in 0 until lesson.getJSONArray("pages").length()) {
                    val segments = LessonNarrationText.page(lesson, index, language)
                    assertTrue("${file.name} $index $language", segments.isNotEmpty())
                    assertTrue(segments.all { it.isNotBlank() && it.length <= 700 })
                    assertFalse(segments.any { it.contains("\\frac") || it.contains("\\sqrt") || it.contains("\\begin") })
                }
            }
            assertEquals(before, lesson.toString())
        }
    }
}
