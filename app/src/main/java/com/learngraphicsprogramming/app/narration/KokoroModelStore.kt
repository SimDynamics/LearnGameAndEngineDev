package com.learngraphicsprogramming.app.narration

import android.app.DownloadManager
import android.content.Context
import android.net.Uri
import android.os.Handler
import android.os.Looper
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
    private val appContext = context.applicationContext
    private val preferences = appContext.getSharedPreferences("kokoro_model_downloads", Context.MODE_PRIVATE)
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
                        catch (error: Exception) { KokoroModelState(KokoroModelStatus.ERROR, error = error.message,
                            totalBytes = artifacts.sumOf { it.bytes }) }
                    }
                }
            } finally { inspecting.set(false) }
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
                KokoroModelState(KokoroModelStatus.ERROR, error = error.message ?: "No se pudieron comprobar los archivos")
            } finally {
                inspecting.set(false)
            }
            main.post { if (!closed) onState(state) }
        }
    }

    fun startDownload(userAccepted: Boolean, artifacts: List<KokoroModelManifest.Artifact> = KokoroModelManifest.sharedArtifacts) {
        require(userAccepted) { "La descarga requiere aceptación explícita del usuario" }
        require(artifacts.isNotEmpty() && artifacts.all { it in KokoroModelManifest.artifacts })
        require(artifacts.none { it.localImportOnly }) { "Los recursos convertidos se instalan mediante importación local" }
        submit {
            synchronized(storeLock) {
                try {
                    val directory = currentDirectory()
                    if (!directory.isDirectory && !directory.mkdirs()) {
                        throw IOException("No se pudo crear el directorio de Kokoro")
                    }
                    val revisions = managedRevisions() + KokoroModelManifest.revision
                    if (!preferences.edit().putStringSet(KEY_MANAGED_REVISIONS, revisions).commit()) {
                        throw IOException("No se pudo guardar el directorio de la descarga")
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
                            throw IOException("No se pudo limpiar una importación interrumpida")
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
                            throw IOException("No se pudo limpiar una descarga incompleta")
                        }
                        val request = DownloadManager.Request(Uri.parse(artifact.url))
                            .setTitle("Kokoro: ${artifact.fileName}")
                            .setDescription("Modelo de lectura en voz alta")
                            .setNotificationVisibility(DownloadManager.Request.VISIBILITY_VISIBLE_NOTIFY_COMPLETED)
                            .setAllowedOverMetered(true)
                            .setAllowedOverRoaming(false)
                            .setDestinationUri(Uri.fromFile(partial))
                        val newId = downloads.enqueue(request)
                        // Commit immediately: a recreated store must recover the same transfer ID.
                        if (!preferences.edit().putLong(idKey(artifact), newId).commit()) {
                            downloads.remove(newId)
                            throw IOException("No se pudo guardar el estado de la descarga")
                        }
                    }
                } catch (error: Exception) {
                    preferences.edit().putString(errorKey(artifacts), error.message ?: "No se pudo iniciar la descarga").apply()
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
                    writeOperationError(artifacts, error.message ?: "No se pudo cancelar la descarga")
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
                                if (file.exists() && !file.delete()) throw IOException("No se pudo eliminar $name")
                            }
                        }
                        if (directory.isDirectory && directory.list()?.isEmpty() == true) directory.delete()
                    }
                    verified.clear()
                    val editor = preferences.edit().remove(errorKey(artifacts))
                    if (artifacts.any { it.fileName == "model.onnx" }) editor.remove(KEY_INSTALLED_REVISION)
                    editor.apply()
                } catch (error: Exception) {
                    writeOperationError(artifacts, error.message ?: "No se pudo eliminar Kokoro")
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
        var failure = preferences.getString(errorKey(artifacts), null)
        val totalBytes = artifacts.sumOf { it.bytes }
        val operationError = artifacts.firstNotNullOfOrNull { preferences.getString("operation_error:${it.fileName}", null) }
        artifacts.forEach { artifact ->
            val destination = ownedFile(directory, artifact.fileName)
            // A killed import leaves a bounded temporary file. Expose it so the user can
            // retry the import or remove these resources even when no entry was installed.
            val importedPartial = ownedFile(directory, "${artifact.fileName}.import-part")
            if (importedPartial.exists()) {
                allVerified = false
                failure = "La importación de ${artifact.fileName} fue interrumpida; vuelve a importar o elimina estos recursos"
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
                    if (destination.exists() && !destination.delete()) throw IOException("No se pudo sustituir ${artifact.fileName}")
                    if (!partial.renameTo(destination)) throw IOException("No se pudo guardar ${artifact.fileName}")
                    verified[destination.absolutePath] = destination.length() to destination.lastModified()
                    downloadedBytes += artifact.bytes
                    cleanupSuccessfulTransfer(artifact)
                    return@forEach
                }
                failure = "La integridad de ${artifact.fileName} no coincide con SHA-256; vuelve a descargar"
                downloads.remove(id)
                clearDownloadId(artifact)
                partial.delete()
            } else if (record != null && record.status != DownloadManager.STATUS_FAILED) {
                active = true
                downloadedBytes += record.bytes.coerceIn(0L, artifact.bytes)
            } else if (record?.status == DownloadManager.STATUS_FAILED) {
                failure = "Falló la descarga de ${artifact.fileName} (código ${record.reason}); puedes reintentar"
            } else if (id != NO_DOWNLOAD) {
                failure = "La descarga de ${artifact.fileName} fue interrumpida; puedes reintentar"
                clearDownloadId(artifact)
            } else if (destination.exists()) {
                failure = "El archivo ${artifact.fileName} está incompleto o no supera la comprobación SHA-256"
            }
            allVerified = false
        }
        val percentage = if (totalBytes == 0L) 100 else ((downloadedBytes * 100L) / totalBytes).toInt().coerceIn(0, 100)
        if (operationError != null) return KokoroModelState(KokoroModelStatus.ERROR, percentage, operationError, downloadedBytes, totalBytes)
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
                throw IOException("No se pudo guardar la revisión instalada de Kokoro")
            }
            return KokoroModelState(KokoroModelStatus.INSTALLED, 100, downloadedBytes = downloadedBytes, totalBytes = totalBytes)
        }
        if (failure != null) preferences.edit().putString(errorKey(artifacts), failure).apply()
        // Keep cancellation visible until every in-flight transfer has stopped, even if
        // another artifact failed. The terminal snapshot then exposes the retryable error.
        if (active) return KokoroModelState(KokoroModelStatus.DOWNLOADING, percentage, failure, downloadedBytes, totalBytes)
        if (failure != null) return KokoroModelState(KokoroModelStatus.ERROR, percentage, failure, downloadedBytes, totalBytes)
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
        if (artifact.localImportOnly) return NO_DOWNLOAD
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
                    if (!root.isDirectory && !root.mkdirs()) throw IOException("No se pudo crear el directorio de recursos")
                    val expected = KokoroModelManifest.artifacts.associateBy { it.fileName }
                    val installed = mutableSetOf<String>()
                    appContext.contentResolver.openInputStream(uri)?.use { input ->
                        ZipInputStream(input.buffered()).use { zip ->
                            while (true) {
                                val entry = zip.nextEntry ?: break
                                val artifact = expected[entry.name] ?: throw IOException("El paquete contiene un recurso no declarado: ${entry.name}")
                                if (entry.isDirectory || !installed.add(entry.name)) throw IOException("El paquete contiene una entrada repetida o no válida")
                                val temporary = ownedFile(root, "${artifact.fileName}.import-part")
                                try {
                                    temporary.outputStream().buffered().use { output ->
                                        val buffer = ByteArray(64 * 1024)
                                        var bytes = 0L
                                        while (true) {
                                            val count = zip.read(buffer)
                                            if (count < 0) break
                                            bytes += count
                                            if (bytes > artifact.bytes) throw IOException("Tamaño incorrecto: ${artifact.fileName}")
                                            output.write(buffer, 0, count)
                                        }
                                    }
                                    if (!KokoroModelManifest.verify(temporary, artifact)) throw IOException("No coincide SHA-256: ${artifact.fileName}")
                                    val destination = ownedFile(root, artifact.fileName)
                                    if (!isVerified(destination, artifact)) {
                                        if (destination.exists() && !destination.delete()) throw IOException("No se pudo sustituir ${artifact.fileName}")
                                        if (!temporary.renameTo(destination)) throw IOException("No se pudo guardar ${artifact.fileName}")
                                        verified.remove(destination.absolutePath)
                                    }
                                    writeOperationError(listOf(artifact), null)
                                } finally { temporary.delete() }
                                zip.closeEntry()
                            }
                        }
                    } ?: throw IOException("No se pudo abrir el paquete seleccionado")
                    if (installed.isEmpty()) throw IOException("El paquete no contiene recursos")
                    preferences.edit().putStringSet(KEY_MANAGED_REVISIONS, managedRevisions() + KokoroModelManifest.revision).commit()
                }
                null
            } catch (error: Exception) { error.message ?: "No se pudieron importar los recursos" }
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
        if (directory.canonicalFile != File(root.canonicalFile, revision)) throw IOException("Directorio de Kokoro no válido")
        return directory
    }

    private fun storageRoot(): File {
        val privateRoot = appContext.getExternalFilesDir(null)?.canonicalFile
            ?: throw IOException("El almacenamiento privado de la aplicación no está disponible")
        val root = appContext.getExternalFilesDir("kokoro")
            ?: throw IOException("El almacenamiento privado de la aplicación no está disponible")
        if (root.canonicalFile.parentFile != privateRoot || root.canonicalFile.name != "kokoro") {
            throw IOException("Directorio privado de Kokoro no válido")
        }
        return root
    }

    private fun ownedFile(directory: File, name: String): File {
        val file = File(directory, name)
        if (file.canonicalFile.parentFile != directory.canonicalFile) throw IOException("Archivo de Kokoro no válido")
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
