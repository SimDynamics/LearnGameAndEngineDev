package com.learngraphicsprogramming.app.narration

import android.content.Context
import android.os.StatFs
import java.io.File

/**
 * User-selected ceiling for persistent narration audio on internal storage.
 * No file eviction: a smaller ceiling only pauses optional new rendering.
 * The ceiling is bounded by current free space + already cached audio - 1 GiB,
 * rather than total disk capacity, so unrelated apps/files keep their space.
 */
internal object NarrationStorageBudget {
    const val RESERVE_BYTES = 1L shl 30
    private const val PREFERENCES = "lesson_narration_settings"
    private const val KEY_LIMIT = "offline_audio_budget_bytes"
    const val MIB = 1024L * 1024L

    data class Snapshot(
        val storedBytes: Long,
        val availableBytes: Long,
        val maximumBytes: Long,
        val configuredBytes: Long,
        val effectiveBytes: Long
    ) {
        val mayGenerate: Boolean get() = effectiveBytes > storedBytes &&
            availableBytes > RESERVE_BYTES
    }

    fun savedLimit(context: Context): Long? {
        val prefs = context.applicationContext.getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE)
        return if (prefs.contains(KEY_LIMIT)) prefs.getLong(KEY_LIMIT, 0).coerceAtLeast(0) else null
    }

    fun saveLimit(context: Context, bytes: Long) {
        context.applicationContext.getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE)
            .edit().putLong(KEY_LIMIT, bytes.coerceAtLeast(0)).apply()
    }

    fun capacity(storedBytes: Long, freeBytes: Long): Long =
        // Saturation is defensive for unusual mounted storage filesystems.
        (if (Long.MAX_VALUE - storedBytes < freeBytes) Long.MAX_VALUE
         else storedBytes + freeBytes).minus(RESERVE_BYTES).coerceAtLeast(0L)

    fun snapshot(context: Context, cachedBytes: Long? = null): Snapshot {
        val files = File(context.applicationContext.filesDir, "kokoro-narration-cache")
        val used = cachedBytes ?: files.listFiles().orEmpty().asSequence()
            .filter { it.isFile && (it.extension == "wav" || it.extension == "ogg") }
            .sumOf { it.length() }
        val available = StatFs(context.applicationContext.filesDir.absolutePath).availableBytes
        val max = capacity(used, available)
        val saved = savedLimit(context)
        return Snapshot(used, available, max, saved ?: max, (saved ?: max).coerceAtMost(max))
    }

    fun fitsNext(snapshot: Snapshot, estimatedBytes: Long = 0): Boolean =
        snapshot.mayGenerate && estimatedBytes.coerceAtLeast(0) <=
            snapshot.effectiveBytes - snapshot.storedBytes
}
