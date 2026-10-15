package com.learngraphicsprogramming.app.narration

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Assert.assertThrows
import org.junit.Test

class ManualEnglishSpeechTest {
    private fun entry(spelling: String) = ManualEnglishTerms.entries.single { it.spelling == spelling }
    private fun selected(text: String) = ManualEnglishSpeech.spans(text).map { it.entry.spelling }

    @Test fun directivesHeadersAndQualifiedTypesKeepTheirOriginalEnglishSelection() {
        val directive = SpanishTechnicalSpeech.prepareForKokoro("La línea #include <iostream> prepara el programa.").text
        assertEquals(listOf("include", "iostream"), selected(directive))
        assertEquals(SpanishTechnicalSpeech.prepare("La línea #include <iostream> prepara el programa.").text,
            ManualEnglishSpeech.strip(directive))

        val template = SpanishTechnicalSpeech.prepareForKokoro("Utiliza std::vector<int> para guardar valores.").text
        assertTrue(selected(template).contains("vector"))
        assertEquals(SpanishTechnicalSpeech.prepare("Utiliza std::vector<int> para guardar valores.").text,
            ManualEnglishSpeech.strip(template))

        assertEquals(listOf("iostream"), selected(SpanishTechnicalSpeech.prepareForKokoro("La biblioteca <iostream>.").text))
    }

    @Test fun wholeStudentIdentifiersAreNeverReclassifiedAfterSplitting() {
        for (source in listOf("`imprime_total(valor);`", "`stringUsuario;`", "`mi_string;`", "`vectorDatos;`")) {
            val prepared = SpanishTechnicalSpeech.prepareForKokoro(source)
            assertTrue(source, ManualEnglishSpeech.isPrepared(prepared.text))
            assertTrue(source, selected(prepared.text).isEmpty())
            assertEquals(source, SpanishTechnicalSpeech.prepare(source).text, ManualEnglishSpeech.strip(prepared.text))
            assertEquals(source, prepared.text, SpanishTechnicalSpeech.prepareForKokoro(prepared.text).text)
        }
    }

    @Test fun codeStringContentsAndOrdinarySpanishRemainSpanish() {
        val code = SpanishTechnicalSpeech.prepareForKokoro("`std::cout << \"include vector imprime\";`").text
        assertTrue(selected(code).contains("cout"))
        assertFalse(selected(code).contains("include"))
        assertFalse(selected(code).contains("vector"))
        assertFalse(selected(code).contains("imprime"))

        val prose = "El vector representa una dirección e imprime el resultado."
        val prepared = SpanishTechnicalSpeech.prepareForKokoro(prose).text
        assertTrue(selected(prepared).isEmpty())
        assertEquals(prose, ManualEnglishSpeech.strip(prepared))
    }

    @Test fun completedSelectionIsIdempotentEvenWithoutEnglishTerms() {
        for (source in listOf("La programación permite resolver problemas.", "Utilizamos include para incluir iostream.",
            "`stringUsuario = imprime_total;`", "std::vector<int>", "# include <iostream>")) {
            val first = SpanishTechnicalSpeech.prepareForKokoro(source).text
            assertTrue(ManualEnglishSpeech.isPrepared(first))
            val second = SpanishTechnicalSpeech.prepareForKokoro(first)
            assertEquals(first, second.text)
            assertFalse(second.changed)
        }
        assertEquals("", SpanishTechnicalSpeech.prepareForKokoro("").text)
    }

    @Test fun publicPreparationRetainsItsLegacyContract() {
        val source = "#include <iostream>"
        val legacy = SpanishTechnicalSpeech.prepare(source)
        assertEquals("almohadilla include abre ángulo iostream cierra ángulo", legacy.text)
        assertFalse(ManualEnglishSpeech.isPrepared(legacy.text))
        assertTrue(ManualEnglishSpeech.spans(legacy.text).isEmpty())
    }

    @Test fun strippingKnownCompoundAndNumericNamesRecoversLegacyTechnicalSpeech() {
        for (source in listOf("`std::string_view texto;`", "`std::invalid_argument(razon);`",
            "`static_cast<int>(valor)`", "`std::mt19937 motor;`", "`size_t longitud;`")) {
            val prepared = SpanishTechnicalSpeech.prepareForKokoro(source).text
            assertTrue(source, ManualEnglishSpeech.spans(prepared).isNotEmpty())
            assertEquals(source, SpanishTechnicalSpeech.prepare(source).text, ManualEnglishSpeech.strip(prepared))
        }
    }

    @Test fun strippingNumericNamesRetainsEachOccurrencesOriginalProseOrCodeReading() {
        val prose = "El motor mt19937 genera la secuencia."
        val prosePrepared = SpanishTechnicalSpeech.prepareForKokoro(prose).text
        assertEquals(prose, ManualEnglishSpeech.strip(prosePrepared))
        assertFalse(ManualEnglishSpeech.spans(prosePrepared).single().code)
        val code = "`mt19937 motor;`"
        val codePrepared = SpanishTechnicalSpeech.prepareForKokoro(code).text
        assertTrue(ManualEnglishSpeech.spans(codePrepared).single().code)
        assertEquals(SpanishTechnicalSpeech.prepare(code).text, ManualEnglishSpeech.strip(codePrepared))
    }

    @Test fun normalizationTreatsAnnotationsAsAtomicAndAvoidsExistingPrivateCharacters() {
        val annotation = ManualEnglishSpeech.encode(entry("include"))
        val original = "${ManualEnglishSpeech.PREPARED}Usa \uE100 \uE101 $annotation y $annotation."
        var normalizedInput = ""
        val normalized = ManualEnglishSpeech.protect(original) { input ->
            normalizedInput = input
            input.uppercase()
        }
        assertFalse(normalizedInput.contains("include", ignoreCase = true))
        assertFalse(normalizedInput.contains(ManualEnglishSpeech.PREPARED))
        assertTrue(normalizedInput.contains('\uE100'))
        assertTrue(normalizedInput.contains('\uE101'))
        assertEquals("${ManualEnglishSpeech.PREPARED}USA \uE100 \uE101 $annotation Y $annotation.", normalized)
        assertEquals(listOf("include", "include"), selected(normalized))
        assertEquals("USA \uE100 \uE101 include Y include.", ManualEnglishSpeech.strip(normalized))
    }

    @Test fun sentinelCannotPreventPureMathematicalNormalization() {
        val prepared = "${ManualEnglishSpeech.PREPARED}3/4"
        val normalized = ManualEnglishSpeech.protect(prepared) { SpanishMathSpeech.text(it, "block") }
        assertEquals("tres cuartos", ManualEnglishSpeech.strip(normalized))
        assertTrue(ManualEnglishSpeech.isPrepared(normalized))
        for (mode in listOf("inline", "block")) {
            assertTrue(selected(SpanishTechnicalSpeech.prepareForKokoro("std::vector<int>", mode).text).isEmpty())
        }
    }

    @Test fun malformedOrUnknownAnnotationsAreRejectedInsteadOfSilentlyDiscarded() {
        assertThrows(IllegalArgumentException::class.java) { ManualEnglishSpeech.strip("${ManualEnglishSpeech.START}missing") }
        assertThrows(IllegalArgumentException::class.java) {
            ManualEnglishSpeech.spans("${ManualEnglishSpeech.START}missing${ManualEnglishSpeech.END}")
        }
        assertThrows(IllegalArgumentException::class.java) { ManualEnglishSpeech.protect("${ManualEnglishSpeech.END}") { it } }
    }

    @Test fun aTightLengthLimitCannotSplitAnAnnotation() {
        val annotation = ManualEnglishSpeech.encode(entry("iostream"))
        val text = "a".repeat(695) + annotation + "b".repeat(100)
        val boundary = ManualEnglishSpeech.safeBoundary(text, 700)
        assertEquals(695, boundary)
        val before = text.substring(0, boundary)
        val after = text.substring(boundary)
        assertEquals(ManualEnglishSpeech.strip(text), ManualEnglishSpeech.strip(before) + ManualEnglishSpeech.strip(after))
        assertEquals(695, ManualEnglishSpeech.safeBoundary(text, 695))
        assertEquals(695 + annotation.length, ManualEnglishSpeech.safeBoundary(text, 695 + annotation.length))
        assertEquals(annotation.length, ManualEnglishSpeech.safeBoundary(annotation, 1))
    }

    @Test fun invisibleMetadataCannotMoveLongSentenceBoundariesOrUnchangedCacheKeys() {
        val raw = "include " + "palabra ".repeat(320) + ". Una frase española sin términos técnicos."
        val plain = SpanishTechnicalSpeech.prepare(raw).text
        val prepared = SpanishTechnicalSpeech.prepareForKokoro(raw).text
        val legacy = NarrationSegments.sentences(listOf(plain))
        val mixed = NarrationSegments.sentences(listOf(prepared))
        assertEquals(legacy, mixed.map(ManualEnglishSpeech::strip))
        assertTrue(mixed.size >= 3)
        assertEquals(1, mixed.sumOf { ManualEnglishSpeech.spans(it).size })
        for ((before, after) in legacy.zip(mixed)) {
            if (ManualEnglishSpeech.spans(after).isEmpty()) {
                assertEquals(KokoroNarrationCache.key("es", "ef_dora", before),
                    KokoroNarrationCache.key("es", "ef_dora", after))
            }
        }
    }
}
