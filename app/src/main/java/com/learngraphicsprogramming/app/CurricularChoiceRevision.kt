package com.learngraphicsprogramming.app

import org.json.JSONArray
import org.json.JSONObject

/**
 * Reviewed single-option changes preserve a saved answer index, not a new
 * grading rule. Historical content must be an exact prior full block from
 * stateLayout.choiceRevisions and be present in the editorial proof chain.
 */
internal object CurricularChoiceRevision {
    fun project(current: JSONObject, historical: JSONObject): JSONObject {
        require(current.getString("type") == "multiple_choice" &&
            historical.getString("type") == "multiple_choice")
        val before = historical.getJSONArray("options")
        val after = current.getJSONArray("options")
        require(before.length() >= 2 && before.length() == after.length())
        val correct = historical.get("correctIndex") as? Int
            ?: error("Historical correct option index is not an integer")
        require(correct in 0 until before.length() &&
            (current.get("correctIndex") as? Int) == correct)
        val changes = (0 until before.length()).count { index ->
            val old = before.get(index) as? String ?: error("Historical non-string option")
            val next = after.get(index) as? String ?: error("Revised non-string option")
            old != next
        }
        require(changes == 1) {
            "A reviewed option change must be one real replacement, not a new answer list"
        }
        val projected = JSONObject(current.toString())
        projected.put("options", JSONArray(before.toString()))
        // Other response-bearing keys, such as correctIndex, answer modes
        // and IDs, cannot use the editorial archive to bypass old signatures.
        require(LessonStateLayoutJson.stateFingerprint(projected) ==
            LessonStateLayoutJson.stateFingerprint(historical))
        return projected
    }
}
