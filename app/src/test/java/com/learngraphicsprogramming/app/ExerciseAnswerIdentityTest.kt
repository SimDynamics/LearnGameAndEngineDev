package com.learngraphicsprogramming.app

import com.learngraphicsprogramming.app.math.MathAnswer
import com.learngraphicsprogramming.app.math.MathAnswerJson
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import java.math.BigDecimal

class ExerciseAnswerIdentityTest {
    @Test fun recursivelySortsObjectKeysAndMapEntriesButPreservesArrays() {
        val first = JSONObject().put("values", JSONObject().put("b", 2).put("a", 1))
            .put("ordered", JSONArray(listOf("x", "y")))
        val second = JSONObject().put("ordered", JSONArray(listOf("x", "y")))
            .put("values", JSONObject().put("a", 1).put("b", 2))
        assertEquals(ExerciseAnswerIdentity.of(first), ExerciseAnswerIdentity.of(second))
        assertEquals(ExerciseAnswerIdentity.of(first), ExerciseAnswerIdentity.of(mapOf(
            "ordered" to listOf("x", "y"), "values" to linkedMapOf("a" to 1, "b" to 2))))
        second.put("ordered", JSONArray(listOf("y", "x")))
        assertNotEquals(ExerciseAnswerIdentity.of(first), ExerciseAnswerIdentity.of(second))
    }

    @Test fun choiceSetsAndFieldMatchingMapsHaveStableTypedAnswerIdentity() {
        assertEquals(identity(MathAnswer.Choice(linkedSetOf("b", "a"))), identity(MathAnswer.Choice(linkedSetOf("a", "b"))))
        assertNotEquals(identity(MathAnswer.Choice(setOf("a"))), identity(MathAnswer.Choice(setOf("b"))))
        assertEquals(identity(MathAnswer.Fields(linkedMapOf("second" to "2", "first" to "1"))),
            identity(MathAnswer.Fields(linkedMapOf("first" to "1", "second" to "2"))))
        assertEquals(identity(MathAnswer.Matching(linkedMapOf("b" to "y", "a" to "x"))),
            identity(MathAnswer.Matching(linkedMapOf("a" to "x", "b" to "y"))))
        assertNotEquals(identity(MathAnswer.Order(listOf("a", "b"))), identity(MathAnswer.Order(listOf("b", "a"))))
    }

    @Test fun primitiveTypesDelimitersAndExactNumericValuesDoNotCollide() {
        assertEquals(ExerciseAnswerIdentity.of(null), ExerciseAnswerIdentity.of(JSONObject.NULL))
        assertEquals(ExerciseAnswerIdentity.of(1), ExerciseAnswerIdentity.of(1.0))
        assertEquals(ExerciseAnswerIdentity.of(1000L), ExerciseAnswerIdentity.of(BigDecimal("1E+3")))
        assertEquals(ExerciseAnswerIdentity.of(0), ExerciseAnswerIdentity.of(-0.0))
        assertNotEquals(ExerciseAnswerIdentity.of(1), ExerciseAnswerIdentity.of("1"))
        assertNotEquals(ExerciseAnswerIdentity.of(false), ExerciseAnswerIdentity.of("false"))
        assertNotEquals(ExerciseAnswerIdentity.of(BigDecimal("10000000000000001")),
            ExerciseAnswerIdentity.of(BigDecimal("10000000000000000")))
        assertNotEquals(ExerciseAnswerIdentity.of(listOf("a|b", "c")), ExerciseAnswerIdentity.of(listOf("a", "b|c")))
        assertNotEquals(ExerciseAnswerIdentity.of(" a "), ExerciseAnswerIdentity.of("a"))
        assertNotEquals(ExerciseAnswerIdentity.of("a\nb"), ExerciseAnswerIdentity.of("a b"))
    }

    @Test fun arraysAndSnapshotsHashTheWholeStateWithABoundedIdentity() {
        assertEquals(ExerciseAnswerIdentity.of(intArrayOf(1, 2)), ExerciseAnswerIdentity.of(JSONArray(listOf(1, 2))))
        val fields = JSONObject().put("first", "wrong").put("second", "answer")
        val identity = ExerciseAnswerIdentity.of(fields)
        assertTrue(identity.matches(Regex("[0-9a-f]{64}")))
        assertEquals(identity, ExerciseAnswerIdentity.of(JSONObject(fields.toString())))
        fields.put("second", "changed")
        assertNotEquals(identity, ExerciseAnswerIdentity.of(fields))
        assertEquals(64, ExerciseAnswerIdentity.of("x".repeat(100_000)).length)
    }

    @Test fun invalidValuesAndCircularContainersFailInsteadOfCreatingFalseIdentities() {
        for (value in listOf(Double.NaN, Double.POSITIVE_INFINITY, Any(), mapOf(1 to "a"))) {
            try { ExerciseAnswerIdentity.of(value); fail("Invalid value must be rejected") }
            catch (_: IllegalArgumentException) { }
        }
        val circular = JSONObject()
        circular.put("self", circular)
        try { ExerciseAnswerIdentity.of(circular); fail("Circular JSON must be rejected") }
        catch (_: IllegalArgumentException) { }
    }

    private fun identity(answer: MathAnswer) = ExerciseAnswerIdentity.of(MathAnswerJson.encode(answer))
}
