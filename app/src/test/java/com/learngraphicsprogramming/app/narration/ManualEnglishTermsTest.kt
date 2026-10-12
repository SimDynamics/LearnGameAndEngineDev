package com.learngraphicsprogramming.app.narration

import org.junit.Assert.*
import org.junit.Test

class ManualEnglishTermsTest {
    private fun selected(source: String, mathMode: String = "auto") =
        ManualEnglishTerms.select(source, mathMode).map { it.entry.spelling }

    @Test fun requestedSentenceUsesOnlyTheTwoReviewedEnglishNames() {
        val source = "Utilizamos include para incluir la biblioteca iostream. El programa imprime."
        val matches = ManualEnglishTerms.select(source)
        assertEquals(listOf("include", "iostream"), matches.map { it.entry.spelling })
        matches.forEach { assertEquals(it.entry.spelling, source.substring(it.start, it.endExclusive)) }
    }

    @Test fun englishTermsAreCompleteCaseSensitiveIdentifiers() {
        assertEquals(listOf("main", "return", "double", "string", "cout", "cin"),
            selected("main y return; double y string; cout y cin."))
        assertTrue(selected("Main Cout Include Iostream mi_vector imprime imprimir imprime_total " +
            "stringDeEntrada vector_datos cout_total ñmain mainñ 123main main\u0301").isEmpty())
    }

    @Test fun spanishVectorNeedsAnExplicitCppTypeOrHeaderContext() {
        assertTrue(selected("Un vector contiene valores. Calcula el vector de fuerza.").isEmpty())
        assertEquals(listOf("vector", "vector", "vector", "vector", "vector"),
            selected("std::vector, <vector>, vector<int>, tipo vector y contenedor vector.")
                .filter { it == "vector" })
        assertTrue(selected("La función geometria::vector() calcula la fuerza.").isEmpty())
        assertTrue(selected("mi_std::vector() y otrostd::vector() conservan sus nombres.").isEmpty())
        assertTrue(selected("`vector` y `vector_datos` conservan los nombres elegidos.").isEmpty())
    }

    @Test fun spanishDefineAndFinalStaySpanishOutsideNamedTechnicalContexts() {
        assertTrue(selected("Define el resultado final. La función define() calcula un final.").isEmpty())
        assertEquals(listOf("define", "final", "final"),
            selected("#define LIMITE 4; especificador final; void ejecutar() final;")
                .filter { it in setOf("define", "final") })
        assertTrue(selected("`int final{3};` y `int define{4};`").none { it in setOf("define", "final") })
    }

    @Test fun typesAndAutoUseReviewedCuesRatherThanAssumingEveryOccurrenceIsEnglish() {
        assertTrue(selected("El auto llegó. int y char son etiquetas elegidas.").isEmpty())
        assertEquals(listOf("auto", "int", "char", "auto", "int", "char"), selected(
            "auto cantidad{3}; int total{4}; char letra{'a'}; " +
                "auto deduce el tipo; tipo int y tipo char."))
        assertEquals(listOf("int", "char"), selected("`int` y `char` son palabras reservadas."))
    }

    @Test fun compoundLibraryNamesAreMatchedWholeWhileSpanishAuthorNamesArePreserved() {
        val source = "std::invalid_argument y out_of_range; uniform_int_distribution. " +
            "La función imprime_total y cantidad_invalid_argument conservan sus nombres."
        assertEquals(listOf("invalid_argument", "out_of_range", "uniform_int_distribution"), selected(source))
    }

    @Test fun codeLiteralsNeverSelectTheirContents() {
        assertEquals(listOf("cout", "main"), selected(
            "`std::cout << \"include iostream vector main imprime\";` main conserva su papel."))
        assertEquals(listOf("cout"), selected("std::cout << \"include \\\"string\\\" return\";"))
        assertTrue(selected("Un literal \"include\" y el carácter 'x' son datos.").isEmpty())
        assertEquals(listOf("include"), selected("#include \"main.cpp\""))
    }

    @Test fun explicitMathAndForcedMathCannotBeReclassified() {
        val source = "include \$string + main\$ \\(vector + double\\) \\[return\\] \$\$cout\$\$ iostream"
        assertEquals(listOf("include", "iostream"), selected(source))
        assertTrue(selected("include iostream", "inline").isEmpty())
        assertTrue(selected("include iostream", "block").isEmpty())
        assertTrue(selected("\$include sin cerrar").isEmpty())
    }

    @Test fun literalEscapesAndDigitSeparatorsDoNotLeakSelections() {
        assertEquals(listOf("main"), selected("```cpp\n\"include\\\"string\"; main();\n```"))
        assertEquals(listOf("include"), selected("1'000 include"))
    }

    @Test fun rawCppLiteralsProtectEmbeddedQuotesAndCustomDelimiters() {
        for (prefix in listOf("R", "u8R", "uR", "UR", "LR")) {
            assertEquals(listOf("cout", "main"), selected(
                "`std::cout << $prefix\"(include \" return \" vector)\";` main."))
            assertEquals(listOf("cout", "main"), selected(
                "`std::cout << $prefix\"tag(string \"return\" )tag\";` main."))
        }
        assertTrue(selected("R\"tag(include \"return\" sin cierre").isEmpty())
        assertTrue(selected("u8R\"delimiter_demasiado_largo(include \"return\"").isEmpty())
    }

    @Test fun sourceUrlsAreReferencesRatherThanNarrationTerms() {
        assertEquals(listOf("include"), selected("Consulta https://example.org/main/string y luego include."))
    }

    @Test fun additionalSpanishHomonymsDoNotBecomeEnglishWithoutTechnicalCues() {
        assertTrue(selected("El registro data de ayer. Pide que reserve el espacio; " +
            "cuando se case. Si el auto llega;").isEmpty())
        assertEquals(listOf("data", "reserve", "case", "case", "case", "auto", "int"),
            selected("datos.data() y datos.reserve(20); un case marca la entrada; " +
                "case 1: break; palabra clave case; auto total{3}; int total;")
                .filter { it in setOf("data", "reserve", "case", "auto", "int") })
        assertTrue(selected("`int data{3}; int reserve{4};`").none { it in setOf("data", "reserve") })
    }

    @Test fun englishForInBibliographyIsNotAnIsolatedCppKeyword() {
        assertTrue(selected("National Center for Education Research, Institute of Education Sciences.").isEmpty())
        assertTrue(selected("Working draft, standard for programming language C++ (N4950).").isEmpty())
        assertEquals(listOf("for", "for", "for", "for", "for", "for"), selected(
            "Un for con índice; El bucle for; for(;;); `for`; dentro del for; estructura for."))
        assertEquals(listOf("for"), selected("compara for y while").filter { it == "for" })
        assertEquals(listOf("for"), selected("while en lugar de for").filter { it == "for" })
    }

    @Test fun spanishAuthorFunctionsAreNotMistakenForReviewedLibraryMembers() {
        assertTrue(selected("La función reserve() guarda una plaza. La función data() devuelve una fecha.").isEmpty())
        assertTrue(selected("La función obj.data conserva un campo elegido.").isEmpty())
        assertEquals(listOf("reserve", "data", "reserve", "data", "reserve", "data"),
            selected("valores.reserve(10); datos.data(); puntero->reserve(12); puntero->data(); " +
                "método reserve y miembro data."))
        assertEquals(listOf("data"), selected("std::data(valores)"))
    }

    @Test fun aliasesAndPosAreExplicitReviewableData() {
        val entries = ManualEnglishTerms.entries.associateBy { it.spelling }
        assertEquals("I O stream", entries.getValue("iostream").englishText)
        assertEquals(listOf("PRP", "NN", "NN"), entries.getValue("iostream").tags)
        assertEquals("see out", entries.getValue("cout").englishText)
        assertEquals("see in", entries.getValue("cin").englishText)
        assertEquals("invalid argument", entries.getValue("invalid_argument").englishText)
        assertEquals("[int](/ˈɪnt/)", entries.getValue("int").englishText)
        assertFalse(entries.containsKey("imprime"))
        assertFalse(entries.containsKey("imprime_total"))
    }
}
