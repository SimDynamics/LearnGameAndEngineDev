package com.learngraphicsprogramming.app.narration

import java.security.MessageDigest
import org.junit.Assert.*
import org.junit.Test

class TechnicalSymbolsCacheTest {
    @Test fun newSymbolReadingsInvalidateOnlyAffectedEnglishAndFrenchRecordings() {
        for ((language, voice) in listOf("en-US" to "af_heart", "en-GB" to "bf_emma", "fr" to "ff_siwis")) {
            for (text in listOf("#include", "std:: indicates", "::")) {
                assertNotEquals(legacyAudio(language, voice, text), KokoroNarrationCache.key(language, voice, text))
            }
            for (text in listOf("An ordinary sentence.", "La main reste calme.", "std standard stdexcept")) {
                assertEquals(legacyAudio(language, voice, text), KokoroNarrationCache.key(language, voice, text))
            }
        }
    }

    @Test fun onlyNeuralBookmarksWithChangedAudioReceiveADifferentTimingIdentity() {
        for ((language, voice) in listOf("en-US" to "af_heart", "en-GB" to "bf_emma", "fr" to "ff_siwis")) {
            for (engine in NarrationEngine.entries) {
                val technical = "#include; std:: indicates scope."
                val key = NarrationResumePolicy.key("page", language, engine.name, voice, listOf(technical))
                if (engine == NarrationEngine.ANDROID) assertEquals(legacyResume(language, voice, engine, technical), key)
                else assertNotEquals(legacyResume(language, voice, engine, technical), key)
                val ordinary = "Une phrase normale."
                assertEquals(legacyResume(language, voice, engine, ordinary),
                    NarrationResumePolicy.key("page", language, engine.name, voice, listOf(ordinary)))
            }
        }
    }

    @Test fun frenchManualTermsAndSpanishRecordingsKeepTheirVerifiedIdentities() {
        assertEquals("ab290b5d9f4bc312dfeef7a282e0ea5c929ba723f184dc5b3a9452e0e9a7b4f3",
            KokoroNarrationCache.key("fr", "ff_siwis", FrenchTechnicalSpeech.prepareForKokoro(
                "La directive include permet d'inclure la bibliothèque iostream.")))
        val source = "Una biblioteca reúne recursos ya preparados para tareas comunes."
        assertEquals(legacyAudio("es", "ef_dora", source), KokoroNarrationCache.key("es", "ef_dora", source))
        for (engine in NarrationEngine.entries) assertEquals(legacyResume("es", "ef_dora", engine, source),
            NarrationResumePolicy.key("page", "es", engine.name, "ef_dora", listOf(source)))
    }

    private fun legacyAudio(language: String, voice: String, text: String) = MessageDigest.getInstance("SHA-256")
        .digest(listOf("kokoro-wav-v1", KokoroModelManifest.revision, language, voice, text)
            .joinToString("\u0000").toByteArray()).joinToString("") { "%02x".format(it.toInt() and 255) }

    private fun legacyResume(language: String, voice: String, engine: NarrationEngine, source: String): String {
        val digest = MessageDigest.getInstance("SHA-256")
        for (text in listOf("resume-v1", "page", language, engine.name, voice, source)) {
            val bytes = text.toByteArray(Charsets.UTF_8)
            digest.update(bytes.size.toString().toByteArray(Charsets.US_ASCII))
            digest.update(':'.code.toByte())
            digest.update(bytes)
        }
        return digest.digest().joinToString("") { "%02x".format(it.toInt() and 255) }
    }
}
