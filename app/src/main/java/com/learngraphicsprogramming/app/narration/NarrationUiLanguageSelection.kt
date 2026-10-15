package com.learngraphicsprogramming.app.narration

/** One-way choice made only by an explicit change to the application's language. */
internal object NarrationUiLanguageSelection {
    fun resolve(
        selectedUiTags: String,
        systemTags: String,
        previousEnglish: NarrationLanguage?
    ): NarrationLanguage {
        val tags = selectedUiTags.ifEmpty { systemTags }
        val language = tags.split(',').firstNotNullOfOrNull { NarrationLanguage.fromTag(it) }
            ?: NarrationLanguage.SPANISH
        return if (language.resourceGroup == "en") {
            previousEnglish?.takeIf { it.resourceGroup == "en" }
                ?: NarrationLanguage.AMERICAN_ENGLISH
        } else language
    }
}
