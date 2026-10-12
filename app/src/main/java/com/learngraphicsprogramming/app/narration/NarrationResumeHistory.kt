package com.learngraphicsprogramming.app.narration

import java.security.MessageDigest

internal data class NarrationResumePoint(val segment: Int = 0, val positionMs: Long = 0,
                                        val finished: Boolean = false, val feedbackHeardThrough: Int = 0)
internal data class NarrationResumeEntry(val key: String, val point: NarrationResumePoint)
internal data class NarrationResumeHistory(
    val entries: List<NarrationResumeEntry> = emptyList(),
    val activeKey: String = "",
    val activeOwner: String = ""
)

/** Three small bookmarks, never players, audio buffers, Activities or background jobs. */
internal object NarrationResumePolicy {
    const val MAX_BOOKMARKS = 3

    fun key(page: String, language: String, engine: String, voice: String,
            sentences: List<String>): String {
        val digest = MessageDigest.getInstance("SHA-256")
        for (text in listOf("resume-v1", page, language, engine, voice) + sentences) {
            val bytes = text.toByteArray(Charsets.UTF_8)
            digest.update(bytes.size.toString().toByteArray(Charsets.US_ASCII))
            digest.update(':'.code.toByte())
            digest.update(bytes)
        }
        return digest.digest().joinToString("") { "%02x".format(it.toInt() and 255) }
    }

    fun restore(history: NarrationResumeHistory, key: String, segmentCount: Int): NarrationResumePoint? =
        history.entries.firstOrNull { it.key == key }?.point?.takeIf {
            !it.finished && it.segment in 0 until segmentCount && it.positionMs >= 0
        }

    fun owns(history: NarrationResumeHistory, key: String, owner: String?): Boolean =
        owner != null && owner.isNotEmpty() && history.activeKey == key && history.activeOwner == owner

    /** Only an explicit playback request promotes a page or evicts the oldest bookmark. */
    fun begin(history: NarrationResumeHistory, key: String, owner: String,
              point: NarrationResumePoint): NarrationResumeHistory {
        require(key.isNotEmpty() && owner.isNotEmpty())
        require(point.segment >= 0 && point.positionMs >= 0)
        val entry = NarrationResumeEntry(key, point.copy(finished = false))
        return NarrationResumeHistory(
            (listOf(entry) + history.entries.filterNot { it.key == key }).take(MAX_BOOKMARKS),
            key, owner
        )
    }

    fun update(history: NarrationResumeHistory, key: String, owner: String,
               point: NarrationResumePoint): NarrationResumeHistory {
        if (!owns(history, key, owner) || point.segment < 0 || point.positionMs < 0) return history
        if (point.finished) return history.copy(entries = history.entries.filterNot { it.key == key },
            activeKey = "", activeOwner = "")
        // Never resurrect an evicted entry from a delayed callback.
        return history.copy(entries = history.entries.map {
            if (it.key == key) it.copy(point = point) else it
        }.take(MAX_BOOKMARKS))
    }
}

/** Main-thread ownership: starting another controller releases the previous decoder first. */
internal object NarrationPlaybackOwner {
    private var owner: Any? = null
    private var pauseOwner: (() -> Unit)? = null

    fun acquire(next: Any, pause: () -> Unit) {
        if (owner === next) return
        val previousPause = pauseOwner
        owner = null
        pauseOwner = null
        previousPause?.invoke()
        owner = next
        pauseOwner = pause
    }

    fun release(requester: Any) {
        if (owner === requester) {
            owner = null
            pauseOwner = null
        }
    }
}
