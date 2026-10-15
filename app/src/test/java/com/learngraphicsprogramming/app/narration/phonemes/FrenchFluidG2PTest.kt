package com.learngraphicsprogramming.app.narration.phonemes

import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

class FrenchFluidG2PTest {
    // Pronunciations/expected strings from FluidAudio's upstream FrenchG2PTests.
    private val entries = mapOf(
        "arrivés" to "aʁivˈe", "au" to "o", "autres" to "ˈotʁ", "aux" to "o", "comme" to "kˈɔm",
        "continents" to "kɔ̃tinˈɑ̃", "de" to "də", "des" to "de", "elle" to "ˈɛl", "est" to "ˈɛ", "et" to "ˈe",
        "fine" to "fˈin", "hommage" to "ɔmˈaʒ", "héros" to "eʁˈo", "itinéraire" to "itineʁˈɛʁ", "les" to "lˈe",
        "luna" to "lynˈa", "lutteurs" to "lytˈœʁ", "mers" to "mˈɛʁ", "niveau" to "nivˈo", "ont" to "ˈɔ̃",
        "pensez" to "pɑ̃sˈe", "plus" to "plˈy", "randonnée" to "ʁɑ̃dɔnˈe", "rendu" to "ʁɑ̃dˈy",
        "similaire" to "similˈɛʁ", "ski" to "skˈi", "sont" to "sˈɔ̃", "sous" to "sˈu", "un" to "ˈœ̃", "unis" to "ynˈi",
        "à" to "ˈa", "également" to "eɡalmˈɑ̃", "épaisse" to "epˈɛs", "états" to "etˈa", "ans" to "ˈɑ̃", "sept" to "sˈɛt",
        "demain" to "dəmˈɛ̃", "île" to "ˈil", "ami" to "amˈi", "bonjour" to "bɔ̃ʒˈuʁ", "dit" to "dˈi", "il" to "ˈil",
        "mot" to "mˈo", "le" to "lˈə", "là" to "lˈa", "va" to "vˈa", "partir" to "paʁtˈiʁ", "fais" to "fˈɛ",
        "que" to "kˈə", "vrai" to "vʁˈɛ", "dis" to "dˈi", "moi" to "mwˈa", "quelqu'un" to "kɛlkˈœ̃", "venu" to "vənˈy"
    )
    private fun frontend() = FrenchFluidG2P.fromLexicon(entries, setOf("héros"))

    @Test fun preservesUpstreamSentenceStressLiaisonAndNasalVowels() {
        assertEquals("lez otʁ lytˈœʁz ˈɔ̃t eɡalmˈɑ̃ ʁɑ̃dˈy ɔmˈaʒ a lynˈa.", frontend().phonemize("Les autres lutteurs ont également rendu hommage à Luna."))
        assertEquals("pɑ̃sˈez a litineʁˈɛʁ də skˈi kɔm a œ̃n itineʁˈɛʁ də ʁɑ̃dɔnˈe similˈɛʁ.", frontend().phonemize("Pensez à l'itinéraire de ski comme à un itinéraire de randonnée similaire."))
    }
    @Test fun aspiratedHBlocksLiaisonAndCompoundsKeepStress() {
        val result = frontend().phonemize("Les héros sont arrivés aux États-Unis.")
        assertTrue(result.startsWith("le eʁˈo "))
        assertTrue(result.endsWith("oz etˈazynˈi."))
    }
    @Test fun nasalLiaisonElisionAndSilentFinalConsonants() {
        assertEquals("dœ̃n amˈi", frontend().phonemize("d'un ami"))
        assertEquals("sˈiz ˈɑ̃", frontend().phonemize("six ans"))
        assertEquals("sˈɛt ˈɑ̃", frontend().phonemize("sept ans"))
        assertEquals("kɛlkˈœ̃ ɛ vənˈy.", frontend().phonemize("Quelqu'un est venu."))
    }
    @Test fun accentedCapitalAndNfcInputAreWords() {
        assertEquals("a dəmˈɛ̃.", frontend().phonemize("À demain."))
        assertEquals(frontend().phonemize("Épaisse."), frontend().phonemize("E\u0301paisse."))
    }
    @Test fun hyphenatedCliticsAndQuotationSpacingMatchUpstream() {
        assertEquals("vˈatil paʁtˈiʁ ?", frontend().phonemize("Va-t-il partir ?"))
        assertEquals("ɛs vʁˈɛ ?", frontend().phonemize("Est-ce vrai ?"))
        assertEquals("dˈimwˌa.", frontend().phonemize("Dis-moi."))
        assertEquals("il dˈi “ bɔ̃ʒˈuʁ ” !", frontend().phonemize("il dit « bonjour » !"))
    }
    @Test fun rawCharsiuIpaIsAdaptedToKokoroConventions() {
        assertEquals("pʁoʒɛ", FrenchPhonology.mapToKokoro("pʁɔʒɛ", "projet"))
        assertEquals("lyi", FrenchPhonology.mapToKokoro("lɥi", "lui"))
        assertEquals("mɔ̃tanj", FrenchPhonology.mapToKokoro("mɔ̃taɲ", "montagnes"))
        assertEquals("dəvny", FrenchPhonology.mapToKokoro("dəvəny", "devenu"))
        assertEquals("pʁˈɛskə", FrenchPhonology.addStress("pʁɛskə", FrenchPhonology.Stress.PRIMARY))
    }
    @Test(expected = FrenchG2PException::class) fun unknownWordCannotBeSilentlyDropped() {
        frontend().phonemize("mot xyzzinconnu mot")
    }
    @Test fun selectedFallbackIsCachedPerWord() {
        var calls = 0
        val g2p = FrenchFluidG2P.fromLexicon(entries, fallback = FrenchWordG2P { calls++; "pʁɔʒɛ" })
        assertEquals("pʁoʒˈɛ pʁoʒˈɛ", g2p.phonemize("projet projet"))
        assertEquals(1, calls)
    }
    @Test(expected = FrenchG2PException::class) fun incompatiblePhonesAreRejected() {
        FrenchFluidG2P.fromLexicon(entries, vocabulary = setOf('a')).phonemize("bonjour")
    }
    @Test fun frenchNumbersAreSpokenWithDecimalsAndPercentages() {
        assertEquals("mille et un", FrenchNumberNormalizer.normalize("1001"))
        assertEquals("soixante et onze", FrenchNumberNormalizer.normalize("71"))
        assertEquals("quatre-vingts", FrenchNumberNormalizer.normalize("80"))
        assertEquals("douze virgule zéro cinq pour cent", FrenchNumberNormalizer.normalize("12,05 %"))
        assertEquals("deux mille cent", FrenchNumberNormalizer.normalize("2\u202F100"))
    }

    @Test fun emptyForeignSelectionKeepsOrdinaryFrenchOutputExactly() {
        val g2p = frontend()
        val sentences = listOf(
            "Les autres lutteurs ont également rendu hommage à Luna.",
            "Pensez à l'itinéraire de ski comme à un itinéraire de randonnée similaire.",
            "Les héros sont arrivés aux États-Unis.", "d'un ami", "six ans", "sept ans",
            "Quelqu'un est venu.", "À demain.", "E\u0301paisse.", "Va-t-il partir ?",
            "Est-ce vrai ?", "Dis-moi.", "il dit « bonjour » !")
        sentences.forEach { sentence ->
            assertEquals(sentence, g2p.phonemize(sentence), g2p.phonemizeWithForeignTokens(sentence, emptyMap()))
        }
    }

    @Test fun opaqueEnglishKeepsWholeSentenceFrenchStressAndLiaisons() {
        val sentence = "Les autres \uE100 ont également rendu hommage à Luna."
        assertEquals("lez otʁ ˈɛls ˈɔ̃t eɡalmˈɑ̃ ʁɑ̃dˈy ɔmˈaʒ a lynˈa.",
            frontend().phonemizeWithForeignTokens(sentence, mapOf('\uE100' to "ˈɛls")))
        // Liaison still applies between French words elsewhere in this same utterance.
        assertEquals("le ˈɛls lez ˈotʁ", frontend().phonemizeWithForeignTokens(
            "Les \uE100 les autres", mapOf('\uE100' to "ˈɛls")))
    }

    @Test fun foreignPhonesAreNeverRestressedRewrittenOrSentToTheFrenchFallback() {
        var calls = 0
        val g2p = FrenchFluidG2P.fromLexicon(entries, fallback = FrenchWordG2P {
            calls++
            throw AssertionError("An opaque English token cannot invoke Charsiu")
        })
        val first = "sˈi ˈWt"
        val second = "ˌOvəɹɹˈId"
        assertEquals("lə $first ɛ $second.", g2p.phonemizeWithForeignTokens(
            "Le \uE100 est \uE101.", mapOf('\uE100' to first, '\uE101' to second)))
        assertEquals(0, calls)
    }

    @Test fun foreignBoundariesBlockLiaisonWithoutDisablingFrenchElisionOrPunctuation() {
        val foreign = mapOf('\uE100' to "ˈɛls")
        assertEquals("le ˈɛls.", frontend().phonemizeWithForeignTokens("Les \uE100.", foreign))
        assertEquals("d ˈɛls amˈi.", frontend().phonemizeWithForeignTokens("d'\uE100 ami.", foreign))
        assertEquals("il dˈi “ ˈɛls ” !", frontend().phonemizeWithForeignTokens("il dit « \uE100 » !", foreign))
    }

    @Test fun foreignInsertionDoesNotBypassStrictKokoroVocabularyValidation() {
        val g2p = FrenchFluidG2P.fromLexicon(entries, vocabulary = "le .mˈAn".toSet())
        assertEquals("le mˈAn.", g2p.phonemizeWithForeignTokens("Les \uE100.", mapOf('\uE100' to "mˈAn")))
        assertThrows(FrenchG2PException::class.java) {
            g2p.phonemizeWithForeignTokens("Les \uE100.", mapOf('\uE100' to "m@An"))
        }
        assertThrows(FrenchG2PException::class.java) { g2p.phonemize("Les @.") }
    }

    @Test fun foreignMarkersRequireExplicitBoundedPhonesAndCannotInjectMoreMetadata() {
        val g2p = frontend()
        for ((marker, phones) in listOf('a' to "mˈAn", '\uE100' to "", '\uE100' to " ",
            '\uE100' to "m\nAn", '\uE100' to "m$", '\uE100' to "\uE101", '\uE100' to "a".repeat(4097))) {
            assertThrows(FrenchG2PException::class.java) {
                g2p.phonemizeWithForeignTokens("\uE100", mapOf(marker to phones))
            }
        }
        assertThrows(FrenchG2PException::class.java) {
            g2p.phonemizeWithForeignTokens("\uE101", mapOf('\uE100' to "mˈAn"))
        }
    }

    @Test fun aForeignTokenDoesNotTurnUnknownFrenchWordsIntoAnEnglishFallback() {
        assertThrows(FrenchG2PException::class.java) {
            frontend().phonemizeWithForeignTokens("mot \uE100 xyzzinconnu mot", mapOf('\uE100' to "mˈAn"))
        }
    }
}
