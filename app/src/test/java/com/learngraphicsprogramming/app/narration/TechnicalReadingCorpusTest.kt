package com.learngraphicsprogramming.app.narration

import java.io.File
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class TechnicalReadingCorpusTest {
    private fun observed(source: String, mode: String = "auto") {
        val consumed = mutableListOf<Int>()
        val pieces = StringBuilder()
        val result = SpanishTechnicalSpeech.prepareForReading(source, mode) { start, end, spoken ->
            assertTrue(start in 0..source.length && end in start..source.length)
            consumed += start until end
            pieces.append(spoken)
        }
        assertEquals("An observational trace must preserve the production audio input: $source",
            SpanishTechnicalSpeech.prepareForKokoro(source, mode), result)
        assertEquals("Every original glyph has one source owner: $source", source.indices.toList(), consumed)
        // The original technical reader performs this final whitespace cleanup only
        // when its preparation changes notation. The observer exposes pre-cleanup pieces.
        fun normalized(value: String) = value.removePrefix(ManualEnglishSpeech.PREPARED.toString())
            .replace(Regex("[ \\t]+"), " ").replace(Regex(" +([,.;:!?])"), "$1").trim()
        assertEquals(normalized(result.text), normalized(pieces.toString()))
    }

    private fun assets(): File = requireNotNull(File(System.getProperty("translation.resources") ?: "src/main/res")
        .absoluteFile.parentFile).resolve("assets/content/es/programming")

    private fun mapping(source: String, mode: String, location: String) {
        // Isolate the authored visible stem. Exercise options have their own text
        // extraction; they do not belong to the question's registered prose view.
        val block = JSONObject().put("type", "paragraph").put("text", source).put("mathMode", mode)
        val lesson = JSONObject().put("pages", JSONArray().put(
            JSONObject().put("blocks", JSONArray().put(block))))
        val snapshot = lesson.toString()
        val script = LessonNarrationScript.page(lesson, 0, NarrationLanguage.SPANISH, forKokoro = true)
        val plan = requireNotNull(script.readingPlan(NarrationEngine.KOKORO))
        val parts = NarrationSegments.sentences(script.texts)
        val selected = plan.origins.indices.filter {
            plan.origins[it].blockIndex == 0 && !plan.origins[it].feedback
        }.associateWith { parts[it] }
        assertTrue("No spoken source segments: $location", selected.isNotEmpty())
        val actual = NarrationWordMappings.forBlock(0, source, selected)
        assertEquals("No source projection for $location: $source", selected.keys, actual.keys)
        assertEquals("A glyph remains outside source projection: $location", source.indices.toSet(),
            actual.values.flatMap { value -> value.displayed.flatMap {
                it.start until it.endExclusive
            } }.toSet())
        actual.forEach { (index, value) ->
            assertEquals("Wrong block owner: $location/$index", 0, value.blockIndex)
            assertEquals("Word offsets differ from the exact played text: $location/$index",
                NarrationSpokenWords.lexemes(selected.getValue(index)).map {
                    ReadingTextRange(it.start, it.endExclusive)
                }, value.spoken)
            assertEquals("Missing token source owner: $location/$index", value.spoken.size, value.displayed.size)
            assertTrue("Invalid glyph owner: $location/$index", value.displayed.all {
                it.start >= 0 && it.start < it.endExclusive && it.endExclusive <= source.length
            })
        }
        assertEquals(snapshot, lesson.toString())
    }

    @Test fun theReportedLibraryParagraphAndMixedTechnicalSignsKeepTheirExactSpeechInput() {
        val lesson = JSONObject(assets().resolve(
            "cpp_fundamentals/programas_y_ejecucion/lesson_primer_programa_y_salida.json").readText())
        val source = lesson.getJSONArray("pages").getJSONObject(0).getJSONArray("blocks")
            .getJSONObject(3).getString("text")
        assertTrue(source.startsWith("Una biblioteca reúne recursos que otros programas pueden utilizar."))
        observed(source)
        observed("¿Lee #include <iostream>, std::cout << 42; y std::? { main(); } ¡Después, return 0!")
        observed("`std::cout << 7;` contiene ::, < >, { }, ; y números. Después calcula \\(x+2\\).")
        observed("Primero 17.º, sección § 1.2; después 0x2A, 3.5f y contar({1, 4, 7}); fin.")
    }

    @Test fun codeFragmentsKeepSeparateOwnersForHeadersQualifiedNamesAndTerminators() {
        val source = "#include <iostream> permite usar std::cout; y valores {1, 2}."
        val owners = mutableListOf<String>()
        val result = SpanishTechnicalSpeech.prepareForReading(source) { start, end, spoken ->
            if (spoken.isNotBlank() && start < end) owners += source.substring(start, end)
        }
        assertEquals(SpanishTechnicalSpeech.prepareForKokoro(source), result)
        assertTrue(owners.toString(), owners.containsAll(listOf("#include", "<", "iostream", ">",
            "std", "::", "cout", ";", "{", "1", ",", "2", "}")))
        assertTrue("No whole-header owner may replace its actual technical fragments",
            "#include <iostream>" !in owners)
        assertTrue("A qualified name must not wait for a later C++ token", "std::cout;" !in owners)
    }

    @Test fun bothExistingProgrammingCoursesKeepAllTechnicalSpeechAndOriginalSourceCoverage() {
        val relevant = Regex("#include|std::|[{}]|<<|>>|->|&&|\\|\\|")
        val checkedCourses = mutableSetOf<String>()
        var checked = 0
        for (course in listOf("cpp_fundamentals", "programming_practice_tools")) {
            for (file in assets().resolve(course).walkTopDown().filter {
                it.isFile && it.name.startsWith("lesson_") && it.extension == "json"
            }) {
                val lesson = JSONObject(file.readText())
                val snapshot = lesson.toString()
                val pages = lesson.optJSONArray("pages") ?: continue
                for (pageIndex in 0 until pages.length()) {
                    val blocks = pages.optJSONObject(pageIndex)?.optJSONArray("blocks") ?: continue
                    for (blockIndex in 0 until blocks.length()) {
                        val block = blocks.optJSONObject(blockIndex) ?: continue
                        val source = listOf("text", "question", "prompt").firstNotNullOfOrNull {
                            (block.opt(it) as? String)?.takeIf(String::isNotBlank)
                        } ?: continue
                        if (!relevant.containsMatchIn(source)) continue
                        val mode = block.optString("mathMode", "auto")
                        observed(source, mode)
                        mapping(source, mode, "${file.relativeTo(assets())}:page=${pageIndex + 1}:block=$blockIndex")
                        checkedCourses += course
                        checked++
                    }
                }
                assertEquals(snapshot, lesson.toString())
            }
        }
        assertEquals(setOf("cpp_fundamentals", "programming_practice_tools"), checkedCourses)
        assertTrue("Exercise the published technical corpus rather than only synthetic samples", checked > 400)
    }
}
