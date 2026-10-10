package com.learngraphicsprogramming.app.narration

import ai.onnxruntime.OrtException
import android.app.KeyguardManager
import android.content.Context
import android.content.Intent
import android.media.MediaPlayer
import android.os.ParcelFileDescriptor
import android.os.PowerManager
import android.os.SystemClock
import android.view.WindowManager
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.AppearanceSettingsRepository
import com.learngraphicsprogramming.app.CoursesActivity
import com.learngraphicsprogramming.app.LessonRepository
import com.learngraphicsprogramming.app.narration.phonemes.SpanishFluidG2P
import java.io.File
import java.util.UUID
import java.util.concurrent.CountDownLatch
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference
import kotlin.math.sqrt
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/** Real reported page and installed native model; never removes existing audio/resources. */
@RunWith(AndroidJUnit4::class)
class KokoroPhaseAAcceptanceDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val preferenceSnapshots = linkedMapOf<String, Map<String, *>>()
    private var scenario: ActivityScenario<CoursesActivity>? = null
    private var controller: LessonNarrationController? = null
    private val errors = CopyOnWriteArrayList<String>()
    private val states = CopyOnWriteArrayList<Boolean>()
    private var originallyInteractive = true
    private var originalPage: NarrationPageQueue.Snapshot? = null

    @Before fun preserveUserStateAndSelectTheReportedSpanishVoice() {
        originallyInteractive = context.getSystemService(PowerManager::class.java).isInteractive
        originalPage = KokoroPagePreparationQueue.snapshot(context)
        for (name in listOf("lesson_narration_settings", "navigation_state", "learning_progress")) {
            preferenceSnapshots[name] = context.getSharedPreferences(name, Context.MODE_PRIVATE).all.toMap()
        }
        shell("input keyevent KEYCODE_WAKEUP")
        val keyguard = context.getSystemService(KeyguardManager::class.java)
        if (!keyguard.isKeyguardSecure) shell("wm dismiss-keyguard")
        assertFalse("Unlock the phone before the actual playback check", keyguard.isDeviceLocked)
        NarrationSettingsRepository.saveEnabled(context, true)
        NarrationSettingsRepository.saveLanguage(context, NarrationLanguage.SPANISH)
        NarrationSettingsRepository.saveKokoroVoice(context, "ef_dora")
        NarrationSettingsRepository.saveEngine(context, NarrationEngine.KOKORO)
        NarrationSettingsRepository.saveRate(context, 2f)
    }

    @After fun releaseOnlyTestObjectsAndRestoreUserState() {
        try {
            controller?.let { current ->
                onMain { current.close() }
                await("Controller worker closes without deleting persistent recordings", 15_000, false) {
                    !field<Thread>(current, "workerThread").isAlive
                }
            }
        } finally {
            try { scenario?.close() } finally {
                try { restorePagePriority() } finally {
                    try { preferenceSnapshots.forEach { (name, original) -> restorePreferences(name, original) } }
                    finally { shell(if (originallyInteractive) "input keyevent KEYCODE_WAKEUP" else "input keyevent KEYCODE_SLEEP") }
                }
            }
        }
    }

    @Test fun everySentenceOfRealMathLessonThreeFirstPageSynthesizesWithoutTts() {
        val texts = realPageTexts()
        val sentences = NarrationSegments.sentences(texts)
        assertTrue("Exercise the whole authored page rather than a short sample", sentences.size > 10)
        val store = KokoroModelStore(context)
        val modelRoot = try { store.directory() } finally { store.close() }
        val artifact = KokoroModelManifest.resources(NarrationLanguage.SPANISH).single()
        val lexiconFile = File(modelRoot, artifact.fileName)
        assertTrue("Installed Spanish dictionary must match its pinned digest", KokoroModelManifest.verify(lexiconFile, artifact))
        val lexicon = SpanishFluidG2P.loadLexicon(lexiconFile)
        // Fresh test-owned evidence proves every sentence reaches native inference,
        // even when the user already has a complete persistent page cache.
        val evidence = File(context.filesDir, "kokoro-phase-a-native-${UUID.randomUUID()}")
        assertTrue(evidence.mkdirs())
        val lease = NarrationWorkPriority.beginInteractive()
        try {
            KokoroEngineCoordinator.withEngine(this, modelRoot, { false }, foreground = true) { engine ->
                sentences.forEachIndexed { index, text ->
                    val hash = KokoroNarrationCache.key("es", "ef_dora", text)
                    try {
                        val phonemes = SpanishFluidG2P.phonemize(text, lexicon)
                        assertTrue("Sentence $index/$hash has phonemes", phonemes.isNotBlank())
                        val parts = KokoroPhonemeChunks.split(phonemes).map { part ->
                            engine.synthesizePhonemes(part, "ef_dora", 1f).also { assertPcm(index, hash, it) }
                        }
                        val output = File(evidence, "$hash.wav")
                        // Equal repeated sentences are not rewritten within this evidence set.
                        if (!output.exists()) NarrationWavWriter.writeChunks(output, parts)
                        assertTrue("Real native sentence $index/$hash has a complete WAV", output.length() > 44)
                    } catch (error: Throwable) {
                        throw IllegalStateException("Real math c1/u1/l3/page1 sentence=$index hash=$hash stage=G2P/ONNX/WAV", error)
                    }
                }
            }
            assertEquals("Each unique sentence has native evidence", sentences.distinct().size, evidence.listFiles().orEmpty().size)
        } finally {
            lease.close()
            // Valid test WAVs remain available to inspect; no recording is deleted.
        }
    }

    @Test fun nativePreemptionIsControlledAndCannotCancelTheNextForegroundOwner() {
        val store = KokoroModelStore(context)
        val modelRoot = try { store.directory() } finally { store.close() }
        val dictionary = KokoroModelManifest.resources(NarrationLanguage.SPANISH).single()
        val phonemes = SpanishFluidG2P.phonemize("Las fracciones equivalentes representan la misma cantidad.",
            SpanishFluidG2P.loadLexicon(File(modelRoot, dictionary.fileName)))
        val backgroundOwner = Any()
        val foregroundOwner = Any()
        val lease = NarrationWorkPriority.beginInteractive()
        var background: Thread? = null
        var foreground: Thread? = null
        try {
            // Warm up the installed background profile before measuring actual ORT cancellation.
            KokoroEngineCoordinator.withEngine(backgroundOwner, modelRoot, { false }) { engine ->
                assertPcm(0, "background-warmup", engine.synthesizePhonemes(phonemes, "ef_dora", 1f))
            }
            val longPhonemes = List((KokoroModelManifest.maximumPhonemeTokens / (phonemes.length + 1)).coerceAtMost(8)) {
                phonemes
            }.joinToString(" ")
            assertTrue("Cancellation reaches a substantial valid native input", longPhonemes.length in 200..KokoroModelManifest.maximumPhonemeTokens)
            val backgroundEngine = AtomicReference<KokoroOnnxEngine>()
            val backgroundFailure = AtomicReference<Throwable?>()
            val backgroundFinished = CountDownLatch(1)
            background = Thread({
                try {
                    KokoroEngineCoordinator.withEngine(backgroundOwner, modelRoot, { false }) { engine ->
                        backgroundEngine.set(engine)
                        engine.synthesizePhonemes(longPhonemes, "ef_dora", 0.5f)
                    }
                } catch (error: Throwable) { backgroundFailure.set(error) }
                finally { backgroundFinished.countDown() }
            }, "Kokoro-owner-preemption-test").apply { start() }
            awaitNativeRun("Background test enters a real ORT run", backgroundEngine, backgroundFinished, backgroundFailure)
            KokoroEngineCoordinator.cancel(foregroundOwner)
            assertFalse("A foreign owner cannot mark the current native lease cancelled", activeLeaseCancelled())
            assertTrue("Wrong-owner cancellation leaves the native run active", isNativeRunning(backgroundEngine.get()))
            KokoroEngineCoordinator.cancel(backgroundOwner)
            assertTrue("Matching owner must terminate the active native run", backgroundFinished.await(15, TimeUnit.SECONDS))
            background.join(1_000)
            val preempted = backgroundFailure.get()
            assertTrue("Native scheduling cancellation maps to InterruptedException rather than a TTS-worthy synthesis failure: $preempted",
                preempted is InterruptedException)
            assertTrue("Cancellation must originate in active ONNX termination", generateSequence(preempted) { it.cause }.any {
                it is OrtException && it.message.orEmpty().contains("terminate", ignoreCase = true)
            })

            val foregroundEngine = AtomicReference<KokoroOnnxEngine>()
            val foregroundFailure = AtomicReference<Throwable?>()
            val foregroundPcm = AtomicReference<FloatArray?>()
            val foregroundFinished = CountDownLatch(1)
            foreground = Thread({
                try {
                    KokoroEngineCoordinator.withEngine(foregroundOwner, modelRoot, { false }, foreground = true) { engine ->
                        foregroundEngine.set(engine)
                        foregroundPcm.set(engine.synthesizePhonemes(phonemes, "ef_dora", 1f))
                    }
                } catch (error: Throwable) { foregroundFailure.set(error) }
                finally { foregroundFinished.countDown() }
            }, "Kokoro-foreground-owner-test").apply { start() }
            awaitNativeRun("Foreground recovery enters its own real ORT run", foregroundEngine, foregroundFinished, foregroundFailure)
            KokoroEngineCoordinator.cancel(backgroundOwner)
            assertFalse("A stale background cancellation cannot cancel the foreground lease", activeLeaseCancelled())
            assertTrue("The foreground run survives stale background cancellation", foregroundFinished.await(120, TimeUnit.SECONDS))
            foreground.join(1_000)
            foregroundFailure.get()?.let { throw AssertionError("Foreground must recover after background native termination", it) }
            assertPcm(1, "foreground-after-preemption", requireNotNull(foregroundPcm.get()))
        } finally {
            // Cancellation is restricted to our own two request owners.
            KokoroEngineCoordinator.cancel(backgroundOwner)
            KokoroEngineCoordinator.cancel(foregroundOwner)
            background?.join(15_000)
            foreground?.join(15_000)
            lease.close()
        }
    }

    private fun isNativeRunning(engine: KokoroOnnxEngine?): Boolean = engine != null &&
        synchronized(field<Any>(engine, "runLock")) { field<Any?>(engine, "activeRun") != null }

    private fun activeLeaseCancelled(): Boolean {
        val coordinator = KokoroEngineCoordinator
        return synchronized(field<Any>(coordinator, "state")) {
            field<Any?>(coordinator, "active")?.let { field<Boolean>(it, "cancelled") } ?: false
        }
    }

    private fun awaitNativeRun(message: String, engine: AtomicReference<KokoroOnnxEngine>,
                               completed: CountDownLatch, failure: AtomicReference<Throwable?>) {
        val deadline = SystemClock.elapsedRealtime() + 120_000
        while (SystemClock.elapsedRealtime() < deadline && completed.count > 0) {
            if (isNativeRunning(engine.get())) return
            completed.await(10, TimeUnit.MILLISECONDS)
        }
        throw AssertionError("$message; finished=${completed.count == 0L}", failure.get())
    }

    @Test fun realMathPagePrefetchAndPlaybackNeverCreateAndroidFallback() {
        scenario = ActivityScenario.launch<CoursesActivity>(Intent(context, CoursesActivity::class.java)
            .putExtra("routeId", AppearanceSettingsRepository.ROUTE_MATH_PHYSICS)).also {
            it.onActivity { activity -> activity.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON) }
        }
        val texts = realPageTexts()
        val expected = NarrationSegments.sentences(texts)
        val current = onMain {
            LessonNarrationController(context, states::add, errors::add).also {
                controller = it
                it.prefetchPage(texts, NarrationLanguage.SPANISH)
            }
        }
        await("All sentences of the actual page must finish passive preparation", 600_000) {
            assertNeuralOnly(current)
            val segments = field<List<*>>(current, "segments")
            segments.size == expected.size && segments.all { segment ->
                segment != null && field<File?>(segment, "file")?.isFile == true &&
                    (field<Int?>(segment, "duration") ?: 0) > 0
            }
        }
        val files = onMain {
            field<List<*>>(current, "segments").map { field<File>(it!!, "file") }
        }
        files.forEachIndexed { index, file ->
            val found = KokoroNarrationCache.lookup(context, "es", "ef_dora", expected[index])
            assertNotNull("Prepared sentence $index must be reusable through the real persistent cache", found)
            assertEquals("Prepared audio belongs to this exact sentence/voice", found!!.canonicalFile, file.canonicalFile)
        }
        onMain { current.toggle(texts, NarrationLanguage.SPANISH) }
        await("Playback of the actual page must start through the neural cached files", 20_000) {
            assertNeuralOnly(current)
            field<MediaPlayer?>(current, "player")?.isPlaying == true
        }
        await("The entire reported page plays without an Android TTS recovery", 600_000) {
            assertNeuralOnly(current)
            field<Boolean>(current, "finished") && !current.isPlaying
        }
        assertTrue("Actual playback reports a start and completion", true in states && states.last() == false)
        assertTrue("All generated/cached recordings survive completion", files.all { it.isFile })
    }

    @Test fun pendingRealPageCompletesWithScreenOffThenResumesCurriculumOrder() {
        val priority = NarrationWorkPriority.beginInteractive()
        var priorityHeld = true
        try {
            val curriculum = NarrationCurriculum.sentences(context).toList()
            val grouped = curriculum.groupBy { it.location }
            // Prefer a complete short real page so the physical screen-off check
            // fits an uninterrupted session; keep the original coverage as fallback.
            fun uncachedPage(maximumSentences: Int) = grouped.values.asSequence().firstOrNull { sentences ->
                sentences.size in 2..maximumSentences &&
                    sentences.all { it.language == NarrationLanguage.SPANISH && it.voice == "ef_dora" } &&
                    sentences.any { KokoroNarrationCache.lookup(context, it.language.tag, it.voice, it.text) == null }
            }
            val page = uncachedPage(8) ?: uncachedPage(30)
            assertNotNull("A real uncached curricular page is required to prove screen-off synthesis; no existing WAV may be deleted to force it", page)
            val selectedPage = requireNotNull(page)
            val pageHashes = selectedPage.map(NarrationPageQueue::hash).toSet()
            val missingPageHashes = selectedPage.filter {
                KokoroNarrationCache.lookup(context, it.language.tag, it.voice, it.text) == null
            }.map(NarrationPageQueue::hash).toSet()
            val expectedNext = curriculum.firstOrNull { sentence ->
                NarrationPageQueue.hash(sentence) !in pageHashes &&
                    KokoroNarrationCache.lookup(context, sentence.language.tag, sentence.voice, sentence.text) == null
            }
            assertNotNull("Unfinished curriculum must exist after the selected page", expectedNext)

            scenario = ActivityScenario.launch<CoursesActivity>(Intent(context, CoursesActivity::class.java)
                .putExtra("routeId", AppearanceSettingsRepository.ROUTE_MATH_PHYSICS))
            val beganAt = System.currentTimeMillis()
            val submitted = onMain {
                KokoroPagePreparationQueue.submit(context, selectedPage.map { it.text },
                    NarrationLanguage.SPANISH, "ef_dora")
            }
            assertTrue("The selected page really has uncached audio before locking", missingPageHashes.isNotEmpty())
            await("Android starts the actual foreground narration service", 20_000, false) {
                shell("dumpsys activity services ${context.packageName}").contains("isForeground=true")
            }
            shell("input keyevent KEYCODE_SLEEP")
            await("The device screen is actually off", 10_000, false) {
                !context.getSystemService(PowerManager::class.java).isInteractive
            }
            priority.close()
            priorityHeld = false

            await("The service must complete all pending page sentences while the screen remains off", 600_000, false) {
                assertScreenOff("Current-page preparation")
                val snapshot = KokoroPagePreparationQueue.snapshot(context)
                snapshot?.revision == submitted.revision && snapshot.pending == 0
            }
            val completed = requireNotNull(KokoroPagePreparationQueue.snapshot(context))
            assertEquals(pageHashes, completed.completed)
            selectedPage.forEach { sentence ->
                assertNotNull("Every exact page hash has reusable valid audio after screen-off generation",
                    KokoroNarrationCache.lookup(context, sentence.language.tag, sentence.voice, sentence.text))
            }
            val eventsFile = File(context.filesDir, KokoroNarrationDiagnostics.EVENTS_FILE)
            await("After the page, synthesis resumes the first missing curriculum sentence in original order", 300_000, false) {
                assertScreenOff("Curriculum continuation after completing the page")
                val firstCurriculum = readEvents(eventsFile).firstOrNull { event ->
                    event.optLong("at") >= beganAt && event.optString("event") == "Cached lesson speech" &&
                        event.optString("scope") == "curriculum"
                }
                if (firstCurriculum != null) {
                    assertEquals("Page priority must not reorder subsequent curriculum generation",
                        NarrationPageQueue.hash(requireNotNull(expectedNext)), firstCurriculum.getString("hash"))
                    true
                } else false
            }
            val pageEvents = readEvents(eventsFile).filter { event ->
                event.optLong("at") >= beganAt && event.optString("event") == "Cached lesson speech" &&
                    event.optString("scope") == "page"
            }
            assertTrue("All previously missing page phrases were really synthesized while locked",
                pageEvents.map { it.optString("hash") }.toSet().containsAll(missingPageHashes))
        } finally {
            if (priorityHeld) priority.close()
            shell("input keyevent KEYCODE_WAKEUP")
        }
    }

    private fun assertScreenOff(stage: String) {
        val interactive = context.getSystemService(PowerManager::class.java).isInteractive
        if (!interactive) return
        val power = shell("dumpsys power").lineSequence().filter { line ->
            line.contains("mWakefulness=") || line.contains("ACQUIRE_CAUSES_WAKEUP") ||
                line.contains("bright_wakelock_for_alarm_alert") || line.contains("deskclock", ignoreCase = true)
        }.take(12).joinToString(" | ")
        fail("$stage must be measured with the display off; the display woke during the check. " +
            "Check alarms or user interaction before repeating. power=[$power]; " +
            "narration=${KokoroNarrationDiagnostics.snapshot(context)}")
    }

    private fun readEvents(file: File): List<JSONObject> = if (!file.isFile) emptyList() else
        file.readLines(Charsets.UTF_8).mapNotNull { line -> runCatching { JSONObject(line) }.getOrNull() }

    private fun restorePagePriority() {
        val previous = originalPage ?: return
        val first = previous.sentences.firstOrNull() ?: return
        require(previous.sentences.all { it.language == first.language && it.voice == first.voice })
        val restored = onMain {
            KokoroPagePreparationQueue.submit(context, previous.sentences.map { it.text }, first.language, first.voice)
        }
        previous.completed.forEach { hash -> KokoroPagePreparationQueue.markCompleted(context, restored.revision, hash) }
    }

    private fun realPageTexts(): List<String> {
        val lesson = LessonRepository.loadLessonJson(context, AppearanceSettingsRepository.ROUTE_MATH_PHYSICS,
            "math_course_01__numeros_y_operaciones__sumar_y_restar_enteros")
        assertEquals("The reproduction uses the current real course content", "Sumar y restar: estados y cambios", lesson.getString("title"))
        return LessonNarrationText.page(lesson, 0, NarrationLanguage.SPANISH) { block ->
            NarrationUiInstructions.forBlock(context, block)
        }.also { texts ->
            assertTrue("The real opening heading is narrated", texts.any { it.contains("Dónde estás y qué cambia") })
        }
    }

    private fun assertNeuralOnly(current: LessonNarrationController) {
        assertTrue("Kokoro must not report recovery/fallback: $errors", errors.isEmpty())
        assertNull("Installed Kokoro never creates an Android TTS provider for this page", field<Any?>(current, "tts"))
        assertFalse("No neural-to-TTS fallback was reported", field<Boolean>(current, "reportedFallback"))
        val selected = field<NarrationSettings>(current, "settings")
        assertEquals(NarrationLanguage.SPANISH, selected.language)
        assertEquals("ef_dora", selected.kokoroVoice)
    }

    private fun assertPcm(index: Int, hash: String, pcm: FloatArray) {
        assertTrue("Sentence $index/$hash contains audible PCM", pcm.size >= 2_400)
        assertTrue("Sentence $index/$hash samples are finite", pcm.all(Float::isFinite))
        val rms = sqrt(pcm.sumOf { it.toDouble() * it } / pcm.size)
        assertTrue("Sentence $index/$hash is non-silent", rms > 0.00001)
    }

    private fun restorePreferences(name: String, original: Map<String, *>) {
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
        assertTrue("Original $name is restored", editor.commit())
    }

    private fun shell(command: String): String = ParcelFileDescriptor.AutoCloseInputStream(
        instrumentation.uiAutomation.executeShellCommand(command)
    ).bufferedReader().use { it.readText() }

    @Suppress("UNCHECKED_CAST") private fun <T> field(instance: Any, name: String): T =
        instance.javaClass.getDeclaredField(name).apply { isAccessible = true }.get(instance) as T

    private fun <T> onMain(block: () -> T): T {
        val value = AtomicReference<T>()
        instrumentation.runOnMainSync { value.set(block()) }
        return value.get()
    }

    private fun await(message: String, timeout: Long, checkErrors: Boolean = true, condition: () -> Boolean) {
        val end = SystemClock.elapsedRealtime() + timeout
        while (SystemClock.elapsedRealtime() < end) {
            if (checkErrors && errors.isNotEmpty()) fail("$message; errors=$errors")
            if (onMain(condition)) return
            SystemClock.sleep(100)
        }
        fail("$message; errors=$errors; worker=" + controller?.let { field<Thread>(it, "workerThread").state })
    }
}
