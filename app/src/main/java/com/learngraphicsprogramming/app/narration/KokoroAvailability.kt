package com.learngraphicsprogramming.app.narration

import android.content.Context
import com.learngraphicsprogramming.app.R

/** Synthesized resources and the implementation's language limits are separate states. */
object KokoroAvailability {
    val isAvailable: Boolean = true

    fun reasonFor(context: Context, language: NarrationLanguage, englishResourcesInstalled: Boolean = false): String =
        context.getString(when (language) {
            NarrationLanguage.SPANISH -> R.string.narration_availability_es
            NarrationLanguage.AMERICAN_ENGLISH, NarrationLanguage.BRITISH_ENGLISH ->
                if (englishResourcesInstalled) R.string.narration_availability_en_installed else R.string.narration_availability_en_missing
            NarrationLanguage.FRENCH -> R.string.narration_availability_fr
        })
}
