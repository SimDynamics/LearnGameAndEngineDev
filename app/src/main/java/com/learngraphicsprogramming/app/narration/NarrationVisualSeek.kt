package com.learngraphicsprogramming.app.narration

/** Projects an explicit forward control using measured WAV durations, without moving playback. */
internal object NarrationVisualSeek {
    data class Result(val destination: NarrationGatePosition, val resolved: Boolean)

    /**
     * [start] is the controller's segment and unresolved source-audio offset, not a bookmark.
     * [seek] observes the existing exercise gate; this projection never acknowledges feedback.
     * An unknown duration exposes only the beginning of that segment until its WAV is available.
     */
    fun resolve(
        start: NarrationGatePosition,
        durations: List<Int?>,
        seek: (NarrationGatePosition, NarrationGatePosition) -> NarrationGatePosition
    ): Result {
        var destination = start
        var resolved = false
        while (true) {
            val duration = durations.getOrNull(destination.segment)?.toLong() ?: break
            if (destination.positionMs < duration || destination.segment == durations.lastIndex) {
                destination = destination.copy(positionMs = destination.positionMs.coerceAtMost(duration))
                resolved = true
                break
            }
            val edge = NarrationGatePosition(destination.segment, duration)
            val proposed = NarrationGatePosition(destination.segment + 1, destination.positionMs - duration)
            val safe = seek(edge, proposed)
            if (safe == edge) {
                destination = edge
                resolved = true
                break
            }
            destination = safe
            if (destination.positionMs == 0L) {
                resolved = true
                break
            }
        }
        if (!resolved) destination = destination.copy(positionMs = 0L)
        return Result(destination, resolved)
    }
}
