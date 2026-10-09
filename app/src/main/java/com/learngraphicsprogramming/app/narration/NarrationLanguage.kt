package com.learngraphicsprogramming.app.narration

import android.content.Context
import com.learngraphicsprogramming.app.R
import org.json.JSONObject
import java.util.Locale

enum class NarrationLanguage(val tag: String, val labelRes: Int, val defaultVoice: String) {
    SPANISH("es", R.string.narration_language_es, "ef_dora"),
    AMERICAN_ENGLISH("en-US", R.string.narration_language_en_us, "af_heart"),
    BRITISH_ENGLISH("en-GB", R.string.narration_language_en_gb, "bf_emma"),
    FRENCH("fr", R.string.narration_language_fr, "ff_siwis");

    fun label(context: Context): String = context.getString(labelRes)

    val locale: Locale get() = Locale.forLanguageTag(tag)
    val resourceGroup: String get() = if (locale.language == "en") "en" else tag

    companion object {
        fun fromTag(value: String?): NarrationLanguage? {
            val tag = value?.trim()?.lowercase(Locale.ROOT)?.replace('_', '-') ?: return null
            return when {
                tag in setOf("es", "español", "spanish") || tag.startsWith("es-") -> SPANISH
                tag in setOf("en-gb", "en-uk") || tag.startsWith("en-gb-") -> BRITISH_ENGLISH
                tag in setOf("en", "english") || tag.startsWith("en-") -> AMERICAN_ENGLISH
                tag in setOf("fr", "français", "french") || tag.startsWith("fr-") -> FRENCH
                else -> null
            }
        }

        /** Only explicit language metadata is authoritative; the device locale is irrelevant. */
        fun forLesson(lesson: JSONObject, pageIndex: Int, preference: NarrationLanguage): NarrationLanguage {
            val page = lesson.optJSONArray("pages")?.optJSONObject(pageIndex)
            for (source in listOfNotNull(page, page?.optJSONObject("metadata"), lesson, lesson.optJSONObject("metadata"))) {
                for (key in listOf("language", "locale", "lang", "idioma")) {
                    fromTag(source.optString(key))?.let { return it }
                }
            }
            return preference
        }
    }
}
