package com.learngraphicsprogramming.app.narration

import org.junit.Assert.assertEquals
import org.junit.Test

class NarrationRateTest {
    @Test
    fun speedCycleStartsAtOneAndReturnsToOne() {
        var rate = 1f
        val values = List(4) {
            rate = NarrationSettingsRepository.nextRate(rate)
            rate
        }
        assertEquals(listOf(1.5f, 2f, 0.5f, 1f), values)
        assertEquals(listOf("0.5X", "1X", "1.5X", "2X"),
            NarrationSettingsRepository.RATES.map(NarrationSettingsRepository::rateLabel))
    }
}
