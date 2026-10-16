package com.learngraphicsprogramming.app.math

import com.learngraphicsprogramming.app.R

import org.junit.Assert.*
import org.junit.Test

class MathIncorrectGuidanceTest {
    @Test fun genericNumericWrongFeedbackUsesProcessGuidanceWithoutReadingRawDiagnostics() {
        val result = MathValidationResult(false, "HIDDEN_INTERNAL_DIAGNOSTIC",
            uiMessage = MathUiMessage(R.string.math_ui_incorrect))
        assertEquals("Check the relation against the given data.",
            MathPuzzleFeedback.processMessage(result, "Check the relation against the given data.") {
                error("Generic diagnostic IDs do not belong in spoken guidance")
            })
    }

    @Test fun successfulTechnicalAndSpecificDiagnosticsKeepTheirExistingMessages() {
        for (result in listOf(
            MathValidationResult(true, "correct", uiMessage = MathUiMessage(R.string.math_ui_correct)),
            MathValidationResult(false, "technical", evaluable = false, uiMessage = MathUiMessage(R.string.math_ui_incorrect)),
            MathValidationResult(false, "specific", uiMessage = MathUiMessage(R.string.math_ui_simple_polygon)))) {
            assertEquals(result.message, MathPuzzleFeedback.processMessage(result, "fallback") { result.message })
        }
    }

    @Test fun missingOrBlankAuthoredGuidanceUsesTheSafeProcessFallback() {
        for (hasSelections in listOf(false, true)) {
            assertEquals(listOf("Review the relation and the given data."),
                MathPuzzleFeedback.incorrectGuidance(hasSelections, " \n", listOf("", "\t"),
                    "Review the relation and the given data."))
        }
    }

    @Test fun choicesNeverReadTheGlobalExplanationOrAbsentOptions() {
        assertEquals(listOf("Review only the selected claim."),
            MathPuzzleFeedback.incorrectGuidance(true, "FORBIDDEN_GLOBAL_SOLUTION",
                listOf("Review only the selected claim."), "safe fallback"))
        assertEquals(listOf("safe fallback"),
            MathPuzzleFeedback.incorrectGuidance(true, "FORBIDDEN_GLOBAL_SOLUTION", emptyList(), "safe fallback"))
    }

    @Test fun nonselectionGuidanceKeepsItsAuthoredProcessAndVisibleFieldDetailsOnly() {
        assertEquals(listOf("Check the units.", "Quantity: preserve the relation."),
            MathPuzzleFeedback.incorrectGuidance(false, "Check the units.",
                listOf("Quantity: preserve the relation.", ""), "fallback"))
    }
}
