package com.learngraphicsprogramming.app.narration

import org.junit.Assert.*
import org.junit.Test

class SpanishIncludeReadingTest {
    @Test fun includeNeverInventsADirectiveWordAndNeverRemovesAnAuthoredOne() {
        for ((source, expected) in listOf(
            "línea #include" to "línea almohadilla include",
            "directiva #include" to "directiva almohadilla include",
            "La línea #include <iostream> conserva la directiva escrita." to
                "La línea almohadilla include abre ángulo iostream cierra ángulo conserva la directiva escrita."
        )) {
            assertEquals(source, expected, SpanishTechnicalSpeech.prepare(source).text)
            assertEquals(source, expected,
                ManualEnglishSpeech.strip(SpanishTechnicalSpeech.prepareForKokoro(source).text))
        }
        assertEquals("directiva almohadilla define valor 1", SpanishTechnicalSpeech.prepare("#define valor 1").text)
        assertEquals("El identificador include guion bajo total permanece igual.",
            SpanishTechnicalSpeech.prepare("El identificador include_total permanece igual.").text)
    }

    @Test fun includeExpansionsKeepTheirOriginalGlyphOwnersAndDistinctFollowingWords() {
        for (source in listOf("La línea #include permite continuar.", "La directiva #include permite continuar.")) {
            val projection = requireNotNull(NarrationSourceProjection.spanish(source, "auto"))
            val parts = NarrationSegments.sentences(listOf(projection.text))
            val mapping = NarrationWordMappings.forBlock(0, source, parts.withIndex().associate { it.index to it.value })
            assertEquals(parts.indices.toSet(), mapping.keys)
            assertEquals(source.indices.toSet(), mapping.values.flatMap { it.displayed.flatMap { range ->
                range.start until range.endExclusive
            } }.toSet())
            val last = source.indexOf("continuar")
            assertTrue(mapping.values.flatMap { it.displayed }.any { it.start <= last && it.endExclusive > last })
            assertEquals(source.contains("directiva"), ManualEnglishSpeech.strip(projection.text).contains("directiva"))
        }
    }

    @Test fun standaloneNamespaceBeforeProseUsesSpanishLetterNamesAndKeepsTheScopeOperator() {
        for ((source, expected) in listOf(
            "std:: indica que el nombre pertenece a esa biblioteca." to "ese te de operador de resolución de ámbito indica que el nombre pertenece a esa biblioteca.",
            "El prefijo std:: tiene dos signos." to "El prefijo ese te de operador de resolución de ámbito tiene dos signos.",
            "std::cout" to "ese te de operador de resolución de ámbito cout",
            "std::vector<int>" to "ese te de operador de resolución de ámbito vector abre ángulo int cierra ángulo"
        )) assertEquals(source, expected, SpanishTechnicalSpeech.prepare(source).text)
        for (source in listOf("El nombre stdio sigue igual.", "mis_std::", "std_total::")) {
            assertFalse(source, SpanishTechnicalSpeech.prepare(source).text.contains("ese te de"))
        }
        val source = "std:: indica que el nombre pertenece a esa biblioteca."
        val speech = SpanishTechnicalSpeech.prepareForKokoro(source).text
        val mapping = NarrationWordMappings.forBlock(0, source, mapOf(0 to speech)).getValue(0)
        assertEquals(NarrationSpokenWords.lexemes(speech).size, mapping.spoken.size)
        assertEquals(source.indices.toSet(), mapping.displayed.flatMap { it.start until it.endExclusive }.toSet())
        val nextWord = source.indexOf("indica")
        val namespaceRanges = mapping.displayed.take(3)
        assertTrue(namespaceRanges.all { it.endExclusive <= nextWord })
    }

    @Test fun correctedPreparedSpeechHasAnotherAudioAndResumeIdentityOnlyWhenItsSpeechChanges() {
        val corrected = SpanishTechnicalSpeech.prepareForKokoro("La línea #include <iostream>.").text
        val previous = corrected.replace("almohadilla", "directiva almohadilla")
        assertNotEquals(KokoroNarrationCache.key("es", "ef_dora", previous),
            KokoroNarrationCache.key("es", "ef_dora", corrected))
        assertNotEquals(NarrationResumePolicy.key("page", "es", "KOKORO", "ef_dora", listOf(previous)),
            NarrationResumePolicy.key("page", "es", "KOKORO", "ef_dora", listOf(corrected)))
        val ordinary = "La palabra directiva aparece aquí."
        assertEquals(ordinary, ManualEnglishSpeech.strip(SpanishTechnicalSpeech.prepareForKokoro(ordinary).text))
    }
}
