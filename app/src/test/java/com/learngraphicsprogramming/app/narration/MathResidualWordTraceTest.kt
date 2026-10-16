package com.learngraphicsprogramming.app.narration

import com.learngraphicsprogramming.app.narration.phonemes.SpanishFluidG2P
import org.junit.Assert.*
import org.junit.Test

class MathResidualWordTraceTest {
    @Test fun rawResidualEqualityConsumesItsRealPhonesWithoutInventingALexicalWord() {
        val prepared = ManualEnglishSpeech.PREPARED + "El seis aporta seis por cien = 600."
        val trace = assertTrace(prepared)
        assertEquals(listOf("el", "seis", "aporta", "seis", "por", "cien", "600"), trace.words.map { it.word })
        val before = trace.words[5]
        val after = trace.words[6]
        assertTrue("The spoken equality occupies real phones between its operands", before.endExclusive < after.start)
        assertFalse(trace.phonemes.substring(before.endExclusive, after.start).isBlank())
    }

    @Test fun bothReportedGreenFeedbacksRetainWholeProductionPhonesAndEveryOriginalLexicalOwner() {
        for (visible in listOf(
            "El 6 está en las centenas y aporta 6 × 100 = 600.",
            "5006 = 5000 + 0 + 0 + 6. Los dos ceros mantienen centenas y decenas."
        )) {
            val prepared = prepared(visible)
            assertTrue(visible, '=' in prepared)
            NarrationSegments.sentences(listOf(prepared)).forEach(::assertTrace)
        }
    }

    @Test fun existingScriptNormalizesSupportedNumericOperatorsWithoutChangingTheProductionFrontend() {
        for (expression in listOf("6 + 2", "6 - 2", "6 − 2", "6 × 2", "6 ÷ 2", "6 · 2", "6/2",
            "6 = 2", "6 < 2", "6 > 2", "6 ≤ 2", "6 ≥ 2", "6 ≠ 2", "6 ≈ 2")) {
            NarrationSegments.sentences(listOf(prepared("Calcula $expression."))).forEach(::assertTrace)
        }
    }

    @Test fun normalProseCppNamesAndReviewedEnglishAnnotationsKeepTheirExistingLexicalContract() {
        for (visible in listOf("La vida de los gatos continúa.", "C++ conserva 21 opciones.",
            "La línea #include <iostream> permite enviar texto con std::cout.")) {
            NarrationSegments.sentences(listOf(prepared(visible))).forEach(::assertTrace)
        }
        val plain = "La vida de los gatos."
        assertEquals(SpanishFluidG2P.traceWords(plain),
            MixedSpanishG2P.traceWordsPrepared(ManualEnglishSpeech.PREPARED + plain))
    }

    @Test fun nonlinearOrUnsupportedTransformsStillFailClosedInsteadOfInventingAlignment() {
        for (source in listOf("x = 2", "x²", "a + b", "3/4", "std::cout", "a < > b", "a 😀")) {
            assertNull(source, MixedSpanishG2P.traceWordsPrepared(ManualEnglishSpeech.PREPARED + source))
        }
    }

    private fun prepared(visible: String) = ManualEnglishSpeech.protect(
        SpanishTechnicalSpeech.prepareForKokoro(visible).text) { SpanishMathSpeech.text(it) }

    private fun assertTrace(source: String): SpanishFluidG2P.SpanishPhoneTrace {
        val trace = requireNotNull(MixedSpanishG2P.traceWordsPrepared(source)) { source }
        assertEquals(source, MixedSpanishG2P.phonemizePrepared(source, emptyMap()), trace.phonemes)
        assertEquals(source, NarrationSpokenWords.lexemes(source).map { it.canonical }, trace.words.map { it.word })
        trace.words.forEach {
            assertTrue(it.word, it.start >= 0 && it.start < it.endExclusive && it.endExclusive <= trace.phonemes.length)
            assertFalse(it.word, trace.phonemes.substring(it.start, it.endExclusive).isBlank())
        }
        trace.words.zipWithNext().forEach { (before, after) -> assertTrue(before.endExclusive <= after.start) }
        return trace
    }
}
