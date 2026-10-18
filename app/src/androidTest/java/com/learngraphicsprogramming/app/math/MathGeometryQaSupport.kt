package com.learngraphicsprogramming.app.math

import android.app.Activity
import android.app.Application
import android.app.KeyguardManager
import android.content.Context
import android.content.Intent
import android.content.pm.ActivityInfo
import android.content.res.Configuration
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Rect
import android.os.Bundle
import android.os.PowerManager
import android.os.SystemClock
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.Button
import android.widget.ScrollView
import androidx.test.core.app.ActivityScenario
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.AttemptRepository
import com.learngraphicsprogramming.app.BuildConfig
import com.learngraphicsprogramming.app.LessonActivity
import com.learngraphicsprogramming.app.LessonTextSizeSettingsRepository
import com.learngraphicsprogramming.app.PremiumRepository
import com.learngraphicsprogramming.app.ProgressRepository
import com.learngraphicsprogramming.app.R
import com.learngraphicsprogramming.app.narration.NarrationSettingsRepository
import org.json.JSONArray
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import java.io.File
import java.util.UUID

/** Only in-memory lessons with fresh QA identities are interacted with. All preferences are
 * restored, including drafts incidentally created when initializing the ordinary lesson route. */
abstract class MathGeometryQaSupport(private val qaPackage: String = BuildConfig.APPLICATION_ID.takeIf {
    it in setOf(QA_PACKAGE, "com.learngraphicsprogramming.app.realdownloadsqa")
} ?: QA_PACKAGE) {
    protected val instrumentation = InstrumentationRegistry.getInstrumentation()
    protected val context = instrumentation.targetContext
    private val snapshots = linkedMapOf<String, Map<String, *>>()
    private var originalPreferenceFiles = emptySet<String>()
    private var captured = false
    private var registered = false
    private var screenWakeLock: PowerManager.WakeLock? = null
    private val screenGuard = object : Application.ActivityLifecycleCallbacks {
        override fun onActivityCreated(activity: Activity, state: Bundle?) {
            if (activity is LessonActivity && activity.packageName == qaPackage)
                activity.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        }
        override fun onActivityStarted(activity: Activity) = Unit
        override fun onActivityResumed(activity: Activity) = Unit
        override fun onActivityPaused(activity: Activity) = Unit
        override fun onActivityStopped(activity: Activity) = Unit
        override fun onActivitySaveInstanceState(activity: Activity, state: Bundle) = Unit
        override fun onActivityDestroyed(activity: Activity) = Unit
    }

    @Before fun requireQaAndPreserveEveryPreference() {
        assertTrue("Only isolated QA application IDs are allowed", qaPackage in setOf(QA_PACKAGE,
            "com.learngraphicsprogramming.app.realdownloadsqa"))
        assertEquals("Never run this fixture against the normal app", qaPackage, context.packageName)
        assertEquals(qaPackage, BuildConfig.APPLICATION_ID)
        val keyguard = context.getSystemService(Context.KEYGUARD_SERVICE) as KeyguardManager
        assertFalse("Unlock the device before visual verification", keyguard.isDeviceLocked)
        assertFalse("Dismiss Keyguard before visual verification", keyguard.isKeyguardLocked)
        originalPreferenceFiles = preferenceFiles()
        (originalPreferenceFiles + PREFS).forEach { name ->
            snapshots[name] = context.getSharedPreferences(name, Context.MODE_PRIVATE).all.mapValues { (_, value) ->
                if (value is Set<*>) value.toSet() else value
            }
        }
        captured = true
        // Standalone Canvas checks do not launch an Activity to hold KEEP_SCREEN_ON.
        // Retain the already-unlocked display only for this test, without changing settings.
        @Suppress("DEPRECATION")
        val power = context.getSystemService(Context.POWER_SERVICE) as PowerManager
        @Suppress("DEPRECATION")
        val lock = power.newWakeLock(PowerManager.SCREEN_BRIGHT_WAKE_LOCK or PowerManager.ON_AFTER_RELEASE,
            "$qaPackage:GeometryQa")
        screenWakeLock = lock
        lock.acquire(600_000)
        (context.applicationContext as Application).registerActivityLifecycleCallbacks(screenGuard)
        registered = true
        NarrationSettingsRepository.saveEnabled(context, false)
        PremiumRepository.setVerifiedPremium(context)
        assertTrue(AttemptRepository.getState(context, ROUTE).unlimited)
        LessonTextSizeSettingsRepository.saveUsesGlobalScale(context, COURSE, true)
        LessonTextSizeSettingsRepository.saveGlobalScale(context, 100)
    }

    @After fun restoreEveryQaPreference() {
        if (context.packageName != qaPackage || BuildConfig.APPLICATION_ID != qaPackage || !captured) return
        try {
            instrumentation.waitForIdleSync()
            (preferenceFiles() + snapshots.keys).forEach { name ->
                if (name !in originalPreferenceFiles) {
                    assertTrue("Delete only a new QA preference file", context.deleteSharedPreferences(name))
                } else {
                    val original = snapshots.getValue(name)
                    val prefs = context.getSharedPreferences(name, Context.MODE_PRIVATE)
                    val editor = prefs.edit().clear()
                    original.forEach { (key, value) ->
                        when (value) {
                            is String -> editor.putString(key, value)
                            is Int -> editor.putInt(key, value)
                            is Long -> editor.putLong(key, value)
                            is Float -> editor.putFloat(key, value)
                            is Boolean -> editor.putBoolean(key, value)
                            is Set<*> -> editor.putStringSet(key, value.filterIsInstance<String>().toSet())
                            else -> error("Unsupported preference $name/$key")
                        }
                    }
                    assertTrue(editor.commit())
                    assertEquals("Every original QA value, including progress, is restored", original, prefs.all)
                }
            }
            assertEquals(originalPreferenceFiles, preferenceFiles())
        } finally {
            screenWakeLock?.takeIf { it.isHeld }?.release()
            screenWakeLock = null
            if (registered) {
                (context.applicationContext as Application).unregisterActivityLifecycleCallbacks(screenGuard)
                registered = false
            }
        }
    }

    protected data class Fixture(val id: String, val block: JSONObject, val lesson: JSONObject,
                                 val page: Int, val index: Int, val fingerprint: String)

    protected fun fixture(block: JSONObject, page: Int = 0, index: Int = 0): Fixture {
        MathPuzzleParser.parse(block)
        val id = "math_geometria_plana__fundamentos_geometricos__native_qa_${UUID.randomUUID().toString().replace("-", "")}"
        val pages = JSONArray().apply {
            repeat(page + 1) { p -> put(JSONObject().put("blocks", JSONArray().apply {
                if (p == page) {
                    repeat(index) { put(JSONObject().put("type", "paragraph").put("text", "")) }
                    put(JSONObject(block.toString()))
                } else put(JSONObject().put("type", "paragraph").put("text", ""))
            })) }
        }
        return Fixture(id, JSONObject(block.toString()), JSONObject().put("id", id)
            .put("title", "Verificación técnica aislada").put("pages", pages), page, index,
            MathPuzzleState.fingerprint(block))
    }

    protected fun withFixture(fixture: Fixture, test: (ActivityScenario<LessonActivity>) -> Unit) {
        val before = fixture.block.toString()
        ProgressRepository.saveCurrentPage(context, ROUTE, SOURCE_LESSON, 0)
        val intent = Intent(context, LessonActivity::class.java).putExtra("routeId", ROUTE)
            .putExtra("courseId", COURSE).putExtra("unitId", "estructura_y_divisibilidad")
            .putExtra("lessonId", SOURCE_LESSON).putExtra("lessonNumber", 1).putExtra("lessonTotal", 1)
        ActivityScenario.launch<LessonActivity>(intent).use { scenario ->
            scenario.onActivity { inject(it, fixture) }
            awaitLayout(scenario, fixture)
            test(scenario)
        }
        assertEquals("The renderer never rewrites the original block", before, fixture.block.toString())
        assertEquals(fixture.fingerprint, MathPuzzleState.fingerprint(fixture.block))
    }

    protected fun inject(activity: LessonActivity, fixture: Fixture) {
        assertEquals(qaPackage, activity.packageName)
        setField(activity, "currentLessonId", fixture.id)
        setField(activity, "lessonJson", JSONObject(fixture.lesson.toString()))
        setField(activity, "totalPages", fixture.lesson.getJSONArray("pages").length())
        setField(activity, "currentPage", fixture.page)
        listOf("pageViewCache", "pageRequiredExercises", "pageCompletedExercises").forEach {
            @Suppress("UNCHECKED_CAST")
            (field(activity, it) as MutableMap<Int, Any>).clear()
        }
        LessonActivity::class.java.getDeclaredMethod("renderCurrentPage").apply { isAccessible = true }.invoke(activity)
        assertEquals(1, field(activity, "requiredExercises"))
    }

    protected fun awaitLayout(scenario: ActivityScenario<LessonActivity>, fixture: Fixture, orientation: Int? = null) {
        val deadline = SystemClock.uptimeMillis() + 6_000
        while (SystemClock.uptimeMillis() < deadline) {
            instrumentation.waitForIdleSync()
            var ready = false
            scenario.onActivity { activity ->
                if (orientation == null || activity.resources.configuration.orientation == orientation) {
                    if (field(activity, "currentLessonId") != fixture.id) inject(activity, fixture)
                    ready = content(activity).width > 0 && check(activity).width > 0
                }
            }
            if (ready) return
            SystemClock.sleep(50)
        }
        fail("The actual LessonActivity fixture did not finish native layout")
    }

    protected fun orient(scenario: ActivityScenario<LessonActivity>, fixture: Fixture, orientation: Int) {
        scenario.onActivity { it.requestedOrientation = if (orientation == Configuration.ORIENTATION_PORTRAIT)
            ActivityInfo.SCREEN_ORIENTATION_PORTRAIT else ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE }
        awaitLayout(scenario, fixture, orientation)
    }

    protected fun scrollVisible(activity: LessonActivity, view: View) {
        val scroll = activity.findViewById<ScrollView>(R.id.lessonScroll)
        val bounds = Rect()
        view.getDrawingRect(bounds)
        scroll.offsetDescendantRectToMyCoords(view, bounds)
        scroll.scrollTo(0, (bounds.top - 12 * activity.resources.displayMetrics.density).toInt().coerceAtLeast(0))
    }

    /** Stability is checked on the actual relevant window area. The saved PNG is always the
     * original complete native screenshot, rather than a fabricated test bitmap. */
    protected fun capture(name: String, target: () -> View, metadata: JSONObject = JSONObject()) {
        instrumentation.waitForIdleSync()
        var screen: Bitmap? = null
        var previousCrop: Bitmap? = null
        var consecutive = 0
        var sampled = 0
        val deadline = SystemClock.uptimeMillis() + 6_000
        try {
            while (SystemClock.uptimeMillis() < deadline) {
                var rect = Rect()
                instrumentation.runOnMainSync {
                    val view = target()
                    assertTrue("Native evidence target is focused", view.hasWindowFocus())
                    assertTrue(view.getGlobalVisibleRect(rect))
                    assertEquals("The entire target width is visible", view.width, rect.width())
                    assertEquals("The entire target height is visible", view.height, rect.height())
                    // getGlobalVisibleRect is relative to this window's root. A popup has
                    // its own root positioned elsewhere on the physical display.
                    val rootScreenOrigin = IntArray(2)
                    view.rootView.getLocationOnScreen(rootScreenOrigin)
                    rect.offset(rootScreenOrigin[0], rootScreenOrigin[1])
                }
                val next = requireNotNull(instrumentation.uiAutomation.takeScreenshot())
                assertTrue("The complete target is within the physical screenshot", rect.left >= 0 && rect.top >= 0 &&
                    rect.right <= next.width && rect.bottom <= next.height)
                val crop = Bitmap.createBitmap(next, rect.left, rect.top, rect.width(), rect.height())
                consecutive = if (previousCrop?.sameAs(crop) == true) consecutive + 1 else 1
                previousCrop?.recycle()
                previousCrop = crop
                screen?.recycle()
                screen = next
                sampled++
                if (consecutive >= 4) {
                    metadata.put("nativeBounds", JSONArray(listOf(rect.left, rect.top, rect.right, rect.bottom)))
                    break
                }
                SystemClock.sleep(250)
            }
            assertTrue("Native rotation/popup drawing is stable before capture: $name", consecutive >= 4)
            metadata.put("stableConsecutiveFrames", consecutive).put("sampledFrames", sampled)
                .put("samplingIntervalMillis", 250)
                .put("boundsCoordinateSpace", "displayPixels")
            saveBitmap(requireNotNull(screen), "$name.png")
            saveMetadata(name, metadata)
        } finally { previousCrop?.recycle(); screen?.recycle() }
    }

    protected fun saveBitmap(bitmap: Bitmap, name: String) {
        File(evidenceDirectory(), name).outputStream().use { assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) }
    }
    protected fun saveMetadata(name: String, metadata: JSONObject) {
        File(evidenceDirectory(), "$name.json").writeText(metadata.toString(2))
    }
    protected fun draw(view: View) = Bitmap.createBitmap(view.width, view.height, Bitmap.Config.ARGB_8888)
        .apply { view.draw(Canvas(this)) }
    protected fun descendants(view: View): List<View> = listOf(view) + if (view is ViewGroup)
        (0 until view.childCount).flatMap { descendants(view.getChildAt(it)) } else emptyList()
    protected fun content(activity: LessonActivity): ViewGroup = activity.findViewById(R.id.lessonContent)
    protected fun check(activity: LessonActivity) = descendants(content(activity)).filterIsInstance<Button>()
        .single { it.tag == MathPuzzleRenderer.CHECK_ACTION_TAG }
    protected fun state(activity: LessonActivity, fixture: Fixture) = requireNotNull(
        ProgressRepository.getExerciseState(activity, fixture.id, fixture.page, fixture.index))
    protected fun assertState(activity: LessonActivity, fixture: Fixture, correct: Boolean, answer: MathAnswer) {
        val saved = state(activity, fixture)
        assertEquals(fixture.fingerprint, saved.getString("contentFingerprint"))
        assertEquals(correct, saved.getBoolean("correct"))
        val responseId = fixture.block.getJSONArray("responses").getJSONObject(0).getString("id")
        assertEquals(answer, MathAnswerJson.decode(saved.getJSONObject("responses").getJSONObject(responseId)))
        assertEquals(correct, MathPuzzleState.isCurrentCorrect(fixture.block, saved))
        assertEquals(if (correct) 1 else 0, field(activity, "completedExercises"))
        assertEquals(!correct, check(activity).isEnabled)
        assertTrue(context.getSharedPreferences("learning_progress", Context.MODE_PRIVATE)
            .contains("exercise_${fixture.id}_page_${fixture.page}_block_${fixture.index}"))
    }
    protected fun field(activity: LessonActivity, name: String): Any? = LessonActivity::class.java.getDeclaredField(name)
        .apply { isAccessible = true }.get(activity)
    protected fun setField(activity: LessonActivity, name: String, value: Any) = LessonActivity::class.java.getDeclaredField(name)
        .apply { isAccessible = true }.set(activity, value)
    private fun evidenceDirectory() = File(requireNotNull(context.getExternalFilesDir(null)), "geometry-dropdown-qa").apply { mkdirs() }
    private fun preferenceFiles() = File(context.applicationInfo.dataDir, "shared_prefs").listFiles().orEmpty()
        .filter { it.name.endsWith(".xml") }.map { it.name.removeSuffix(".xml") }.toSet()

    companion object {
        protected const val QA_PACKAGE = "com.learngraphicsprogramming.app.curricularqa"
        protected const val ROUTE = "math_physics"
        private const val COURSE = "math_course_01"
        private const val SOURCE_LESSON = "math_course_01__estructura_y_divisibilidad__divisibilidad_y_primos"
        private val PREFS = listOf("learning_progress", "attempt_reserve_seconds_test", "navigation_state",
            "lesson_narration_settings", "math_development_mode", "app_settings", "appearance_settings",
            "ui_language_settings", "premium_state", "lesson_text_size_settings")
    }
}
