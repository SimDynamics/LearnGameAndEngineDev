package com.learngraphicsprogramming.app

import java.io.File
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

/** Exercises the opt-in content contract and the shared renderer evaluator, not C++ execution. */
class CppDecimalFillAnswersTest {
    private fun published(): JSONObject {
        val resources = File(System.getProperty("translation.resources") ?: "src/main/res")
        val path = resources.absoluteFile.parentFile!!.resolve("assets/content/es/programming/cpp_fundamentals/" +
            "errores_y_construccion_gradual/lesson_requisitos_casos_y_primera_version.json")
        return JSONObject(path.readText()).getJSONArray("pages").getJSONObject(2)
            .getJSONArray("blocks").getJSONObject(4)
    }

    @Test fun realBlockKeepsCanonicalAnswersAndAcceptsExactDecimalVariants() {
        val block = published()
        assertEquals("conservar_preparacion", block.getString("id"))
        assertEquals("cpp_decimal_literal_value", block.getString("answerComparison"))
        assertEquals(listOf("3", "3.0"), strings(block.getJSONArray("answers")))
        for (answer in listOf("3", "3.0", "3.00", "3e0", "3E+0", "30e-1", "0.3e1", "3.",
            ".3e1", "03.0", "0003e0", "3'000e-3", "3'0e-1", "3.0'0", "3e0'0", "3e-0", "3.e+0")) {
            assertTrue(answer, CppExerciseAnswers.fillMatches(block, answer))
            assertTrue("External whitespace: $answer", CppExerciseAnswers.fillMatches(block, " \t$answer\r\n "))
        }
    }

    @Test fun differentValuesAndNonLiteralSyntaxAreRejectedWithoutFloatingRounding() {
        val block = published()
        for (answer in listOf("0", "2", "4", "3.1", "3.9", "3e1", "3e-1",
            "3.0000000000000001", "2.9999999999999999", "3e", "3e+", "3..0", "3 .0", "3,0",
            "preparacion", "//3", "3/* comentario */", "\"3\"", "'3'", "1+2", "(3)", "+3", "-3",
            "0x3", "0X3", "0b11", "03", "0'3", "3u", "3L", "3.0f", "3.0L", "3_suffix",
            "3''0e-1", "3'", "'3", "3e'0", "3.0'", "3\t.0", "3\n.0", "3e +0",
            "3.00000000000000000000000000000000000000000000000000001")) {
            assertFalse(answer, CppExerciseAnswers.fillMatches(block, answer))
        }
    }

    @Test fun expectedValueComesFromFirstAnswerAndEveryAlternativeMustAgreeExactly() {
        val block = published().put("answers", JSONArray(listOf("0.3e1", "3'000e-3", "3.")))
        assertTrue(CppExerciseAnswers.fillMatches(block, "3.00"))
        assertFalse(CppExerciseAnswers.fillMatches(block, "0.3"))
        block.put("answers", JSONArray(listOf("2.50", "25e-1")))
        assertTrue(CppExerciseAnswers.fillMatches(block, "2.5"))
        assertFalse(CppExerciseAnswers.fillMatches(block, "3"))
        // Zero's exponent cannot trigger expansion or change the exact decimal quantity.
        block.put("answers", JSONArray(listOf("0", "0.0")))
        assertTrue(CppExerciseAnswers.fillMatches(block, "0e" + "9".repeat(5000)))
    }

    @Test fun absentSelectorPreservesLegacyMatchingAndInternalWhitespaceReachesNumericParser() {
        val block = published()
        val unchanged = JSONObject(block.toString()).apply { remove("answerComparison") }
        assertTrue(CppExerciseAnswers.fillMatches(unchanged, " \t3.0\n"))
        for (extra in listOf("3.00", "3e0", "3'000e-3")) assertFalse(CppExerciseAnswers.fillMatches(unchanged, extra))
        assertEquals("3  .0", CppExerciseAnswers.normalizeFill(block, "\t 3  .0 \n"))
        assertEquals("3 .0", CppExerciseAnswers.normalizeFill(unchanged, "\t 3  .0 \n"))
        val types = JSONObject().put("code", "___ n;").put("answers", JSONArray(listOf("signed int")))
        assertTrue(CppExerciseAnswers.fillMatches(types, "\t signed  \n int  "))
        assertFalse(CppExerciseAnswers.fillMatches(types, "3e0"))
        val literal = JSONObject().put("code", "std::cout << ___;").put("answers", JSONArray(listOf("\"A B\"")))
        assertTrue(CppExerciseAnswers.fillMatches(literal, " \"A B\" "))
        assertFalse(CppExerciseAnswers.fillMatches(literal, "\"A  B\""))
    }

    @Test fun malformedSelectorsAndAcceptedAnswersThrowTechnicalConfigurationErrors() {
        for (mode in listOf(JSONObject.NULL, 7, true, JSONArray(), "", "cpp_decimal_value", "trimmed")) {
            invalid { CppExerciseAnswers.fillMatches(published().put("answerComparison", mode), "3") }
        }
        for (answers in listOf(listOf("3", "3.1"), listOf("03"), listOf("+3"), listOf("3.0f"),
            listOf("3", "3.0000000000000001"), listOf("3", "1+2"), emptyList())) {
            invalid { CppExerciseAnswers.fillMatches(published().put("answers", JSONArray(answers)), "invalid student input") }
        }
    }

    @Test fun resourceLimitsRemainTechnicalFailuresInsideTheExistingAttemptEvaluator() {
        val block = published()
        val tooLong = "3." + "0".repeat(CppDecimalLiteral.MAX_LENGTH)
        val ledger = ExerciseAttemptLedger()
        var consumed = 0
        try {
            ledger.evaluate(ExerciseAttemptKey("programming", "numeric_fixture", 2, 4),
                ExerciseAnswerIdentity.of(tooLong), canEvaluate = { true },
                evaluation = { CppExerciseAnswers.fillMatches(block, tooLong) }, verdict = { it },
                consumeIncorrect = { consumed++ })
            fail("An analysis limit must remain a technical exception")
        } catch (_: CppDecimalLiteral.ResourceLimitException) { }
        assertEquals(0, consumed)
        assertTrue(CppExerciseAnswers.fillMatches(block, "3e0"))
        assertFalse(CppExerciseAnswers.fillMatches(block, "3e" + "9".repeat(5000)))
        val expectedTooLong = published().put("answers", JSONArray(listOf(tooLong)))
        try { CppExerciseAnswers.fillMatches(expectedTooLong, "3"); fail("Expected input has the same resource guard") }
        catch (_: CppDecimalLiteral.ResourceLimitException) { }
    }

    private fun invalid(action: () -> Boolean) {
        try { action(); fail("Malformed content must throw before evaluating the student") }
        catch (_: IllegalArgumentException) { }
    }
    private fun strings(array: JSONArray) = (0 until array.length()).map { array.getString(it) }
}
