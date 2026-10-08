package com.learngraphicsprogramming.app.narration.phonemes

import org.junit.Assert.assertEquals
import org.junit.Test

class MisakiEnglishNumbersTest {
    @Test fun fractionsAndOrdinalsRemainDeterministic() {
        assertEquals(listOf("three", "point", "one", "four"), MisakiEnglishNumbers.expand("3.14").words)
        assertEquals(listOf("twenty", "first"), MisakiEnglishNumbers.expand("21st").words)
        assertEquals(listOf("six", "hundred", "seventy", "eighth"), MisakiEnglishNumbers.expand("678th").words)
    }

    @Test fun yearsUseMisakiYearConvention() {
        assertEquals(listOf("nineteen", "ninety", "nine"), MisakiEnglishNumbers.expand("1999").words)
        assertEquals(listOf("two", "thousand", "five"), MisakiEnglishNumbers.expand("2005").words)
        assertEquals(listOf("twenty", "twenty", "six"), MisakiEnglishNumbers.expand("2026").words)
    }

    @Test fun signedLargeNumbersAndDecimalCurrencyAreExpanded() {
        assertEquals(listOf("minus", "one", "million", "two"), MisakiEnglishNumbers.expand("-1,000,002").words)
        assertEquals(listOf("three", "dollars", "and", "twenty", "five", "cents"),
            MisakiEnglishNumbers.expand("3.25", "$" ).words)
        assertEquals(listOf("five", "pence"), MisakiEnglishNumbers.expand("0.05", "£").words)
    }
}
