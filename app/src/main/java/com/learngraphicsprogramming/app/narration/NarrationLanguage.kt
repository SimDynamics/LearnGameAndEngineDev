package com.learngraphicsprogramming.app.narration

import org.json.JSONObject
import java.util.Locale

enum class NarrationLanguage(val tag: String, val label: String, val defaultVoice: String) {
    SPANISH("es", "Español", "ef_dora"),
    AMERICAN_ENGLISH("en-US", "Inglés estadounidense", "af_heart"),
    BRITISH_ENGLISH("en-GB", "Inglés británico", "bf_emma"),
    FRENCH("fr", "Francés", "ff_siwis");

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
