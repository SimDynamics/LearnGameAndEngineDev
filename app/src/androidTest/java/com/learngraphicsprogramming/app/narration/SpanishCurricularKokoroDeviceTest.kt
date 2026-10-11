package com.learngraphicsprogramming.app.narration

import android.app.Activity
import android.app.Application
import android.app.KeyguardManager
import android.content.Context
import android.content.Intent
import android.content.SharedPreferences
import android.media.AudioDeviceInfo
import android.media.AudioManager
import android.media.MediaPlayer
import android.os.SystemClock
import android.view.WindowManager
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.AppearanceSettingsRepository
import com.learngraphicsprogramming.app.CoursesActivity
import com.learngraphicsprogramming.app.narration.phonemes.SpanishFluidG2P
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.security.MessageDigest
import java.util.UUID
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.atomic.AtomicReference
import kotlin.math.abs
import kotlin.math.sin
import kotlin.math.sqrt
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/** Uses installed models and the production controller; every recording belongs to this test. */
@RunWith(AndroidJUnit4::class)
class SpanishCurricularKokoroDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val preferences = linkedMapOf<String, Map<String, *>>()
    private var originalPreferenceFiles = emptySet<String>()
    private lateinit var sandbox: File
    private lateinit var isolated: IsolatedApplication
    private lateinit var modelRoot: File
    private var originalModels = emptyMap<String, Pair<Long, Long>>()
    private var scenario: ActivityScenario<CoursesActivity>? = null
    private var foreground: Activity? = null
    private var controller: LessonNarrationController? = null
    private val errors = CopyOnWriteArrayList<String>()
    private val states = CopyOnWriteArrayList<Boolean>()
    private var originalVolume: Int? = null
    private var originalMuted = false

    @Before fun isolateAudioAndPreserveUserState() {
        originalPreferenceFiles = preferenceFiles()
        (originalPreferenceFiles + KNOWN_PREFERENCES).forEach { name ->
            preferences[name] = context.getSharedPreferences(name, Context.MODE_PRIVATE).all.mapValues { (_, value) ->
                if (value is Set<*>) value.toSet() else value
            }
        }
        deviceShell("input keyevent KEYCODE_WAKEUP")
        val keyguard = context.getSystemService(KeyguardManager::class.java)
        if (!keyguard.isKeyguardSecure) deviceShell("wm dismiss-keyguard")
        deviceShell("cmd statusbar collapse")
        assertFalse("Unlock the Honor before real neural playback", keyguard.isDeviceLocked)
        // The real application's service must not generate user audio while this
        // isolated controller is exercising its own current-page queue.
        assertTrue(context.getSharedPreferences("lesson_narration_settings", Context.MODE_PRIVATE).edit()
            .putBoolean("enabled", false).putString("engine", NarrationEngine.ANDROID.name).commit())
        sandbox = File(context.cacheDir, "spanish-kokoro-acceptance-${UUID.randomUUID()}").canonicalFile
        assertEquals(context.cacheDir.canonicalFile, sandbox.parentFile)
        assertTrue(sandbox.mkdirs())
        isolated = IsolatedApplication(context.applicationContext as Application, sandbox)
        NarrationSettingsRepository.saveEnabled(isolated, true)
        NarrationSettingsRepository.saveEngine(isolated, NarrationEngine.KOKORO)
        NarrationSettingsRepository.saveLanguage(isolated, NarrationLanguage.SPANISH)
        NarrationSettingsRepository.saveKokoroVoice(isolated, VOICE)
        NarrationSettingsRepository.saveAndroidVoice(isolated, null)
        NarrationSettingsRepository.saveRate(isolated, 1f)
        NarrationStorageBudget.saveLimit(isolated, 64L * NarrationStorageBudget.MIB)
        val store = KokoroModelStore(isolated)
        modelRoot = try { store.directory().canonicalFile } finally { store.close() }
        assertTrue("Use already installed resources; never import or download a fixture", modelRoot.isDirectory)
        originalModels = modelMetadata()
        scenario = ActivityScenario.launch<CoursesActivity>(Intent(context, CoursesActivity::class.java)
            .putExtra("routeId", AppearanceSettingsRepository.ROUTE_PROGRAMMING)).also { host ->
            host.onActivity {
                foreground = it
                it.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
            }
        }
        await("A focused foreground Honor window must own playback", 15_000) { foreground?.hasWindowFocus() == true }
        val audio = context.getSystemService(AudioManager::class.java)
        originalVolume = audio.getStreamVolume(AudioManager.STREAM_MUSIC)
        originalMuted = audio.isStreamMute(AudioManager.STREAM_MUSIC)
        audio.adjustStreamVolume(AudioManager.STREAM_MUSIC, AudioManager.ADJUST_UNMUTE, 0)
        audio.setStreamVolume(AudioManager.STREAM_MUSIC,
            maxOf(1, audio.getStreamMaxVolume(AudioManager.STREAM_MUSIC) / 3), 0)
    }

    @After fun closeWorkersThenRestoreUserState() {
        try {
            controller?.let { current ->
                onMain { current.close() }
                await("Close the owned synthesis worker before removing its recordings", 15_000, false) {
                    !field<Thread>(current, "workerThread").isAlive
                }
            }
        } finally {
            try {
                scenario?.close()
                foreground = null
                instrumentation.waitForIdleSync()
                // This singleton uses the directory passed to its most recent call.
                KokoroPagePreparationQueue.snapshot(context)
                if (::modelRoot.isInitialized) assertEquals("Installed resources must retain size and timestamps",
                    originalModels, modelMetadata())
            } finally {
                try {
                    if (::sandbox.isInitialized) {
                        controller?.let {
                            assertFalse("Never remove recordings while their synthesis worker is still alive",
                                field<Thread>(it, "workerThread").isAlive)
                        }
                        assertEquals(context.cacheDir.canonicalFile, sandbox.canonicalFile.parentFile)
                        assertTrue("Remove only the sandbox owned by this test", sandbox.deleteRecursively())
                    }
                } finally {
                    restorePreferences()
                    originalVolume?.let { volume ->
                        val audio = context.getSystemService(AudioManager::class.java)
                        audio.setStreamVolume(AudioManager.STREAM_MUSIC, volume, 0)
                        audio.adjustStreamVolume(AudioManager.STREAM_MUSIC,
                            if (originalMuted) AudioManager.ADJUST_MUTE else AudioManager.ADJUST_UNMUTE, 0)
                    }
                }
            }
        }
    }

    @Test fun realCurricularSymbolFamiliesGenerateFreshNativeSpeechAndPlayOnTheHonor() {
        verifyInstalledResources()
        val samples = representativeSamples()
        assertEquals("Keep every technical symbol family represented", 9, samples.size)
        val lexicon = SpanishFluidG2P.loadLexicon(File(modelRoot,
            KokoroModelManifest.resources(NarrationLanguage.SPANISH).single().fileName))
        val current = newController()
        for (sample in samples) {
            assertTrue("The original authored ${sample.family} example must remain intact", sample.token in sample.raw)
            assertTrue("The direct G2P entry point must also accept ${sample.family}",
                SpanishFluidG2P.phonemize(sample.raw, lexicon).isNotBlank())
            assertMissingAudio(sample.spoken)
            val hash = key(sample.spoken)
            onMain { current.toggle(listOf(sample.spoken), NarrationLanguage.SPANISH) }
            await("Fresh neural ${sample.family} audio must play through the production MediaPlayer", 210_000) {
                ready(current) && current.isPlaying && player(current).isPlaying && player(current).currentPosition >= 700
            }
            val file = onMain { segmentFile(current, 0)!! }
            assertOwnedSpeech(file)
            assertEquals("Exactly one fresh native inference must produce this sentence", 1, generationEvents(hash).size)
            assertNoSubstitutedAudio(hash)
            onMain {
                assertNull("Native success must never initialize Android fallback", field<Any?>(current, "tts"))
                val route = player(current).routedDevice
                assertNotNull("MediaPlayer must report its active output route", route)
                assertTrue("Listen through the Honor's local speaker or earpiece, rather than an absent remote sink",
                    route!!.type in setOf(AudioDeviceInfo.TYPE_BUILTIN_SPEAKER, AudioDeviceInfo.TYPE_BUILTIN_EARPIECE))
                assertTrue(context.getSystemService(AudioManager::class.java).getStreamVolume(AudioManager.STREAM_MUSIC) > 0)
                current.stop()
            }
            assertTrue("Stopping playback preserves its newly generated recording", file.isFile)
        }
        assertTrue("Playback callbacks must include starts and stops", true in states && false in states)
    }

    @Test fun passivePagePreparationYieldsThenCachesAndReusesRealPlayback() {
        verifyInstalledResources()
        val samples = representativeSamples()
        val texts = listOf(samples.first().spoken, samples[2].spoken)
        texts.forEach(::assertMissingAudio)
        val hashes = texts.map(::key)
        val current = newController()
        val operation = NarrationWorkPriority.beginInteractive()
        try {
            onMain { current.prefetchPage(texts, NarrationLanguage.SPANISH) }
            SystemClock.sleep(700)
            onMain {
                assertFalse("Page opening must not take playback focus", current.isPlaying)
                assertNull("An interactive operation postpones passive audio preparation", field<Any?>(current, "player"))
                assertNull("Yielding must not switch to Android TTS", field<Any?>(current, "tts"))
            }
            assertTrue(hashes.all { generationEvents(it).isEmpty() })
            texts.forEach { assertNull(KokoroNarrationCache.lookup(isolated, "es", VOICE, it)) }
            val queue = requireNotNull(KokoroPagePreparationQueue.snapshot(isolated))
            assertEquals(hashes.toSet(), queue.sentences.map { NarrationPageQueue.hash(it) }.toSet())
            assertEquals(0, queue.completedCount)
        } finally { operation.close() }
        await("Releasing interactive work must finish the page's own neural preparation", 240_000) {
            ready(current) && (0..1).all { segmentFile(current, it)?.isFile == true } &&
                KokoroPagePreparationQueue.snapshot(isolated)?.completedCount == 2
        }
        onMain {
            assertFalse("Prepared audio remains paused until Play", current.isPlaying)
            assertFalse(player(current).isPlaying)
            assertFalse("Passive preparation must not acquire audio focus", field<Boolean>(current, "focusHeld"))
        }
        val files = onMain { (0..1).map { segmentFile(current, it)!! } }
        files.forEach(::assertOwnedSpeech)
        val contents = files.map(::sha256)
        hashes.forEach { assertEquals(1, generationEvents(it).size); assertNoSubstitutedAudio(it) }
        val generation = onMain { field<Long>(current, "generation") }
        onMain { current.toggle(texts, NarrationLanguage.SPANISH) }
        await("Play must advance the pre-generated source", 10_000) {
            current.isPlaying && player(current).isPlaying && player(current).currentPosition >= 700
        }
        onMain { current.toggle(texts, NarrationLanguage.SPANISH) }
        assertPaused(current)
        val position = onMain { player(current).currentPosition }
        onMain { current.seekBy(1) }
        await("Seeking a paused generated sentence must advance its source position", 10_000) {
            ready(current) && abs(player(current).currentPosition - (position + 1_000)) <= 200
        }
        assertPaused(current)
        val pausedPlayer = onMain { player(current) }
        onMain {
            NarrationSettingsRepository.saveRate(isolated, 1.5f)
            current.settingsChanged()
            assertSame(pausedPlayer, player(current))
            assertFalse(current.isPlaying)
            current.toggle(texts, NarrationLanguage.SPANISH)
        }
        await("Resume must retain the actual prepared player at the requested rate", 5_000) {
            pausedPlayer.isPlaying && pausedPlayer.currentPosition > position + 1_100
        }
        val start = onMain {
            assertSame(pausedPlayer, player(current))
            assertEquals(1.5f, pausedPlayer.playbackParams.speed, 0.001f)
            assertEquals(1f, pausedPlayer.playbackParams.pitch, 0.001f)
            SystemClock.uptimeMillis() to pausedPlayer.currentPosition
        }
        SystemClock.sleep(800)
        val end = onMain {
            assertSame("Rate changes must reuse the source player", pausedPlayer, player(current))
            assertTrue(pausedPlayer.isPlaying)
            val measured = SystemClock.uptimeMillis() to pausedPlayer.currentPosition
            current.toggle(texts, NarrationLanguage.SPANISH)
            measured
        }
        val expected = (end.first - start.first) * 1.5f
        assertTrue("Actual playback must run at 1.5X", abs((end.second - start.second) - expected) <= maxOf(250f, expected * 0.15f))
        assertPaused(current)
        assertEquals(contents, files.map(::sha256))
        assertEquals(generation, onMain { field<Long>(current, "generation") })
        hashes.forEach { assertEquals("Playing cached audio must not repeat inference", 1, generationEvents(it).size) }
        assertEquals(2, KokoroPagePreparationQueue.snapshot(isolated)?.completedCount)
    }

    @Test fun oldTechnicalCacheKeysAreRetainedButOrdinarySpokenKeysRemainUsable() {
        val technical = representativeSamples().first().raw
        val ordinary = "La programación permite resolver problemas."
        assertTrue(SpanishTechnicalSpeech.prepare(technical).changed)
        assertFalse(SpanishTechnicalSpeech.prepare(ordinary).changed)
        assertNotEquals("Changed technical pronunciation needs its own cache key", legacyKey(technical), key(technical))
        assertEquals("Unaffected speech must continue to use its established cache key", legacyKey(ordinary), key(ordinary))
        val cache = File(isolated.filesDir, "kokoro-narration-cache").apply { assertTrue(mkdirs()) }
        val oldTechnical = File(cache, "${legacyKey(technical)}.wav")
        val existingOrdinary = File(cache, "${legacyKey(ordinary)}.wav")
        val tone = FloatArray(2_400) { index -> (0.1 * sin(index * 2.0 * Math.PI * 440.0 / 24_000)).toFloat() }
        NarrationWavWriter.write(oldTechnical, tone)
        NarrationWavWriter.write(existingOrdinary, tone)
        val originals = listOf(oldTechnical, existingOrdinary).map(::sha256)
        assertNull("A valid recording from the old technical frontend cannot mask the repair",
            KokoroNarrationCache.lookup(isolated, "es", VOICE, technical))
        assertEquals(existingOrdinary, KokoroNarrationCache.lookup(isolated, "es", VOICE, ordinary))
        KokoroNarrationCache.prune(isolated)
        assertEquals("Both existing recordings are preserved byte for byte", originals,
            listOf(oldTechnical, existingOrdinary).map(::sha256))
        assertEquals(2, cache.listFiles().orEmpty().size)
    }

    private data class Sample(val family: String, val token: String, val raw: String, val spoken: String)

    private fun representativeSamples(): List<Sample> {
        fun sample(family: String, asset: String, token: String, anchor: String, spokenAnchor: String = anchor): Sample {
            val lesson = JSONObject(context.assets.open("content/es/programming/cpp_fundamentals/$asset.json")
                .bufferedReader().use { it.readText() })
            val pages = lesson.getJSONArray("pages")
            for (page in 0 until pages.length()) {
                val blocks = pages.getJSONObject(page).getJSONArray("blocks")
                for (block in 0 until blocks.length()) {
                    val text = blocks.getJSONObject(block).optString("text")
                    if (!text.contains(token) || !text.contains(anchor)) continue
                    val raw = NarrationSegments.sentences(listOf(text)).first { token in it && anchor in it }
                    val spoken = NarrationSegments.sentences(LessonNarrationText.page(lesson, page,
                        NarrationLanguage.SPANISH) { NarrationUiInstructions.forBlock(context, it) }).first { spokenAnchor in it }
                    return Sample(family, token, raw, spoken)
                }
            }
            error("Missing authored $family example: $asset / $token / $anchor")
        }
        return listOf(
            sample("include directive", "programas_y_ejecucion/lesson_primer_programa_y_salida", "#include", "La línea"),
            sample("brace initializer", "objetos_y_estado/lesson_declarar_e_inicializar", "{5}", "int filas"),
            sample("compound multiplication", "objetos_y_estado/lesson_estado_asignacion_y_copia", "*=", "En la recuperación"),
            sample("logical operators and code identifiers", "expresiones_y_decisiones/lesson_combinar_condiciones", "sensor_a", "cualquiera de los sensores"),
            sample("reference ampersand", "funciones_y_contratos/lesson_referencias_para_modificar", "int&", "Un parámetro"),
            sample("single vertical separator", "archivos_y_programa_completo/lesson_componer_informes_sin_cambiar_los_datos", "|", "Cada fila"),
            sample("section reference", "aleatoriedad_y_reproducibilidad/lesson_producir_enteros_en_un_intervalo", "§4.7.5", "Fuente principal"),
            sample("page range", "aleatoriedad_y_reproducibilidad/lesson_producir_enteros_en_un_intervalo", "108–110", "108", "ciento ocho a ciento diez"),
            sample("escaped newline", "programas_y_ejecucion/lesson_texto_secuencia_y_formato", "\\n", "La secuencia")
        )
    }

    private fun newController(): LessonNarrationController = onMain {
        LessonNarrationController(isolated, { states.add(it) }, { errors.add(it) }).also { controller = it }
    }

    private fun verifyInstalledResources() {
        val artifacts = KokoroModelManifest.sharedArtifacts + KokoroModelManifest.resources(NarrationLanguage.SPANISH) +
            requireNotNull(KokoroVoiceCatalog.find(VOICE)).artifact
        artifacts.forEach {
            assertTrue("An installed resource must verify without replacement: ${it.fileName}",
                KokoroModelManifest.verify(File(modelRoot, it.fileName), it))
        }
    }

    private fun assertMissingAudio(text: String) {
        assertNull("Neither old persistent audio nor packaged audio may satisfy a native test",
            KokoroNarrationCache.lookup(isolated, "es", VOICE, text))
    }

    private fun assertOwnedSpeech(file: File) {
        assertEquals(File(isolated.filesDir, "kokoro-narration-cache").canonicalFile, file.canonicalFile.parentFile)
        val bytes = file.readBytes()
        assertTrue("Native inference must produce speech-duration PCM", bytes.size > 4_844)
        val data = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        assertEquals("RIFF", String(bytes, 0, 4, Charsets.US_ASCII))
        assertEquals("WAVE", String(bytes, 8, 4, Charsets.US_ASCII))
        assertEquals(1, data.getShort(20).toInt())
        assertEquals(1, data.getShort(22).toInt())
        assertEquals(24_000, data.getInt(24))
        assertEquals(16, data.getShort(34).toInt())
        assertEquals(bytes.size - 44, data.getInt(40))
        var peak = 0.0
        var squares = 0.0
        var count = 0
        data.position(44)
        while (data.remaining() >= 2) {
            val sample = data.short / 32_767.0
            peak = maxOf(peak, abs(sample))
            squares += sample * sample
            count++
        }
        assertTrue("A valid WAV container must contain non-silent native speech", peak > 0.005 && sqrt(squares / count) > 0.0005)
    }

    private fun assertNoSubstitutedAudio(hash: String) {
        assertTrue("No engine error is allowed", errors.isEmpty())
        assertTrue("A packaged recording cannot replace actual inference", events().none {
            it.optString("event") == "packaged-audio-loaded" && it.optString("cacheKey") == hash
        })
        assertTrue("Native failure cannot be hidden by Android fallback", events().none {
            it.optString("event") == "Neural narration failed" || it.has("exception") &&
                it.optString("event") != "NNAPI request retried on CPU"
        })
    }

    private fun generationEvents(hash: String) = events().filter {
        it.optString("event") == "Page sentence generated" && it.optString("hash") == hash
    }
    private fun events(): List<JSONObject> = File(isolated.filesDir, KokoroNarrationDiagnostics.EVENTS_FILE)
        .takeIf { it.isFile }?.readLines()?.filter(String::isNotBlank)?.map(::JSONObject).orEmpty()
    private fun assertPaused(current: LessonNarrationController) {
        val position = onMain { player(current).currentPosition }
        SystemClock.sleep(350)
        onMain {
            assertFalse(current.isPlaying)
            assertFalse(player(current).isPlaying)
            assertTrue("Paused native media must not advance", abs(player(current).currentPosition - position) <= 80)
        }
    }
    private fun ready(current: LessonNarrationController): Boolean = field(current, "playerReady")
    private fun player(current: LessonNarrationController): MediaPlayer = field(current, "player")
    private fun segmentFile(current: LessonNarrationController, index: Int): File? =
        field<List<Any>>(current, "segments").getOrNull(index)?.let { field(it, "file") }
    private fun key(text: String) = KokoroNarrationCache.key("es", VOICE, text)
    private fun legacyKey(text: String) = digest(listOf("kokoro-wav-v1", KokoroModelManifest.revision, "es", VOICE, text)
        .joinToString("\u0000").toByteArray(Charsets.UTF_8))
    private fun digest(bytes: ByteArray) = MessageDigest.getInstance("SHA-256").digest(bytes)
        .joinToString("") { "%02x".format(it.toInt() and 0xff) }
    private fun sha256(file: File) = digest(file.readBytes())
    private fun modelMetadata() = modelRoot.listFiles().orEmpty().associate { it.name to (it.length() to it.lastModified()) }
    private fun preferenceFiles() = File(context.applicationInfo.dataDir, "shared_prefs").listFiles().orEmpty()
        .filter { it.name.endsWith(".xml") }.map { it.name.removeSuffix(".xml") }.toSet()

    private fun restorePreferences() {
        (preferenceFiles() + preferences.keys + if (::isolated.isInitialized) isolated.preferenceNames else emptySet()).forEach { name ->
            if (name !in originalPreferenceFiles) {
                assertTrue("Remove only test-created preferences: $name", context.deleteSharedPreferences(name))
            } else {
                val edit = context.getSharedPreferences(name, Context.MODE_PRIVATE).edit().clear()
                preferences.getValue(name).forEach { (key, value) ->
                    when (value) {
                        is String -> edit.putString(key, value)
                        is Int -> edit.putInt(key, value)
                        is Long -> edit.putLong(key, value)
                        is Float -> edit.putFloat(key, value)
                        is Boolean -> edit.putBoolean(key, value)
                        is Set<*> -> edit.putStringSet(key, value.filterIsInstance<String>().toSet())
                        else -> error("Unsupported preference: $name/$key")
                    }
                }
                assertTrue("Restore user preference values: $name", edit.commit())
            }
        }
    }

    @Suppress("UNCHECKED_CAST") private fun <T> field(instance: Any, name: String): T =
        instance.javaClass.getDeclaredField(name).apply { isAccessible = true }.get(instance) as T
    private fun deviceShell(command: String) {
        android.os.ParcelFileDescriptor.AutoCloseInputStream(
            instrumentation.uiAutomation.executeShellCommand(command)
        ).use { it.readBytes() }
    }
    private fun <T> onMain(block: () -> T): T {
        val result = AtomicReference<T>()
        instrumentation.runOnMainSync { result.set(block()) }
        return result.get()
    }
    private fun await(label: String, timeout: Long, checkErrors: Boolean = true, condition: () -> Boolean) {
        val end = SystemClock.uptimeMillis() + timeout
        while (SystemClock.uptimeMillis() < end) {
            if (checkErrors) {
                assertTrue("$label: unexpected fallback or playback error: $errors", errors.isEmpty())
                controller?.let { assertNull("$label: Android TTS must remain uninitialized", field<Any?>(it, "tts")) }
            }
            if (onMain(condition)) return
            SystemClock.sleep(40)
        }
        fail("$label; states=$states errors=$errors diagnostics=${if (::isolated.isInitialized) events().takeLast(5) else emptyList<Any>()}")
    }

    private class IsolatedApplication(private val actual: Application, root: File) : Application() {
        private val files = File(root, "files").apply { check(mkdirs()) }
        private val cache = File(root, "cache").apply { check(mkdirs()) }
        private val prefix = "spanish_kokoro_test_${UUID.randomUUID()}_"
        val preferenceNames = java.util.Collections.synchronizedSet(mutableSetOf<String>())
        init { attachBaseContext(actual) }
        override fun getApplicationContext(): Context = this
        override fun getFilesDir(): File = files
        override fun getCacheDir(): File = cache
        override fun getSharedPreferences(name: String, mode: Int): SharedPreferences {
            val scoped = prefix + name
            preferenceNames.add(scoped)
            return actual.getSharedPreferences(scoped, mode)
        }
    }

    companion object {
        private const val VOICE = "ef_dora"
        private val KNOWN_PREFERENCES = setOf("learning_progress", "navigation_state", "lesson_narration_settings",
            "app_settings", "ui_language_settings", "kokoro_model_downloads")
    }
}
