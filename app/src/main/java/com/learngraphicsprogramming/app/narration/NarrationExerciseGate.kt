package com.learngraphicsprogramming.app.narration

/** [start, endExclusive) contains only the successful feedback, never the question. */
data class NarrationFeedbackGate(val blockIndex: Int, val start: Int, val endExclusive: Int)
data class NarrationGatePosition(val segment: Int, val positionMs: Long = 0L)

/** Playback permission is independent of generation: all sentences can be synthesized. */
internal class NarrationExerciseGate(
    private val count: Int,
    private val gates: List<NarrationFeedbackGate>,
    correctBlocks: Set<Int> = emptySet(),
    heardThrough: Int = 0
) {
    private var correct = correctBlocks.toSet()

    /** Explicit answer priority is playback permission, NOT evidence of hearing. */
    private data class Priority(val blockIndex: Int, var allowedThrough: Int)
    private var priority: Priority? = null
    var heardThrough: Int = heardThrough.coerceIn(0, count)
        private set

    init {
        require(count >= 0)
        require(gates.all { it.start >= 0 && it.start < it.endExclusive && it.endExclusive <= count })
        require(gates.zipWithNext().all { (a,b) -> a.endExclusive <= b.start })
        require(gates.map { it.blockIndex }.distinct().size == gates.size)
        invalidateUnsolved()
    }

    fun updateCorrect(blocks: Set<Int>) {
        correct = blocks.toSet()
        if (priority?.blockIndex !in correct) priority = null
        invalidateUnsolved()
    }
    fun restoreHeard(value: Int) {
        heardThrough = value.coerceIn(0, count)
        invalidateUnsolved()
    }
    fun restart() {
        heardThrough = 0
        priority = null
    }
    fun hasExercise(blockIndex: Int): Boolean = gates.any { it.blockIndex == blockIndex }

    /** Incorrect feedback may use this source boundary for visual-only skipping. */
    fun feedbackStart(blockIndex: Int): NarrationGatePosition? =
        gates.firstOrNull { it.blockIndex == blockIndex }?.let { NarrationGatePosition(it.start) }

    /** A newly correct answer takes immediate priority over every earlier
     * option or explanation. Do not advance heardThrough for skipped material.
     */
    fun prioritizeCorrect(blockIndex: Int): NarrationGatePosition? {
        if (blockIndex !in correct) return null
        val gate = gates.firstOrNull { it.blockIndex == blockIndex } ?: return null
        priority = Priority(blockIndex, gate.start)
        return NarrationGatePosition(gate.start)
    }

    /** A paused prioritized page retains the same one of three bookmarks,
     * including when earlier success feedback was skipped deliberately.
     */
    fun restoreAnsweredPosition(position: NarrationGatePosition) {
        if (priority != null || clamp(position) == position) return
        val gate = gates.lastOrNull { it.blockIndex in correct && it.start <= position.segment }
            ?: return
        val subsequentUnsolved = gates.drop(gates.indexOf(gate) + 1)
            .firstOrNull { it.blockIndex !in correct }
        if (subsequentUnsolved != null && position.segment >= subsequentUnsolved.start) return
        priority = Priority(gate.blockIndex, maxOf(gate.start, position.segment))
    }

    /** Skip a deliberately answered question's pending options, but never
     * bypass a previous unsolved puzzle or unheard successful feedback.
     */
    fun answerDestination(blockIndex: Int, from: NarrationGatePosition): NarrationGatePosition? {
        val start = gates.firstOrNull { it.blockIndex == blockIndex }?.start ?: return null
        if (from.segment >= start) return null
        val permitted = clamp(NarrationGatePosition(start))
        return permitted.takeIf { it.segment > from.segment }
    }

    private fun invalidateUnsolved() {
        gates.firstOrNull { it.blockIndex !in correct && it.start < heardThrough }?.let {
            heardThrough = it.start
        }
    }
    private fun activeGates(): List<NarrationFeedbackGate> {
        val current = priority ?: return gates
        val first = gates.indexOfFirst { it.blockIndex == current.blockIndex }
        return if (first < 0) gates else gates.drop(first)
    }

    fun boundary(): NarrationFeedbackGate? = activeGates().firstOrNull {
        it.blockIndex !in correct ||
            (priority?.allowedThrough ?: heardThrough) < it.endExclusive
    }

    fun canPlay(index: Int): Boolean {
        if (index !in 0 until count) return false
        val current = priority
        // Rewinding never exposes success feedback for an unanswered puzzle.
        if (current != null && gates.any {
                it.blockIndex !in correct && index in it.start until it.endExclusive
            }) return false
        val gate = boundary() ?: return true
        if (gate.blockIndex !in correct) return index < gate.start
        return index <= maxOf(gate.start, current?.allowedThrough ?: heardThrough)
    }
    fun blockedByAnswer(index: Int): Boolean = boundary()?.let {
        it.blockIndex !in correct && index >= it.start
    } == true

    /** Bookmark restoration cannot jump past a reset exercise or unheard feedback. */
    fun clamp(position: NarrationGatePosition): NarrationGatePosition {
        if (count == 0) return NarrationGatePosition(0)
        val safe = position.copy(segment = position.segment.coerceIn(0, count - 1),
            positionMs = position.positionMs.coerceAtLeast(0))
        val current = priority
        if (current != null) {
            val hidden = gates.takeWhile { it.blockIndex != current.blockIndex }
                .firstOrNull { it.blockIndex !in correct &&
                    safe.segment in it.start until it.endExclusive }
            if (hidden != null) return NarrationGatePosition((hidden.start - 1).coerceAtLeast(0))
        }
        val gate = boundary() ?: return safe
        val edge = if (gate.blockIndex !in correct) gate.start else
            maxOf(gate.start, current?.allowedThrough ?: heardThrough)
        return if (safe.segment > edge || (current == null &&
                gate.blockIndex !in correct && safe.segment == edge))
            NarrationGatePosition(edge) else safe
    }

    /** Back/forward skips have normal 3/5-second semantics after an answer,
     * without inventing any naturally heard explanation.
     */
    fun seek(from: NarrationGatePosition, to: NarrationGatePosition): NarrationGatePosition {
        val current = priority
        if (current != null) {
            val requested = to.copy(
                segment = to.segment.coerceIn(0, (count - 1).coerceAtLeast(0)),
                positionMs = to.positionMs.coerceAtLeast(0))
            val forward = requested.segment > from.segment ||
                (requested.segment == from.segment && requested.positionMs > from.positionMs)
            var target = requested
            val hidden = gates.takeWhile { it.blockIndex != current.blockIndex }
                .firstOrNull { it.blockIndex !in correct &&
                    target.segment in it.start until it.endExclusive }
            if (hidden != null) target = NarrationGatePosition(if (forward)
                hidden.endExclusive.coerceAtMost(count - 1)
                else (hidden.start - 1).coerceAtLeast(0))
            val laterUnsolved = activeGates().drop(1).firstOrNull { it.blockIndex !in correct }
            if (laterUnsolved != null && target.segment > laterUnsolved.start)
                target = NarrationGatePosition(laterUnsolved.start)
            if (forward) current.allowedThrough = maxOf(current.allowedThrough, target.segment)
            return clamp(target)
        }
        val safe = clamp(to)
        val gate = boundary() ?: return safe
        val edge = if (gate.blockIndex !in correct) gate.start else maxOf(gate.start, heardThrough)
        val forward = safe.segment > from.segment ||
            (safe.segment == from.segment && safe.positionMs > from.positionMs)
        if (!forward || safe.segment < edge) return safe
        return if (from.segment == edge && canPlay(edge))
            from.copy(positionMs = from.positionMs.coerceAtLeast(0)) else NarrationGatePosition(edge)
    }

    fun completedNaturally(index: Int) {
        val gate = boundary() ?: return
        val current = priority
        val expected = maxOf(gate.start, current?.allowedThrough ?: heardThrough)
        if (gate.blockIndex !in correct || index != expected || index >= gate.endExclusive) return
        if (current == null) {
            heardThrough = index + 1
            return
        }
        current.allowedThrough = index + 1
        // Previously skipped feedback never becomes "heard". Only a contiguous
        // chain of actually completed earlier success gates permits this update.
        val older = gates.takeWhile { it.blockIndex != gate.blockIndex }
        if (older.all { it.blockIndex in correct && heardThrough >= it.endExclusive })
            heardThrough = index + 1
    }
}
