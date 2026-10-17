package com.learngraphicsprogramming.app.narration

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Intent
import android.content.Context
import android.content.SharedPreferences
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder
import android.os.Handler
import android.os.Looper
import android.os.PowerManager
import android.os.SystemClock
import com.learngraphicsprogramming.app.MainActivity
import com.learngraphicsprogramming.app.R

/**
 * User-visible offline narration preparation that remains active when the
 * screen is locked. A bounded PARTIAL_WAKE_LOCK is renewed during each
 * synthesis slice and held while waiting for interactive work to finish, so
 * screen-off idle time cannot indefinitely suspend the continuation.
 *
 * This is a directly launched foreground service, not a JobScheduler worker:
 * Android 16 job quotas can otherwise suspend long-running WorkManager jobs.
 */
internal class KokoroContinuousNarrationService : Service() {
    @Volatile private var running = false
    @Volatile private var thread: Thread? = null
    @Volatile private var worker: KokoroBackgroundNarrationWorker? = null
    private lateinit var cpuWakeLock: PowerManager.WakeLock
    private var foregroundStarted = false
    // Main-thread only: a refresh received during an ending session must not be lost.
    private var startEpoch = 0L
    private val main = Handler(Looper.getMainLooper())
    private lateinit var settingsPreferences: SharedPreferences
    // Keep a strong reference: SharedPreferences listeners are weakly held.
    private val settingsListener = SharedPreferences.OnSharedPreferenceChangeListener { _, key ->
        if (key == "enabled" || key == "engine" ||
            key == NarrationSettingsRepository.KEY_ADVANCE_GENERATION) {
            val stopIfDisabled = {
                if (!narrationAllowed()) {
                    running = false
                    worker?.cancel()
                    thread?.interrupt()
                    // Let the existing epoch/finally hand-off finish the old
                    // worker before stopping or honoring a subsequent ON.
                    if (thread?.isAlive != true) stopSelf()
                }
            }
            if (Looper.myLooper() == Looper.getMainLooper()) stopIfDisabled()
            else main.post { stopIfDisabled() }
        } else if (NarrationSettingsRepository.isCourseGenerationPreference(key)) {
            // A course can be paused while phonemizing or synthesizing a
            // sentence. Cancel only this background worker and reconsider
            // eligible courses. The foreground reader is a different owner.
            val reconsider = { worker?.cancel() }
            if (Looper.myLooper() == Looper.getMainLooper()) reconsider()
            else main.post { reconsider() }
        }
    }
    private var wakeRenewedAt = 0L
    private val heartbeat = object : Runnable {
        override fun run() {
            if (!foregroundStarted) return
            val current = thread
            val fields = mapOf<String, Any>("serviceRunning" to running,
                "serviceThreadAlive" to (current?.isAlive == true),
                "serviceThreadState" to (current?.state?.name ?: "NONE"),
                "wakeLockHeld" to cpuWakeLock.isHeld, "interactiveBusy" to NarrationWorkPriority.isBusy)
            KokoroNarrationDiagnostics.update(applicationContext, fields)
            KokoroNarrationDiagnostics.event(applicationContext, TAG, "Offline narration heartbeat", fields = fields)
            main.postDelayed(this, HEARTBEAT_MS)
        }
    }

    override fun onCreate() {
        super.onCreate()
        cpuWakeLock = (getSystemService(POWER_SERVICE) as PowerManager)
            .newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "LearnGraphicsProgramming:OfflineKokoro")
            .apply { setReferenceCounted(false) }
        if (Build.VERSION.SDK_INT >= 26) {
            val manager = getSystemService(NotificationManager::class.java)
            manager.createNotificationChannel(
                NotificationChannel(
                    CHANNEL_ID,
                    getString(R.string.narration_background_notification_title),
                    NotificationManager.IMPORTANCE_LOW
                ).apply {
                    description = getString(R.string.narration_background_notification_text)
                    setShowBadge(false)
                }
            )
        }
        // A stopService request can arrive before Android delivers onStartCommand.
        // Acknowledge startForegroundService as soon as this instance exists,
        // before any master-switch check or cancellation can stop it.
        ensureForegroundStarted()
        settingsPreferences = applicationContext.getSharedPreferences("lesson_narration_settings", Context.MODE_PRIVATE)
        settingsPreferences.registerOnSharedPreferenceChangeListener(settingsListener)
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (!ensureForegroundStarted()) return START_NOT_STICKY
        // A foreground-service intent may have been queued before the master
        // switch was turned off. Fulfil Android's promotion contract first,
        // then stop without acquiring an engine or starting synthesis.
        if (!narrationAllowed()) {
            running = false
            worker?.cancel()
            thread?.interrupt()
            if (thread?.isAlive != true) stopSelfResult(startId)
            return START_NOT_STICKY
        }
        KokoroNarrationDiagnostics.event(applicationContext, TAG, "Offline narration start command",
            fields = mapOf("startId" to startId, "threadAlive" to (thread?.isAlive == true)))
        startEpoch++
        // A rapid OFF -> ON can arrive while the cancelled worker is still
        // releasing its sessions. Its finally block owns the safe replacement.
        running = true
        if (thread?.isAlive != true) {
            launchSession()
        }
        return START_NOT_STICKY
    }

    private fun ensureForegroundStarted(): Boolean {
        if (!foregroundStarted) {
            try {
                val notification = createNotification()
                if (Build.VERSION.SDK_INT >= 34) {
                    startForeground(NOTIFICATION_ID, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE)
                } else {
                    startForeground(NOTIFICATION_ID, notification)
                }
                foregroundStarted = true
            } catch (error: RuntimeException) {
                KokoroNarrationDiagnostics.event(applicationContext, TAG, "Cannot start foreground narration; Android denied the service", error)
                stopSelf()
                return false
            }
            main.post(heartbeat)
        }
        return true
    }

    @Suppress("DEPRECATION")
    private fun createNotification(): Notification {
        val launch = Intent(this, MainActivity::class.java)
            .addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP or Intent.FLAG_ACTIVITY_CLEAR_TOP)
        val pending = PendingIntent.getActivity(
            this, 0, launch, PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )
        val builder = if (Build.VERSION.SDK_INT >= 26) Notification.Builder(this, CHANNEL_ID)
            else Notification.Builder(this)
        return builder
            .setSmallIcon(android.R.drawable.stat_sys_download)
            .setContentTitle(getString(R.string.narration_background_notification_title))
            .setContentText(getString(R.string.narration_background_notification_text))
            .setContentIntent(pending)
            .setOngoing(true)
            .setCategory(Notification.CATEGORY_SERVICE)
            .build()
    }

    private fun launchSession() {
        if (!foregroundStarted || !narrationAllowed()) {
            running = false
            stopSelf()
            return
        }
        val epoch = startEpoch
        running = true
        val next = Thread({ runSession(epoch) }, "KokoroOfflineNarration").apply {
            priority = Thread.MIN_PRIORITY
        }
        thread = next
        next.start()
    }

    private fun runSession(epoch: Long) {
        val current = Thread.currentThread()
        KokoroNarrationDiagnostics.update(applicationContext, mapOf("serviceRunning" to true,
            "serviceThreadAlive" to true, "serviceThreadState" to current.state.name))
        KokoroNarrationDiagnostics.event(applicationContext, TAG, "Foreground offline narration session started")
        val currentWorker = KokoroBackgroundNarrationWorker(applicationContext)
        worker = currentWorker
        try {
            while (running && !Thread.currentThread().isInterrupted) {
                val selection = NarrationSettingsRepository.get(applicationContext)
                if (!selection.enabled || selection.engine == NarrationEngine.ANDROID ||
                    !NarrationSettingsRepository.advanceGenerationEnabled(applicationContext)) break
                if (NarrationWorkPriority.isBusy) {
                    // With the screen locked, remain schedulable until the
                    // higher-priority speech/editor operation is finished.
                    renewWakeLock()
                    Thread.sleep(1_000L)
                    continue
                }
                // A slice lasts at most four minutes; renew the five-minute
                // lock before each pass. Delayed retries remain schedulable.
                renewWakeLock()
                val outcome = currentWorker.runSlice {
                    val settings = NarrationSettingsRepository.get(applicationContext)
                    running && !Thread.currentThread().isInterrupted &&
                        settings.enabled && settings.engine != NarrationEngine.ANDROID &&
                            NarrationSettingsRepository.advanceGenerationEnabled(applicationContext)
                }
                KokoroNarrationDiagnostics.update(applicationContext, mapOf("lastOutcome" to outcome.name))
                when (outcome) {
                    KokoroBackgroundNarrationWorker.Outcome.MORE -> continue
                    KokoroBackgroundNarrationWorker.Outcome.BUSY -> Thread.sleep(1_000L)
                    KokoroBackgroundNarrationWorker.Outcome.RETRY -> Thread.sleep(20_000L)
                    KokoroBackgroundNarrationWorker.Outcome.COMPLETE -> {
                        KokoroNarrationDiagnostics.event(applicationContext, TAG, "Foreground offline narration curriculum scan complete")
                        break
                    }
                    KokoroBackgroundNarrationWorker.Outcome.PAUSED -> {
                        KokoroNarrationDiagnostics.event(applicationContext, TAG, "Foreground offline narration paused (settings/storage)")
                        val full = runCatching {
                            !NarrationStorageBudget.fitsNext(NarrationStorageBudget.snapshot(applicationContext),
                                4L * NarrationStorageBudget.MIB)
                        }.getOrDefault(false)
                        if (full) KokoroNarrationSessionMonitor.resumeWhenStorageAvailable(applicationContext)
                        break
                    }
                    KokoroBackgroundNarrationWorker.Outcome.UNAVAILABLE -> {
                        KokoroNarrationDiagnostics.event(applicationContext, TAG, "Foreground offline narration waiting for installed Kokoro resources")
                        break
                    }
                }
            }
        } catch (_: InterruptedException) {
            Thread.currentThread().interrupt()
        } catch (error: Throwable) {
            KokoroNarrationDiagnostics.event(applicationContext, TAG, "Foreground offline narration stopped by unexpected error", error)
        } finally {
            runCatching { KokoroEngineCoordinator.releaseIfIdle() }.onFailure { error ->
                KokoroNarrationDiagnostics.event(applicationContext, TAG, "Could not release idle narration engine", error)
            }
            KokoroNarrationDiagnostics.event(applicationContext, TAG, "Foreground offline narration session stopped")
            // All service lifecycle changes are serialized on main. An older
            // finished thread must not stop a newer onStartCommand session.
            main.post {
                if (thread === current) {
                    thread = null
                    if (running && foregroundStarted && startEpoch != epoch && narrationAllowed()) {
                        // The old worker has completely finished. Its wake lock
                        // remains held until this replacement can resume pending work.
                        launchSession()
                    } else {
                        running = false
                        if (cpuWakeLock.isHeld) cpuWakeLock.release()
                        KokoroNarrationDiagnostics.update(applicationContext, mapOf("serviceRunning" to false,
                            "serviceThreadAlive" to false, "serviceThreadState" to "TERMINATED", "wakeLockHeld" to false))
                        stopSelf()
                    }
                }
            }
        }
    }

    private fun renewWakeLock() {
        val now = SystemClock.elapsedRealtime()
        if (!cpuWakeLock.isHeld || now - wakeRenewedAt >= 60_000L) {
            cpuWakeLock.acquire(5L * 60L * 1_000L)
            wakeRenewedAt = now
        }
    }

    private fun narrationAllowed(): Boolean {
        if (!NarrationSettingsRepository.isEnabled(applicationContext)) return false
        return NarrationSettingsRepository.get(applicationContext).engine != NarrationEngine.ANDROID &&
            NarrationSettingsRepository.advanceGenerationEnabled(applicationContext)
    }

    override fun onTaskRemoved(rootIntent: Intent?) {
        // Swiping the task away explicitly closes this preparation session.
        stopSelf()
        super.onTaskRemoved(rootIntent)
    }

    override fun onDestroy() {
        if (::settingsPreferences.isInitialized) {
            settingsPreferences.unregisterOnSharedPreferenceChangeListener(settingsListener)
        }
        running = false
        worker?.cancel()
        thread?.interrupt()
        main.removeCallbacks(heartbeat)
        foregroundStarted = false
        if (::cpuWakeLock.isInitialized && cpuWakeLock.isHeld) cpuWakeLock.release()
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    private companion object {
        const val TAG = "KokoroBackground"
        const val CHANNEL_ID = "kokoro_offline_narration"
        const val NOTIFICATION_ID = 17023
        const val HEARTBEAT_MS = 30_000L
    }
}
