package com.learngraphicsprogramming.app.narration

import android.content.Context
import com.learngraphicsprogramming.app.R
import com.learngraphicsprogramming.app.UiLanguageSettings

/**
 * Instructions displayed immediately below exercise headings.
 * Resolve them from exactly the same Android string resources as the UI,
 * including its independently configured interface language.
 */
internal object NarrationUiInstructions {
    fun forBlock(context: Context, blockType: String): String {
        val resource = when (blockType) {
            "order_code" -> R.string.exercise_order_instruction
            "drag_fill_code" -> R.string.exercise_fill_instruction
            else -> return ""
        }
        return UiLanguageSettings.localizedContext(context).getString(resource)
    }
}
