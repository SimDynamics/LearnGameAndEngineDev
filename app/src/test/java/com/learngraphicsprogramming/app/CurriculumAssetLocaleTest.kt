package com.learngraphicsprogramming.app

import org.junit.Assert.assertEquals
import org.junit.Test

class CurriculumAssetLocaleTest {
    @Test fun primaryLanguageAndRegionalPreferencesUseTheirAssetFolders() {
        assertEquals("es", CurriculumAssetLocale.languageFor("es-MX", "fr"))
        assertEquals("en", CurriculumAssetLocale.languageFor("en-US", "es"))
        assertEquals("en", CurriculumAssetLocale.languageFor("en-GB,fr-CA", "es"))
        assertEquals("fr", CurriculumAssetLocale.languageFor("fr-CA", "en"))
        assertEquals("fr", CurriculumAssetLocale.languageFor("fr_FR,en-US", "es"))
    }

    @Test fun automaticUsesDeviceLanguageAndUnsupportedLanguagesUseSpanish() {
        assertEquals("en", CurriculumAssetLocale.languageFor("", "en-US"))
        assertEquals("fr", CurriculumAssetLocale.languageFor("", "fr-FR,es-MX"))
        assertEquals("es", CurriculumAssetLocale.languageFor("", "es-MX"))
        assertEquals("es", CurriculumAssetLocale.languageFor("", "de-DE"))
        assertEquals("es", CurriculumAssetLocale.languageFor("it-IT", "fr-FR"))
        assertEquals("es", CurriculumAssetLocale.languageFor("", ""))
    }
}
