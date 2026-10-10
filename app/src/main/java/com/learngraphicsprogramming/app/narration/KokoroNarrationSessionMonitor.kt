package com.learngraphicsprogramming.app.narration

import android.app.Activity
import android.app.Application
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.PowerManager
import android.util.Log
import androidx.work.WorkManager

/**
 * A screen turning off does not mean the user closed the app.
 *
 * While any activity is visible, keep offline narration in a session-scoped
 * foreground service. If the phone locks with the app open, that service
 * remains active and owns a bounded PARTIAL_WAKE_LOCK while synthesizing.
 * Going Home or switching apps with the screen on ends that session.
 */
internal object KokoroNarrationSessionMonitor {
    private const val TAG = "KokoroSession"
    private const val OLD_WORK_NAME = "kokoro-offline-curriculum-v1"
    private val main = Handler(Looper.getMainLooper())
    private var initialized = false
    private var startedActivities = 0
    private lateinit var application: Application

    fun refresh(context: Context) {
        val app = context.applicationContext as Application
        main.post {
            if (!initialized) install(app)
            if (startedActivities > 0) startService()
        }
    }

    private fun install(app: Application) {
        initialized = true
        application = app

        // Jobs created by earlier APK versions must not run a second copy.
        WorkManager.getInstance(app).cancelUniqueWork(OLD_WORK_NAME)

        app.registerActivityLifecycleCallbacks(object : Application.ActivityLifecycleCallbacks {
            override fun onActivityCreated(activity: Activity, savedInstanceState: Bundle?) = Unit
            override fun onActivityStarted(activity: Activity) {
                startedActivities++
                startService()
            }
            override fun onActivityResumed(activity: Activity) = Unit
            override fun onActivityPaused(activity: Activity) = Unit
            override fun onActivityStopped(activity: Activity) {
                startedActivities = (startedActivities - 1).coerceAtLeast(0)
                if (startedActivities == 0) scheduleStop(2_500L)
            }
            override fun onActivitySaveInstanceState(activity: Activity, outState: Bundle) = Unit
            override fun onActivityDestroyed(activity: Activity) = Unit
        })

        // A screen-on event ends an unattended screen-off session if the user
        // woke into another app rather than returning to LearnGraphics.
        app.registerReceiver(object : BroadcastReceiver() {
            override fun onReceive(context: Context, intent: Intent) {
                if (intent.action == Intent.ACTION_SCREEN_ON && startedActivities == 0) {
                    scheduleStop(10_000L)
                }
            }
        }, IntentFilter(Intent.ACTION_SCREEN_ON))
    }

    private fun powerInteractive(): Boolean =
        (application.getSystemService(Context.POWER_SERVICE) as PowerManager).isInteractive

    private fun scheduleStop(delayMillis: Long) {
        main.postDelayed({
            if (startedActivities == 0 && powerInteractive()) {
                application.stopService(Intent(application, KokoroContinuousNarrationService::class.java))
            }
        }, delayMillis)
    }

    private fun startService() {
        if (startedActivities <= 0) return
        val settings = NarrationSettingsRepository.get(application)
        if (!settings.enabled || settings.engine == NarrationEngine.ANDROID) {
            application.stopService(Intent(application, KokoroContinuousNarrationService::class.java))
            return
        }
        val intent = Intent(application, KokoroContinuousNarrationService::class.java)
        try {
            if (Build.VERSION.SDK_INT >= 26) application.startForegroundService(intent)
            else application.startService(intent)
        } catch (error: RuntimeException) {
            // Foreground launch restrictions can still be imposed by Android
            // or the phone manufacturer; report them, never silently swallow.
            Log.e(TAG, "Could not start offline narration foreground service", error)
        }
    }
}
