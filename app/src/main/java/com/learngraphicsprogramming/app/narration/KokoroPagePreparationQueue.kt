package com.learngraphicsprogramming.app.narration

import android.content.Context
import android.util.AtomicFile
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.util.concurrent.CopyOnWriteArrayList

/** One durable current-page priority; completed audio remains in the permanent cache. */
internal object KokoroPagePreparationQueue {
    const val FILE_NAME = "kokoro-page-preparation.json"
    private var directory: File? = null
    private var queue: NarrationPageQueue? = null
    private val changes = CopyOnWriteArrayList<() -> Unit>()

    @Synchronized
    private fun get(context: Context): NarrationPageQueue {
        val files = context.applicationContext.filesDir
        if (directory != files || queue == null) {
            val file = File(files, FILE_NAME)
            val stored = runCatching { AtomicFile(file).openRead().bufferedReader().use { it.readText() } }
                .getOrNull()
            queue = NarrationPageQueue(stored) { snapshot ->
                writeAtomic(file, snapshot.encode())
            }
            directory = files
        }
        return requireNotNull(queue)
    }

    fun submit(context: Context, texts: List<String>, language: NarrationLanguage, voice: String): NarrationPageQueue.Snapshot {
        val sentences = NarrationSegments.sentences(texts).mapIndexed { index, text ->
            NarrationCurriculum.Sentence(text, language, voice, "current-page/${index + 1}")
        }
        val previous = get(context).snapshot()?.revision
        val result = get(context).submit(sentences)
        if (previous != result.revision) changes.forEach { listener -> runCatching { listener() } }
        KokoroNarrationDiagnostics.page(context, result)
        KokoroNarrationSessionMonitor.refresh(context)
        return result
    }

    fun snapshot(context: Context): NarrationPageQueue.Snapshot? = get(context).snapshot()

    fun onChanged(listener: () -> Unit): AutoCloseable {
        changes.add(listener)
        return AutoCloseable { changes.remove(listener) }
    }

    fun isCurrent(context: Context, revision: Long): Boolean = snapshot(context)?.revision == revision

    fun markCompleted(context: Context, revision: Long, hash: String) {
        get(context).markCompleted(revision, hash)?.let { KokoroNarrationDiagnostics.page(context, it) }
    }

    fun markPending(context: Context, revision: Long, hash: String) {
        get(context).markPending(revision, hash)?.let { KokoroNarrationDiagnostics.page(context, it) }
    }

    internal fun writeAtomic(file: File, text: String) {
        val atomic = AtomicFile(file)
        val output = atomic.startWrite()
        try {
            output.write(text.toByteArray(Charsets.UTF_8))
            atomic.finishWrite(output)
        } catch (error: Throwable) {
            atomic.failWrite(output)
            throw error
        }
    }
}

/** Pure queue rules, including restart and completion accounting, independently testable. */
internal class NarrationPageQueue(stored: String? = null, private val persist: (Snapshot) -> Unit = {}) {
    data class Snapshot(val revision: Long, val sentences: List<NarrationCurriculum.Sentence>, val completed: Set<String>) {
        val total: Int get() = sentences.size
        val completedCount: Int get() = completed.size
        val pending: Int get() = total - completedCount

        fun encode(): String = JSONObject()
            .put("format", 1).put("revision", revision)
            .put("sentences", JSONArray(sentences.map { sentence ->
                JSONObject().put("text", sentence.text).put("language", sentence.language.tag)
                    .put("voice", sentence.voice).put("location", sentence.location)
            }))
            .put("completed", JSONArray(completed.toList())).toString()
    }

    private var current: Snapshot? = stored?.let { runCatching { decode(it) }.getOrNull() }

    @Synchronized fun snapshot(): Snapshot? = current

    @Synchronized fun submit(sentences: List<NarrationCurriculum.Sentence>): Snapshot {
        val unique = sentences.distinctBy(::hash)
        val previous = current
        if (previous?.sentences?.map(::hash) == unique.map(::hash)) return previous
        val keys = unique.map(::hash).toSet()
        val next = Snapshot((previous?.revision ?: 0L) + 1L, unique,
            previous?.completed.orEmpty().intersect(keys))
        persist(next)
        current = next
        return next
    }

    @Synchronized fun markCompleted(revision: Long, hash: String): Snapshot? {
        val previous = current ?: return null
        if (previous.revision != revision || previous.sentences.none { NarrationPageQueue.hash(it) == hash } || hash in previous.completed) return null
        val next = previous.copy(completed = previous.completed + hash)
        persist(next)
        current = next
        return next
    }

    @Synchronized fun markPending(revision: Long, hash: String): Snapshot? {
        val previous = current ?: return null
        if (previous.revision != revision || hash !in previous.completed) return null
        val next = previous.copy(completed = previous.completed - hash)
        persist(next)
        current = next
        return next
    }

    companion object {
        fun hash(sentence: NarrationCurriculum.Sentence): String =
            KokoroNarrationCache.key(sentence.language.tag, sentence.voice, sentence.text)

        private fun decode(value: String): Snapshot {
            val json = JSONObject(value)
            require(json.getInt("format") == 1)
            val entries = json.getJSONArray("sentences")
            val sentences = List(entries.length()) { index ->
                val item = entries.getJSONObject(index)
                NarrationCurriculum.Sentence(item.getString("text"),
                    requireNotNull(NarrationLanguage.fromTag(item.getString("language"))),
                    item.getString("voice"), item.getString("location"))
            }.distinctBy(::hash)
            val keys = sentences.map(::hash).toSet()
            val completed = json.optJSONArray("completed") ?: JSONArray()
            return Snapshot(json.getLong("revision"), sentences,
                (0 until completed.length()).map { completed.getString(it) }.toSet().intersect(keys))
        }
    }
}
