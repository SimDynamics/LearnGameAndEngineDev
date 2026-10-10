package com.learngraphicsprogramming.app.narration

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder
import android.os.PowerManager
import android.util.Log
import com.learngraphicsprogramming.app.MainActivity
import com.learngraphicsprogramming.app.R

/**
 * User-visible offline narration preparation that remains active when the
 * screen is locked. A bounded PARTIAL_WAKE_LOCK is held only during each
 * synthesis pass, not during playback priority waits or retry delays.
 *
 * This is a directly launched foreground service, not a JobScheduler worker:
 * Android 16 job quotas can otherwise suspend long-running WorkManager jobs.
 */
internal class KokoroContinuousNarrationService : Service() {
    @Volatile private var running = false
    private var thread: Thread? = null
    private lateinit var cpuWakeLock: PowerManager.WakeLock
    private var foregroundStarted = false

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
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
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
                Log.e(TAG, "Cannot start foreground narration; Android denied the service", error)
                stopSelf()
                return START_NOT_STICKY
            }
        }
        if (!running) {
            running = true
            thread = Thread({ runSession() }, "KokoroOfflineNarration").apply {
                priority = Thread.MIN_PRIORITY
                start()
            }
        }
        return START_NOT_STICKY
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

    private fun runSession() {
        Log.i(TAG, "Foreground offline narration session started")
        try {
            while (running && !Thread.currentThread().isInterrupted) {
                if (NarrationWorkPriority.isBusy) {
                    Thread.sleep(1_000L)
                    continue
                }
                val outcome = try {
                    cpuWakeLock.acquire(5L * 60L * 1_000L)
                    KokoroBackgroundNarrationWorker(applicationContext).runSlice {
                        val settings = NarrationSettingsRepository.get(applicationContext)
                        running && !Thread.currentThread().isInterrupted &&
                            settings.enabled && settings.engine != NarrationEngine.ANDROID
                    }
                } finally {
                    if (cpuWakeLock.isHeld) cpuWakeLock.release()
                }
                when (outcome) {
                    KokoroBackgroundNarrationWorker.Outcome.MORE -> continue
                    KokoroBackgroundNarrationWorker.Outcome.BUSY -> Thread.sleep(1_000L)
                    KokoroBackgroundNarrationWorker.Outcome.RETRY -> Thread.sleep(20_000L)
                    KokoroBackgroundNarrationWorker.Outcome.COMPLETE -> {
                        Log.i(TAG, "Foreground offline narration curriculum scan complete")
                        break
                    }
                    KokoroBackgroundNarrationWorker.Outcome.PAUSED -> {
                        Log.i(TAG, "Foreground offline narration paused (settings/storage)")
                        break
                    }
                    KokoroBackgroundNarrationWorker.Outcome.UNAVAILABLE -> {
                        Log.i(TAG, "Foreground offline narration waiting for installed Kokoro resources")
                        break
                    }
                }
            }
        } catch (_: InterruptedException) {
            Thread.currentThread().interrupt()
        } catch (error: Exception) {
            Log.e(TAG, "Foreground offline narration stopped by unexpected error", error)
        } finally {
            if (cpuWakeLock.isHeld) cpuWakeLock.release()
            running = false
            Log.i(TAG, "Foreground offline narration session stopped")
            stopSelf()
        }
    }

    override fun onTaskRemoved(rootIntent: Intent?) {
        // Swiping the task away explicitly closes this preparation session.
        stopSelf()
        super.onTaskRemoved(rootIntent)
    }

    override fun onDestroy() {
        running = false
        thread?.interrupt()
        if (::cpuWakeLock.isInitialized && cpuWakeLock.isHeld) cpuWakeLock.release()
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    private companion object {
        const val TAG = "KokoroBackground"
        const val CHANNEL_ID = "kokoro_offline_narration"
        const val NOTIFICATION_ID = 17023
    }
}
