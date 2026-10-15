package com.learngraphicsprogramming.app.narration

import com.learngraphicsprogramming.app.narration.phonemes.SpanishFluidG2P
import java.io.File
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class NarrationQuestionWordMappingsTest {
    @Test fun originalFourthPageQuestionKeepsItsOwnWordsSeparateFromOptionNarration() {
        val resources = File(System.getProperty("translation.resources") ?: "src/main/res")
        val source = requireNotNull(resources.absoluteFile.parentFile).resolve(
            "assets/content/es/programming/cpp_fundamentals/programas_y_ejecucion/lesson_problemas_instrucciones_y_resultados.json")
        val lesson = JSONObject(source.readText())
        val original = lesson.toString()
        val blocks = lesson.getJSONArray("pages").getJSONObject(3).getJSONArray("blocks")
        val blockIndex = (0 until blocks.length()).single {
            blocks.getJSONObject(it).optString("id") == "prueba_mensaje"
        }
        val question = blocks.getJSONObject(blockIndex).getString("question")
        assertEquals("El requisito es mostrar «Caja preparada». ¿Qué comprobación responde directamente a él?", question)
        val script = LessonNarrationScript.page(lesson, 3, NarrationLanguage.SPANISH, forKokoro = true)
        val plan = requireNotNull(script.readingPlan(NarrationEngine.KOKORO))
        val parts = NarrationSegments.sentences(script.texts)
        val allBlockSegments = plan.origins.indices.filter {
            plan.origins[it].blockIndex == blockIndex && !plan.origins[it].feedback
        }.associateWith { parts[it] }

        // The heading contains only the question; options are independent interactive views.
        assertTrue(allBlockSegments.size > 2)
        val mapping = NarrationWordMappings.forView(blockIndex, question, allBlockSegments)
        assertEquals(allBlockSegments.keys.take(2), mapping.keys.toList())
        assertEquals(question.indices.toList(), mapping.values.flatMap { item ->
            item.displayed.flatMap { it.start until it.endExclusive }
        })
        assertTrue(mapping.values.all { it.blockIndex == blockIndex })
        val second = mapping.values.last()
        val firstPendingWord = second.displayed.first()
        assertEquals("¿Qué ", question.substring(firstPendingWord.start, firstPendingWord.endExclusive))
        assertEquals("él?", second.displayed.last().let { question.substring(it.start, it.endExclusive) })
        val alignments = mapping.mapValues { (segment, words) ->
            val spoken = parts[segment]
            val trace = requireNotNull(SpanishFluidG2P.traceWords(
                spoken.removePrefix(ManualEnglishSpeech.PREPARED.toString())))
            assertEquals(words.spoken.size, trace.words.size)
            // Unit-only token-frame fixtures exercise provenance; device tests use ONNX timings.
            val durations = LongArray(trace.phonemes.length + 2) { 1L }
            requireNotNull(NarrationWordTimingBuilder.build(spoken, "a".repeat(64),
                durations.sum() * 600L, trace, durations))
        }
        val progress = LessonWordReadingProgress()
        val identity = "original-question"
        progress.bind(identity, mapping, replace = true)
        fun event(segment: Int, phase: NarrationPlaybackPhase, position: Long = 0L,
                  alignment: NarrationWordAlignment? = null) = NarrationPlaybackProgress(
            identity, 1L, segment, parts.size, phase, positionMs = position, alignment = alignment)
        val firstSegment = mapping.keys.first()
        val secondSegment = mapping.keys.last()
        progress.update(event(firstSegment, NarrationPlaybackPhase.PREPARED))
        alignments.forEach { (segment, alignment) ->
            progress.update(event(segment, NarrationPlaybackPhase.ALIGNMENT_AVAILABLE, alignment = alignment))
        }
        progress.update(event(firstSegment, NarrationPlaybackPhase.PLAYING))
        progress.update(event(firstSegment, NarrationPlaybackPhase.SEGMENT_COMPLETED))
        val secondAlignment = alignments.getValue(secondSegment)
        val firstWordStart = secondAlignment.words.first().startSample / 24L + 1L
        progress.update(event(secondSegment, NarrationPlaybackPhase.PLAYING))
        progress.update(event(secondSegment, NarrationPlaybackPhase.ADVANCED, firstWordStart))
        assertEquals("Starting a question reveals its current word, never the complete question",
            second.displayed.drop(1), progress.unread(identity).getValue(blockIndex))
        val secondWordStart = secondAlignment.words[1].startSample / 24L + 1L
        progress.update(event(secondSegment, NarrationPlaybackPhase.ADVANCED, secondWordStart))
        assertEquals(second.displayed.drop(2), progress.unread(identity).getValue(blockIndex))
        assertEquals(original, lesson.toString())
    }

    @Test fun aRegisteredHeadingMayCoverSeveralCompleteSentencesInsideOneExercise() {
        val question = "Una caja está preparada. ¿Qué comprobación corresponde?"
        val segments = linkedMapOf(2 to "Una caja está preparada.", 3 to "¿Qué comprobación corresponde?",
            4 to "Opción uno.", 5 to "Comprueba el mensaje.")
        val mapping = NarrationWordMappings.forView(7, question, segments)
        assertEquals(listOf(2, 3), mapping.keys.toList())
        assertEquals(question.indices.toList(), mapping.values.flatMap { item ->
            item.displayed.flatMap { it.start until it.endExclusive }
        })
        assertEquals("¿Qué ", mapping.getValue(3).displayed.first().let {
            question.substring(it.start, it.endExclusive)
        })
    }

    @Test fun ambiguousRepeatedViewsAndPartialSegmentsNeverInventCorrespondence() {
        assertTrue(NarrationWordMappings.forView(0, "Lee bien.", linkedMapOf(
            1 to "Lee bien.", 2 to "Opción uno.", 3 to "Lee bien.")).isEmpty())
        assertTrue(NarrationWordMappings.forView(0, "Lee bien.", mapOf(
            1 to "Lee bien y continúa.")).isEmpty())
        assertTrue(NarrationWordMappings.forView(0, "¿Qué valor?", mapOf(
            1 to "¿Qué resultado?")).isEmpty())
    }

    @Test fun preparationMarkersNeverMoveSpokenOffsetsOrRevealQuestionPunctuationEarly() {
        val visible = "¿Qué comprobación? ¡La caja!"
        val parts = linkedMapOf(4 to ManualEnglishSpeech.PREPARED + "¿Qué comprobación?",
            5 to ManualEnglishSpeech.PREPARED + "¡La caja!", 6 to "Opción uno.")
        val mapping = NarrationWordMappings.forView(1, visible, parts)
        assertEquals(listOf(4, 5), mapping.keys.toList())
        assertEquals(2, mapping.getValue(4).spoken.first().start)
        assertEquals(2, mapping.getValue(5).spoken.first().start)
        assertEquals(listOf("¿Qué ", "comprobación? ", "¡La ", "caja!"),
            mapping.values.flatMap { it.displayed }.map { visible.substring(it.start, it.endExclusive) })
        assertEquals(visible.indices.toList(), mapping.values.flatMap { item ->
            item.displayed.flatMap { it.start until it.endExclusive }
        })
    }

    @Test fun completeProseKeepsItsExistingMappingIncludingPunctuationOnlySegments() {
        val visible = "Lee. ... ¿Lee?"
        val segments = linkedMapOf(1 to "Lee.", 2 to "...", 3 to "¿Lee?")
        val original = NarrationWordMappings.forBlock(0, visible, segments)
        assertFalse(original.isEmpty())
        assertEquals(original, NarrationWordMappings.forView(0, visible, segments))
    }
}
