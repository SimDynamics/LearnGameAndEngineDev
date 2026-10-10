package com.learngraphicsprogramming.app.narration

import java.io.File
import javax.xml.parsers.DocumentBuilderFactory
import org.json.JSONObject
import org.junit.Assert.assertTrue
import org.junit.Test

class FirstUnitNarrationPackTest {
    private val resources = File(System.getProperty("translation.resources") ?: "src/main/res")
    private val assets = requireNotNull(resources.absoluteFile.parentFile).resolve("assets")

    @Test fun bundledSpanishDoraPackMatchesEveryCurrentFirstUnitSentence() {
        val strings = DocumentBuilderFactory.newInstance().newDocumentBuilder()
            .parse(resources.resolve("values/strings.xml")).getElementsByTagName("string")
        fun instruction(type: String): String {
            val name = when (type) {
                "order_code" -> "exercise_order_instruction"
                "drag_fill_code" -> "exercise_fill_instruction"
                else -> return ""
            }
            return (0 until strings.length).map(strings::item)
                .first { it.attributes.getNamedItem("name").nodeValue == name }.textContent
        }

        val required = linkedMapOf<String, String>()
        for (lessonName in listOf("what_is_programming", "first_contact_cpp", "program_structure")) {
            val file = assets.resolve(
                "content/es/programming/cpp_fundamentals/intro_programming/lesson_$lessonName.json"
            )
            val lesson = JSONObject(file.readText())
            for (page in 0 until lesson.getJSONArray("pages").length()) {
                val texts = LessonNarrationText.page(lesson, page, NarrationLanguage.SPANISH, ::instruction)
                for (sentence in NarrationSegments.sentences(texts)) {
                    required[KokoroNarrationCache.key("es", "ef_dora", sentence)] = sentence
                }
            }
        }
        assertTrue("The authored first unit must have narration", required.isNotEmpty())
        val pack = assets.resolve("narration/prerendered")
        val missing = required.filterKeys { hash -> !pack.resolve("$hash.ogg").isFile }
        assertTrue("Current sentences absent from the shipped pack: ${missing.values}", missing.isEmpty())
        for (hash in required.keys) {
            val audio = pack.resolve("$hash.ogg")
            val header = audio.inputStream().use { it.readNBytes(128) }
            assertTrue("Invalid bundled Opus: ${audio.name}", audio.length() >= 64 &&
                header.toString(Charsets.ISO_8859_1).startsWith("OggS") &&
                header.toString(Charsets.ISO_8859_1).contains("OpusHead"))
        }
    }
}
