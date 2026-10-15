package com.learngraphicsprogramming.app.narration

import android.app.DownloadManager
import android.content.Context
import android.net.Uri
import android.os.Handler
import android.os.Looper
import com.learngraphicsprogramming.app.R
import com.learngraphicsprogramming.app.UiLanguageSettings
import java.io.File
import java.io.IOException
import java.util.zip.ZipInputStream
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean

enum class KokoroModelStatus { NOT_INSTALLED, DOWNLOADING, INSTALLED, ERROR, UPDATE_AVAILABLE }

data class KokoroModelState(
    val status: KokoroModelStatus,
    val percentage: Int = 0,
    val error: String? = null,
    val downloadedBytes: Long = 0L,
    val totalBytes: Long = KokoroModelManifest.sharedArtifacts.sumOf { it.bytes }
)

/**
 * DownloadManager owns transfers across Activity recreation/process death. Only an explicit
 * accepted download starts network work. Inspection and digest verification run on a worker;
 * INSTALLED describes verified files, not availability of a Spanish synthesis engine.
 */
class KokoroModelStore(context: Context) {
    private val uiContext: Context get() = UiLanguageSettings.localizedContext(appContext)
    private fun problem(code: NarrationResourceProblem, vararg args: Any) = NarrationResourceMessages.encode(code, *args)
    private fun problem(error: Exception, code: NarrationResourceProblem) = NarrationResourceMessages.encode(error, code)
    private fun message(encoded: String?) = NarrationResourceMessages.render(uiContext, encoded)
    private val appContext = context.applicationContext
    private val preferences = appContext.getSharedPreferences("kokoro_model_downloads", Context.MODE_PRIVATE)
    private fun storedProblem(key: String): String? {
        val saved = preferences.getString(key, null) ?: return null
        val normalized = NarrationResourceMessages.normalize(saved)
        if (normalized != saved) preferences.edit().putString(key, normalized).apply()
        return normalized
    }
    private val downloads = appContext.getSystemService(Context.DOWNLOAD_SERVICE) as DownloadManager
    private val worker = Executors.newSingleThreadExecutor()
    private val main = Handler(Looper.getMainLooper())
    private val inspecting = AtomicBoolean(false)
    private val verified = mutableMapOf<String, Pair<Long, Long>>()
    @Volatile private var closed = false

    /** Delivers a snapshot on the main thread; does not enqueue any transfer. */
    fun inspect(onState: (KokoroModelState) -> Unit) = inspectArtifacts(KokoroModelManifest.sharedArtifacts, onState)

    /** One bounded snapshot, sharing verified digests across the model, languages and voices. */
    fun inspectResources(onState: (Map<String, KokoroModelState>) -> Unit) {
        if (closed || !inspecting.compareAndSet(false, true)) return
        submit {
            val states = linkedMapOf<String, KokoroModelState>()
            try {
                synchronized(storeLock) {
                    val groups = linkedMapOf("model" to KokoroModelManifest.sharedArtifacts)
                    if (EnglishAuxResources.artifacts.isNotEmpty()) groups["en_neural"] = EnglishAuxResources.artifacts
                    NarrationLanguage.entries.forEach { groups[it.resourceGroup] = KokoroModelManifest.resources(it) }
                    KokoroVoiceCatalog.voices.forEach { groups[it.id] = listOf(it.artifact) }
                    groups.forEach { (name, artifacts) ->
                        states[name] = try { inspectInternal(artifacts) }
                        catch (error: Exception) { KokoroModelState(KokoroModelStatus.ERROR, error = message(problem(error, NarrationResourceProblem.CHECK_FAILED)),
                            totalBytes = artifacts.sumOf { it.bytes }) }
                    }
                }
            } finally { inspecting.set(false) }
            main.post { if (!closed) onState(states) }
        }
    }

    /**
     * Lightweight check for lesson-level voice selection. Verifies each voice
     * through the same installed-file pipeline, but does not scan the 311 MiB
     * shared model or dictionaries whenever lesson settings are opened.
     */
    fun inspectVoices(onState: (Map<String, KokoroModelState>) -> Unit) {
        if (closed || !inspecting.compareAndSet(false, true)) return
        submit {
            val states = linkedMapOf<String, KokoroModelState>()
            try {
                synchronized(storeLock) {
                    KokoroVoiceCatalog.voices.forEach { voice ->
                        states[voice.id] = try {
                            inspectInternal(listOf(voice.artifact))
                        } catch (error: Exception) {
                            KokoroModelState(
                                KokoroModelStatus.ERROR,
                                error = message(problem(error, NarrationResourceProblem.CHECK_FAILED)),
                                totalBytes = voice.artifact.bytes
                            )
                        }
                    }
                }
            } finally {
                inspecting.set(false)
            }
            main.post { if (!closed) onState(states) }
        }
    }

    fun inspectArtifacts(artifacts: List<KokoroModelManifest.Artifact>, onState: (KokoroModelState) -> Unit) {
        if (closed || !inspecting.compareAndSet(false, true)) return
        submit {
            val state = try {
                synchronized(storeLock) { inspectInternal(artifacts) }
            } catch (_: InterruptedException) {
                return@submit
            } catch (error: Exception) {
                KokoroModelState(KokoroModelStatus.ERROR, error = message(problem(error, NarrationResourceProblem.CHECK_FAILED)))
            } finally {
                inspecting.set(false)
            }
            main.post { if (!closed) onState(state) }
        }
    }

    fun startDownload(userAccepted: Boolean, artifacts: List<KokoroModelManifest.Artifact> = KokoroModelManifest.sharedArtifacts) {
        require(userAccepted) { "La descarga requiere aceptación explícita del usuario" }
        require(artifacts.isNotEmpty() && artifacts.all { it in KokoroModelManifest.artifacts })
        submit {
            synchronized(storeLock) {
                try {
                    val directory = currentDirectory()
                    if (!directory.isDirectory && !directory.mkdirs()) {
                        throw NarrationResourceException(NarrationResourceProblem.CREATE_DIRECTORY)
                    }
                    val revisions = managedRevisions() + KokoroModelManifest.revision
                    if (!preferences.edit().putStringSet(KEY_MANAGED_REVISIONS, revisions).commit()) {
                        throw NarrationResourceException(NarrationResourceProblem.SAVE_DIRECTORY)
                    }
                    // A replacement keeps the previous verified model until every new file
                    // passes integrity checking, but never runs transfers for two revisions.
                    cancelTransfers(revisions - KokoroModelManifest.revision)
                    writeOperationError(artifacts, null)
                    preferences.edit().remove(errorKey(artifacts)).apply()
                    artifacts.forEach { artifact ->
                        val destination = ownedFile(directory, artifact.fileName)
                        val importedPartial = ownedFile(directory, "${artifact.fileName}.import-part")
                        if (importedPartial.exists() && !importedPartial.delete()) {
                            throw NarrationResourceException(NarrationResourceProblem.IMPORT_CLEANUP)
                        }
                        if (isVerified(destination, artifact)) return@forEach
                        val id = recoverDownloadId(directory, artifact)
                        if (id != NO_DOWNLOAD) {
                            val record = query(id)
                            if (record != null && record.status != DownloadManager.STATUS_FAILED) return@forEach
                            downloads.remove(id)
                            clearDownloadId(artifact)
                        }
                        if (destination.exists()) destination.delete()
                        val partial = ownedFile(directory, "${artifact.fileName}.part")
                        if (partial.exists() && !partial.delete()) {
                            throw NarrationResourceException(NarrationResourceProblem.DOWNLOAD_CLEANUP)
                        }
                        val request = DownloadManager.Request(Uri.parse(artifact.url))
                            .setTitle(uiContext.getString(R.string.narration_resource_notification_title, artifact.fileName))
                            .setDescription(uiContext.getString(R.string.narration_resource_notification_description))
                            .setNotificationVisibility(DownloadManager.Request.VISIBILITY_VISIBLE_NOTIFY_COMPLETED)
                            .setAllowedOverMetered(true)
                            .setAllowedOverRoaming(false)
                            .setDestinationUri(Uri.fromFile(partial))
                        val newId = downloads.enqueue(request)
                        // Commit immediately: a recreated store must recover the same transfer ID.
                        if (!preferences.edit().putLong(idKey(artifact), newId).commit()) {
                            downloads.remove(newId)
                            throw NarrationResourceException(NarrationResourceProblem.SAVE_DOWNLOAD)
                        }
                    }
                } catch (error: Exception) {
                    preferences.edit().putString(errorKey(artifacts), problem(error, NarrationResourceProblem.START_FAILED)).apply()
                }
            }
        }
    }

    /** Pending/paused DownloadManager transfers resume automatically when the network returns. */
    fun resumeDownload(userAccepted: Boolean) = startDownload(userAccepted)
    fun retryDownload(userAccepted: Boolean) = startDownload(userAccepted)

    fun cancelDownload(artifacts: List<KokoroModelManifest.Artifact> = KokoroModelManifest.artifacts) {
        submit {
            synchronized(storeLock) {
                writeOperationError(artifacts, null)
                try {
                    val revisions = managedRevisions()
                    cancelTransfers(artifacts = artifacts)
                    revisions.forEach { revision ->
                        val directory = revisionDirectory(revision)
                        artifacts.forEach { artifact ->
                            ownedFile(directory, "${artifact.fileName}.part").delete()
                        }
                    }
                    preferences.edit().remove(errorKey(artifacts)).apply()
                } catch (error: Exception) {
                    writeOperationError(artifacts, problem(error, NarrationResourceProblem.CANCEL_FAILED))
                }
            }
        }
    }

    /** Cancels our transfers and removes only files declared in our managed model directories. */
    fun remove(artifacts: List<KokoroModelManifest.Artifact> = KokoroModelManifest.artifacts) {
        NarrationResourceEvents.removingResources()
        submit {
            synchronized(storeLock) {
                writeOperationError(artifacts, null)
                try {
                    val revisions = managedRevisions()
                    cancelTransfers(artifacts = artifacts)
                    revisions.forEach { revision ->
                        val directory = revisionDirectory(revision)
                        artifacts.forEach { artifact ->
                            listOf(artifact.fileName, "${artifact.fileName}.part", "${artifact.fileName}.import-part").forEach { name ->
                                val file = ownedFile(directory, name)
                                if (file.exists() && !file.delete()) throw NarrationResourceException(NarrationResourceProblem.REMOVE_FILE, name)
                            }
                        }
                        if (directory.isDirectory && directory.list()?.isEmpty() == true) directory.delete()
                    }
                    verified.clear()
                    val editor = preferences.edit().remove(errorKey(artifacts))
                    if (artifacts.any { it.fileName == "model.onnx" }) editor.remove(KEY_INSTALLED_REVISION)
                    editor.apply()
                } catch (error: Exception) {
                    writeOperationError(artifacts, problem(error, NarrationResourceProblem.REMOVE_FAILED))
                }
            }
        }
    }

    /** Leaves DownloadManager transfers intact; a future instance can inspect/recover them. */
    fun close() {
        closed = true
        worker.shutdown()
    }

    private fun inspectInternal(artifacts: List<KokoroModelManifest.Artifact>): KokoroModelState {
        val directory = currentDirectory()
        var downloadedBytes = 0L
        var active = false
        var allVerified = true
        var failure = storedProblem(errorKey(artifacts))
        val totalBytes = artifacts.sumOf { it.bytes }
        val operationError = artifacts.firstNotNullOfOrNull { storedProblem("operation_error:${it.fileName}") }
        artifacts.forEach { artifact ->
            val destination = ownedFile(directory, artifact.fileName)
            // A killed import leaves a bounded temporary file. Expose it so the user can
            // retry the import or remove these resources even when no entry was installed.
            val importedPartial = ownedFile(directory, "${artifact.fileName}.import-part")
            if (importedPartial.exists()) {
                allVerified = false
                failure = problem(NarrationResourceProblem.IMPORT_INTERRUPTED, artifact.fileName)
                if (!destination.isFile) downloadedBytes += importedPartial.length().coerceIn(0L, artifact.bytes)
            }
            if (isVerified(destination, artifact)) {
                downloadedBytes += artifact.bytes
                cleanupSuccessfulTransfer(artifact)
                return@forEach
            }
            val id = recoverDownloadId(directory, artifact)
            val record = if (id == NO_DOWNLOAD) null else query(id)
            if (record?.status == DownloadManager.STATUS_SUCCESSFUL) {
                val partial = ownedFile(directory, "${artifact.fileName}.part")
                if (KokoroModelManifest.verify(partial, artifact)) {
                    if (destination.exists() && !destination.delete()) throw NarrationResourceException(NarrationResourceProblem.REPLACE_FILE, artifact.fileName)
                    if (!partial.renameTo(destination)) throw NarrationResourceException(NarrationResourceProblem.SAVE_FILE, artifact.fileName)
                    verified[destination.absolutePath] = destination.length() to destination.lastModified()
                    downloadedBytes += artifact.bytes
                    cleanupSuccessfulTransfer(artifact)
                    return@forEach
                }
                failure = problem(NarrationResourceProblem.DOWNLOAD_DIGEST, artifact.fileName)
                downloads.remove(id)
                clearDownloadId(artifact)
                partial.delete()
            } else if (record != null && record.status != DownloadManager.STATUS_FAILED) {
                active = true
                downloadedBytes += record.bytes.coerceIn(0L, artifact.bytes)
            } else if (record?.status == DownloadManager.STATUS_FAILED) {
                failure = problem(NarrationResourceProblem.DOWNLOAD_FAILED, artifact.fileName, record.reason)
            } else if (id != NO_DOWNLOAD) {
                failure = problem(NarrationResourceProblem.DOWNLOAD_INTERRUPTED, artifact.fileName)
                clearDownloadId(artifact)
            } else if (destination.exists()) {
                failure = problem(NarrationResourceProblem.INVALID_FILE, artifact.fileName)
            }
            allVerified = false
        }
        val percentage = if (totalBytes == 0L) 100 else ((downloadedBytes * 100L) / totalBytes).toInt().coerceIn(0, 100)
        if (operationError != null) return KokoroModelState(KokoroModelStatus.ERROR, percentage, message(operationError), downloadedBytes, totalBytes)
        if (allVerified) {
            val revisions = managedRevisions()
            val sharedModel = artifacts.any { it.fileName == "model.onnx" }
            if (sharedModel) {
                cancelTransfers(revisions - KokoroModelManifest.revision)
                KokoroManagedFiles.removeObsolete(
                    storageRoot(), KokoroModelManifest.revision, revisions,
                    KokoroModelManifest.artifacts.map { it.fileName }.toSet()
                )
            }
            // Commit only after obsolete owned files are gone. Failed cleanup remains a
            // retryable ERROR rather than reporting two permanent models as installed.
            val editor = preferences.edit().putStringSet(KEY_MANAGED_REVISIONS,
                    if (sharedModel) setOf(KokoroModelManifest.revision) else revisions)
                    .remove(errorKey(artifacts))
            if (sharedModel) editor.putString(KEY_INSTALLED_REVISION, KokoroModelManifest.revision)
            if (!editor.commit()) {
                throw NarrationResourceException(NarrationResourceProblem.SAVE_REVISION)
            }
            return KokoroModelState(KokoroModelStatus.INSTALLED, 100, downloadedBytes = downloadedBytes, totalBytes = totalBytes)
        }
        if (failure != null) preferences.edit().putString(errorKey(artifacts), failure).apply()
        // Keep cancellation visible until every in-flight transfer has stopped, even if
        // another artifact failed. The terminal snapshot then exposes the retryable error.
        if (active) return KokoroModelState(KokoroModelStatus.DOWNLOADING, percentage, message(failure), downloadedBytes, totalBytes)
        if (failure != null) return KokoroModelState(KokoroModelStatus.ERROR, percentage, message(failure), downloadedBytes, totalBytes)
        val installedRevision = preferences.getString(KEY_INSTALLED_REVISION, null)
        if (artifacts.any { it.fileName == "model.onnx" } && installedRevision != null && installedRevision != KokoroModelManifest.revision) {
            return KokoroModelState(KokoroModelStatus.UPDATE_AVAILABLE, percentage, downloadedBytes = downloadedBytes, totalBytes = totalBytes)
        }
        return KokoroModelState(KokoroModelStatus.NOT_INSTALLED, percentage, downloadedBytes = downloadedBytes, totalBytes = totalBytes)
    }

    private fun isVerified(file: File, artifact: KokoroModelManifest.Artifact): Boolean {
        val stamp = file.length() to file.lastModified()
        if (file.isFile && verified[file.absolutePath] == stamp) return true
        verified.remove(file.absolutePath)
        if (!KokoroModelManifest.verify(file, artifact)) return false
        verified[file.absolutePath] = stamp
        return true
    }

    private data class DownloadRecord(val status: Int, val bytes: Long, val reason: Int)

    private fun query(id: Long): DownloadRecord? = downloads.query(DownloadManager.Query().setFilterById(id)).use { cursor ->
        if (cursor == null || !cursor.moveToFirst()) return@use null
        DownloadRecord(
            cursor.getInt(cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_STATUS)),
            cursor.getLong(cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_BYTES_DOWNLOADED_SO_FAR)),
            cursor.getInt(cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_REASON))
        )
    }

    private fun cleanupSuccessfulTransfer(artifact: KokoroModelManifest.Artifact) {
        val id = downloadId(artifact)
        if (id != NO_DOWNLOAD) downloads.remove(id)
        clearDownloadId(artifact)
    }

    private fun cancelTransfers(revisions: Set<String> = managedRevisions(), artifacts: List<KokoroModelManifest.Artifact> = KokoroModelManifest.artifacts) {
        revisions.forEach { revision ->
            try {
                val directory = revisionDirectory(revision)
                artifacts.forEach { recoverDownloadId(directory, it, revision) }
            } catch (_: IOException) {
                // Saved transfer IDs still need cancellation when external storage is absent.
            }
        }
        // All IDs saved in this namespace belong to this store, including a previous revision.
        val entries = preferences.all.filterKeys {
            it.startsWith(DOWNLOAD_PREFIX) && it.removePrefix(DOWNLOAD_PREFIX).substringBefore(':') in revisions &&
                it.substringAfterLast(':') in artifacts.map { artifact -> artifact.fileName }
        }
        val ids = entries.values.mapNotNull { it as? Long }.filter { it != NO_DOWNLOAD }.toLongArray()
        if (ids.isNotEmpty()) downloads.remove(*ids)
        val editor = preferences.edit()
        entries.keys.forEach { editor.remove(it) }
        editor.apply()
    }

    private fun downloadId(artifact: KokoroModelManifest.Artifact, revision: String = KokoroModelManifest.revision) =
        preferences.getLong(idKey(artifact, revision), NO_DOWNLOAD)

    /** Recovers the narrow enqueue-before-preference-commit window after process death. */
    private fun recoverDownloadId(
        directory: File,
        artifact: KokoroModelManifest.Artifact,
        revision: String = KokoroModelManifest.revision
    ): Long {
        val savedId = downloadId(artifact, revision)
        if (savedId != NO_DOWNLOAD) return savedId
        val destination = Uri.fromFile(ownedFile(directory, "${artifact.fileName}.part")).toString()
        val source = artifact.sourceUrl ?: "${KokoroModelManifest.source}/resolve/$revision/${artifact.remotePath}"
        downloads.query(DownloadManager.Query()).use { cursor ->
            if (cursor == null) return NO_DOWNLOAD
            val uriColumn = cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_URI)
            val destinationColumn = cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_LOCAL_URI)
            val idColumn = cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_ID)
            while (cursor.moveToNext()) {
                if (cursor.getString(uriColumn) == source && cursor.getString(destinationColumn) == destination) {
                    val recoveredId = cursor.getLong(idColumn)
                    preferences.edit().putLong(idKey(artifact, revision), recoveredId).commit()
                    return recoveredId
                }
            }
        }
        return NO_DOWNLOAD
    }

    private fun managedRevisions(): Set<String> =
        (preferences.getStringSet(KEY_MANAGED_REVISIONS, emptySet()).orEmpty() +
            setOfNotNull(KokoroModelManifest.revision, preferences.getString(KEY_INSTALLED_REVISION, null)) +
            preferences.all.keys.filter { it.startsWith(DOWNLOAD_PREFIX) }
                .map { it.removePrefix(DOWNLOAD_PREFIX).substringBefore(':') })
            .filter { it.matches(REVISION_PATTERN) }.toSet()

    private fun clearDownloadId(artifact: KokoroModelManifest.Artifact) {
        preferences.edit().remove(idKey(artifact)).apply()
    }
    private fun idKey(artifact: KokoroModelManifest.Artifact, revision: String = KokoroModelManifest.revision) =
        "$DOWNLOAD_PREFIX$revision:${artifact.fileName}"

    fun directory(): File = currentDirectory()

    /** SAF-selected ZIP, strictly restricted to pinned artifact names, sizes and digests. */
    fun importResources(uri: Uri, onResult: (String?) -> Unit) {
        NarrationResourceEvents.removingResources()
        submit {
            val message = try {
                synchronized(storeLock) {
                    val root = currentDirectory()
                    if (!root.isDirectory && !root.mkdirs()) throw NarrationResourceException(NarrationResourceProblem.CREATE_DIRECTORY)
                    // Preserve the shared importer for other resources; English neural
                    // models are installed only through their verified downloads.
                    val expected = (KokoroModelManifest.sharedArtifacts + KokoroModelManifest.voiceArtifacts +
                        KokoroModelManifest.languageArtifacts).associateBy { it.fileName }
                    val installed = mutableSetOf<String>()
                    appContext.contentResolver.openInputStream(uri)?.use { input ->
                        ZipInputStream(input.buffered()).use { zip ->
                            while (true) {
                                val entry = zip.nextEntry ?: break
                                val artifact = expected[entry.name] ?: throw NarrationResourceException(NarrationResourceProblem.UNKNOWN_ENTRY, entry.name)
                                if (entry.isDirectory || !installed.add(entry.name)) throw NarrationResourceException(NarrationResourceProblem.INVALID_ENTRY)
                                val temporary = ownedFile(root, "${artifact.fileName}.import-part")
                                try {
                                    temporary.outputStream().buffered().use { output ->
                                        val buffer = ByteArray(64 * 1024)
                                        var bytes = 0L
                                        while (true) {
                                            val count = zip.read(buffer)
                                            if (count < 0) break
                                            bytes += count
                                            if (bytes > artifact.bytes) throw NarrationResourceException(NarrationResourceProblem.INVALID_SIZE, artifact.fileName)
                                            output.write(buffer, 0, count)
                                        }
                                    }
                                    if (!KokoroModelManifest.verify(temporary, artifact)) throw NarrationResourceException(NarrationResourceProblem.IMPORT_DIGEST, artifact.fileName)
                                    val destination = ownedFile(root, artifact.fileName)
                                    if (!isVerified(destination, artifact)) {
                                        if (destination.exists() && !destination.delete()) throw NarrationResourceException(NarrationResourceProblem.REPLACE_FILE, artifact.fileName)
                                        if (!temporary.renameTo(destination)) throw NarrationResourceException(NarrationResourceProblem.SAVE_FILE, artifact.fileName)
                                        verified.remove(destination.absolutePath)
                                    }
                                    writeOperationError(listOf(artifact), null)
                                } finally { temporary.delete() }
                                zip.closeEntry()
                            }
                        }
                    } ?: throw NarrationResourceException(NarrationResourceProblem.OPEN_PACKAGE)
                    if (installed.isEmpty()) throw NarrationResourceException(NarrationResourceProblem.EMPTY_PACKAGE)
                    preferences.edit().putStringSet(KEY_MANAGED_REVISIONS, managedRevisions() + KokoroModelManifest.revision).commit()
                }
                null
            } catch (error: Exception) { message(problem(error, NarrationResourceProblem.IMPORT_FAILED)) }
            main.post { if (!closed) onResult(message) }
        }
    }

    private fun errorKey(artifacts: List<KokoroModelManifest.Artifact>) = "error:" + artifacts.map { it.fileName }.sorted().joinToString(",")

    private fun writeOperationError(artifacts: List<KokoroModelManifest.Artifact>, error: String?) {
        val editor = preferences.edit()
        artifacts.forEach { editor.putString("operation_error:${it.fileName}", error) }
        editor.apply()
    }

    private fun currentDirectory() = revisionDirectory(KokoroModelManifest.revision)

    private fun revisionDirectory(revision: String): File {
        require(revision.matches(REVISION_PATTERN))
        val root = storageRoot()
        val directory = File(root, revision)
        if (directory.canonicalFile != File(root.canonicalFile, revision)) throw NarrationResourceException(NarrationResourceProblem.INVALID_DIRECTORY)
        return directory
    }

    private fun storageRoot(): File {
        val privateRoot = appContext.getExternalFilesDir(null)?.canonicalFile
            ?: throw NarrationResourceException(NarrationResourceProblem.STORAGE_UNAVAILABLE)
        val root = appContext.getExternalFilesDir("kokoro")
            ?: throw NarrationResourceException(NarrationResourceProblem.STORAGE_UNAVAILABLE)
        if (root.canonicalFile.parentFile != privateRoot || root.canonicalFile.name != "kokoro") {
            throw NarrationResourceException(NarrationResourceProblem.INVALID_PRIVATE_DIRECTORY)
        }
        return root
    }

    private fun ownedFile(directory: File, name: String): File {
        val file = File(directory, name)
        if (file.canonicalFile.parentFile != directory.canonicalFile) throw NarrationResourceException(NarrationResourceProblem.INVALID_PATH)
        return file
    }

    private fun submit(action: () -> Unit) {
        if (!closed) {
            try { worker.execute(action) }
            catch (_: java.util.concurrent.RejectedExecutionException) { /* closed concurrently */ }
        }
    }

    companion object {
        private val storeLock = Any()
        private const val DOWNLOAD_PREFIX = "download:"
        private const val KEY_INSTALLED_REVISION = "installed_revision"
        private const val KEY_MANAGED_REVISIONS = "managed_revisions"
        private const val KEY_ERROR = "error"
        private const val NO_DOWNLOAD = -1L
        private val REVISION_PATTERN = Regex("[0-9a-f]{40}")
    }
}
