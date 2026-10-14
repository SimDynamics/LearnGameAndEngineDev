package com.learngraphicsprogramming.app.math

import org.json.JSONObject
import java.security.MessageDigest

/** The renderer and progress calculation agree on whether saved answers belong to this content. */
object MathPuzzleState {
    fun fingerprint(block: JSONObject): String = MessageDigest.getInstance("SHA-256")
        .digest(withoutReviewFeedback(block).toString().toByteArray(Charsets.UTF_8)).joinToString("") { "%02x".format(it) }

    /** The optional selected-option guidance does not change any response, validator or
     * legacy content field. Removing just that new key retains pre-existing saved states. */
    internal fun withoutReviewFeedback(block: JSONObject): JSONObject {
        fun responses(value: JSONObject): List<JSONObject> = when (value.optString("type")) {
            "math_puzzle" -> value.optJSONArray("responses")?.let { responses ->
                (0 until responses.length()).mapNotNull { responses.optJSONObject(it) }
                    .filter { it.optString("type") in MathPuzzleTypes.responseTypes }
            }.orEmpty()
            in MathPuzzleTypes.responseTypes -> listOf(value)
            else -> emptyList()
        }
        fun options(value: JSONObject): List<JSONObject> = value.optJSONArray("options")?.let { array ->
            (0 until array.length()).mapNotNull { array.optJSONObject(it) }
        }.orEmpty()
        fun selectionOptions(response: JSONObject): List<JSONObject> =
            if (response.optString("type") in setOf("math_multiple_choice_single", "math_multiple_choice_multi")) options(response)
            else response.optJSONArray("fields")?.let { fields ->
                (0 until fields.length()).mapNotNull { fields.optJSONObject(it) }
                    .filter { it.optString("type") == "dropdown" }.flatMap(::options)
            }.orEmpty()
        if (responses(block).none { response -> selectionOptions(response).any { it.has("reviewFeedback") } }) return block
        return JSONObject(block.toString()).also { copy ->
            responses(copy).forEach { response -> selectionOptions(response).forEach { it.remove("reviewFeedback") } }
        }
    }

    fun isCurrentCorrect(block: JSONObject, state: JSONObject?): Boolean {
        if (state == null || !state.optBoolean("correct", false) || state.optString("contentFingerprint") != fingerprint(block)) return false
        return try {
            val answers = state.getJSONObject("responses")
            MathPuzzleParser.parse(block).responses.all { response ->
                val answer = answers.optJSONObject(response.id)?.let { MathAnswerJson.decode(it) }
                answer != null && MathValidators.validate(response.validator, answer).correct
            }
        } catch (_: Exception) { false }
    }
}
