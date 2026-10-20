package com.learngraphicsprogramming.app.narration

import com.learngraphicsprogramming.app.CppExerciseAnswers
import com.learngraphicsprogramming.app.CppExerciseFeedback
import com.learngraphicsprogramming.app.narration.phonemes.SpanishFluidG2P
import java.io.File
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

/** Authored assets, rather than the historical in-memory replacement explanations. */
class CurrentInlineFeedbackNarrationTest {
    @Test fun allNineAuthoredGuidesAndEntireExplanationsReachOnlyTheirExistingNarrationPaths() {
        for ((path, page, number) in LOCATIONS) {
            val source = source(path)
            val block = block(source, page, number)
            val before = source.toString()
            val guide = block.getString("reviewFeedback")
            val explanation = block.getString("explanation")
            assertTrue(path, guide.isNotBlank())
            assertTrue(path, explanation.length > 100)
            assertEquals(guide, CppExerciseFeedback.review(block, fallback = "legacy"))
            assertEquals(explanation, CppExerciseFeedback.successExplanation(block))
            val isolated = page(block)
            val script = LessonNarrationScript.page(isolated, 0, NarrationLanguage.SPANISH, true)
            val gate = script.gates.single()
            val expected = LessonNarrationText.page(page(JSONObject().put("type", "paragraph").put("text", explanation)),
                0, NarrationLanguage.SPANISH, true)
            assertEquals("The complete authored explanation survives front-end preparation: $path", expected,
                script.texts.subList(gate.start + 1, gate.endExclusive))
            assertTrue("No success text occurs before its answer gate: $path", script.origins.take(gate.start).none { it.feedback })
            assertEquals("Only success parts own feedback provenance: $path",
                (gate.start until gate.endExclusive).toList(), script.origins.indices.filter { script.origins[it].feedback })
            assertFalse("Wrong guidance is never a page/success sentence: $path", script.texts.containsAll(
                IncorrectFeedbackNarration.prepare(listOf(guide), NarrationLanguage.SPANISH, true)))
            assertEquals(before, source.toString())
        }
    }

    @Test fun currentGuideAndSuccessWordsRetainExactFrontendPhonemeOwnership() {
        val missing = mutableListOf<String>()
        for ((path, page, number) in LOCATIONS) {
            val block = block(source(path), page, number)
            for (field in listOf("reviewFeedback", "explanation")) {
                val texts = IncorrectFeedbackNarration.prepare(listOf(block.getString(field)), NarrationLanguage.SPANISH, true)
                val parts = NarrationSegments.sentences(texts)
                assertTrue(path, parts.isNotEmpty())
                for (part in parts) {
                    val trace = MixedSpanishG2P.traceWordsPrepared(part)
                    if (trace == null) {
                        missing += "$path/$field: $part\nnormalized: ${SpanishFluidG2P.normalizeText(ManualEnglishSpeech.strip(part))}"
                        continue
                    }
                    assertEquals("$path/$field", MixedSpanishG2P.phonemizePrepared(part, emptyMap()), trace.phonemes)
                    assertEquals("$path/$field", NarrationSpokenWords.lexemes(part).map { it.canonical }, trace.words.map { it.word })
                    assertTrue("$path/$field", trace.words.all { it.start >= 0 && it.endExclusive <= trace.phonemes.length &&
                        it.start < it.endExclusive && trace.phonemes.substring(it.start, it.endExclusive).isNotBlank() })
                    assertTrue("$path/$field", trace.words.zipWithNext().all { (a, b) -> a.endExclusive <= b.start })
                }
            }
        }
        assertTrue("All current spoken fragments must retain exact phone ownership:\n${missing.joinToString("\n")}", missing.isEmpty())
    }

    @Test fun everyCurrentSuccessSentenceRequiresItsOwnNaturalCompletionBeforeTheNextOne() {
        for ((path, page, number) in LOCATIONS) {
            val authored = block(source(path), page, number)
            val lesson = page(authored)
            lesson.getJSONArray("pages").getJSONObject(0).getJSONArray("blocks")
                .put(JSONObject().put("type", "paragraph").put("text", "El texto posterior permanece protegido."))
            val script = LessonNarrationScript.page(lesson, 0, NarrationLanguage.SPANISH, true)
            val parts = NarrationSegments.sentences(script.texts)
            val boundary = NarrationScriptSegments.gates(script.texts, NarrationEngine.KOKORO, script.gates).single()
            val gate = NarrationExerciseGate(parts.size, listOf(boundary))
            assertFalse(path, gate.canPlay(boundary.start))
            gate.updateCorrect(setOf(0))
            assertEquals(NarrationGatePosition(boundary.start), gate.prioritizeCorrect(0))
            assertEquals(0, gate.heardThrough)
            for (index in boundary.start until boundary.endExclusive) {
                assertTrue("$path/$index", gate.canPlay(index))
                assertFalse("$path/$index", gate.canPlay(index + 1))
                gate.completedNaturally(index)
                assertEquals(index + 1, gate.heardThrough)
            }
            assertTrue(path, gate.canPlay(parts.lastIndex))
            assertNull(gate.boundary())
        }
    }

    @Test fun authoredFeedbackNeverChangesHoleCountCanonicalAnswersOrIndependentAlternatives() {
        for ((path, page, number) in LOCATIONS) {
            val block = block(source(path), page, number)
            val before = block.toString()
            val answers = strings(block.getJSONArray("answers"))
            assertTrue(path, CppExerciseAnswers.inlineMatches(block, answers))
            assertFalse(path, CppExerciseAnswers.inlineMatches(block, answers.toMutableList().apply { this[0] = "incorrecto_qa" }))
            block.optJSONArray("answerAlternatives")?.let { groups ->
                for (index in 0 until groups.length()) for (candidate in strings(groups.getJSONArray(index))) {
                    assertTrue(path, CppExerciseAnswers.inlineMatches(block, answers.toMutableList().apply { this[index] = candidate }))
                }
            }
            assertEquals(before, block.toString())
        }
    }

    private fun source(path: String): JSONObject {
        val main = requireNotNull(File(System.getProperty("translation.resources") ?: "src/main/res").absoluteFile.parentFile)
        return JSONObject(main.resolve("assets/content/es/programming/cpp_fundamentals/$path").readText())
    }
    private fun block(source: JSONObject, page: Int, block: Int) = source.getJSONArray("pages").getJSONObject(page - 1)
        .getJSONArray("blocks").getJSONObject(block - 1).also { assertEquals("inline_fill_code", it.getString("type")) }
    private fun strings(array: JSONArray) = List(array.length()) { array.getString(it) }
    private fun page(block: JSONObject) = JSONObject().put("pages", JSONArray().put(JSONObject().put("blocks", JSONArray().put(block))))

    companion object {
        val LOCATIONS = listOf(
            Triple("objetos_y_estado/lesson_enteros_y_aproximaciones.json", 8, 7),
            Triple("objetos_y_estado/lesson_enteros_y_aproximaciones.json", 4, 4),
            Triple("objetos_y_estado/lesson_declarar_e_inicializar.json", 3, 5),
            Triple("objetos_y_estado/lesson_estado_asignacion_y_copia.json", 6, 5),
            Triple("objetos_y_estado/lesson_nombres_con_intencion.json", 4, 5),
            Triple("objetos_y_estado/lesson_conversiones_y_deduccion.json", 4, 6),
            Triple("objetos_y_estado/lesson_cadenas_y_texto.json", 4, 5),
            Triple("objetos_y_estado/lesson_caracteres_y_valores_logicos.json", 3, 4),
            Triple("programas_y_ejecucion/lesson_texto_secuencia_y_formato.json", 5, 2))
    }
}
