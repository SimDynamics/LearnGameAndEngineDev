package com.learngraphicsprogramming.app

import java.security.MessageDigest

/** A content error is never a reason to read another exercise's positional state. */
internal class LessonStateLayoutException(message: String) : IllegalStateException(message)

internal data class LessonStatePosition(val page: Int, val block: Int)
internal data class LessonStatePage(val id: String, val blocks: List<String>)

/**
 * Stable identities resolve to immutable storage slots. Keeping the original slots
 * preserves existing answers, code drafts and project histories without copying,
 * clearing or temporarily swapping student data during a content reorder.
 *
 * This first contract permits permutations, not replacing, adding or removing an
 * activity under an existing identity. The authoring tool also verifies content.
 */
internal class StableLessonStateLayout(
    origin: List<LessonStatePage>,
    current: List<LessonStatePage>
) {
    // Defensive copies: the JSON parser or author must not mutate a checked map.
    val origin: List<LessonStatePage> = origin.map { it.copy(blocks = it.blocks.toList()) }
    val current: List<LessonStatePage> = current.map { it.copy(blocks = it.blocks.toList()) }
    private val originPages: Map<String, Int>
    private val originBlocks: Map<String, LessonStatePosition>
    private val currentPages: Map<String, Int>
    /** Each historical progress page contains the CURRENT positions of its own blocks. */
    val progressGroups: List<List<LessonStatePosition>>
    val lineage: String

    init {
        checkLayout(this.origin.isNotEmpty(), "A state layout needs at least one page")
        checkLayout(this.origin.size <= 2048, "Too many state pages")
        validateIds(this.origin)
        validateIds(this.current)
        checkLayout(this.current.map { it.id }.toSet() == this.origin.map { it.id }.toSet(),
            "Reordering must preserve every page identity")
        checkLayout(this.current.flatMap { it.blocks }.toSet() == this.origin.flatMap { it.blocks }.toSet(),
            "Reordering must preserve every block identity")
        originPages = this.origin.mapIndexed { index, page -> page.id to index }.toMap()
        currentPages = this.current.mapIndexed { index, page -> page.id to index }.toMap()
        originBlocks = buildMap {
            this@StableLessonStateLayout.origin.forEachIndexed { p, page ->
                page.blocks.forEachIndexed { b, id -> put(id, LessonStatePosition(p, b)) }
            }
        }
        val displayedBlocks = buildMap {
            this@StableLessonStateLayout.current.forEachIndexed { p, page ->
                page.blocks.forEachIndexed { b, id -> put(id, LessonStatePosition(p, b)) }
            }
        }
        progressGroups = this.origin.map { page -> page.blocks.map(displayedBlocks::getValue) }
        // IDs are ASCII and cannot contain these framing characters.
        val framing = this.origin.joinToString("|") { it.id + ":" + it.blocks.joinToString(",") }
        lineage = MessageDigest.getInstance("SHA-256").digest(framing.toByteArray(Charsets.UTF_8))
            .joinToString("") { "%02x".format(it) }
    }

    fun storagePosition(page: Int, block: Int): LessonStatePosition {
        val currentPage = current.getOrNull(page)
            ?: throw LessonStateLayoutException("Unknown current page")
        val id = currentPage.blocks.getOrNull(block)
            ?: throw LessonStateLayoutException("Unknown current block")
        return originBlocks.getValue(id)
    }

    fun storagePage(currentPage: Int): Int = originPages.getValue(
        current.getOrNull(currentPage)?.id ?: throw LessonStateLayoutException("Unknown current page")
    )

    fun currentPage(storagePage: Int): Int {
        // The old reader already clamped a saved index after a content update.
        val page = origin[storagePage.coerceIn(0, origin.lastIndex)]
        return currentPages.getValue(page.id)
    }

    /** Seed only the pages the legacy reader could actually have visited. */
    fun legacyReadPages(started: Boolean, savedPage: Int): Set<String> =
        if (!started) emptySet() else origin.take(savedPage.coerceIn(0, origin.lastIndex) + 1)
            .mapTo(linkedSetOf()) { it.id }

    fun legacyReadBlocks(started: Boolean, savedPage: Int): Set<String> =
        if (!started) emptySet() else origin.take(savedPage.coerceIn(0, origin.lastIndex) + 1)
            .flatMapTo(linkedSetOf()) { it.blocks }

    fun pageRead(currentPage: Int, readPages: Set<String>, readBlocks: Set<String>): Boolean {
        val page = current.getOrNull(currentPage)
            ?: throw LessonStateLayoutException("Unknown current page")
        return pageRead(page, readPages, readBlocks)
    }

    fun progressPageRead(originalPage: Int, readPages: Set<String>, readBlocks: Set<String>): Boolean {
        val page = origin.getOrNull(originalPage)
            ?: throw LessonStateLayoutException("Unknown original progress page")
        return pageRead(page, readPages, readBlocks)
    }

    private fun pageRead(page: LessonStatePage, readPages: Set<String>, readBlocks: Set<String>): Boolean =
        if (page.blocks.isEmpty()) page.id in readPages else page.blocks.all { it in readBlocks }

    fun visited(currentPage: Int, readPages: Set<String>, readBlocks: Set<String>): Pair<Set<String>, Set<String>> {
        val page = current.getOrNull(currentPage)
            ?: throw LessonStateLayoutException("Unknown current page")
        return (readPages + page.id) to (readBlocks + page.blocks)
    }

    fun validateReadState(readPages: Set<String>, readBlocks: Set<String>) {
        checkLayout(readPages.all { it in originPages }, "Unknown saved page identity")
        checkLayout(readBlocks.all { it in originBlocks }, "Unknown saved block identity")
    }

    private fun validateIds(pages: List<LessonStatePage>) {
        checkLayout(pages.size <= 2048, "Too many state pages")
        val pageIds = pages.map { it.id }
        val blockIds = pages.flatMap { it.blocks }
        checkLayout(blockIds.size <= 100_000, "Too many state blocks")
        checkLayout((pageIds + blockIds).all { ID.matches(it) }, "Invalid stable identity")
        checkLayout(pageIds.size == pageIds.distinct().size, "Duplicate page identity")
        checkLayout(blockIds.size == blockIds.distinct().size, "Duplicate block identity")
    }

    companion object {
        private val ID = Regex("[A-Za-z0-9_-]{1,128}")
        private fun checkLayout(condition: Boolean, message: String) {
            if (!condition) throw LessonStateLayoutException(message)
        }
    }
}
