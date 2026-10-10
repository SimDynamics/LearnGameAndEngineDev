package com.learngraphicsprogramming.app

import java.io.File
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Guardrails for screen-off narration. Device scheduling and wake locks still
 * require an on-phone instrumentation check; these catch manifest regressions.
 */
class KokoroOfflineSessionConfigurationTest {
    private val res = File(System.getProperty("translation.resources", "src/main/res"))
    private val main = res.parentFile
    private val narration = File(main, "java/com/learngraphicsprogramming/app/narration")

    @Test fun offlineGeneratorRunsFromForegroundSessionNotDeferredWorkManager() {
        val worker = File(narration, "KokoroBackgroundNarrationWorker.kt").readText()
        val monitor = File(narration, "KokoroNarrationSessionMonitor.kt").readText()
        val service = File(narration, "KokoroContinuousNarrationService.kt").readText()
        assertFalse(worker.contains("OneTimeWorkRequest"))
        assertFalse(worker.contains("setRequiresBatteryNotLow"))
        assertTrue(worker.contains("KokoroNarrationCache.lookup("))
        assertTrue(monitor.contains("onActivityStarted"))
        assertTrue(monitor.contains("onActivityStopped"))
        assertTrue(monitor.contains("isKeyguardLocked"))
        assertTrue(service.contains("PowerManager.PARTIAL_WAKE_LOCK"))
        assertTrue(service.contains("startForeground("))
        assertTrue(service.contains("Outcome.MORE"))
        assertTrue(service.contains("Outcome.BUSY"))
    }

    @Test fun lifecycleObserverIsRegisteredBeforeAnyActivityStarts() {
        val app = File(main, "java/com/learngraphicsprogramming/app/UiLanguageSettings.kt").readText()
        val monitor = File(main, "java/com/learngraphicsprogramming/app/narration/KokoroNarrationSessionMonitor.kt").readText()
        val manifest = File(main, "AndroidManifest.xml").readText()
        assertTrue(manifest.contains("android:name=\".LearnGraphicsApplication\""))
        assertTrue(app.contains("KokoroNarrationSessionMonitor.refresh(this)"))
        assertTrue(monitor.contains("registerActivityLifecycleCallbacks"))
    }

    @Test fun manifestDeclaresAndroid14ForegroundServicePermissionsAndSubtype() {
        val xml = File(main, "AndroidManifest.xml").readText()
        assertTrue(xml.contains("android.permission.FOREGROUND_SERVICE"))
        assertTrue(xml.contains("android.permission.FOREGROUND_SERVICE_SPECIAL_USE"))
        assertTrue(xml.contains("android:foregroundServiceType=\"specialUse\""))
        assertTrue(xml.contains("android:name=\".narration.KokoroContinuousNarrationService\""))
        assertTrue(xml.contains("android.app.PROPERTY_SPECIAL_USE_FGS_SUBTYPE"))
    }

    @Test fun persistentNotificationTextIsLocalized() {
        for (lang in listOf("values", "values-en", "values-fr")) {
            val values = File(res, "$lang/strings.xml").readText()
            assertTrue(values.contains("name=\"narration_background_notification_title\""))
            assertTrue(values.contains("name=\"narration_background_notification_text\""))
        }
    }
}
