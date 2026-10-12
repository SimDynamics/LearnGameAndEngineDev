package com.learngraphicsprogramming.app.narration

import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class LessonNarrationScriptTest {
    private fun page(vararg blocks: JSONObject) = JSONObject().put("title","Test")
        .put("pages",JSONArray().put(JSONObject().put("blocks",JSONArray(blocks.toList()))))
    private fun paragraph(text: String) = JSONObject().put("type","paragraph").put("text",text)
    private fun choice() = JSONObject().put("type","multiple_choice").put("question","Choose a value.")
        .put("options",JSONArray(listOf("One","Two"))).put("correctAnswer",1)
        .put("explanation","The second value meets the condition.")
        .put("validator",JSONObject().put("expected","SECRET_VALIDATOR"))
    @Test fun successfulFeedbackHasItsOwnGateAfterTheQuestionAndBeforeLaterText() {
        val result = LessonNarrationScript.page(page(paragraph("Before."),choice(),paragraph("After.")),0,
            NarrationLanguage.AMERICAN_ENGLISH)
        val gate = result.gates.single()
        assertEquals(1,gate.blockIndex)
        assertTrue(result.texts.take(gate.start).joinToString(" ").contains("Choose"))
        assertTrue(result.texts.subList(gate.start,gate.endExclusive).joinToString(" ").contains("second value"))
        assertTrue(result.texts.drop(gate.endExclusive).contains("After."))
        assertFalse(result.texts.joinToString(" ").contains("SECRET_VALIDATOR"))
    }
    @Test fun mathematicalFeedbackDoesNotExposeWrongFeedbackOrValidator() {
        val block = JSONObject().put("type","math_puzzle").put("prompt","Compute.")
            .put("responses",JSONArray())
            .put("feedback",JSONObject().put("correct","This preserves equality.").put("incorrect","SECRET_WRONG"))
            .put("validator",JSONObject().put("expected","SECRET_EXPECTED"))
        val result = LessonNarrationScript.page(page(block),0,NarrationLanguage.AMERICAN_ENGLISH)
        assertEquals(1,result.gates.size)
        assertTrue(result.texts.joinToString(" ").contains("preserves equality"))
        assertFalse(result.texts.joinToString(" ").contains("SECRET"))
    }
    @Test fun compilerLaboratoriesAndOrdinaryProseAreNotExercisesToUnlock() {
        val result = LessonNarrationScript.page(page(paragraph("Introduction."),
            JSONObject().put("type","cpp_compiler").put("initialCode","int main() {}")),0,
            NarrationLanguage.AMERICAN_ENGLISH)
        assertTrue(result.gates.isEmpty())
    }
    @Test fun splittingDoesNotMoveGateAcrossASentenceOrEngineBoundary() {
        val result = LessonNarrationScript.page(page(choice(),paragraph("After.")),0,
            NarrationLanguage.AMERICAN_ENGLISH)
        for(engine in listOf(NarrationEngine.KOKORO,NarrationEngine.ANDROID)) {
            val gates = NarrationScriptSegments.gates(result.texts,engine,result.gates)
            val parts = if(engine == NarrationEngine.ANDROID) NarrationSegments.prepare(result.texts,250)
                else NarrationSegments.sentences(result.texts)
            assertEquals("Correct.",parts[gates.single().start])
            assertEquals("After.",parts[gates.single().endExclusive])
        }
    }
    @Test fun preRenderingIncludesAllFeedbackWithoutRequiringAnswers() {
        val result = LessonNarrationScript.page(page(choice(),choice(),paragraph("End.")),0,
            NarrationLanguage.AMERICAN_ENGLISH)
        assertEquals(2,result.gates.size)
        assertEquals(2,result.texts.count { it.contains("second value") })
        assertEquals("End.",result.texts.last())
    }
}
