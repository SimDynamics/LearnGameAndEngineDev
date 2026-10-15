package com.learngraphicsprogramming.app.narration

import java.security.MessageDigest

/** Only provenance is retained; the displayed text remains owned by the lesson views. */
data class NarrationReadingPlan(val origins: List<NarrationTextOrigin>, val signature: String)

/** Expand provenance with the exact segmentation used by the existing audio controller. */
fun LessonNarrationScript.readingPlan(engine: NarrationEngine): NarrationReadingPlan? {
    if (texts.isEmpty() || origins.size != texts.size ||
        origins.any { it.blockIndex < -1 || (it.feedback && it.blockIndex < 0) }) return null
    fun split(items: List<String>) = if (engine == NarrationEngine.ANDROID) {
        NarrationSegments.prepare(items, 250)
    } else NarrationSegments.sentences(items)
    return runCatching {
        val prepared = split(texts)
        val expandedText = mutableListOf<String>()
        val expandedOrigins = mutableListOf<NarrationTextOrigin>()
        texts.forEachIndexed { index, text ->
            val parts = split(listOf(text))
            expandedText += parts
            expandedOrigins += List(parts.size) { origins[index] }
        }
        if (prepared.isEmpty() || expandedText != prepared) return null
        val digest = MessageDigest.getInstance("SHA-256")
        fun field(value: String) {
            val bytes = value.toByteArray(Charsets.UTF_8)
            digest.update(byteArrayOf((bytes.size ushr 24).toByte(), (bytes.size ushr 16).toByte(),
                (bytes.size ushr 8).toByte(), bytes.size.toByte()))
            digest.update(bytes)
        }
        field(engine.name)
        texts.forEachIndexed { index, text ->
            field(text)
            field(origins[index].blockIndex.toString())
            field(origins[index].feedback.toString())
        }
        NarrationReadingPlan(expandedOrigins.toList(), digest.digest().joinToString("") { "%02x".format(it) })
    }.getOrNull()
}

/**
 * Ephemeral reading presentation. Actually heard audio and explicit visual skips stay
 * distinct; neither a generated WAV nor a visual skip completes educational feedback.
 * No educational completion or narration bookmark is written here.
 */
class LessonReadingProgressModel {
    private class Page(val plan: NarrationReadingPlan, val playbackKey: String?) {
        val heard = mutableSetOf<Int>()
        val uncertain = mutableSetOf<Int>()
        var generation = Long.MIN_VALUE
        var available = true
        var canReactivateOnBind = false
        var eligibleSegment: Int? = null
        var lastAudibleSegment: Int? = null
        var currentSegment: Int? = null

        fun cancelCoverage() {
            eligibleSegment = null
            lastAudibleSegment = null
            currentSegment = null
        }

        fun allBodyHeard(): Boolean = plan.origins.indices
            .filter { !plan.origins[it].feedback }.all { it in heard }
    }

    private val pages = mutableMapOf<String, Page>()
    private val words = LessonWordReadingProgress()

    fun bind(pageIdentity: String, plan: NarrationReadingPlan, playbackKey: String? = null,
             wordMappings: Map<Int, NarrationWordMapping> = emptyMap()) {
        val previous = pages[pageIdentity]
        if (previous != null && previous.plan.signature == plan.signature && previous.plan.origins == plan.origins &&
            previous.playbackKey == playbackKey) {
            words.bind(pageIdentity, wordMappings, replace = false)
            // Reattaching cached views keeps both a paused current block and genuine evidence.
            if (previous.canReactivateOnBind) {
                previous.available = valid(plan)
                previous.canReactivateOnBind = false
            }
            return
        }
        pages[pageIdentity] = Page(plan.copy(origins = plan.origins.toList()), playbackKey)
            .apply { available = valid(plan) }
        words.bind(pageIdentity, wordMappings, replace = true)
    }

    fun update(event: NarrationPlaybackProgress) {
        val page = pages[event.pageIdentity] ?: return
        if (event.generation < page.generation) return
        if (!valid(page.plan) || event.segmentCount != page.plan.origins.size ||
            event.segmentIndex !in page.plan.origins.indices ||
            (page.playbackKey != null && event.playbackKey != page.playbackKey) ||
            event.positionMs < 0L) {
            page.available = false
            page.canReactivateOnBind = false
            page.cancelCoverage()
            words.unavailable(event.pageIdentity)
            return
        }
        if (event.generation > page.generation) {
            page.generation = event.generation
            page.cancelCoverage()
        }
        page.canReactivateOnBind = false
        words.update(event)
        when (event.phase) {
            NarrationPlaybackPhase.ADVANCED, NarrationPlaybackPhase.ALIGNMENT_AVAILABLE -> Unit
            NarrationPlaybackPhase.PREPARED -> {
                page.available = true
                // The existing reader starts at zero after a bookmark is evicted. Its
                // visual state follows that recovery; it must not retain a fourth position.
                if (event.segmentIndex == 0 && event.positionMs == 0L) {
                    page.heard.clear()
                    page.uncertain.clear()
                } else {
                    // A restored bookmark may include seeks. Without in-memory hearing
                    // evidence the recovered prefix stays ordinary, never falsely unread/read.
                    val through = event.segmentIndex - if (event.positionMs == 0L) 1 else 0
                    page.uncertain += (0..through).filter { it !in page.heard }
                }
                page.cancelCoverage()
            }
            NarrationPlaybackPhase.BUFFERING -> {
                page.available = true
                if (page.eligibleSegment != event.segmentIndex) page.eligibleSegment = null
                page.currentSegment = null
            }
            NarrationPlaybackPhase.PLAYING -> {
                page.available = true
                if (event.positionMs == 0L) page.eligibleSegment = event.segmentIndex
                else if (page.eligibleSegment != event.segmentIndex) page.eligibleSegment = null
                page.lastAudibleSegment = event.segmentIndex
                page.currentSegment = event.segmentIndex.takeUnless { page.plan.origins[it].feedback }
            }
            NarrationPlaybackPhase.PAUSED -> {
                page.available = true
                if (page.lastAudibleSegment != event.segmentIndex) page.cancelCoverage()
                else page.currentSegment = event.segmentIndex.takeUnless { page.plan.origins[it].feedback }
            }
            NarrationPlaybackPhase.POSITION_CHANGED -> {
                page.available = true
                // Keep block fallback consistent with the current visual cursor too.
                // These sets never grant puzzle permissions or persist reader positions.
                page.heard.removeAll { it >= event.segmentIndex }
                page.uncertain.removeAll { it >= event.segmentIndex }
                page.cancelCoverage()
            }
            NarrationPlaybackPhase.SEGMENT_COMPLETED -> {
                page.available = true
                if (page.eligibleSegment == event.segmentIndex &&
                    page.lastAudibleSegment == event.segmentIndex &&
                    !page.plan.origins[event.segmentIndex].feedback) page.heard += event.segmentIndex
                page.cancelCoverage()
            }
            NarrationPlaybackPhase.RESTARTED -> {
                page.available = true
                page.heard.clear()
                page.uncertain.clear()
                page.cancelCoverage()
            }
            NarrationPlaybackPhase.FINISHED -> {
                page.cancelCoverage()
                // A deliberate skip can complete the visual body without accrediting hearing.
                val visual = words.visuallyCompletedSegments(event.pageIdentity)
                page.available = page.allBodyHeard() || page.plan.origins.indices
                    .filter { !page.plan.origins[it].feedback }.all { it in page.heard || it in visual }
            }
            NarrationPlaybackPhase.STOPPED -> {
                page.available = false
                page.canReactivateOnBind = true
                page.cancelCoverage()
            }
            NarrationPlaybackPhase.UNAVAILABLE -> {
                page.available = false
                page.cancelCoverage()
            }
        }
    }

    fun states(pageIdentity: String): Map<Int, ReadingBlockState> {
        val page = pages[pageIdentity] ?: return emptyMap()
        if (!page.available) return emptyMap()
        val body = page.plan.origins.indices.filter { !page.plan.origins[it].feedback }
            .groupBy { page.plan.origins[it].blockIndex }
        val current = page.currentSegment?.let { page.plan.origins[it].blockIndex }
        val visual = words.visuallyCompletedSegments(pageIdentity)
        return body.mapNotNull { (block, segments) ->
            val state = when {
                block == current -> ReadingBlockState.CURRENT
                segments.all { it in page.heard || it in visual } -> ReadingBlockState.READ
                segments.any { it in page.uncertain } -> null
                else -> ReadingBlockState.UNREAD
            }
            state?.let { block to it }
        }.toMap()
    }

    /** The Activity rejects callbacks from the previous controller before forwarding events. */
    fun newController() {
        words.newController()
        pages.values.forEach {
            it.generation = Long.MIN_VALUE
            it.available = false
            it.canReactivateOnBind = true
            it.cancelCoverage()
        }
    }

    fun unreadWords(pageIdentity: String) = words.unread(pageIdentity)

    fun clear() { pages.clear(); words.clear() }

    fun forget(pageIdentity: String) { pages.remove(pageIdentity); words.forget(pageIdentity) }

    private fun valid(plan: NarrationReadingPlan) = plan.signature.isNotBlank() &&
        plan.origins.isNotEmpty() && plan.origins.all { it.blockIndex >= -1 && (!it.feedback || it.blockIndex >= 0) }
}
