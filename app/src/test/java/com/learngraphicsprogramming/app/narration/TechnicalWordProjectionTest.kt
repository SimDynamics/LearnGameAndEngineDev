package com.learngraphicsprogramming.app.narration

import java.io.File
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class TechnicalWordProjectionTest {
    private fun lesson(): JSONObject {
        val resources = File(System.getProperty("translation.resources") ?: "src/main/res")
        return JSONObject(requireNotNull(resources.absoluteFile.parentFile).resolve(
            "assets/content/es/programming/cpp_fundamentals/programas_y_ejecucion/lesson_primer_programa_y_salida.json").readText())
    }

    @Test fun libraryParagraphRetainsEverySourceGlyphAndEveryRealSpokenAtom() {
        val lesson = lesson()
        val unchanged = lesson.toString()
        val source = lesson.getJSONArray("pages").getJSONObject(0).getJSONArray("blocks").getJSONObject(3).getString("text")
        val script = LessonNarrationScript.page(lesson, 0, NarrationLanguage.SPANISH, forKokoro = true)
        val parts = NarrationSegments.sentences(script.texts)
        val plan = requireNotNull(script.readingPlan(NarrationEngine.KOKORO))
        val selected = parts.indices.filter { plan.origins[it] == NarrationTextOrigin(3) }.associateWith { parts[it] }
        val mappings = NarrationWordMappings.forBlock(3, source, selected)
        assertEquals("One technical expansion cannot discard the other sentences", selected.keys, mappings.keys)
        assertEquals(source.indices.toSet(), mappings.values.flatMap { mapping ->
            mapping.displayed.flatMap { it.start until it.endExclusive }
        }.toSet())
        mappings.forEach { (index, mapping) ->
            assertEquals(NarrationSpokenWords.lexemes(parts[index]).map { ReadingTextRange(it.start, it.endExclusive) }, mapping.spoken)
            assertEquals(mapping.spoken.size, mapping.displayed.size)
        }
        val first = mappings.getValue(selected.keys.first())
        assertEquals("Una ", source.substring(first.displayed.first().start, first.displayed.first().endExclusive))
        assertTrue(first.displayed.last().endExclusive < source.indexOf("#include"))
        assertEquals(unchanged, lesson.toString())
    }

    @Test fun transformedSymbolsAndNumbersAreBoundedByTheirOwnPronouncedExpansion() {
        val source = "¿Lee #include <iostream>, std::cout y std::? { return 0; } Después, C++ termina."
        val prepared = SpanishTechnicalSpeech.prepareForKokoro(source).text
        val speech = ManualEnglishSpeech.protect(prepared) { MultilingualMathSpeech.text(it) }
        val parts = NarrationSegments.sentences(listOf(speech)).withIndex().associate { it.index to it.value }
        val mapping = NarrationWordMappings.forBlock(0, source, parts)
        assertEquals(parts.keys, mapping.keys)
        val ranges = mapping.values.flatMap { it.displayed }
        assertEquals(source.indices.toSet(), ranges.flatMap { it.start until it.endExclusive }.toSet())
        assertTrue("No single token can own the whole mixed paragraph", ranges.none { it.start == 0 && it.endExclusive == source.length })
        assertTrue(ranges.any { it.start <= source.indexOf('0') && it.endExclusive > source.indexOf('0') })
    }

    @Test fun aLaterExpandedSentenceNeverWhitesTheCurrentParagraphAndRewindRestoresItsTail() {
        val source = "Una biblioteca reúne recursos. La línea #include <iostream> permite usar std::cout."
        val speech = SpanishTechnicalSpeech.prepareForKokoro(source).text
        val parts = NarrationSegments.sentences(listOf(speech)).withIndex().associate { it.index to it.value }
        val mappings = NarrationWordMappings.forBlock(0, source, parts)
        assertEquals(parts.keys, mappings.keys)
        val model = LessonReadingProgressModel()
        model.bind("page", NarrationReadingPlan(List(parts.size) { NarrationTextOrigin(0) }, "technical"), wordMappings = mappings)
        fun event(phase: NarrationPlaybackPhase, segment: Int, position: Long = 0, alignment: NarrationWordAlignment? = null) =
            NarrationPlaybackProgress("page", 1, segment, parts.size, phase, positionMs = position, alignment = alignment)
        model.update(event(NarrationPlaybackPhase.PREPARED, 0))
        val first = mappings.getValue(0)
        val alignment = NarrationWordAlignment("a".repeat(64), first.spoken.size * 2400L,
            first.spoken.mapIndexed { index, range -> NarrationWordTiming(range.start, range.endExclusive, index * 2400L, (index + 1) * 2400L) })
        model.update(event(NarrationPlaybackPhase.ALIGNMENT_AVAILABLE, 0, alignment = alignment))
        model.update(event(NarrationPlaybackPhase.PLAYING, 0))
        val unread = requireNotNull(model.unreadWords("page")[0])
        assertFalse(unread.contains(first.displayed.first()))
        assertTrue(unread.contains(first.displayed.last()))
        assertTrue(unread.any { it.endExclusive == source.length })
        model.update(event(NarrationPlaybackPhase.SEGMENT_COMPLETED, 0))
        assertTrue(requireNotNull(model.unreadWords("page")[0]).all { it.start >= first.displayed.last().endExclusive })
        model.update(event(NarrationPlaybackPhase.POSITION_CHANGED, 0))
        assertTrue(requireNotNull(model.unreadWords("page")[0]).contains(first.displayed.first()))
    }
}
