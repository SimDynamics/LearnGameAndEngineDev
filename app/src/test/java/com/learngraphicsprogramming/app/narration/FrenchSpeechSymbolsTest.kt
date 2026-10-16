package com.learngraphicsprogramming.app.narration

import com.learngraphicsprogramming.app.narration.phonemes.FrenchFluidG2P
import com.learngraphicsprogramming.app.narration.phonemes.FrenchWordG2P
import java.io.File
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
            "x+=1; x!=0 && x<=5" to "x addition et affectation 1; x est différent de 0 et logique x est inférieur ou égal à 5",
            "std::cout << x; x>>=1" to "std opérateur de résolution de portée cout double chevron ouvrant x; x décalage à droite et affectation 1",
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
                assertTrue(source, normalized.contains("opérateur de résolution de portée"))
                assertTrue(source, pieces.filter { it.first == source.indexOf("::") && it.second == it.first + 2 }
                    .joinToString("") { it.third }.contains("opérateur de résolution de portée"))
            }
        }
    }

    @Test fun unambiguousCompoundOperatorsUseTheirFrenchTechnicalNamesAndExactOriginalRanges() {
        val samples = linkedMapOf(
            "::" to "opérateur de résolution de portée",
            "<<=" to "décalage à gauche et affectation", ">>=" to "décalage à droite et affectation",
            "+=" to "addition et affectation", "-=" to "soustraction et affectation",
            "*=" to "multiplication et affectation", "/=" to "division et affectation",
            "%=" to "modulo et affectation", "&=" to "et binaire et affectation",
            "|=" to "ou binaire et affectation", "^=" to "ou exclusif binaire et affectation",
            ".*" to "pointeur vers membre sur objet", "->*" to "pointeur vers membre sur pointeur",
            "<=>" to "comparaison à trois voies"
        )
        for ((operator, words) in samples) {
            val source = "gauche${operator}droite"
            val pieces = mutableListOf<Triple<Int, Int, String>>()
            val actual = FrenchSpeechSymbols.normalizeObserved(source) { start, end, rendered ->
                pieces += Triple(start, end, rendered)
            }
            assertEquals(source, "gauche $words droite", actual)
            assertEquals(actual, pieces.joinToString("") { it.third })
            assertEquals(source, words, pieces.filter { it.first == 6 && it.second == 6 + operator.length }
                .joinToString("") { it.third }.trim())
        }
    }

    @Test fun ambiguousCppGlyphsKeepTheirNeutralNamesAndDoNotGuessStreamOrPointerSemantics() {
        for ((source, expected) in mapOf(
            "std::cout << valeur" to "std opérateur de résolution de portée cout double chevron ouvrant valeur",
            "std::cin >> valeur" to "std opérateur de résolution de portée cin double chevron fermant valeur",
            "bits << 2" to "bits double chevron ouvrant 2",
            "int& référence; &objet; a & b" to "int esperluette référence; esperluette objet; a esperluette b",
            "int* pointeur; *pointeur; a * b" to "int astérisque pointeur; astérisque pointeur; a astérisque b",
            "x->membre" to "x flèche membre", "C++" to "C plus plus"
        )) assertEquals(source, expected, FrenchSpeechSymbols.normalize(source))
        val ordinary = "L'algorithme reste clair : il n'ajoute ni mot ni formule."
        assertEquals(ordinary, FrenchSpeechSymbols.normalize(ordinary))
    }

    @Test fun quotedCppDataAndLiteralSignsRetainGraphicalNamesWithoutChangingUnquotedOperators() {
        val source = "std::cout << \"x += y; std::objet\"; total += valeur; objet.*membre"
        val normalized = FrenchSpeechSymbols.normalize(source)
        assertEquals("std opérateur de résolution de portée cout double chevron ouvrant \"x plus égal y; std double deux points objet\"; total addition et affectation valeur; objet pointeur vers membre sur objet membre", normalized)
        val changes = FrenchSpeechSymbols.changedOperatorReadings(source)
        assertEquals(listOf("::", "+=", ".*"), changes.map { source.substring(it.first, it.second) })
        assertEquals(listOf("opérateur de résolution de portée", "addition et affectation", "pointeur vers membre sur objet"),
            changes.map { it.third })
        for (literal in listOf("\":: +=\"", "\"x \\\":: +=\\\" y\"", "R\"tag(x \":: +=\" y)tag\"",
            "'::'", "'+='", "« :: += »", "“:: +=”")) {
            assertTrue(literal, FrenchSpeechSymbols.changedOperatorReadings(literal).isEmpty())
            assertFalse(literal, FrenchSpeechSymbols.normalize(literal).contains("résolution"))
            assertFalse(literal, FrenchSpeechSymbols.normalize(literal).contains("affectation"))
        }
        assertTrue(FrenchSpeechSymbols.changedOperatorReadings("#include <iostream>").isEmpty())
        assertTrue(FrenchSpeechSymbols.changedOperatorReadings("Le résultat est 2 + 3; x < y.").isEmpty())
    }

    @Test fun incrementDecrementAndNegationUseTechnicalNamesOnlyWhenTheirOperatorContextIsClear() {
        for ((source, expected) in linkedMapOf(
            "x++" to "x incrémentation", "++x" to "incrémentation x",
            "x--" to "x décrémentation", "--x" to "décrémentation x",
            "compteur++;" to "compteur incrémentation;", "++compteur;" to "incrémentation compteur;",
            "`compteur--`" to "accent grave compteur décrémentation accent grave",
            "L'opérateur ++." to "L'opérateur incrémentation.",
            "L'opérateur --." to "L'opérateur décrémentation.",
            "!condition" to "négation logique condition", "(!condition)" to "( négation logique condition)",
            "`! condition`" to "accent grave négation logique condition accent grave",
            "L'opérateur !." to "L'opérateur négation logique."
        )) {
            assertEquals(source, expected, FrenchSpeechSymbols.normalize(source))
            val changes = FrenchSpeechSymbols.changedOperatorReadings(source)
            assertEquals(source, 1, changes.size)
            assertTrue(changes.single().third in setOf("incrémentation", "décrémentation", "négation logique"))
        }
        for (source in listOf("C++", "C++20", "C++/CLI", "Le langage C++ utilise une bibliothèque.",
            "C ++", "attention--", "Un texte--sans espace.", "Bonjour !", "Très bien! À demain.",
            "\"x++ et !condition\"", "'++'", "« -- »", "https://example.org/a::b?x+=2",
            "[voir](https://example.org/a::b)", "[mot](/::/)")) {
            assertTrue(source, FrenchSpeechSymbols.changedOperatorReadings(source).isEmpty())
        }
        assertEquals("C plus plus 20", FrenchSpeechSymbols.normalize("C++20"))
        assertEquals("négation logique négation logique condition", FrenchSpeechSymbols.normalize("!!condition"))
        assertEquals(2, FrenchSpeechSymbols.changedOperatorReadings("!!condition").size)
        assertTrue(FrenchSpeechSymbols.normalize("teste!std::empty(valeurs)").contains("négation logique"))
        assertEquals(0, FrenchSpeechSymbols.changedOperatorReadings("Bonjour!ami").size)
        assertFalse(FrenchSpeechSymbols.normalize("5!std::empty").contains("négation logique"))
        assertEquals("Bonjour !", FrenchSpeechSymbols.normalize("Bonjour !"))
        assertEquals("Très bien! À demain.", FrenchSpeechSymbols.normalize("Très bien! À demain."))
    }

    @Test fun explicitlyDiscussedSemicolonUsesItsFrenchNameWhileOrdinaryPunctuationKeepsItsPause() {
        for ((source, expected) in linkedMapOf(
            ";" to "point-virgule",
            "N'ajoute pas ; à toutes les lignes." to "N'ajoute pas point-virgule à toutes les lignes.",
            "Chaque instruction se termine par ;." to "Chaque instruction se termine par point-virgule.",
            "Le symbole ; termine une instruction." to "Le symbole point-virgule termine une instruction.",
            "Écris ; après l'instruction." to "Écris point-virgule après l'instruction.",
            "Le signe \";\" est un terminateur." to "Le signe \" point-virgule \" est un terminateur.",
            "`x;`" to "accent grave x point-virgule accent grave"
        )) {
            assertEquals(source, expected, FrenchSpeechSymbols.normalize(source))
            assertEquals(source, listOf("point-virgule"), FrenchSpeechSymbols.changedOperatorReadings(source).map { it.third })
        }
        for (source in listOf("La fonction main ; nous étudierons ensuite son retour.",
            "Les accolades délimitent un groupe d'instructions ; ce ne sont pas des messages.",
            "Je lis ; puis je vérifie.", "Il ajoute ; ensuite, il vérifie.", "\"x;y\"", "R\"(x;y)\"")) {
            assertTrue(source, FrenchSpeechSymbols.changedOperatorReadings(source).isEmpty())
            assertFalse(source, FrenchSpeechSymbols.normalize(source).contains("point-virgule"))
        }
    }

    @Test fun theActualFrenchFirstProgramPageNamesItsTwoSemicolonMentionsButNotItsProseSeparator() {
        val resources = File(System.getProperty("translation.resources") ?: "src/main/res")
        val lesson = JSONObject(requireNotNull(resources.absoluteFile.parentFile).resolve("assets/content/fr/programming/cpp_fundamentals/programas_y_ejecucion/lesson_primer_programa_y_salida.json").readText())
        val blocks = lesson.getJSONArray("pages").getJSONObject(2).getJSONArray("blocks")
        val source = (0 until blocks.length()).map { blocks.getJSONObject(it).optString("text") }
            .single { it.contains("N'ajoute pas ;") }
        assertEquals(3, source.count { it == ';' })
        val prepared = FrenchTechnicalSpeech.prepareForKokoro(source)
        val readings = FrenchSpeechSymbols.resolvedOperatorReadings(prepared).filter { it.symbol == ";" }
        assertEquals(listOf("point-virgule", "point-virgule", null), readings.map { it.words })
        assertEquals(listOf(true, true, false), readings.map { it.changed })
        assertEquals(2, FrenchTechnicalSpeech.changedOperatorReadings(prepared).count { it.third == "point-virgule" })
        assertEquals(source, ManualEnglishSpeech.strip(prepared))
        val generated = LessonNarrationText.page(lesson, 2, NarrationLanguage.FRENCH, forKokoro = true)
            .single { ManualEnglishSpeech.strip(it).contains("N'ajoute pas;") }
        assertTrue(ManualEnglishSpeech.strip(generated).contains("termine par;."))
        val generatedReadings = FrenchSpeechSymbols.resolvedOperatorReadings(generated).filter { it.symbol == ";" }
        assertEquals(listOf("point-virgule", "point-virgule", null), generatedReadings.map { it.words })
        val sentenceReadings = NarrationSegments.sentences(listOf(generated)).flatMap {
            FrenchSpeechSymbols.resolvedOperatorReadings(it).filter { it.symbol == ";" }
        }
        assertEquals(listOf("point-virgule", "point-virgule", null), sentenceReadings.map { it.words })
    }

    @Test fun explicitCommandAndOptionContextsKeepDoubleHyphensGraphicalEvenInsideCode() {
        for (command in listOf("cmake", "clang", "clang++", "ctest", "ninja", "git", "gcc", "g++", "gradle")) {
            val source = "Exécute `$command --version`."
            val normalized = FrenchSpeechSymbols.normalize(source)
            assertTrue(source, normalized.contains("moins moins version"))
            assertFalse(source, normalized.contains("décrémentation"))
            assertFalse(source, normalized.contains("incrémentation"))
            assertTrue(source, FrenchSpeechSymbols.changedOperatorReadings(source).isEmpty())
        }
        for (source in listOf("L'option `--build` construit le projet.", "L'argument --target choisit la cible.",
            "`--build` est une option.", "--target est un argument.")) {
            assertTrue(source, FrenchSpeechSymbols.normalize(source).contains("moins moins"))
            assertTrue(source, FrenchSpeechSymbols.changedOperatorReadings(source).isEmpty())
        }
        assertEquals("décrémentation compteur", FrenchSpeechSymbols.normalize("--compteur"))
        assertEquals("accent grave décrémentation compteur accent grave", FrenchSpeechSymbols.normalize("`--compteur`"))
    }

    @Test fun explicitRegularExpressionAndPrimitiveReferenceContextsDoNotBorrowAnotherOperatorMeaning() {
        for (source in listOf("La regex .* accepte plusieurs caractères.",
            "L'expression régulière `.*` accepte plusieurs caractères.", ".* est une expression régulière.",
            "La regex ->* est citée comme texte.")) {
            val normalized = FrenchSpeechSymbols.normalize(source)
            assertFalse(source, normalized.contains("pointeur vers membre"))
            assertTrue(source, FrenchSpeechSymbols.changedOperatorReadings(source).isEmpty())
        }
        assertEquals("objet pointeur vers membre sur objet membre", FrenchSpeechSymbols.normalize("objet.*membre"))
        assertEquals("objet pointeur vers membre sur pointeur membre", FrenchSpeechSymbols.normalize("objet->*membre"))
        for (source in listOf("int&& valeur", "auto&& valeur", "const unsigned long&& valeur", "int const && valeur")) {
            assertTrue(source, FrenchSpeechSymbols.normalize(source).contains("double esperluette"))
            assertEquals(source, listOf("double esperluette"), FrenchSpeechSymbols.changedOperatorReadings(source).map { it.third })
        }
        assertEquals("a et logique b", FrenchSpeechSymbols.normalize("a && b"))
        assertTrue(FrenchSpeechSymbols.changedOperatorReadings("a && b").isEmpty())
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
