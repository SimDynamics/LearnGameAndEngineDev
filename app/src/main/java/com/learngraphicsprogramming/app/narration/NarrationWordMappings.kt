package com.learngraphicsprogramming.app.narration

/** Projects lexical provenance onto all existing glyphs, without changing either text. */
internal object NarrationWordMappings {
    private const val OPENING = "([{«“‘¿¡"

    /**
     * A registered view can be only the question of an exercise whose narration also
     * contains options. Require one exact run of whole segments, never a word search
     * inside a sentence or a choice between repeated, ambiguous occurrences.
     */
    fun forView(blockIndex: Int, displayed: CharSequence,
                segments: Map<Int, String>): Map<Int, NarrationWordMapping> {
        val completeBlock = forBlock(blockIndex, displayed, segments)
        if (completeBlock.isNotEmpty()) return completeBlock
        val projected = projected(blockIndex, displayed, segments, allowSubset = true)
        if (projected.isNotEmpty()) return projected
        val visible = NarrationSpokenWords.lexemes(displayed).map { it.canonical }
        if (visible.isEmpty()) return emptyMap()
        val ordered = segments.toSortedMap().entries.toList()
        val spoken = ordered.map { entry -> NarrationSpokenWords.lexemes(entry.value).map { it.canonical } }
        var selected: Map<Int, String>? = null
        for (start in ordered.indices) {
            var cursor = 0
            for (end in start until ordered.size) {
                val current = spoken[end]
                if (current.isEmpty() || cursor + current.size > visible.size ||
                    current.indices.any { current[it] != visible[cursor + it] }) break
                cursor += current.size
                if (cursor == visible.size) {
                    if (selected != null) return emptyMap()
                    selected = (start..end).associate { ordered[it].key to ordered[it].value }
                    break
                }
            }
        }
        return selected?.let { forBlock(blockIndex, displayed, it) }.orEmpty()
    }

    fun forBlock(blockIndex: Int, displayed: CharSequence,
                 segments: Map<Int, String>): Map<Int, NarrationWordMapping> {
        val visible = NarrationSpokenWords.lexemes(displayed)
        val spoken = segments.toSortedMap().mapValues { NarrationSpokenWords.lexemes(it.value) }
        if (visible.isEmpty() || visible.map { it.canonical } != spoken.values.flatten().map { it.canonical })
            return projected(blockIndex, displayed, segments, allowSubset = false)

        // Closing punctuation belongs to the preceding word; opening punctuation belongs
        // to the following word. Every intervening glyph is covered, including C++'s ++.
        val boundaries = mutableListOf(0)
        for (index in 1 until visible.size) {
            val gapStart = visible[index - 1].endExclusive
            val gapEnd = visible[index].start
            val opening = (gapStart until gapEnd).firstOrNull {
                displayed[it] in OPENING ||
                    (displayed[it] == '"' && it > gapStart && displayed[it - 1].isWhitespace())
            }
            boundaries += opening ?: gapEnd
        }
        boundaries += displayed.length

        var cursor = 0
        return spoken.mapValues { (_, matches) ->
            val ranges = matches.indices.map { index ->
                ReadingTextRange(boundaries[cursor + index], boundaries[cursor + index + 1])
            }
            cursor += matches.size
            NarrationWordMapping(blockIndex,
                matches.map { ReadingTextRange(it.start, it.endExclusive) }, ranges)
        }
    }

    /** Source ranges come from actual normalization, never a search for similar words. */
    private fun projected(block: Int, displayed: CharSequence, segments: Map<Int, String>,
                          allowSubset: Boolean): Map<Int, NarrationWordMapping> {
        if (segments.isEmpty() || segments.values.none { it.startsWith(ManualEnglishSpeech.PREPARED) }) return emptyMap()
        val ordered = segments.toSortedMap().entries.toList()
        for (mode in listOf("auto", "text", "inline", "block")) {
            val projection = NarrationSourceProjection.spanish(displayed.toString(), mode) ?: continue
            val expected = NarrationSpokenWords.lexemes(projection.text)
            if (expected.isEmpty()) continue
            fun content(text: String) = text.filterNot { it.isWhitespace() || it == ManualEnglishSpeech.PREPARED }
            val sourceContent = content(projection.text)
            val candidates = mutableListOf<List<Map.Entry<Int, String>>>()
            for (start in if (allowSubset) ordered.indices else 0..0) {
                var accumulated = ""
                for (end in start until ordered.size) {
                    accumulated += content(ordered[end].value)
                    if (!sourceContent.startsWith(accumulated)) break
                    if (accumulated == sourceContent) { candidates += ordered.subList(start, end + 1); break }
                }
            }
            val selected = candidates.singleOrNull() ?: continue
            if (!allowSubset && selected.size != ordered.size) continue
            val spoken = selected.flatMap { NarrationSpokenWords.lexemes(it.value) }
            if (spoken.map { it.canonical } != expected.map { it.canonical }) continue
            val owners = expected.map { NarrationSourceProjection.owner(projection.owners.subList(it.start, it.endExclusive)) }
            if (owners.any { it.start >= it.endExclusive }) continue
            val groups = mutableListOf<ReadingTextRange>()
            for (range in owners) {
                val previous = groups.lastOrNull()
                if (previous != null && range.start < previous.start) return emptyMap()
                if (previous != null && range.start < previous.endExclusive)
                    groups[groups.lastIndex] = ReadingTextRange(previous.start, maxOf(previous.endExclusive, range.endExclusive))
                else groups += range
            }
            val boundaries = mutableListOf(0)
            for (index in 1 until groups.size) {
                val start = groups[index - 1].endExclusive
                val end = groups[index].start
                boundaries += (start until end).firstOrNull { displayed[it] in OPENING ||
                    (displayed[it] == '"' && it > start && displayed[it - 1].isWhitespace()) } ?: end
            }
            boundaries += displayed.length
            val ranges = owners.map { range ->
                val group = groups.indexOfFirst { range.start >= it.start && range.endExclusive <= it.endExclusive }
                if (group < 0) return emptyMap()
                ReadingTextRange(boundaries[group], boundaries[group + 1])
            }
            var cursor = 0
            return selected.associate { segment ->
                val lexemes = NarrationSpokenWords.lexemes(segment.value)
                val shown = ranges.subList(cursor, cursor + lexemes.size).toList()
                cursor += lexemes.size
                segment.key to NarrationWordMapping(block, lexemes.map { ReadingTextRange(it.start, it.endExclusive) }, shown)
            }
        }
        return emptyMap()
    }

}
