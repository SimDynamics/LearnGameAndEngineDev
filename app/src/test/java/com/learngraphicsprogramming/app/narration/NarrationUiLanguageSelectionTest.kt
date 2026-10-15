package com.learngraphicsprogramming.app.narration

import org.junit.Assert.assertEquals
import org.junit.Test

class NarrationUiLanguageSelectionTest {
    @Test fun explicitFrenchAndSpanishOverrideEitherEnglishVariant() {
        for (previous in listOf(NarrationLanguage.AMERICAN_ENGLISH, NarrationLanguage.BRITISH_ENGLISH)) {
            assertEquals(NarrationLanguage.FRENCH, NarrationUiLanguageSelection.resolve("fr", "es", previous))
            assertEquals(NarrationLanguage.SPANISH, NarrationUiLanguageSelection.resolve("es", "fr", previous))
        }
    }

    @Test fun englishRetainsTheSelectedVariantAcrossUiChanges() {
        for (english in listOf(NarrationLanguage.AMERICAN_ENGLISH, NarrationLanguage.BRITISH_ENGLISH)) {
            assertEquals(english, NarrationUiLanguageSelection.resolve("en", "fr", english))
            assertEquals(english, NarrationUiLanguageSelection.resolve("en-US", "fr", english))
        }
    }

    @Test fun englishDefaultsToAmericanWithoutAPreviousEnglishChoice() {
        assertEquals(NarrationLanguage.AMERICAN_ENGLISH, NarrationUiLanguageSelection.resolve("en", "fr", null))
        assertEquals(NarrationLanguage.AMERICAN_ENGLISH,
            NarrationUiLanguageSelection.resolve("en", "fr", NarrationLanguage.FRENCH))
    }

    @Test fun automaticSelectionUsesTheSystemSupportedLanguageButRetainsEnglishVariant() {
        assertEquals(NarrationLanguage.FRENCH, NarrationUiLanguageSelection.resolve("", "fr-CA,en-US", null))
        assertEquals(NarrationLanguage.BRITISH_ENGLISH,
            NarrationUiLanguageSelection.resolve("", "en-US", NarrationLanguage.BRITISH_ENGLISH))
        assertEquals(NarrationLanguage.SPANISH, NarrationUiLanguageSelection.resolve("", "de-DE", null))
    }
}
