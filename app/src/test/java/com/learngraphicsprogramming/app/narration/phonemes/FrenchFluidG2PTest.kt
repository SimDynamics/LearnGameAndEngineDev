package com.learngraphicsprogramming.app.narration.phonemes

import org.junit.Assert.assertEquals
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
}
