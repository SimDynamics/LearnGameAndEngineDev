package com.learngraphicsprogramming.app.narration

import com.learngraphicsprogramming.app.narration.phonemes.SpanishFluidG2P
import java.io.File
import javax.xml.parsers.DocumentBuilderFactory
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** Published indices exercise the same available content as offline Android preparation. */
class MixedSpanishCurriculumTest {
    private data class Counts(
        var lessons: Int = 0,
        var pages: Int = 0,
        var sentences: Int = 0,
        var matchedSentences: Int = 0,
        var englishOccurrences: Int = 0,
        var newKeys: Int = 0,
        var unchangedKeys: Int = 0
    ) {
        fun json(): JSONObject = JSONObject()
            .put("lessons", lessons).put("pages", pages).put("sentences", sentences)
            .put("matchedSentences", matchedSentences).put("englishOccurrences", englishOccurrences)
            .put("newKeys", newKeys).put("unchangedKeys", unchangedKeys)
    }

    @Test fun everyIndexedSentenceSupportsMixedPhonesAndSelectiveCacheWithoutChangingAuthoredSpeech() {
        val resources = File(System.getProperty("translation.resources") ?: "src/main/res")
        val assets = requireNotNull(resources.absoluteFile.parentFile).resolve("assets")
        val root = assets.resolve("content/es")
        val routes = listOf("programming", "math_physics")
        val files = linkedMapOf<File, String>()
        for (route in routes) {
            val courses = JSONArray(root.resolve("$route/courses.json").readText())
            for (courseIndex in 0 until courses.length()) {
                val course = courses.getJSONObject(courseIndex).getString("id")
                val courseFile = root.resolve("$route/$course/course_$course.json")
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
                        if (file.isFile) files[file] = route
                    }
                }
            }
        }
        assertTrue("No indexed Spanish narration was found", files.isNotEmpty())
        assertEquals(ManualEnglishTerms.entries.map { it.id }.toSet(), ManualEnglishPronunciations.phones.keys)

        val nodes = DocumentBuilderFactory.newInstance().newDocumentBuilder()
            .parse(resources.resolve("values/strings.xml")).getElementsByTagName("string")
        val strings = (0 until nodes.length).map(nodes::item).associate {
            it.attributes.getNamedItem("name").nodeValue to it.textContent
        }
        val instructions = mapOf(
            "order_code" to strings.getValue("exercise_order_instruction"),
            "drag_fill_code" to strings.getValue("exercise_fill_instruction")
        )
        // When an existing model directory is supplied, use its verified published
        // tokenizer as an additional independent check. No model is downloaded.
        val modelDirectory = System.getenv("LGP_KOKORO_DIRECTORY") ?: System.getenv("LGP_MISAKI_DIRECTORY")
        val tokenizer = modelDirectory?.let { File(it, "tokenizer.json") }?.takeIf { it.isFile }
        val vocabulary = tokenizer?.let { file ->
            val artifact = KokoroModelManifest.sharedArtifacts.single { it.fileName == "tokenizer.json" }
            assertTrue("The optional Kokoro tokenizer must pass the pinned SHA-256", KokoroModelManifest.verify(file, artifact))
            KokoroOnnxInputs.loadVocabulary(file)
        }

        val total = Counts()
        val byRoute = routes.associateWith { Counts() }
        val terms = sortedMapOf<String, Int>()
        val mathMatches = JSONArray()
        val failures = mutableListOf<String>()
        val changesInStrippedText = mutableListOf<String>()
        val sentenceBoundaryChanges = mutableListOf<String>()
        fun failure(location: String, error: Throwable) {
            failures += "$location: ${error.message ?: error.javaClass.simpleName}"
        }
        for ((file, route) in files) {
            val lesson = JSONObject(file.readText())
            val authored = lesson.toString()
            val routeCounts = byRoute.getValue(route)
            total.lessons++; routeCounts.lessons++
            val pages = lesson.getJSONArray("pages")
            for (page in 0 until pages.length()) {
                total.pages++; routeCounts.pages++
                val location = "${file.relativeTo(root)} page ${page + 1}"
                val plain: List<String>
                val prepared: List<String>
                try {
                    plain = LessonNarrationText.page(lesson, page, NarrationLanguage.SPANISH) { instructions[it].orEmpty() }
                    prepared = LessonNarrationText.page(lesson, page, NarrationLanguage.SPANISH, forKokoro = true) {
                        instructions[it].orEmpty()
                    }
                    val stripped = prepared.map(ManualEnglishSpeech::strip)
                    val selected = prepared.flatMap(ManualEnglishSpeech::spans)
                    // Metadata must preserve every word and each legacy split boundary.
                    if (plain.joinToString(" ") != stripped.joinToString(" ")) {
                        changesInStrippedText += "$location\nlegacy: ${plain.joinToString(" ")}\nstripped: ${stripped.joinToString(" ")}"
                    }
                    if (selected.isEmpty()) assertEquals("Unchanged page speech at $location", plain, stripped)
                } catch (error: Throwable) {
                    failure(location, error)
                    continue
                }
                val sentences = try {
                    val mixedSentences = NarrationSegments.sentences(prepared)
                    val legacySentences = NarrationSegments.sentences(plain)
                    val strippedSentences = mixedSentences.map(ManualEnglishSpeech::strip)
                    if (legacySentences != strippedSentences) {
                        val firstDifference = (0 until maxOf(legacySentences.size, strippedSentences.size)).first {
                            legacySentences.getOrNull(it) != strippedSentences.getOrNull(it)
                        }
                        sentenceBoundaryChanges += "$location sentence ${firstDifference + 1}" +
                            "\nlegacy (${legacySentences.size}): ${legacySentences.getOrNull(firstDifference)}" +
                            "\nstripped (${strippedSentences.size}): ${strippedSentences.getOrNull(firstDifference)}"
                    }
                    mixedSentences
                } catch (error: Throwable) {
                    failure(location, error)
                    continue
                }
                for ((index, sentence) in sentences.withIndex()) {
                    total.sentences++; routeCounts.sentences++
                    try {
                        val selected = ManualEnglishSpeech.spans(sentence)
                        total.englishOccurrences += selected.size; routeCounts.englishOccurrences += selected.size
                        selected.forEach { terms[it.entry.spelling] = terms.getOrDefault(it.entry.spelling, 0) + 1 }
                        if (selected.isNotEmpty()) {
                            total.matchedSentences++; routeCounts.matchedSentences++
                            if (route == "math_physics") mathMatches.put(JSONObject()
                                .put("location", "$location sentence ${index + 1}")
                                .put("text", ManualEnglishSpeech.strip(sentence))
                                .put("terms", JSONArray(selected.map { it.entry.spelling })))
                        }
                        val spoken = ManualEnglishSpeech.strip(sentence)
                        val phones = MixedSpanishG2P.phonemizePrepared(sentence, emptyMap())
                        assertTrue("Empty mixed phones", phones.isNotBlank())
                        val chunks = KokoroPhonemeChunks.split(phones)
                        assertEquals("Phoneme chunking must preserve every symbol", phones, chunks.joinToString(""))
                        assertTrue(chunks.all { it.isNotBlank() && it.length <= KokoroModelManifest.maximumPhonemeTokens })
                        vocabulary?.let { table -> chunks.forEach { KokoroOnnxInputs.tokenize(it, table) } }

                        val plainKey = KokoroNarrationCache.key("es", "ef_dora", spoken)
                        val mixedKey = KokoroNarrationCache.key("es", "ef_dora", sentence)
                        assertTrue("The presence of selected English must be the only reason for a new key",
                            (plainKey != mixedKey) == selected.isNotEmpty())
                        if (mixedKey != plainKey) {
                            total.newKeys++; routeCounts.newKeys++
                        } else {
                            total.unchangedKeys++; routeCounts.unchangedKeys++
                            assertEquals("Unselected Spanish phones must remain unchanged",
                                SpanishFluidG2P.phonemize(spoken), phones)
                        }
                    } catch (error: Throwable) {
                        val diagnosticText = runCatching { ManualEnglishSpeech.strip(sentence) }.getOrDefault(sentence)
                        failure("$location sentence ${index + 1}: $diagnosticText", error)
                    }
                }
            }
            assertEquals("Narration must never mutate the authored JSON: ${file.relativeTo(root)}", authored, lesson.toString())
        }
        val report = JSONObject().put("total", total.json())
            .put("routes", JSONObject(byRoute.mapValues { it.value.json() }))
            .put("selectedTerms", JSONObject(terms)).put("verifiedTokenizer", vocabulary != null)
            .put("mathMatches", mathMatches)
            .put("sentenceBoundaryChanges", sentenceBoundaryChanges.size)
            .put("strippedSpeechChanges", changesInStrippedText.size).put("failures", failures.size)
        println("MIXED_SPANISH_CURRICULUM $report")
        assertTrue("The curriculum contains no narrated sentences", total.sentences > 0)
        assertTrue("${changesInStrippedText.size} pages changed legacy spoken text:\n${changesInStrippedText.take(8).joinToString("\n")}",
            changesInStrippedText.isEmpty())
        assertTrue("${sentenceBoundaryChanges.size} pages changed legacy sentence boundaries:\n${sentenceBoundaryChanges.take(8).joinToString("\n")}",
            sentenceBoundaryChanges.isEmpty())
        assertTrue("${failures.size} of ${total.sentences} mixed sentences failed:\n${failures.take(20).joinToString("\n")}",
            failures.isEmpty())
        assertEquals(total.sentences, total.newKeys + total.unchangedKeys)
        assertEquals(total.matchedSentences, total.newKeys)
    }
}
