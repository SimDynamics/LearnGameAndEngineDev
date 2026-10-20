package com.learngraphicsprogramming.app

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.random.Random

class StableLessonStateLayoutTest {
    private val original = listOf(
        LessonStatePage("p0", listOf("paragraph0", "prediction")),
        LessonStatePage("p1", listOf("paragraph1", "editor")),
        LessonStatePage("p2", listOf("math"))
    )

    @Test fun originalSlotsNeverChangeWhenEditorsAndPuzzlesMove() {
        val layout = StableLessonStateLayout(original, listOf(
            LessonStatePage("p1", listOf("editor", "prediction")),
            LessonStatePage("p0", listOf("paragraph0", "paragraph1")), original[2]
        ))
        assertEquals(LessonStatePosition(1, 1), layout.storagePosition(0, 0))
        assertEquals(LessonStatePosition(0, 1), layout.storagePosition(0, 1))
        assertEquals(LessonStatePosition(0, 0), layout.storagePosition(1, 0))
        assertEquals(LessonStatePosition(1, 0), layout.storagePosition(1, 1))
    }

    @Test fun bookmarkFollowsItsPageNotItsDisplayIndex() {
        val layout = StableLessonStateLayout(original, original.reversed())
        assertEquals(0, layout.currentPage(2))
        assertEquals(2, layout.storagePage(0))
        assertEquals(1, layout.currentPage(1))
    }

    @Test fun readingCoverageDoesNotCreditUnvisitedMaterialAfterReorder() {
        val layout = StableLessonStateLayout(original, original.reversed())
        val pages = layout.legacyReadPages(true, 0)
        val blocks = layout.legacyReadBlocks(true, 0)
        assertFalse(layout.pageRead(0, pages, blocks))
        assertTrue(layout.pageRead(2, pages, blocks))
        val (visitedPages, visitedBlocks) = layout.visited(0, pages, blocks)
        assertTrue(layout.pageRead(0, visitedPages, visitedBlocks))
        assertFalse(layout.pageRead(1, visitedPages, visitedBlocks))
        assertTrue(layout.pageRead(2, visitedPages, visitedBlocks))
    }

    @Test fun readingCoverageTravelsWithBlocksAcrossPageBoundaries() {
        val layout = StableLessonStateLayout(original, listOf(
            LessonStatePage("p0", listOf("paragraph0", "paragraph1")),
            LessonStatePage("p1", listOf("prediction", "editor")), original[2]))
        val pages = layout.legacyReadPages(true, 0)
        val blocks = layout.legacyReadBlocks(true, 0)
        assertFalse(layout.pageRead(0, pages, blocks))
    }

    @Test fun freshStudentHasNoInheritedReadCoverage() {
        val layout = StableLessonStateLayout(original, original)
        assertTrue(layout.legacyReadPages(false, 100).isEmpty())
        assertTrue(layout.legacyReadBlocks(false, 100).isEmpty())
    }

    @Test fun completedReadingRemainsCompletedAfterReordering() {
        val layout = StableLessonStateLayout(original, original.reversed())
        val pages = layout.legacyReadPages(true, 2)
        val blocks = layout.legacyReadBlocks(true, 2)
        assertTrue((0..2).all { layout.pageRead(it, pages, blocks) })
    }

    @Test fun duplicatesMissingAndUnknownIdentitiesAreRejected() {
        val cases = listOf(
            listOf(original[0], original[0], original[2]),
            listOf(original[0], LessonStatePage("p1", listOf("prediction", "editor")), original[2]),
            listOf(original[0], LessonStatePage("p1", listOf("paragraph1")), original[2]),
            listOf(original[0], LessonStatePage("p1", listOf("other", "editor")), original[2])
        )
        cases.forEach { invalid ->
            assertTrue(runCatching { StableLessonStateLayout(original, invalid) }.exceptionOrNull()
                is LessonStateLayoutException)
        }
    }

    @Test fun invalidCoordinatesNeverFallBackToSomeoneElsesSavedAnswer() {
        val layout = StableLessonStateLayout(original, original)
        listOf(-1 to 0, 3 to 0, 0 to -1, 0 to 2).forEach { (p, b) ->
            assertTrue(runCatching { layout.storagePosition(p, b) }.exceptionOrNull()
                is LessonStateLayoutException)
        }
    }

    @Test fun identityLineageIsIndependentOfDisplayOrder() {
        assertEquals(StableLessonStateLayout(original, original).lineage,
            StableLessonStateLayout(original, original.reversed()).lineage)
    }

    @Test fun progressKeepsItsHistoricalGroupsWhenSolvedAndPendingExercisesAreRedistributed() {
        val origin = listOf(LessonStatePage("p0", listOf("correct0", "correct1")),
            LessonStatePage("p1", listOf("pending0", "pending1")))
        val current = listOf(LessonStatePage("p1", listOf("correct0", "pending0")),
            LessonStatePage("p0", listOf("correct1", "pending1")))
        val layout = StableLessonStateLayout(origin, current)
        val correct = setOf("correct0", "correct1")
        val completed = layout.progressGroups.count { positions ->
            positions.all { current[it.page].blocks[it.block] in correct }
        }
        assertEquals(1, completed)
        assertEquals(2, layout.progressGroups.size)
        assertEquals(listOf(LessonStatePosition(0, 0), LessonStatePosition(1, 0)), layout.progressGroups[0])
    }

    @Test fun theoreticalProgressUsesOriginalCoverageIncludingEmptyPages() {
        val origin = listOf(LessonStatePage("p0", listOf("a", "b")),
            LessonStatePage("p1", listOf("c")), LessonStatePage("empty", emptyList()))
        val current = listOf(LessonStatePage("p1", listOf("a", "c")),
            LessonStatePage("empty", listOf("b")), LessonStatePage("p0", emptyList()))
        val layout = StableLessonStateLayout(origin, current)
        assertTrue(layout.progressPageRead(0, setOf("p0"), setOf("a", "b")))
        assertFalse(layout.progressPageRead(1, setOf("p0"), setOf("a", "b")))
        assertFalse(layout.progressPageRead(2, setOf("p0"), setOf("a", "b")))
        assertTrue(layout.progressPageRead(2, setOf("empty"), emptySet()))
        assertFalse(layout.progressPageRead(0, setOf("p0"), setOf("a")))
    }

    @Test fun originalAndCurrentCollectionsCannotBeMutatedAfterValidation() {
        val blocks = mutableListOf("x")
        val pages = mutableListOf(LessonStatePage("p", blocks))
        val layout = StableLessonStateLayout(pages, pages)
        blocks[0] = "y"
        pages.clear()
        assertEquals(LessonStatePosition(0, 0), layout.storagePosition(0, 0))
    }

    @Test fun randomizedPermutationsAlwaysResolveToTheSameOriginalAddresses() {
        val random = Random(9317)
        val expected = original.flatMapIndexed { p, page -> page.blocks.mapIndexed { b, id ->
            id to LessonStatePosition(p, b)
        }}.toMap()
        repeat(500) {
            val shuffled = expected.keys.shuffled(random)
            var offset = 0
            val displayed = original.shuffled(random).map { page ->
                val blocks = shuffled.subList(offset, offset + page.blocks.size).toList()
                offset += page.blocks.size
                page.copy(blocks = blocks)
            }
            val layout = StableLessonStateLayout(original, displayed)
            displayed.forEachIndexed { p, page -> page.blocks.forEachIndexed { b, id ->
                assertEquals(expected.getValue(id), layout.storagePosition(p, b))
            }}
        }
    }
}
