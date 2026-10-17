package com.learngraphicsprogramming.app.narration

/** Negative seek intervals span both the temporary incorrect guide and the
 * original page. This is pure timing policy; only the controller may switch
 * playback owners or update the existing bounded page bookmark.
 */
internal object IncorrectFeedbackRewind {
    data class Destination(val segment: Int, val positionMs: Long,
                           val pageRemainderMs: Long? = null)

    fun resolve(segment: Int, positionMs: Long, deltaMs: Long,
                durationsMs: List<Int?>): Destination {
        require(segment in durationsMs.indices)
        var index = segment
        var offset = positionMs + deltaMs
        var unresolved = false
        while (offset < 0L && index > 0) {
            index--
            val duration = durationsMs[index]
            if (duration == null) {
                unresolved = true
                break
            }
            offset += duration
        }
        // At the real first segment, the remaining negative duration belongs
        // to the page recording that preceded this temporary guide.
        if (!unresolved && index == 0 && offset < 0L)
            return Destination(0, 0L, pageRemainderMs = offset)
        return Destination(index, if (unresolved) offset else offset.coerceAtLeast(0L))
    }
}
