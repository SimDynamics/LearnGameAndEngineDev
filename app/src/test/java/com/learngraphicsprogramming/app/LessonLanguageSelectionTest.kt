package com.learngraphicsprogramming.app

import org.junit.Assert.assertEquals
import org.junit.Test

class LessonLanguageSelectionTest {
    private val tags = listOf("", "es", "en", "fr")

    @Test fun automaticIsTheEmptyTag() {
        assertEquals(0, LessonLanguageSelection.indexOf(tags, ""))
    }

    @Test fun exactSupportedLanguagesAreSelected() {
        tags.forEachIndexed { index, tag -> assertEquals(index, LessonLanguageSelection.indexOf(tags, tag)) }
    }

    @Test fun regionalAndroidSettingsMapToTheSameLanguage() {
        assertEquals(1, LessonLanguageSelection.indexOf(tags, "es-MX"))
        assertEquals(2, LessonLanguageSelection.indexOf(tags, "en-US"))
        assertEquals(2, LessonLanguageSelection.indexOf(tags, "en-GB"))
        assertEquals(3, LessonLanguageSelection.indexOf(tags, "fr-CA"))
    }

    @Test fun languageListsUseTheirPrimaryPreference() {
        assertEquals(1, LessonLanguageSelection.indexOf(tags, "es-MX,en-US"))
        assertEquals(3, LessonLanguageSelection.indexOf(tags, "fr,en"))
    }

    @Test fun anExplicitlySupportedRegionIsNotLost() {
        val regional = listOf("", "es", "en", "en-GB", "fr")
        assertEquals(3, LessonLanguageSelection.indexOf(regional, "en-GB,en-US"))
    }

    @Test fun unknownLanguageDoesNotInventAnotherSupportedChoice() {
        assertEquals(0, LessonLanguageSelection.indexOf(tags, "de-DE"))
    }
}
