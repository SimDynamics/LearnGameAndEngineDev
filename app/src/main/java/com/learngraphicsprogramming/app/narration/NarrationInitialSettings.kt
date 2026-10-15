package com.learngraphicsprogramming.app.narration

/** Only the first saved reader language is derived from Android's preferred locales. */
internal object NarrationInitialSettings {
    fun language(systemTags: String): NarrationLanguage = systemTags.split(',')
        .firstNotNullOfOrNull { NarrationLanguage.fromTag(it) }
        ?: NarrationLanguage.AMERICAN_ENGLISH
}
