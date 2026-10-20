package com.learngraphicsprogramming.app.narration

import java.security.MessageDigest

internal data class NarrationResumePoint(val segment: Int = 0, val positionMs: Long = 0,
                                        val finished: Boolean = false, val feedbackHeardThrough: Int = 0)
internal data class NarrationResumeEntry(val key: String, val point: NarrationResumePoint,
                                        val stable: NarrationStablePage? = null)
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
        val audioIdentity = sentences.map { sentence ->
            if (engine != NarrationEngine.ANDROID.name &&
                ((language in setOf(NarrationLanguage.AMERICAN_ENGLISH.tag, NarrationLanguage.BRITISH_ENGLISH.tag) &&
                    EnglishTechnicalSpeech.hasChangedSymbolPronunciation(sentence)) ||
                    (language == NarrationLanguage.FRENCH.tag && FrenchTechnicalSpeech.hasChangedSymbolPronunciation(sentence)))) {
                // Only corrected audio has different timing. Existing bookmarks for
                // all unchanged recordings retain their exact identity and policy.
                "qualified-output-resume-v1\u0000" + KokoroNarrationCache.key(language, voice, sentence)
            } else if ((language != NarrationLanguage.SPANISH.tag && language != NarrationLanguage.FRENCH.tag) ||
                !ManualEnglishSpeech.isPrepared(sentence)) sentence
            else if (engine == NarrationEngine.ANDROID.name || ManualEnglishSpeech.spans(sentence).isEmpty()) {
                // Internal metadata cannot invalidate a bookmark for unchanged audio.
                ManualEnglishSpeech.strip(sentence)
            } else {
                // Duration can change after a corrected English pronunciation. The audio
                // key includes the selected occurrences and their actual compiled phones.
                "manual-english-resume-v1\u0000" + KokoroNarrationCache.key(language, voice, sentence)
            }
        }
        val digest = MessageDigest.getInstance("SHA-256")
        for (text in listOf("resume-v1", page, language, engine, voice) + audioIdentity) {
            val bytes = text.toByteArray(Charsets.UTF_8)
            digest.update(bytes.size.toString().toByteArray(Charsets.US_ASCII))
            digest.update(':'.code.toByte())
            digest.update(bytes)
        }
        return digest.digest().joinToString("") { "%02x".format(it.toInt() and 255) }
    }

    fun restore(history: NarrationResumeHistory, key: String, segmentCount: Int): NarrationResumePoint? =
        history.entries.firstOrNull { it.key == key }?.point?.takeIf {
            it.segment in 0 until segmentCount && it.positionMs >= 0 &&
                (!it.finished || it.segment == segmentCount - 1)
        }

    fun owns(history: NarrationResumeHistory, key: String, owner: String?): Boolean =
        owner != null && owner.isNotEmpty() && history.activeKey == key && history.activeOwner == owner

    /** A presentation change updates its existing slot without promoting or evicting a page. */
    fun relocate(history: NarrationResumeHistory, oldKey: String, newKey: String,
                 stable: NarrationStablePage? = null,
                 transform: (NarrationResumePoint) -> NarrationResumePoint?): NarrationResumeHistory {
        require(oldKey.isNotEmpty() && newKey.isNotEmpty())
        if (oldKey == newKey) return history
        val previous = history.entries.firstOrNull { it.key == oldKey } ?: return history
        if (history.entries.any { it.key == newKey }) {
            // A newer recording of this exact presentation takes precedence. The
            // former owner's token must not gain permission to overwrite it.
            val entries = history.entries.filterNot { it.key == oldKey }
            return if (history.activeKey == oldKey) history.copy(entries = entries, activeKey = "", activeOwner = "")
                else history.copy(entries = entries)
        }
        val point = transform(previous.point)?.takeIf {
            it.segment >= 0 && it.positionMs >= 0 && it.feedbackHeardThrough >= 0
        } ?: return history
        return history.copy(entries = history.entries.map {
            if (it.key == oldKey) NarrationResumeEntry(newKey, point, stable) else it
        }, activeKey = if (history.activeKey == oldKey) newKey else history.activeKey)
    }

    /** Only an explicit playback request promotes a page or evicts the oldest bookmark. */
    fun begin(history: NarrationResumeHistory, key: String, owner: String,
              point: NarrationResumePoint, stable: NarrationStablePage? = null): NarrationResumeHistory {
        require(key.isNotEmpty() && owner.isNotEmpty())
        require(point.segment >= 0 && point.positionMs >= 0)
        val entry = NarrationResumeEntry(key, point.copy(finished = false), stable)
        return NarrationResumeHistory(
            (listOf(entry) + history.entries.filterNot { it.key == key }).take(MAX_BOOKMARKS),
            key, owner
        )
    }

    fun update(history: NarrationResumeHistory, key: String, owner: String,
               point: NarrationResumePoint): NarrationResumeHistory {
        if (!owns(history, key, owner) || point.segment < 0 || point.positionMs < 0) return history
        // Keep the final position as a bounded bookmark. A finished page must not
        // automatically start from zero when revisited. Its ownership is released
        // so delayed callbacks from the old player cannot overwrite the result.
        val entries = history.entries.map {
            if (it.key == key) it.copy(point = point) else it
        }.take(MAX_BOOKMARKS)
        return if (point.finished) history.copy(entries = entries, activeKey = "", activeOwner = "")
            else history.copy(entries = entries)
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
