package com.learngraphicsprogramming.app.math

import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import java.security.MessageDigest

class MathSelectedOptionFeedbackTest {
    @Test fun incorrectSingleChoiceReceivesOnlyItsOwnReview() {
        val response = MathPuzzleParser.parse(block()).responses.single()
        assertEquals(listOf("Reconsidera la unidad de la medición."),
            MathPuzzleFeedback.selectedReviews(response, MathAnswer.Choice(setOf("wrong")), "safe fallback"))
    }

    @Test fun aMultipleSelectionDoesNotClassifyCorrectMembersOrRevealMissingMembers() {
        val raw = block(multi = true)
        val response = MathPuzzleParser.parse(raw).responses.single()
        val selection = MathAnswer.Choice(setOf("right", "wrong"))
        assertFalse(MathValidators.validate(response.validator, selection).correct)
        assertEquals(listOf("Comprueba qué cantidad se está comparando.", "Reconsidera la unidad de la medición."),
            MathPuzzleFeedback.selectedReviews(response, selection, "safe fallback"))
        assertFalse(MathPuzzleFeedback.selectedReviews(response, selection, "safe fallback").joinToString().contains("ausente"))
    }

    @Test fun theReviewIsIndependentOfTheExpectedAnswers() {
        val original = block(multi = true)
        val changed = JSONObject(original.toString())
        changed.getJSONArray("responses").getJSONObject(0).getJSONObject("validator")
            .put("expected", JSONArray().put("wrong"))
        val answer = MathAnswer.Choice(setOf("right", "wrong"))
        assertEquals(MathPuzzleFeedback.selectedReviews(MathPuzzleParser.parse(original).responses.single(), answer, "safe"),
            MathPuzzleFeedback.selectedReviews(MathPuzzleParser.parse(changed).responses.single(), answer, "safe"))
    }

    @Test fun selectedLegacyOptionsUseAConceptFallbackWithoutTheGlobalExplanation() {
        val raw = block()
        raw.getJSONArray("responses").getJSONObject(0).getJSONArray("options").getJSONObject(1).remove("reviewFeedback")
        val response = MathPuzzleParser.parse(raw).responses.single()
        val result = MathPuzzleFeedback.selectedReviews(response, MathAnswer.Choice(setOf("wrong")), "Reconsidera el significado.")
        assertEquals(listOf("Reconsidera el significado."), result)
        assertFalse(result.joinToString().contains("GLOBAL REVEALS"))
    }

    @Test fun noSelectionOrAnUnknownIdCannotExposeAnUnselectedOptionReview() {
        val response = MathPuzzleParser.parse(block()).responses.single()
        for (answer in listOf(MathAnswer.Choice(emptySet()), MathAnswer.Choice(setOf("unknown")))) {
            assertEquals(listOf("safe"), MathPuzzleFeedback.selectedReviews(response, answer, "safe"))
        }
    }

    @Test fun anUnchangedLegacyBlockRetainsItsExactPreviousFingerprint() {
        val legacy = legacyBlock()
        assertEquals(legacyDigest(legacy), MathPuzzleState.fingerprint(legacy))
        assertSame(legacy, MathPuzzleState.withoutReviewFeedback(legacy))
    }

    @Test fun addingOrRevisingReviewsRetainsTheLegacyFingerprintWithoutMutatingContent() {
        val legacy = legacyBlock()
        val enhanced = block()
        val before = enhanced.toString()
        assertEquals(legacyDigest(legacy), MathPuzzleState.fingerprint(enhanced))
        assertEquals(before, enhanced.toString())
        enhanced.getJSONArray("responses").getJSONObject(0).getJSONArray("options").getJSONObject(1)
            .put("reviewFeedback", "Una revisión más precisa del mismo concepto.")
        assertEquals(legacyDigest(legacy), MathPuzzleState.fingerprint(enhanced))
    }

    @Test fun standaloneChoiceAlsoPreservesItsLegacyFingerprint() {
        val legacy = legacyBlock().getJSONArray("responses").getJSONObject(0)
            .put("prompt", "Compara las cantidades.")
        val enhanced = JSONObject(legacy.toString())
        enhanced.getJSONArray("options").getJSONObject(1).put("reviewFeedback", "Reconsidera las unidades.")
        assertEquals(legacyDigest(legacy), MathPuzzleState.fingerprint(enhanced))
        assertEquals("Reconsidera las unidades.", MathPuzzleFeedback.selectedReviews(
            MathPuzzleParser.parse(enhanced).responses.single(), MathAnswer.Choice(setOf("wrong")), "safe").single())
    }

    @Test fun onlyChoiceOptionReviewsAreExcludedFromTheFingerprint() {
        val original = block()
        val changed = JSONObject(original.toString()).put("reviewFeedback", "top-level contract field")
        assertNotEquals(MathPuzzleState.fingerprint(original), MathPuzzleState.fingerprint(changed))
        val representation = JSONObject(original.toString()).put("representations", JSONArray().put(
            JSONObject().put("type", "math_text").put("options", JSONArray().put(
                JSONObject().put("id", "diagram").put("reviewFeedback", "representation content")))))
        assertNotEquals(MathPuzzleState.fingerprint(original), MathPuzzleState.fingerprint(representation))
    }

    @Test fun oldCorrectProgressRestoresAndStillRevalidatesAgainstTheUnchangedAnswers() {
        val legacy = legacyBlock()
        val state = JSONObject().put("correct", true).put("contentFingerprint", legacyDigest(legacy))
            .put("responses", JSONObject().put("contar_o_medir__eleccion", MathAnswerJson.encode(MathAnswer.Choice(setOf("right")))))
        assertTrue(MathPuzzleState.isCurrentCorrect(block(), state))
        val forged = JSONObject(state.toString())
        forged.getJSONObject("responses").put("contar_o_medir__eleccion", MathAnswerJson.encode(MathAnswer.Choice(setOf("wrong"))))
        assertFalse(MathPuzzleState.isCurrentCorrect(block(), forged))
    }

    @Test fun aChangedValidatorStillInvalidatesOldProgress() {
        val legacy = legacyBlock()
        val state = JSONObject().put("correct", true).put("contentFingerprint", legacyDigest(legacy))
            .put("responses", JSONObject().put("contar_o_medir__eleccion", MathAnswerJson.encode(MathAnswer.Choice(setOf("right")))))
        val changed = block()
        changed.getJSONArray("responses").getJSONObject(0).getJSONObject("validator").put("expected", JSONArray().put("wrong"))
        assertNotEquals(legacyDigest(legacy), MathPuzzleState.fingerprint(changed))
        assertFalse(MathPuzzleState.isCurrentCorrect(changed, state))
    }

    @Test fun promptLabelsAndLegacyFeedbackRemainPartOfTheSavedContentContract() {
        val original = block()
        for (changed in listOf(JSONObject(original.toString()).put("prompt", "Otro enunciado."),
            JSONObject(original.toString()).apply { getJSONObject("feedback").put("incorrect", "Una explicación distinta.") },
            JSONObject(original.toString()).apply { getJSONArray("responses").getJSONObject(0).getJSONArray("options")
                .getJSONObject(1).put("label", "Otro inciso.") })) {
            assertNotEquals(MathPuzzleState.fingerprint(original), MathPuzzleState.fingerprint(changed))
        }
    }

    @Test fun malformedAuthoredReviewsAreRejectedWithoutChangingLegacyAcceptance() {
        MathPuzzleParser.parse(legacyBlock())
        for (invalid in listOf<Any>(JSONObject.NULL, 7, true, JSONObject(), JSONArray(), "", "  ")) {
            val malformed = block()
            malformed.getJSONArray("responses").getJSONObject(0).getJSONArray("options").getJSONObject(1)
                .put("reviewFeedback", invalid)
            try { MathPuzzleParser.parse(malformed); fail("Invalid reviewFeedback was accepted: $invalid") }
            catch (_: IllegalArgumentException) { }
        }
    }

    @Test fun responseDiagnosticsUseAnAuthoredLabelOrTheNumberedFallbackInsteadOfAnId() {
        val response = MathPuzzleParser.parse(block()).responses.single()
        assertEquals("Respuesta 1", MathPuzzleFeedback.responseLabel(response, "Respuesta 1"))
        response.config.put("label", response.id)
        assertEquals("Respuesta 1", MathPuzzleFeedback.responseLabel(response, "Respuesta 1"))
        response.config.put("label", "Comparación de unidades")
        assertEquals("Comparación de unidades", MathPuzzleFeedback.responseLabel(response, "Respuesta 1"))
    }

    @Test fun fieldDiagnosticsHideStableIdsAndPreserveUsefulHumanLabels() {
        val response = fieldResponse()
        assertEquals("Campo 1: Vuelve a intentarlo.", MathPuzzleFeedback.fieldMessage(response, "interno__primero",
            "interno__primero: Vuelve a intentarlo.") { "Campo $it" })
        assertEquals("Longitud: Vuelve a intentarlo.", MathPuzzleFeedback.fieldMessage(response, "interno__segundo",
            "Vuelve a intentarlo.") { "Campo $it" })
        assertEquals("Campo 1: Revisa el formato.", MathPuzzleFeedback.fieldMessage(response, "interno__desconocido",
            "interno__desconocido: Revisa el formato.") { "Campo $it" })
    }

    @Test fun aLegacyFieldPrefixIsTranslatedEvenWithoutStructuredFieldMetadata() {
        assertEquals("Longitud: Revisa el formato.", MathPuzzleFeedback.fieldMessage(fieldResponse(), null,
            "interno__segundo: Revisa el formato.") { "Campo $it" })
        assertEquals("Revisa las unidades.", MathPuzzleFeedback.fieldMessage(fieldResponse(), null,
            "Revisa las unidades.") { "Campo $it" })
    }

    @Test fun dropdownGuidanceIsLimitedToTheSelectedAlternativesAndUsesHumanFieldLabels() {
        val response = MathPuzzleParser.parse(dropdownBlock()).responses.single()
        val answer = MathAnswer.Fields(mapOf("quantity" to "3", "lower" to "included", "upper" to "included"))
        assertTrue(MathPuzzleFeedback.hasSelection(response))
        assertEquals(listOf("Campo 2: Examina la comparación del extremo que elegiste incluir.",
            "Extremo superior: Reconsidera qué significa admitir igualdad en esta comparación."),
            MathPuzzleFeedback.selectedReviews(response, answer, "safe") { "Campo $it" })
        assertFalse(MathPuzzleFeedback.selectedReviews(response, answer, "safe").joinToString().contains("excluir"))
    }

    @Test fun dropdownReviewsDoNotDependOnTheExpectedChoiceOrClassifyCorrectFields() {
        val original = dropdownBlock()
        val revised = JSONObject(original.toString())
        revised.getJSONArray("responses").getJSONObject(0).getJSONObject("validator").getJSONObject("fields")
            .getJSONObject("lower").put("expected", "included")
        val answer = MathAnswer.Fields(mapOf("quantity" to "2", "lower" to "included", "upper" to "included"))
        assertEquals(MathPuzzleFeedback.selectedReviews(MathPuzzleParser.parse(original).responses.single(), answer, "safe"),
            MathPuzzleFeedback.selectedReviews(MathPuzzleParser.parse(revised).responses.single(), answer, "safe"))
    }

    @Test fun dropdownAndNumericFieldsAreDistinguishedWithoutAnotherEvaluation() {
        val response = MathPuzzleParser.parse(dropdownBlock()).responses.single()
        assertTrue(MathPuzzleFeedback.isDropdownField(response, "lower"))
        assertFalse(MathPuzzleFeedback.isDropdownField(response, "quantity"))
        assertFalse(MathPuzzleFeedback.isDropdownField(response, "unknown"))
        val result = MathValidators.validate(response.validator,
            MathAnswer.Fields(mapOf("quantity" to "3", "lower" to "excluded", "upper" to "included")))
        assertFalse(result.correct)
        assertEquals("quantity", result.fieldId)
        assertFalse(MathPuzzleFeedback.isDropdownField(response, requireNotNull(result.fieldId)))
        assertEquals("Campo 1: Revisa la cantidad.", MathPuzzleFeedback.fieldMessage(response, result.fieldId,
            "quantity: Revisa la cantidad.") { "Campo $it" })
    }

    @Test fun aMissingDropdownSelectionAndLegacyOptionGetSafeGuidanceWithoutOtherOptions() {
        val raw = dropdownBlock()
        raw.getJSONArray("responses").getJSONObject(0).getJSONArray("fields").getJSONObject(1)
            .getJSONArray("options").getJSONObject(0).remove("reviewFeedback")
        val response = MathPuzzleParser.parse(raw).responses.single()
        assertEquals(listOf("Campo 2: safe"),
            MathPuzzleFeedback.selectedReviews(response, MathAnswer.Fields(mapOf("lower" to "included")), "safe") { "Campo $it" })
    }

    @Test fun unselectedDropdownFieldsReceiveNeitherAReviewNorAFieldLabel() {
        val response = MathPuzzleParser.parse(dropdownBlock()).responses.single()
        assertEquals(listOf("Campo 2: Examina la comparación del extremo que elegiste incluir."),
            MathPuzzleFeedback.selectedReviews(response, MathAnswer.Fields(mapOf("lower" to "included", "upper" to "")), "safe") { "Campo $it" })
        for (values in listOf(emptyMap(), mapOf("lower" to "", "upper" to ""), mapOf("lower" to "unknown"))) {
            assertEquals(listOf("safe"), MathPuzzleFeedback.selectedReviews(response, MathAnswer.Fields(values), "safe") { "Campo $it" })
        }
    }

    @Test fun dropdownReviewEditsKeepOldCorrectStatesAndStillRevalidateTheirAnswers() {
        val enhanced = dropdownBlock()
        val legacy = JSONObject(enhanced.toString())
        val fields = legacy.getJSONArray("responses").getJSONObject(0).getJSONArray("fields")
        for (index in 0 until fields.length()) fields.getJSONObject(index).optJSONArray("options")?.let { options ->
            for (option in 0 until options.length()) options.getJSONObject(option).remove("reviewFeedback")
        }
        val expected = MathAnswer.Fields(mapOf("quantity" to "2", "lower" to "excluded", "upper" to "included"))
        val state = JSONObject().put("correct", true).put("contentFingerprint", legacyDigest(legacy))
            .put("responses", JSONObject().put("bounds__fields", MathAnswerJson.encode(expected)))
        assertEquals(legacyDigest(legacy), MathPuzzleState.fingerprint(enhanced))
        assertTrue(MathPuzzleState.isCurrentCorrect(enhanced, state))
        state.getJSONObject("responses").put("bounds__fields", MathAnswerJson.encode(
            MathAnswer.Fields(expected.values + ("lower" to "included"))))
        assertFalse(MathPuzzleState.isCurrentCorrect(enhanced, state))
    }

    @Test fun standaloneDropdownFieldsAlsoRetainTheirPreviousIdentity() {
        val enhanced = dropdownBlock().getJSONArray("responses").getJSONObject(0).put("prompt", "Revisa los extremos.")
        val legacy = JSONObject(enhanced.toString())
        val fields = legacy.getJSONArray("fields")
        for (index in 0 until fields.length()) fields.getJSONObject(index).optJSONArray("options")?.let { options ->
            for (option in 0 until options.length()) options.getJSONObject(option).remove("reviewFeedback")
        }
        assertEquals(legacyDigest(legacy), MathPuzzleState.fingerprint(enhanced))
        MathPuzzleParser.parse(enhanced)
    }

    @Test fun aReviewKeyOnANonDropdownFieldRemainsPartOfTheIdentity() {
        val original = dropdownBlock()
        val changed = JSONObject(original.toString())
        changed.getJSONArray("responses").getJSONObject(0).getJSONArray("fields").getJSONObject(0)
            .put("options", JSONArray().put(JSONObject().put("id", "nonselection").put("reviewFeedback", "contract data")))
        assertNotEquals(MathPuzzleState.fingerprint(original), MathPuzzleState.fingerprint(changed))
        assertTrue(MathPuzzleState.withoutReviewFeedback(changed).getJSONArray("responses").getJSONObject(0)
            .getJSONArray("fields").getJSONObject(0).getJSONArray("options").getJSONObject(0).has("reviewFeedback"))
    }

    @Test fun malformedDropdownReviewsAreRejectedWithoutInvalidatingTheLegacyContract() {
        for (invalid in listOf<Any>(JSONObject.NULL, 12, false, JSONObject(), "", "  ")) {
            val malformed = dropdownBlock()
            malformed.getJSONArray("responses").getJSONObject(0).getJSONArray("fields").getJSONObject(1)
                .getJSONArray("options").getJSONObject(0).put("reviewFeedback", invalid)
            try { MathPuzzleParser.parse(malformed); fail("Invalid dropdown reviewFeedback was accepted") }
            catch (_: IllegalArgumentException) { }
        }
    }

    private fun dropdownBlock(): JSONObject = JSONObject().put("type", "math_puzzle").put("prompt", "Interpreta las cantidades y sus extremos.")
        .put("responses", JSONArray().put(JSONObject().put("id", "bounds__fields").put("type", "math_multi_input")
            .put("fields", JSONArray().put(JSONObject().put("id", "quantity").put("type", "numeric"))
                .put(JSONObject().put("id", "lower").put("type", "dropdown").put("options", JSONArray()
                    .put(JSONObject().put("id", "included").put("label", "Incluir el extremo")
                        .put("reviewFeedback", "Examina la comparación del extremo que elegiste incluir."))
                    .put(JSONObject().put("id", "excluded").put("label", "Excluir el extremo")
                        .put("reviewFeedback", "Examina la comparación del extremo que elegiste excluir."))))
                .put(JSONObject().put("id", "upper").put("type", "dropdown").put("label", "Extremo superior")
                    .put("options", JSONArray().put(JSONObject().put("id", "included").put("label", "Incluir")
                        .put("reviewFeedback", "Reconsidera qué significa admitir igualdad en esta comparación."))
                        .put(JSONObject().put("id", "excluded").put("label", "Excluir")
                            .put("reviewFeedback", "Reconsidera el extremo que has decidido excluir.")))))
            .put("validator", JSONObject().put("type", "fields").put("fields", JSONObject()
                .put("quantity", JSONObject().put("type", "numeric").put("expected", "2"))
                .put("lower", JSONObject().put("type", "exact_text").put("expected", "excluded"))
                .put("upper", JSONObject().put("type", "exact_text").put("expected", "included"))))))
        .put("feedback", JSONObject().put("correct", "Conclusión comprobada.").put("incorrect", "LEGACY reveals expected bounds."))

    private fun block(multi: Boolean = false): JSONObject = JSONObject()
        .put("type", "math_puzzle").put("schemaVersion", 1).put("id", "contar_o_medir")
        .put("prompt", "Compara las cantidades.").put("responses", JSONArray().put(JSONObject()
            .put("id", "contar_o_medir__eleccion").put("type", if (multi) "math_multiple_choice_multi" else "math_multiple_choice_single")
            .put("options", JSONArray().put(JSONObject().put("id", "right").put("label", "Unidades compatibles")
                .put("reviewFeedback", "Comprueba qué cantidad se está comparando."))
                .put(JSONObject().put("id", "wrong").put("label", "Unidades sin comparar")
                    .put("reviewFeedback", "Reconsidera la unidad de la medición."))
                .put(JSONObject().put("id", "missing").put("label", "Una cantidad adicional")
                    .put("reviewFeedback", "Este mensaje de un inciso ausente no debe aparecer.")))
            .put("validator", JSONObject().put("type", "choice").put("expected",
                JSONArray().put("right").apply { if (multi) put("missing") }))))
        .put("feedback", JSONObject().put("correct", "Conclusión completa.").put("incorrect", "GLOBAL REVEALS: right is correct; missing also correct."))

    private fun legacyBlock(): JSONObject = block().also {
        val options = it.getJSONArray("responses").getJSONObject(0).getJSONArray("options")
        for (index in 0 until options.length()) options.getJSONObject(index).remove("reviewFeedback")
    }
    private fun legacyDigest(block: JSONObject) = MessageDigest.getInstance("SHA-256")
        .digest(block.toString().toByteArray(Charsets.UTF_8)).joinToString("") { "%02x".format(it) }
    private fun fieldResponse() = MathResponseSpec("internal__answer", "math_multi_input", JSONObject()
        .put("fields", JSONArray().put(JSONObject().put("id", "interno__primero"))
            .put(JSONObject().put("id", "interno__segundo").put("label", "Longitud"))), JSONObject())
}
