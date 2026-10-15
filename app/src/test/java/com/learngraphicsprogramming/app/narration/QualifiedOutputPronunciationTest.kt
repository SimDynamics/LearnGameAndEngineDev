package com.learngraphicsprogramming.app.narration

import java.security.MessageDigest
import org.junit.Assert.*
import org.junit.Test

class QualifiedOutputPronunciationTest {
    @Test fun exactQualifiedOutputPreservesBothOriginalComponentOffsets() {
        val text = "Use std::cout; then `std :: cout`, std::cout<<2 and std::cout."
        val matches = EnglishTechnicalSpeech.qualifiedOutputMatches(text)
        assertEquals(4, matches.size)
        matches.forEach { match ->
            assertEquals("std", text.substring(match.stdStart, match.stdEndExclusive))
            assertEquals("cout", text.substring(match.coutStart, match.coutEndExclusive))
            assertEquals(match.start, match.stdStart)
            assertEquals(match.endExclusive, match.coutEndExclusive)
        }
        assertEquals("ˈɛs tˈi dˈi", EnglishTechnicalSpeech.STD_PHONES)
        assertEquals(ManualEnglishPronunciations.phones.getValue("cout"), EnglishTechnicalSpeech.coutPhones)
    }

    @Test fun englishCorrectionDoesNotMatchOrdinaryWordsLongerIdentifiersOtherNamespacesOrUrls() {
        for (source in listOf("standard stock out cout", "mystd::cout", "std::cout_total", "std::cout2",
            "std::couté", "std\u0301::cout", "std::cout\u0301", "std::counter", "std::cin", "other::cout",
            "STD::cout", "std::Cout", "https://example.org/std::cout", "www.example.org/std::cout")) {
            assertTrue(source, EnglishTechnicalSpeech.qualifiedOutputMatches(source).isEmpty())
        }
    }

    @Test fun onlyCorrectedEnglishSentencesGetNewAudioKeysForBothVoices() {
        for ((language, voice) in listOf("en-US" to "af_heart", "en-GB" to "bf_emma")) {
            val changed = "Use std::cout to display the result."
            assertNotEquals(legacyAudio(language, voice, changed), KokoroNarrationCache.key(language, voice, changed))
            assertEquals(KokoroNarrationCache.key(language, voice, changed), KokoroNarrationCache.key(language, voice, changed))
            for (source in listOf("Use a standard output stream.", "std::cin and std::vector<int>.",
                "mystd::cout, std::cout_total", "https://example.org/std::cout")) {
                assertEquals(source, legacyAudio(language, voice, source), KokoroNarrationCache.key(language, voice, source))
            }
        }
    }

    @Test fun englishCorrectedAudioChangesOnlyItsKokoroResumeIdentity() {
        for ((language, voice) in listOf("en-US" to "af_heart", "en-GB" to "bf_emma")) {
            val source = "Use std::cout to display the result."
            assertNotEquals(legacyResume(language, voice, NarrationEngine.KOKORO, source),
                NarrationResumePolicy.key("page", language, NarrationEngine.KOKORO.name, voice, listOf(source)))
            assertEquals(legacyResume(language, voice, NarrationEngine.ANDROID, source),
                NarrationResumePolicy.key("page", language, NarrationEngine.ANDROID.name, voice, listOf(source)))
            val ordinary = "The standard output is correct."
            for (engine in NarrationEngine.entries) {
                assertEquals(legacyResume(language, voice, engine, ordinary),
                    NarrationResumePolicy.key("page", language, engine.name, voice, listOf(ordinary)))
            }
        }
    }

    @Test fun frenchQualifiedOutputUsesFrenchLettersAndKeepsSharedCoutPhones() {
        val text = FrenchTechnicalSpeech.prepareForKokoro("La fonction utilise std::cout.")
        val cout = ManualEnglishSpeech.spans(text).single { it.entry.id == "cout" }
        assertEquals("ˈɛs tˈe dˈe", MixedFrenchG2P.namespacePhonesBefore(text, cout))
        assertEquals(ManualEnglishPronunciations.phonemes(cout.entry), MixedFrenchG2P.englishPhones(cout.entry))
        val other = FrenchTechnicalSpeech.prepareForKokoro("La fonction utilise std::cin.")
        val cin = ManualEnglishSpeech.spans(other).single { it.entry.id == "cin" }
        assertEquals("Unrelated qualified identifiers keep their accepted recording", MixedFrenchG2P.namespaceUsPhones,
            MixedFrenchG2P.namespacePhonesBefore(other, cin))
        // The old namespace contributed these phones to its existing key; the new
        // key must change even though the visible/stripped text is identical.
        val previousParts = mutableListOf("kokoro-wav-v1", KokoroModelManifest.revision, "fr", "ff_siwis",
            ManualEnglishSpeech.strip(text), "manual-english-same-voice-v1",
            ManualEnglishSpeech.strip(text.substring(0, cout.start)).length.toString(), cout.entry.id,
            MixedFrenchG2P.englishPhones(cout.entry), "french-us-namespace-v1", MixedFrenchG2P.namespaceUsPhones)
        assertNotEquals(digest(previousParts), KokoroNarrationCache.key("fr", "ff_siwis", text))
    }

    @Test fun observedFrenchPreparationIsTheActualOutputAndRetainsEachSourceOccurrence() {
        val source = "La fonction main utilise std::cout; puis la fonction main reste visible."
        val pieces = mutableListOf<Triple<Int, Int, String>>()
        val prepared = FrenchTechnicalSpeech.prepareForReading(source) { start, end, text -> pieces += Triple(start, end, text) }
        assertEquals(FrenchTechnicalSpeech.prepareForKokoro(source), prepared)
        assertEquals(prepared, pieces.joinToString("") { it.third })
        assertEquals(source, ManualEnglishSpeech.strip(prepared))
        val annotations = pieces.filter { it.third.startsWith(ManualEnglishSpeech.START) }
        assertEquals(listOf("main", "cout", "main"), annotations.map { source.substring(it.first, it.second) })
        assertTrue(annotations.first().first < annotations.last().first)
        for (ordinary in listOf("La main reste calme.", "", "main + main")) {
            val unchanged = mutableListOf<String>()
            assertEquals(FrenchTechnicalSpeech.prepareForKokoro(ordinary, "block"),
                FrenchTechnicalSpeech.prepareForReading(ordinary, "block") { _, _, text -> unchanged += text })
            assertEquals(ordinary, unchanged.joinToString(""))
        }
    }

    private fun legacyAudio(language: String, voice: String, text: String) = digest(listOf(
        "kokoro-wav-v1", KokoroModelManifest.revision, language, voice, text))

    private fun digest(parts: List<String>) = MessageDigest.getInstance("SHA-256")
        .digest(parts.joinToString("\u0000").toByteArray(Charsets.UTF_8))
        .joinToString("") { "%02x".format(it.toInt() and 255) }

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
