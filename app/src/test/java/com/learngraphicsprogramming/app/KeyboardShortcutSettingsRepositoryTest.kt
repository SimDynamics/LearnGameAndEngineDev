package com.learngraphicsprogramming.app

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** Keyboard shortcut defaults only. No editor, IME, stored settings or progress is mutated. */
class KeyboardShortcutSettingsRepositoryTest {
    @Test fun originalShortcutsRemainAvailableInTheirOriginalOrder() {
        val original = listOf(
            "(", "{", "[", ";", "\"", "<", ">", "=", "+", "-", "*",
            "/", "&", "|", "!", ",", ":", "::", "<<", ">>"
        )
        val expanded = KeyboardShortcutSettingsRepository.getDefaultShortcuts()
        assertEquals(original, expanded.filter { it in original })
        assertEquals("No duplicates can create identical neighboring keys",
            expanded.size, expanded.distinct().size)
        assertFalse("TAB is not duplicated among the customizable keys", "\t" in expanded)
        assertEquals("The separate fixed Tab key is unchanged",
            "\t", KeyboardShortcutSettingsRepository.getTabText())
    }

    @Test fun defaultShortcutsIncludeBackslashDotApostropheAndAdvancedCppTokens() {
        val defaults = KeyboardShortcutSettingsRepository.getDefaultShortcuts()
        for (token in listOf(
            "\\", ".", "'", ")", "}", "]", "?", "%", "^", "~", "#", "##",
            "->", "->*", ".*", "++", "--", "&&", "||", "==", "!=", "<=", ">=", "..."
        )) {
            assertTrue("Required C++ punctuation/operator $token", token in defaults)
        }
        assertTrue("The second slash is placed beside /",
            defaults.indexOf("\\") == defaults.indexOf("/") + 1)
        assertTrue("Single quote stays next to double quote",
            defaults.indexOf("'") == defaults.indexOf("\"") + 1)
        assertTrue("The dot is alongside commas/colons",
            defaults.indexOf(".") == defaults.indexOf(",") + 1)
    }
}
