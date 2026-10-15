package com.learngraphicsprogramming.app.narration.phonemes

import org.junit.Assert.*
import org.junit.Test

class FrenchRuleBasedG2PTest {
    // These actual curricular forms are absent from the pinned, SHA-verified 250k
    // FluidAudio cache. Citation IPA, not times or spelled-out French substitutes.
    @Test fun pronouncesPreviouslyMissingCurricularForms() {
        val cases = mapOf(
            "suffit" to "syfi", "reçoit" to "ʁəswa", "reçue" to "ʁəsy", "vaut" to "vo", "valent" to "val",
            "correctes" to "kɔʁɛkt", "implémentation" to "ɛ̃plemɑ̃tasjɔ̃", "ultérieure" to "ylteʁjœʁ",
            "nulle" to "nyl", "supérieure" to "sypeʁjœʁ", "absolue" to "absoly", "terminateur" to "tɛʁminatœʁ",
            "factorielle" to "faktoʁjɛl", "cosécante" to "kozekɑ̃t"
        )
        cases.forEach { (word, ipa) -> assertEquals(word, ipa, FrenchRuleBasedG2P.phonemizeWord(word)) }
    }

    @Test fun treatsProductiveInflectionsAsFrenchWords() {
        mapOf(
            "affiche" to "afiʃ", "affiches" to "afiʃ", "affichent" to "afiʃ",
            "programme" to "pʁoɡʁam", "programmes" to "pʁoɡʁam", "programment" to "pʁoɡʁam",
            "reçoivent" to "ʁəswav", "reçues" to "ʁəsy", "suffisent" to "syfiz", "lisent" to "liz",
            "absolues" to "absoly", "correcte" to "kɔʁɛkt", "supérieures" to "sypeʁjœʁ",
            "calculs" to "kalkyl", "lectures" to "lɛktyʁ", "équations" to "ekwasjɔ̃"
        ).forEach { (word, ipa) -> assertEquals(word, ipa, FrenchRuleBasedG2P.phonemizeWord(word)) }
    }

    @Test fun correctsClosedEAndPalatalSuffixesRatherThanCopyingUpstreamErrors() {
        mapOf("merci" to "mɛʁsi", "travail" to "tʁavaj", "soleil" to "solɛj", "lecture" to "lɛktyʁ",
            "recueil" to "ʁəkœj", "fauteuil" to "fotœj", "accueil" to "akœj")
            .forEach { (word, ipa) -> assertEquals(word, ipa, FrenchRuleBasedG2P.phonemizeWord(word)) }
    }

    @Test fun handlesNasalVowelsGlidesAndExplicitHiatus() {
        mapOf("inconnue" to "ɛ̃kɔny", "inconnues" to "ɛ̃kɔny", "recompilation" to "ʁəkɔ̃pilasjɔ̃",
            "naïve" to "naiv", "haïr" to "aiʁ", "ambiguë" to "ɑ̃biɡy", "lui" to "lɥi",
            "variable" to "vaʁjabl", "science" to "sjɑ̃s", "expérience" to "ɛkspeʁjɑ̃s")
            .forEach { (word, ipa) -> assertEquals(word, ipa, FrenchRuleBasedG2P.phonemizeWord(word)) }
    }

    @Test fun handlesTechnicalFrenchWithoutEnglishDetection() {
        mapOf("initialisation" to "inisjalizasjɔ̃", "identificateur" to "idɑ̃tifikatœʁ",
            "récursivité" to "ʁekyʁsivite", "mathématiques" to "matematik", "exception" to "ɛksɛpsjɔ̃",
            "théorème" to "teoʁɛm", "algorithmiquement" to "alɡoʁitmikəmɑ̃")
            .forEach { (word, ipa) -> assertEquals(word, ipa, FrenchRuleBasedG2P.phonemizeWord(word)) }
        assertNotEquals("No language detection: ordinary French remains French", "ɹˈid",
            FrenchRuleBasedG2P.phonemizeWord("read"))
    }

    @Test fun newWordsRemainAudibleAndUseOnlySupportedFrenchIpa() {
        val rawInventory = "abdefijklmnopstuvwyzøœɛɔɑəʁɡʃʒɲɥŋ̃ ".toSet()
        for (word in listOf("zorbule", "réimplémentations", "microdébogage", "zoélumineuse", "pseudovariables",
            "futurificateur", "hhh", "cfg", "xyzzinconnu")) {
            val raw = FrenchRuleBasedG2P.phonemizeWord(word)
            assertNotNull(word, raw)
            assertTrue(word, raw!!.isNotBlank())
            assertTrue("$word: $raw", raw.all { it in rawInventory })
            val mapped = FrenchPhonology.mapToKokoro(raw, word)
            assertTrue("The existing IPA adapter remains the only Kokoro adaptation", 'ɥ' !in mapped)
        }
        assertEquals("se ɛf ʒe", FrenchRuleBasedG2P.phonemizeWord("cfg"))
    }

    @Test fun installedLexiconKeepsPrecedenceAndForeignTokensStayOpaque() {
        var fallbacks = 0
        val frontend = FrenchFluidG2P.fromLexicon(mapOf("merci" to "mɛʁsˈi", "soleil" to "solˈɛj"),
            fallback = FrenchWordG2P { word -> fallbacks++; FrenchRuleBasedG2P.phonemizeWord(word) })
        assertEquals("mɛʁsˈi solˈɛj", frontend.phonemize("merci soleil"))
        assertEquals(0, fallbacks)
        assertEquals("sˈi ˈWt mɛʁsˈi", frontend.phonemizeWithForeignTokens("\uE100 merci", mapOf('\uE100' to "sˈi ˈWt")))
        assertEquals(0, fallbacks)
        assertEquals("syfˈi", frontend.phonemize("suffit"))
        assertEquals(1, fallbacks)
    }

    @Test fun normalizesFrenchCaseAndAccentsButRejectsNonWordInput() {
        assertEquals(FrenchRuleBasedG2P.phonemizeWord("implémentation"),
            FrenchRuleBasedG2P.phonemizeWord("IMPLE\u0301MENTATION"))
        for (text in listOf("", " ", "std::cout", "C++", "mot\n", "a\uE100", "слово", "😀", "a".repeat(257))) {
            assertNull(text, FrenchRuleBasedG2P.phonemizeWord(text))
        }
    }

    @Test fun unfamiliarLatinNameDiacriticsGetAnExplicitFrenchApproximation() {
        assertEquals("kœp", FrenchRuleBasedG2P.phonemizeWord("Köppe"))
        assertEquals("kœp", FrenchRuleBasedG2P.phonemizeWord("Ko\u0308ppe"))
        assertEquals(FrenchRuleBasedG2P.phonemizeWord("Garcia"), FrenchRuleBasedG2P.phonemizeWord("García"))
        assertEquals(FrenchRuleBasedG2P.phonemizeWord("Dvorak"), FrenchRuleBasedG2P.phonemizeWord("Dvořák"))
        assertEquals(FrenchRuleBasedG2P.phonemizeWord("strasse"), FrenchRuleBasedG2P.phonemizeWord("Straße"))
        assertEquals(FrenchRuleBasedG2P.phonemizeWord("Łódź"), FrenchRuleBasedG2P.phonemizeWord("Lodz"))
        assertEquals(FrenchRuleBasedG2P.phonemizeWord("Istanbul"), FrenchRuleBasedG2P.phonemizeWord("İstanbul"))
        // This mapping is deliberately French, not native German/Polish synthesis.
        // It must not normalize mathematical superscripts or different scripts into words.
        for (word in listOf("aᵐ", "deuxᵃ", "troisᵉ", "Русский", "Ελλάδα", "\u0301")) {
            assertNull(word, FrenchRuleBasedG2P.phonemizeWord(word))
        }
    }

    @Test fun actualOovAdjectivesKeepNasalEntAndFrenchHexaPrefix() {
        mapOf("jacent" to "ʒasɑ̃", "jacents" to "ʒasɑ̃", "jacente" to "ʒasɑ̃t", "jacentes" to "ʒasɑ̃t",
            "hexadécimal" to "ɛɡzadesimal", "hexadécimale" to "ɛɡzadesimal", "hexadécimales" to "ɛɡzadesimal",
            "hexadécimaux" to "ɛɡzadesimo")
            .forEach { (word, ipa) -> assertEquals(word, ipa, FrenchRuleBasedG2P.phonemizeWord(word)) }
    }
}
