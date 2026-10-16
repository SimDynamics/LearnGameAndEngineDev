package com.learngraphicsprogramming.app.narration

/** Reuses a bounded bookmark when only the runtime presentation of a page changes. */
internal object NarrationPresentationRemap {
    private data class Part(val text: String, val origin: NarrationTextOrigin, val occurrence: Int)

    fun remap(point: NarrationResumePoint, previous: LessonNarrationScript,
              current: LessonNarrationScript, engine: NarrationEngine): NarrationResumePoint? {
        val before = parts(previous, engine) ?: return null
        val after = parts(current, engine) ?: return null
        val source = before.getOrNull(point.segment) ?: return null
        if (after.isEmpty() || point.positionMs < 0) return null
        val exact = after.indexOf(source)
        val destination = if (exact >= 0) exact else fallback(source.origin, after)
        return NarrationResumePoint(destination,
            if (exact >= 0) point.positionMs else 0L,
            finished = point.finished && point.segment == before.lastIndex && exact == after.lastIndex,
            feedbackHeardThrough = heardThrough(point.feedbackHeardThrough, previous, current, before, after, engine))
    }

    private fun parts(script: LessonNarrationScript, engine: NarrationEngine): List<Part>? {
        if (script.origins.size != script.texts.size || script.origins.any {
            it.blockIndex < -1 || (it.blockIndex == -1 && it.feedback)
        }) return null
        val occurrences = mutableMapOf<Pair<String, NarrationTextOrigin>, Int>()
        return script.texts.indices.flatMap { index ->
            val origin = script.origins[index]
            val sentences = if (engine == NarrationEngine.ANDROID)
                NarrationSegments.prepare(listOf(script.texts[index]), 250)
            else NarrationSegments.sentences(listOf(script.texts[index]))
            sentences.map { text ->
                val key = text to origin
                val occurrence = occurrences[key] ?: 0
                occurrences[key] = occurrence + 1
                Part(text, origin, occurrence)
            }
        }
    }

    private fun fallback(origin: NarrationTextOrigin, current: List<Part>): Int {
        // A changed or split recording has no reliable millisecond correspondence.
        // Start its own original block rather than inventing a duration conversion.
        val sameBlock = current.indexOfFirst { it.origin.blockIndex == origin.blockIndex && !it.origin.feedback }
        if (sameBlock >= 0) return sameBlock
        val previousBlock = current.filter { !it.origin.feedback && it.origin.blockIndex <= origin.blockIndex }
            .maxOfOrNull { it.origin.blockIndex }
        return if (previousBlock == null) 0 else current.indexOfFirst {
            it.origin.blockIndex == previousBlock && !it.origin.feedback
        }
    }

    private fun heardThrough(heard: Int, previous: LessonNarrationScript, current: LessonNarrationScript,
                             before: List<Part>, after: List<Part>, engine: NarrationEngine): Int {
        val oldGates = NarrationScriptSegments.gates(previous.texts, engine, previous.gates).associateBy { it.blockIndex }
        val newGates = NarrationScriptSegments.gates(current.texts, engine, current.gates)
        var preserved = 0
        for (gate in newGates) {
            val old = oldGates[gate.blockIndex] ?: return preserved
            if (heard <= old.start) return preserved
            val oldSpeech = before.subList(old.start, old.endExclusive).map { it.text }
            val newSpeech = after.subList(gate.start, gate.endExclusive).map { it.text }
            if (oldSpeech != newSpeech) return preserved
            val count = (heard - old.start).coerceAtMost(oldSpeech.size)
            preserved = gate.start + count
            if (count < oldSpeech.size) return preserved
        }
        return preserved
    }
}
