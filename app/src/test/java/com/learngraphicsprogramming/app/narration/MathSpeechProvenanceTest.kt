package com.learngraphicsprogramming.app.narration

import java.io.File
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class MathSpeechProvenanceTest {
    private data class Piece(val start: Int, val end: Int, val spoken: String)

    private fun observed(source: String, mode: String, language: NarrationLanguage): List<Piece> {
        val pieces = mutableListOf<Piece>()
        val actual = MultilingualMathSpeech.textObserved(source, mode, language) { start, end, spoken ->
            pieces += Piece(start, end, spoken)
        }
        assertEquals("Observing must not change speech: $language/$mode/$source",
            MultilingualMathSpeech.text(source, mode, language), actual)
        if (source.isNotBlank()) {
            assertEquals(source.indices.toList(), pieces.flatMap { it.start until it.end })
            assertEquals(actual, clean(pieces.joinToString("") { it.spoken }))
        }
        return pieces
    }

    private fun clean(value: String) = value.replace(Regex("\\s+"), " ")
        .replace(Regex("\\s+([,.;:!?])"), "$1").trim()

    @Test fun replacementsCarryTheirConsumedRangesWithoutChangingAnyLanguageSpeech() {
        val examples = listOf(
            "Antes \\(\\frac{3}{5}\\), después x² y √9; fin." to "auto",
            "  3/4  " to "auto",
            "x \\times 3" to "block",
            "x \\unsupported{3}" to "inline",
            "Texto 3/4; std::cout << 7; { } ¿fin?" to "text",
            "El valor π y la relación ∈ llegan después." to "auto",
            "Abre C:/1/2/main.cpp https://example.com/1/2 el 08/10/2026 y `1/2`." to "auto"
        )
        NarrationLanguage.entries.forEach { language ->
            examples.forEach { (source, mode) -> observed(source, mode, language) }
        }
    }

    @Test fun repeatedFractionsKeepDistinctOriginalLocationsAndOrdinaryProsePieces() {
        val source = "Antes 3/4; después 3/4, fin."
        val pieces = observed(source, "auto", NarrationLanguage.SPANISH)
        val fractions = pieces.filter { source.substring(it.start, it.end) == "3/4" }
        assertEquals(2, fractions.size)
        assertTrue(fractions[0].end < fractions[1].start)
        assertEquals(listOf("Antes ", "; después ", ", fin."), pieces.filter {
            source.substring(it.start, it.end) != "3/4"
        }.map { it.spoken })
        assertEquals(listOf(" tres cuartos ", " tres cuartos "), fractions.map { it.spoken })
    }

    @Test fun aWholeStructuredFormulaUsesOneExactSourceRangeRatherThanInventingSubwordOffsets() {
        val source = "  \\frac{x+1}{2}  "
        val pieces = observed(source, "block", NarrationLanguage.SPANISH)
        assertEquals(1, pieces.size)
        assertEquals(0, pieces.single().start)
        assertEquals(source.length, pieces.single().end)
        assertTrue(pieces.single().spoken.startsWith("fracción con numerador"))
    }

    @Test fun existingProgrammingAndMathematicsMathSourcesPreserveSpeechAndSourceCoverage() {
        val assets = requireNotNull(File(System.getProperty("translation.resources") ?: "src/main/res")
            .absoluteFile.parentFile).resolve("assets/content")
        val relevant = Regex("[\\\\${'$'}×÷≤≥≠≈π∞∈^²³√]|\\d/\\d")
        val checkedRoutes = mutableSetOf<String>()
        var checked = 0
        for ((tag, language) in listOf("es" to NarrationLanguage.SPANISH,
                "en" to NarrationLanguage.AMERICAN_ENGLISH, "fr" to NarrationLanguage.FRENCH)) {
            for (route in listOf("programming", "math_physics")) {
                for (file in assets.resolve("$tag/$route").walkTopDown().filter {
                    it.isFile && it.name.startsWith("lesson_") && it.extension == "json"
                }) {
                    val original = file.readText()
                    val lesson = JSONObject(original)
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
                            observed(source, block.optString("mathMode", "auto"), language)
                            checkedRoutes += "$tag/$route"
                            checked++
                        }
                    }
                    assertEquals(snapshot, lesson.toString())
                    assertEquals(original, file.readText())
                }
            }
        }
        assertEquals(setOf("es/programming", "es/math_physics", "en/programming",
            "en/math_physics", "fr/programming", "fr/math_physics"), checkedRoutes)
        assertTrue("The corpus must exercise real existing mixed mathematical text", checked > 300)
    }
}
