package com.learngraphicsprogramming.app.narration

import org.junit.Assert.assertEquals
import org.junit.Test

class NarrationInitialSettingsTest {
    @Test fun supportedDeviceLocalesSelectTheirOriginalNarrationLanguage() {
        for ((tag, expected) in mapOf("es-MX" to NarrationLanguage.SPANISH,
            "fr-CA" to NarrationLanguage.FRENCH, "en-US" to NarrationLanguage.AMERICAN_ENGLISH,
            "en-GB" to NarrationLanguage.BRITISH_ENGLISH))
            assertEquals(tag, expected, NarrationInitialSettings.language(tag))
    }

    @Test fun preferredSupportedLocaleIsUsedAndUnavailableLanguagesFallBackToUsEnglish() {
        assertEquals(NarrationLanguage.FRENCH, NarrationInitialSettings.language("de-DE,fr-FR,en-US"))
        for (tags in listOf("de-DE", "ja-JP,ko-KR", "", "und"))
            assertEquals(tags, NarrationLanguage.AMERICAN_ENGLISH, NarrationInitialSettings.language(tags))
    }
}
