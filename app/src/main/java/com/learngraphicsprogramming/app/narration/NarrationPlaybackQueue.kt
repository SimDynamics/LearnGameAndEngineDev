package com.learngraphicsprogramming.app.narration

internal data class NarrationUtterance(
    val id: String,
    val text: String,
    val generation: Long
)

/** Mutaciones en el hilo principal; el motor sólo consulta los campos volátiles. */
internal class NarrationPlaybackQueue {
    private var segments: List<String> = emptyList()
    private var index = 0

    @Volatile
    var generation: Long = 0
        private set

    @Volatile
    var isPlaying: Boolean = false
        private set

    @Volatile
    private var currentId: String? = null

    fun start(texts: List<String>) {
        stop()
        segments = texts.filter { it.isNotBlank() }
        isPlaying = segments.isNotEmpty()
        currentId = if (isPlaying) utteranceId() else null
    }

    fun current(): NarrationUtterance? {
        if (!isPlaying) return null
        return NarrationUtterance(currentId ?: return null, segments[index], generation)
    }

    fun isCurrent(utterance: NarrationUtterance): Boolean =
        isPlaying && generation == utterance.generation && currentId == utterance.id

    fun complete(id: String): Boolean {
        if (!isPlaying || id != currentId) return false
        index++
        if (index >= segments.size) {
            stop()
        } else {
            currentId = utteranceId()
        }
        return true
    }

    fun stop() {
        generation++
        isPlaying = false
        currentId = null
        segments = emptyList()
        index = 0
    }

    private fun utteranceId(): String = "lesson_narration_${generation}_$index"
}
