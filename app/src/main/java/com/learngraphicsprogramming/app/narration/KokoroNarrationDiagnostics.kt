package com.learngraphicsprogramming.app.narration

import android.content.Context
import android.util.Log
import org.json.JSONObject
import java.io.File
import java.io.PrintWriter
import java.io.StringWriter

/** Bounded private diagnostics. Events contain hashes/counters, never the narrated text. */
internal object KokoroNarrationDiagnostics {
    const val EVENTS_FILE = "kokoro-diagnostics.jsonl"
    const val STATUS_FILE = "kokoro-status.json"
    private const val MAX_BYTES = 256 * 1024
    private val lock = Any()

    fun snapshot(context: Context): JSONObject = synchronized(lock) {
        runCatching { JSONObject(File(context.applicationContext.filesDir, STATUS_FILE).readText()) }
            .getOrDefault(JSONObject())
    }

    fun update(context: Context, fields: Map<String, Any>) = synchronized(lock) {
        runCatching {
            val status = snapshot(context)
            fields.forEach { (key, value) -> status.put(key, value) }
            status.put("updatedAt", System.currentTimeMillis())
            KokoroPagePreparationQueue.writeAtomic(File(context.applicationContext.filesDir, STATUS_FILE), status.toString())
        }.onFailure { Log.e("KokoroSession", "Could not preserve narration status", it) }
        Unit
    }

    fun page(context: Context, page: NarrationPageQueue.Snapshot) = update(context, mapOf(
        "pageRevision" to page.revision, "pageTotal" to page.total,
        "pageCompleted" to page.completedCount, "pagePending" to page.pending
    ))

    fun event(context: Context, tag: String, message: String, error: Throwable? = null,
              fields: Map<String, Any> = emptyMap()) {
        if (error == null) Log.i(tag, message) else Log.e(tag, message, error)
        synchronized(lock) {
            runCatching {
                val event = JSONObject().put("at", System.currentTimeMillis()).put("tag", tag)
                    .put("event", message).put("thread", Thread.currentThread().name)
                fields.forEach { (key, value) -> event.put(key, value) }
                if (error != null) {
                    val stack = StringWriter().also { error.printStackTrace(PrintWriter(it)) }.toString()
                    event.put("exception", stack)
                }
                val file = File(context.applicationContext.filesDir, EVENTS_FILE)
                BoundedNarrationDiagnosticLog.append(file, event.toString(), MAX_BYTES)
            }.onFailure { Log.e("KokoroSession", "Could not preserve narration diagnostic event", it) }
        }
    }
}

/** Each JSON event is one line, including complete ordinary exception chains. */
internal object BoundedNarrationDiagnosticLog {
    fun append(file: File, event: String, maximumBytes: Int) {
        require(maximumBytes >= 128)
        val encoded = (event + "\n").toByteArray(Charsets.UTF_8)
        // An exceptional unbounded exception chain must not exhaust app storage.
        if (encoded.size > maximumBytes) {
            val replacement = JSONObject().put("event", "diagnostic-event-exceeds-limit")
                .put("originalBytes", encoded.size).toString()
            append(file, replacement, maximumBytes)
            return
        }
        if (file.length() + encoded.size > maximumBytes) {
            val entries = file.readLines(Charsets.UTF_8).asReversed()
            var bytes = encoded.size
            val retained = entries.takeWhile { line ->
                val size = (line + "\n").toByteArray(Charsets.UTF_8).size
                if (bytes + size <= maximumBytes) { bytes += size; true } else false
            }.asReversed()
            file.writeText(if (retained.isEmpty()) "" else retained.joinToString("\n", postfix = "\n"), Charsets.UTF_8)
        }
        file.appendBytes(encoded)
    }
}
