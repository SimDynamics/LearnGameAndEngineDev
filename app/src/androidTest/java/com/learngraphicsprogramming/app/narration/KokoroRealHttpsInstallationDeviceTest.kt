package com.learngraphicsprogramming.app.narration

import android.app.DownloadManager
import android.content.Context
import android.media.AudioAttributes
import android.media.MediaPlayer
import android.os.SystemClock
import android.graphics.Bitmap
import android.graphics.Rect
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.Button
import android.widget.TextView
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.OptionsActivity
import com.learngraphicsprogramming.app.R
import com.learngraphicsprogramming.app.narration.phonemes.EnglishMisakiBartG2P
import com.learngraphicsprogramming.app.narration.phonemes.EnglishMisakiG2P
import com.learngraphicsprogramming.app.narration.phonemes.EnglishMisakiSpacyPos
import com.learngraphicsprogramming.app.narration.phonemes.FrenchFluidG2P
import com.learngraphicsprogramming.app.narration.phonemes.SpanishFluidG2P
import java.io.DataOutputStream
import java.io.File
import java.util.concurrent.CountDownLatch
import java.util.concurrent.ExecutorService
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import kotlin.math.abs
import kotlin.math.sqrt
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Before
import org.junit.FixMethodOrder
import org.junit.Test
import org.junit.runner.RunWith
import org.junit.runners.MethodSorters

/**
 * Real network acceptance in an otherwise empty, separately installed APK. This suite neither
 * imports fixtures nor borrows existing app files. It deliberately retains its verified downloads
 * and raw evidence, so a failed network run can be inspected without clearing any user's data.
 * Run only against LDPlayer with the dedicated -Plgp.realDownloadsQa=true application ID.
 */
@RunWith(AndroidJUnit4::class)
@FixMethodOrder(MethodSorters.NAME_ASCENDING)
class KokoroRealHttpsInstallationDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val preferences get() = context.getSharedPreferences("kokoro_model_downloads", Context.MODE_PRIVATE)
    private val downloads get() = context.getSystemService(Context.DOWNLOAD_SERVICE) as DownloadManager
    private val artifacts get() = KokoroModelManifest.artifacts
    private val evidence get() = File(context.filesDir, "real-https-kokoro").apply { mkdirs() }

    @Before fun requireDedicatedEmptyQaApplicationOnAnEmulator() {
        assertEquals("Never mutate the user's app or existing curricular QA installation",
            "com.learngraphicsprogramming.app.realdownloadsqa", context.packageName)
        val emulatorProperties = listOf("ro.boot.qemu", "ro.kernel.qemu").associateWith { property ->
            val result = instrumentation.uiAutomation.executeShellCommand("getprop $property")
            android.os.ParcelFileDescriptor.AutoCloseInputStream(result).bufferedReader().use { it.readText().trim() }
        }
        assertTrue("This suite is reserved for the explicitly selected LDPlayer emulator: $emulatorProperties",
            emulatorProperties.values.any { it == "1" })
        assertEquals("The manifest must still include one shared graph, 32 voices, six dictionaries and eight auxiliary files", 50, artifacts.size)
        assertEquals(50, artifacts.map { it.fileName }.toSet().size)
        assertTrue("Every installation target needs a genuine HTTPS origin", artifacts.all { it.url.startsWith("https://") })
    }

    @Test fun a_realVoiceTransferCanBeCancelledRetriedAndRecovered() {
        var store = KokoroModelStore(context)
        try {
            val root = store.directory()
            assertTrue("Use a fresh dedicated APK, without copied or previously installed resources",
                artifacts.none { File(root, it.fileName).exists() })
            assertTrue("The clean application must not already own resource transfers", records().isEmpty())
            val voice = KokoroVoiceCatalog.find("em_santa")!!.artifact
            val selection = listOf(voice)
            assertEquals(KokoroModelStatus.NOT_INSTALLED, inspect(store, selection).status)
            assertTrue("Inspection alone must not download anything", records().isEmpty())
            try {
                store.startDownload(false, selection)
                fail("A real transfer needs the user's explicit acceptance")
            } catch (_: IllegalArgumentException) { }
            assertTrue(records().isEmpty())

            store.startDownload(true, selection)
            settle(store)
            val firstId = preferences.getLong(key(voice), -1L)
            assertTrue("A system DownloadManager ID must be persisted", firstId >= 0)
            val first = records().single { it.id == firstId }
            assertEquals(voice.url, first.url)
            // A completed OS transfer remains unpromoted until the store verifies it. Cancellation
            // therefore remains meaningful even when this small file finishes before this line.
            assertFalse(File(root, voice.fileName).exists())
            store.cancelDownload(selection)
            settle(store)
            assertEquals(KokoroModelStatus.NOT_INSTALLED, inspect(store, selection).status)
            assertFalse(preferences.contains(key(voice)))
            assertFalse(File(root, "${voice.fileName}.part").exists())
            assertTrue("Cancellation removes only this application's system transfer", records().isEmpty())

            store.startDownload(true, selection)
            settle(store)
            val retriedId = preferences.getLong(key(voice), -1L)
            assertTrue("An actual retry creates a new system transfer", retriedId >= 0 && retriedId != firstId)
            assertEquals(voice.url, records().single { it.id == retriedId }.url)
            close(store)
            store = KokoroModelStore(context)
            assertEquals("Store recreation must recover the same transfer", retriedId, preferences.getLong(key(voice), -1L))
            awaitInstalled(store, selection, "voice-retry-progress.json", 180_000)
            assertTrue(KokoroModelManifest.verify(File(root, voice.fileName), voice))
            assertTrue(records().isEmpty())
            save("voice-recovery.json", JSONObject().put("cleanApplication", true)
                .put("source", voice.url).put("firstDownloadId", firstId)
                .put("statusBeforeCancellation", first.status).put("bytesBeforeCancellation", first.bytes)
                .put("retriedDownloadId", retriedId).put("sameIdAfterStoreRecreation", true)
                .put("installedSha256", voice.sha256).put("manualCopies", false))
        } finally { close(store) }
    }

    @Test fun b_allFiftyFilesDownloadOverHttpsVerifyAndRemainReusable() {
        var store = KokoroModelStore(context)
        try {
            val root = store.directory()
            assertFalse("The FP32 shared graph must still be absent before this real network installation", File(root, "model.onnx").exists())
            val installedBefore = artifacts.filter { KokoroModelManifest.verify(File(root, it.fileName), it) }
            assertEquals("Only the small voice from the recovery acceptance may already be present", listOf("em_santa.bin"), installedBefore.map { it.fileName })
            val preserved = installedBefore.associate { it.fileName to stamp(File(root, it.fileName)) }
            assertTrue(records().isEmpty())
            store.startDownload(true, artifacts)
            settle(store)
            val actualTransfers = records()
            assertEquals("Every absent file must have its own real system-managed transfer", artifacts.size - installedBefore.size, actualTransfers.size)
            actualTransfers.forEach { record ->
                val artifact = artifacts.single { it.url == record.url }
                assertEquals(record.id, preferences.getLong(key(artifact), -1L))
                assertTrue(record.url.startsWith("https://"))
            }
            save("download-origins.json", JSONObject().put("manualCopies", false)
                .put("bundletoolLocalTesting", false).put("network", "Android DownloadManager HTTPS")
                .put("transfers", JSONArray(actualTransfers.map { it.json() })))
            val idsBefore = preferences.all.filterKeys { it.startsWith("download:") }
            close(store)
            store = KokoroModelStore(context)
            assertEquals("Recreating the owner cannot enqueue replacement downloads", idsBefore,
                preferences.all.filterKeys { it.startsWith("download:") })
            awaitInstalled(store, artifacts, "all-resource-progress.json", 25 * 60_000L)
            val stamps = artifacts.associate { artifact ->
                val file = File(root, artifact.fileName)
                assertTrue("Downloaded bytes must match the pinned SHA-256: ${artifact.fileName}", KokoroModelManifest.verify(file, artifact))
                artifact.fileName to stamp(file)
            }
            preserved.forEach { (name, before) -> assertEquals("A valid voice must be reused without replacement", before, stamps[name]) }
            assertTrue("Successful promotion clears pending DownloadManager rows", records().isEmpty())
            store.startDownload(true, artifacts)
            settle(store)
            assertEquals(KokoroModelStatus.INSTALLED, inspect(store, artifacts).status)
            assertTrue("Explicit installation of valid resources must not download any file twice", records().isEmpty())
            artifacts.forEach { artifact -> assertEquals("Reusing ${artifact.fileName} must preserve bytes and modification time", stamps[artifact.fileName], stamp(File(root, artifact.fileName))) }
            save("installed-resources.json", JSONObject().put("processId", android.os.Process.myPid()).put("count", artifacts.size)
                .put("totalBytes", artifacts.sumOf { it.bytes }).put("allVerified", true)
                .put("unchangedOnSecondInstall", true).put("resources", JSONArray(artifacts.map {
                    JSONObject().put("name", it.fileName).put("bytes", it.bytes).put("sha256", it.sha256).put("url", it.url)
                })))
        } finally { close(store) }
    }

    @Test fun c_downloadedModelsSpeakRealCurriculumAndPreserveOriginalDialectsAndContext() {
        val store = KokoroModelStore(context)
        val root = try {
            assertEquals("Synthesis must use the bytes installed by the preceding HTTPS tests", KokoroModelStatus.INSTALLED, inspect(store, artifacts).status)
            store.directory()
        } finally { close(store) }
        val before = artifacts.associate { it.fileName to stamp(File(root, it.fileName)) }
        val cases = mutableListOf<Speech>()
        val spanish = SpanishFluidG2P.loadLexicon(File(root, "es_lexicon_cache.json"))
        val french = FrenchFluidG2P(root, File(root, "tokenizer.json"))
        cases += Speech("spanish", "ef_dora", "Las fracciones equivalentes representan la misma cantidad.",
            SpanishFluidG2P.phonemize("Las fracciones equivalentes representan la misma cantidad.", spanish))
        cases += Speech("french", "ff_siwis", "Les fractions équivalentes représentent la même quantité.",
            french.phonemize("Les fractions équivalentes représentent la même quantité."))
        listOf(Triple(false, "af_heart", "Wtˌɑfdˈɪkʃənˌɛɹi"), Triple(true, "bf_emma", "Wtˌɒfdɪktˈɪʃənəɹi")).forEach { (british, voice, expected) ->
            val phones = EnglishMisakiBartG2P(root, british).use { it.phonemizeWord("outofdictionary") }
            assertEquals("The FP32 download must preserve the original source model's phonemes", expected, phones)
            cases += Speech(if (british) "gb-oov" else "us-oov", voice, "outofdictionary", phones)
        }
        val contextSentence = "They will read it. Yesterday they read it."
        EnglishMisakiSpacyPos(root).use { pos ->
            assertEquals(listOf("VB", "VBD"), pos.tokenizeAndTag(contextSentence).filter { it.text == "read" }.map { it.tag })
        }
        EnglishMisakiG2P(root).use { english ->
            val phones = english.phonemize(contextSentence)
            assertTrue("Native POS must choose the distinct source pronunciations", "ɹˈid" in phones && "ɹˈɛd" in phones)
            cases += Speech("us-context", "af_heart", contextSentence, phones)
            val lessonPath = "content/en/programming/cpp_fundamentals/programas_y_ejecucion/lesson_primer_programa_y_salida.json"
            val lesson = JSONObject(context.assets.open(lessonPath).bufferedReader(Charsets.UTF_8).use { it.readText() })
            val firstPage = NarrationSegments.sentences(LessonNarrationText.page(lesson, 0, NarrationLanguage.AMERICAN_ENGLISH, forKokoro = true))
            val technical = firstPage.first { "iostream" in it }
            val question = lesson.getJSONArray("pages").getJSONObject(0).getJSONArray("blocks").getJSONObject(4).getString("question")
            assertTrue("The real puzzle's original question must be part of its narration", question in firstPage)
            listOf("curricular-technical" to technical, "curricular-puzzle" to question).forEach { (name, text) ->
                cases += Speech(name, "af_heart", text, english.phonemize(text))
            }
            val british = "A program expresses instructions that a computer can execute."
            cases += Speech("gb-sentence", "bf_emma", british, english.phonemize(british, british = true))
            val fallbackPhones = mapOf(
                NarrationLanguage.SPANISH to SpanishFluidG2P.phonemize("La suma.", spanish),
                NarrationLanguage.FRENCH to french.phonemize("La somme."),
                NarrationLanguage.AMERICAN_ENGLISH to english.phonemize("The sum."),
                NarrationLanguage.BRITISH_ENGLISH to english.phonemize("The sum.", british = true))
            KokoroOnnxEngine(root).use { engine ->
                val audio = JSONArray()
                cases.forEach { sample ->
                    val pcm = synthesize(engine, sample.phonemes, sample.voice)
                    assertPcm(sample.voice, pcm)
                    val wav = File(evidence, "${sample.name}.wav")
                    writeWav(wav, pcm)
                    playToCompletion(wav, pcm.size)
                    audio.put(JSONObject().put("name", sample.name).put("voice", sample.voice)
                        .put("text", sample.text).put("phonemes", sample.phonemes)
                        .put("samples", pcm.size).put("actualPlaybackCompleted", true))
                }
                KokoroVoiceCatalog.voices.forEach { voice ->
                    assertPcm(voice.id, synthesize(engine, fallbackPhones.getValue(voice.language), voice.id))
                }
                save("real-synthesis-playback.json", JSONObject().put("processId", android.os.Process.myPid()).put("audio", audio)
                    .put("all32VoicesProducedNonSilentPcm", true).put("resourcesInstalledThroughHttps", true))
            }
        }
        artifacts.forEach { assertEquals("Synthesis must preserve all installed resources", before[it.fileName], stamp(File(root, it.fileName))) }
        assertTrue("Synthesis must not request additional downloads", records().isEmpty())
    }

    @Test fun d_realSettingsShowVerifiedEnglishInstallationWithoutZipOrExtraDownloads() {
        val selectedPreferences = context.getSharedPreferences("lesson_narration_settings", Context.MODE_PRIVATE)
        val saved = selectedPreferences.all.toMap()
        val transfersBefore = records()
        val store = KokoroModelStore(context)
        try {
            assertEquals(KokoroModelStatus.INSTALLED, inspect(store, artifacts).status)
            NarrationSettingsRepository.saveLanguage(context, NarrationLanguage.AMERICAN_ENGLISH)
            ActivityScenario.launch(OptionsActivity::class.java).use { scenario ->
                val deadline = SystemClock.uptimeMillis() + 90_000
                var verified = false
                while (!verified && SystemClock.uptimeMillis() < deadline) {
                    scenario.onActivity { activity ->
                        activity.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                        val host = activity.findViewById<ViewGroup>(R.id.narrationSettingsSection)
                        val label = activity.getString(R.string.narration_install_english)
                        val button = views(host).filterIsInstance<Button>().single { it.text.toString() == label }
                        val status = activity.getString(R.string.narration_aux_status, activity.getString(R.string.narration_verified))
                        verified = views(host).filterIsInstance<TextView>().any { it.text.toString() == status } && !button.isEnabled
                        if (verified) {
                            assertEquals("The English installer remains part of the normal settings UI", View.VISIBLE, button.visibility)
                            assertFalse("Installed, verified resources disable only the redundant installation action", button.isEnabled)
                            assertTrue("No ZIP import action may remain in the normal resource interface",
                                views(host).filterIsInstance<Button>().none {
                                    it.text.toString().lowercase().let { text -> "zip" in text || "import" in text }
                                })
                            assertTrue("Android TTS must retain its official installation control",
                                views(host).filterIsInstance<Button>().any { it.text.toString() == activity.getString(R.string.narration_install_android) })
                            button.requestRectangleOnScreen(Rect(0, 0, button.width, button.height), true)
                        }
                    }
                    if (!verified) SystemClock.sleep(250)
                }
                assertTrue("Settings must display the real verified English resource state", verified)
                instrumentation.waitForIdleSync()
                instrumentation.uiAutomation.takeScreenshot()?.let { bitmap ->
                    File(evidence, "settings-english-installed.png").outputStream().use {
                        assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it))
                    }
                    bitmap.recycle()
                }
                save("settings-english-installed.json", JSONObject().put("installedFromHttps", true)
                    .put("englishInstallationButtonVisible", true).put("disabledBecauseVerified", true)
                    .put("zipImportButtonPresent", false).put("androidTtsInstallControlPresent", true)
                    .put("androidTtsInstallerActuallyLaunched", false))
            }
            assertEquals("Opening real settings must not start another transfer", transfersBefore, records())
        } finally {
            close(store)
            val editor = selectedPreferences.edit().clear()
            saved.forEach { (key, value) ->
                when (value) {
                    is String -> editor.putString(key, value)
                    is Int -> editor.putInt(key, value)
                    is Long -> editor.putLong(key, value)
                    is Float -> editor.putFloat(key, value)
                    is Boolean -> editor.putBoolean(key, value)
                    is Set<*> -> editor.putStringSet(key, value.filterIsInstance<String>().toSet())
                }
            }
            assertTrue("Restore the original narration selection", editor.commit())
        }
    }

    private fun views(view: View): List<View> = listOf(view) +
        if (view is ViewGroup) (0 until view.childCount).flatMap { views(view.getChildAt(it)) } else emptyList()

    private data class Speech(val name: String, val voice: String, val text: String, val phonemes: String)
    private data class Transfer(val id: Long, val url: String, val status: Int, val bytes: Long, val total: Long, val reason: Int) {
        fun json() = JSONObject().put("id", id).put("url", url).put("status", status)
            .put("downloadedBytes", bytes).put("totalBytes", total).put("reason", reason)
    }
    private fun records(): List<Transfer> = downloads.query(DownloadManager.Query()).use { cursor ->
        if (cursor == null) return@use emptyList()
        val list = mutableListOf<Transfer>()
        while (cursor.moveToNext()) list += Transfer(cursor.getLong(cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_ID)),
            cursor.getString(cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_URI)),
            cursor.getInt(cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_STATUS)),
            cursor.getLong(cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_BYTES_DOWNLOADED_SO_FAR)),
            cursor.getLong(cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_TOTAL_SIZE_BYTES)),
            cursor.getInt(cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_REASON)))
        list
    }
    private fun key(artifact: KokoroModelManifest.Artifact) = "download:${KokoroModelManifest.revision}:${artifact.fileName}"
    private fun stamp(file: File) = file.length() to file.lastModified()
    private fun executor(store: KokoroModelStore) = store.javaClass.getDeclaredField("worker").apply { isAccessible = true }.get(store) as ExecutorService
    private fun settle(store: KokoroModelStore) { executor(store).submit {}.get(90, TimeUnit.SECONDS) }
    private fun close(store: KokoroModelStore) { store.close(); assertTrue("The old store must release its worker", executor(store).awaitTermination(90, TimeUnit.SECONDS)) }
    private fun inspect(store: KokoroModelStore, selected: List<KokoroModelManifest.Artifact>): KokoroModelState {
        val latch = CountDownLatch(1)
        var result: KokoroModelState? = null
        store.inspectArtifacts(selected) { result = it; latch.countDown() }
        assertTrue("Resource inspection must complete", latch.await(90, TimeUnit.SECONDS))
        return result!!
    }
    private fun awaitInstalled(store: KokoroModelStore, selected: List<KokoroModelManifest.Artifact>, name: String, timeout: Long) {
        val start = SystemClock.elapsedRealtime()
        val progress = JSONArray()
        var lastBytes = -1L
        while (true) {
            val transfers = records()
            val state = inspect(store, selected)
            if (state.downloadedBytes != lastBytes || state.status != KokoroModelStatus.DOWNLOADING) {
                progress.put(JSONObject().put("elapsedMs", SystemClock.elapsedRealtime() - start)
                    .put("status", state.status.name).put("percentage", state.percentage)
                    .put("downloadedBytes", state.downloadedBytes).put("totalBytes", state.totalBytes)
                    .put("error", state.error ?: JSONObject.NULL).put("systemTransfers", JSONArray(transfers.map { it.json() })))
                save(name, JSONObject().put("progress", progress))
                lastBytes = state.downloadedBytes
            }
            if (state.status == KokoroModelStatus.INSTALLED) {
                assertEquals(100, state.percentage)
                assertEquals(selected.sumOf { it.bytes }, state.downloadedBytes)
                return
            }
            assertNotEquals("A real HTTPS failure must remain visible in the evidence: ${state.error}", KokoroModelStatus.ERROR, state.status)
            assertEquals("An accepted real transfer must remain recoverable", KokoroModelStatus.DOWNLOADING, state.status)
            assertTrue("The real system downloads did not complete: ${state.error}", SystemClock.elapsedRealtime() - start < timeout)
            SystemClock.sleep(500)
        }
    }
    private fun save(name: String, json: JSONObject) { File(evidence, name).writeText(json.toString(2), Charsets.UTF_8) }
    private fun synthesize(engine: KokoroOnnxEngine, phonemes: String, voice: String): FloatArray {
        val pieces = KokoroPhonemeChunks.split(phonemes).map { engine.synthesizePhonemes(it, voice, 1f) }
        val output = FloatArray(pieces.sumOf { it.size })
        var at = 0
        pieces.forEach { piece -> piece.copyInto(output, at); at += piece.size }
        return output
    }
    private fun assertPcm(voice: String, pcm: FloatArray) {
        assertTrue("$voice must generate speech-duration, finite PCM", pcm.size > 2_400 && pcm.all { it.isFinite() })
        assertTrue("$voice must produce audible non-silent samples", pcm.maxOf { abs(it) } > 0.005f && sqrt(pcm.sumOf { it.toDouble() * it } / pcm.size) > 0.0005)
    }
    private fun writeWav(file: File, pcm: FloatArray) {
        DataOutputStream(file.outputStream().buffered()).use { output ->
            fun intLE(value: Int) = output.writeInt(Integer.reverseBytes(value))
            fun shortLE(value: Int) = output.writeShort(java.lang.Short.reverseBytes(value.toShort()).toInt())
            output.writeBytes("RIFF"); intLE(36 + pcm.size * 2); output.writeBytes("WAVE")
            output.writeBytes("fmt "); intLE(16); shortLE(1); shortLE(1)
            intLE(24_000); intLE(48_000); shortLE(2); shortLE(16)
            output.writeBytes("data"); intLE(pcm.size * 2)
            pcm.forEach { shortLE((it.coerceIn(-1f, 1f) * 32767f).toInt()) }
        }
    }
    private fun playToCompletion(file: File, samples: Int) {
        val prepared = CountDownLatch(1)
        val completed = CountDownLatch(1)
        val error = AtomicInteger(0)
        var player: MediaPlayer? = null
        try {
            instrumentation.runOnMainSync {
                player = MediaPlayer().apply {
                    setAudioAttributes(AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA).setContentType(AudioAttributes.CONTENT_TYPE_SPEECH).build())
                    setDataSource(file.absolutePath)
                    setOnPreparedListener { prepared.countDown() }
                    setOnCompletionListener { completed.countDown() }
                    setOnErrorListener { _, what, _ -> error.set(what); prepared.countDown(); completed.countDown(); true }
                    prepareAsync()
                }
            }
            assertTrue("Generated Kokoro WAV must prepare", prepared.await(15, TimeUnit.SECONDS))
            assertEquals(0, error.get())
            instrumentation.runOnMainSync {
                assertTrue(abs(player!!.duration.toLong() - samples * 1000L / 24_000L) <= 100)
                player!!.start()
            }
            assertTrue("The complete real sentence must finish playback", completed.await(samples / 24_000L + 20, TimeUnit.SECONDS))
            assertEquals(0, error.get())
        } finally { instrumentation.runOnMainSync { player?.release() } }
    }
}
