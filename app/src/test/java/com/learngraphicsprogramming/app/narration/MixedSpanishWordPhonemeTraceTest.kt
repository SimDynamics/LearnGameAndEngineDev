package com.learngraphicsprogramming.app.narration

import com.learngraphicsprogramming.app.narration.phonemes.SpanishFluidG2P
import java.io.File
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class MixedSpanishWordPhonemeTraceTest {
    @Test fun manualEnglishAnnotationsAreSingleLexicalAtomsIncludingTheProseSuffix() {
        val include = term("include")
        val cout = term("cout")
        val source = ManualEnglishSpeech.PREPARED + "¿Lee " +
            ManualEnglishSpeech.encode(include, code = false) + ", " +
            ManualEnglishSpeech.encode(cout) + " y 12.5?"
        val words = NarrationSpokenWords.lexemes(source)
        assertEquals(listOf("lee", "include", "cout", "y", "12.5"), words.map { it.canonical })
        assertEquals(ManualEnglishSpeech.encode(include, code = false),
            source.substring(words[1].start, words[1].endExclusive))
        assertEquals(ManualEnglishSpeech.encode(cout), source.substring(words[2].start, words[2].endExclusive))
        assertEquals("12.5", source.substring(words.last().start, words.last().endExclusive))
        assertTrue(NarrationSpokenWords.lexemes("\uE000include").isEmpty())
    }

    @Test fun mixedTraceUsesExactSentencePhonesAndRetainsEveryEnglishAliasAsOneWord() {
        val source = ManualEnglishSpeech.PREPARED + "¿La directiva " +
            ManualEnglishSpeech.encode(term("include")) + " usa " +
            ManualEnglishSpeech.encode(term("iostream")) + "? Después, " +
            ManualEnglishSpeech.encode(term("cout"), code = false) + " continúa."
        val trace = assertTrace(source)
        assertEquals(listOf("la", "directiva", "include", "usa", "iostream", "después", "cout", "continúa"),
            trace.words.map { it.word })
        for (entry in listOf(term("include"), term("iostream"), term("cout"))) {
            val range = trace.words.single { it.word == entry.spelling }
            assertEquals(ManualEnglishPronunciations.phonemes(entry),
                trace.phonemes.substring(range.start, range.endExclusive))
        }
        // Context still comes from the entire utterance, including Spanish allophones.
        assertFalse(trace.phonemes.contains('¿'))
        assertEquals(MixedSpanishG2P.phonemizePrepared(source, emptyMap()), trace.phonemes)
    }

    @Test fun numbersOwnTheirExactWholeContextExpansionWithoutChangingLegacyCppOwnership() {
        for (source in listOf("Quedan 12 casas.", "La lectura termina en 0.",
            "El valor es 3.14.", "C++ conserva 21 opciones.")) {
            val prepared = ManualEnglishSpeech.PREPARED + source
            val trace = assertTrace(prepared)
            assertEquals(NarrationSpokenWords.lexemes(prepared).map { it.canonical }, trace.words.map { it.word })
        }
        val source = ManualEnglishSpeech.PREPARED + ManualEnglishSpeech.encode(term("return")) +
            " 0 termina " + ManualEnglishSpeech.encode(term("main"), code = false) + " correctamente."
        val trace = assertTrace(source)
        val zero = trace.words.single { it.word == "0" }
        assertEquals(SpanishFluidG2P.phonemize("cero"), trace.phonemes.substring(zero.start, zero.endExclusive))
        val cpp = assertTrace(ManualEnglishSpeech.PREPARED + "C++.").words.single()
        assertEquals("c", cpp.word)
    }

    @Test fun publishedLibraryParagraphSupportsEveryActualSentenceWithoutChangingLessonOrAudioFrontend() {
        val resources = File(System.getProperty("translation.resources") ?: "src/main/res")
        val file = requireNotNull(resources.absoluteFile.parentFile).resolve(
            "assets/content/es/programming/cpp_fundamentals/programas_y_ejecucion/lesson_primer_programa_y_salida.json")
        val lesson = JSONObject(file.readText())
        val before = lesson.toString()
        val block = lesson.getJSONArray("pages").getJSONObject(0).getJSONArray("blocks").getJSONObject(3)
        assertTrue(block.getString("text").startsWith("Una biblioteca reúne recursos ya preparados"))
        val script = LessonNarrationScript.page(lesson, 0, NarrationLanguage.SPANISH, forKokoro = true)
        val plan = requireNotNull(script.readingPlan(NarrationEngine.KOKORO))
        val parts = NarrationSegments.sentences(script.texts)
        val selected = plan.origins.indices.filter { plan.origins[it].blockIndex == 3 && !plan.origins[it].feedback }
        assertTrue(selected.size > 6)
        for (index in selected) assertTrace(parts[index])
        assertEquals(before, lesson.toString())
    }

    @Test fun preparedTechnicalPunctuationUsesExactOwnershipAfterTheExistingSpokenTransform() {
        for (visible in listOf(
            "La línea #include <iostream> permite enviar texto con std::cout.",
            "¿Qué hace std::cout << 21; después de las llaves { y }?",
            "main usa std::vector<int> y std::string; return 0 indica el resultado.",
            "Lee C++, luego #include <iostream>; conserva (::), { } y puntuación."
        )) {
            val prepared = prepared(visible)
            for (part in NarrationSegments.sentences(listOf(prepared))) assertTrace(part)
        }
    }

    @Test fun tokenDurationsUseExactMetadataRangesForAnnotationsAndNumberAtoms() {
        val source = ManualEnglishSpeech.PREPARED + "La llamada " +
            ManualEnglishSpeech.encode(term("main"), code = false) + " retorna 0."
        val trace = assertTrace(source)
        val chunks = KokoroPhonemeChunks.split(trace.phonemes)
        val durations = chunks.map { chunk -> LongArray(chunk.length + 2) { (it % 3 + 1).toLong() } }
        val sampleCount = durations.sumOf { it.sum() } * NarrationWordTimingBuilder.SAMPLES_PER_FRAME
        val alignment = requireNotNull(NarrationWordTimingBuilder.buildChunked(
            source, "a".repeat(64), sampleCount, trace, chunks, durations))
        assertTrue(NarrationWordTimingBuilder.valid(source, sampleCount, alignment.words))
        assertEquals(NarrationSpokenWords.lexemes(source).map { it.start to it.endExclusive },
            alignment.words.map { it.start to it.endExclusive })
        assertNull(NarrationWordTimingBuilder.buildChunked(source, "a".repeat(64),
            sampleCount + 600L, trace, chunks, durations))
    }

    @Test fun unknownTransformsNeverInventAWordTraceOrAcceptUnvalidatedAnnotations() {
        for (source in listOf("x = 2", "x²", "a + b", "3/4", "std::cout", "a < > b", "a 😀",
            "\uE000unknown\uE001", "\uE000include")) {
            assertNull(source, MixedSpanishG2P.traceWordsPrepared(ManualEnglishSpeech.PREPARED + source))
        }
        assertEquals(SpanishFluidG2P.traceWords("La vida de los gatos."),
            MixedSpanishG2P.traceWordsPrepared(ManualEnglishSpeech.PREPARED + "La vida de los gatos."))
    }

    private fun assertTrace(source: String): SpanishFluidG2P.SpanishPhoneTrace {
        val trace = requireNotNull(MixedSpanishG2P.traceWordsPrepared(source)) { source }
        assertEquals(source, MixedSpanishG2P.phonemizePrepared(source, emptyMap()), trace.phonemes)
        assertEquals(source, NarrationSpokenWords.lexemes(source).map { it.canonical }, trace.words.map { it.word })
        trace.words.forEach {
            assertTrue(it.word, it.start >= 0 && it.start < it.endExclusive && it.endExclusive <= trace.phonemes.length)
            assertFalse(it.word, trace.phonemes.substring(it.start, it.endExclusive).isBlank())
        }
        trace.words.zipWithNext().forEach { (left, right) -> assertTrue(left.endExclusive <= right.start) }
        return trace
    }

    private fun prepared(visible: String): String = ManualEnglishSpeech.protect(
        SpanishTechnicalSpeech.prepareForKokoro(visible).text) { SpanishMathSpeech.text(it) }

    private fun term(spelling: String) = ManualEnglishTerms.entries.single { it.spelling == spelling }
}
