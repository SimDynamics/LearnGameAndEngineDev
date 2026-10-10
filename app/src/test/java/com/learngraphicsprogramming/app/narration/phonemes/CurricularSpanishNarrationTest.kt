package com.learngraphicsprogramming.app.narration.phonemes

import com.learngraphicsprogramming.app.narration.LessonNarrationText
import com.learngraphicsprogramming.app.narration.NarrationLanguage
import com.learngraphicsprogramming.app.narration.NarrationSegments
import java.io.File
import javax.xml.parsers.DocumentBuilderFactory
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.assertTrue
import org.junit.Test

/** Walk the published indices, like NarrationCurriculum, without changing authored content. */
class CurricularSpanishNarrationTest {
    @Test fun everyAvailableIndexedSpanishSentenceIsAcceptedByTheKokoroFrontend() {
        val resources = File(System.getProperty("translation.resources") ?: "src/main/res")
        val assets = requireNotNull(resources.absoluteFile.parentFile).resolve("assets")
        val root = assets.resolve("content/es")
        val files = linkedSetOf<File>()
        for (route in listOf("programming", "math_physics")) {
            val courses = JSONArray(root.resolve("$route/courses.json").readText())
            for (courseIndex in 0 until courses.length()) {
                val course = courses.getJSONObject(courseIndex).getString("id")
                val courseFile = root.resolve("$route/$course/course_$course.json")
                // The public list also has future courses; the real generator skips those.
                if (!courseFile.isFile) continue
                val units = JSONObject(courseFile.readText()).getJSONArray("units")
                for (unitIndex in 0 until units.length()) {
                    val unit = units.getJSONObject(unitIndex).getString("id").removePrefix("${course}__")
                    val unitFile = root.resolve("$route/$course/$unit/unit_$unit.json")
                    if (!unitFile.isFile) continue
                    val lessons = JSONObject(unitFile.readText()).getJSONArray("lessons")
                    for (lessonIndex in 0 until lessons.length()) {
                        val id = lessons.getJSONObject(lessonIndex).getString("id").split("__", limit = 3)
                        val file = root.resolve("$route/${id[0]}/${id[1]}/lesson_${id[2]}.json")
                        if (file.isFile) files += file
                    }
                }
            }
        }
        assertTrue("No indexed Spanish narration was found", files.isNotEmpty())
        val nodes = DocumentBuilderFactory.newInstance().newDocumentBuilder()
            .parse(resources.resolve("values/strings.xml")).getElementsByTagName("string")
        val strings = (0 until nodes.length).map(nodes::item).associate {
            it.attributes.getNamedItem("name").nodeValue to it.textContent
        }
        val instructions = mapOf(
            "order_code" to strings.getValue("exercise_order_instruction"),
            "drag_fill_code" to strings.getValue("exercise_fill_instruction")
        )
        val failures = mutableListOf<String>()
        var sentenceCount = 0
        for (file in files) {
            val lesson = JSONObject(file.readText())
            val pages = lesson.getJSONArray("pages")
            for (page in 0 until pages.length()) {
                val texts = LessonNarrationText.page(lesson, page, NarrationLanguage.SPANISH) {
                    instructions[it].orEmpty()
                }
                for (sentence in NarrationSegments.sentences(texts)) {
                    sentenceCount++
                    try {
                        assertTrue(SpanishFluidG2P.phonemize(sentence).isNotBlank())
                    } catch (error: Exception) {
                        failures += "${file.relativeTo(root)} page ${page + 1}: $sentence (${error.message})"
                    }
                }
            }
        }
        assertTrue("The curriculum contains no narrated sentences", sentenceCount > 0)
        assertTrue("${failures.size} of $sentenceCount sentences rejected:\n${failures.take(20).joinToString("\n")}",
            failures.isEmpty())
    }
}
