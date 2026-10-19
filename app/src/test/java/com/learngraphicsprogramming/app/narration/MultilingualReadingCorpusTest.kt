package com.learngraphicsprogramming.app.narration

import java.io.File
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

/** Real lesson/script ownership remains independent of translation and curriculum preparation. */
class MultilingualReadingCorpusTest {
    private val languages = listOf(NarrationLanguage.AMERICAN_ENGLISH, NarrationLanguage.BRITISH_ENGLISH, NarrationLanguage.FRENCH)
    private fun assets() = requireNotNull(File(System.getProperty("translation.resources") ?: "src/main/res")
        .absoluteFile.parentFile).resolve("assets/content")

    private fun assertMapping(source: String, mode: String, language: NarrationLanguage, location: String) {
        val block = JSONObject().put("type", "paragraph").put("text", source).put("mathMode", mode)
        val lesson = JSONObject().put("pages", JSONArray().put(JSONObject().put("blocks", JSONArray().put(block))))
        val before = lesson.toString()
        val script = LessonNarrationScript.page(lesson, 0, language, forKokoro = true)
        val plan = requireNotNull(script.readingPlan(NarrationEngine.KOKORO))
        val parts = NarrationSegments.sentences(script.texts)
        val selected = plan.origins.indices.filter { plan.origins[it].blockIndex == 0 && !plan.origins[it].feedback }
            .associateWith { parts[it] }
        assertTrue("No speech in $language/$location", selected.isNotEmpty())
        val mapping = NarrationWordMappings.forBlock(0, source, selected, language)
        assertEquals("Missing source mapping in $language/$location: $source", selected.keys, mapping.keys)
        assertEquals("Unowned visible glyph in $language/$location", source.indices.toSet(),
            mapping.values.flatMap { value -> value.displayed.flatMap { it.start until it.endExclusive } }.toSet())
        mapping.forEach { (index, value) ->
            // Pronounced technical symbols now own exact frontend boundaries too;
            // the explicit language preserves Spanish's original lexical contract.
            assertEquals("Spoken source atoms in $language/$location",
                NarrationSpokenWords.technicalAtoms(selected.getValue(index), language)
                    .map { ReadingTextRange(it.start, it.endExclusive) }, value.spoken)
            assertEquals(value.spoken.size, value.displayed.size)
            assertTrue(value.displayed.all { it.start >= 0 && it.start < it.endExclusive && it.endExclusive <= source.length })
        }
        assertEquals("Reading projection cannot modify content", before, lesson.toString())
    }

    @Test fun actualFirstProgramLibraryParagraphQuestionsAndHeadingsKeepBothTranslationsUnchanged() {
        for (language in languages) {
            val file = assets().resolve("${language.resourceGroup}/programming/cpp_fundamentals/programas_y_ejecucion/lesson_primer_programa_y_salida.json")
            val lesson = JSONObject(file.readText())
            val before = lesson.toString()
            val blocks = lesson.getJSONArray("pages").getJSONObject(0).getJSONArray("blocks")
            for (index in 0 until blocks.length()) {
                val block = blocks.getJSONObject(index)
                val source = block.optString("text").ifBlank { block.optString("question") }
                if (source.isNotBlank()) assertMapping(source, block.optString("mathMode", "auto"), language, "U1/L2/P1/block=$index")
            }
            assertEquals(before, lesson.toString())
        }
    }

    @Test fun technicalPunctuationFormulasAndElisionsKeepAllOriginalGlyphOwners() {
        val english = listOf(
            "Which check follows? Read #include <iostream>, std::cout << 42; and { return 0; }. Then continue.",
            "The function's result isn't a new variable; use std::cout. What comes next?",
            "Calculate \\(x^2+1\\), then compare \\(a \\leq b\\). The code remains unchanged.",
            "First \\(\\frac{1}{2}+\\frac{3}{4}\\), then the repeated fraction \\(\\frac{1}{2}\\)."
        )
        val french = listOf(
            "Quelle vérification convient ? Lis #include <iostream>, std::cout << 42; et { return 0; }. Ensuite, continue.",
            "L'algorithme n'est pas une variable ; utilise std::cout. Qu'est-ce qui vient ensuite ?",
            "Calcule \\(x^2+1\\), puis compare \\(a \\leq b\\). Le code reste inchangé.",
            "D'abord \\(\\frac{1}{2}+\\frac{3}{4}\\), puis la fraction répétée \\(\\frac{1}{2}\\)."
        )
        for (language in languages) {
            val samples = if (language == NarrationLanguage.FRENCH) french else english
            samples.forEachIndexed { index, source -> assertMapping(source, "auto", language, "mixed=$index") }
        }
    }

    @Test fun namedOperatorsKeepEveryOriginalGlyphInEveryLanguage() {
        for (language in listOf(NarrationLanguage.SPANISH) + languages) {
            for (symbol in listOf("::", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>=", ".*", "->*", "<=>", "++", "--", "!")) {
                val expression = when (symbol) { "++", "--" -> "x$symbol"; "!" -> "!x"; else -> "x${symbol}y" }
                val source = when (language) {
                    NarrationLanguage.SPANISH -> "Lee $expression y después std::cout. Conserva el texto."
                    NarrationLanguage.FRENCH -> "Lis $expression puis std::cout. Conserve le texte."
                    else -> "Read $expression then std::cout. Keep the text."
                }
                assertMapping(source, "auto", language, "named-operator=$symbol")
            }
        }
    }

    @Test fun actualPageThreeSemicolonExplanationKeepsWordOwnershipAfterSentenceSplitting() {
        for (language in listOf(NarrationLanguage.SPANISH) + languages) {
            val lesson = JSONObject(assets().resolve("${language.resourceGroup}/programming/cpp_fundamentals/programas_y_ejecucion/lesson_primer_programa_y_salida.json").readText())
            val original = lesson.toString()
            val blocks = lesson.getJSONArray("pages").getJSONObject(2).getJSONArray("blocks")
            val source = (0 until blocks.length()).map { blocks.getJSONObject(it).optString("text") }
                // Editorial wording and citation punctuation differ by locale;
                // the actual technical-terminator explanation remains the fixture.
                .single { it.contains("#include <iostream>") && it.contains(";.") }
            assertMapping(source, "auto", language, "U1/L2/P3/semicolon-explanation")
            val regression = when (language) {
                NarrationLanguage.SPANISH -> "Cada instrucción termina con ;. No añadas ; a cualquier línea. Las llaves delimitan instrucciones; tampoco son una salida."
                NarrationLanguage.FRENCH -> "Chaque instruction se termine par ;. N'ajoute pas ; à toutes les lignes. Les accolades délimitent les instructions ; elles ne sont pas un message."
                else -> "Each statement ends with ;. Do not add ; to every line. Braces delimit statements; they are not an output message."
            }
            assertEquals("Keep the earlier two technical signs plus ordinary punctuation as an explicit regression", 3,
                regression.count { it == ';' })
            assertMapping(regression, "auto", language, "three-semicolons-regression")
            assertEquals(original, lesson.toString())
        }
    }

    @Test fun technicalBlocksFromBothProgrammingCoursesRemainAutomaticallyCompatible() {
        val technical = Regex("#include|std::|[{}]|<<|>>|->|&&|\\|\\|")
        for (language in languages) {
            val seenCourses = mutableSetOf<String>()
            val existingCourses = mutableSetOf<String>()
            val inventory = mutableSetOf<String>()
            val scanned = mutableSetOf<String>()
            var count = 0
            for (course in listOf("cpp_fundamentals", "programming_practice_tools")) {
                val root = assets().resolve("${language.resourceGroup}/programming/$course")
                val files = root.walkTopDown().filter { it.isFile && it.name.startsWith("lesson_") && it.extension == "json" }.toList()
                if (files.isNotEmpty()) existingCourses += course
                inventory += files.map { it.relativeTo(assets()).path }
                for (file in files) {
                    scanned += file.relativeTo(assets()).path
                    val lesson = JSONObject(file.readText())
                    val before = lesson.toString()
                    val pages = lesson.optJSONArray("pages") ?: continue
                    for (pageIndex in 0 until pages.length()) {
                        val blocks = pages.optJSONObject(pageIndex)?.optJSONArray("blocks") ?: continue
                        for (blockIndex in 0 until blocks.length()) {
                            val block = blocks.optJSONObject(blockIndex) ?: continue
                            val source = listOf("text", "question", "prompt").firstNotNullOfOrNull {
                                (block.opt(it) as? String)?.takeIf(String::isNotBlank)
                            } ?: continue
                            if (!technical.containsMatchIn(source)) continue
                            assertMapping(source, block.optString("mathMode", "auto"), language,
                                "$course/${file.name}/p=${pageIndex + 1}/b=$blockIndex")
                            count++
                            seenCourses += course
                        }
                    }
                    assertEquals(before, lesson.toString())
                }
            }
            assertEquals("Every installed translated lesson is scanned for $language", inventory, scanned)
            assertEquals("Every course with translated lessons is exercised for $language", existingCourses, seenCourses)
            assertTrue("Exercise actual translated technical content for $language", count > 0)
        }
    }

    @Test fun existingSecondCourseSourcesRequireNoPreparationForAnotherNarrationLanguage() {
        // Course 2 currently has no EN/FR lesson translations. Exercise its actual ES source
        // as a source-projection fixture in each language, without inventing translated content.
        val root = assets().resolve("es/programming/programming_practice_tools")
        val technical = Regex("#include|std::|[{}]|<<|>>|->|&&|\\|\\|")
        var exercised = 0
        for (file in root.walkTopDown().filter { it.isFile && it.name.startsWith("lesson_") && it.extension == "json" }) {
            val lesson = JSONObject(file.readText())
            val before = lesson.toString()
            val pages = lesson.optJSONArray("pages") ?: continue
            for (pageIndex in 0 until pages.length()) {
                val blocks = pages.optJSONObject(pageIndex)?.optJSONArray("blocks") ?: continue
                for (blockIndex in 0 until blocks.length()) {
                    val block = blocks.optJSONObject(blockIndex) ?: continue
                    val source = listOf("text", "question", "prompt").firstNotNullOfOrNull {
                        (block.opt(it) as? String)?.takeIf(String::isNotBlank)
                    } ?: continue
                    if (!technical.containsMatchIn(source)) continue
                    for (language in languages) assertMapping(source, block.optString("mathMode", "auto"), language,
                        "ES-course2-source/${file.name}/p=${pageIndex + 1}/b=$blockIndex")
                    exercised++
                }
            }
            assertEquals(before, lesson.toString())
        }
        assertTrue("Existing Course 2 must supply real source fixtures", exercised > 0)
    }
}
