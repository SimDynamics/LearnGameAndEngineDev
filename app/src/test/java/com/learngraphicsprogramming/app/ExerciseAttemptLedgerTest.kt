package com.learngraphicsprogramming.app

import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class ExerciseAttemptLedgerTest {
    private val key = ExerciseAttemptKey("programming", "lesson", 0, 1)
    private data class Result(val correct: Boolean?)

    @Test fun preservesEveryWrongResponseInsteadOfOnlyTheLastError() {
        val ledger = ExerciseAttemptLedger()
        var consumed = 0
        var evaluations = 0
        for (answer in listOf("A", "A", "B", "A", "B")) {
            assertEquals(Result(false), ledger.evaluate(key, answer, { true },
                { evaluations++; Result(false) }, { it.correct }, { consumed++ }))
        }
        assertEquals(2, consumed)
        assertEquals("Repeated answers can still receive feedback", 5, evaluations)
    }

    @Test fun correctIncompleteAndInvalidResultsNeverConsumeOrReserveTheirIdentity() {
        val ledger = ExerciseAttemptLedger()
        var consumed = 0
        for (correct in listOf(true, null, null, true)) {
            assertEquals(Result(correct), ledger.evaluate(key, "same", { true },
                { Result(correct) }, { it.correct }, { consumed++ }))
        }
        assertEquals(0, consumed)
        ledger.evaluate(key, "same", { true }, { Result(false) }, { it.correct }, { consumed++ })
        assertEquals(1, consumed)
    }

    @Test fun blockedEvaluationDoesNotRunAndCanBeRetriedAfterRecovery() {
        val ledger = ExerciseAttemptLedger()
        var evaluated = 0
        var consumed = 0
        assertNull(ledger.evaluate(key, "A", { false }, { evaluated++; Result(false) },
            { it.correct }, { consumed++ }))
        assertEquals(0, evaluated)
        assertEquals(0, consumed)
        ledger.evaluate(key, "A", { true }, { evaluated++; Result(false) }, { it.correct }, { consumed++ })
        assertEquals(1, evaluated)
        assertEquals(1, consumed)
        assertNull("A depleted reserve blocks even an already seen answer",
            ledger.evaluate(key, "A", { false }, { error("Must not evaluate") }, { false }, { consumed++ }))
        assertEquals(1, consumed)
    }

    @Test fun reentrantCallbacksCannotEvaluateTheSameExerciseAndReleaseTheGuardAfterward() {
        val ledger = ExerciseAttemptLedger()
        var consumed = 0
        var nestedEvaluations = 0
        fun nested() = ledger.evaluate(key, "nested", { true },
            { nestedEvaluations++; Result(false) }, { it.correct }, { consumed++ })
        ledger.evaluate(key, "A", { assertNull(nested()); true },
            { assertNull(nested()); Result(false) },
            { assertNull(nested()); it.correct },
            { assertNull(nested()); consumed++ })
        assertEquals(0, nestedEvaluations)
        assertEquals(1, consumed)
        assertNotNull(nested())
        assertEquals(1, nestedEvaluations)
        assertEquals(2, consumed)
    }

    @Test fun exceptionsPropagateWithoutPenalizingOrKeepingTheExerciseLocked() {
        val ledger = ExerciseAttemptLedger()
        var consumed = 0
        for (stage in listOf("gate", "evaluation", "verdict")) {
            val failure = IllegalStateException(stage)
            try {
                ledger.evaluate(key, "A", { if (stage == "gate") throw failure else true },
                    { if (stage == "evaluation") throw failure else Result(false) },
                    { if (stage == "verdict") throw failure else it.correct }, { consumed++ })
                fail("Exception must reach the UI error handler")
            } catch (actual: IllegalStateException) {
                assertSame(failure, actual)
            }
            assertEquals(0, consumed)
        }
        ledger.evaluate(key, "A", { true }, { Result(false) }, { it.correct }, { consumed++ })
        assertEquals(1, consumed)
    }

    @Test fun consumptionExceptionsReleaseTheGuardAndDoNotCauseADuplicateCharge() {
        val ledger = ExerciseAttemptLedger()
        var calls = 0
        try {
            ledger.evaluate(key, "A", { true }, { Result(false) }, { it.correct },
                { calls++; throw IllegalStateException("consumption") })
            fail("Consumption failure must propagate")
        } catch (_: IllegalStateException) { }
        assertNotNull(ledger.evaluate(key, "A", { true }, { Result(false) }, { it.correct }, { calls++ }))
        assertEquals("A callback may have already applied its charge before failing", 1, calls)
    }

    @Test fun scopesSeparateRoutesLessonsPagesAndBlocksAndAllowIndependentCallbacks() {
        val ledger = ExerciseAttemptLedger()
        var consumed = 0
        val keys = listOf(key, key.copy(routeId = "math"), key.copy(lessonId = "other"),
            key.copy(pageIndex = 1), key.copy(blockIndex = 2))
        for (scope in keys) {
            ledger.evaluate(scope, "A", { true }, { Result(false) }, { it.correct }, { consumed++ })
        }
        assertEquals(keys.size, consumed)
        assertNotNull(ledger.evaluate(key, "B", { true }, {
            assertNotNull(ledger.evaluate(keys[1], "B", { true }, { Result(false) }, { it.correct }, { consumed++ }))
            Result(false)
        }, { it.correct }, { consumed++ }))
        assertEquals(keys.size + 2, consumed)
    }

    @Test fun saveAndRestoreRetainTheWholeHistoryAndClearStartsANewSession() {
        val ledger = ExerciseAttemptLedger()
        var consumed = 0
        for (answer in listOf("A", "B")) ledger.evaluate(key, answer, { true },
            { Result(false) }, { it.correct }, { consumed++ })
        ledger.evaluate(key.copy(blockIndex = 2), "A", { true }, { Result(false) }, { it.correct }, { consumed++ })
        val saved = JSONObject(ledger.save().toString())
        val restored = ExerciseAttemptLedger()
        assertTrue(restored.restore(saved))
        for (answer in listOf("A", "B")) restored.evaluate(key, answer, { true },
            { Result(false) }, { it.correct }, { consumed++ })
        restored.evaluate(key.copy(blockIndex = 2), "A", { true }, { Result(false) }, { it.correct }, { consumed++ })
        assertEquals(3, consumed)
        restored.evaluate(key, "C", { true }, { Result(false) }, { it.correct }, { consumed++ })
        assertEquals(4, consumed)
        restored.clear()
        restored.evaluate(key, "A", { true }, { Result(false) }, { it.correct }, { consumed++ })
        assertEquals(5, consumed)
        assertEquals("Saving is a snapshot, not a live view", 2,
            saved.getJSONArray("exercises").getJSONObject(0).getJSONArray("answers").length())
    }

    @Test fun corruptRestoreNeverThrowsOrPartiallyReplacesExistingHistory() {
        val ledger = ExerciseAttemptLedger()
        var consumed = 0
        ledger.evaluate(key, "A", { true }, { Result(false) }, { it.correct }, { consumed++ })
        val good = ledger.save()
        val entry = good.getJSONArray("exercises").getJSONObject(0)
        val malformed = listOf(JSONObject(), JSONObject().put("version", 2).put("exercises", JSONArray()),
            JSONObject().put("version", 1).put("exercises", "not an array"),
            JSONObject(good.toString()).apply { getJSONArray("exercises").getJSONObject(0).put("pageIndex", "0") },
            JSONObject(good.toString()).apply { getJSONArray("exercises").getJSONObject(0).put("blockIndex", -1) },
            JSONObject().put("version", 1).put("exercises", JSONArray().put(entry).put(JSONObject())),
            JSONObject(good.toString()).apply { getJSONArray("exercises").getJSONObject(0).put("answers", JSONArray().put(7)) })
        for (snapshot in malformed) {
            assertFalse(ledger.restore(snapshot))
            ledger.evaluate(key, "A", { true }, { Result(false) }, { it.correct }, { consumed++ })
            assertEquals(1, consumed)
        }
    }
}
