package com.learngraphicsprogramming.app.narration

/** Exact source projections for the ordered, already registered text views of one puzzle. */
internal object NarrationPuzzleWordMappings {
    private data class ViewPart(val target: Int, val start: Int, val end: Int,
                                val owners: List<ReadingTextRange>)
    private data class VisibleRun(
        val text: CharSequence,
        val parts: List<ViewPart>,
        val signatures: List<List<String>>
    )
    private data class Match(val endSegment: Int, val endView: Int,
                             val mappings: Map<Int, NarrationWordMapping>)

    fun forViews(
        block: Int,
        views: List<Pair<Int, CharSequence>>,
        segments: Map<Int, String>,
        language: NarrationLanguage,
        feedback: Boolean = false
    ): Map<Int, NarrationWordMapping> {
        if (views.isEmpty() || segments.isEmpty() || views.map { it.first }.distinct().size != views.size)
            return emptyMap()
        val ordered = segments.toSortedMap().entries.toList()
        val spoken = ordered.map { entry ->
            NarrationSpokenWords.technicalAtoms(entry.value, language).map { it.canonical }
        }
        val visibleRuns = mutableMapOf<Pair<Int, Int>, VisibleRun>()
        fun visibleRun(start: Int, end: Int) = visibleRuns.getOrPut(start to end) {
            val text = StringBuilder()
            val formulas = mutableListOf<ReadingTextRange>()
            val parts = (start..end).map { viewIndex ->
                if (text.isNotEmpty()) text.append(' ')
                val offset = text.length
                val source = views[viewIndex].second
                val projection = feedbackPresentation(source, language, feedback)
                text.append(projection.text)
                formulas += projection.formulas.map { ReadingTextRange(offset + it.start, offset + it.endExclusive) }
                ViewPart(views[viewIndex].first, offset, text.length, projection.owners)
            }
            val source = NarrationSourceProjection.Source(text.toString(), formulas)
            val signatures = mutableListOf(NarrationSpokenWords.technicalAtoms(source, language).map { it.canonical })
            for (restoreFormulas in if (formulas.isEmpty()) listOf(false) else listOf(false, true))
            for (mode in listOf("auto", "text", "inline", "block")) {
                val projection = NarrationSourceProjection.forLanguage(source, mode, language, restoreFormulas) ?: continue
                signatures += NarrationSpokenWords.technicalAtoms(projection.text, language).map { it.canonical }
            }
            VisibleRun(source, parts, signatures.filter { it.isNotEmpty() }.distinct())
        }

        var nextSegment = 0
        var nextView = 0
        val result = linkedMapOf<Int, NarrationWordMapping>()
        while (nextView < views.size && nextSegment < ordered.size) {
            var selected: Match? = null
            // Earliest source occurrence wins. Repeated options consume successive
            // whole runs instead of competing for the same segment.
            search@ for (start in nextSegment until ordered.size) {
                for (endView in nextView until views.size) {
                    val visible = visibleRun(nextView, endView)
                    if (visible.signatures.isEmpty()) continue
                    var candidates = visible.signatures
                    val accumulated = mutableListOf<String>()
                    for (end in start until ordered.size) {
                        accumulated += spoken[end]
                        candidates = candidates.filter { expected ->
                            accumulated.size <= expected.size &&
                                accumulated.indices.all { accumulated[it] == expected[it] }
                        }
                        if (candidates.isEmpty()) break
                        if (candidates.none { it.size == accumulated.size }) continue
                        val run = (start..end).associate { ordered[it].key to ordered[it].value }
                        // The existing mapper proves the complete correspondence,
                        // including pronunciation expansion and its source ownership.
                        val mapping = NarrationWordMappings.forBlock(block, visible.text, run, language)
                        val targeted = target(mapping, visible) ?: continue
                        if (targeted.isEmpty()) continue
                        selected = Match(end, endView, targeted)
                        break@search
                    }
                }
            }
            if (selected == null) {
                // Controls, rendered formula pieces or other unalignable representations
                // keep the conservative fallback; they never borrow another view's words.
                nextView++
            } else {
                result.putAll(selected.mappings)
                nextSegment = selected.endSegment + 1
                nextView = selected.endView + 1
            }
        }
        return result
    }

    private fun target(mapping: Map<Int, NarrationWordMapping>, visible: VisibleRun): Map<Int, NarrationWordMapping>? {
        val result = linkedMapOf<Int, NarrationWordMapping>()
        for ((segment, value) in mapping) {
            val targets = mutableListOf<Int>()
            val displayed = mutableListOf<ReadingTextRange>()
            for (range in value.displayed) {
                // A synthetic joiner or a view's surrounding spaces may straddle a
                // boundary. Actual glyphs must still belong to exactly one view.
                val owners = visible.parts.filter { part ->
                    val start = maxOf(range.start, part.start)
                    val end = minOf(range.endExclusive, part.end)
                    start < end && visible.text.substring(start, end).any { !it.isWhitespace() }
                }
                val owner = owners.singleOrNull() ?: return null
                targets += owner.target
                val localStart = maxOf(range.start, owner.start) - owner.start
                val localEnd = minOf(range.endExclusive, owner.end) - owner.start
                val original = NarrationSourceProjection.owner(owner.owners.subList(localStart, localEnd))
                if (original.endExclusive <= original.start) return null
                displayed += original
            }
            result[segment] = value.copy(displayed = displayed, targets = targets)
        }
        return result
    }

    /** Only the app-generated successful-feedback heading differs from the script. */
    private fun feedbackPresentation(displayed: CharSequence, language: NarrationLanguage,
                                     feedback: Boolean): NarrationSourceProjection.Projection {
        val original = NarrationSourceProjection.Projection(displayed.toString(),
            displayed.indices.map { ReadingTextRange(it, it + 1) }, NarrationSourceProjection.formulaRanges(displayed))
        val source = original.text
        val ordinary = { original }
        if (!feedback) return ordinary()
        val heading = listOf("✓ Correcto", "✓ Correct", "✓ ¡Correcto!").firstOrNull {
            source == it || source.startsWith("$it\n")
        } ?: return ordinary()
        val spoken = if (language == NarrationLanguage.SPANISH) "Correcto." else "Correct."
        val text = spoken + source.substring(heading.length)
        val owners = List(spoken.length) { NarrationSourceProjection.owner(original.owners.subList(0, heading.length)) } +
            original.owners.subList(heading.length, original.owners.size)
        return NarrationSourceProjection.Projection(text, owners, original.formulas.map {
            ReadingTextRange(it.start + spoken.length - heading.length, it.endExclusive + spoken.length - heading.length)
        })
    }
}
