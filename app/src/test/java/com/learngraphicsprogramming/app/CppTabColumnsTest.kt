package com.learngraphicsprogramming.app

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class CppTabColumnsTest {
    @Test fun tabsAdvanceToTheNextFourColumnBoundaryRatherThanAddingFourSpaces() {
        assertEquals(listOf(4, 8, 12, 16), CppTabColumns.tabs("\t\t\t \t").map { it.target })
        assertEquals(listOf(4), CppTabColumns.tabs("abc\t").map { it.target })
        assertEquals(listOf(8), CppTabColumns.tabs("abcd\t").map { it.target })
        assertEquals(listOf(4, 12), CppTabColumns.tabs("\tabcd\t").map { it.target })
    }

    @Test fun eachOriginalParagraphResetsItsColumnsAndKeepsUtf16Offsets() {
        val source = "a\t\r\n\t\t\nplain\n\ud83d\ude00\t"
        assertEquals(listOf(
            CppTabColumns.Tab(1, 0, 1, 4),
            CppTabColumns.Tab(4, 4, 0, 4),
            CppTabColumns.Tab(5, 4, 4, 8),
            CppTabColumns.Tab(15, 13, 1, 4)
        ), CppTabColumns.tabs(source))
        assertEquals("a\t\r\n\t\t\nplain\n\ud83d\ude00\t", source)
    }

    @Test fun ordinaryTextHasNoTabLayoutMetadata() {
        assertTrue(CppTabColumns.tabs("").isEmpty())
        assertTrue(CppTabColumns.tabs("    int value = 1;\n").isEmpty())
    }

    @Test fun fractionalFontMetricsDoNotAccumulateRoundingOverRepeatedTabs() {
        val source = "\t\t\t \t".repeat(100)
        val tabs = CppTabColumns.tabs(source)
        for (cell in listOf(7.125f, 7.2f, 10.796875f, 14.40625f, 18.59375f)) {
            val widths = CppTabColumns.pixelWidths(tabs, cell)
            var pixels = 0f
            var column = 0
            tabs.forEachIndexed { index, tab ->
                pixels += (tab.column - column) * cell + widths[index]
                assertEquals(tab.target * cell, pixels, .51f)
                column = tab.target
            }
        }
    }
}
