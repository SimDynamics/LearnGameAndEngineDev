package com.learngraphicsprogramming.app.narration

import android.content.Context
import android.content.Intent
import android.app.KeyguardManager
import android.app.Activity
import android.media.MediaPlayer
import android.os.Looper
import android.os.SystemClock
import android.view.WindowManager
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.CoursesActivity
import com.learngraphicsprogramming.app.AppearanceSettingsRepository
import java.io.File
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.atomic.AtomicReference
import kotlin.math.abs
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/** Actual controller synthesis and playback; requires the approved Kokoro Spanish resources. */
@RunWith(AndroidJUnit4::class)
class NarrationSharedPlaybackDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private var originalPreferences: Map<String, *> = emptyMap<String, Any>()
    private var originalNavigation: Map<String, *> = emptyMap<String, Any>()
    private var scenario: ActivityScenario<CoursesActivity>? = null
    private var foreground: Activity? = null
    private var controller: LessonNarrationController? = null
    private val errors = CopyOnWriteArrayList<String>()
    private val states = CopyOnWriteArrayList<Boolean>()
    private val text = "Las fracciones equivalentes representan la misma cantidad. " +
        "Tres cuartos es igual a seis octavos. " +
        "Las fracciones equivalentes representan la misma cantidad. " +
        "Tres cuartos es igual a seis octavos. Tres cuartos es igual a seis octavos."

    @Before fun preserveNarrationPreferences() {
        originalPreferences = context.getSharedPreferences("lesson_narration_settings", Context.MODE_PRIVATE).all.toMap()
        originalNavigation = context.getSharedPreferences("navigation_state", Context.MODE_PRIVATE).all.toMap()
        android.os.ParcelFileDescriptor.AutoCloseInputStream(
            instrumentation.uiAutomation.executeShellCommand("input keyevent KEYCODE_WAKEUP")
        ).use { it.readBytes() }
        val keyguard = context.getSystemService(KeyguardManager::class.java)
        if (!keyguard.isKeyguardSecure) {
            android.os.ParcelFileDescriptor.AutoCloseInputStream(
                instrumentation.uiAutomation.executeShellCommand("wm dismiss-keyguard")
            ).use { it.readBytes() }
        }
        assertFalse("Unlock the phone before testing real playback", keyguard.isDeviceLocked)
        NarrationSettingsRepository.saveEnabled(context, true)
        NarrationSettingsRepository.saveLanguage(context, NarrationLanguage.SPANISH)
        NarrationSettingsRepository.saveKokoroVoice(context, "ef_dora")
        NarrationSettingsRepository.saveAndroidVoice(context, null)
        NarrationSettingsRepository.saveRate(context, 1f)
        assertTrue("One long segment exercises seeking without regenerated audio", text.length in 180..250)
        scenario = ActivityScenario.launch<CoursesActivity>(Intent(context, CoursesActivity::class.java)
            .putExtra("routeId", AppearanceSettingsRepository.ROUTE_PROGRAMMING)).also { host ->
            host.onActivity { activity ->
                foreground = activity
                activity.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
            }
        }
        await("A foreground app window must retain audio focus during native synthesis", 15_000) {
            foreground?.hasWindowFocus() == true
        }
    }

    @After fun closeAndRestoreNarrationPreferences() {
        try {
            controller?.let { current ->
                onMain { current.close() }
                await("The synthesis worker must close", 10_000, checkErrors = false) {
                    !field<Thread>(current, "workerThread").isAlive
                }
            }
        } finally {
            try { scenario?.close(); foreground = null } finally {
                restore("lesson_narration_settings", originalPreferences)
                restore("navigation_state", originalNavigation)
            }
        }
    }

    private fun restore(name: String, original: Map<String, *>) {
            val editor = context.getSharedPreferences(name, Context.MODE_PRIVATE).edit().clear()
            original.forEach { (key, value) ->
                when (value) {
                    is String -> editor.putString(key, value)
                    is Int -> editor.putInt(key, value)
                    is Long -> editor.putLong(key, value)
                    is Float -> editor.putFloat(key, value)
                    is Boolean -> editor.putBoolean(key, value)
                    is Set<*> -> editor.putStringSet(key, value.filterIsInstance<String>().toSet())
                }
            }
            assertTrue("Restore $name after real playback tests", editor.commit())
    }

    @Test fun androidTtsUsesSharedPauseSeekRateResumeAndCleanup() = exercise(NarrationEngine.ANDROID)

    @Test fun kokoroSpanishUsesSharedPauseSeekRateResumeAndCleanup() = exercise(NarrationEngine.KOKORO)

    @Test fun stoppingActiveEnglishBartInferenceDoesNotFallBackOrReviveThePage() {
        NarrationSettingsRepository.saveLanguage(context, NarrationLanguage.AMERICAN_ENGLISH)
        NarrationSettingsRepository.saveKokoroVoice(context, "af_heart")
        NarrationSettingsRepository.saveEngine(context, NarrationEngine.KOKORO)
        val unfamiliarWords = List(8) { "glorbaxylophonically" }.joinToString(" ")
        assertTrue(unfamiliarWords.length < 250)
        val current = onMain {
            LessonNarrationController(context, { states.add(it) }, { errors.add(it) }).also {
                controller = it
                it.toggle(listOf(unfamiliarWords), NarrationLanguage.AMERICAN_ENGLISH)
            }
        }
        await("Real Misaki BART must enter an active native run before cancellation", 120_000) {
            val english = field<Any?>(current, "english") ?: return@await false
            val bart = field<Lazy<Any?>>(english, "americanBart")
            bart.isInitialized() && bart.value?.let { field<Any?>(it, "activeRun") != null } == true
        }
        val directory = onMain { field<File>(current, "directory") }
        onMain {
            assertNull("The active frontend must precede Kokoro audio inference", field<Any?>(current, "kokoro"))
            current.stop()
            assertFalse(current.isPlaying)
            assertFalse(current.hasNarration)
        }
        val generation = onMain { field<Long>(current, "generation") }
        await("Cancelled POS/BART must promptly release their sessions and temporary page", 5_000) {
            field<Any?>(current, "english") == null && !directory.exists()
        }
        SystemClock.sleep(350)
        onMain {
            assertEquals(generation, field<Long>(current, "generation"))
            assertFalse("A stale phonemizer callback cannot resume narration", current.isPlaying)
            assertFalse(current.hasNarration)
            assertNull("Cancellation must not initialize Android fallback", field<Any?>(current, "tts"))
            assertNull(field<Any?>(current, "player"))
            assertTrue("Intentional cancellation must not report an unavailable engine", errors.isEmpty())
            assertEquals(false, states.last())
        }
    }

    private fun exercise(engine: NarrationEngine) {
        NarrationSettingsRepository.saveEngine(context, engine)
        val current = onMain {
            LessonNarrationController(context, { states.add(it) }, { errors.add(it) }).also {
                controller = it
                it.toggle(listOf(text), NarrationLanguage.SPANISH)
            }
        }
        await("The selected real engine must prepare and advance its shared MediaPlayer", 180_000) {
            ready(current) && current.isPlaying && player(current).currentPosition >= 700
        }
        val directory = onMain { field<File>(current, "directory") }
        val audio = directory.listFiles()!!.single { it.extension == "wav" }
        val fileLength = audio.length()
        val fileTimestamp = audio.lastModified()
        val generation = onMain { field<Long>(current, "generation") }
        assertTrue("Synthesis must produce actual audio", fileLength > 4_096L)
        assertTrue("The real sample must provide room for a five-second jump", onMain { player(current).duration } > 8_000)
        if (engine == NarrationEngine.KOKORO) {
            assertNotNull("Kokoro must synthesize through its actual ONNX engine", field<Any?>(current, "kokoro"))
            assertNull("Kokoro success must not initialize Android as a hidden fallback", field<Any?>(current, "tts"))
        } else {
            assertNotNull("Android must synthesize through the actual system TTS engine", field<Any?>(current, "tts"))
            assertNull("Android selection must not create a Kokoro session", field<Any?>(current, "kokoro"))
        }
        assertTrue("No unavailable-engine warning or fallback is permitted", errors.isEmpty())

        onMain { current.toggle(listOf(text), NarrationLanguage.SPANISH) }
        assertFalse(onMain { current.isPlaying })
        assertPausedWithoutAdvance(current)

        var position = onMain { player(current).currentPosition }
        seekPaused(current, 5, position + 5_000)
        position = onMain { player(current).currentPosition }
        seekPaused(current, -3, position - 3_000)
        position = onMain { player(current).currentPosition }
        seekPaused(current, -5, (position - 5_000).coerceAtLeast(0))
        seekPaused(current, 5, 5_000)

        for (rate in listOf(0.5f, 1f, 1.5f, 2f)) {
            // Keep every measurement away from completion, including short system voices.
            seekPaused(current, -3_600, 0)
            val pausedPlayer = onMain { player(current) }
            val before = onMain { pausedPlayer.currentPosition }
            onMain {
                NarrationSettingsRepository.saveRate(context, rate)
                current.settingsChanged()
                assertSame("Changing speed must reuse the prepared audio and player", pausedPlayer, player(current))
                assertEquals("The requested speed must remain stored while paused", rate,
                    NarrationSettingsRepository.get(context).rate, 0.001f)
                // NuPlayer reports effective speed 0 while paused; verify the rate when running.
                assertEquals("Pitch remains neutral", 1f, player(current).playbackParams.pitch, 0.001f)
                assertFalse("Speed must preserve the requested paused state", current.isPlaying)
                assertFalse("PlaybackParams must not leave a paused player running", player(current).isPlaying)
            }
            assertPausedWithoutAdvance(current)
            assertTrue("Speed changes must preserve the source timestamp", abs(onMain { player(current).currentPosition } - before) <= 100)
            assertActualPlaybackRate(current, pausedPlayer, rate)
            assertPausedWithoutAdvance(current)
        }
        assertEquals("Rate changes and seeks must not synthesize another file", listOf(audio.name), directory.listFiles()!!.map { it.name })
        assertEquals("Cached audio bytes must be reused", fileLength, audio.length())
        assertEquals("Cached audio must not be rewritten", fileTimestamp, audio.lastModified())
        assertEquals("Changing playback position/rate must retain the page generation", generation, onMain { field<Long>(current, "generation") })

        val beforeResume = onMain { player(current).currentPosition }
        onMain { current.toggle(listOf(text), NarrationLanguage.SPANISH) }
        await("Resume must advance the same cached audio", 5_000) {
            current.isPlaying && player(current).isPlaying && player(current).currentPosition > beforeResume + 200
        }
        onMain { current.restart() }
        await("Restart must return to the beginning and continue playback", 5_000) {
            ready(current) && current.isPlaying && player(current).isPlaying && player(current).currentPosition < 1_000
        }
        assertTrue("Restart must retain the audio cache", audio.isFile)
        onMain { current.toggle(listOf(text), NarrationLanguage.SPANISH) }
        onMain { current.restart() }
        await("Long-press restart also starts a paused page", 5_000) {
            ready(current) && current.isPlaying && player(current).isPlaying && player(current).currentPosition < 1_000
        }
        assertTrue("Callbacks must report both playback and pause", true in states && false in states)

        onMain {
            current.stop()
            assertFalse(current.isPlaying)
            assertFalse(current.hasNarration)
            assertNull(field<Any?>(current, "player"))
        }
        await("Stopping a page must delete its audio cache", 10_000) {
            !directory.exists()
        }
        if (engine == NarrationEngine.KOKORO) {
            assertNotNull("Keeping the neural session warm avoids reloading it on the next page",
                field<Any?>(current, "kokoro"))
            assertNotNull("The Spanish pronunciation dictionary must be reusable between pages",
                field<Any?>(current, "spanishLexicon"))
        }
        onMain { current.close() }
        await("Closing must release the engine and worker", 10_000) { !field<Thread>(current, "workerThread").isAlive }
        onMain {
            assertNull(field<Any?>(current, "tts"))
            assertNull(field<Any?>(current, "kokoro"))
            assertNull(field<Any?>(current, "english"))
            assertNull(field<Any?>(current, "french"))
            assertNull(field<Any?>(current, "spanishLexicon"))
            current.toggle(listOf(text), NarrationLanguage.SPANISH)
            assertFalse("A closed controller cannot revive a page", current.hasNarration)
            assertFalse(current.isPlaying)
        }
    }

    private fun seekPaused(current: LessonNarrationController, seconds: Int, expected: Int) {
        onMain { current.seekBy(seconds) }
        await("A paused $seconds-second seek must reach $expected ms", 10_000) {
            ready(current) && abs(player(current).currentPosition - expected) <= 200
        }
        assertFalse("Seeking must preserve pause", onMain { current.isPlaying || player(current).isPlaying })
        assertPausedWithoutAdvance(current)
    }

    private fun assertPausedWithoutAdvance(current: LessonNarrationController) {
        val before = onMain { player(current).currentPosition }
        SystemClock.sleep(350)
        onMain {
            assertFalse(current.isPlaying)
            assertFalse(player(current).isPlaying)
            assertTrue("Paused media must not advance", abs(player(current).currentPosition - before) <= 80)
        }
    }

    private fun assertActualPlaybackRate(current: LessonNarrationController, cachedPlayer: MediaPlayer, rate: Float) {
        val before = onMain { cachedPlayer.currentPosition }
        onMain { current.toggle(listOf(text), NarrationLanguage.SPANISH) }
        await("Resuming at $rate must advance the prepared audio", 5_000) {
            current.isPlaying && cachedPlayer.isPlaying && cachedPlayer.currentPosition > before + 120
        }
        val start = onMain {
            assertSame("Resume after a speed change must reuse the player", cachedPlayer, player(current))
            assertEquals("The running player must use the requested speed", rate, cachedPlayer.playbackParams.speed, 0.001f)
            assertEquals("Running pitch remains neutral", 1f, cachedPlayer.playbackParams.pitch, 0.001f)
            SystemClock.uptimeMillis() to cachedPlayer.currentPosition
        }
        SystemClock.sleep(2_000)
        val end = onMain {
            assertSame("Rate measurement must retain the cached player", cachedPlayer, player(current))
            if (!current.isPlaying || !cachedPlayer.isPlaying) {
                fail("Real playback must continue during the measurement; requestedRate=$rate, " +
                    "startPosition=${start.second}, elapsed=${SystemClock.uptimeMillis() - start.first}; ${diagnostics()}")
            }
            assertEquals(rate, cachedPlayer.playbackParams.speed, 0.001f)
            assertEquals(1f, cachedPlayer.playbackParams.pitch, 0.001f)
            val measured = SystemClock.uptimeMillis() to cachedPlayer.currentPosition
            current.toggle(listOf(text), NarrationLanguage.SPANISH)
            measured
        }
        val elapsed = end.first - start.first
        val advanced = end.second - start.second
        val expected = elapsed * rate
        // Allow timestamp/buffer jitter while still rejecting every other supported speed.
        val tolerance = maxOf(200f, expected * 0.15f)
        assertTrue("Speed $rate: advanced $advanced ms in $elapsed ms; expected $expected +/- $tolerance",
            abs(advanced - expected) <= tolerance)
        assertFalse("Measurement must return to pause", onMain { current.isPlaying || cachedPlayer.isPlaying })
    }

    private fun ready(current: LessonNarrationController): Boolean = field(current, "playerReady")
    private fun player(current: LessonNarrationController): MediaPlayer = field(current, "player")
    @Suppress("UNCHECKED_CAST") private fun <T> field(instance: Any, name: String): T =
        instance.javaClass.getDeclaredField(name).apply { isAccessible = true }.get(instance) as T
    private fun <T> onMain(block: () -> T): T {
        val value = AtomicReference<T>()
        instrumentation.runOnMainSync { value.set(block()) }
        return value.get()
    }
    private fun await(message: String, timeout: Long, checkErrors: Boolean = true, condition: () -> Boolean) {
        val end = SystemClock.uptimeMillis() + timeout
        while (SystemClock.uptimeMillis() < end) {
            if (checkErrors && errors.isNotEmpty()) fail("$message; unexpected errors: $errors; ${diagnostics()}")
            if (onMain(condition)) return
            SystemClock.sleep(40)
        }
        fail("$message; errors=$errors; ${diagnostics()}")
    }

    private fun diagnostics(): String = if (Looper.myLooper() == Looper.getMainLooper()) {
        diagnosticsOnMain()
    } else onMain(::diagnosticsOnMain)

    private fun diagnosticsOnMain(): String = runCatching {
            val current = controller ?: return@runCatching "controller=null, focus=${foreground?.hasWindowFocus()}"
            val media = field<MediaPlayer?>(current, "player")
            val mediaState = if (media == null) "null" else runCatching {
                "running=${media.isPlaying}, position=${media.currentPosition}, duration=${media.duration}, " +
                    "effectiveRate=${media.playbackParams.speed}, pitch=${media.playbackParams.pitch}"
            }.getOrElse { "unprepared: ${it.javaClass.simpleName}" }
            val files = field<File?>(current, "directory")?.listFiles()?.joinToString { "${it.name}:${it.length()}" }
            val worker = field<Thread>(current, "workerThread")
            "states=$states, errors=$errors, playing=${current.isPlaying}, hasNarration=${current.hasNarration}, " +
                "finished=${field<Boolean>(current, "finished")}, requestedRate=${NarrationSettingsRepository.get(context).rate}, " +
                "ready=${field<Boolean>(current, "playerReady")}, focusHeld=${field<Boolean>(current, "focusHeld")}, " +
                "windowFocus=${foreground?.hasWindowFocus()}, player=[$mediaState], " +
                "index=${field<Int>(current, "index")}, offset=${field<Long>(current, "offset")}, " +
                "pending=${field<Set<Int>>(current, "pending")}, files=[$files], " +
                "worker=${worker.state} ${worker.stackTrace.take(6).joinToString(" <- ")}, " +
                "kokoro=${field<Any?>(current, "kokoro") != null}, tts=${field<Any?>(current, "tts") != null}"
        }.getOrElse { "diagnostics unavailable: $it" }
}
