package com.learngraphicsprogramming.app.narration

import java.io.File
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class LessonNarrationTextTest {
    @Test fun firstProgrammingPageNarratesTheBodyNotOnlyTitleAndHeading() {
        val assets = listOf(File("src/main/assets"), File("app/src/main/assets"))
            .first { it.isDirectory }
        val source = File(
            assets,
            "content/es/programming/cpp_fundamentals/intro_programming/lesson_what_is_programming.json"
        )
        val lesson = JSONObject(source.readText())
        val blocks = lesson.getJSONArray("pages").getJSONObject(0).getJSONArray("blocks")
        val paragraph = blocks.getJSONObject(1).getString("text")

        val spoken = LessonNarrationText.page(lesson, 0)
        assertTrue("Title, heading and lesson body must be narrated", spoken.size >= 3)
        assertEquals(lesson.getString("title"), spoken.first())
        assertEquals(blocks.getJSONObject(0).getString("text"), spoken[1])
        assertTrue(
            "First page must contain its actual explanation",
            spoken.drop(2).joinToString(" ").contains("La programación consiste en describir")
        )
        assertTrue(
            "Do not skip the end of the explanation",
            spoken.drop(2).joinToString(" ").contains(paragraph.takeLast(35))
        )

        val prepared = NarrationSegments.prepare(spoken, 250)
        assertTrue("All visible authored prose must remain in the playback list", prepared.size >= 3)
        assertTrue(prepared.all { it.isNotBlank() && it.length <= 250 })
        assertEquals(spoken.joinToString(" "), prepared.joinToString(" "))
    }

    @Test fun pageReadsVisibleContentInOrderAndNeverTraversesAnswerMetadata() {
        val lesson = JSONObject("""{
          "title":"Lección",
          "pages":[{"blocks":[
            {"type":"heading","text":"Fracciones"},
            {"type":"paragraph","text":"Una fracción: 1/2."},
            {"type":"math_puzzle","prompt":"Elige una fracción.",
             "representations":[{"type":"math_formula","text":"3/4"}],
             "responses":[{"type":"math_multiple_choice_single","label":"Elige una opción",
                "options":[{"id":"SECRET_ID","label":"Una mitad"}],
                "validator":{"expected":"HIDDEN_ANSWER"}}],
             "feedback":{"correct":"HIDDEN_FEEDBACK"}},
            {"type":"multiple_choice","question":"¿Qué significa?","options":["Primero","Segundo"],
             "correctIndex":1,"explanation":"HIDDEN_EXPLANATION"},
            {"type":"code","language":"cpp","code":"SECRET_CODE << 123"},
            {"type":"note","text":"Nota final"}
          ]},{"blocks":[{"type":"paragraph","text":"Otra página"}]}]
        }""")
        val spoken = LessonNarrationText.page(lesson, 0)
        assertEquals(listOf("Lección", "Fracciones", "Una fracción: un medio.", "Elige una fracción.",
            "tres cuartos", "Elige una opción", "Opción 1.", "Una mitad", "¿Qué significa?",
            "Opción 1.", "Primero", "Opción 2.", "Segundo", "Ejemplo de código C más más.", "Nota final"), spoken)
        assertFalse(spoken.joinToString(" ").contains("HIDDEN"))
        assertFalse(spoken.joinToString(" ").contains("SECRET"))
        assertEquals(listOf("Otra página"), LessonNarrationText.page(lesson, 1))
        assertTrue(LessonNarrationText.page(lesson, 4).isEmpty())
    }

    @Test fun richOptionsAndEquationTemplatesReadVisibleBodiesAndBlankLabels() {
        val lesson = JSONObject("""{"pages":[{"blocks":[{"type":"math_puzzle","prompt":"Completa.",
          "responses":[{"type":"math_equation_fill_blanks",
            "fields":[{"id":"missing","type":"numeric","label":"resultado","validator":{"expected":45}}],
            "segments":[{"text":"5 + "},{"field":"missing"},{"text":" = 10"}],
            "validator":{"expected":"SECRET"}},
          {"type":"math_multiple_choice_single","options":[{"label":"INVISIBLE_LABEL","content":[
            {"type":"math_formula","text":"x²"},{"type":"text","text":"de área"}]}]}]}]}]}""")
        val spoken = LessonNarrationText.page(lesson, 0).joinToString(" ")
        assertTrue(spoken.contains("cinco más resultado es igual a diez"))
        assertTrue(spoken.contains("equis al cuadrado de área"))
        assertFalse(spoken.contains("INVISIBLE_LABEL"))
        assertFalse(spoken.contains("SECRET"))
    }

    @Test fun hiddenGraphValuesAreNotDisclosed() {
        val lesson = JSONObject("""{"pages":[{"blocks":[{"type":"math_puzzle","prompt":"Lee la marca.",
          "representations":[{"type":"math_number_line","value":123456,"showValueLabel":false}],
          "responses":[],"validator":{"expected":123456}}]}]}""")
        assertEquals(listOf("Lee la marca."), LessonNarrationText.page(lesson, 0))
    }

    @Test fun richOptionExplanationIsReadBecauseItIsVisibleWhileBlockExplanationStaysHidden() {
        val lesson = JSONObject("""{"pages":[{"blocks":[
          {"type":"math_puzzle","prompt":"Compara las opciones.","explanation":"HIDDEN_BLOCK_EXPLANATION",
           "responses":[{"type":"math_multiple_choice_single","options":[
             {"label":"Fracción","content":[{"type":"math_formula","text":"1/2"}],
              "explanation":"Esta opción representa una mitad.","validator":{"expected":"HIDDEN_VALIDATOR"}}
           ],"validator":{"expected":"HIDDEN_RESPONSE"}}],"feedback":{"correct":"HIDDEN_FEEDBACK"}},
          {"type":"multiple_choice","question":"Elige.","options":["A","B"],"explanation":"HIDDEN_BLOCK_EXPLANATION"}
        ]}]}""")
        val spoken = LessonNarrationText.page(lesson, 0).joinToString(" ")
        assertTrue(spoken.contains("un medio Esta opción representa una mitad."))
        assertFalse(spoken.contains("HIDDEN"))
    }

    @Test fun longNarrationHasBoundedSegmentsAndKeepsAllWordsInOrder() {
        val prose = (1..280).joinToString(" ") { "Palabra$it." }
        val lesson = JSONObject().put("pages", org.json.JSONArray().put(JSONObject().put("blocks",
            org.json.JSONArray().put(JSONObject().put("type", "paragraph").put("text", prose)))))
        val segments = LessonNarrationText.page(lesson, 0)
        assertTrue(segments.size > 1)
        assertTrue(segments.all { it.length in 1..700 })
        assertEquals(prose, segments.joinToString(" "))
    }

    @Test fun everyRealCourseOnePageCanBeExtractedWithoutMutatingLessons() {
        val assets = listOf(File("src/main/assets"), File("app/src/main/assets")).first { it.isDirectory }
        val course = File(assets, "content/es/math_physics/math_course_01")
        val lessons = course.walkTopDown().filter { it.isFile && it.name.startsWith("lesson_") && it.extension == "json" }.toList()
        assertTrue(lessons.size > 20)
        lessons.forEach { file ->
            val lesson = JSONObject(file.readText())
            val before = lesson.toString()
            val pages = lesson.getJSONArray("pages")
            for (index in 0 until pages.length()) {
                val segments = LessonNarrationText.page(lesson, index)
                assertTrue("${file.name} page $index", segments.isNotEmpty())
                assertTrue("${file.name} page $index", segments.all { it.isNotBlank() && it.length <= 700 })
                assertFalse("${file.name} page $index", segments.any { it.contains("\\frac") || it.contains("\\sqrt") })
            }
            assertEquals(before, lesson.toString())
        }
    }
}
