package com.learngraphicsprogramming.app

import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class CppExerciseFeedbackTest {
    @Test fun aChoiceShowsOnlyTheSelectedReviewWithoutReadingAnySolutionFields() {
        val block = choice()
        assertEquals("Revisa el dominio del caso elegido.", CppExerciseFeedback.review(block, 1, "safe"))
        block.put("correctIndex", JSONObject()).put("answers", false).put("explanation", "SOLUTION")
        assertEquals("Revisa el dominio del caso elegido.", CppExerciseFeedback.review(block, 1, "safe"))
        assertFalse(CppExerciseFeedback.review(block, 1, "safe").contains("ausente"))
    }

    @Test fun absentOrUnknownSelectionCannotDisplayAnyOptionReview() {
        for (index in listOf(null, -1, 3, Int.MAX_VALUE)) {
            assertEquals("safe", CppExerciseFeedback.review(choice(), index, "safe"))
        }
    }

    @Test fun legacyBlocksKeepTheirSafeFallbackAndNeverReadExplanation() {
        for (type in listOf("multiple_choice", "fill_code", "output_prediction", "order_code")) {
            val legacy = JSONObject().put("type", type).put("explanation", "SOLUTION")
            assertEquals("Revisa tu razonamiento.", CppExerciseFeedback.review(legacy, 1, "Revisa tu razonamiento."))
        }
    }

    @Test fun freeResponsesAndOrderUseOnlyTheirAuthoredProcessReview() {
        for (type in listOf("fill_code", "output_prediction", "order_code")) {
            val block = JSONObject().put("type", type).put("reviewFeedback", "Comprueba el proceso.\nConserva el borrador.")
                .put("answers", "DO NOT READ").put("correctOrder", "DO NOT READ").put("explanation", "SOLUTION")
            assertEquals("Comprueba el proceso.\nConserva el borrador.", CppExerciseFeedback.review(block, fallback = "safe"))
        }
    }

    @Test fun malformedOptionalFeedbackIsATechnicalErrorEvenWhenItsBadEntryIsNotSelected() {
        val malformed = listOf<Any>(JSONObject.NULL, "text", JSONObject(), false, 1,
            JSONArray(), JSONArray(listOf("one")), JSONArray(listOf("a", "", "c")),
            JSONArray(listOf("a", "  \t", "c")), JSONArray().put("a").put(JSONObject.NULL).put("c"),
            JSONArray().put("a").put(2).put("c"))
        for (value in malformed) {
            assertInvalid { CppExerciseFeedback.review(choice().put("reviewFeedback", value), 0, "safe") }
            assertInvalid { CppExerciseFeedback.review(choice().put("reviewFeedback", value), fallback = "safe") }
        }
    }

    @Test fun malformedTextAndUnsupportedFamiliesAreNotSilentlyAccepted() {
        for (type in listOf("fill_code", "output_prediction", "order_code")) {
            for (value in listOf<Any>(JSONObject.NULL, "", "\n\t", JSONArray().put("review"), JSONObject(), 1, false)) {
                assertInvalid { CppExerciseFeedback.review(JSONObject().put("type", type).put("reviewFeedback", value), fallback = "safe") }
            }
        }
        for (type in listOf("inline_fill_code", "drag_fill_code", "cpp_compiler", "paragraph", "math_multiple_choice_single")) {
            assertInvalid { CppExerciseFeedback.review(JSONObject().put("type", type).put("reviewFeedback", "review"), fallback = "safe") }
        }
        assertInvalid { CppExerciseFeedback.review(choice().put("options", "wrong"), 0, "safe") }
        assertInvalid { CppExerciseFeedback.review(choice().put("options", JSONArray().put("a").put(2).put("c")), 0, "safe") }
    }

    @Test fun readingTheReviewDoesNotMutateContentOrTheCanonicalOptionStrings() {
        val block = choice().put("correctIndex", 2).put("id", "internal_only")
        val before = block.toString()
        CppExerciseFeedback.review(block, 1, "safe")
        assertEquals(before, block.toString())
        assertEquals(listOf("Primera", "Segunda", "Tercera"), (0 until 3).map { block.getJSONArray("options").getString(it) })
    }

    @Test fun oldDraftsAndOldCorrectProgressDoNotBecomeReviewedAttempts() {
        for (state in listOf(null, JSONObject(), JSONObject().put("answer", "draft"),
            JSONObject().put("answer", "draft").put("correct", false),
            JSONObject().put("answer", "draft").put("correct", true))) {
            assertFalse(CppExerciseFeedback.wasReviewed(state, "draft"))
        }
    }

    @Test fun markerSurvivesJsonRestorationAndMatchesTheExactTypedDraftOnly() {
        for (answer in listOf<Any>(1, " wrong\n ", JSONArray(listOf(2, 0, 1)))) {
            val state = JSONObject().put("correct", false).put("unrelated", "preserved")
            assertSame(state, CppExerciseFeedback.recordIncorrect(state, answer))
            assertEquals("preserved", state.getString("unrelated"))
            val restored = JSONObject(state.toString())
            assertTrue(CppExerciseFeedback.wasReviewed(restored, answer))
            assertFalse(CppExerciseFeedback.wasReviewed(restored, "another"))
            restored.put("correct", true)
            assertFalse(CppExerciseFeedback.wasReviewed(restored, answer))
        }
        val state = CppExerciseFeedback.recordIncorrect(JSONObject(), 1)
        assertFalse(CppExerciseFeedback.wasReviewed(state, "1"))
        val literal = CppExerciseFeedback.recordIncorrect(JSONObject(), " a\n ")
        for (changed in listOf("a", " a ", " a\n")) assertFalse(CppExerciseFeedback.wasReviewed(literal, changed))
    }

    @Test fun markerUsesTheExistingIdentityWithoutSavingASolutionOrDuplicatingTheDraft() {
        val answer = JSONArray(listOf(2, 0, 1))
        val state = CppExerciseFeedback.recordIncorrect(JSONObject().put("order", answer).put("correct", false), answer)
        assertEquals(ExerciseAnswerIdentity.of(answer), state.getString(CppExerciseFeedback.REVIEWED_ANSWER_IDENTITY))
        assertEquals(setOf("order", "correct", CppExerciseFeedback.REVIEWED_ANSWER_IDENTITY), state.keys().asSequence().toSet())
        assertFalse(CppExerciseFeedback.wasReviewed(state, JSONArray(listOf(0, 2, 1))))
        assertInvalid { CppExerciseFeedback.recordIncorrect(JSONObject().put("correct", true), answer) }
    }

    private fun choice() = JSONObject().put("type", "multiple_choice")
        .put("options", JSONArray(listOf("Primera", "Segunda", "Tercera")))
        .put("reviewFeedback", JSONArray(listOf("No debe aparecer para un inciso ausente.",
            "Revisa el dominio del caso elegido.", "Otra orientación ausente.")))

    private fun assertInvalid(action: () -> Unit) {
        try { action(); fail("Malformed optional feedback must be a technical error") }
        catch (_: IllegalArgumentException) { }
    }
}
