package com.learngraphicsprogramming.app.narration

import org.junit.Assert.*
import org.junit.Test

class IncorrectFeedbackRewindTest {
    @Test fun rewindInsideTheWrongAnswerGuideBehavesLikeNormalPlayback() {
        assertEquals(IncorrectFeedbackRewind.Destination(0, 2600L),
            IncorrectFeedbackRewind.resolve(0, 5600L, -3000L, listOf(9000)))
        assertEquals(IncorrectFeedbackRewind.Destination(1, 300L),
            IncorrectFeedbackRewind.resolve(2, 1000L, -2500L, listOf(900, 1800, 5000)))
        assertEquals(IncorrectFeedbackRewind.Destination(0, 3400L),
            IncorrectFeedbackRewind.resolve(0, 400L, 3000L, listOf(9000)))
    }

    @Test fun rewindPastGuideStartReturnsOnlyTheUnspentTimeToTheOriginalPage() {
        assertEquals(IncorrectFeedbackRewind.Destination(0, 0L, -2700L),
            IncorrectFeedbackRewind.resolve(0, 2300L, -5000L, listOf(9000)))
        assertEquals(IncorrectFeedbackRewind.Destination(0, 0L, -1000L),
            IncorrectFeedbackRewind.resolve(2, 1000L, -5000L, listOf(1200, 1800, 5000)))
        assertEquals(IncorrectFeedbackRewind.Destination(0, 0L, -3000L),
            IncorrectFeedbackRewind.resolve(0, 0L, -3000L, listOf(9000)))
    }

    @Test fun anUnpreparedPreviousGuideSentenceDoesNotInventItsDurationOrSwitchOwner() {
        assertEquals(IncorrectFeedbackRewind.Destination(0, -2500L),
            IncorrectFeedbackRewind.resolve(1, 1500L, -4000L, listOf(null, 6000)))
    }
}
