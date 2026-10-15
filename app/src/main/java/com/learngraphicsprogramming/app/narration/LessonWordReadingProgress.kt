package com.learngraphicsprogramming.app.narration

/** Offsets only: both sources remain owned by the existing script and TextView. */
data class NarrationWordMapping(val blockIndex: Int, val spoken: List<ReadingTextRange>,
                                val displayed: List<ReadingTextRange>)

/** Position-bound presentation: listened and explicitly skipped visual intervals stay distinct. */
internal class LessonWordReadingProgress {
    private data class Interval(val start: Long, val end: Long)
    private class Page(val mapping: Map<Int, NarrationWordMapping>) {
        val alignments = mutableMapOf<Int, NarrationWordAlignment>()
        val covered = mutableMapOf<Int, MutableList<Interval>>()
        val skipped = mutableMapOf<Int, MutableList<Interval>>()
        val unknown = mutableMapOf<Int, Long>()
        val completed = mutableSetOf<Int>()
        var active: Int? = null
        var from: Long? = null
        var lastPosition = 0L
        var lastClock = 0L
        var current: Pair<Int, Long>? = null
        var visible = true
        var observed = false
        fun stop() { active = null; from = null; current = null; lastClock = 0 }
        fun reset() { covered.clear(); skipped.clear(); unknown.clear(); completed.clear(); observed = false; stop() }
    }
    private val pages = mutableMapOf<String, Page>()

    fun bind(identity: String, mapping: Map<Int, NarrationWordMapping>, replace: Boolean) {
        val old = pages[identity]
        if (replace || old == null || old.mapping != mapping) pages[identity] = Page(mapping.toMap())
        else old.visible = true
    }

    fun update(event: NarrationPlaybackProgress) {
        val page = pages[event.pageIdentity] ?: return
        val segment = event.segmentIndex
        val position = event.positionMs * 24L
        when (event.phase) {
            NarrationPlaybackPhase.PREPARED -> {
                page.visible = true
                page.stop()
                if (segment == 0 && position == 0L) page.reset()
                else if (!page.observed) {
                    for (earlier in 0 until segment) page.unknown[earlier] = Long.MAX_VALUE
                    if (position > 0) page.unknown[segment] = position
                }
            }
            NarrationPlaybackPhase.ALIGNMENT_AVAILABLE -> {
                val alignment = event.alignment ?: return
                val mapping = page.mapping[segment] ?: return
                if (alignment.words.size != mapping.spoken.size || alignment.words.indices.any {
                    alignment.words[it].start != mapping.spoken[it].start ||
                        alignment.words[it].endExclusive != mapping.spoken[it].endExclusive
                }) return
                page.alignments[segment] = alignment
            }
            NarrationPlaybackPhase.PLAYING -> {
                page.visible = true
                page.observed = true
                page.active = segment
                page.from = position
                page.lastPosition = position
                page.lastClock = event.observedUptimeMs
                page.current = segment to position
            }
            NarrationPlaybackPhase.ADVANCED, NarrationPlaybackPhase.PAUSED -> {
                if (page.active == segment && page.from != null) {
                    val elapsed = (event.observedUptimeMs - page.lastClock).coerceAtLeast(0)
                    val plausible = position >= page.lastPosition && (event.observedUptimeMs == 0L ||
                        page.lastClock == 0L || position - page.lastPosition <=
                        ((elapsed * event.playbackRate.coerceIn(.5f, 2f) + 150) * 24).toLong())
                    if (plausible) {
                        cover(page, segment, requireNotNull(page.from), position)
                    } else page.from = position // An unexplained jump is never listening evidence.
                    page.lastPosition = position
                    page.lastClock = event.observedUptimeMs
                    page.current = segment to position
                }
                if (event.phase == NarrationPlaybackPhase.PAUSED) {
                    page.active = null; page.from = null
                }
            }
            NarrationPlaybackPhase.SEGMENT_COMPLETED -> {
                if (page.active == segment && page.from != null) {
                    cover(page, segment, requireNotNull(page.from), Long.MAX_VALUE)
                    if (page.covered[segment].orEmpty().any { it.start == 0L && it.end == Long.MAX_VALUE })
                        page.completed += segment
                }
                page.stop()
            }
            NarrationPlaybackPhase.POSITION_CHANGED -> {
                // Replaying a passage makes its future pending again. These intervals are
                // presentation only; the reader's separate feedback permissions are untouched.
                trimToPosition(page, segment, position)
                event.manualForwardFrom?.let { from ->
                    if (from.segment in 0..segment && from.positionMs >= 0 &&
                        (from.segment < segment || from.positionMs < event.positionMs)) {
                        for (index in from.segment..segment) {
                            val start = if (index == from.segment) from.positionMs * 24L else 0L
                            val end = if (index == segment && !event.manualForwardToEnd) position else Long.MAX_VALUE
                            cover(page.skipped, index, start, end)
                        }
                    }
                }
                page.stop()
            }
            NarrationPlaybackPhase.RESTARTED -> { page.reset(); page.visible = true }
            NarrationPlaybackPhase.BUFFERING -> page.current = null
            NarrationPlaybackPhase.FINISHED -> {
                page.stop()
                // A restored completed page has no observable history; retain normal presentation.
                if (page.covered.isEmpty()) for (i in 0..segment) page.unknown[i] = Long.MAX_VALUE
            }
            NarrationPlaybackPhase.STOPPED -> { page.stop(); page.visible = false }
            NarrationPlaybackPhase.UNAVAILABLE -> { page.stop(); page.visible = false }
        }
    }

    private fun trimToPosition(page: Page, segment: Int, position: Long) {
        fun trim(intervals: MutableMap<Int, MutableList<Interval>>) {
            intervals.keys.removeAll { it > segment }
            val current = intervals[segment] ?: return
            val prefix = current.mapNotNull { interval ->
                val end = minOf(interval.end, position)
                if (end > interval.start) Interval(interval.start, end) else null
            }
            if (prefix.isEmpty()) intervals.remove(segment)
            else { current.clear(); current.addAll(prefix) }
        }
        trim(page.covered)
        trim(page.skipped)
        page.completed.removeAll { it >= segment }
        page.unknown.keys.removeAll { it > segment }
        page.unknown[segment]?.let { prefix ->
            if (position == 0L) page.unknown.remove(segment)
            else page.unknown[segment] = minOf(prefix, position)
        }
    }

    private fun cover(page: Page, segment: Int, start: Long, end: Long) {
        cover(page.covered, segment, start, end)
    }

    private fun cover(intervals: MutableMap<Int, MutableList<Interval>>, segment: Int, start: Long, end: Long) {
        if (end <= start) return
        val values = intervals.getOrPut(segment) { mutableListOf() }
        val sorted = (values + Interval(start, end)).sortedBy { it.start }
        values.clear()
        sorted.forEach { item ->
            val last = values.lastOrNull()
            if (last != null && item.start <= last.end)
                values[values.lastIndex] = Interval(last.start, maxOf(last.end, item.end))
            else values += item
        }
    }

    private fun visuallyComplete(page: Page, segment: Int): Boolean {
        if (segment in page.completed) return true
        var through = 0L
        for (interval in (page.covered[segment].orEmpty() + page.skipped[segment].orEmpty()).sortedBy { it.start }) {
            if (interval.start > through) break
            through = maxOf(through, interval.end)
        }
        return through == Long.MAX_VALUE
    }

    /** Whole-segment visual coverage also supports views that cannot expose safe word ranges. */
    fun visuallyCompletedSegments(identity: String): Set<Int> {
        val page = pages[identity] ?: return emptySet()
        if (!page.visible) return emptySet()
        return (page.covered.keys + page.skipped.keys + page.completed)
            .filterTo(mutableSetOf()) { visuallyComplete(page, it) }
    }

    /** Only pending glyph ranges are returned; no TextView content is retained or rewritten. */
    fun unread(identity: String): Map<Int, List<ReadingTextRange>> {
        val page = pages[identity] ?: return emptyMap()
        if (!page.visible) return emptyMap()
        val result = mutableMapOf<Int, MutableList<ReadingTextRange>>()
        page.mapping.forEach { (segment, mapping) ->
            val ranges = result.getOrPut(mapping.blockIndex) { mutableListOf() }
            val alignment = page.alignments[segment]
            val skipped = page.skipped[segment].orEmpty()
            // An unsupported alignment still has a reliable whole-segment boundary.
            // This union changes presentation only; skipped audio never becomes heard audio.
            val knownComplete = visuallyComplete(page, segment)
            mapping.displayed.forEachIndexed { index, displayed ->
                val word = alignment?.words?.getOrNull(index)
                val unknown = page.unknown[segment]
                val ordinary = knownComplete || unknown == Long.MAX_VALUE ||
                    (word != null && (
                    (unknown != null && word.startSample < unknown) ||
                    skipped.any { it.start < word.endSample && it.end > word.startSample } ||
                    page.covered[segment].orEmpty().any { it.start <= word.startSample && it.end >= word.endSample } ||
                    page.current?.let { it.first == segment && it.second >= word.startSample && it.second < word.endSample } == true))
                if (!ordinary) ranges += displayed
            }
        }
        return result
    }

    fun unavailable(identity: String) { pages[identity]?.let { it.stop(); it.visible = false } }
    fun newController() = pages.values.forEach { it.stop(); it.visible = false }
    fun forget(identity: String) { pages.remove(identity) }
    fun clear() = pages.clear()
}
