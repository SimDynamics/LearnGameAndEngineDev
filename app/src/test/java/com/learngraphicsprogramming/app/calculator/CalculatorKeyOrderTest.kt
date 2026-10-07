package com.learngraphicsprogramming.app.calculator

import org.junit.Assert.*
import org.junit.Test

class CalculatorKeyOrderTest {
    private val defaults = listOf("sin", "cos", "tan", "log", "ln", null)

    @Test fun swapsPersistWithoutChangingOtherCellsOrAnotherMode() {
        val basic = CalculatorKeyOrder(defaults)
        val advanced = CalculatorKeyOrder(defaults + listOf("asin", "acos", "atan", null, null, null))
        assertTrue(basic.swap("sin", "ln"))
        assertEquals(listOf("ln", "cos", "tan", "log", "sin", null), basic.slots)
        assertEquals("sin", advanced.slots.first())
        val reopened = CalculatorKeyOrder(defaults)
        reopened.restore(basic.slots)
        assertEquals(basic.slots, reopened.slots)
        assertFalse(reopened.swap("sin", "missing"))
        assertFalse(reopened.swap("sin", "sin"))
    }

    @Test fun olderOrCorruptOrdersRetainEveryAvailableToolExactlyOnce() {
        val order = CalculatorKeyOrder(defaults)
        order.restore(listOf("ln", "sin", "sin", "removed", null, "cos"))
        assertEquals("ln", order.slots.first())
        assertEquals(defaults.filterNotNull().toSet(), order.slots.filterNotNull().toSet())
        assertEquals(5, order.slots.filterNotNull().size)
        assertEquals(0, order.slots.size % CalculatorKeyOrder.COLUMNS)
        order.restore(List(257) { "sin" })
        assertEquals(defaults, order.slots)
        order.restore(null)
        assertEquals(defaults, order.slots)
    }
}
