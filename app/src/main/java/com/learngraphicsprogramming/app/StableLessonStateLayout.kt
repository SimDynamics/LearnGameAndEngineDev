package com.learngraphicsprogramming.app

import java.security.MessageDigest

/** A content error is never a reason to read another exercise's positional state. */
internal class LessonStateLayoutException(message: String) : IllegalStateException(message)

internal data class LessonStatePosition(val page: Int, val block: Int)
internal data class LessonStatePage(val id: String, val blocks: List<String>)
internal data class LessonStateFamily(
    val sha256: String, val origin: List<LessonStatePage>, val signatures: Map<String, String>
)
internal data class LessonStateAddition(
    val id: String, val sha256: String, val storage: LessonStatePosition, val progressPage: String
)

/**
 * Stable identities resolve to immutable storage slots. Keeping the original slots
 * preserves existing answers, code drafts and project histories without copying,
 * clearing or temporarily swapping student data during a content reorder.
 *
 * Version one permits permutations only. Version two also names historical locale
 * families and explicitly allocates isolated addresses for new activities.
 */
internal class StableLessonStateLayout(
    origin: List<LessonStatePage>,
    current: List<LessonStatePage>,
    val version: Int = 1,
    val locale: String? = null,
    family: Map<String, LessonStateFamily> = emptyMap(),
    additions: List<LessonStateAddition> = emptyList()
) {
    // Defensive copies: the JSON parser or author must not mutate a checked map.
    val origin: List<LessonStatePage> = origin.map { it.copy(blocks = it.blocks.toList()) }
    val current: List<LessonStatePage> = current.map { it.copy(blocks = it.blocks.toList()) }
    val family = family.mapValues { (_, record) -> record.copy(
        origin = record.origin.map { it.copy(blocks = it.blocks.toList()) },
        signatures = record.signatures.toMap()) }.toMap()
    val additions = additions.toList()
    private val originPages: Map<String, Int>
    private val originBlocks: Map<String, LessonStatePosition>
    private val currentPages: Map<String, Int>
    /** Each historical progress page contains the CURRENT positions of its own blocks. */
    val progressGroups: List<List<LessonStatePosition>>
    val lineage: String
    val legacyLineage: String
    private val progressPages: List<LessonStatePage>

    init {
        checkLayout(this.origin.isNotEmpty(), "A state layout needs at least one page")
        checkLayout(this.origin.size <= 2048, "Too many state pages")
        checkLayout(version in 1..2, "Unsupported state layout version")
        checkLayout(if (version == 1) locale == null && this.family.isEmpty() && this.additions.isEmpty()
            else locale != null && locale in LOCALES && this.family.keys.all { it in LOCALES } && locale in this.family,
            "Invalid state layout family")
        validateIds(this.origin)
        validateIds(this.current)
        this.family.values.forEach { record ->
            validateIds(record.origin)
            checkLayout(record.origin.isNotEmpty() && HASH.matches(record.sha256), "Invalid historical family")
            checkLayout(record.signatures.keys == record.origin.flatMap { it.blocks }.toSet() &&
                record.signatures.values.all(HASH::matches), "Invalid family signatures")
        }
        if (version == 2) checkLayout(this.family.getValue(locale!!).origin == this.origin,
            "The selected family origin changed")
        val originalIds = this.origin.flatMap { it.blocks }.toSet()
        val addedIds = this.additions.map { it.id }
        checkLayout(addedIds.size == addedIds.distinct().size && addedIds.none { it in originalIds } &&
            addedIds.all(ID::matches), "Duplicate or invalid new identity")
        val reservedPage = this.family.values.maxOfOrNull { it.origin.size } ?: this.origin.size
        checkLayout(this.additions.map { it.storage }.distinct().size == this.additions.size &&
            this.additions.all { HASH.matches(it.sha256) && it.storage.page in reservedPage..1_000_000 &&
                it.storage.block in 0..100_000 && this.origin.any { page -> page.id == it.progressPage } },
            "Invalid or colliding new storage slot")
        checkLayout(this.current.map { it.id }.toSet() == this.origin.map { it.id }.toSet(),
            "Reordering must preserve every page identity")
        checkLayout(this.current.flatMap { it.blocks }.toSet() == originalIds + addedIds,
            "Reordering must preserve every block identity")
        originPages = this.origin.mapIndexed { index, page -> page.id to index }.toMap()
        currentPages = this.current.mapIndexed { index, page -> page.id to index }.toMap()
        originBlocks = buildMap {
            this@StableLessonStateLayout.origin.forEachIndexed { p, page ->
                page.blocks.forEachIndexed { b, id -> put(id, LessonStatePosition(p, b)) }
            }
            this@StableLessonStateLayout.additions.forEach { put(it.id, it.storage) }
        }
        val displayedBlocks = buildMap {
            this@StableLessonStateLayout.current.forEachIndexed { p, page ->
                page.blocks.forEachIndexed { b, id -> put(id, LessonStatePosition(p, b)) }
            }
        }
        progressPages = this.origin.map { page -> page.copy(blocks = page.blocks +
            this.additions.filter { it.progressPage == page.id }.map { it.id }) }
        progressGroups = progressPages.map { page -> page.blocks.map(displayedBlocks::getValue) }
        // New activities do not change the historical lineage or reuse old addresses.
        fun framing(pages: List<LessonStatePage>) = pages.joinToString("|") {
            it.id + ":" + it.blocks.joinToString(",") }
        legacyLineage = sha(framing(this.origin))
        lineage = if (version == 1) legacyLineage else sha("v2/$locale/" +
            this.family.toSortedMap().entries.joinToString(";") { (language, record) ->
                language + ":" + record.sha256 + ":" + framing(record.origin) + ":" +
                    record.signatures.toSortedMap().entries.joinToString(",") { it.key + "=" + it.value }
            })
    }

    fun storagePosition(page: Int, block: Int): LessonStatePosition {
        val currentPage = current.getOrNull(page)
            ?: throw LessonStateLayoutException("Unknown current page")
        val id = currentPage.blocks.getOrNull(block)
            ?: throw LessonStateLayoutException("Unknown current block")
        return originBlocks.getValue(id)
    }

    fun additionNamespace(page: Int, block: Int): String? {
        val id = current.getOrNull(page)?.blocks?.getOrNull(block)
            ?: throw LessonStateLayoutException("Unknown current block")
        return additions.firstOrNull { it.id == id }?.let { sha("$lineage/${it.id}") }
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
        val page = progressPages.getOrNull(originalPage)
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
        private val HASH = Regex("[0-9a-f]{64}")
        private val LOCALES = setOf("es", "en", "fr")
        private fun sha(text: String) = MessageDigest.getInstance("SHA-256")
            .digest(text.toByteArray(Charsets.UTF_8)).joinToString("") { "%02x".format(it) }
        private fun checkLayout(condition: Boolean, message: String) {
            if (!condition) throw LessonStateLayoutException(message)
        }
    }
}
