package com.learngraphicsprogramming.app.narration.phonemes

import com.learngraphicsprogramming.app.narration.ManualEnglishSpeech
import com.learngraphicsprogramming.app.narration.MixedSpanishG2P
import com.learngraphicsprogramming.app.narration.NarrationSpokenWords
import org.junit.Assert.*
import org.junit.Test

class ContextualSlashWordTraceTest {
    @Test fun exactCurrentRepartoExplanationRetainsItsExistingPhonesAndEveryWordOwner() {
        val source = "Si ambos operandos fueran enteros, 9 / 2 produciría 4;"
        // This is the real, previously generated continuous frontend; do not change it
        // to the pronunciation of the operator in an isolated mathematical formula.
        assertEquals("Si ambos operandos fueran enteros, nueve barra dos produciría cuatro ;",
            SpanishFluidG2P.normalizeText(source))
        assertTrace(source)
        assertEquals(listOf("si", "ambos", "operandos", "fueran", "enteros", "9", "2", "produciría", "4"),
            assertTrace(source).words.map { it.word })
    }

    @Test fun contextualSlashAndStandaloneDivisionKeepTheirOwnActualWholeUtteranceReadings() {
        for (source in listOf("Al usar 9 / 2 se conserva la expresión.",
            "Se calcula 9 / 2; después continúa.", "9 / 2", "Calcula 18 / 3 y revisa el resultado.")) {
            assertTrace(source)
        }
        val source = "Al usar 9 / 2 se conserva la expresión."
        val trace = assertTrace(source)
        val nine = trace.words.single { it.word == "9" }
        val two = trace.words.single { it.word == "2" }
        assertTrue("The operator's actually spoken phones lie between the numeric owners",
            trace.phonemes.substring(nine.endExclusive, two.start).isNotBlank())
    }

    @Test fun ambiguousRepeatedNamesAndStructuredFractionsStillRejectUnprovenOwnership() {
        for (source in listOf("nueve barra / dos", "9/2")) {
            val prepared = ManualEnglishSpeech.PREPARED + source
            val before = MixedSpanishG2P.phonemizePrepared(prepared, emptyMap())
            assertNull(source, MixedSpanishG2P.traceWordsPrepared(prepared))
            assertEquals(before, MixedSpanishG2P.phonemizePrepared(prepared, emptyMap()))
        }
    }

    private fun assertTrace(source: String): SpanishFluidG2P.SpanishPhoneTrace {
        val prepared = ManualEnglishSpeech.PREPARED + source
        val before = MixedSpanishG2P.phonemizePrepared(prepared, emptyMap())
        val trace = requireNotNull(MixedSpanishG2P.traceWordsPrepared(prepared)) { source }
        assertEquals(source, before, trace.phonemes)
        assertEquals(source, NarrationSpokenWords.lexemes(prepared).map { it.canonical }, trace.words.map { it.word })
        assertTrue(source, trace.words.all { it.start >= 0 && it.start < it.endExclusive && it.endExclusive <= trace.phonemes.length })
        assertTrue(source, trace.words.zipWithNext().all { (a, b) -> a.endExclusive <= b.start })
        assertEquals(source, before, MixedSpanishG2P.phonemizePrepared(prepared, emptyMap()))
        return trace
    }
}
