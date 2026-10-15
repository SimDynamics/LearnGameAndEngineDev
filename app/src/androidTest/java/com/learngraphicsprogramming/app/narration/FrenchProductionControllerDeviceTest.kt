package com.learngraphicsprogramming.app.narration

import android.app.Activity
import android.app.Application
import android.content.Context
import android.content.Intent
import android.content.SharedPreferences
import android.media.MediaPlayer
import android.os.Build
import android.os.SystemClock
import android.system.Os
import android.view.WindowManager
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.AppearanceSettingsRepository
import com.learngraphicsprogramming.app.BuildConfig
import com.learngraphicsprogramming.app.CoursesActivity
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.security.MessageDigest
import java.util.UUID
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.atomic.AtomicReference
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

/** The existing production player/worker with private QA cache and bookmark namespaces. */
@RunWith(AndroidJUnit4::class)
class FrenchProductionControllerDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val app = instrumentation.targetContext

    @Test fun aFrenchLessonUsesTheProductionWorkerAndResumesItsReleasedPlayerExactly() {
        assertEquals("Never run against Work or the ordinary application", QA_PACKAGE, app.packageName)
        assertEquals(QA_PACKAGE, BuildConfig.APPLICATION_ID)
        assertTrue("Use the dedicated LDPlayer emulator", Build.SUPPORTED_ABIS.any { it == "x86" || it == "x86_64" })
        val actual = app.applicationContext as Application
        val external = requireNotNull(actual.getExternalFilesDir(null)).canonicalFile
        val root = File(external, "kokoro/${KokoroModelManifest.revision}").canonicalFile
        val resources = KokoroModelManifest.sharedArtifacts + KokoroModelManifest.resources(NarrationLanguage.FRENCH) +
            requireNotNull(KokoroVoiceCatalog.find(VOICE)).artifact
        resources.forEach { assertTrue("Only already-installed pinned QA resources are used: ${it.fileName}",
            KokoroModelManifest.verify(File(root, it.fileName), it)) }
        val modelsBefore = root.listFiles().orEmpty().associate { it.name to (it.length() to it.lastModified()) }
        val preferencesBefore = preferenceNames().associateWith { name ->
            app.getSharedPreferences(name, Context.MODE_PRIVATE).all.mapValues { (_, value) ->
                if (value is Set<*>) value.toSet() else value
            }
        }
        val sandbox = File(app.cacheDir, "french-production-controller-${UUID.randomUUID()}").canonicalFile
        assertEquals(app.cacheDir.canonicalFile, sandbox.parentFile)
        assertTrue(sandbox.mkdir())
        val isolated = IsolatedApplication(actual, sandbox)
        val output = File(external, "french-manual-qa").apply { check(isDirectory || mkdir()) }
            .resolve("controller-${UUID.randomUUID()}").apply { check(mkdir()) }
        val report = JSONObject().put("package", app.packageName).put("device", Build.MODEL)
            .put("scope", "LDPlayer actual production controller; isolated preferences, bookmarks and audio cache")
            .put("voice", VOICE).put("language", "fr").put("sandbox", sandbox.absolutePath)
            .put("naturalnessAccepted", false)
        fun persist() = File(output, "controller.json").writeText(report.toString(2))
        persist()
        var host: ActivityScenario<CoursesActivity>? = null
        var controller: LessonNarrationController? = null
        val errors = CopyOnWriteArrayList<String>()
        val states = CopyOnWriteArrayList<Boolean>()
        val starts = CopyOnWriteArrayList<Pair<Long, Int>>()
        try {
            // The real QA host must not start unrelated synthesis while this isolated reader runs.
            assertTrue(app.getSharedPreferences("lesson_narration_settings", Context.MODE_PRIVATE).edit()
                .putBoolean("enabled", false).putString("engine", NarrationEngine.ANDROID.name).commit())
            NarrationSettingsRepository.saveEnabled(isolated, true)
            NarrationSettingsRepository.saveEngine(isolated, NarrationEngine.KOKORO)
            NarrationSettingsRepository.saveLanguage(isolated, NarrationLanguage.FRENCH)
            NarrationSettingsRepository.saveKokoroVoice(isolated, VOICE)
            NarrationSettingsRepository.saveRate(isolated, 1f)
            NarrationSettingsRepository.saveControlsVisible(isolated, true)
            NarrationStorageBudget.saveLimit(isolated, 64L * NarrationStorageBudget.MIB)
            val store = KokoroModelStore(isolated)
            val actualRoot = try { store.directory().canonicalFile } finally { store.close() }
            assertEquals("Share only the existing read-only model directory", root, actualRoot)
            host = ActivityScenario.launch<CoursesActivity>(Intent(app, CoursesActivity::class.java)
                .putExtra("routeId", AppearanceSettingsRepository.ROUTE_PROGRAMMING))
            val focused = requireNotNull(host)
            var activity: Activity? = null
            focused.onActivity { activity = it; it.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON) }
            await("The ordinary QA host owns a foreground window", 15_000) { activity?.hasWindowFocus() == true }
            val fixture = JSONObject().put("title", "").put("language", "fr").put("pages", JSONArray()
                .put(JSONObject().put("blocks", JSONArray().put(JSONObject().put("type", "paragraph").put("text", SOURCE)))))
            val authored = fixture.toString()
            val page = LessonNarrationText.page(fixture, 0, NarrationLanguage.FRENCH, forKokoro = true)
            assertEquals("The existing extractor yields one complete French sentence", 1, page.size)
            assertEquals(listOf("main", "cout"), ManualEnglishSpeech.spans(page.single()).map { it.entry.id })
            assertEquals("The spoken annotations retain the authored spelling", SOURCE, ManualEnglishSpeech.strip(page.single()))
            val identity = "qa-french-production-${UUID.randomUUID()}"
            val reader = onMain {
                LessonNarrationController(isolated, { states.add(it) }, { errors.add(it) }, { event ->
                    if (event.phase == NarrationPlaybackPhase.PLAYING) {
                        val activeReader = controller
                        val player = activeReader?.let { field<MediaPlayer?>(it, "player") }
                        if (player != null) starts.add(event.positionMs to player.currentPosition)
                    }
                }).also { controller = it }
            }
            onMain { reader.prefetchPage(page, NarrationLanguage.FRENCH, identity) }
            awaitReader("The production worker generates the real annotated French WAV", reader, errors) {
                segmentFile(reader)?.isFile == true
            }
            val audio = onMain { requireNotNull(segmentFile(reader)) }
            val hash = sha256(audio)
            val originalIdentity = fileIdentity(audio)
            val key = KokoroNarrationCache.key("fr", VOICE, page.single())
            fun generationCount(): Int = File(isolated.filesDir, KokoroNarrationDiagnostics.EVENTS_FILE)
                .takeIf { it.isFile }?.readLines()?.count { line ->
                    val row = JSONObject(line)
                    row.optString("event") == "Page sentence generated" && row.optString("hash") == key
                } ?: 0
            assertEquals("A fresh isolated page synthesizes its sentence exactly once", 1, generationCount())
            assertEquals("The selected production voice remains French", VOICE,
                onMain { field<NarrationSettings>(reader, "settings").kokoroVoice })
            assertEquals(audio, KokoroNarrationCache.lookup(isolated, "fr", VOICE, page.single()))
            assertTrue("The production worker writes actual lossless PCM", audio.extension == "wav" && audio.length() > 44L)
            val header = ByteArray(44).also { bytes -> audio.inputStream().use { java.io.DataInputStream(it).readFully(bytes) } }
            val wave = ByteBuffer.wrap(header).order(ByteOrder.LITTLE_ENDIAN)
            assertEquals("RIFF", String(header, 0, 4, Charsets.US_ASCII))
            assertEquals(1, wave.getShort(22).toInt())
            assertEquals(24_000, wave.getInt(24))
            assertEquals(16, wave.getShort(34).toInt())
            onMain {
                assertFalse(reader.isPlaying)
                assertNull("Prefetch never creates or retains a player", field<MediaPlayer?>(reader, "player"))
                assertFalse(field(reader, "focusHeld"))
                reader.toggle(page, NarrationLanguage.FRENCH, identity)
            }
            awaitReader("The existing player reads the prepared French sentence", reader, errors) {
                reader.isPlaying && field<Boolean>(reader, "playerReady") &&
                    requireNotNull(field<MediaPlayer?>(reader, "player")).currentPosition >= 450
            }
            var sourceBeforePause = 0L
            val paused = onMain {
                sourceBeforePause = requireNotNull(field<MediaPlayer?>(reader, "player")).currentPosition.toLong()
                reader.pauseKeepingPosition()
                field<Long>(reader, "offset").also { saved ->
                    assertTrue("Pause saves the exact live source position", saved in sourceBeforePause..sourceBeforePause + 120)
                    assertFalse(reader.isPlaying)
                    assertNull("Pause releases MediaPlayer and all its decoder resources", field<MediaPlayer?>(reader, "player"))
                    assertFalse(field(reader, "playerReady"))
                    val resumeKey = requireNotNull(field<String?>(reader, "resumeKey"))
                    val bookmark = requireNotNull(NarrationResumeStore(isolated).restore(resumeKey, 1))
                    assertEquals(saved, bookmark.positionMs)
                    assertFalse(bookmark.finished)
                }
            }
            SystemClock.sleep(250)
            onMain {
                assertEquals("The released paused player cannot advance", paused, field<Long>(reader, "offset"))
                assertNull(field<MediaPlayer?>(reader, "player"))
                assertEquals(hash, sha256(audio))
                assertEquals(originalIdentity, fileIdentity(audio))
                reader.toggle(page, NarrationLanguage.FRENCH, identity)
                assertEquals("Resume selects the existing bookmark instead of resetting", paused, field<Long>(reader, "offset"))
            }
            awaitReader("The production player resumes the exact bookmarked audio", reader, errors) {
                starts.size >= 2 && reader.isPlaying && field<Boolean>(reader, "playerReady") &&
                    requireNotNull(field<MediaPlayer?>(reader, "player")).currentPosition >= paused + 150
            }
            onMain {
                val resumed = starts.last()
                assertEquals("The playback event retains the exact saved seek offset", paused, resumed.first)
                assertTrue("The real decoder started at that saved offset", resumed.second.toLong() in
                    (paused - 80).coerceAtLeast(0)..(paused + 120))
                reader.pauseKeepingPosition()
                assertNull(field<MediaPlayer?>(reader, "player"))
                assertNull("A native failure must never be hidden with Android TTS", field<Any?>(reader, "tts"))
            }
            assertEquals(hash, sha256(audio))
            assertEquals(originalIdentity, fileIdentity(audio))
            assertEquals("Pause and resume never synthesize a second French recording", 1, generationCount())
            assertEquals("The original in-memory JSON remains untouched", authored, fixture.toString())
            assertTrue(errors.toString(), errors.isEmpty())
            assertTrue(true in states && false in states)
            audio.copyTo(File(output, "production-french.wav"), overwrite = false)
            report.put("status", "passed").put("source", SOURCE).put("sourceJsonUnchanged", true)
                .put("cacheKey", key).put("wavSha256", hash).put("nativeGenerations", generationCount())
                .put("sourcePositionBeforePauseMillis", sourceBeforePause).put("savedPositionMillis", paused)
                .put("actualPlayerStarts", JSONArray(starts.map { JSONArray().put(it.first).put(it.second) }))
                .put("pauseReleasesPlayer", true).put("resumeUsesExactBookmark", true)
                .put("sameAudioInodeAndBytes", true).put("errors", JSONArray(errors.toList()))
            persist()
        } catch (error: Throwable) {
            report.put("status", "failed").put("failure", error.toString()).put("errors", JSONArray(errors.toList()))
            persist()
            throw error
        } finally {
            try {
                controller?.let { reader ->
                    onMain { reader.close() }
                    await("The owned reader worker closes before restoring QA settings", 15_000) {
                        !field<Thread>(reader, "workerThread").isAlive
                    }
                }
                host?.close()
                instrumentation.waitForIdleSync()
                KokoroPagePreparationQueue.snapshot(app)
            } finally {
                restorePreferences(preferencesBefore)
                assertEquals("Installed model resources remain intact", modelsBefore,
                    root.listFiles().orEmpty().associate { it.name to (it.length() to it.lastModified()) })
            }
        }
    }

    private class IsolatedApplication(private val actual: Application, root: File) : Application() {
        private val files = File(root, "files").apply { check(mkdir()) }
        private val cache = File(root, "cache").apply { check(mkdir()) }
        private val prefix = "french_production_${UUID.randomUUID()}_"
        init { attachBaseContext(actual) }
        override fun getApplicationContext(): Context = this
        override fun getFilesDir(): File = files
        override fun getCacheDir(): File = cache
        // These installed model resources are the only shared data; no importer/downloader runs.
        override fun getExternalFilesDir(type: String?): File = requireNotNull(actual.getExternalFilesDir(type))
        override fun getSharedPreferences(name: String, mode: Int): SharedPreferences =
            actual.getSharedPreferences(prefix + name, mode)
    }
    private fun preferenceNames() = File(app.applicationInfo.dataDir, "shared_prefs").listFiles().orEmpty()
        .filter { it.name.endsWith(".xml") }.map { it.name.removeSuffix(".xml") }.toSet()
    private fun restorePreferences(original: Map<String, Map<String, *>>) {
        (preferenceNames() - original.keys).forEach { assertTrue(app.deleteSharedPreferences(it)) }
        original.forEach { (name, values) ->
            val target = app.getSharedPreferences(name, Context.MODE_PRIVATE)
            val edit = target.edit().clear()
            values.forEach { (key, value) -> when (value) {
                is String -> edit.putString(key, value)
                is Int -> edit.putInt(key, value)
                is Long -> edit.putLong(key, value)
                is Float -> edit.putFloat(key, value)
                is Boolean -> edit.putBoolean(key, value)
                is Set<*> -> edit.putStringSet(key, value.filterIsInstance<String>().toSet())
                else -> error("Unexpected QA preference $name/$key")
            } }
            assertTrue(edit.commit())
            assertEquals("The original QA progress and settings are restored", values, target.all)
        }
        assertEquals(original.keys, preferenceNames())
    }
    private fun awaitReader(message: String, reader: LessonNarrationController, errors: List<String>, condition: () -> Boolean) =
        await(message, 180_000) {
            assertTrue("The real production worker reports no error: $errors", errors.isEmpty())
            onMain { assertNull("Android TTS must never mask a native French failure", field<Any?>(reader, "tts")); condition() }
        }
    private fun await(message: String, timeout: Long, condition: () -> Boolean) {
        val deadline = SystemClock.uptimeMillis() + timeout
        while (SystemClock.uptimeMillis() < deadline) {
            if (condition()) return
            SystemClock.sleep(40)
        }
        fail(message)
    }
    private fun <T> onMain(action: () -> T): T {
        val result = AtomicReference<T>()
        instrumentation.runOnMainSync { result.set(action()) }
        return result.get()
    }
    @Suppress("UNCHECKED_CAST") private fun <T> field(instance: Any, name: String): T =
        instance.javaClass.getDeclaredField(name).apply { isAccessible = true }.get(instance) as T
    private fun segmentFile(reader: LessonNarrationController): File? = field<List<Any>>(reader, "segments")
        .singleOrNull()?.let { field(it, "file") }
    private fun fileIdentity(file: File) = Os.stat(file.absolutePath).let { listOf(it.st_dev, it.st_ino, it.st_size) }
    private fun sha256(file: File): String {
        val digest = MessageDigest.getInstance("SHA-256")
        file.inputStream().buffered().use { input ->
            val bytes = ByteArray(64 * 1024)
            while (true) { val size = input.read(bytes); if (size < 0) break; digest.update(bytes, 0, size) }
        }
        return digest.digest().joinToString("") { "%02x".format(it) }
    }

    companion object {
        private const val QA_PACKAGE = "com.learngraphicsprogramming.app.curricularqa"
        private const val VOICE = "ff_siwis"
        private const val SOURCE = "La fonction main utilise std::cout pour afficher le résultat."
    }
}
