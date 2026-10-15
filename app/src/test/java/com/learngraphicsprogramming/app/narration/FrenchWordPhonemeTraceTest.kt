package com.learngraphicsprogramming.app.narration

import com.learngraphicsprogramming.app.narration.phonemes.FrenchFluidG2P
import com.learngraphicsprogramming.app.narration.phonemes.FrenchRuleBasedG2P
import org.junit.Assert.*
import org.junit.Test

/** Timings observe full-sentence phones; no independently synthesized word or guessed duration. */
class FrenchWordPhonemeTraceTest {
    private fun frontend() = FrenchFluidG2P.fromLexicon(mapOf(
        "les" to "le", "autres" to "ˈotʁ", "amis" to "amˈi", "sont" to "sˈɔ̃",
        "arrivés" to "aʁivˈe", "héros" to "eʁˈo", "ont" to "ɔ̃", "lu" to "ly",
        "la" to "la", "le" to "lə", "fonction" to "fɔ̃ksjˈɔ̃", "utilise" to "ytilˈiz",
        "main" to "mˈɛ̃", "droite" to "dʁwˈat", "reste" to "ʁˈɛst", "ouverte" to "uvˈɛʁt",
        "bonjour" to "bɔ̃ʒˈuʁ", "ami" to "amˈi", "à" to "a", "demain" to "dəmˈɛ̃"
    ), hAspire = setOf("héros"), fallback = FrenchRuleBasedG2P)

    private fun assertValid(source: String, trace: FrenchFluidG2P.FrenchPhoneTrace) {
        assertTrue(trace.words.isNotEmpty())
        trace.words.forEachIndexed { index, range ->
            assertTrue(range.sourceStart >= 0 && range.sourceEndExclusive <= source.length)
            assertTrue(range.sourceStart < range.sourceEndExclusive)
            assertEquals(source.substring(range.sourceStart, range.sourceEndExclusive), range.word)
            assertTrue(range.start >= 0 && range.start < range.endExclusive && range.endExclusive <= trace.phonemes.length)
            if (index > 0) {
                assertTrue(trace.words[index - 1].sourceEndExclusive <= range.sourceStart)
                assertTrue(trace.words[index - 1].endExclusive <= range.start)
            }
        }
    }

    @Test fun ordinaryWordsKeepTheActualWholeSentenceLiaisonAndStress() {
        val french = frontend()
        for (source in listOf("Les autres amis sont arrivés.", "Les héros ont lu.",
            "Bonjour, ami ! À demain.", "La main droite reste ouverte.")) {
            val trace = requireNotNull(french.traceWords(source)) { source }
            assertValid(source, trace)
            assertEquals(french.phonemize(source), trace.phonemes)
        }
        val trace = requireNotNull(french.traceWords("Les autres amis sont arrivés."))
        assertEquals(listOf("Les", "autres", "amis", "sont", "arrivés"), trace.words.map { it.word })
        val les = trace.words.first()
        assertEquals("lez", trace.phonemes.substring(les.start, les.endExclusive))
    }

    @Test fun elisionsAndCompoundsStayHonestWhileFixedMultiwordPhrasesRetainTheirActualWords() {
        val french = frontend()
        for (source in listOf("L'algorithme est clair.", "Va-t-il partir ?", "Tout le monde comprend parce que le code est clair.",
            "Est-ce que le programme fonctionne ?", "C'est-à-dire une fonction.")) {
            val trace = requireNotNull(french.traceWords(source)) { source }
            assertValid(source, trace)
            assertEquals(french.phonemize(source), trace.phonemes)
        }
        assertEquals(listOf("Tout", "le", "monde"), requireNotNull(french.traceWords("Tout le monde comprend.")).words.take(3).map { it.word })
        assertEquals("L'algorithme", requireNotNull(french.traceWords("L'algorithme est clair.")).words.first().word)
        assertEquals("C'est-à-dire", requireNotNull(french.traceWords("C'est-à-dire une fonction.")).words.first().word)
    }

    @Test fun fixedFusedPhrasesOwnExactExistingPhonesWithoutChangingTheirContinuousUtterance() {
        val french = frontend()
        val cases = listOf(
            Triple("Tout le monde", listOf("Tout", "le", "monde"), listOf("tu", "l", "mˈɔ̃d")),
            Triple("parce que", listOf("parce", "que"), listOf("paʁs", "kˌə")),
            Triple("Est-ce que", listOf("Est-ce", "que"), listOf("ɛs", "kə"))
        )
        for ((source, words, phoneParts) in cases) {
            val trace = requireNotNull(french.traceWords(source))
            assertValid(source, trace)
            assertEquals(french.phonemize(source), trace.phonemes)
            assertEquals(words, trace.words.map { it.word })
            assertEquals(phoneParts, trace.words.map { trace.phonemes.substring(it.start, it.endExclusive) })
            assertTrue("Each distinct source word owns a later real phoneme boundary",
                trace.words.zipWithNext().all { (first, second) -> first.endExclusive <= second.start && first.start < second.start })
        }
    }

    @Test fun fixedPhraseWordTimesUseObservedPhonemeFrameBoundariesIncludingElidedSchwa() {
        val french = frontend()
        for (source in listOf("Tout le monde", "parce que", "Est-ce que")) {
            val trace = requireNotNull(french.traceWords(source))
            // Deliberately nonuniform unit-test frames: no waveform or physical performance
            // claim is made here. Android checks the real model against actual WAV samples.
            val frames = LongArray(trace.phonemes.length + 2) { (it % 5 + 1).toLong() }
            val sampleCount = frames.sum() * NarrationWordTimingBuilder.SAMPLES_PER_FRAME
            val actual = requireNotNull(NarrationWordTimingBuilder.buildSourceChunked(source, "0".repeat(64), sampleCount,
                trace.phonemes, trace.words.map { NarrationSourcePhones(it.sourceStart, it.sourceEndExclusive, it.start, it.endExclusive) },
                listOf(trace.phonemes), listOf(frames)))
            assertTrue(NarrationWordTimingBuilder.valid(source, sampleCount, actual.words))
            for (word in actual.words) {
                val phone = trace.words.single { word.start >= it.sourceStart && word.endExclusive <= it.sourceEndExclusive }
                assertEquals(frames.take(phone.start + 1).sum() * 600L, word.startSample)
                assertEquals(frames.take(phone.endExclusive + 1).sum() * 600L, word.endSample)
            }
            assertTrue("The phrase no longer gives all of its words a shared onset", actual.words.first().startSample < actual.words.last().startSample)
        }
    }

    @Test fun numericExpansionsRetainExactOriginalSourceRanges() {
        val french = frontend()
        for (source in listOf("Le résultat est 12,05 %.", "Il existe 1001 éléments.", "Le total est 2\u202F100.")) {
            val trace = requireNotNull(french.traceWords(source)) { source }
            assertValid(source, trace)
            assertEquals(french.phonemize(source), trace.phonemes)
            assertEquals(1, trace.words.count { it.word.any(Char::isDigit) })
        }
    }

    @Test fun decomposedAccentsAndCurlyApostrophesKeepOriginalUtf16Offsets() {
        val french = frontend()
        val source = "E\u0301coute l’algorithme, à demain."
        val trace = requireNotNull(french.traceWords(source))
        assertValid(source, trace)
        assertEquals(source.substring(0, 7), trace.words.first().word)
        assertEquals(french.phonemize(source), trace.phonemes)
    }

    @Test fun technicalAnnotationsSymbolsAndNumbersUseTheSameContinuousFrenchPhones() {
        val french = frontend()
        for (source in listOf("La fonction main utilise std::cout.",
            "La directive #include <iostream> prépare le programme.",
            "Le code { return 0; } affiche une valeur.", "La condition x <= 12,05 est vraie.")) {
            val prepared = FrenchTechnicalSpeech.prepareForKokoro(source)
            val trace = requireNotNull(MixedFrenchG2P.traceWordsPrepared(prepared, french)) { source }
            assertValid(prepared, trace)
            assertEquals(MixedFrenchG2P.phonemizePrepared(prepared, french), trace.phonemes)
            ManualEnglishSpeech.spans(prepared).forEach { annotation ->
                val group = trace.words.single { it.sourceStart <= annotation.start && it.sourceEndExclusive >= annotation.endExclusive }
                assertTrue(trace.phonemes.substring(group.start, group.endExclusive)
                    .contains(ManualEnglishPronunciations.phonemes(annotation.entry)))
            }
        }
    }

    @Test fun knownOrdinaryFrenchWordsNeverBecomeForeignTermsDuringTracing() {
        val french = frontend()
        val source = "La main droite reste ouverte."
        val prepared = FrenchTechnicalSpeech.prepareForKokoro(source)
        assertTrue(ManualEnglishSpeech.spans(prepared).isEmpty())
        val trace = requireNotNull(MixedFrenchG2P.traceWordsPrepared(prepared, french))
        assertValid(prepared, trace)
        assertEquals(french.phonemize(source), trace.phonemes)
        assertEquals(listOf("La", "main", "droite", "reste", "ouverte"), trace.words.map { it.word })
    }

    @Test fun unsupportedInputCannotProduceInventedAlignment() {
        val french = FrenchFluidG2P.fromLexicon(mapOf("le" to "lə"))
        assertNull(french.traceWords("Le motinconnu."))
        assertNull(french.traceWords(""))
        assertNull(french.traceWords("\uE101", mapOf('\uE100' to "mˈAn")))
    }
}
