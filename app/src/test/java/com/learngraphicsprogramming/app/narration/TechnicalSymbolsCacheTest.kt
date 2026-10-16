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

    @Test fun verifiedEnglishScopeAndFrenchHashRecordingsRemainReusable() {
        val source = "The #include <iostream> line contains { std::cout } and std:: indicates scope."
        assertEquals("7576c24de1d58906c3e0b5cad2c2be3c90247749302cb9ccb4faa19a660bad46",
            KokoroNarrationCache.key("en-US", "af_heart", source))
        assertEquals("15dec87eacc498310cbff47a0f581af5935e89b8a14205d823d5909be854f0d3",
            KokoroNarrationCache.key("en-GB", "bf_emma", source))
        val hashOnly = "La ligne #include prépare le programme."
        assertTrue(FrenchTechnicalSpeech.changedOperatorReadings(hashOnly).isEmpty())
        assertEquals(digest(listOf("kokoro-wav-v1", KokoroModelManifest.revision, "fr", "ff_siwis", hashOnly,
            FrenchTechnicalSpeech.SYMBOL_CACHE_REVISION)), KokoroNarrationCache.key("fr", "ff_siwis", hashOnly))
    }

    @Test fun newFrenchOperatorNamesChangeOnlyTheirOwnAudioAndNeuralBookmark() {
        for (symbol in listOf("::", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>=", ".*", "->*", "++", "--", "!")) {
            val text = if (symbol == "++" || symbol == "--" || symbol == "!") "${symbol}x" else "gauche $symbol droite"
            val previousParts = mutableListOf("kokoro-wav-v1", KokoroModelManifest.revision, "fr", "ff_siwis", text)
            if (symbol == "::") previousParts += FrenchTechnicalSpeech.SYMBOL_CACHE_REVISION
            assertNotEquals(text, digest(previousParts), KokoroNarrationCache.key("fr", "ff_siwis", text))
            assertNotEquals(text, legacyResume("fr", "ff_siwis", NarrationEngine.KOKORO, text),
                NarrationResumePolicy.key("page", "fr", NarrationEngine.KOKORO.name, "ff_siwis", listOf(text)))
            assertEquals(text, legacyResume("fr", "ff_siwis", NarrationEngine.ANDROID, text),
                NarrationResumePolicy.key("page", "fr", NarrationEngine.ANDROID.name, "ff_siwis", listOf(text)))
        }
        for (text in listOf("C++20", "Bonjour !", "\"x += y\"", "\"++x\"", "Le pointeur * garde sa forme.", "Une phrase sans code.")) {
            assertEquals(text, legacyAudio("fr", "ff_siwis", text), KokoroNarrationCache.key("fr", "ff_siwis", text))
        }
    }

    @Test fun spanishScopeRevisesItsAudioWithoutInvalidatingOtherTechnicalRecordings() {
        val source = "std:: indica el espacio de nombres."
        val now = SpanishTechnicalSpeech.prepare(source).text
        assertTrue(now.startsWith("ese te de operador de resolución de ámbito"))
        val before = now.replace("operador de resolución de ámbito", "doble dos puntos")
        assertNotEquals(digest(listOf("kokoro-wav-v1", KokoroModelManifest.revision, "es", "ef_dora", source,
            "spanish-technical-speech-v1", before)), KokoroNarrationCache.key("es", "ef_dora", source))
        val unchanged = "#include <iostream>"
        assertEquals(digest(listOf("kokoro-wav-v1", KokoroModelManifest.revision, "es", "ef_dora", unchanged,
            "spanish-technical-speech-v1", "almohadilla include abre ángulo iostream cierra ángulo")),
            KokoroNarrationCache.key("es", "ef_dora", unchanged))
    }

    private fun digest(parts: List<String>) = MessageDigest.getInstance("SHA-256")
        .digest(parts.joinToString("\u0000").toByteArray(Charsets.UTF_8))
        .joinToString("") { "%02x".format(it.toInt() and 255) }

    @Test fun semicolonMentionsReviseOnlyAudioThatGainsANewSpokenName() {
        for ((language, voice, source) in listOf(
            Triple("fr", "ff_siwis", "N'ajoute pas ; à toutes les lignes."),
            Triple("en-US", "af_heart", "Do not add ; to every line."),
            Triple("en-GB", "bf_emma", "Do not add ; to every line."),
            Triple("es", "ef_dora", "No añadas ; a cualquier línea.")
        )) {
            assertNotEquals(legacyAudio(language, voice, source), KokoroNarrationCache.key(language, voice, source))
            val ordinary = "Primero; después."
            assertEquals(legacyAudio(language, voice, ordinary), KokoroNarrationCache.key(language, voice, ordinary))
        }
    }

    @Test fun frenchReferenceSpellingDoesNotInvalidateLogicalAndRecordings() {
        val logical = "La condition a && b reste vraie."
        assertEquals(legacyAudio("fr", "ff_siwis", logical), KokoroNarrationCache.key("fr", "ff_siwis", logical))
        val reference = "int&& valeur"
        assertNotEquals(legacyAudio("fr", "ff_siwis", reference), KokoroNarrationCache.key("fr", "ff_siwis", reference))
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
