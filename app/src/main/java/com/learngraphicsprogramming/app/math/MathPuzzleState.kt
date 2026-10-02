package com.learngraphicsprogramming.app.math

import org.json.JSONObject
import java.security.MessageDigest

/** The renderer and progress calculation agree on whether saved answers belong to this content. */
object MathPuzzleState {
    fun fingerprint(block: JSONObject): String = MessageDigest.getInstance("SHA-256")
        .digest(block.toString().toByteArray(Charsets.UTF_8)).joinToString("") { "%02x".format(it) }

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
