package com.learngraphicsprogramming.app.narration

import org.junit.Assert.*
import org.junit.Test

class NarrationPresentationRemapTest {
    private fun script(texts: List<String>, origins: List<NarrationTextOrigin> = List(texts.size) { NarrationTextOrigin(0) },
                       gates: List<NarrationTextGate> = emptyList()) = LessonNarrationScript(texts, gates, origins)
    private fun remap(point: NarrationResumePoint, before: LessonNarrationScript, after: LessonNarrationScript,
                      engine: NarrationEngine = NarrationEngine.KOKORO) =
        requireNotNull(NarrationPresentationRemap.remap(point, before, after, engine))

    @Test fun insertedInstructionAndReorderedCardsPreserveTheExactOriginalAudioOffset() {
        val before = script(listOf("Order the numbers.", "Six hundred.", "Eight thousand.", "Three thousand."))
        val after = script(listOf("Order the numbers.", "Drag a card or tap the cards in order.",
            "Three thousand.", "Six hundred.", "Eight thousand."))
        for (engine in listOf(NarrationEngine.KOKORO, NarrationEngine.ANDROID)) {
            assertEquals(NarrationResumePoint(3, 1_537), remap(NarrationResumePoint(1, 1_537), before, after, engine))
            assertEquals(NarrationResumePoint(2, 418), remap(NarrationResumePoint(3, 418), before, after, engine))
        }
    }

    @Test fun equalWordsInDifferentBlocksAndFeedbackNeverExchangeBookmarks() {
        val before = script(listOf("Same text.", "Same text.", "Same text."), listOf(
            NarrationTextOrigin(0), NarrationTextOrigin(1), NarrationTextOrigin(1, true)),
            listOf(NarrationTextGate(1, 2, 3)))
        val after = script(listOf("Same text.", "New instruction.", "Same text.", "Same text."), listOf(
            NarrationTextOrigin(0), NarrationTextOrigin(1), NarrationTextOrigin(1), NarrationTextOrigin(1, true)),
            listOf(NarrationTextGate(1, 3, 4)))
        assertEquals(0, remap(NarrationResumePoint(0, 250), before, after).segment)
        assertEquals(2, remap(NarrationResumePoint(1, 550), before, after).segment)
        assertEquals(3, remap(NarrationResumePoint(2, 775), before, after).segment)
    }

    @Test fun repeatedSegmentsWithinOneBlockKeepTheirOccurrenceIdentity() {
        val before = script(listOf("Again.", "Another.", "Again."))
        val after = script(listOf("Instruction.", "Again.", "Again.", "Another."))
        assertEquals(NarrationResumePoint(2, 653), remap(NarrationResumePoint(2, 653), before, after))
    }

    @Test fun changedOrSplitRecordingFallsBackToItsOwnBlockWithoutInventingTimes() {
        val before = script(listOf("Earlier.", "Six hundred eight thousand three thousand."), listOf(
            NarrationTextOrigin(0), NarrationTextOrigin(1)))
        val after = script(listOf("Earlier.", "Order the numbers.", "Six hundred.", "Eight thousand.", "Three thousand."),
            listOf(NarrationTextOrigin(0)) + List(4) { NarrationTextOrigin(1) })
        assertEquals(NarrationResumePoint(1, 0), remap(NarrationResumePoint(1, 3_429), before, after))
    }

    @Test fun completedPageRetainsItsFinalOffsetWhenTheSameFinalRecordingRemainsLast() {
        val before = script(listOf("Question.", "Final sentence."))
        val after = script(listOf("Question.", "Secondary instruction.", "Final sentence."))
        assertEquals(NarrationResumePoint(2, 4_319, finished = true),
            remap(NarrationResumePoint(1, 4_319, finished = true), before, after))
        val extended = script(after.texts + "New ending.")
        assertEquals(NarrationResumePoint(2, 4_319),
            remap(NarrationResumePoint(1, 4_319, finished = true), before, extended))
    }

    @Test fun wholeAndPartialSuccessFeedbackCreditShiftOnlyWithTheSameFeedbackAudio() {
        val before = script(listOf("Question.", "Correct.", "This is why.", "After."), listOf(
            NarrationTextOrigin(0), NarrationTextOrigin(0, true), NarrationTextOrigin(0, true), NarrationTextOrigin(1)),
            listOf(NarrationTextGate(0, 1, 3)))
        val after = script(listOf("Question.", "Instruction.", "First card.", "Second card.", "Correct.", "This is why.", "After."),
            List(4) { NarrationTextOrigin(0) } + List(2) { NarrationTextOrigin(0, true) } + NarrationTextOrigin(1),
            listOf(NarrationTextGate(0, 4, 6)))
        assertEquals(NarrationResumePoint(6, 815, feedbackHeardThrough = 6),
            remap(NarrationResumePoint(3, 815, feedbackHeardThrough = 3), before, after))
        assertEquals(NarrationResumePoint(5, 100, feedbackHeardThrough = 5),
            remap(NarrationResumePoint(2, 100, feedbackHeardThrough = 2), before, after))
        assertEquals(0, remap(NarrationResumePoint(0, 1_100), before, after).feedbackHeardThrough)
        val changed = after.copy(texts = after.texts.map { if (it == "This is why.") "Different feedback." else it })
        assertEquals(0, remap(NarrationResumePoint(3, 815, feedbackHeardThrough = 3), before, changed).feedbackHeardThrough)
    }

    @Test fun anUnheardEarlierFeedbackGateCannotBeCreditedFromALaterMatchingGate() {
        val before = script(listOf("Question.", "Correct.", "After."), listOf(
            NarrationTextOrigin(1), NarrationTextOrigin(1, true), NarrationTextOrigin(2)),
            listOf(NarrationTextGate(1, 1, 2)))
        val after = script(listOf("Inserted question.", "Inserted feedback.", "Question.", "Correct.", "After."), listOf(
            NarrationTextOrigin(0), NarrationTextOrigin(0, true), NarrationTextOrigin(1),
            NarrationTextOrigin(1, true), NarrationTextOrigin(2)),
            listOf(NarrationTextGate(0, 1, 2), NarrationTextGate(1, 3, 4)))
        assertEquals(0, remap(NarrationResumePoint(2, 250, feedbackHeardThrough = 2), before, after).feedbackHeardThrough)
    }

    @Test fun missingProvenanceOrInvalidPositionDoesNotGuessAReassociation() {
        val original = script(listOf("Visible text."))
        assertNull(NarrationPresentationRemap.remap(NarrationResumePoint(), original.copy(origins = emptyList()), original,
            NarrationEngine.KOKORO))
        assertNull(NarrationPresentationRemap.remap(NarrationResumePoint(2, 10), original, original, NarrationEngine.KOKORO))
        assertNull(NarrationPresentationRemap.remap(NarrationResumePoint(0, -1), original, original, NarrationEngine.KOKORO))
    }
}
