package com.learngraphicsprogramming.app.narration

import com.learngraphicsprogramming.app.narration.phonemes.FrenchFluidG2P
import com.learngraphicsprogramming.app.narration.phonemes.FrenchWordG2P
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class FrenchSpeechSymbolsTest {
    private val entries = mapOf(
        "la" to "la", "le" to "lə", "fonction" to "fɔ̃ksjɔ̃", "utilise" to "ytiliz",
        "programmation" to "pʁɔɡʁamasjɔ̃", "est" to "ɛ", "ce" to "sə", "vrai" to "vʁɛ",
        "va" to "va", "il" to "il", "partir" to "paʁtiʁ", "dis" to "di", "moi" to "mwa",
        "bonjour" to "bɔ̃ʒuʁ", "côté" to "kote", "nord" to "nɔʁ", "un" to "œ̃"
    )
    private fun frontend(words: MutableList<String>? = null): FrenchFluidG2P {
        // A lookup observer isolates notation/provenance tests from the independent OOV engine.
        val vocabulary = (entries.values.joinToString("") +
            ManualEnglishPronunciations.phones.values.joinToString("") + " .,;:!?()“”ˈˌaeɛply").toSet()
        return FrenchFluidG2P.fromLexicon(entries, fallback = FrenchWordG2P {
            words?.add(it)
            "a"
        }, vocabulary = vocabulary)
    }

    @Test fun ordinaryFrenchPunctuationAndCompoundWordsRemainByteForByteUnchanged() {
        for (source in listOf("Est-ce vrai ?", "Va-t-il partir ?", "Dis-moi.",
            "Il dit « bonjour » !", "L'algorithme — l’ordre : d'abord; ensuite…", "À demain.\n", "3-5")) {
            assertEquals(source, FrenchSpeechSymbols.normalize(source))
        }
        val french = frontend()
        for (source in listOf("Est-ce vrai ?", "Va-t-il partir ?", "Dis-moi.")) {
            assertEquals(french.phonemize(source), MixedFrenchG2P.phonemizePrepared(source, french))
        }
    }

    @Test fun cppNameIsSpokenFullyInCurrentAndOlderUnannotatedQueues() {
        val source = "La programmation C++."
        assertEquals("La programmation C plus plus.", FrenchSpeechSymbols.normalize(source))
        val french = frontend()
        val expected = french.phonemize("La programmation C plus plus.")
        assertEquals(expected, MixedFrenchG2P.phonemizePrepared(source, french))
        assertEquals(expected, MixedFrenchG2P.phonemizePrepared(ManualEnglishSpeech.PREPARED + source, french))
        assertTrue(expected.contains("sˈe ply plˈy"))
    }

    @Test fun residualProgrammingSymbolsAreNamedAndLongestOperatorsRemainWhole() {
        val samples = mapOf(
            "x+=1; x!=0 && x<=5" to "x plus égal 1; x est différent de 0 et logique x est inférieur ou égal à 5",
            "std::cout << x; x>>=1" to "std double deux points cout double chevron ouvrant x; x double chevron fermant égal 1",
            "{côté_nord[2]/3}\\n" to "accolade ouvrante côté trait de soulignement nord crochet ouvrant 2 crochet fermant barre oblique 3 accolade fermante barre oblique inverse n",
            "`#define` <int> | & * % ^ ~ @" to "accent grave dièse define accent grave inférieur à int supérieur à barre verticale esperluette astérisque pour cent accent circonflexe tilde arobase",
            "x->y; a<=>b" to "x flèche y; a comparaison à trois voies b"
        )
        for ((source, expected) in samples) assertEquals(source, expected, FrenchSpeechSymbols.normalize(source))
    }

    @Test fun hashesAndScopeOperatorsRetainDistinctFrenchWordsAndSourceOwnership() {
        val sources = listOf("#", "::", "#include <iostream>", "std:: indique la portée.",
            "std::cout", "std :: cout", "std::cin", "::valeur", "autre::valeur")
        for (source in sources) {
            val pieces = mutableListOf<Triple<Int, Int, String>>()
            val normalized = FrenchSpeechSymbols.normalizeObserved(source) { start, end, value ->
                pieces += Triple(start, end, value)
            }
            assertEquals(FrenchSpeechSymbols.normalize(source), normalized)
            assertEquals(normalized, pieces.joinToString("") { it.third })
            if ('#' in source) {
                assertTrue(source, normalized.contains("dièse"))
                assertTrue(source, pieces.filter { it.first == source.indexOf('#') && it.second == it.first + 1 }
                    .joinToString("") { it.third }.contains("dièse"))
            }
            if ("::" in source) {
                assertTrue(source, normalized.contains("double deux points"))
                assertTrue(source, pieces.filter { it.first == source.indexOf("::") && it.second == it.first + 2 }
                    .joinToString("") { it.third }.contains("double deux points"))
            }
        }
    }

    @Test fun residualMathematicalNotationUsesFrenchNamesRatherThanDiscardingSymbols() {
        val source = "−3×x÷2≈1; y≥0; 30°; a⁺; b⁻"
        assertEquals("moins 3 fois x divisé par 2 approximativement égal à 1; y supérieur ou égal à 0; 30 degré; a plus; b moins",
            FrenchSpeechSymbols.normalize(source))
        assertEquals("un dollar euro livre sterling section",
            FrenchSpeechSymbols.normalize("un $ € £ §"))
        val observed = mutableListOf<String>()
        val phones = MixedFrenchG2P.phonemizePrepared(source, frontend(observed))
        assertTrue(phones.isNotBlank())
        assertTrue(observed.containsAll(listOf("fois", "divisé", "approximativement", "supérieur", "degré")))
        assertFalse(phones.any { it in "−×÷≈≥°⁺⁻0123456789" })
    }

    @Test fun selectedEnglishPhonesStayOpaqueWhileAdjacentNotationIsFullyRead() {
        val source = "La fonction main utilise std::cout << côté_nord/2."
        val prepared = FrenchTechnicalSpeech.prepareForKokoro(source)
        val observed = mutableListOf<String>()
        val phones = MixedFrenchG2P.phonemizePrepared(prepared, frontend(observed))
        val selected = ManualEnglishSpeech.spans(prepared)
        assertEquals(listOf("main", "cout"), selected.map { it.entry.id })
        selected.forEach { assertTrue(phones.contains(ManualEnglishPronunciations.phonemes(it.entry))) }
        assertTrue(phones.contains(MixedFrenchG2P.namespaceFrenchLetters))
        assertTrue(observed.containsAll(listOf("chevron", "ouvrant", "trait", "soulignement", "barre", "oblique")))
        assertFalse(observed.contains("main"))
        assertFalse(observed.contains("cout"))
        assertFalse(phones.any { it in "<>_+/" || it in '\uE100'..'\uF8FF' })
        assertEquals(source, ManualEnglishSpeech.strip(prepared))
    }

    @Test fun narrationBoundaryNormalizationDoesNotChangeContentOrSentenceSegmentation() {
        val source = "La programmation C++. La fonction main utilise std::cout << côté_nord/2."
        val lesson = JSONObject().put("pages", JSONArray().put(JSONObject().put("blocks", JSONArray()
            .put(JSONObject().put("type", "paragraph").put("text", source)))))
        val original = lesson.toString()
        val page = LessonNarrationText.page(lesson, 0, NarrationLanguage.FRENCH, forKokoro = true)
        val sentences = NarrationSegments.sentences(page)
        val visibleBefore = sentences.map(ManualEnglishSpeech::strip)
        sentences.forEach { MixedFrenchG2P.phonemizePrepared(it, frontend()) }
        assertEquals(2, sentences.size)
        assertEquals(visibleBefore, NarrationSegments.sentences(page).map(ManualEnglishSpeech::strip))
        assertEquals(original, lesson.toString())
        assertEquals(source, lesson.getJSONArray("pages").getJSONObject(0).getJSONArray("blocks").getJSONObject(0).getString("text"))
    }

    @Test fun superscriptRunsRetainPowersSignsAndImplicitProducts() {
        val samples = mapOf(
            "(aᵐ)ⁿ = aᵐⁿ" to "(a à la puissance emme ) à la puissance enne égal a à la puissance emme fois enne, fin de l'exposant",
            "aᵐ⁺ⁿ" to "a à la puissance emme plus enne, fin de l'exposant",
            "2ᵃ × 3ᵇ × 5ᶜ" to "2 à la puissance a fois 3 à la puissance bé fois 5 à la puissance cé",
            "( deux au cube )²" to "( deux au cube ) au carré",
            "(−2)³" to "( moins 2) au cube",
            "k⁻¹" to "k à la puissance moins un, fin de l'exposant",
            "x¹²" to "x à la puissance douze",
            "x₁₂ + aₘ₊ₙ" to "x indice douze plus a indice emme plus enne, fin de l'indice"
        )
        for ((source, expected) in samples) {
            assertEquals(source, expected, FrenchSpeechSymbols.normalize(source))
            assertTrue(MixedFrenchG2P.phonemizePrepared(source, frontend()).isNotBlank())
        }
    }

    @Test fun squaredAndCubedUnitsAndUnitFractionsAreNamedCorrectly() {
        val samples = mapOf(
            "6 g/cm³" to "6 gramme par centimètre cube",
            "cm³/g" to "centimètre cube par gramme",
            "10000 cm²" to "10000 centimètre carré",
            "1 m³" to "1 mètre cube",
            "12 km/h" to "12 kilomètre par heure",
            "800 g/L" to "800 gramme par litre"
        )
        for ((source, expected) in samples) {
            assertEquals(source, expected, FrenchSpeechSymbols.normalize(source))
            assertTrue(MixedFrenchG2P.phonemizePrepared(source, frontend()).isNotBlank())
        }
        assertEquals("le cm de côté", FrenchSpeechSymbols.normalize("le cm de côté"))
    }

    @Test fun isolatedSuperscriptSignsDenoteTheirSignAndDoNotInventAnExponent() {
        assertEquals("Na plus Cl moins", FrenchSpeechSymbols.normalize("Na⁺ Cl⁻"))
        assertEquals("x à la puissance moins un, fin de l'exposant", FrenchSpeechSymbols.normalize("x⁻¹"))
    }

    @Test fun characterLiteralsDistinguishLettersDigitsPunctuationSpacesAndNewlines() {
        val samples = mapOf(
            "'D'" to "dé", "'n'" to "enne", "'5'" to "5", "'!'" to "point d'exclamation",
            "' '" to "espace", "'\\n'" to "saut de ligne", "'\n'" to "saut de ligne",
            "'\\t'" to "tabulation", "'\\0'" to "caractère nul", "'\\\\'" to "barre oblique inverse"
        )
        for ((source, expected) in samples) {
            assertEquals(source, "guillemet simple ouvrant $expected guillemet simple fermant", FrenchSpeechSymbols.normalize(source))
            assertTrue(MixedFrenchG2P.phonemizePrepared(source, frontend()).isNotBlank())
        }
        assertEquals("guillemet simple ouvrant espace espace guillemet simple fermant", FrenchSpeechSymbols.normalize("'  '"))
    }

    @Test fun ordinarySingleQuotedFrenchKeepsItsElisionsAndItsEntireText() {
        for (quoted in listOf("'bonjour'", "'l'homme et l'heure'", "‘l’homme et l’heure’")) {
            val inner = quoted.substring(1, quoted.length - 1)
            assertEquals("guillemet simple ouvrant $inner guillemet simple fermant", FrenchSpeechSymbols.normalize(quoted))
            assertTrue(MixedFrenchG2P.phonemizePrepared(quoted, frontend()).isNotBlank())
        }
        assertEquals("L'homme s'arrête d'abord lorsqu'il reçoit l'heure.",
            FrenchSpeechSymbols.normalize("L'homme s'arrête d'abord lorsqu'il reçoit l'heure."))
        assertEquals("apostrophe bonjour", FrenchSpeechSymbols.normalize("'bonjour"))
        val opaque = '\uE123'
        assertEquals("guillemet simple ouvrant $opaque guillemet simple fermant", FrenchSpeechSymbols.normalize("'$opaque'"))
    }
}
