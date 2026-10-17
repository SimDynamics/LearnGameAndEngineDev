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
        invalidateUnsolved()
    }
    fun restoreHeard(value: Int) {
        heardThrough = value.coerceIn(0, count)
        invalidateUnsolved()
    }
    fun restart() { heardThrough = 0 }
    fun hasExercise(blockIndex: Int): Boolean = gates.any { it.blockIndex == blockIndex }

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
    fun boundary(): NarrationFeedbackGate? = gates.firstOrNull {
        it.blockIndex !in correct || heardThrough < it.endExclusive
    }
    fun canPlay(index: Int): Boolean {
        if (index !in 0 until count) return false
        val gate = boundary() ?: return true
        if (gate.blockIndex !in correct) return index < gate.start
        // Later feedback segments become playable only after their predecessor finishes.
        return index <= maxOf(gate.start, heardThrough)
    }
    fun blockedByAnswer(index: Int): Boolean = boundary()?.let {
        it.blockIndex !in correct && index >= it.start
    } == true

    /** Bookmark restoration cannot jump past a reset exercise or unheard feedback. */
    fun clamp(position: NarrationGatePosition): NarrationGatePosition {
        if (count == 0) return NarrationGatePosition(0)
        val safe = position.copy(segment = position.segment.coerceIn(0, count - 1),
            positionMs = position.positionMs.coerceAtLeast(0))
        val gate = boundary() ?: return safe
        val edge = if (gate.blockIndex !in correct) gate.start else maxOf(gate.start, heardThrough)
        return if (safe.segment > edge || (gate.blockIndex !in correct && safe.segment == edge))
            NarrationGatePosition(edge) else safe
    }

    /** Seeking forward never stands in for hearing the successful feedback. */
    fun seek(from: NarrationGatePosition, to: NarrationGatePosition): NarrationGatePosition {
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
        val expected = maxOf(gate.start, heardThrough)
        if (gate.blockIndex in correct && index == expected && index < gate.endExclusive)
            heardThrough = index + 1
    }
}
