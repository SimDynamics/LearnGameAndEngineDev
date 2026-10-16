package com.learngraphicsprogramming.app.narration

import com.learngraphicsprogramming.app.math.MathPuzzleTypes
import org.json.JSONArray
import org.json.JSONObject

/** Gate indices refer to text items, before the selected engine splits sentences. */
data class NarrationTextGate(val blockIndex: Int, val start: Int, val endExclusive: Int)
/** In-memory provenance derived while extracting the existing lesson text. */
data class NarrationTextOrigin(val blockIndex: Int, val feedback: Boolean = false)

data class LessonNarrationScript(
    val texts: List<String>,
    val gates: List<NarrationTextGate>,
    val origins: List<NarrationTextOrigin> = emptyList()
) {
    companion object {
        private val codeExercises = setOf("multiple_choice", "fill_code", "inline_fill_code",
            "output_prediction", "order_code", "drag_fill_code")
        fun isExercise(type: String): Boolean = type in codeExercises || MathPuzzleTypes.isExercise(type)

        fun page(lesson: JSONObject, pageIndex: Int, language: NarrationLanguage,
                 instruction: (String) -> String): LessonNarrationScript =
            page(lesson, pageIndex, language, false, instruction)

        /** Authoring/validation trees stay private. Only explicit successful feedback is included. */
        fun page(lesson: JSONObject, pageIndex: Int, language: NarrationLanguage,
                 forKokoro: Boolean = false,
                 instruction: (String) -> String = { "" }): LessonNarrationScript {
            val blocks = lesson.optJSONArray("pages")?.optJSONObject(pageIndex)?.optJSONArray("blocks")
                ?: return LessonNarrationScript(emptyList(), emptyList())
            val texts = mutableListOf<String>()
            val gates = mutableListOf<NarrationTextGate>()
            val origins = mutableListOf<NarrationTextOrigin>()
            fun append(items: List<String>, blockIndex: Int, feedback: Boolean = false) {
                texts += items
                origins += List(items.size) { NarrationTextOrigin(blockIndex, feedback) }
            }
            fun authored(block: JSONObject): List<String> {
                val one = JSONObject().put("pages", JSONArray().put(JSONObject().put("blocks", JSONArray().put(block))))
                return LessonNarrationText.page(one, 0, language, forKokoro, instruction)
            }
            fun paragraph(text: String, mode: String = "auto") = authored(JSONObject()
                .put("type", "paragraph").put("text", text).put("mathMode", mode))
            if (pageIndex == 0) append(paragraph(lesson.optString("title", "")), -1)
            for (index in 0 until blocks.length()) {
                val block = blocks.optJSONObject(index) ?: continue
                append(authored(block), index)
                val type = block.optString("type", "")
                if (!isExercise(type)) continue
                val start = texts.size
                // Never inspect validator.expected, correctAnswer, solutions or incorrect feedback.
                val success = if (MathPuzzleTypes.isExercise(type)) {
                    block.optJSONObject("feedback")?.opt("correct") as? String ?: ""
                } else block.opt("explanation") as? String ?: ""
                val heading = when (language) {
                    NarrationLanguage.SPANISH -> "Correcto."
                    NarrationLanguage.FRENCH -> "Correct."
                    else -> "Correct."
                }
                append(paragraph(heading), index, feedback = true)
                if (success.isNotBlank()) {
                    append(paragraph(success, block.optString("mathMode", "auto")), index, feedback = true)
                }
                gates += NarrationTextGate(index, start, texts.size)
            }
            return LessonNarrationScript(texts, gates, origins)
        }
    }
}

internal object NarrationScriptSegments {
    fun gates(texts: List<String>, engine: NarrationEngine, gates: List<NarrationTextGate>): List<NarrationFeedbackGate> {
        if (gates.isEmpty()) return emptyList()
        val offsets = mutableListOf(0)
        texts.forEach { text ->
            val count = if (engine == NarrationEngine.ANDROID) NarrationSegments.prepare(listOf(text),250).size
                else NarrationSegments.sentences(listOf(text)).size
            offsets += offsets.last() + count
        }
        return gates.map {
            require(it.start in texts.indices && it.endExclusive in (it.start + 1)..texts.size)
            NarrationFeedbackGate(it.blockIndex,offsets[it.start],offsets[it.endExclusive])
        }
    }
}
