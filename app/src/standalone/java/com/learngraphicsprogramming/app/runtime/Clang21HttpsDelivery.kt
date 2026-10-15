package com.learngraphicsprogramming.app.runtime

import android.app.DownloadManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.content.pm.PackageInstaller
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.provider.Settings
import android.util.Log
import androidx.activity.result.ActivityResultLauncher
import androidx.activity.result.IntentSenderRequest
import com.learngraphicsprogramming.app.runtime.Clang21FeatureDelivery.Phase
import com.learngraphicsprogramming.app.runtime.Clang21FeatureDelivery.State
import java.io.File
import java.security.MessageDigest
import java.util.concurrent.CopyOnWriteArraySet
import java.util.concurrent.CancellationException
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicLong

/** Standalone APK distribution only. No executable is ever launched from staging. */
internal class Clang21HttpsDelivery(context: Context) : Clang21Delivery {
    private val engine = Clang21HttpsEngine.get(context)
    private var observation: ((State) -> Unit)? = null

    init { engine.recover() }

    override fun currentState(): State = engine.currentState()

    override fun observe(onUpdate: (State) -> Unit) {
        observation?.let(engine::unobserve)
        observation = onUpdate
        engine.observe(onUpdate)
    }

    override fun requestInstall(onUpdate: (State) -> Unit) {
        observe(onUpdate)
        engine.requestInstall()
    }

    override fun requestUserConfirmation(launcher: ActivityResultLauncher<IntentSenderRequest>): Boolean =
        engine.requestUserConfirmation(launcher)

    override fun cancel(onFailure: (String?) -> Unit) {
        runCatching { engine.cancel() }.onFailure { onFailure(it.message) }
    }

    override fun close() {
        observation?.let(engine::unobserve)
        observation = null
    }
}

/** Process-wide ownership prevents recreated activities from starting competing installers. */
internal class Clang21HttpsEngine private constructor(context: Context) {
    companion object {
        private const val TAG = "Clang21Https"
        private const val PREFS = "clang21_https_delivery"
        private const val RECIPE_ASSET = "runtime/clang21_installation/recipe.json"
        private const val ACTIVE = "active"
        private const val SESSION = "installer_session"
        private const val PENDING_INTENT = "confirmation_intent"
        private const val CONFIRM_PERMISSION = "confirmation_permission"
        private const val JOB_ID = -2
        private const val PERMISSION_ID = -1
        @Volatile private var instance: Clang21HttpsEngine? = null

        fun get(context: Context): Clang21HttpsEngine = instance ?: synchronized(this) {
            instance ?: Clang21HttpsEngine(context.applicationContext).also { instance = it }
        }
    }

    private val app = context.applicationContext
    private val preferences = app.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
    private val manager = app.getSystemService(Context.DOWNLOAD_SERVICE) as DownloadManager
    private val installer = app.packageManager.packageInstaller
    private val main = Handler(Looper.getMainLooper())
    private val worker = Executors.newSingleThreadExecutor { task ->
        Thread(task, "clang21-https-installer").apply { isDaemon = true }
    }
    private val running = AtomicBoolean(false)
    private val generation = AtomicLong(0)
    private val observers = CopyOnWriteArraySet<(State) -> Unit>()
    private val recipe by lazy {
        app.assets.open(RECIPE_ASSET).bufferedReader().use { Clang21InstallationRecipe.parse(it.readText()) }
    }
    private val privateRoot get() = File(app.filesDir, "clang21-https-installation")
    private val stage get() = File(privateRoot, "staging")
    private val downloadRoot get() = File(
        checkNotNull(app.getExternalFilesDir(null)) { "Private download storage is unavailable" },
        "clang21-https-downloads"
    )

    fun observe(observer: (State) -> Unit) {
        observers.add(observer)
        dispatch(observer, currentState())
    }

    fun unobserve(observer: (State) -> Unit) { observers.remove(observer) }

    private fun dispatch(observer: (State) -> Unit, state: State) {
        main.post { if (observer in observers) observer(state) }
    }

    private fun platformInstalled(): Boolean = runCatching {
        app.packageManager.getPackageInfo(app.packageName, 0).splitNames
            ?.contains(Clang21FeatureDelivery.MODULE_NAME) == true
    }.getOrDefault(false)

    private fun allowedToInstall(): Boolean = Build.VERSION.SDK_INT < 26 ||
        app.packageManager.canRequestPackageInstalls()

    private fun storedState(): State {
        val phase = runCatching {
            Phase.valueOf(preferences.getString("phase", Phase.NOT_INSTALLED.name)!!)
        }.getOrDefault(Phase.NOT_INSTALLED)
        val session = preferences.getInt(SESSION, -1)
        return State(phase, preferences.getLong("downloaded", 0), preferences.getLong("total", 0),
            when {
                preferences.getBoolean(CONFIRM_PERMISSION, false) -> PERMISSION_ID
                session >= 0 -> session
                preferences.getBoolean(ACTIVE, false) -> JOB_ID
                else -> null
            },
            preferences.getInt("error", 0).takeIf { it != 0 },
            preferences.getString("message", null))
    }

    fun currentState(): State {
        if (platformInstalled()) {
            if (storedState().phase != Phase.INSTALLED) finishInstalled()
            return storedState()
        }
        if (preferences.getBoolean(ACTIVE, false) &&
            preferences.getBoolean(CONFIRM_PERMISSION, false) && allowedToInstall()) {
            preferences.edit().putBoolean(CONFIRM_PERMISSION, false).commit()
            publish(State(Phase.REQUESTED, sessionId = JOB_ID))
            startWorker()
        }
        val state = storedState()
        // An installed split may have been removed by an APK update. Do not keep
        // claiming that its old successful installation is still available.
        val available = if (state.phase == Phase.INSTALLED) {
            publish(State(Phase.NOT_INSTALLED))
            storedState()
        } else state
        return if (available.totalBytes == 0L) runCatching {
            available.copy(totalBytes = recipe.archives.sumOf { it.bytes })
        }.getOrElse { available.copy(phase = Phase.FAILED, message = it.message) } else available
    }

    fun recover() {
        currentState()
        if (preferences.getBoolean(ACTIVE, false)) startWorker()
    }

    @Synchronized fun requestInstall() {
        if (platformInstalled()) { finishInstalled(); return }
        if (!preferences.getBoolean(ACTIVE, false)) {
            // A failed cancellation must not forget an Android-owned committed
            // session and start a second installation on the next retry.
            val previousSession = preferences.getInt(SESSION, -1)
            val retainedSession = if (previousSession >= 0) runCatching {
                installer.getSessionInfo(previousSession) != null
            }.getOrElse {
                publish(State(Phase.FAILED, sessionId = previousSession, message = it.message))
                return
            } else false
            generation.incrementAndGet()
            preferences.edit().putBoolean(ACTIVE, true).apply {
                if (!retainedSession) remove(PENDING_INTENT).putBoolean(CONFIRM_PERMISSION, false).remove(SESSION)
            }.commit()
            publish(State(Phase.REQUESTED, sessionId = if (retainedSession) previousSession else JOB_ID))
        }
        startWorker()
    }

    @Synchronized private fun publish(state: State, token: Long? = null) {
        if (token != null && !active(token)) return
        val old = storedState()
        if (old.phase == state.phase && old.downloadedBytes == state.downloadedBytes &&
            old.totalBytes == state.totalBytes && old.errorCode == state.errorCode && old.message == state.message) {
            return
        }
        preferences.edit().putString("phase", state.phase.name)
            .putLong("downloaded", state.downloadedBytes).putLong("total", state.totalBytes)
            .putInt("error", state.errorCode ?: 0).putString("message", state.message).commit()
        val delivered = storedState()
        observers.forEach { dispatch(it, delivered) }
    }

    private fun active(token: Long) = preferences.getBoolean(ACTIVE, false) && generation.get() == token
    private fun requireActive(token: Long) {
        if (!active(token)) throw CancellationException("Compiler installation canceled")
    }

    private fun startWorker() {
        if (!preferences.getBoolean(ACTIVE, false) || !running.compareAndSet(false, true)) return
        val token = generation.get()
        worker.execute {
            try { perform(token) }
            catch (_: CancellationException) { /* The user cancellation already published its state. */ }
            catch (error: Exception) {
                synchronized(this) {
                    if (active(token)) {
                        Log.e(TAG, "Clang 21 HTTPS installation failed", error)
                        preferences.edit().putBoolean(ACTIVE, false).commit()
                        publish(State(Phase.FAILED, message = error.message ?: error.javaClass.simpleName))
                    }
                }
            } finally {
                running.set(false)
                if (generation.get() != token && preferences.getBoolean(ACTIVE, false)) startWorker()
            }
        }
    }

    @Suppress("DEPRECATION")
    private fun verifyInstalledIdentity() {
        check(recipe.packageName == app.packageName) { "Compiler split belongs to another application" }
        val flags = if (Build.VERSION.SDK_INT >= 28) PackageManager.GET_SIGNING_CERTIFICATES else PackageManager.GET_SIGNATURES
        val info = app.packageManager.getPackageInfo(app.packageName, flags)
        val version = if (Build.VERSION.SDK_INT >= 28) info.longVersionCode else info.versionCode.toLong()
        check(version == recipe.versionCode) { "Compiler split version does not match this application" }
        val certificates = if (Build.VERSION.SDK_INT >= 28) info.signingInfo?.apkContentsSigners else info.signatures
        val hashes = certificates.orEmpty().map { signature ->
            MessageDigest.getInstance("SHA-256").digest(signature.toByteArray()).joinToString("") { "%02x".format(it) }
        }
        check(hashes.size == 1 && hashes.single() == recipe.certificateSha256) {
            "Compiler split signing certificate does not match this application"
        }
    }

    private fun perform(token: Long) {
        requireActive(token)
        if (platformInstalled()) { finishInstalled(); return }
        verifyInstalledIdentity()
        if (!allowedToInstall()) {
            preferences.edit().putBoolean(CONFIRM_PERMISSION, true).commit()
            publish(State(Phase.NEEDS_CONFIRMATION, sessionId = PERMISSION_ID), token)
            return
        }
        val existingSession = preferences.getInt(SESSION, -1)
        if (existingSession >= 0) {
            val info = installer.getSessionInfo(existingSession)
            if (info == null) {
                error("The previous Android installation session is no longer available; retry the installation")
            }
            if (info.isSealed) {
                publish(State(if (preferences.contains(PENDING_INTENT)) Phase.NEEDS_CONFIRMATION else Phase.INSTALLING,
                    sessionId = existingSession), token)
                return
            }
            // A process interruption during staging left an uncommitted session.
            // Discard only this incomplete session, then reuse verified downloads.
            installer.abandonSession(existingSession)
            preferences.edit().remove(SESSION).commit()
        }
        downloadRoot.mkdirs()
        val total = recipe.archives.sumOf { it.bytes }
        var complete = 0L
        val archives = linkedMapOf<String, File>()
        recipe.archives.forEach { archive ->
            requireActive(token)
            val file = File(downloadRoot, archive.name)
            if (!Clang21InstallationFiles.valid(file, archive.bytes, archive.sha256)) {
                acquireArchive(archive, file, complete, total, token)
                requireActive(token)
                publish(State(Phase.DOWNLOADED, complete + archive.bytes, total, JOB_ID), token)
                check(Clang21InstallationFiles.valid(file, archive.bytes, archive.sha256)) {
                    "Downloaded compiler archive SHA-256 mismatch: ${archive.name}"
                }
            }
            Log.i(TAG, "Verified HTTPS archive ${archive.name} sha256=${archive.sha256}")
            archives[archive.name] = file
            complete += archive.bytes
        }
        requireActive(token)
        publish(State(Phase.DOWNLOADED, complete, total, JOB_ID), token)
        stage.mkdirs()
        val libraryDirectory = File(stage, "libraries")
        Clang21InstallationFiles.extractLibraries(recipe, archives, libraryDirectory) { !active(token) }
        val envelope = File(stage, "envelope.bin")
        if (!Clang21InstallationFiles.valid(envelope, recipe.envelopeBytes, recipe.envelopeSha256)) {
            app.assets.open(recipe.envelopeFile).use { input -> envelope.outputStream().use(input::copyTo) }
        }
        val apk = File(stage, "clang21.apk")
        Clang21InstallationFiles.reconstruct(recipe, envelope, libraryDirectory, apk) { !active(token) }
        Log.i(TAG, "Reconstructed signed compiler split sha256=${recipe.apkSha256}")
        requireActive(token)
        stageInstallation(apk, token, total)
    }

    private fun acquireArchive(
        archive: Clang21InstallationRecipe.Archive, file: File,
        complete: Long, total: Long, token: Long
    ) {
        val key = "download:${archive.name}"
        var id = preferences.getLong(key, -1)
        var snapshot = if (id >= 0) download(id) else null
        if (snapshot == null || snapshot.status == DownloadManager.STATUS_FAILED ||
            snapshot.status == DownloadManager.STATUS_SUCCESSFUL) {
            if (id >= 0) manager.remove(id)
            check(!file.exists() || file.delete()) { "Cannot replace incomplete compiler archive" }
            val request = DownloadManager.Request(Uri.parse(archive.url))
                .setTitle("Clang 21 · ${archive.name}")
                .setDescription("LearnGraphicsProgramming")
                .setAllowedOverMetered(true)
                .setAllowedOverRoaming(false)
                .setNotificationVisibility(DownloadManager.Request.VISIBILITY_VISIBLE_NOTIFY_COMPLETED)
                .setDestinationUri(Uri.fromFile(file))
            synchronized(this) {
                requireActive(token)
                id = manager.enqueue(request)
                preferences.edit().putLong(key, id).commit()
            }
            Log.i(TAG, "Enqueued DownloadManager id=$id HTTPS=${archive.url}")
        }
        while (true) {
            requireActive(token)
            snapshot = download(id) ?: error("The Android download was removed; retry the installation")
            when (snapshot.status) {
                DownloadManager.STATUS_SUCCESSFUL -> return
                DownloadManager.STATUS_FAILED -> error("Android download failed (${snapshot.reason}): ${archive.name}")
                DownloadManager.STATUS_PENDING, DownloadManager.STATUS_PAUSED ->
                    publish(State(Phase.PENDING, complete + snapshot.bytes.coerceIn(0, archive.bytes), total, JOB_ID), token)
                else -> publish(State(Phase.DOWNLOADING,
                    complete + snapshot.bytes.coerceIn(0, archive.bytes), total, JOB_ID), token)
            }
            Thread.sleep(750)
        }
    }

    private data class Download(val status: Int, val bytes: Long, val reason: Int)
    private fun download(id: Long): Download? = manager.query(DownloadManager.Query().setFilterById(id))?.use { cursor ->
        if (!cursor.moveToFirst()) null else Download(
            cursor.getInt(cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_STATUS)),
            cursor.getLong(cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_BYTES_DOWNLOADED_SO_FAR)),
            cursor.getInt(cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_REASON)))
    }

    private fun stageInstallation(apk: File, token: Long, total: Long) {
        verifyInstalledIdentity()
        requireActive(token)
        val params = PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_INHERIT_EXISTING).apply {
            setAppPackageName(app.packageName)
            setSize(recipe.apkBytes)
            if (Build.VERSION.SDK_INT >= 31) setRequireUserAction(PackageInstaller.SessionParams.USER_ACTION_REQUIRED)
            if (Build.VERSION.SDK_INT >= 34) setDontKillApp(true)
        }
        val id = synchronized(this) {
            requireActive(token)
            installer.createSession(params).also {
                preferences.edit().putInt(SESSION, it).remove(PENDING_INTENT)
                    .putBoolean(CONFIRM_PERMISSION, false).commit()
            }
        }
        publish(State(Phase.INSTALLING, total, total, id), token)
        try {
            installer.openSession(id).use { session ->
                session.openWrite("clang21.apk", 0, recipe.apkBytes).use { destination ->
                    apk.inputStream().buffered().use { input ->
                        val buffer = ByteArray(1024 * 1024)
                        while (true) {
                            requireActive(token)
                            val count = input.read(buffer)
                            if (count < 0) break
                            destination.write(buffer, 0, count)
                        }
                    }
                    session.fsync(destination)
                }
                requireActive(token)
                val callback = Intent(app, Clang21InstallReceiver::class.java)
                    .setAction("${app.packageName}.CLANG21_INSTALL_RESULT")
                val flags = PendingIntent.FLAG_UPDATE_CURRENT or
                    if (Build.VERSION.SDK_INT >= 31) PendingIntent.FLAG_MUTABLE else 0
                session.commit(PendingIntent.getBroadcast(app, id, callback, flags).intentSender)
            }
        } catch (error: Exception) {
            runCatching { installer.abandonSession(id) }
            preferences.edit().remove(SESSION).commit()
            throw error
        }
    }

    fun requestUserConfirmation(launcher: ActivityResultLauncher<IntentSenderRequest>): Boolean {
        val state = currentState()
        if (state.phase != Phase.NEEDS_CONFIRMATION) return false
        val intent = if (preferences.getBoolean(CONFIRM_PERMISSION, false)) {
            Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:${app.packageName}"))
        } else {
            val encoded = preferences.getString(PENDING_INTENT, null) ?: return false
            Intent.parseUri(encoded, Intent.URI_INTENT_SCHEME)
        }
        val pending = PendingIntent.getActivity(app, state.sessionId ?: JOB_ID,
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)
        launcher.launch(IntentSenderRequest.Builder(pending.intentSender).build())
        return true
    }

    @Suppress("DEPRECATION")
    fun installationResult(intent: Intent) {
        val id = intent.getIntExtra(PackageInstaller.EXTRA_SESSION_ID, -1)
        if (id < 0 || id != preferences.getInt(SESSION, -1)) return
        when (val status = intent.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE)) {
            PackageInstaller.STATUS_PENDING_USER_ACTION -> {
                val confirmation = if (Build.VERSION.SDK_INT >= 33)
                    intent.getParcelableExtra(Intent.EXTRA_INTENT, Intent::class.java)
                else intent.getParcelableExtra<Intent>(Intent.EXTRA_INTENT)
                if (confirmation == null) {
                    preferences.edit().putBoolean(ACTIVE, false).remove(SESSION).commit()
                    publish(State(Phase.FAILED, message = "Android did not supply an installation confirmation"))
                } else {
                    preferences.edit().putString(PENDING_INTENT, confirmation.toUri(Intent.URI_INTENT_SCHEME))
                        .putBoolean(CONFIRM_PERMISSION, false).commit()
                    publish(State(Phase.NEEDS_CONFIRMATION, sessionId = id))
                }
            }
            PackageInstaller.STATUS_SUCCESS -> finishInstalled()
            else -> {
                preferences.edit().putBoolean(ACTIVE, false).remove(SESSION).remove(PENDING_INTENT).commit()
                val canceled = status == PackageInstaller.STATUS_FAILURE_ABORTED
                publish(State(if (canceled) Phase.CANCELED else Phase.FAILED,
                    errorCode = if (canceled) null else status,
                    message = intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE)))
            }
        }
    }

    private fun finishInstalled() {
        preferences.edit().putBoolean(ACTIVE, false).remove(SESSION).remove(PENDING_INTENT)
            .putBoolean(CONFIRM_PERMISSION, false).commit()
        publish(State(Phase.INSTALLED))
        // Sources remain verified/reusable; temporary uninstalled copies are expendable.
        worker.execute { stage.deleteRecursively() }
    }

    @Synchronized fun cancel() {
        if (platformInstalled()) { finishInstalled(); return }
        generation.incrementAndGet()
        preferences.edit().putBoolean(ACTIVE, false).commit()
        publish(State(Phase.CANCELING))
        val id = preferences.getInt(SESSION, -1)
        var failure: Throwable? = null
        if (id >= 0) runCatching { installer.abandonSession(id) }.onFailure { failure = it }
        try {
            // Completed archives are retained and reverified on retry; only our
            // own unfinished DownloadManager rows are removed.
            preferences.all.filterKeys { it.startsWith("download:") }.forEach { (key, value) ->
                val downloadId = value as? Long ?: return@forEach
                if (download(downloadId)?.status != DownloadManager.STATUS_SUCCESSFUL) {
                    manager.remove(downloadId)
                    preferences.edit().remove(key).commit()
                }
            }
        } catch (error: Exception) {
            if (failure == null) failure = error else failure?.addSuppressed(error)
        }
        if (platformInstalled()) { finishInstalled(); return }
        val sessionRemains = if (id >= 0) runCatching { installer.getSessionInfo(id) != null }.getOrElse { error ->
            if (failure == null) failure = error else failure?.addSuppressed(error)
            // An inspection failure is not evidence that Android released it.
            true
        } else false
        preferences.edit().putBoolean(CONFIRM_PERMISSION, false).apply {
            if (!sessionRemains) remove(SESSION).remove(PENDING_INTENT)
        }.commit()
        val error = failure
        if (error != null) {
            Log.w(TAG, "Android compiler installation cancellation failed", error)
            publish(State(Phase.FAILED, message = error.message ?: error.javaClass.simpleName))
            throw error
        }
        publish(State(Phase.CANCELED))
    }
}
