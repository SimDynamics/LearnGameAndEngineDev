package com.learngraphicsprogramming.app

import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class CppLabInputPolicyTest {
    @Test fun aPlainCompilerDoesNotExposeInputEvenIfItsEditedSourceReadsStdin() {
        val block = JSONObject().put("type", "cpp_compiler")
            .put("initialCode", "int main() { int value; std::cin >> value; }")
        assertFalse(CppLabInputPolicy.requested(block, null))
    }

    @Test fun explicitPreparedInputEnablesTheFieldIncludingAnEmptyEofExercise() {
        for (input in listOf("", "Rita 4", "\n", " \t ")) {
            val block = JSONObject().put("type", "cpp_compiler").put("initialStandardInput", input)
            assertTrue(CppLabInputPolicy.requested(block, null))
            assertEquals(input, CppLabInputPolicy.effectiveInput(true, input))
        }
    }

    @Test fun aCheckThatDeclaresNonemptyInputAlsoRequiresTheField() {
        val block = JSONObject().put("type", "cpp_compiler").put("outputCheck", JSONObject()
            .put("cases", JSONArray().put(JSONObject().put("standardInput", "Leo 2")
                .put("outputs", JSONArray().put("Leo: 5")))))
        assertTrue(CppLabInputPolicy.requested(block, CppOutputCheck.fromBlock(block)))
    }

    @Test fun anEmptyInputCheckDoesNotExposeAnUnnecessaryField() {
        val block = JSONObject().put("type", "cpp_compiler").put("outputCheck", JSONObject()
            .put("cases", JSONArray().put(JSONObject().put("standardInput", "")
                .put("outputs", JSONArray().put("Caja recibida")))))
        assertFalse(CppLabInputPolicy.requested(block, CppOutputCheck.fromBlock(block)))
    }

    @Test fun anyDeclaredCaseWithInputEnablesTheFieldWithoutDependingOnTheSelectedCase() {
        val block = JSONObject().put("type", "cpp_compiler").put("outputCheck", JSONObject()
            .put("cases", JSONArray()
                .put(JSONObject().put("standardInput", "").put("outputs", JSONArray().put("empty")))
                .put(JSONObject().put("standardInput", "\n").put("outputs", JSONArray().put("line")))))
        assertTrue(CppLabInputPolicy.requested(block, CppOutputCheck.fromBlock(block)))
    }

    @Test fun aHiddenDraftCannotAlterTheRuntimeAndIsNotMutatedOrDiscarded() {
        val savedDraft = "private earlier draft\n  42\t"
        assertEquals("", CppLabInputPolicy.effectiveInput(false, savedDraft))
        assertEquals("private earlier draft\n  42\t", savedDraft)
        assertEquals(savedDraft, CppLabInputPolicy.effectiveInput(true, savedDraft))
    }
}
