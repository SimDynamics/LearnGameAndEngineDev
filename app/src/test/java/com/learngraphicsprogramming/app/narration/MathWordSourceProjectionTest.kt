package com.learngraphicsprogramming.app.narration

import java.io.File
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class MathWordSourceProjectionTest {
    @Test fun decompositionFeedbackOwnsEachNumberAndOperatorBeforeItsFollowingProse() {
        val source = "6000 + 300 + 8 = 6308; no hay decenas y se conserva esa posición con cero."
        val (parts, mapping) = mapping(source, NarrationLanguage.SPANISH)
        assertEquals("Every spoken segment needs original source ownership: $parts", parts.keys, mapping.keys)
        val atoms = Regex("6000|300|6308|8|[+=]").findAll(source).toList()
        val owners = atoms.map { atom ->
            mapping.values.flatMap { it.displayed }.distinct().single {
                atom.range.first in it.start until it.endExclusive
            }
        }
        assertEquals("The expression must reveal seven original atoms, not a single formula: $parts/$mapping",
            atoms.size, owners.distinct().size)
        owners.zipWithNext().forEach { (before, after) -> assertTrue(before.endExclusive <= after.start) }
        assertTrue("Following prose cannot own the complete expression", owners.last().endExclusive <= source.indexOf("no hay"))
    }

    @Test fun observedFlatMathematicsKeepsExactAudioInputAndIndividualSourceAtomsInEveryLanguage() {
        for (language in NarrationLanguage.entries) for (expression in listOf(
            "6 × 100 = 600", "5006 = 5000 + 0 + 0 + 6", "3047=3000+0+40+7",
            "8 ÷ 2 ≤ 4", "3 · 2 ≠ 5", "-2 + 4 ≥ 1", "2,5 + 1 = 3,5")) {
            val source = "\\($expression\\)"
            val pieces = mutableListOf<Triple<Int, Int, String>>()
            val spoken = MultilingualMathSpeech.textObserved(source, "auto", language) { start, end, text ->
                pieces += Triple(start, end, text)
            }
            assertEquals("Observation cannot change compatible audio: $language/$source",
                MultilingualMathSpeech.text(source, "auto", language), spoken)
            assertEquals(spoken.filterNot(Char::isWhitespace), pieces.joinToString("") { it.third }.filterNot(Char::isWhitespace))
            assertTrue("The relation has its own real pronunciation owner: $language/$source", pieces.any {
                source.substring(it.first, it.second) in listOf("=", "≤", "≠", "≥")
            })
            assertTrue(pieces.all { it.first >= 0 && it.first < it.second && it.second <= source.length })
        }
    }

    @Test fun actualMathPageAndFeedbackKeepEverySpokenSegmentAndEveryVisibleGlyph() {
        val assets = requireNotNull(File(System.getProperty("translation.resources") ?: "src/main/res").absoluteFile.parentFile)
            .resolve("assets/content")
        for (language in NarrationLanguage.entries) {
            val folder = when (language) {
                NarrationLanguage.SPANISH -> "es"
                NarrationLanguage.FRENCH -> "fr"
                else -> "en"
            }
            val lesson = JSONObject(assets.resolve("$folder/math_physics/math_course_01/numeros_y_operaciones/lesson_magnitudes_y_numeracion.json").readText())
            val original = lesson.toString()
            val blocks = lesson.getJSONArray("pages").getJSONObject(1).getJSONArray("blocks")
            val sources = listOf(blocks.getJSONObject(1).getString("text"),
                blocks.getJSONObject(4).getJSONObject("feedback").getString("correct"),
                blocks.getJSONObject(6).getJSONObject("feedback").getString("correct"))
            sources.forEach { source ->
                val (parts, mapping) = mapping(source, language)
                assertEquals("All native narration segments retain source ownership: $language/$source", parts.keys, mapping.keys)
                assertEquals(source.indices.toSet(), mapping.values.flatMap { value ->
                    value.displayed.flatMap { it.start until it.endExclusive }
                }.toSet())
                mapping.forEach { (index, value) ->
                    assertEquals(NarrationSpokenWords.technicalAtoms(parts.getValue(index), language).map {
                        ReadingTextRange(it.start, it.endExclusive)
                    }, value.spoken)
                }
            }
            assertEquals(original, lesson.toString())
        }
    }

    @Test fun aRelationAndItsResultCannotBorrowTheFirstNumbersPlaybackRange() {
        val source = "Antes \\(6 × 100 = 600\\) después."
        val (_, mapping) = mapping(source, NarrationLanguage.SPANISH)
        val ranges = mapping.values.flatMap { it.displayed }.distinct()
        val first = source.indexOf('6')
        val equal = source.indexOf('=')
        val result = source.indexOf("600")
        val firstOwner = ranges.single { first in it.start until it.endExclusive }
        val relationOwner = ranges.single { equal in it.start until it.endExclusive }
        val resultOwner = ranges.single { result in it.start until it.endExclusive }
        assertTrue(firstOwner.endExclusive <= relationOwner.start)
        assertTrue(relationOwner.endExclusive <= resultOwner.start)
        assertTrue("Prose after the formula retains its own words", ranges.any { it.start >= source.indexOf("después") })
    }

    @Test fun aResidualMathSignWithoutItsOwnLexicalTimingStaysWithTheFollowingOperand() {
        val source = "El 6 aporta 6 × 100 = 600."
        val (_, mapping) = mapping(source, NarrationLanguage.SPANISH)
        val ranges = mapping.values.flatMap { it.displayed }.distinct()
        val relation = source.indexOf('=')
        val preceding = source.indexOf("100")
        val following = source.indexOf("600")
        val relationOwner = ranges.single { relation in it.start until it.endExclusive }
        assertTrue("The sign cannot turn read with the preceding operand before it is spoken", preceding < relationOwner.start)
        assertTrue("Without a separate lexical range, retain the sign conservatively through the following actual word",
            following in relationOwner.start until relationOwner.endExclusive)
    }

    private fun mapping(source: String, language: NarrationLanguage): Pair<Map<Int, String>, Map<Int, NarrationWordMapping>> {
        val lesson = JSONObject().put("pages", JSONArray().put(JSONObject().put("blocks", JSONArray()
            .put(JSONObject().put("type", "paragraph").put("text", source)))))
        val script = LessonNarrationScript.page(lesson, 0, language, forKokoro = true)
        val parts = NarrationSegments.sentences(script.texts).withIndex().associate { it.index to it.value }
        return parts to NarrationWordMappings.forBlock(0, source, parts, language)
    }
}
