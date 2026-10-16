package com.learngraphicsprogramming.app.runtime

import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.DataInputStream
import java.io.DataOutputStream
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.nio.ByteBuffer
import java.nio.charset.CodingErrorAction
import java.security.MessageDigest
import java.util.Collections
import java.util.UUID

/**
 * Local revisions of the student's editable files, independent of drafts and build products.
 * A complete, synced, immutable record is published by one same-directory rename. There is no
 * mutable index to lose, no automatic pruning, and no operation on a Git repository.
 */
internal class CppProjectHistory(rootDirectory: File, projectIdentity: String) {
    private val root = rootDirectory.absoluteFile
    private val projectKey: String

    init {
        require(projectIdentity.isNotBlank() && projectIdentity.length <= 16_384) {
            "El historial necesita una identidad de proyecto válida."
        }
        projectKey = sha256(projectIdentity.toByteArray(Charsets.UTF_8))
    }

    internal class Revision internal constructor(
        val id: String,
        val label: String,
        val createdAt: Long,
        files: Map<String, String>,
        sourceFiles: List<String>,
        indentation: Map<String, String>,
        internal val projectKey: String
    ) {
        val files: Map<String, String> = immutableMap(files)
        val sourceFiles: List<String> = Collections.unmodifiableList(ArrayList(sourceFiles))
        val indentation: Map<String, String> = immutableMap(indentation)
    }

    enum class Change { UNCHANGED, ADDED, MODIFIED, REMOVED }
    enum class LineType { CONTEXT, ADDED, REMOVED }
    data class Summary(val id: String, val label: String, val createdAt: Long)
    data class FileStatus(val path: String, val change: Change)
    data class DiffLine(
        val type: LineType,
        val beforeLine: Int?,
        val afterLine: Int?,
        val text: String,
        val lineEnding: String
    )
    data class FileDiff(val path: String, val change: Change, val lines: List<DiffLine>)

    class StorageException(message: String, cause: Throwable? = null) : IOException(message, cause)

    /** Empty/incomplete drafts can be registered; selected sources must still belong to them. */
    @Synchronized
    fun register(
        files: Map<String, String>,
        sourceFiles: List<String>,
        label: String,
        indentation: Map<String, String> = emptyMap()
    ): Revision {
        require(label.isNotBlank() && label.toByteArray(Charsets.UTF_8).size <= MAX_LABEL_BYTES) {
            "Escribe un nombre de revisión de hasta 8 KiB."
        }
        // Copy before serialization: later editor changes cannot change the revision.
        val snapshotFiles = LinkedHashMap(files)
        val selectedSources = ArrayList(sourceFiles)
        val snapshotIndentation = LinkedHashMap(indentation)
        validateSnapshot(snapshotFiles, selectedSources, snapshotIndentation)
        val bytes = encode(label, System.currentTimeMillis(), snapshotFiles, selectedSources, snapshotIndentation)
        val id = sha256(bytes)
        var temporary: File? = null
        try {
            val directory = directory(create = true)!!
            val destination = File(directory, "$id.revision")
            requireSafeEntry(destination)
            if (destination.exists()) throw StorageException("La identidad de revisión ya existe; no se ha sobrescrito.")
            temporary = File.createTempFile("revision-", ".pending", directory)
            requireSafeEntry(temporary)
            FileOutputStream(temporary).use { stream ->
                stream.write(bytes)
                stream.fd.sync()
            }
            requireSafeEntry(directory)
            requireSafeEntry(destination)
            if (destination.exists() || !temporary.renameTo(destination)) {
                throw StorageException("No se pudo publicar la revisión; el historial anterior se conserva.")
            }
            temporary = null
            return decode(id, bytes)
        } catch (error: IOException) {
            throw if (error is StorageException) error else StorageException(
                "No se pudo guardar la revisión; el borrador y las revisiones anteriores se conservan.", error
            )
        } finally {
            // Only our unpublished staging file is disposable. Registered revisions are never removed.
            temporary?.let { if (it.exists()) it.delete() }
        }
    }

    /** Newest first. A damaged record is an error, never silently omitted from the history. */
    @Synchronized
    fun list(): List<Revision> {
        val directory = directory(create = false) ?: return emptyList()
        val entries = directory.listFiles() ?: throw StorageException("No se pudo leer el historial local.")
        return Collections.unmodifiableList(entries.filter { it.name.endsWith(".revision") }.map { file ->
            val id = file.name.removeSuffix(".revision")
            if (!ID.matches(id)) throw StorageException("El historial contiene una revisión con identidad dañada.")
            read(file, id)
        }.sortedWith(compareByDescending<Revision> { it.createdAt }.thenByDescending { it.id }))
    }

    /**
     * Verify every complete record but retain only small labels/identities for the chooser.
     * Unlike list(), memory consumption does not grow with the sum of all historical source texts.
     */
    @Synchronized
    fun summaries(): List<Summary> {
        val directory = directory(create = false) ?: return emptyList()
        val entries = directory.listFiles() ?: throw StorageException("No se pudo leer el historial local.")
        val result = entries.filter { it.name.endsWith(".revision") }.map { file ->
            val id = file.name.removeSuffix(".revision")
            if (!ID.matches(id)) throw StorageException("El historial contiene una revisión con identidad dañada.")
            val revision = read(file, id)
            Summary(revision.id, revision.label, revision.createdAt)
        }.sortedWith(compareByDescending<Summary> { it.createdAt }.thenByDescending { it.id })
        return Collections.unmodifiableList(result)
    }

    @Synchronized
    fun load(id: String): Revision {
        require(ID.matches(id)) { "Identidad de revisión no válida." }
        val directory = directory(create = false) ?: throw StorageException("No existe esa revisión local.")
        return read(File(directory, "$id.revision"), id)
    }

    /** Status is always the current editable content compared with the selected revision. */
    fun statuses(files: Map<String, String>, revision: Revision): List<FileStatus> {
        validateRevision(revision)
        CppProject.validateFiles(files)
        return (revision.files.keys + files.keys).distinct().sorted().map { path ->
            FileStatus(path, change(revision.files[path], files[path]))
        }
    }

    /** Exact source lines, including whitespace and CR/LF differences; no text normalization. */
    fun diff(path: String, currentFiles: Map<String, String>, revision: Revision): FileDiff {
        CppProject.validatePath(path)
        validateRevision(revision)
        CppProject.validateFiles(currentFiles)
        val before = revision.files[path]
        val after = currentFiles[path]
        return FileDiff(path, change(before, after), Collections.unmodifiableList(lineDiff(before.orEmpty(), after.orEmpty())))
    }

    /** Returns a new draft map, replacing/removing only the explicitly selected file. */
    fun restoreFile(path: String, currentFiles: Map<String, String>, revision: Revision): Map<String, String> {
        CppProject.validatePath(path)
        validateRevision(revision)
        CppProject.validateFiles(currentFiles)
        val restored = LinkedHashMap(currentFiles)
        if (revision.files.containsKey(path)) restored[path] = revision.files.getValue(path) else restored.remove(path)
        CppProject.validateFiles(restored)
        return immutableMap(restored)
    }

    private fun validateRevision(revision: Revision) {
        require(revision.projectKey == projectKey) { "La revisión pertenece a otro proyecto." }
    }

    private fun directory(create: Boolean): File? {
        requireSafeEntry(root)
        if (!root.exists()) {
            if (!create) return null
            if (!root.mkdirs() && !root.isDirectory) throw StorageException("No se pudo crear la carpeta de historial.")
        }
        requireSafeEntry(root)
        if (!root.isDirectory) throw StorageException("La ubicación del historial no es una carpeta.")
        val directory = File(root.canonicalFile, projectKey)
        requireSafeEntry(directory)
        if (!directory.exists()) {
            if (!create) return null
            if (!directory.mkdir() && !directory.isDirectory) throw StorageException("No se pudo crear el historial del proyecto.")
        }
        requireSafeEntry(directory)
        if (!directory.isDirectory) throw StorageException("El historial del proyecto no es una carpeta.")
        return directory
    }

    /** java.io keeps the runtime compatible with API 24; canonical comparison rejects links. */
    private fun requireSafeEntry(file: File) {
        val parent = file.parentFile ?: throw StorageException("La ubicación del historial no es válida.")
        val expected = File(parent.canonicalFile, file.name).absolutePath
        val actual = file.canonicalPath
        val same = expected.equals(actual, ignoreCase = File.separatorChar == '\\')
        if (!same) throw StorageException("El historial no admite enlaces ni rutas fuera de su carpeta.")
    }

    private fun read(file: File, id: String): Revision {
        try {
            requireSafeEntry(file)
            if (!file.isFile) throw StorageException("No existe el archivo de la revisión elegida.")
            if (file.length() > MAX_REVISION_BYTES) throw StorageException("El archivo de revisión supera el límite de seguridad.")
            val bytes = file.inputStream().use { input ->
                val output = ByteArrayOutputStream()
                val buffer = ByteArray(8192)
                while (true) {
                    val count = input.read(buffer)
                    if (count < 0) break
                    if (output.size().toLong() + count > MAX_REVISION_BYTES) {
                        throw StorageException("El archivo de revisión supera el límite de seguridad.")
                    }
                    output.write(buffer, 0, count)
                }
                output.toByteArray()
            }
            if (sha256(bytes) != id) throw StorageException("La integridad SHA-256 de la revisión no coincide; no se ha modificado.")
            return decode(id, bytes)
        } catch (error: IOException) {
            throw if (error is StorageException) error else StorageException("No se pudo leer la revisión local.", error)
        }
    }

    private fun encode(
        label: String,
        createdAt: Long,
        files: Map<String, String>,
        sourceFiles: List<String>,
        indentation: Map<String, String>
    ): ByteArray {
        val bytes = ByteArrayOutputStream()
        DataOutputStream(bytes).use { out ->
            fun string(value: String) {
                val encoded = value.toByteArray(Charsets.UTF_8)
                require(String(encoded, Charsets.UTF_8) == value) { "El texto contiene una secuencia Unicode incompleta." }
                out.writeInt(encoded.size)
                out.write(encoded)
                require(bytes.size().toLong() <= MAX_REVISION_BYTES) { "La revisión supera el límite de 32 MiB." }
            }
            string(FORMAT)
            string(projectKey)
            string(UUID.randomUUID().toString())
            out.writeLong(createdAt)
            string(label)
            out.writeInt(files.size)
            files.forEach { (path, text) -> string(path); string(text) }
            out.writeInt(sourceFiles.size)
            sourceFiles.forEach(::string)
            out.writeInt(indentation.size)
            indentation.forEach { (path, state) -> string(path); string(state) }
        }
        return bytes.toByteArray()
    }

    private fun decode(id: String, bytes: ByteArray): Revision {
        try {
            DataInputStream(ByteArrayInputStream(bytes)).use { input ->
                fun string(maxBytes: Int = MAX_TEXT_BYTES): String {
                    val size = input.readInt()
                    if (size < 0 || size > maxBytes || size > input.available()) throw IOException("Longitud de texto dañada.")
                    val value = ByteArray(size)
                    input.readFully(value)
                    return Charsets.UTF_8.newDecoder().onMalformedInput(CodingErrorAction.REPORT)
                        .onUnmappableCharacter(CodingErrorAction.REPORT).decode(ByteBuffer.wrap(value)).toString()
                }
                fun count(): Int = input.readInt().also {
                    if (it !in 0..MAX_FILES) throw IOException("Cantidad de archivos dañada.")
                }
                if (string(128) != FORMAT || string(128) != projectKey) throw IOException("Formato o proyecto de revisión no válido.")
                UUID.fromString(string(128))
                val createdAt = input.readLong()
                val label = string(MAX_LABEL_BYTES)
                val files = LinkedHashMap<String, String>()
                repeat(count()) {
                    val path = string(MAX_PATH_BYTES)
                    if (files.put(path, string()) != null) throw IOException("Ruta duplicada en la revisión.")
                }
                val sources = List(count()) { string(MAX_PATH_BYTES) }
                val indentation = LinkedHashMap<String, String>()
                repeat(count()) {
                    val path = string(MAX_PATH_BYTES)
                    if (indentation.put(path, string()) != null) throw IOException("Sangría duplicada en la revisión.")
                }
                if (input.available() != 0) throw IOException("La revisión tiene datos adicionales no reconocidos.")
                require(label.isNotBlank())
                validateSnapshot(files, sources, indentation)
                return Revision(id, label, createdAt, files, sources, indentation, projectKey)
            }
        } catch (error: Exception) {
            throw StorageException("La revisión local está dañada; sus archivos se conservan para revisión.", error)
        }
    }

    private fun validateSnapshot(files: Map<String, String>, sources: List<String>, indentation: Map<String, String>) {
        require(files.size <= MAX_FILES) { "El historial admite hasta 256 archivos por revisión." }
        CppProject.validateFiles(files)
        files.forEach { (path, text) ->
            require(path.toByteArray(Charsets.UTF_8).size <= MAX_PATH_BYTES) { "La ruta es demasiado larga." }
            require(text.toByteArray(Charsets.UTF_8).size <= MAX_TEXT_BYTES) { "Un archivo supera el límite de 16 MiB de texto." }
        }
        require(sources.size <= MAX_FILES && sources.distinct().size == sources.size) { "La selección de fuentes contiene duplicados." }
        sources.forEach { path ->
            CppProject.validatePath(path)
            require(path in files && CppProject.isSourceFile(path)) { "Selecciona una fuente C++ del proyecto: $path" }
        }
        require(indentation.size <= MAX_FILES && indentation.keys.all { it in files }) { "La sangría debe pertenecer a archivos del proyecto." }
        require(indentation.values.all { it.toByteArray(Charsets.UTF_8).size <= MAX_TEXT_BYTES }) { "Los datos de sangría superan el límite de seguridad." }
    }

    private data class SourceLine(val text: String, val ending: String)

    private fun splitLines(text: String): List<SourceLine> {
        val lines = ArrayList<SourceLine>()
        var start = 0
        var cursor = 0
        while (cursor < text.length) {
            if (text[cursor] == '\r' || text[cursor] == '\n') {
                val end = cursor
                if (text[cursor] == '\r' && cursor + 1 < text.length && text[cursor + 1] == '\n') cursor++
                cursor++
                require(lines.size < MAX_DIFF_LINES) { "El archivo contiene demasiadas líneas para compararlo de una vez." }
                lines += SourceLine(text.substring(start, end), text.substring(end, cursor))
                start = cursor
            } else cursor++
        }
        if (start < text.length) {
            require(lines.size < MAX_DIFF_LINES) { "El archivo contiene demasiadas líneas para compararlo de una vez." }
            lines += SourceLine(text.substring(start), "")
        }
        return lines
    }

    private fun lineDiff(before: String, after: String): ArrayList<DiffLine> {
        val old = splitLines(before)
        val new = splitLines(after)
        val result = ArrayList<DiffLine>()
        fun emit(type: LineType, oldIndex: Int?, newIndex: Int?) {
            val line = if (type == LineType.ADDED) new[newIndex!!] else old[oldIndex!!]
            result += DiffLine(type, oldIndex?.plus(1), newIndex?.plus(1), line.text, line.ending)
        }
        var prefix = 0
        while (prefix < old.size && prefix < new.size && old[prefix] == new[prefix]) {
            emit(LineType.CONTEXT, prefix, prefix)
            prefix++
        }
        var oldEnd = old.size
        var newEnd = new.size
        while (oldEnd > prefix && newEnd > prefix && old[oldEnd - 1] == new[newEnd - 1]) { oldEnd--; newEnd-- }
        val oldCount = oldEnd - prefix
        val newCount = newEnd - prefix
        if ((oldCount.toLong() + 1) * (newCount.toLong() + 1) <= MAX_DIFF_CELLS) {
            val width = newCount + 1
            val lengths = IntArray((oldCount + 1) * width)
            for (a in oldCount - 1 downTo 0) for (b in newCount - 1 downTo 0) {
                lengths[a * width + b] = if (old[prefix + a] == new[prefix + b]) {
                    lengths[(a + 1) * width + b + 1] + 1
                } else maxOf(lengths[(a + 1) * width + b], lengths[a * width + b + 1])
            }
            var a = 0
            var b = 0
            while (a < oldCount || b < newCount) {
                when {
                    a < oldCount && b < newCount && old[prefix + a] == new[prefix + b] -> {
                        emit(LineType.CONTEXT, prefix + a, prefix + b); a++; b++
                    }
                    a < oldCount && (b == newCount || lengths[(a + 1) * width + b] >= lengths[a * width + b + 1]) -> {
                        emit(LineType.REMOVED, prefix + a, null); a++
                    }
                    else -> { emit(LineType.ADDED, null, prefix + b); b++ }
                }
            }
        } else {
            // Large changes remain exact without allocating a quadratic matrix.
            for (a in prefix until oldEnd) emit(LineType.REMOVED, a, null)
            for (b in prefix until newEnd) emit(LineType.ADDED, null, b)
        }
        for (offset in 0 until old.size - oldEnd) emit(LineType.CONTEXT, oldEnd + offset, newEnd + offset)
        return result
    }

    private fun change(before: String?, after: String?): Change = when {
        before == after -> Change.UNCHANGED
        before == null -> Change.ADDED
        after == null -> Change.REMOVED
        else -> Change.MODIFIED
    }

    companion object {
        private const val FORMAT = "lgp-cpp-local-history-v1"
        private const val MAX_FILES = 256
        private const val MAX_PATH_BYTES = 4096
        private const val MAX_TEXT_BYTES = 16 * 1024 * 1024
        private const val MAX_LABEL_BYTES = 8192
        private const val MAX_REVISION_BYTES = 32L * 1024 * 1024
        private const val MAX_DIFF_CELLS = 1_000_000L
        private const val MAX_DIFF_LINES = 100_000
        private val ID = Regex("[0-9a-f]{64}")
        private fun sha256(bytes: ByteArray): String = MessageDigest.getInstance("SHA-256")
            .digest(bytes).joinToString("") { "%02x".format(it) }
        private fun <K, V> immutableMap(value: Map<K, V>): Map<K, V> =
            Collections.unmodifiableMap(LinkedHashMap(value))
    }
}
