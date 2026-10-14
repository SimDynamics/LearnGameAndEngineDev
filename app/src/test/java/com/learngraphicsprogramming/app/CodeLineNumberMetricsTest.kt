package com.learngraphicsprogramming.app

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class CodeLineNumberMetricsTest {
    @Test fun widthIncludesWholeTwoThreeAndMoreDigitNumbersAndRoundsUp() {
        val measure: (String) -> Float = { it.length * 10.25f }
        assertEquals(22, CodeLineNumberMetrics.requiredWidth("1", measure, 10, 18))
        assertEquals(32, CodeLineNumberMetrics.requiredWidth("1\n9\n10\n12", measure, 10, 18))
        assertEquals(42, CodeLineNumberMetrics.requiredWidth("99\n100\n140", measure, 10, 18))
        assertEquals(52, CodeLineNumberMetrics.requiredWidth("999\n1000", measure, 10, 18))
    }

    @Test fun blankContinuationRowsAndTrailingNewlinesDoNotInflateOrChangeLabels() {
        val labels = "1\n\n\n2\n\n10\n"
        val seen = mutableListOf<String>()
        val width = CodeLineNumberMetrics.requiredWidth(labels, { seen += it; it.length * 8f }, 6, 18)
        assertEquals(23, width)
        assertEquals(listOf("1", "", "", "2", "", "10", ""), seen)
        assertEquals("1\n\n\n2\n\n10\n", labels)
    }

    @Test fun everyLabelIsMeasuredInsteadOfAssumingLastLabelOrMonospacedDigits() {
        assertEquals(44, CodeLineNumberMetrics.requiredWidth("888\n1000\n", {
            it.sumOf { digit -> if (digit == '8') 13 else 5 }.toFloat()
        }, 4, 18))
    }

    @Test fun sameLabelsFitAfterFontScaleAndPaddingChanges() {
        val labels = "1\n10\n100\n1000"
        for (factor in listOf(.75f, 1f, 1.5f, 2f)) {
            val measure: (String) -> Float = { it.length * 9.7f * factor }
            val width = CodeLineNumberMetrics.requiredWidth(labels, measure, 13, 18)
            assertTrue(width > measure("1000") + 13)
        }
        assertEquals(18, CodeLineNumberMetrics.requiredWidth("", { 0f }, 2, 18))
    }

    @Test(expected = IllegalArgumentException::class)
    fun invalidFontMetricsAreNeverSilentlyTruncated() {
        CodeLineNumberMetrics.requiredWidth("10", { Float.NaN }, 0, 18)
    }
}
