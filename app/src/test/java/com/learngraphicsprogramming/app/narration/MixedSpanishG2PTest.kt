package com.learngraphicsprogramming.app.narration

import com.learngraphicsprogramming.app.narration.phonemes.SpanishFluidG2P
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class MixedSpanishG2PTest {
    private val testPhones = mapOf("include" to "ɪnklˈud", "iostream" to "ˈI ˈO stɹˈim",
        "double" to "dˈʌbᵊl", "string" to "stɹˈɪŋ", "vector" to "vˈɛktəɹ", "cout" to "sˈi ˈWt")

    @Test fun onlyManualSpansEnterEnglishWhileSpanishStressIsPreserved() {
        val calls = mutableListOf<String>()
        val raw = "Utilizamos include para incluir la biblioteca iostream. El programa imprime."
        val phones = MixedSpanishG2P.phonemize(raw, emptyMap()) {
            calls += it.spelling; testPhones.getValue(it.spelling)
        }
        assertEquals(listOf("include", "iostream"), calls)
        assertTrue(phones.contains("impɾˈime"))
        assertTrue(phones.contains("ɪnklˈud"))
        assertTrue(phones.contains("ˈI ˈO stɹˈim"))
        assertFalse(phones.contains("impɾˈIme"))
    }

    @Test fun englishIsNeverRespeltOrSubjectedToSpanishAllophones() {
        val phones = MixedSpanishG2P.phonemize("Usa double y string.", emptyMap()) {
            testPhones.getValue(it.spelling)
        }
        assertTrue(phones.contains("dˈʌbᵊl"))
        assertFalse(phones.contains("ðˈʌβᵊl"))
        assertTrue(phones.contains("stɹˈɪŋ"))
        assertEquals(phones, MixedSpanishG2P.plan("Usa double y string.", emptyMap()) {
            testPhones.getValue(it.spelling)
        }.joinToString("") { it.phonemes })
    }

    @Test fun preparedIdentifiersAreNeverReselectedAfterBeingSplitIntoWords() {
        val prepared = SpanishTechnicalSpeech.prepareForKokoro("`mi_string` y `imprime_total` conservan sus nombres.").text
        assertTrue(ManualEnglishSpeech.spans(prepared).isEmpty())
        assertEquals(SpanishFluidG2P.phonemize(ManualEnglishSpeech.strip(prepared)),
            MixedSpanishG2P.phonemizePrepared(prepared, emptyMap()))
    }

    @Test fun oldDurablePlainSentencesKeepTheExistingPronunciationAndHash() {
        val plain = "Utilizamos include para incluir la biblioteca iostream."
        assertEquals(SpanishFluidG2P.phonemize(plain), MixedSpanishG2P.phonemizePrepared(plain, emptyMap()))
        val ordinary = "El programa imprime el resultado."
        val prepared = SpanishTechnicalSpeech.prepareForKokoro(ordinary).text
        assertEquals(KokoroNarrationCache.key("es", "ef_dora", ordinary),
            KokoroNarrationCache.key("es", "ef_dora", prepared))
    }

    @Test fun paragraphSentenceAndAndroidSplittingKeepCorrectMetadataOwnership() {
        val raw = "Usa include. `mi_string` conserva su nombre. Utiliza std::vector."
        val lesson = JSONObject().put("pages", JSONArray().put(JSONObject().put("blocks", JSONArray()
            .put(JSONObject().put("type", "paragraph").put("text", raw)))))
        val original = LessonNarrationText.page(lesson, 0)
        val annotated = LessonNarrationText.page(lesson, 0, forKokoro = true)
        assertEquals(original, annotated.map(ManualEnglishSpeech::strip))
        val sentences = NarrationSegments.sentences(annotated)
        assertTrue(sentences.all { it.startsWith(ManualEnglishSpeech.PREPARED) })
        val protectedIdentifier = sentences.single { ManualEnglishSpeech.strip(it).contains("mi guion bajo string") }
        assertTrue(ManualEnglishSpeech.spans(protectedIdentifier).isEmpty())
        assertEquals(NarrationSegments.prepare(original, 250), NarrationSegments.prepare(annotated, 250))
    }

    @Test fun foreignPunctuationBelongsToTheSameUtteranceWithoutEmptyInferenceSpans() {
        val plan = MixedSpanishG2P.plan("Usa include e iostream.", emptyMap()) { testPhones.getValue(it.spelling) }
        assertTrue(plan.all { it.phonemes.isNotBlank() })
        assertTrue(plan.last().english)
        assertTrue(plan.last().phonemes.endsWith('.'))
    }

    @Test fun typographicApostropheAlsoWorksBesideManualEnglishWithoutChangingContent() {
        val raw = "Kiselev’s geometry utiliza std::vector."
        val lesson = JSONObject().put("pages", JSONArray().put(JSONObject().put("blocks", JSONArray()
            .put(JSONObject().put("type", "paragraph").put("text", raw)))))
        val authored = lesson.toString()
        val prepared = LessonNarrationText.page(lesson, 0, forKokoro = true).single()
        // Keep the same selected English spans while comparing only apostrophe normalization.
        val ascii = prepared.replace('’', '\'')
        assertTrue(ManualEnglishSpeech.spans(prepared).any { it.entry.spelling == "vector" })
        assertTrue(ManualEnglishSpeech.strip(prepared).contains("Kiselev’s geometry"))
        assertEquals(MixedSpanishG2P.phonemizePrepared(ascii, emptyMap()),
            MixedSpanishG2P.phonemizePrepared(prepared, emptyMap()))
        assertEquals(authored, lesson.toString())
    }

    @Test fun mixedKeysAreSelectiveAndKeepEveryLegacyPlainKeyAccessible() {
        val raw = "Utilizamos include para incluir la biblioteca iostream."
        val prepared = SpanishTechnicalSpeech.prepareForKokoro(raw).text
        val old = KokoroNarrationCache.key("es", "ef_dora", raw)
        val fresh = KokoroNarrationCache.key("es", "ef_dora", prepared)
        assertNotEquals(old, fresh)
        assertEquals(fresh, KokoroNarrationCache.key("es", "ef_dora", prepared))
        assertNotEquals(fresh, KokoroNarrationCache.key("es", "em_alex", prepared))
        assertEquals(old, KokoroNarrationCache.key("es", "ef_dora", ManualEnglishSpeech.strip(prepared)))
        val ordinary = "Un vector representa una dirección e imprime el resultado."
        assertEquals(KokoroNarrationCache.key("es", "ef_dora", ordinary), KokoroNarrationCache.key("es", "ef_dora",
            SpanishTechnicalSpeech.prepareForKokoro(ordinary).text))
    }

    @Test fun unknownSymbolsStillFailAndExistingPrivateCharactersNeverBecomeEnglish() {
        for (source in listOf("Usa include y @.", "Usa include y \uE100.")) {
            assertThrows(IllegalArgumentException::class.java) {
                MixedSpanishG2P.phonemize(source, emptyMap()) { testPhones.getValue(it.spelling) }
            }
        }
    }
}
