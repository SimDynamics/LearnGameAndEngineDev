package com.learngraphicsprogramming.app.narration

import java.text.Normalizer
import java.util.Locale

/** Projects lexical provenance onto all existing glyphs, without changing either text. */
internal object NarrationWordMappings {
    private val words = Regex("\\p{L}[\\p{L}\\p{M}]*")
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
        val visible = words.findAll(displayed).map { canonical(it.value) }.toList()
        if (visible.isEmpty()) return emptyMap()
        val ordered = segments.toSortedMap().entries.toList()
        val spoken = ordered.map { entry -> words.findAll(entry.value).map { canonical(it.value) }.toList() }
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
        val visible = words.findAll(displayed).toList()
        val spoken = segments.toSortedMap().mapValues { words.findAll(it.value).toList() }
        if (visible.isEmpty() || visible.map { canonical(it.value) } !=
            spoken.values.flatten().map { canonical(it.value) }) return emptyMap()

        // Closing punctuation belongs to the preceding word; opening punctuation belongs
        // to the following word. Every intervening glyph is covered, including C++'s ++.
        val boundaries = mutableListOf(0)
        for (index in 1 until visible.size) {
            val gapStart = visible[index - 1].range.last + 1
            val gapEnd = visible[index].range.first
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
                matches.map { ReadingTextRange(it.range.first, it.range.last + 1) }, ranges)
        }
    }

    private fun canonical(value: String) =
        Normalizer.normalize(value, Normalizer.Form.NFC).lowercase(Locale.ROOT)
}
