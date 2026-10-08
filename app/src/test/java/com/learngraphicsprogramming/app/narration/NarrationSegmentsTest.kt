package com.learngraphicsprogramming.app.narration

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class NarrationSegmentsTest {

    @Test
    fun longExplanationsStayInOrderWithoutExceedingEngineLimit() {
        val input = (1..60).joinToString(" ") { "Explicación $it." }
        val segments = NarrationSegments.prepare(listOf(input), 80)

        assertTrue(segments.size > 1)
        assertTrue(segments.all { it.length <= 80 && it.isNotBlank() })
        assertEquals(input, segments.joinToString(" "))
    }

    @Test
    fun splittingDoesNotProduceUnpairedUtf16Surrogates() {
        val input = "a".repeat(7) + "\uD83D\uDE00" + "b".repeat(12)
        val segments = NarrationSegments.prepare(listOf(input), 8)

        assertEquals(input, segments.joinToString(""))
        assertTrue(segments.all { it.length <= 8 })
        assertFalse(segments.any { it.last().isHighSurrogate() || it.first().isLowSurrogate() })
    }

    @Test
    fun separateEducationalBlocksRemainSeparate() {
        assertEquals(
            listOf("Título", "Una explicación.", "Otra explicación."),
            NarrationSegments.prepare(listOf(" Título ", "", "Una explicación.", "Otra explicación."), 100)
        )
    }

    @Test
    fun decimalsStayInOneSegmentWhenTheNumberFits() {
        val input = "El valor esperado es 3.14159265 y después se redondea con cuidado."
        val segments = NarrationSegments.prepare(listOf(input), 35)
        assertTrue(segments.any { "3.14159265" in it })
        assertEquals(input, segments.joinToString(" "))
    }
}
