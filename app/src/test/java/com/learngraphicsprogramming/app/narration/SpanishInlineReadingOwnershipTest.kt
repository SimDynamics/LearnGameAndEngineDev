package com.learngraphicsprogramming.app.narration

import org.junit.Assert.*
import org.junit.Test

/** The observer changes source ownership only; production speech and audio identity stay exact. */
class SpanishInlineReadingOwnershipTest {
    private data class Piece(val start: Int, val end: Int, val speech: String)

    @Test fun inlineCodeDelimitersAndEachTechnicalPieceHaveSeparateOriginalOwners() {
        for (delimiter in listOf("`", "```")) {
            val source = "Lee ${delimiter}std::cout << 7;${delimiter} después."
            val pieces = mutableListOf<Piece>()
            val observed = SpanishTechnicalSpeech.prepareForReading(source) { start, end, speech ->
                pieces += Piece(start, end, speech)
            }
            val production = SpanishTechnicalSpeech.prepareForKokoro(source)
            assertEquals("Observing offsets cannot change production speech", production, observed)
            val openingSpeech = if (delimiter.length == 3) "código, " else " "
            val closingSpeech = if (delimiter.length == 3) ", fin de código" else " "
            val expected = if (delimiter.length == 3)
                "Lee código, ese te de operador de resolución de ámbito cout doble menor que siete punto y coma, fin de código después."
                else "Lee ese te de operador de resolución de ámbito cout doble menor que siete punto y coma después."
            assertEquals(expected,
                ManualEnglishSpeech.strip(observed.text))
            assertEquals(source.indices.toList(), pieces.flatMap { it.start until it.end })
            val opening = source.indexOf(delimiter)
            val closing = source.lastIndexOf(delimiter)
            assertTrue(pieces.any { it.start == opening && it.end == opening + delimiter.length && it.speech == openingSpeech })
            assertTrue(pieces.any { it.start == closing && it.end == closing + delimiter.length && it.speech == closingSpeech })
            val chunks = pieces.filter { it.start < it.end }.map { source.substring(it.start, it.end) }
            assertTrue(chunks.toString(), chunks.containsAll(listOf("std", "::", "cout", "<<", "7", ";")))
            assertFalse("A delimiter cannot own its entire code expression", pieces.any {
                it.start <= opening && it.end >= closing + delimiter.length
            })
            fun cleaned(value: String) = value.removePrefix(ManualEnglishSpeech.PREPARED.toString())
                .replace(Regex("[ \\t]+"), " ").replace(Regex(" +([,.;:!?])"), "$1").trim()
            assertEquals(cleaned(observed.text), cleaned(pieces.joinToString("") { it.speech }))
            assertEquals(KokoroNarrationCache.key("es", "ef_dora", production.text),
                KokoroNarrationCache.key("es", "ef_dora", observed.text))
            assertEquals(NarrationResumePolicy.key("page", "es", "KOKORO", "ef_dora", listOf(production.text)),
                NarrationResumePolicy.key("page", "es", "KOKORO", "ef_dora", listOf(observed.text)))
        }
    }

    @Test fun inlineCodeProjectionNeverGroupsItsWordsWithTheRestOfTheParagraph() {
        val source = "Primero `std::cout << 7;` y luego seguimos leyendo."
        val projection = requireNotNull(NarrationSourceProjection.spanish(source, "auto"))
        val segments = NarrationSegments.sentences(listOf(projection.text)).withIndex().associate { it.index to it.value }
        val mappings = NarrationWordMappings.forBlock(0, source, segments)
        assertEquals(segments.keys, mappings.keys)
        assertEquals(source.indices.toSet(), mappings.values.flatMap { it.displayed.flatMap { range ->
            range.start until range.endExclusive
        } }.toSet())
        val open = source.indexOf('`')
        val close = source.lastIndexOf('`')
        val std = source.indexOf("std")
        val scope = source.indexOf("::")
        val cout = source.indexOf("cout")
        val later = source.indexOf("seguimos")
        val displayed = mappings.values.flatMap { it.displayed }
        assertFalse("A silent delimiter must not create a narrated word", displayed.any {
            it.start == open && it.endExclusive == open + 1
        })
        for ((start, end) in listOf(std to scope, scope to cout, cout to source.indexOf("<<"))) {
            assertTrue("The real technical pieces have distinct glyph owners", displayed.any {
                it.start in open..start && it.endExclusive == end
            })
        }
        assertFalse("The closing delimiter must also remain silent", displayed.any {
            it.start == close && it.endExclusive <= close + 2
        })
        assertTrue(displayed.any { it.start == later && it.endExclusive <= later + "seguimos ".length })
        assertFalse("No early code word may illuminate the entire expression", displayed.any {
            it.start <= open && it.endExclusive >= close + 1
        })
        assertFalse("No code token may borrow later prose", displayed.any { it.start in open..close && it.endExclusive > later })
    }

    @Test fun separateInlineFragmentsAndAnOrdinaryFormulaKeepTheirOwnSourceBoundaries() {
        val source = "Usa `std::cout` antes de `std::cin`; luego calcula \\(x + 2\\) y continúa."
        val pieces = mutableListOf<Piece>()
        val observed = SpanishTechnicalSpeech.prepareForReading(source) { start, end, speech ->
            pieces += Piece(start, end, speech)
        }
        assertEquals(SpanishTechnicalSpeech.prepareForKokoro(source), observed)
        assertEquals(source.indices.toList(), pieces.flatMap { it.start until it.end })
        val delimiters = source.indices.filter { source[it] == '`' }
        assertEquals(4, delimiters.size)
        assertTrue(delimiters.all { delimiter -> pieces.any { it.start == delimiter && it.end == delimiter + 1 } })
        val formula = source.indexOf("\\(")
        val formulaEnd = source.indexOf("\\)", formula) + 2
        assertTrue("An explicit formula remains owned by the existing math stage", pieces.any {
            it.start == formula && it.end == formulaEnd && it.speech == source.substring(formula, formulaEnd)
        })
        assertTrue(ManualEnglishSpeech.strip(observed.text).contains("\\(x + 2\\)"))
    }
}
