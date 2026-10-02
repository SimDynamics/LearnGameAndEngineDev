package com.learngraphicsprogramming.app.math

import android.widget.EditText
import org.json.JSONArray
import org.json.JSONObject
import java.util.WeakHashMap

/** Keeps field context through popup reattachment without overwriting gesture/accessibility tags. */
internal object MathKeyboardContext {
    data class Keys(val symbols: List<String>, val minus: String = "-")
    private val contexts = WeakHashMap<EditText, Keys>()

    fun configure(input: EditText, validator: JSONObject?, mode: String, symbols: JSONArray?) {
        val literal = validator?.optString("type") in setOf("text", "exact_text")
        contexts[input] = Keys(MathShortcutResolver.resolve(validator, mode, symbols),
            if (literal && validator!!.optString("expected").contains('−')) "−" else "-")
    }

    fun configureVisual(input: EditText, config: JSONObject, validator: JSONObject, axis: String? = null) {
        contexts[input] = Keys(MathShortcutResolver.visualSymbols(config, validator, axis))
    }

    fun get(input: EditText): Keys? = contexts[input]
}
