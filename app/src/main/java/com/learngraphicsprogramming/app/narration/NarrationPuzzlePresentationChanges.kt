package com.learngraphicsprogramming.app.narration

import org.json.JSONObject

/** A cleared, already empty verdict does not change passive labels in these controls.
 * Movable cards and future/unknown controls conservatively retain the ordinary rebuild. */
internal object NarrationPuzzlePresentationChanges {
    private val fixedResponses = setOf("math_numeric_input", "math_expression_input", "math_multi_input",
        "math_structured_answer", "math_equation_fill_blanks", "math_multiple_choice_single", "math_multiple_choice_multi")
    fun hasFixedTextOrder(block: JSONObject?): Boolean {
        block ?: return false
        val type = block.optString("type")
        if (type != "math_puzzle") return false
        val tools = block.optJSONArray("tools")
        if (tools != null && (0 until tools.length()).any { tools.optJSONObject(it)?.optBoolean("interactive", false) == true }) return false
        val responses = block.optJSONArray("responses") ?: return false
        return responses.length() > 0 && (0 until responses.length()).all {
            responses.optJSONObject(it)?.optString("type") in fixedResponses
        }
    }
}
