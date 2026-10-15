package com.learngraphicsprogramming.app.narration.phonemes

import java.text.Normalizer
import org.junit.Assert.*
import org.junit.Test

class SpanishWordPhonemeTraceTest {
    @Test fun ordinarySentencesUseExactlyTheExistingWholeSentencePhonemes() {
        val samples = listOf(
            "Por ello, cuando se inventó, el lápiz ganó muchos amigos.",
            "Estas parejas podrían elegir planificar la adopción de un bebé.",
            "Las fracciones equivalentes representan la misma cantidad.",
            "Hoy es muy fácil, generalmente.",
            "La administración conserva nuestra posición de lectura."
        )
        samples.forEach { source ->
            val trace = requireNotNull(SpanishFluidG2P.traceWords(source))
            assertEquals(source, SpanishFluidG2P.phonemize(source), trace.phonemes)
            assertTrue(trace.words.isNotEmpty())
            trace.words.forEach { word ->
                assertTrue(word.word, word.start in trace.phonemes.indices)
                assertTrue(word.word, word.endExclusive in (word.start + 1)..trace.phonemes.length)
                assertFalse(trace.phonemes.substring(word.start, word.endExclusive).isBlank())
            }
            trace.words.zipWithNext().forEach { (left, right) -> assertTrue(left.endExclusive <= right.start) }
        }
    }

    @Test fun contextAllophonesAndRepeatedWordsKeepTheirDistinctOwnership() {
        val trace = requireNotNull(SpanishFluidG2P.traceWords("La vida de los gatos, la vida de los gatos."))
        assertEquals(listOf("la", "vida", "de", "los", "gatos", "la", "vida", "de", "los", "gatos"),
            trace.words.map { it.word })
        assertEquals("βˈiða", trace.phones(1))
        assertEquals("ðe", trace.phones(2))
        assertEquals("ɣˈatos", trace.phones(4))
        assertNotEquals(trace.words[0].start, trace.words[5].start)
        val nasal = requireNotNull(SpanishFluidG2P.traceWords("un vaso"))
        assertTrue(nasal.phones(0).endsWith("m"))
        assertEquals(SpanishFluidG2P.phonemize("un vaso"), nasal.phonemes)
    }

    @Test fun punctuationChangesOffsetsButIsNotAttributedToTheLexicalWord() {
        val source = "¿Dónde está el capítulo? ¡NO, HOLA!"
        val trace = requireNotNull(SpanishFluidG2P.traceWords(source))
        assertEquals(SpanishFluidG2P.phonemize(source), trace.phonemes)
        assertEquals(listOf("dónde", "está", "el", "capítulo", "no", "hola"), trace.words.map { it.word })
        assertEquals(0, trace.words.first().start)
        assertFalse(trace.phonemes.contains('¿'))
        assertFalse(trace.phonemes.contains('¡'))
        trace.words.forEachIndexed { index, _ -> assertFalse(trace.phones(index).any { it in ",.!?" }) }
        assertEquals('?', trace.phonemes[trace.words[3].endExclusive])
        assertEquals('!', trace.phonemes[trace.words.last().endExclusive])
    }

    @Test fun unicodeAccentsCaseWhitespaceAndQuotationKeepCanonicalLexemes() {
        val source = "  «Él»\tleyó\n\nla PALABRA; él leyó.  "
        val canonical = requireNotNull(SpanishFluidG2P.traceWords(source))
        val decomposed = requireNotNull(SpanishFluidG2P.traceWords(Normalizer.normalize(source, Normalizer.Form.NFD)))
        assertEquals(listOf("él", "leyó", "la", "palabra", "él", "leyó"), canonical.words.map { it.word })
        assertEquals(canonical, decomposed)
        assertEquals(SpanishFluidG2P.phonemize(source), canonical.phonemes)
    }

    @Test fun stressNearPunctuationAndAcronymsRemainSentenceContextual() {
        val source = "La BBC, tengo que."
        val trace = requireNotNull(SpanishFluidG2P.traceWords(source))
        assertEquals(listOf("la", "bbc", "tengo", "que"), trace.words.map { it.word })
        assertEquals(SpanishFluidG2P.phonemize(source), trace.phonemes)
        assertEquals("kˈe", trace.phones(3))
        assertTrue(trace.phones(1).contains("ˌ"))
    }

    @Test fun verifiedLexiconPronunciationsKeepTheirActualPhoneRanges() {
        val lexicon = mapOf("web" to "wˈeb", "bueno" to "bwˈeno")
        val source = "La web, muy bueno."
        val trace = requireNotNull(SpanishFluidG2P.traceWords(source, lexicon))
        assertEquals(SpanishFluidG2P.phonemize(source, lexicon), trace.phonemes)
        assertEquals("wˈeb", trace.phones(1))
        assertEquals("βwˈeno", trace.phones(3))
    }

    @Test fun expansionsMathCodeAndOpaquePronunciationAnnotationsHaveNoInferredWordTrace() {
        listOf("12 casas", "x²", "3/4", "C++", "C más más", "std::cout", "a + b", "a = b",
            "\\frac{1}{2}", "m_nombre", "int x = 4;", "90°", "cm³", "\uE002Hola", "\uE000cout\uE001")
            .forEach { assertNull(it, SpanishFluidG2P.traceWords(it)) }
    }

    @Test fun invalidOrNonLexicalInputsFailClosedWithoutChangingTheFrontend() {
        listOf("", " \n\t", "...", "中文", "casa 😀", "casa \uD800", "a".repeat(32_769))
            .forEach { assertNull(it.take(80), SpanishFluidG2P.traceWords(it)) }
        assertNull(SpanishFluidG2P.traceWords("bueno", mapOf("bueno" to "")))
        assertNull(SpanishFluidG2P.traceWords("bueno", mapOf("bueno" to "$")))
    }

    private fun SpanishFluidG2P.SpanishPhoneTrace.phones(index: Int): String =
        words[index].let { phonemes.substring(it.start, it.endExclusive) }
}
