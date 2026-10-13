package com.learngraphicsprogramming.app

import org.json.JSONArray
import org.json.JSONObject

/** Text contracts shared by the C++ puzzle renderers. Existing answers remain canonical. */
object CppExerciseAnswers {
    const val DECIMAL_LITERAL_VALUE = "cpp_decimal_literal_value"

    fun normalizeOutput(block: JSONObject, answer: String): String {
        val mode = if (block.has("outputComparison")) {
            block.get("outputComparison") as? String
                ?: throw IllegalArgumentException("Output comparison must be text")
        } else "trimmed"
        return when (mode) {
            "trimmed" -> answer.trim()
            "exact" -> answer
            else -> throw IllegalArgumentException("Invalid output comparison")
        }
    }

    fun outputMatches(block: JSONObject, answer: String): Boolean {
        val expected = strings(block.getJSONArray("answers"))
        val actual = normalizeOutput(block, answer)
        return expected.any { normalizeOutput(block, it) == actual }
    }

    /** Numeric profiles trim only the boundary; token whitespace must reach their parser intact. */
    fun normalizeFill(block: JSONObject, answer: String): String =
        if (block.has("answerComparison")) answer.trim()
        else CppCodeAnswerNormalizer.normalizeFill(block.optString("code"), answer)

    fun fillMatches(block: JSONObject, answer: String): Boolean {
        val expected = strings(block.getJSONArray("answers"))
        if (block.has("answerComparison")) {
            val mode = block.get("answerComparison") as? String
                ?: throw IllegalArgumentException("Fill comparison must be text")
            require(mode == DECIMAL_LITERAL_VALUE) { "Invalid fill comparison" }
            val values = expected.map {
                CppDecimalLiteral.parse(it)
                    ?: throw IllegalArgumentException("Accepted answer must be a decimal int/double literal")
            }
            require(values.all { it == values.first() }) { "Accepted literals must have the same exact value" }
            return CppDecimalLiteral.parse(answer) == values.first()
        }
        val code = block.optString("code")
        val actual = CppCodeAnswerNormalizer.normalizeFill(code, answer)
        return expected.any {
            CppCodeAnswerNormalizer.normalizeFill(code, it) == actual
        }
    }

    fun inlineMatches(block: JSONObject, answers: List<String>): Boolean {
        val canonical = strings(block.getJSONArray("answers"))
        require(answers.size == canonical.size) { "Invalid inline fill configuration" }
        val alternatives = if (block.has("answerAlternatives")) {
            val groups = block.getJSONArray("answerAlternatives")
            require(groups.length() == canonical.size) { "Invalid inline alternatives count" }
            List(groups.length()) { index ->
                strings(groups.getJSONArray(index)).also {
                    require(it.first() == canonical[index]) { "Canonical inline answer must remain first" }
                }
            }
        } else canonical.map { listOf(it) }
        val template = block.optString("template")
        val actual = CppCodeAnswerNormalizer.normalizeInline(template, answers)
        return answers.indices.all { index ->
            alternatives[index].any { candidate ->
                val expected = canonical.toMutableList().apply { this[index] = candidate }
                CppCodeAnswerNormalizer.normalizeInline(template, expected)[index] == actual[index]
            }
        }
    }

    private fun strings(array: JSONArray): List<String> {
        require(array.length() > 0) { "Missing accepted answers" }
        return List(array.length()) { index ->
            (array.get(index) as? String)?.also {
                require(it.isNotBlank()) { "Empty accepted answer" }
            } ?: throw IllegalArgumentException("Accepted answer must be text")
        }
    }
}
