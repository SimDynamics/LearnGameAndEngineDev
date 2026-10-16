package com.learngraphicsprogramming.app

import org.json.JSONArray
import org.json.JSONObject

/** Optional guidance is presentation only: it never reads a solution or evaluates an answer. */
object CppExerciseFeedback {
    const val REVIEWED_ANSWER_IDENTITY = "reviewedAnswerIdentity"

    /** Validate the whole optional contract, including unselected entries, before displaying it.
     * C++ options remain strings; their parallel reviews preserve the original indices. */
    fun review(block: JSONObject, selectedIndex: Int? = null, fallback: String): String {
        if (!block.has("reviewFeedback")) return fallback
        val value = block.get("reviewFeedback")
        return when (block.optString("type")) {
            "multiple_choice" -> {
                val options = block.optJSONArray("options")
                    ?: throw IllegalArgumentException("Choice options must be an array")
                require(options.length() > 0 && (0 until options.length()).all { options.opt(it) is String }) {
                    "Choice options must remain text"
                }
                val reviews = value as? JSONArray
                    ?: throw IllegalArgumentException("Choice reviewFeedback must be an array")
                require(reviews.length() == options.length()) { "One review is required per choice option" }
                val texts = List(reviews.length()) { index -> text(reviews.opt(index)) }
                if (selectedIndex != null && selectedIndex in texts.indices) texts[selectedIndex] else fallback
            }
            "fill_code", "output_prediction", "order_code", "drag_fill_code" -> text(value)
            else -> throw IllegalArgumentException("Review feedback is not supported by this exercise type")
        }
    }

    /** The optional drag explanation belongs only to successful feedback, never to a hint. */
    fun dragSuccessExplanation(block: JSONObject): String =
        if (block.has("explanation")) text(block.get("explanation")) else ""

    /** Reuse the existing typed answer identity; this is not a content fingerprint or attempt ledger. */
    fun recordIncorrect(state: JSONObject, answer: Any): JSONObject {
        require(!state.optBoolean("correct", false)) { "A correct state cannot record an incorrect review" }
        return state.put(REVIEWED_ANSWER_IDENTITY, ExerciseAnswerIdentity.of(answer))
    }

    /** Old drafts without a marker stay drafts. Restoring guidance never re-evaluates an attempt. */
    fun wasReviewed(state: JSONObject?, answer: Any): Boolean =
        state != null && !state.optBoolean("correct", false) &&
            state.opt(REVIEWED_ANSWER_IDENTITY) == ExerciseAnswerIdentity.of(answer)

    private fun text(value: Any?): String = (value as? String)?.also {
        require(it.isNotBlank()) { "Review feedback must not be blank" }
    } ?: throw IllegalArgumentException("Review feedback must be text")
}
