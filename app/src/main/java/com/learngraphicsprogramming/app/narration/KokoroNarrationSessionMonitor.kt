package com.learngraphicsprogramming.app.narration

import android.app.Activity
import android.app.Application
import android.app.KeyguardManager
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.PowerManager
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
        val update = {
            if (!initialized) install(app)
            if (startedActivities > 0) startService()
        }
        // MainActivity calls this inside onCreate. Registration must happen
        // NOW, before Android dispatches the first onActivityStarted callback.
        if (Looper.myLooper() == Looper.getMainLooper()) update()
        else main.post { update() }
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
                KokoroNarrationDiagnostics.update(application, mapOf("startedActivities" to startedActivities))
                if (startedActivities == 1) startService()
            }
            override fun onActivityResumed(activity: Activity) = Unit
            override fun onActivityPaused(activity: Activity) = Unit
            override fun onActivityStopped(activity: Activity) {
                startedActivities = (startedActivities - 1).coerceAtLeast(0)
                KokoroNarrationDiagnostics.update(application, mapOf("startedActivities" to startedActivities))
                if (startedActivities == 0) scheduleStop(2_500L)
            }
            override fun onActivitySaveInstanceState(activity: Activity, outState: Bundle) = Unit
            override fun onActivityDestroyed(activity: Activity) = Unit
        })

        // A screen-on event ends an unattended screen-off session if the user
        // woke into another app rather than returning to LearnGraphics.
        app.registerReceiver(object : BroadcastReceiver() {
            override fun onReceive(context: Context, intent: Intent) {
                if (intent.action in setOf(Intent.ACTION_SCREEN_ON, Intent.ACTION_USER_PRESENT) &&
                    startedActivities == 0) {
                    scheduleStop(10_000L)
                }
            }
        }, IntentFilter().apply {
            addAction(Intent.ACTION_SCREEN_ON)
            addAction(Intent.ACTION_USER_PRESENT)
        })
    }

    private fun powerInteractive(): Boolean =
        (application.getSystemService(Context.POWER_SERVICE) as PowerManager).isInteractive

    private fun scheduleStop(delayMillis: Long) {
        main.postDelayed({
            val keyguard = application.getSystemService(Context.KEYGUARD_SERVICE) as KeyguardManager
            // A lock screen can be ON while the app remains open behind it.
            // End only when the device is unlocked and our app is no longer visible.
            if (startedActivities == 0 && powerInteractive() && !keyguard.isKeyguardLocked) {
                KokoroNarrationDiagnostics.event(application, TAG, "Ending unattended unlocked narration session")
                application.stopService(Intent(application, KokoroContinuousNarrationService::class.java))
            }
        }, delayMillis)
    }

    private fun startService() {
        if (startedActivities <= 0) return
        val settings = NarrationSettingsRepository.get(application)
        if (!settings.enabled || settings.engine == NarrationEngine.ANDROID) {
            KokoroNarrationDiagnostics.update(application, mapOf("sessionState" to "disabled-or-android-tts"))
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
            KokoroNarrationDiagnostics.event(application, TAG, "Could not start offline narration foreground service", error)
        }
    }
}
