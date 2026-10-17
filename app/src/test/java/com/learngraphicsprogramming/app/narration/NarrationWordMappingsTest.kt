package com.learngraphicsprogramming.app.narration

import java.io.File
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class NarrationWordMappingsTest {
    @Test fun doubleSlashAndRemainingLessonWordsKeepSeparateSourceRanges() {
        val source = "Fuera de un literal, // comienza un comentario que llega hasta el final de esa línea."
        val mapped = NarrationWordMappings.forBlock(1, source, mapOf(0 to source)).getValue(0)
        assertEquals("Los caracteres originales se conservan una sola vez",
            source.indices.toList(), mapped.displayed.flatMap { it.start until it.endExclusive })
        assertEquals(NarrationSpokenWords.lexemes(source).size, mapped.displayed.size)
        val slash = source.indexOf("//")
        assertTrue("La marca de comentario conserva su posición",
            mapped.displayed.any { it.start <= slash && it.endExclusive >= slash + 2 })
        val next = source.indexOf("comienza")
        assertTrue("La palabra siguiente no queda agrupada con toda la frase",
            mapped.displayed.any { it.start <= next && it.endExclusive == next + "comienza".length + 1 })
    }

    @Test fun degreeScaleGlyphIsPaintedWithItsRealSpokenScaleWord() {
        val source = "Una temperatura de -3 °C describe un estado. Un aumento de 5 °C describe un cambio."
        val mapping = NarrationWordMappings.forBlock(1, source, mapOf(0 to source)).getValue(0)
        val visible = mapping.displayed.map { source.substring(it.start, it.endExclusive) }
        assertTrue(visible.contains("°C "))
        assertEquals(2, visible.count { it == "°C " })
        assertTrue(visible.any { it == "-3 " })
        assertTrue(visible.any { it == "5 " })
        assertEquals(source.indices.toList(), mapping.displayed.flatMap { it.start until it.endExclusive })
    }

    @Test fun everyPendingGlyphIncludingOperatorsAndPunctuationBelongsToAWord() {
        val source = "¿Lee C++ (ahora), después; «bien»."
        val mapping = NarrationWordMappings.forBlock(2, source, mapOf(4 to source)).getValue(4)
        assertEquals(listOf("¿Lee ", "C++ ", "(ahora), ", "después; ", "«bien»."),
            mapping.displayed.map { source.substring(it.start, it.endExclusive) })
        assertEquals(source.indices.toList(), mapping.displayed.flatMap { it.start until it.endExclusive })
        assertEquals(listOf("Lee", "C", "ahora", "después", "bien"),
            mapping.spoken.map { source.substring(it.start, it.endExclusive) })
    }

    @Test fun sentenceAndParagraphBoundariesKeepOneMonotonicCorrespondenceForRepeatedWords() {
        val visible = "Lee, lee.\n(Lee C++); lee."
        val parts = linkedMapOf(1 to "Lee, lee.", 3 to "(Lee C++); lee.")
        val mappings = NarrationWordMappings.forBlock(6, visible, parts)
        assertEquals(listOf(1, 3), mappings.keys.toList())
        assertEquals(visible.indices.toList(), mappings.values.flatMap { value ->
            value.displayed.flatMap { it.start until it.endExclusive }
        })
        assertEquals("(Lee ", mappings.getValue(3).displayed.first().let { visible.substring(it.start, it.endExclusive) })
    }

    @Test fun straightClosingQuotesRemainWithTheirSpokenWord() {
        val source = "Lee \"bien\", después."
        val mapping = NarrationWordMappings.forBlock(0, source, mapOf(0 to source)).getValue(0)
        assertEquals(listOf("Lee ", "\"bien\", ", "después."),
            mapping.displayed.map { source.substring(it.start, it.endExclusive) })
    }

    @Test fun changedLexemesFormulaExpansionsAndMissingWordsDoNotInventWordCorrespondence() {
        assertTrue(NarrationWordMappings.forBlock(0, "Lee la vida.", mapOf(0 to "Lee otra vida.")).isEmpty())
        assertTrue(NarrationWordMappings.forBlock(0, "x = 2", mapOf(0 to "equis es igual a dos")).isEmpty())
        assertTrue(NarrationWordMappings.forBlock(0, "a++", mapOf(0 to "a incremento")).isEmpty())
        assertTrue(NarrationWordMappings.forBlock(0, "Lee, lee.", mapOf(0 to "Lee.")).isEmpty())
    }

    @Test fun thePublishedAlgorithmPageKeepsAllCppAndSentenceGlyphsInItsExactMapping() {
        val resources = File(System.getProperty("translation.resources") ?: "src/main/res")
        val lessonFile = requireNotNull(resources.absoluteFile.parentFile).resolve(
            "assets/content/es/programming/cpp_fundamentals/programas_y_ejecucion/lesson_problemas_instrucciones_y_resultados.json")
        val lesson = JSONObject(lessonFile.readText())
        val original = lesson.toString()
        val page = lesson.getJSONArray("pages").getJSONObject(3)
        assertEquals("Algoritmo, programa y prueba", page.getJSONArray("blocks").getJSONObject(0).getString("text"))
        val visible = page.getJSONArray("blocks").getJSONObject(1).getString("text")
        val script = LessonNarrationScript.page(lesson, 3, NarrationLanguage.SPANISH, forKokoro = true)
        val plan = requireNotNull(script.readingPlan(NarrationEngine.KOKORO))
        val parts = NarrationSegments.sentences(script.texts)
        val selected = plan.origins.indices.filter { plan.origins[it].blockIndex == 1 && !plan.origins[it].feedback }
            .associateWith { parts[it] }
        val mappings = NarrationWordMappings.forBlock(1, visible, selected)
        assertEquals(selected.keys, mappings.keys)
        assertEquals(visible.indices.toList(), mappings.values.flatMap { value ->
            value.displayed.flatMap { it.start until it.endExclusive }
        })
        val cppSegment = selected.keys.single { selected.getValue(it).contains("C++") }
        val cppIndex = Regex("\\p{L}[\\p{L}\\p{M}]*").findAll(selected.getValue(cppSegment)).indexOfFirst { it.value == "C" }
        val cppDisplay = mappings.getValue(cppSegment).displayed[cppIndex]
        assertEquals("C++. ", visible.substring(cppDisplay.start, cppDisplay.endExclusive))
        assertEquals(original, lesson.toString())
    }
}
