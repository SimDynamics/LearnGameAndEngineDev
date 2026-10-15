package com.learngraphicsprogramming.app.narration

/** Projects explicit seek controls using measured WAV durations, without moving playback. */
internal object NarrationVisualSeek {
    data class Result(val destination: NarrationGatePosition, val resolved: Boolean)

    /**
     * A negative offset is relative to the end of [start]'s already-selected segment.
     * Keep it unresolved until all crossed WAV durations are known; zero would otherwise
     * erase a correctly read prefix before the decoder can locate the actual destination.
     * This projection neither changes playback nor acknowledges educational feedback.
     */
    fun resolveBackward(start: NarrationGatePosition, durations: List<Int?>): Result {
        var destination = start
        while (destination.positionMs < 0L) {
            val duration = durations.getOrNull(destination.segment)?.takeIf { it > 0 }
                ?.toLong() ?: return Result(destination, false)
            destination = destination.copy(positionMs = destination.positionMs + duration)
            if (destination.positionMs < 0L) {
                if (destination.segment == 0) {
                    destination = destination.copy(positionMs = 0L)
                } else destination = destination.copy(segment = destination.segment - 1)
            }
        }
        return Result(destination, true)
    }

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
