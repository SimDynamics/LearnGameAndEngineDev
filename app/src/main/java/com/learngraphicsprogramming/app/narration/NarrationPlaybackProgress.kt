package com.learngraphicsprogramming.app.narration

/** Observable playback milestones, independent of synthesis and persistent audio. */
enum class NarrationPlaybackPhase {
    PREPARED,
    BUFFERING,
    PLAYING,
    PAUSED,
    POSITION_CHANGED,
    SEGMENT_COMPLETED,
    RESTARTED,
    FINISHED,
    STOPPED,
    UNAVAILABLE
}

/**
 * Indices refer to the controller's actual, engine-specific audio segments.
 * Only [NarrationPlaybackPhase.SEGMENT_COMPLETED] records natural audio completion;
 * seeking, preparing a WAV and restoring a bookmark do not report that milestone.
 */
data class NarrationPlaybackProgress(
    val pageIdentity: String,
    val generation: Long,
    val segmentIndex: Int,
    val segmentCount: Int,
    val phase: NarrationPlaybackPhase,
    val playbackKey: String? = null,
    /** Real start/seek offset in the source audio; never an estimated reading time. */
    val positionMs: Long = 0L
)
