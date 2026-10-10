package com.learngraphicsprogramming.app.narration

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class NarrationStorageBudgetTest {
    private val gib = NarrationStorageBudget.RESERVE_BYTES

    @Test fun physicalMaximumLeavesOneGibFreeForEverythingElse() {
        assertEquals(9 * gib, NarrationStorageBudget.capacity(2 * gib, 8 * gib))
        assertEquals(0L, NarrationStorageBudget.capacity(0L, gib / 2))
        assertEquals(0L, NarrationStorageBudget.capacity(0L, 0L))
    }

    @Test fun aSmallerLimitPausesWithoutDeletingExistingAudio() {
        val budget = NarrationStorageBudget.Snapshot(
            storedBytes = 3 * gib,
            availableBytes = 10 * gib,
            maximumBytes = 12 * gib,
            configuredBytes = 2 * gib,
            effectiveBytes = 2 * gib
        )
        assertFalse(budget.mayGenerate)
        assertFalse(NarrationStorageBudget.fitsNext(budget))
        assertEquals(3 * gib, budget.storedBytes)
    }

    @Test fun automaticWorkRequiresRoomAndPreservesPhysicalReserve() {
        val budget = NarrationStorageBudget.Snapshot(
            storedBytes = 2 * gib,
            availableBytes = 7 * gib,
            maximumBytes = 8 * gib,
            configuredBytes = 5 * gib,
            effectiveBytes = 5 * gib
        )
        assertTrue(NarrationStorageBudget.fitsNext(budget, 4L * NarrationStorageBudget.MIB))
        assertFalse(NarrationStorageBudget.fitsNext(budget, 4 * gib))
        assertFalse(NarrationStorageBudget.fitsNext(budget.copy(availableBytes = gib)))
    }

    @Test fun maximumMathSaturatesInsteadOfOverflowing() {
        assertEquals(Long.MAX_VALUE - gib,
            NarrationStorageBudget.capacity(Long.MAX_VALUE - 3, 9))
    }
}
