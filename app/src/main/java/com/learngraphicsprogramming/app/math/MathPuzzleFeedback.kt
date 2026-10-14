package com.learngraphicsprogramming.app.math

/** Feedback is presentation, never a second evaluator. In particular, an incomplete or
 * incorrect multiple selection must not classify its selected (or absent) options. */
internal object MathPuzzleFeedback {
    fun isChoice(response: MathResponseSpec): Boolean = response.type in setOf(
        "math_multiple_choice_single", "math_multiple_choice_multi"
    )

    fun hasSelection(response: MathResponseSpec): Boolean = isChoice(response) ||
        fields(response).any { it.optString("type") == "dropdown" }

    fun isDropdownField(response: MathResponseSpec, id: String): Boolean =
        fields(response).any { it.optString("type") == "dropdown" && it.getString("id") == id }

    fun selectedReviews(response: MathResponseSpec, answer: MathAnswer, fallback: String,
                        fieldFallback: (Int) -> String = { "" }): List<String> {
        if (!isChoice(response)) {
            val values = (answer as? MathAnswer.Fields)?.values.orEmpty()
            return fields(response).mapIndexedNotNull { index, field ->
                if (field.optString("type") != "dropdown") return@mapIndexedNotNull null
                val options = field.optJSONArray("options")
                val selected = values[field.getString("id")]
                val option = options?.let { array -> (0 until array.length()).map { array.getJSONObject(it) }
                    .firstOrNull { it.getString("id") == selected } }
                    ?: return@mapIndexedNotNull null
                val review = option.optString("reviewFeedback").trim().ifBlank { fallback }
                val label = field.optString("label").trim().takeUnless { it.isBlank() || it == field.getString("id") }
                    ?: fieldFallback(index + 1)
                if (label.isBlank()) review else "$label: $review"
            }.ifEmpty { listOf(fallback) }
        }
        val selected = (answer as? MathAnswer.Choice)?.selected.orEmpty()
        val options = response.config.optJSONArray("options") ?: return listOf(fallback)
        return (0 until options.length()).mapNotNull { index ->
            val option = options.getJSONObject(index)
            if (option.getString("id") !in selected) null
            else option.optString("reviewFeedback").trim().ifBlank { fallback }
        }.distinct().ifEmpty { listOf(fallback) }
    }

    fun responseLabel(response: MathResponseSpec, fallback: String): String =
        response.config.optString("label").trim().takeUnless { it.isBlank() || it == response.id } ?: fallback

    /** Validators keep stable field IDs internally; only authored labels or numbered
     * localized names belong in learner-facing diagnostics. */
    fun fieldMessage(response: MathResponseSpec, fieldId: String?, message: String,
                     fallback: (Int) -> String): String {
        val definitions = response.config.optJSONArray("fields")
        var resolvedId = fieldId
        if (resolvedId == null && definitions != null) {
            for (index in 0 until definitions.length()) {
                val id = definitions.getJSONObject(index).getString("id")
                if (message.startsWith("$id: ")) { resolvedId = id; break }
            }
        }
        if (resolvedId == null) return message
        val index = if (definitions == null) -1 else (0 until definitions.length()).firstOrNull {
            definitions.getJSONObject(it).getString("id") == resolvedId
        } ?: -1
        val label = if (index < 0) fallback(1) else {
            definitions!!.getJSONObject(index).optString("label").trim()
                .takeUnless { it.isBlank() || it == resolvedId } ?: fallback(index + 1)
        }
        return "$label: ${message.removePrefix("$resolvedId: ")}"
    }

    private fun fields(response: MathResponseSpec): List<org.json.JSONObject> =
        response.config.optJSONArray("fields")?.let { definitions ->
            (0 until definitions.length()).map { definitions.getJSONObject(it) }
        }.orEmpty()
}
