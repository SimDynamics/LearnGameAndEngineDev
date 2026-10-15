package com.learngraphicsprogramming.app.narration

import com.learngraphicsprogramming.app.narration.phonemes.SpanishFluidG2P
import java.io.File
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class CmakeNarrationSyntaxTest {
    private val expansion = "\${clientes}"
    private val spoken = "dólar abre llave clientes cierra llave"

    @Test fun cmakeVariableExpansionsReadTheirDollarWithoutDiscardingTheirNameOrBraces() {
        assertEquals(spoken, SpanishTechnicalSpeech.prepare(expansion).text)
        assertEquals("dólar abre llave cliente guion bajo principal cierra llave",
            SpanishTechnicalSpeech.prepare("\${cliente_principal}").text)
        assertEquals("$spoken y $spoken.", SpanishTechnicalSpeech.prepare("$expansion y $expansion.").text)
        assertEquals("$spoken sin comillas compara su valor con $spoken.",
            SpanishTechnicalSpeech.prepare("$expansion sin comillas compara su valor con $expansion.").text)
        assertEquals("$spoken $spoken", SpanishTechnicalSpeech.prepare("$expansion$expansion").text)
        assertEquals("list abre paréntesis APPEND $spoken comprobar guion bajo cajas cierra paréntesis",
            SpanishTechnicalSpeech.prepare("list(APPEND $expansion comprobar_cajas)").text)
        assertEquals("código, $spoken, fin de código", SpanishTechnicalSpeech.prepare("`$expansion`").text)
        assertEquals("abre comilla doble $spoken cierra comilla doble",
            SpanishTechnicalSpeech.prepare("\"$expansion\"").text)
    }

    @Test fun explicitMathematicsAndUnrelatedDollarNotationKeepTheirStrictContract() {
        for (text in listOf("\${x}$", "\${x}+1$", "\${x}^2$", "\${x}^{2}$", "\${x} + 1$",
            "Calcula \${x}+1$ y \${y}^2$.", "Calcula \${x}$ y \${y}$.", "Calcula \$x\$ y \$y\$.", "\$\$x+y\$\$")) {
            assertEquals(SpanishTechnicalSpeech.Preparation(text, false), SpanishTechnicalSpeech.prepare(text))
        }
        for (mode in listOf("inline", "block")) {
            assertEquals(SpanishTechnicalSpeech.Preparation(expansion, false), SpanishTechnicalSpeech.prepare(expansion, mode))
        }
        for (source in listOf("Precio \$ 7", "\$cliente", "\${sin_cierre", "`\$sin_llaves`")) {
            val text = SpanishTechnicalSpeech.prepare(source).text
            assertTrue("Unsupported dollar syntax must reach strict G2P unchanged: $source", '$' in text)
            assertThrows(IllegalArgumentException::class.java) { SpanishFluidG2P.phonemize(text) }
        }
        val prepared = SpanishTechnicalSpeech.prepare("Usa $expansion.")
        assertEquals(SpanishTechnicalSpeech.Preparation(prepared.text, false), SpanishTechnicalSpeech.prepare(prepared.text))
    }

    @Test fun correctedSpeechHasExactPhonemeProvenanceAndSeparatelyOwnedDollarAndBraceGlyphs() {
        val source = "Usa $expansion y \${cliente_principal}."
        val pieces = mutableListOf<Triple<Int, Int, String>>()
        val prepared = SpanishTechnicalSpeech.prepareForReading(source) { start, end, text ->
            pieces += Triple(start, end, text)
        }
        assertEquals(SpanishTechnicalSpeech.prepareForKokoro(source), prepared)
        assertEquals(2, pieces.count { source.substring(it.first, it.second) == "$" && it.third.trim() == "dólar" })
        assertEquals(2, pieces.count { source.substring(it.first, it.second) == "{" && it.third.trim() == "abre llave" })
        val speech = ManualEnglishSpeech.protect(prepared.text) { MultilingualMathSpeech.text(it) }
        val trace = requireNotNull(MixedSpanishG2P.traceWordsPrepared(speech))
        assertEquals(MixedSpanishG2P.phonemizePrepared(speech, emptyMap()), trace.phonemes)
        assertEquals(NarrationSpokenWords.lexemes(speech).map { it.canonical }, trace.words.map { it.word })
        val segments = NarrationSegments.sentences(listOf(speech)).withIndex().associate { it.index to it.value }
        val mappings = NarrationWordMappings.forBlock(0, source, segments)
        assertEquals(segments.keys, mappings.keys)
        assertEquals(source.indices.toSet(), mappings.values.flatMap { value ->
            value.displayed.flatMap { it.start until it.endExclusive }
        }.toSet())
        assertTrue(mappings.values.flatMap { it.displayed }.none { it.start == 0 && it.endExclusive == source.length })
    }

    @Test fun allFourPreviouslyRejectedRealCmakeSentencesNowHaveContinuousStrictSpanishPhones() {
        val resources = File(System.getProperty("translation.resources") ?: "src/main/res")
        val file = requireNotNull(resources.absoluteFile.parentFile).resolve(
            "assets/content/es/programming/programming_practice_tools/comprobaciones_repetibles/lesson_datos_y_listas_de_objetivos.json")
        val lesson = JSONObject(file.readText())
        val unchanged = lesson.toString()
        val checked = mutableListOf<String>()
        for (page in 0 until lesson.getJSONArray("pages").length()) {
            val texts = LessonNarrationText.page(lesson, page, NarrationLanguage.SPANISH)
            val kokoro = LessonNarrationText.page(lesson, page, NarrationLanguage.SPANISH, forKokoro = true)
            assertEquals(texts, kokoro.map(ManualEnglishSpeech::strip))
            for (sentence in NarrationSegments.sentences(kokoro)) {
                assertTrue(MixedSpanishG2P.phonemizePrepared(sentence, emptyMap()).isNotBlank())
                if ("dólar" in ManualEnglishSpeech.strip(sentence)) checked += sentence
            }
        }
        val corrected = checked.map(ManualEnglishSpeech::strip)
        for (expected in listOf(
            "dólar abre llave cliente guion bajo principal cierra llave obtiene el valor",
            "dólar abre llave clientes cierra llave sin comillas aporta",
            "dólar abre llave clientes cierra llave transmite aquí",
            "list abre paréntesis APPEND dólar abre llave clientes cierra llave comprobar guion bajo cajas"
        )) assertTrue("A formerly rejected sentence was not checked: $expected", corrected.any { expected in it })
        val comparison = corrected.single { "qué distingue" in it }
        assertEquals("Two expansions must not be mistaken for a paired math delimiter", 2,
            Regex("dólar abre llave clientes cierra llave").findAll(comparison).count())
        assertEquals(unchanged, lesson.toString())
    }

    @Test fun correctedExpansionsChangeOnlyTheirExistingSentenceAudioIdentity() {
        val source = "Usa $expansion."
        val corrected = SpanishTechnicalSpeech.prepareForKokoro(source).text
        val formerlyRejected = ManualEnglishSpeech.PREPARED + "Usa \$abre llave clientes cierra llave."
        assertNotEquals(KokoroNarrationCache.key("es", "ef_dora", formerlyRejected),
            KokoroNarrationCache.key("es", "ef_dora", corrected))
        val ordinary = "Esta oración conserva su pronunciación."
        assertEquals(KokoroNarrationCache.key("es", "ef_dora", ordinary),
            KokoroNarrationCache.key("es", "ef_dora", SpanishTechnicalSpeech.prepareForKokoro(ordinary).text))
        assertEquals(KokoroNarrationCache.key("es", "ef_dora", corrected),
            KokoroNarrationCache.key("es", "ef_dora", SpanishTechnicalSpeech.prepareForKokoro(corrected).text))
    }
}
