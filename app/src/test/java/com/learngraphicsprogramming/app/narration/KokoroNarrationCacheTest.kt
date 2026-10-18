package com.learngraphicsprogramming.app.narration

import java.security.MessageDigest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class KokoroNarrationCacheTest {
    @Test fun ordinarySpanishKeepsTheExactPreviouslyPublishedHash() {
        val text = "La programación permite resolver problemas."
        val expected = "c2eff306eff398b3f2bb490015f375eb9cb6bef898010cd715ffbd5fd974bbc0"
        assertEquals(expected, legacyKey("es", "ef_dora", text))
        assertEquals(expected, KokoroNarrationCache.key("es", "ef_dora", text))
    }

    @Test fun sameModelVoiceLanguageAndSentenceAlwaysYieldSameFileName() {
        val first = KokoroNarrationCache.key(
            "es", "ef_dora", "La programación permite resolver problemas."
        )
        val second = KokoroNarrationCache.key(
            "es", "ef_dora", "La programación permite resolver problemas."
        )
        assertEquals(first, second)
        assertTrue(first.matches(Regex("[a-f0-9]{64}")))
    }

    @Test fun ordinarySpacedSemicolonUsesItsUnchangedPunctuationAudioRatherThanTheWrongSpokenSign() {
        val text = "Primero ; después sigue."
        val wrongReading = "Primero punto y coma después sigue."
        val wrongKey = digest(listOf("kokoro-wav-v1", PINNED_MODEL_REVISION, "es", "ef_dora", text,
            "spanish-technical-speech-v1", wrongReading))
        assertEquals(legacyKey("es", "ef_dora", text), KokoroNarrationCache.key("es", "ef_dora", text))
        assertNotEquals(wrongKey, KokoroNarrationCache.key("es", "ef_dora", text))
        val technical = "No añadas ; a cualquier línea."
        assertEquals(digest(listOf("kokoro-wav-v1", PINNED_MODEL_REVISION, "es", "ef_dora", technical,
            "spanish-technical-speech-v1", "No añadas punto y coma a cualquier línea.")),
            KokoroNarrationCache.key("es", "ef_dora", technical))
    }

    @Test fun cacheCannotMixTextLanguageOrVoices() {
        val spanish = KokoroNarrationCache.key("es", "ef_dora", "Hola.")
        assertNotEquals(spanish, KokoroNarrationCache.key("es", "ef_dora", "Hola mundo."))
        assertNotEquals(spanish, KokoroNarrationCache.key("en-US", "ef_dora", "Hola."))
        assertNotEquals(spanish, KokoroNarrationCache.key("es", "ef_alex", "Hola."))
    }

    @Test fun alreadySupportedSpanishCodeNamesNumbersAndMathKeepLegacyKeys() {
        for (text in listOf(
            "Un primer vistazo a C++",
            "Ejemplo de código C más más.",
            "Doce casas y 0,05 litros.",
            "Una temperatura de -3 °C describe un estado.",
            "Calcula 3/4 y x².",
            "a^2 + b^3 = c^2",
            "Para exponentes positivos, aᵐ × aⁿ = aᵐ⁺ⁿ.",
            "Completa 3ᵉ.",
            "La segunda, porque un medio > un tercio."
        )) {
            assertEquals(text, legacyKey("es", "ef_dora", text),
                KokoroNarrationCache.key("es", "ef_dora", text))
        }
    }

    @Test fun correctedEnglishAndFrenchTechnicalSymbolsCannotReuseAnOmittingRecording() {
        val text = "std::vector<int> y total_count2."
        for ((language, voice) in listOf("en-US" to "af_heart", "en-GB" to "bf_emma", "fr" to "ff_siwis")) {
            assertNotEquals(language, legacyKey(language, voice, text),
                KokoroNarrationCache.key(language, voice, text))
        }
    }

    @Test fun correctedRawTechnicalReadingCannotReuseItsLegacyRecording() {
        val text = "std::vector<int>"
        val prepared = SpanishTechnicalSpeech.prepare(text)
        assertTrue(prepared.changed)
        val legacy = legacyKey("es", "ef_dora", text)
        val corrected = KokoroNarrationCache.key("es", "ef_dora", text)
        val expected = digest(listOf("kokoro-wav-v1", PINNED_MODEL_REVISION, "es", "ef_dora", text,
            "spanish-technical-speech-v1", prepared.text))
        assertNotEquals(legacy, corrected)
        assertEquals(expected, corrected)
        assertEquals(corrected, KokoroNarrationCache.key("es", "ef_dora", text))
    }

    @Test fun newlySpokenSuperscriptLettersCannotReuseAudioWithLostExponents() {
        for (text in listOf("aᵃ", "xᵇ", "equisᶜ", "La base ya hablada es equisᵃ.")) {
            val prepared = SpanishTechnicalSpeech.prepare(text)
            assertFalse("The exponent correction belongs to G2P, not technical code", prepared.changed)
            val corrected = KokoroNarrationCache.key("es", "ef_dora", text)
            assertNotEquals(text, legacyKey("es", "ef_dora", text), corrected)
            assertEquals(text, digest(listOf("kokoro-wav-v1", PINNED_MODEL_REVISION, "es", "ef_dora", text,
                "spanish-technical-speech-v1", prepared.text)), corrected)
        }
    }

    @Test fun superscriptCorrectionsDoNotChangeOtherLanguageKeys() {
        val text = "aᵃ bᵇ cᶜ"
        for ((language, voice) in listOf("en-US" to "af_heart", "en-GB" to "bf_emma", "fr" to "ff_siwis")) {
            assertEquals(language, legacyKey(language, voice, text),
                KokoroNarrationCache.key(language, voice, text))
        }
    }

    private fun legacyKey(language: String, voice: String, text: String): String =
        digest(listOf("kokoro-wav-v1", PINNED_MODEL_REVISION, language, voice, text))

    private fun digest(parts: List<String>): String = MessageDigest.getInstance("SHA-256")
        .digest(parts.joinToString("\u0000").toByteArray(Charsets.UTF_8))
        .joinToString("") { "%02x".format(it.toInt() and 0xff) }

    private companion object {
        const val PINNED_MODEL_REVISION = "1939ad2a8e416c0acfeecc08a694d14ef25f2231"
    }
}
