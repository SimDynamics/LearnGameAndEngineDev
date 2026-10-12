package com.learngraphicsprogramming.app

import org.junit.Assert.*
import org.junit.Test

class AttemptRecoveryLabelTest {
    @Test fun twoLostLivesAfterTenMinutesMeansOneHourFifty() {
        assertEquals("1h 50m",AttemptRecoveryLabel.format(8,6f+10f/60f,false))
    }
    @Test fun usesOneConsistentHoursMinutesFormat() {
        assertEquals("50m",AttemptRecoveryLabel.format(8,7f+10f/60f,false))
        assertEquals("2h",AttemptRecoveryLabel.format(8,6f,false))
        assertEquals("12h",AttemptRecoveryLabel.format(12,0f,false))
    }
    @Test fun shorterAndLongerLabelsUseExactlyTheSameCompactRules() {
        assertEquals("1m", AttemptRecoveryLabel.format(8, 8f - 1f / 60f, false))
        assertEquals("45m", AttemptRecoveryLabel.format(8, 8f - 45f / 60f, false))
        assertEquals("2h", AttemptRecoveryLabel.format(8, 6f, false))
        assertEquals("7h 55m", AttemptRecoveryLabel.format(8, 5f / 60f, false))
    }
    @Test fun fullAndPremiumReservesDoNotAddNoise() {
        assertEquals("",AttemptRecoveryLabel.format(8,8f,false))
        assertEquals("",AttemptRecoveryLabel.format(8,6f,true))
        assertEquals("",AttemptRecoveryLabel.format(8,Float.NaN,false))
    }
    @Test fun positiveRemainingSecondsRoundUpToOneMinute() {
        assertEquals("1m",AttemptRecoveryLabel.format(8,8f-1f/3600f,false))
    }
    @Test fun matchesAllMinuteBoundariesAcrossAFullEightHourReserve() {
        for(minutes in 1..480) {
            val reserve = 8f-minutes/60f
            assertEquals("${if (minutes < 60) "${minutes}m" else if (minutes % 60 == 0) "${minutes / 60}h" else "${minutes / 60}h ${minutes % 60}m"}",AttemptRecoveryLabel.format(8,reserve,false))
        }
    }
}
