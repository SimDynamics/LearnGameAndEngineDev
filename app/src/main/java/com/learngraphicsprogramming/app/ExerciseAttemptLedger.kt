package com.learngraphicsprogramming.app

import org.json.JSONArray
import org.json.JSONObject

data class ExerciseAttemptKey(
    val routeId: String,
    val lessonId: String,
    val pageIndex: Int,
    val blockIndex: Int
)

/** Session-only response history. Attempt reserves and lesson progress remain their owners' concern. */
class ExerciseAttemptLedger {
    private val lock = Any()
    private val inFlight = mutableSetOf<ExerciseAttemptKey>()
    private val penalized = linkedMapOf<ExerciseAttemptKey, MutableSet<String>>()

    /** A null verdict is incomplete/invalid/technical feedback, never an evaluated wrong answer. */
    fun <T : Any> evaluate(
        key: ExerciseAttemptKey,
        answerIdentity: String,
        canEvaluate: () -> Boolean,
        evaluation: () -> T,
        verdict: (T) -> Boolean?,
        consumeIncorrect: () -> Unit
    ): T? {
        if (!synchronized(lock) { inFlight.add(key) }) return null
        try {
            if (!canEvaluate()) return null
            val result = evaluation()
            if (verdict(result) == false) {
                // Reserve the identity before calling out: even a repeated callback
                // during consumption cannot penalize this response a second time.
                val firstIncorrect = synchronized(lock) {
                    penalized.getOrPut(key) { linkedSetOf() }.add(answerIdentity)
                }
                if (firstIncorrect) consumeIncorrect()
            }
            return result
        } finally {
            synchronized(lock) { inFlight.remove(key) }
        }
    }

    /** Clear response history without unlocking an evaluation still running its callbacks. */
    fun clear() {
        synchronized(lock) { penalized.clear() }
    }

    /** Independent saved-instance state; this is never written to course progress or preferences. */
    fun save(): JSONObject = synchronized(lock) {
        JSONObject().put("version", 1).put("exercises", JSONArray().apply {
            penalized.forEach { (key, answers) ->
                put(JSONObject().put("routeId", key.routeId).put("lessonId", key.lessonId)
                    .put("pageIndex", key.pageIndex).put("blockIndex", key.blockIndex)
                    .put("answers", JSONArray(answers.toList())))
            }
        })
    }

    /** Reject a corrupt snapshot as a whole, retaining the existing history. */
    fun restore(json: JSONObject): Boolean {
        val restored = try {
            require(json.get("version") == 1)
            val exercises = json.getJSONArray("exercises")
            val entries = linkedMapOf<ExerciseAttemptKey, MutableSet<String>>()
            for (index in 0 until exercises.length()) {
                val entry = exercises.getJSONObject(index)
                val route = entry.get("routeId") as? String ?: return false
                val lesson = entry.get("lessonId") as? String ?: return false
                val page = entry.get("pageIndex") as? Int ?: return false
                val block = entry.get("blockIndex") as? Int ?: return false
                require(page >= 0 && block >= 0)
                val key = ExerciseAttemptKey(route, lesson, page, block)
                require(key !in entries)
                val answers = entry.getJSONArray("answers")
                val identities = linkedSetOf<String>()
                for (answerIndex in 0 until answers.length()) {
                    identities.add(answers.get(answerIndex) as? String ?: return false)
                }
                entries[key] = identities
            }
            entries
        } catch (_: Exception) {
            return false
        }
        synchronized(lock) {
            penalized.clear()
            penalized.putAll(restored)
        }
        return true
    }
}
