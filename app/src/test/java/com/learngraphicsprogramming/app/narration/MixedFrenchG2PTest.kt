package com.learngraphicsprogramming.app.narration

import com.learngraphicsprogramming.app.narration.phonemes.FrenchFluidG2P
import com.learngraphicsprogramming.app.narration.phonemes.FrenchG2PException
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class MixedFrenchG2PTest {
    private fun frontend(): FrenchFluidG2P {
        val entries = mapOf(
        "la" to "la", "le" to "lə", "fonction" to "fɔ̃ksjˈɔ̃", "utilise" to "ytilˈiz",
        "standard" to "stɑ̃dˈaʁ", "main" to "mˈɛ̃", "droite" to "dʁwˈat", "reste" to "ʁˈɛst",
        "ouverte" to "uvˈɛʁt", "public" to "pyblˈik", "continue" to "kɔ̃tinˈy", "lecture" to "lɛktˈyʁ",
        "une" to "yn", "double" to "dˈubl", "un" to "œ̃", "long" to "lˈɔ̃", "moment" to "mɔmˈɑ̃",
        "dièse" to "djˈɛz", "chevron" to "ʃəvʁˈɔ̃", "ouvrant" to "uvʁˈɑ̃", "fermant" to "fɛʁmˈɑ̃",
        "deux" to "dˈø", "points" to "pwˈɛ̃", "opérateur" to "ɔpeʁatˈœʁ",
        "de" to "də", "résolution" to "ʁezolysjˈɔ̃", "portée" to "pɔʁtˈe"
        )
        val vocabulary = (entries.values.joinToString("") +
            ManualEnglishPronunciations.phones.values.joinToString("") + MixedFrenchG2P.namespaceFrenchLetters + " .,;").toSet()
        return FrenchFluidG2P.fromLexicon(entries, vocabulary = vocabulary)
    }

    @Test fun oneFrenchUtteranceUsesTheExactSharedAmericanPhones() {
        val text = FrenchTechnicalSpeech.prepareForKokoro("La fonction main utilise std::cout.")
        val phones = MixedFrenchG2P.phonemizePrepared(text, frontend())
        val terms = ManualEnglishSpeech.spans(text)
        assertEquals(listOf("main", "cout"), terms.map { it.entry.id })
        terms.forEach { assertTrue(phones.contains(ManualEnglishPronunciations.phonemes(it.entry))) }
        assertTrue(phones.startsWith("la fɔ̃ksjˈɔ̃ "))
        assertTrue(phones.contains(MixedFrenchG2P.namespaceFrenchLetters))
        assertTrue(phones.contains(frontend().phonemize("opérateur de résolution de portée")))
        assertFalse(phones.contains("stɑ̃dˈaʁ"))
        assertFalse(phones.contains(","))
        assertEquals(1, phones.count { it == '.' })
    }

    @Test fun ordinaryFrenchAndOldUnannotatedQueuesRetainTheirExactPhones() {
        for (text in listOf("La main droite reste ouverte.", "Le public continue la lecture.",
            "Une double lecture.", "Un long moment.", "main")) {
            val french = frontend()
            val prepared = FrenchTechnicalSpeech.prepareForKokoro(text)
            assertTrue(ManualEnglishSpeech.spans(prepared).isEmpty())
            assertEquals(french.phonemize(text), MixedFrenchG2P.phonemizePrepared(prepared, french))
        }
        assertEquals(frontend().phonemize("La fonction main."),
            MixedFrenchG2P.phonemizePrepared("La fonction main.", frontend()))
    }

    @Test fun headersAndMarkdownKeepTheirSelectedIdentifiersWithoutNeedingFrenchOovFallback() {
        val text = "`#include <iostream>`"
        val prepared = FrenchTechnicalSpeech.prepareForKokoro(text)
        val phones = MixedFrenchG2P.phonemizePrepared(prepared, frontend())
        assertEquals(text, ManualEnglishSpeech.strip(prepared))
        assertTrue(phones.contains(ManualEnglishPronunciations.phones.getValue("include")))
        assertTrue(phones.contains(ManualEnglishPronunciations.phones.getValue("iostream")))
        assertTrue(phones.contains("djˈɛz"))
        assertFalse(phones.any { it in "`#<>" })
    }

    @Test fun annotationAndSentenceSplittingPreserveOriginalLessonTextAndBoundaries() {
        val source = "La fonction main utilise std::cout. La main droite reste ouverte."
        val lesson = JSONObject().put("pages", JSONArray().put(JSONObject().put("blocks", JSONArray()
            .put(JSONObject().put("type", "paragraph").put("text", source)))))
        val before = lesson.toString()
        val original = LessonNarrationText.page(lesson, 0, NarrationLanguage.FRENCH)
        val prepared = LessonNarrationText.page(lesson, 0, NarrationLanguage.FRENCH, forKokoro = true)
        assertEquals(original, prepared.map(ManualEnglishSpeech::strip))
        val sentences = NarrationSegments.sentences(prepared)
        assertEquals(NarrationSegments.sentences(original), sentences.map(ManualEnglishSpeech::strip))
        assertEquals(2, sentences.size)
        assertEquals(listOf("main", "cout"), ManualEnglishSpeech.spans(sentences.first()).map { it.entry.id })
        assertTrue(ManualEnglishSpeech.spans(sentences.last()).isEmpty())
        assertEquals(before, lesson.toString())
    }

    @Test fun unknownFrenchAndUnsupportedSymbolsStillFailInsteadOfBeingSilentlyDropped() {
        for (source in listOf("La fonction main inconnue.", "La fonction main ☃.")) {
            assertThrows(FrenchG2PException::class.java) {
                MixedFrenchG2P.phonemizePrepared(FrenchTechnicalSpeech.prepareForKokoro(source), frontend())
            }
        }
    }

    @Test fun onlyChangedFrenchAudioGetsANewCacheAndResumeIdentity() {
        val ordinary = "La main droite reste ouverte."
        val technical = "La fonction main utilise std::cout."
        val preparedOrdinary = FrenchTechnicalSpeech.prepareForKokoro(ordinary)
        val preparedTechnical = FrenchTechnicalSpeech.prepareForKokoro(technical)
        assertEquals(KokoroNarrationCache.key("fr", "ff_siwis", ordinary),
            KokoroNarrationCache.key("fr", "ff_siwis", preparedOrdinary))
        assertNotEquals(KokoroNarrationCache.key("fr", "ff_siwis", technical),
            KokoroNarrationCache.key("fr", "ff_siwis", preparedTechnical))
        fun resume(text: String, engine: NarrationEngine) = NarrationResumePolicy.key(
            "same-page", "fr", engine.name, "ff_siwis", listOf(text))
        assertEquals(resume(ordinary, NarrationEngine.KOKORO), resume(preparedOrdinary, NarrationEngine.KOKORO))
        assertNotEquals(resume(technical, NarrationEngine.KOKORO), resume(preparedTechnical, NarrationEngine.KOKORO))
        assertEquals(resume(technical, NarrationEngine.ANDROID), resume(preparedTechnical, NarrationEngine.ANDROID))
    }

    @Test fun improvingTheQualifiedNamespaceKeepsTheAcceptedUnrelatedRecordings() {
        val samples = mapOf(
            "La directive include permet d'inclure la bibliothèque iostream." to
                "ab290b5d9f4bc312dfeef7a282e0ea5c929ba723f184dc5b3a9452e0e9a7b4f3",
            "Le mot-clé return termine la fonction et transmet sa valeur." to
                "9e94d2470b7f5d048acb5acd808df916fed6e2740a1cfe6910551739a175d03d"
        )
        for ((source, recordedKey) in samples) {
            assertEquals(recordedKey, KokoroNarrationCache.key("fr", "ff_siwis",
                FrenchTechnicalSpeech.prepareForKokoro(source)))
        }
        assertEquals("standard", ManualEnglishTerms.entries.single { it.id == "stdexcept" }.englishText.substringBefore(' '))
    }

    @Test fun theTechnicalBoundaryNeverDuplicatesExistingPunctuationOrChangesTheSharedPhones() {
        for ((source, expected) in listOf("std::cout utilise la fonction main." to true,
            "`std::cout` utilise la fonction main." to true, "std::cout." to false,
            "std::cout, la fonction main." to false, "std::cout; la fonction main." to false)) {
            val text = FrenchTechnicalSpeech.prepareForKokoro(source)
            val term = ManualEnglishSpeech.spans(text).single { it.entry.id == "cout" }
            assertEquals(source, expected, MixedFrenchG2P.needsTermBoundary(text, term))
            assertEquals(ManualEnglishPronunciations.phonemes(term.entry), MixedFrenchG2P.englishPhones(term.entry))
            val phones = MixedFrenchG2P.phonemizePrepared(text, frontend())
            assertEquals(source, expected || source.contains(','),
                phones.contains(ManualEnglishPronunciations.phonemes(term.entry) + ","))
        }
    }
}
