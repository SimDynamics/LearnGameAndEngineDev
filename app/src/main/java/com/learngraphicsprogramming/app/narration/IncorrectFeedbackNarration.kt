package com.learngraphicsprogramming.app.narration

import org.json.JSONArray
import org.json.JSONObject

/** Reuse the ordinary paragraph speech front end over the currently displayed guidance.
 * This temporary extraction has no lesson identity, persistence, answers or success text. */
internal object IncorrectFeedbackNarration {
    /** The native verdict icon is decoration; its visible words remain in the narration. */
    fun visibleSpeech(text: String): String = text.drop(verdictIconPrefix(text))

    fun verdictIconPrefix(text: String): Int =
        if (text.startsWith("✗ ")) 2 else 0

    fun prepare(texts: List<String>, language: NarrationLanguage, forKokoro: Boolean): List<String> {
        val blocks = JSONArray()
        texts.filter { it.isNotBlank() }.forEach { text ->
            blocks.put(JSONObject().put("type", "paragraph").put("text", visibleSpeech(text)))
        }
        if (blocks.length() == 0) return emptyList()
        val page = JSONObject().put("blocks", blocks)
        val source = JSONObject().put("pages", JSONArray().put(page))
        return LessonNarrationText.page(source, 0, language, forKokoro)
    }
}
