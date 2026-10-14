package com.learngraphicsprogramming.app.narration

import android.content.Context
import java.util.UUID

/** Fixed-size persistent metadata only. Audio files and the synthesis queue are separate. */
internal class NarrationResumeStore(context: Context) {
    private val preferences = context.applicationContext.getSharedPreferences(
        "lesson_narration_resume_bounded_v1", Context.MODE_PRIVATE
    )

    fun restore(key: String, count: Int): NarrationResumePoint? = synchronized(lock) {
        NarrationResumePolicy.restore(read(), key, count)
    }
    fun owns(key: String, owner: String?): Boolean = synchronized(lock) {
        NarrationResumePolicy.owns(read(), key, owner)
    }
    fun begin(key: String, point: NarrationResumePoint): String = synchronized(lock) {
        val owner = UUID.randomUUID().toString()
        write(NarrationResumePolicy.begin(read(), key, owner, point))
        owner
    }
    fun save(key: String, owner: String, point: NarrationResumePoint) = synchronized(lock) {
        val old = read()
        val updated = NarrationResumePolicy.update(old, key, owner, point)
        if (updated != old) write(updated)
    }

    private fun read(): NarrationResumeHistory = runCatching {
        val entries = (0 until NarrationResumePolicy.MAX_BOOKMARKS).mapNotNull { index ->
            val key = preferences.getString("key_$index", null) ?: return@mapNotNull null
            NarrationResumeEntry(key, NarrationResumePoint(
                preferences.getInt("segment_$index", 0), preferences.getLong("position_$index", 0L),
                finished = preferences.getBoolean("finished_$index", false),
                feedbackHeardThrough = preferences.getInt("heard_$index", 0).coerceAtLeast(0)
            ))
        }.distinctBy { it.key }
        NarrationResumeHistory(entries, preferences.getString("active_key", "").orEmpty(),
            preferences.getString("active_owner", "").orEmpty())
    }.getOrDefault(NarrationResumeHistory())

    private fun write(history: NarrationResumeHistory) {
        val editor = preferences.edit().putString("active_key", history.activeKey)
            .putString("active_owner", history.activeOwner)
        for (index in 0 until NarrationResumePolicy.MAX_BOOKMARKS) {
            val entry = history.entries.getOrNull(index)
            if (entry == null) {
                editor.remove("key_$index").remove("segment_$index").remove("position_$index")
                    .remove("finished_$index").remove("heard_$index")
            } else {
                editor.putString("key_$index", entry.key).putInt("segment_$index", entry.point.segment)
                    .putLong("position_$index", entry.point.positionMs)
                    .putBoolean("finished_$index", entry.point.finished)
                    .putInt("heard_$index", entry.point.feedbackHeardThrough)
            }
        }
        editor.apply()
    }
    private companion object { val lock = Any() }
}
