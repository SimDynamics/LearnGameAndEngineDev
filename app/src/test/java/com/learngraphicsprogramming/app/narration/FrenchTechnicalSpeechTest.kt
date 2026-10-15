package com.learngraphicsprogramming.app.narration

import java.security.MessageDigest
import org.junit.Assert.*
import org.junit.Test

class FrenchTechnicalSpeechTest {
    private fun selected(source: String, mode: String = "auto") =
        ManualEnglishTerms.selectFrench(source, mode).map { it.entry.spelling }

    @Test fun theSmallReviewedStandaloneListReusesTheSameEntryObjects() {
        val source = "Nous utilisons include pour charger la bibliothèque iostream. std::cout et cin; return termine."
        val matches = ManualEnglishTerms.selectFrench(source)
        assertEquals(listOf("include", "iostream", "cout", "cin", "return"), matches.map { it.entry.spelling })
        matches.forEach { match ->
            assertEquals(match.entry.spelling, source.substring(match.start, match.endExclusive))
            assertSame(ManualEnglishTerms.entries.single { it.id == match.entry.id }, match.entry)
        }
    }

    @Test fun ordinaryFrenchHomonymsDoNotBecomeEnglishEvenInsideATechnicalParagraph() {
        val sentences = listOf(
            "La main droite garde le livre; la main gauche reste libre.",
            "Le public continue de lire un long texte au format habituel.",
            "Le double de cette longueur est pair, sans exception.",
            "Le front est calme; le sort de cette porte close reste inconnu.",
            "Un char lourd avance. Un if pousse devant la maison.",
            "Le short et le string sont deux vêtements; cet insert reste visible.",
            "La fonction continue sur cet intervalle est définie partout.",
            "La fonction sort du domaine.",
            "La fonction sort de cette partie.",
            "La fonction double sa valeur et le résultat final reste pair.",
            "En C++, la main reste libre et le public continue de lire.",
            "Une case est vide. Le type de problème reste sans exception.",
            "Le public (sans bruit) continue (sans interruption)."
        )
        sentences.forEach { assertTrue(it, selected(it).isEmpty()) }
    }

    @Test fun localFrenchRolesDistinguishCallableNamesTypesKeywordsAndLibraries() {
        assertEquals(listOf("main", "format", "front", "close"), selected(
            "La fonction main; la fonction appelée format; la méthode front; la méthode nommée close."))
        assertEquals(listOf("double", "long", "char", "exception"), selected(
            "Le type double; le type de données long; le type char; la classe string; la classe exception."))
        assertEquals(listOf("continue", "public", "if", "for", "format"), selected(
            "Le mot-clé continue; le spécificateur public; l'instruction if; la boucle for; la bibliothèque format."))
        assertTrue(selected("La fonction continue, la fonction double, la classe long, la bibliothèque public, " +
            "la directive exception, le type main et le membre public.").isEmpty())
        assertEquals(listOf("sort", "sort", "sort"), selected(
            "La fonction nommée sort; la méthode appelée sort; std::sort(debut, fin)."))
        assertTrue(selected("La méthode sort du domaine. Le membre sort de cette partie.").isEmpty())
    }

    @Test fun frenchElisionsDoNotHideTheRemainingSource() {
        val straight = "L'instruction continue suit l'appel de la fonction main; d'abord, la directive define est lue."
        assertEquals(listOf("continue", "main", "define"), selected(straight))
        assertEquals(selected(straight), selected(straight.replace('\'', '’')))
        assertEquals(listOf("main", "include"), selected("Aujourd'hui, la fonction main utilise include."))
        assertEquals(listOf("if"), selected("Le mot-clé if précède l'instruction conditionnelle."))
    }

    @Test fun cppSyntaxWorksWithoutBorrowingFrenchProseAsAGlobalHint() {
        assertEquals(listOf("int", "main", "return"), selected("`int main() { return 0; }`"))
        assertEquals(listOf("vector", "int", "main", "double", "public"), selected(
            "std::vector<int>; main();\ndouble valeur{0};\npublic:"))
        assertEquals(listOf("data", "reserve", "sort"), selected("valeurs.data(); valeurs.reserve(8); std::sort(debut, fin);"))
        assertEquals(listOf("define", "pragma"), selected("#define LIMITE 4\n#pragma once"))
        assertTrue(selected("Un char lourd; un double sens; un long chemin; une fonction continue.").isEmpty())
    }

    @Test fun literalDataUrlsAndExplicitMathRemainProtected() {
        assertTrue(selected("Un texte \"include iostream main return\" et 'cout' restent des données.").isEmpty())
        assertEquals(listOf("cout", "return"), selected("`std::cout << \"main include\" << L'x'; return 0;`"))
        assertEquals(listOf("cout"), selected("`std::cout << R\"tag(include \"main\" return)tag\";`"))
        assertTrue(selected("Voir https://example.org/include/main/iostream.").isEmpty())
        assertTrue(selected("\\(main + double\\) et \$return\$").isEmpty())
        assertTrue(selected("include iostream main", "inline").isEmpty())
        assertTrue(selected("include iostream main", "block").isEmpty())
    }

    @Test fun wholeCaseSensitiveIdentifiersAndUnknownFrenchIdentifiersStayUntouched() {
        assertTrue(selected("Main Include Iostream main_total ñmain mainñ main\u0301 variable_include").isEmpty())
        assertEquals(listOf("main"), selected("La fonction main appelle ma_fonction et affiche_total."))
        assertTrue(selected("La fonction affiche_total et la méthode reserve_locale conservent leurs noms.").isEmpty())
    }

    @Test fun preparedFrenchMetadataRoundTripsTheExactOriginalSourceWithoutSpanishCodeNames() {
        val source = "L'instruction return suit la fonction main; include <iostream> et std::cout."
        val prepared = FrenchTechnicalSpeech.prepareForKokoro(source)
        assertTrue(prepared.startsWith(ManualEnglishSpeech.PREPARED))
        val spans = ManualEnglishSpeech.spans(prepared)
        assertEquals(listOf("return", "main", "include", "iostream", "cout"), spans.map { it.entry.spelling })
        assertTrue(spans.all { !it.code })
        assertEquals(source, ManualEnglishSpeech.strip(prepared))
        assertEquals(prepared, FrenchTechnicalSpeech.prepareForKokoro(prepared))
    }

    @Test fun noSelectedTermsMeansNoNewMarkerAndNoChangeToOrdinaryFrench() {
        val source = "La main reste calme. La fonction continue garde son domaine."
        assertSame(source, FrenchTechnicalSpeech.prepareForKokoro(source))
        assertEquals("", FrenchTechnicalSpeech.prepareForKokoro(""))
        assertSame(source, FrenchTechnicalSpeech.prepareForKokoro(source, "block"))
        assertEquals("include main", FrenchTechnicalSpeech.prepareForKokoro("include main", "inline"))
    }

    @Test fun spanishSelectionAndItsOriginalHomonymRulesRemainUnchanged() {
        assertEquals(listOf("main", "double", "public", "continue", "long", "front", "format", "exception"),
            ManualEnglishTerms.select("main double public continue long front format exception").map { it.entry.spelling })
        assertTrue(ManualEnglishTerms.select("El auto llegó. Define el resultado final.").isEmpty())
        assertEquals(listOf("define", "final"), ManualEnglishTerms.select("#define LIMITE 4; especificador final;")
            .map { it.entry.spelling })
    }

    @Test fun frenchLexiconEntriesAlreadyUsingReviewedEnglishKeepTheirOriginalProseCacheKeys() {
        val sources = listOf("Le type string.", "L'instruction break.", "Le conteneur set.", "La fonction flush.")
        sources.forEach { source ->
            assertTrue(source, selected(source).isEmpty())
            val prepared = FrenchTechnicalSpeech.prepareForKokoro(source)
            assertSame(source, prepared)
            val legacy = MessageDigest.getInstance("SHA-256").digest(listOf("kokoro-wav-v1", KokoroModelManifest.revision,
                "fr", "ff_siwis", source).joinToString("\u0000").toByteArray(Charsets.UTF_8))
                .joinToString("") { "%02x".format(it.toInt() and 255) }
            assertEquals("Unchanged pronunciation must retain the exact existing audio key", legacy,
                KokoroNarrationCache.key("fr", "ff_siwis", prepared))
        }
    }

    @Test fun alreadyEnglishWordsStillReceiveMetadataWhenCppNotationRequiresPreparation() {
        assertEquals(listOf("string", "break", "set", "flush"), selected("`string break set flush`"))
        assertEquals(listOf("string", "set", "flush"), selected("std::string; std::set; std::flush;"))
        assertEquals(listOf("string", "set", "int"), selected("<string> et set<int>"))
        val source = "std::string"
        val prepared = FrenchTechnicalSpeech.prepareForKokoro(source)
        assertEquals(listOf("string"), ManualEnglishSpeech.spans(prepared).map { it.entry.spelling })
        assertEquals(source, ManualEnglishSpeech.strip(prepared))
        assertNotEquals(KokoroNarrationCache.key("fr", "ff_siwis", source),
            KokoroNarrationCache.key("fr", "ff_siwis", prepared))
    }
}
