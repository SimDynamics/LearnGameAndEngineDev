package com.learngraphicsprogramming.app

import com.learngraphicsprogramming.app.narration.IncorrectFeedbackNarration
import com.learngraphicsprogramming.app.narration.LessonNarrationScript
import com.learngraphicsprogramming.app.narration.LessonNarrationText
import com.learngraphicsprogramming.app.narration.NarrationLanguage
import java.io.File
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class CppInlineFeedbackContractTest {
    @Test fun optionalInlineGuidanceNeverReadsTheAnswersOrTheSuccessExplanation() {
        val block = JSONObject().put("type", "inline_fill_code").put("reviewFeedback", REVIEW)
            .put("answers", false).put("answerAlternatives", JSONObject()).put("explanation", "SUCCESS ONLY")
        val before = block.toString()
        assertEquals(REVIEW, CppExerciseFeedback.review(block, fallback = "legacy"))
        assertEquals("SUCCESS ONLY", CppExerciseFeedback.successExplanation(block))
        assertEquals(before, block.toString())
        block.remove("reviewFeedback")
        assertEquals("legacy", CppExerciseFeedback.review(block, fallback = "legacy"))
        block.remove("explanation")
        assertEquals("", CppExerciseFeedback.successExplanation(block))
    }

    @Test fun malformedInlineGuidanceAndExplanationsAreTechnicalErrors() {
        for (value in listOf<Any>(JSONObject.NULL, "", " \t\n", JSONArray(), JSONObject(), 1, false)) {
            val block = JSONObject().put("type", "inline_fill_code")
            invalid { CppExerciseFeedback.review(block.put("reviewFeedback", value), fallback = "legacy") }
            invalid { CppExerciseFeedback.successExplanation(block.put("explanation", value)) }
        }
    }

    @Test fun allNinePublishedActivitiesKeepTheirCanonicalAnswersAndIndependentAlternatives() {
        var alternatives = 0
        for ((path, page, blockNumber) in LOCATIONS) {
            val source = lesson(path)
            val original = historicalBlockAt(source, page, blockNumber)
            assertEquals("inline_fill_code", original.getString("type"))
            val before = original.toString()
            val enhanced = JSONObject(before).put("reviewFeedback", REVIEW).put("explanation", EXPLANATION)
            val answers = strings(original.getJSONArray("answers"))
            assertTrue(path, CppExerciseAnswers.inlineMatches(original, answers))
            assertTrue(path, CppExerciseAnswers.inlineMatches(enhanced, answers))
            val rejected = answers.toMutableList().apply { this[0] = "incorrecto_qa" }
            assertFalse(path, CppExerciseAnswers.inlineMatches(enhanced, rejected))
            assertFalse(path, CppExerciseAnswers.inlineMatches(original, rejected))
            original.optJSONArray("answerAlternatives")?.let { groups ->
                for (index in 0 until groups.length()) for (candidate in strings(groups.getJSONArray(index))) {
                    val accepted = answers.toMutableList().apply { this[index] = candidate }
                    assertTrue("$path $accepted", CppExerciseAnswers.inlineMatches(enhanced, accepted))
                    alternatives++
                }
            }
            assertEquals(before, original.toString())
            // Published lessons may already contain editorial feedback. Restore
            // those exact values before checking that every contract field stayed intact.
            for (field in listOf("reviewFeedback", "explanation")) {
                if (original.has(field)) enhanced.put(field, original.get(field))
                else enhanced.remove(field)
            }
            assertEquals("Feedback cannot alter any original contract field", before, enhanced.toString())
        }
        assertEquals(9, LOCATIONS.size)
        assertTrue("The existing conversion's four spellings remain covered", alternatives >= 4)
    }

    @Test fun reviewedInlineDraftsRestoreOnlyTheSameExactHoleValuesWithoutReevaluation() {
        for (values in listOf(listOf("int", "longitud_metros", "partes"), listOf("\"Sector \"", "\"Oeste\""))) {
            val answer = JSONArray(values)
            val state = CppExerciseFeedback.recordIncorrect(JSONObject().put("answers", answer).put("correct", false), answer)
            val restored = JSONObject(state.toString())
            assertEquals(answer.toString(), restored.getJSONArray("answers").toString())
            assertTrue(CppExerciseFeedback.wasReviewed(restored, JSONArray(values)))
            assertFalse(CppExerciseFeedback.wasReviewed(restored, JSONArray(values.reversed())))
            val changed = values.toMutableList().apply { this[0] += " " }
            assertFalse(CppExerciseFeedback.wasReviewed(restored, JSONArray(changed)))
            restored.put("correct", true)
            assertFalse(CppExerciseFeedback.wasReviewed(restored, answer))
        }
        val oldDraft = JSONObject().put("answers", JSONArray(listOf("draft"))).put("correct", false)
        assertFalse(CppExerciseFeedback.wasReviewed(oldDraft, oldDraft.getJSONArray("answers")))
    }

    @Test fun inlineExplanationUsesOnlyTheExistingSuccessGateAndTheWholeReviewUsesTheExistingSpeechFrontend() {
        val block = historicalBlockAt(lesson(LOCATIONS.first().first), 8, 7)
        val authored = JSONObject(block.toString()).put("reviewFeedback", REVIEW).put("explanation", EXPLANATION)
        for (language in NarrationLanguage.entries) for (kokoro in listOf(false, true)) {
            val page = page(authored)
            val script = LessonNarrationScript.page(page, 0, language, kokoro)
            val gate = script.gates.single()
            val renderedExplanation = LessonNarrationText.page(page(JSONObject().put("type", "paragraph").put("text", EXPLANATION)),
                0, language, kokoro)
            assertEquals(renderedExplanation, script.texts.subList(gate.start + 1, gate.endExclusive))
            assertFalse(script.texts.take(gate.start).joinToString(" ").contains(EXPLANATION))
            assertFalse(script.texts.any { it.contains(REVIEW) })
            assertTrue(IncorrectFeedbackNarration.prepare(listOf(REVIEW), language, kokoro).isNotEmpty())
        }
    }

    @Test fun exactOutputPreservesVoluntaryAccentsBoundaryWhitespaceAndMultilineAnswers() {
        val block = historicalBlockAt(lesson("programas_y_ejecucion/lesson_fuente_compilacion_y_diagnostico.json"), 4, 6)
        assertEquals("prediccion_revision_inicial", block.getString("id"))
        assertEquals("exact", block.getString("outputComparison"))
        assertEquals(listOf("Revision inicial"), strings(block.getJSONArray("answers")))
        assertTrue(CppExerciseAnswers.outputMatches(block, "Revision inicial"))
        for (wrong in listOf("Revision inicial ", "Revisión inicial", "revision inicial", "Revision\tinicial")) {
            assertEquals(wrong, CppExerciseAnswers.normalizeOutput(block, wrong))
            assertFalse(CppExerciseAnswers.outputMatches(block, wrong))
        }
        assertTrue(CppExerciseAnswers.usesSingleLineAnswer(block))
        val multi = JSONObject(block.toString()).put("answers", JSONArray(listOf(" A\nB \t")))
        assertFalse(CppExerciseAnswers.usesSingleLineAnswer(multi))
        assertTrue(CppExerciseAnswers.outputMatches(multi, " A\nB \t"))
        assertFalse(CppExerciseAnswers.outputMatches(multi, "A\nB"))
    }

    private fun lesson(relative: String): JSONObject {
        val main = File(System.getProperty("translation.resources") ?: "src/main/res").absoluteFile.parentFile!!
        return JSONObject(main.resolve("assets/content/es/programming/cpp_fundamentals/$relative").readText())
    }
    private fun strings(array: JSONArray) = List(array.length()) { array.getString(it) }
    private fun historicalBlockAt(lesson: JSONObject, page: Int, block: Int): JSONObject {
        val historical = LessonStatePosition(page - 1, block - 1)
        val layout = LessonStateLayoutJson.read(lesson)
        val position = layout?.let { checked -> checked.current.flatMapIndexed { p, current -> current.blocks.indices.map { b ->
            LessonStatePosition(p, b)
        } }.single { checked.storagePosition(it.page, it.block) == historical } } ?: historical
        return lesson.getJSONArray("pages").getJSONObject(position.page).getJSONArray("blocks").getJSONObject(position.block)
    }
    private fun page(block: JSONObject) = JSONObject().put("pages", JSONArray().put(JSONObject().put("blocks", JSONArray().put(block))))
    private fun invalid(action: () -> Unit) {
        try { action(); fail("Malformed feedback must not be silently accepted") }
        catch (_: IllegalArgumentException) { }
    }
    companion object {
        private const val REVIEW = "Revisa los tipos de los operandos antes de considerar el destino del cociente."
        private const val EXPLANATION = "La división conserva la media unidad porque el tipo elegido actúa antes de guardar el resultado."
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
