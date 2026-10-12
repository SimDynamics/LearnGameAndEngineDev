package com.learngraphicsprogramming.app.narration

import android.app.Application
import android.content.Context
import android.content.ContextWrapper
import android.content.Intent
import android.content.SharedPreferences
import android.media.AudioAttributes
import android.media.MediaPlayer
import android.os.Build
import android.os.Looper
import android.os.SystemClock
import android.view.WindowManager
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.BuildConfig
import com.learngraphicsprogramming.app.AppearanceSettingsRepository
import com.learngraphicsprogramming.app.CoursesActivity
import com.learngraphicsprogramming.app.narration.phonemes.EnglishMisakiG2P
import com.learngraphicsprogramming.app.narration.phonemes.MisakiPartOfSpeechTagger
import com.learngraphicsprogramming.app.narration.phonemes.SpanishFluidG2P
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.security.MessageDigest
import java.util.UUID
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.atomic.AtomicReference
import kotlin.math.abs
import kotlin.math.sqrt
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

/**
 * A listening comparison, never an automatic claim of naturalness. Run only in the isolated
 * curricularqa package on an emulator. Uses installed, verified files and exports to a fresh
 * UUID directory; preserves all earlier recordings, preferences, models and lesson assets.
 * B is an experimental join, not an alternative production cache or playback implementation.
 */
@RunWith(AndroidJUnit4::class)
class KokoroManualEnglishComparisonDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val app = instrumentation.targetContext

    @Test fun verifiesExistingComparisonPlaybackAndSelectiveCache() {
        assertEquals(QA_PACKAGE, app.packageName); assertEquals(QA_PACKAGE, BuildConfig.APPLICATION_ID)
        val emulator = android.os.ParcelFileDescriptor.AutoCloseInputStream(
            instrumentation.uiAutomation.executeShellCommand("getprop ro.boot.qemu")
        ).use { it.readBytes().toString(Charsets.UTF_8).trim() }
        assertEquals("Run only on the dedicated emulator", "1", emulator)
        val argument = requireNotNull(InstrumentationRegistry.getArguments().getString("existingRunDirectory"))
        val exports = File(app.filesDir, "manual-english-comparisons").canonicalFile
        val existing = File(argument).let { if (it.isAbsolute) it else File(exports, argument) }.canonicalFile
        require(existing.parentFile == exports && existing.name.matches(Regex("run-[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}")))
        require(existing.isDirectory)
        val originalManifest = File(existing, "comparison.json")
        val document = JSONObject(originalManifest.readText())
        val records = document.getJSONArray("samples")
        assertEquals(12, records.length())
        for (index in 0 until records.length()) {
            val record = records.getJSONObject(index)
            val file = File(existing, record.getString("wav")).canonicalFile
            assertEquals(existing, file.parentFile); assertTrue(file.name.endsWith(".wav"))
            assertEquals("Existing audio must remain byte-for-byte identical", record.getString("wavSha256"), sha256(file))
            val bytes = file.readBytes(); val pcm = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
            assertEquals("RIFF", String(bytes, 0, 4, Charsets.US_ASCII)); assertEquals("WAVE", String(bytes, 8, 4, Charsets.US_ASCII))
            assertEquals(1, pcm.getShort(20).toInt()); assertEquals(1, pcm.getShort(22).toInt())
            assertEquals(SAMPLE_RATE, pcm.getInt(24)); assertEquals(16, pcm.getShort(34).toInt())
            assertEquals(bytes.size - 44, pcm.getInt(40))
            val count = (bytes.size - 44) / 2; assertTrue(count >= 2_400)
            var energy = 0.0; var peak = 0.0; var clipped = 0
            pcm.position(44)
            while (pcm.remaining() >= 2) {
                val value = pcm.short / 32767.0; energy += value * value; peak = maxOf(peak, abs(value))
                if (abs(value) >= 1.0) clipped++
            }
            val waveformRms = sqrt(energy / count)
            assertTrue(waveformRms > 0.0001 && peak > 0.001)
            assertEquals(record.getDouble("rms"), waveformRms, 2.0 / 32767)
            assertEquals(record.getDouble("peak"), peak, 2.0 / 32767)
            assertEquals(record.getDouble("durationSeconds"), count.toDouble() / SAMPLE_RATE, 0.000001)
            assertTrue(clipped.toDouble() / count < 0.001)
        }
        val playback = JSONArray()
        for (index in 0..2) {
            val record = records.getJSONObject(index)
            playback.put(checkPlayback(record.getString("id"), File(existing, record.getString("wav"))))
        }
        val sandbox = File(app.cacheDir, "manual-english-cache-audit-${UUID.randomUUID()}").canonicalFile
        assertEquals(app.cacheDir.canonicalFile, sandbox.parentFile); assertTrue(sandbox.mkdir())
        val isolated = object : ContextWrapper(app) {
            override fun getApplicationContext(): Context = this
            override fun getFilesDir(): File = sandbox
        }
        val original = records.getJSONObject(0); val a = records.getJSONObject(1)
        assertEquals("original", original.getString("variant")); assertEquals("A-same-voice", a.getString("variant"))
        val raw = original.getString("raw"); assertEquals(raw, a.getString("raw"))
        val annotated = SpanishTechnicalSpeech.prepareForKokoro(raw).text
        val cachedOriginal = requireNotNull(KokoroNarrationCache.store(isolated, "es", SPANISH_VOICE, raw,
            File(existing, original.getString("wav"))))
        val cachedA = requireNotNull(KokoroNarrationCache.store(isolated, "es", SPANISH_VOICE, annotated,
            File(existing, a.getString("wav"))))
        assertNotEquals(cachedOriginal, cachedA)
        val originalHash = sha256(cachedOriginal); val mixedHash = sha256(cachedA)
        assertEquals(original.getString("wavSha256"), originalHash); assertEquals(a.getString("wavSha256"), mixedHash)
        assertEquals(cachedOriginal, KokoroNarrationCache.lookup(isolated, "es", SPANISH_VOICE, raw))
        assertEquals(cachedA, KokoroNarrationCache.lookup(isolated, "es", SPANISH_VOICE, annotated))
        KokoroNarrationCache.prune(isolated)
        assertEquals(originalHash, sha256(cachedOriginal)); assertEquals(mixedHash, sha256(cachedA))
        val audit = JSONObject().put("status", "passed").put("existingRun", existing.absolutePath)
            .put("nativeRecordingsVerified", 12).put("newInferenceCalls", 0).put("playbackChecks", playback)
            .put("separatePronunciationCacheKeys", true).put("originalAudioPreserved", true)
            .put("originalSha256", originalHash).put("mixedSha256", mixedHash).put("naturalnessAccepted", false)
        val report = File(existing, "playback-cache-audit-${UUID.randomUUID()}.json")
        assertFalse(report.exists()); report.writeText(audit.toString(2))
        document.put("originalRunStatus", document.optString("status"))
            .put("status", "exported-and-verified-listening-pending").put("revalidationAudit", report.name)
            .put("naturalnessAccepted", false)
        originalManifest.writeText(document.toString(2))
    }

    @Test fun markedSpeechUsesProductionControllerAcrossPauseRateNavigationAndCacheReuse() {
        assertEquals(QA_PACKAGE, app.packageName)
        assertEquals(QA_PACKAGE, BuildConfig.APPLICATION_ID)
        val emulator = android.os.ParcelFileDescriptor.AutoCloseInputStream(
            instrumentation.uiAutomation.executeShellCommand("getprop ro.boot.qemu")
        ).use { it.readBytes().toString(Charsets.UTF_8).trim() }
        assertEquals("Never exercise this test on the user's physical phone", "1", emulator)
        assertTrue(Build.SUPPORTED_ABIS.any { it == "x86_64" || it == "x86" })
        val preferencesBefore = preferenceNames().associateWith { name ->
            app.getSharedPreferences(name, Context.MODE_PRIVATE).all.mapValues { (_, value) ->
                if (value is Set<*>) value.toSet() else value
            }
        }
        val root = modelRoot()
        val modelsBefore = root.listFiles().orEmpty().associate { it.name to (it.length() to it.lastModified()) }
        val oldFiles = protectedFiles()
        val sandbox = File(app.cacheDir, "manual-english-controller-${UUID.randomUUID()}").canonicalFile
        assertEquals(app.cacheDir.canonicalFile, sandbox.parentFile); assertTrue(sandbox.mkdir())
        val isolated = IsolatedApplication(app.applicationContext as Application, sandbox)
        val report = JSONObject().put("schema", 1).put("sandbox", sandbox.absolutePath)
            .put("naturalnessAccepted", false).put("productionController", true)
        fun persist() = File(sandbox, "controller.json").writeText(report.toString(2))
        var host: ActivityScenario<CoursesActivity>? = null
        var current: LessonNarrationController? = null
        val errors = CopyOnWriteArrayList<String>()
        val states = CopyOnWriteArrayList<Boolean>()
        try {
            // Suppress only the actual QA activity's unrelated narration; restore its values below.
            assertTrue(app.getSharedPreferences("lesson_narration_settings", Context.MODE_PRIVATE).edit()
                .putBoolean("enabled", false).putString("engine", NarrationEngine.ANDROID.name).commit())
            NarrationSettingsRepository.saveEnabled(isolated, true)
            NarrationSettingsRepository.saveEngine(isolated, NarrationEngine.KOKORO)
            NarrationSettingsRepository.saveLanguage(isolated, NarrationLanguage.SPANISH)
            NarrationSettingsRepository.saveKokoroVoice(isolated, SPANISH_VOICE)
            NarrationSettingsRepository.saveRate(isolated, 1f)
            NarrationStorageBudget.saveLimit(isolated, 64L * NarrationStorageBudget.MIB)
            val store = KokoroModelStore(isolated)
            val actualRoot = try { store.directory().canonicalFile } finally { store.close() }
            assertEquals("Read only the existing QA private model directory", root, actualRoot)
            host = ActivityScenario.launch<CoursesActivity>(Intent(app, CoursesActivity::class.java)
                .putExtra("routeId", AppearanceSettingsRepository.ROUTE_PROGRAMMING))
            val focusedHost = requireNotNull(host)
            var focusedActivity: android.app.Activity? = null
            focusedHost.onActivity { focusedActivity = it; it.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON) }
            await("The QA activity must be focused") { focusedActivity?.hasWindowFocus() == true }
            val controller = onMain { LessonNarrationController(isolated, { states.add(it) }, { errors.add(it) }) }
            current = controller
            val page = listOf("Utilizamos include para incluir la biblioteca iostream.",
                "El programa imprime mediante std::cout y conserva sus nombres españoles.")
                .map { SpanishTechnicalSpeech.prepareForKokoro(it).text }
            assertTrue(page.all { ManualEnglishSpeech.spans(it).isNotEmpty() })
            onMain { controller.prefetchPage(page, NarrationLanguage.SPANISH, "qa-manual-page-one") }
            awaitController("Two marked sentences must generate through the production worker", controller, errors) {
                page.indices.all { segmentFile(controller, it)?.isFile == true }
            }
            val files = onMain { page.indices.map { requireNotNull(segmentFile(controller, it)) } }
            val hashes = files.map(::sha256)
            fun generatedCount() = File(isolated.filesDir, KokoroNarrationDiagnostics.EVENTS_FILE).takeIf { it.isFile }
                ?.readLines()?.count { JSONObject(it).optString("event") == "Page sentence generated" } ?: 0
            val generations = generatedCount()
            assertEquals("Fresh page generates exactly two recordings", 2, generations)
            onMain {
                assertFalse(controller.isPlaying); assertNull(field<MediaPlayer?>(controller, "player"))
                assertFalse("Passive preparation does not hold audio focus", field<Boolean>(controller, "focusHeld"))
                controller.toggle(page, NarrationLanguage.SPANISH, "qa-manual-page-one")
            }
            awaitController("Play must use the real prepared mixed WAV", controller, errors) {
                field<Boolean>(controller, "playerReady") && controller.isPlaying &&
                    player(controller).isPlaying && player(controller).currentPosition >= 300
            }
            val paused = onMain {
                controller.toggle(page, NarrationLanguage.SPANISH, "qa-manual-page-one")
                field<Long>(controller, "offset")
            }
            SystemClock.sleep(250)
            onMain { assertFalse(controller.isPlaying); assertNull(field<MediaPlayer?>(controller, "player"))
                assertEquals("A paused bookmark must not advance", paused, field<Long>(controller, "offset"))
                controller.seekBy(1) }
            onMain {
                assertFalse("Paused seeking must not resume playback", controller.isPlaying)
                assertNull(field<MediaPlayer?>(controller, "player"))
                assertTrue(abs(field<Long>(controller, "offset") - (paused + 1_000L)) <= 180)
                NarrationSettingsRepository.saveRate(isolated, 1.5f); controller.settingsChanged()
                assertNull(field<MediaPlayer?>(controller, "player")); assertFalse(controller.isPlaying)
                controller.toggle(page, NarrationLanguage.SPANISH, "qa-manual-page-one")
            }
            awaitController("Resume must keep the prepared player at the changed speed", controller, errors) {
                field<Boolean>(controller, "playerReady") && controller.isPlaying && player(controller).isPlaying &&
                    player(controller).currentPosition >= paused + 1_100
            }
            onMain { assertEquals(1.5f, player(controller).playbackParams.speed, 0.001f)
                assertEquals(1f, player(controller).playbackParams.pitch, 0.001f)
                controller.toggle(page, NarrationLanguage.SPANISH, "qa-manual-page-one") }
            assertEquals(hashes, files.map(::sha256)); assertEquals(generations, generatedCount())
            val second = listOf(SpanishTechnicalSpeech.prepareForKokoro(
                "La función main usa double y string, y return entrega el resultado.").text)
            onMain { controller.prefetchPage(second, NarrationLanguage.SPANISH, "qa-manual-page-two") }
            awaitController("Navigation prepares the new marked page", controller, errors) {
                segmentFile(controller, 0)?.isFile == true
            }
            assertEquals(generations + 1, generatedCount())
            assertEquals("Navigation never deletes old WAVs", hashes, files.map(::sha256))
            onMain { controller.prefetchPage(page, NarrationLanguage.SPANISH, "qa-manual-page-one") }
            awaitController("Returning to the original page reuses its own marked recordings", controller, errors) {
                page.indices.all { segmentFile(controller, it)?.isFile == true }
            }
            assertEquals(files, onMain { page.indices.map { requireNotNull(segmentFile(controller, it)) } })
            assertEquals(hashes, files.map(::sha256)); assertEquals(generations + 1, generatedCount())
            assertTrue(true in states && false in states)
            report.put("status", "passed").put("markedSentences", page.size + second.size)
                .put("nativeGenerations", generatedCount()).put("sourceHashes", JSONArray(hashes))
                .put("pauseResumeSeekRateNavigationCachePassed", true)
            persist()
        } catch (error: Throwable) {
            report.put("status", "failed").put("failure", error.toString()).put("errors", JSONArray(errors.toList())); persist()
            throw error
        } finally {
            try {
                current?.let { controller ->
                    onMain { controller.close() }
                    await("Owned controller worker must close before restoring QA settings") {
                        !field<Thread>(controller, "workerThread").isAlive
                    }
                }
                host?.close(); instrumentation.waitForIdleSync()
                KokoroPagePreparationQueue.snapshot(app)
            } finally {
                restorePreferences(preferencesBefore, isolated.preferenceNames)
                assertEquals(modelsBefore, root.listFiles().orEmpty().associate { it.name to (it.length() to it.lastModified()) })
                val preserved = protectedFiles()
                // Preference values are restored via Android's API; its XML serialization can differ.
                oldFiles.filterKeys { !it.endsWith(".xml") }.forEach { (path, hash) -> assertEquals(hash, preserved[path]) }
                preferencesBefore.forEach { (name, values) ->
                    assertEquals("Restore original QA preferences: $name", values, app.getSharedPreferences(name, Context.MODE_PRIVATE).all)
                }
            }
        }
    }

    @Test fun exportsOriginalSameVoiceAndEnglishVoiceThenChecksPlaybackAndIsolatedCache() {
        assertEquals(QA_PACKAGE, app.packageName)
        assertEquals(QA_PACKAGE, BuildConfig.APPLICATION_ID)
        val emulatorProperty = android.os.ParcelFileDescriptor.AutoCloseInputStream(
            instrumentation.uiAutomation.executeShellCommand("getprop ro.boot.qemu")
        ).use { it.readBytes().toString(Charsets.UTF_8).trim() }
        assertTrue("This test must never run on the Honor or another physical phone",
            emulatorProperty == "1" && Build.SUPPORTED_ABIS.any { it == "x86_64" || it == "x86" })
        assertNotEquals(Looper.getMainLooper(), Looper.myLooper())
        val before = protectedFiles()
        val root = modelRoot()
        val resourcesBefore = root.listFiles().orEmpty().associate { it.name to (it.length() to it.lastModified()) }
        val artifacts = KokoroModelManifest.sharedArtifacts + KokoroModelManifest.resources(NarrationLanguage.SPANISH) +
            listOf("us_gold.json", "us_silver.json").map { name -> KokoroModelManifest.englishArtifacts.single { it.fileName == name } } +
            listOf(SPANISH_VOICE, ENGLISH_VOICE).map { requireNotNull(KokoroVoiceCatalog.find(it)).artifact }
        artifacts.forEach { assertTrue("Use an existing verified resource: ${it.fileName}",
            KokoroModelManifest.verify(File(root, it.fileName), it)) }
        val exportRoot = File(app.filesDir, "manual-english-comparisons").canonicalFile
        assertTrue(exportRoot.isDirectory || exportRoot.mkdirs())
        val output = File(exportRoot, "run-${UUID.randomUUID()}").canonicalFile
        assertEquals(exportRoot, output.parentFile)
        assertFalse(output.exists())
        assertTrue(output.mkdir())
        val sandbox = File(app.cacheDir, "manual-english-comparison-${UUID.randomUUID()}").canonicalFile
        assertEquals(app.cacheDir.canonicalFile, sandbox.parentFile)
        assertTrue(sandbox.mkdir())
        val isolated = object : ContextWrapper(app) {
            override fun getApplicationContext(): Context = this
            override fun getFilesDir(): File = sandbox
        }
        val records = JSONArray()
        val manifest = JSONObject().put("schema", 1).put("package", app.packageName)
            .put("sampleRate", SAMPLE_RATE).put("modelRevision", KokoroModelManifest.revision)
            .put("androidSdk", Build.VERSION.SDK_INT).put("abis", JSONArray(Build.SUPPORTED_ABIS.toList()))
            .put("emulator", true).put("performanceScope", "LDPlayer Android; ARM64 ONNX may run through the emulator native bridge")
            .put("spanishVoice", SPANISH_VOICE).put("englishVoice", ENGLISH_VOICE)
            .put("modelRoot", root.absolutePath).put("exportDirectory", output.absolutePath)
            .put("cacheDirectory", sandbox.absolutePath).put("naturalnessAccepted", false)
            .put("productionIntegrationOfB", false).put("samples", records)
        fun persist() = File(output, "comparison.json").writeText(manifest.toString(2))
        persist()
        try {
            val spanish = SpanishFluidG2P.loadLexicon(File(root, "es_lexicon_cache.json"))
            val vocabulary = KokoroOnnxInputs.loadVocabulary(File(root, "tokenizer.json"))
            val englishCache = mutableMapOf<String, String>()
            fun english(entry: ManualEnglishTerms.Entry): String = englishCache.getOrPut(entry.id) {
                EnglishMisakiG2P(openResource = { name ->
                    require(name in setOf("us_gold.json", "us_silver.json"))
                    File(root, name).inputStream()
                }, partOfSpeech = MisakiPartOfSpeechTagger { words ->
                    require(words.size == entry.tags.size) { "Reviewed alias and POS tokens disagree: ${entry.id}: $words" }
                    entry.tags
                }, neuralDirectory = null).use { it.phonemize(entry.englishText, british = false) }
            }
            val sentences = linkedMapOf(
                "01-include-iostream" to "Utilizamos include para incluir la biblioteca iostream.",
                "02-imprime-cout" to "El programa imprime mediante std::cout y la función imprime_total conserva su nombre.",
                "03-keywords" to "La función main usa double y string, y return entrega el resultado.",
                "04-ambiguous-vector" to "Un vector representa una dirección; el contenedor std::vector guarda valores."
            )
            val sources = mutableListOf<Pair<String, File>>()
            val creationStart = SystemClock.elapsedRealtime()
            KokoroOnnxEngine(root, preferAccelerator = false, cpuThreads = 2).use { engine ->
                manifest.put("sessionCreationMs", SystemClock.elapsedRealtime() - creationStart)
                    .put("usesXnnpack", engine.usesXnnpack).put("usesNnapi", engine.usesNnapi)
                for ((id, raw) in sentences) {
                    val matches = ManualEnglishTerms.select(raw)
                    assertTrue("Every comparison contains selected reviewed terms", matches.isNotEmpty())
                    if (id == "02-imprime-cout") assertEquals(listOf("cout"), matches.map { it.entry.spelling })
                    if (id == "04-ambiguous-vector") {
                        assertEquals(1, matches.count { it.entry.spelling == "vector" })
                        assertTrue(matches.single { it.entry.spelling == "vector" }.start > raw.indexOf("std::"))
                    }
                    var frontendStart = SystemClock.elapsedRealtime()
                    val original = SpanishFluidG2P.phonemize(raw, spanish)
                    val originalFrontendMs = SystemClock.elapsedRealtime() - frontendStart
                    frontendStart = SystemClock.elapsedRealtime()
                    val mixed = MixedSpanishG2P.phonemize(raw, spanish, ::english)
                    val mixedFrontendMs = SystemClock.elapsedRealtime() - frontendStart
                    frontendStart = SystemClock.elapsedRealtime()
                    val plan = MixedSpanishG2P.plan(raw, spanish, ::english)
                    val planFrontendMs = SystemClock.elapsedRealtime() - frontendStart
                    assertTrue(plan.any { it.english } && plan.any { !it.english })
                    if (id == "02-imprime-cout") assertTrue("Spanish imprime stays Spanish",
                        mixed.contains(SpanishFluidG2P.phonemize("imprime", spanish)))
                    val definitions = listOf("original" to original, "A-same-voice" to mixed)
                    for ((variant, phones) in definitions) {
                        val record = JSONObject().put("id", "$id-$variant").put("raw", raw)
                            .put("variant", variant).put("phonemes", phones).put("voice", SPANISH_VOICE)
                            .put("status", "synthesizing")
                            .put("frontendMs", if (variant == "original") originalFrontendMs else mixedFrontendMs)
                            .put("frontendTimingScope", "Actual frontend; English dictionaries and aliases are cached after first use")
                        records.put(record); persist()
                        val started = SystemClock.elapsedRealtime()
                        val chunks = KokoroPhonemeChunks.split(phones).map { part ->
                            KokoroOnnxInputs.tokenize(part, vocabulary)
                            engine.synthesizePhonemes(part, SPANISH_VOICE, 1f)
                        }
                        val pcm = join(chunks)
                        record.put("inferenceMs", SystemClock.elapsedRealtime() - started).put("inferenceCalls", chunks.size)
                        val file = export(output, record, pcm)
                        sources += "$id-$variant" to file
                        persist()
                    }
                    val b = JSONObject().put("id", "$id-B-english-voice").put("raw", raw)
                        .put("variant", "B-english-voice").put("status", "synthesizing").put("frontendMs", planFrontendMs)
                        .put("joining", "Boundary silence only, retains 25ms; 5ms fade; English RMS gain limited to 0.75..1.33")
                    records.put(b); persist()
                    val parts = JSONArray()
                    val started = SystemClock.elapsedRealtime()
                    val waves = plan.map { span ->
                        val voice = if (span.english) ENGLISH_VOICE else SPANISH_VOICE
                        val partStarted = SystemClock.elapsedRealtime()
                        val chunks = KokoroPhonemeChunks.split(span.phonemes).map { phones ->
                            KokoroOnnxInputs.tokenize(phones, vocabulary)
                            engine.synthesizePhonemes(phones, voice, 1f)
                        }
                        val pcm = join(chunks)
                        parts.put(JSONObject().put("text", span.text).put("phonemes", span.phonemes)
                            .put("english", span.english).put("voice", voice).put("samples", pcm.size)
                            .put("rms", rms(pcm)).put("inferenceMs", SystemClock.elapsedRealtime() - partStarted)
                            .put("inferenceCalls", chunks.size))
                        pcm
                    }
                    val referenceRms = plan.indices.filter { !plan[it].english }.map { rms(waves[it]) }
                        .sorted().let { it[it.size / 2] }
                    val adjusted = waves.mapIndexed { index, pcm ->
                        val trimmed = trimBoundarySilence(pcm, trimStart = index > 0, trimEnd = index < waves.lastIndex)
                        val peak = trimmed.maxOf { abs(it) }.toDouble()
                        val gain = if (plan[index].english) (referenceRms / rms(trimmed)).coerceIn(0.75, 1.33)
                            .coerceAtMost(if (peak > 0) 0.98 / peak else 1.0) else 1.0
                        val corrected = trimmed.map { (it * gain).toFloat() }.toFloatArray()
                        fade(corrected, fadeStart = index > 0, fadeEnd = index < waves.lastIndex)
                        parts.getJSONObject(index).put("joinedSamples", corrected.size).put("gain", gain)
                        corrected
                    }
                    b.put("parts", parts).put("inferenceMs", SystemClock.elapsedRealtime() - started)
                        .put("inferenceCalls", (0 until parts.length()).sumOf { parts.getJSONObject(it).getInt("inferenceCalls") })
                    sources += "$id-B-english-voice" to export(output, b, join(adjusted))
                    persist()
                }
            }
            // This exercises the Android decoder and clock using fresh native speech from all variants.
            val playback = JSONArray()
            for ((id, file) in sources.take(3)) playback.put(checkPlayback(id, file))
            manifest.put("playbackChecks", playback)
            val selected = sources.first { it.first.endsWith("A-same-voice") }.second
            val raw = sentences.values.first()
            val annotated = SpanishTechnicalSpeech.prepareForKokoro(raw).text
            val original = sources.first { it.first.endsWith("original") }.second
            val storedOriginal = requireNotNull(KokoroNarrationCache.store(isolated, "es", SPANISH_VOICE, raw, original))
            val originalHash = sha256(storedOriginal)
            val stored = requireNotNull(KokoroNarrationCache.store(isolated, "es", SPANISH_VOICE, annotated, selected))
            val hash = sha256(stored)
            assertNotEquals("Different pronunciation must have a different cache address", storedOriginal, stored)
            assertEquals(File(sandbox, "kokoro-narration-cache").canonicalFile, stored.canonicalFile.parentFile)
            assertEquals(stored, KokoroNarrationCache.lookup(isolated, "es", SPANISH_VOICE, annotated))
            assertEquals(storedOriginal, KokoroNarrationCache.lookup(isolated, "es", SPANISH_VOICE, raw))
            assertEquals(hash, sha256(stored))
            KokoroNarrationCache.prune(isolated)
            assertEquals(hash, sha256(stored))
            assertEquals("Old Spanish audio survives and remains usable", originalHash, sha256(storedOriginal))
            assertEquals(resourcesBefore, root.listFiles().orEmpty().associate { it.name to (it.length() to it.lastModified()) })
            val after = protectedFiles()
            before.forEach { (path, hash) -> assertEquals("A pre-existing recording or preference changed: $path", hash, after[path]) }
            manifest.put("status", "exported-and-verified-listening-pending").put("isolatedCacheSha256", hash)
                .put("originalCacheSha256", originalHash).put("cacheReused", true).put("protectedFilesPreserved", true)
            persist()
        } catch (error: Throwable) {
            manifest.put("status", "failed").put("failure", error.toString()); persist()
            throw error
        }
    }

    private fun modelRoot(): File {
        val supplied = InstrumentationRegistry.getArguments().getString("modelRoot")
        if (supplied != null) {
            val external = requireNotNull(app.getExternalFilesDir(null)).canonicalFile
            val private = app.filesDir.canonicalFile
            val file = File(supplied).canonicalFile
            require(file.path.startsWith(external.path + File.separator) || file.path.startsWith(private.path + File.separator)) {
                "Only QA-owned model files are allowed"
            }
            return file
        }
        val store = KokoroModelStore(app)
        return try { store.directory().canonicalFile } finally { store.close() }
    }

    private fun preferenceNames() = File(app.applicationInfo.dataDir, "shared_prefs").listFiles().orEmpty()
        .filter { it.name.endsWith(".xml") }.map { it.name.removeSuffix(".xml") }.toSet()

    private fun restorePreferences(saved: Map<String, Map<String, *>>, owned: Set<String>) {
        (preferenceNames() - saved.keys - owned).forEach { name ->
            assertTrue("Remove only a preference file created by this QA host", app.deleteSharedPreferences(name))
        }
        saved.forEach { (name, values) ->
            val editor = app.getSharedPreferences(name, Context.MODE_PRIVATE).edit().clear()
            values.forEach { (key, value) -> when (value) {
                is String -> editor.putString(key, value)
                is Boolean -> editor.putBoolean(key, value)
                is Int -> editor.putInt(key, value)
                is Long -> editor.putLong(key, value)
                is Float -> editor.putFloat(key, value)
                is Set<*> -> editor.putStringSet(key, value.filterIsInstance<String>().toSet())
                else -> error("Unexpected preference type: $name/$key")
            } }
            assertTrue(editor.commit())
        }
    }

    private class IsolatedApplication(private val actual: Application, root: File) : Application() {
        private val files = File(root, "files").apply { check(mkdir()) }
        private val cache = File(root, "cache").apply { check(mkdir()) }
        private val prefix = "manual_english_controller_${UUID.randomUUID()}_"
        val preferenceNames = java.util.Collections.synchronizedSet(mutableSetOf<String>())
        init { attachBaseContext(actual) }
        override fun getApplicationContext(): Context = this
        override fun getFilesDir(): File = files
        override fun getCacheDir(): File = cache
        // The QA-private model directory is shared read-only. Playback/cache/preferences are scoped.
        override fun getExternalFilesDir(type: String?): File =
            if (type == null) actual.filesDir else File(actual.filesDir, type)
        override fun getSharedPreferences(name: String, mode: Int): SharedPreferences {
            val scoped = prefix + name; preferenceNames.add(scoped)
            return actual.getSharedPreferences(scoped, mode)
        }
    }

    @Suppress("UNCHECKED_CAST") private fun <T> field(instance: Any, name: String): T =
        instance.javaClass.getDeclaredField(name).apply { isAccessible = true }.get(instance) as T

    private fun player(controller: LessonNarrationController): MediaPlayer = field(controller, "player")
    private fun segmentFile(controller: LessonNarrationController, index: Int): File? =
        field<List<Any>>(controller, "segments").getOrNull(index)?.let { field(it, "file") }

    private fun awaitController(label: String, controller: LessonNarrationController,
        errors: List<String>, condition: () -> Boolean) {
        val deadline = SystemClock.elapsedRealtime() + 240_000
        while (SystemClock.elapsedRealtime() < deadline) {
            assertTrue("$label: $errors", errors.isEmpty())
            assertNull("Native success cannot be hidden with Android TTS", field<Any?>(controller, "tts"))
            if (onMain(condition)) return
            SystemClock.sleep(50)
        }
        fail(label)
    }

    private fun export(root: File, record: JSONObject, pcm: FloatArray): File {
        assertTrue("Actual native speech must have duration", pcm.size >= 2_400)
        assertTrue("PCM must be finite", pcm.all { it.isFinite() })
        val peak = pcm.maxOf { abs(it) }
        val energy = rms(pcm)
        assertTrue("Native waveform must be non-silent", peak > 0.001f && energy > 0.0001)
        val file = File(root, record.getString("id") + ".wav").canonicalFile
        assertEquals(root, file.parentFile); assertFalse(file.exists())
        NarrationWavWriter.write(file, pcm)
        val header = ByteBuffer.wrap(file.readBytes(), 0, 44).order(ByteOrder.LITTLE_ENDIAN)
        assertEquals(pcm.size * 2 + 44L, file.length())
        assertEquals("RIFF", String(header.array(), 0, 4, Charsets.US_ASCII))
        assertEquals("WAVE", String(header.array(), 8, 4, Charsets.US_ASCII))
        assertEquals(SAMPLE_RATE, header.getInt(24)); assertEquals(16, header.getShort(34).toInt())
        record.put("status", "wav-ready").put("wav", file.name).put("wavSha256", sha256(file))
            .put("durationSeconds", pcm.size.toDouble() / SAMPLE_RATE).put("rms", energy).put("peak", peak.toDouble())
            .put("clippedFraction", pcm.count { abs(it) >= 1f }.toDouble() / pcm.size)
        assertTrue("Native output must not contain pervasive clipping", record.getDouble("clippedFraction") < 0.001)
        return file
    }

    private fun checkPlayback(id: String, file: File): JSONObject {
        val errors = AtomicReference<String?>()
        val player = onMain {
            MediaPlayer().apply {
                setAudioAttributes(AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA)
                    .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH).build())
                setOnErrorListener { _, what, extra -> errors.set("$what/$extra"); true }
                setDataSource(file.absolutePath); prepare(); start()
            }
        }
        try {
            await("Fresh WAV must play") { errors.get() == null && player.isPlaying && player.currentPosition >= 300 }
            val duration = onMain { player.duration }
            val paused = onMain { player.pause(); player.currentPosition }
            SystemClock.sleep(250)
            onMain { assertFalse(player.isPlaying); assertTrue(abs(player.currentPosition - paused) <= 80) }
            val target = (paused + 700).coerceAtMost(duration - 700)
            onMain { player.seekTo(target); player.playbackParams = player.playbackParams.setSpeed(1.5f).setPitch(1f); player.pause() }
            await("Seeking a paused WAV must update its decoder") { abs(player.currentPosition - target) <= 150 }
            onMain { assertFalse(player.isPlaying); player.start() }
            await("Resumed Android decoder must use the selected playback rate") {
                player.isPlaying && abs(player.playbackParams.speed - 1.5f) <= 0.001f
            }
            val resume = onMain { SystemClock.elapsedRealtime() to player.currentPosition }
            SystemClock.sleep(350)
            val end = onMain { SystemClock.elapsedRealtime() to player.currentPosition }
            assertTrue("Resume advances the requested WAV", end.second > resume.second)
            val expected = (end.first - resume.first) * 1.5
            assertTrue("Android playback rate follows 1.5X", abs(end.second - resume.second - expected) <= 220)
            assertNull(errors.get())
            return JSONObject().put("id", id).put("durationMs", duration).put("pausedPositionMs", paused)
                .put("seekTargetMs", target).put("rate", 1.5).put("pauseResumeSeekRatePassed", true)
        } finally { onMain { player.release() } }
    }

    private fun protectedFiles(): Map<String, String> = listOf(app.filesDir, File(app.applicationInfo.dataDir, "shared_prefs"))
        .flatMap { root -> root.walkTopDown().filter { it.isFile && it.extension in setOf("wav", "ogg", "xml") }.toList() }
        .associate { it.canonicalPath to sha256(it) }

    private fun join(parts: List<FloatArray>): FloatArray {
        val result = FloatArray(parts.sumOf { it.size }); var offset = 0
        parts.forEach { it.copyInto(result, offset); offset += it.size }
        return result
    }

    private fun rms(samples: FloatArray) = sqrt(samples.sumOf { it.toDouble() * it } / samples.size)

    private fun trimBoundarySilence(pcm: FloatArray, trimStart: Boolean, trimEnd: Boolean): FloatArray {
        // Preserve 25ms around the first/last audible sample and trim at most 250ms per edge.
        val keep = SAMPLE_RATE / 40; val maximum = SAMPLE_RATE / 4
        val first = pcm.indexOfFirst { abs(it) > 0.0003f }.coerceAtLeast(0)
        val last = pcm.indexOfLast { abs(it) > 0.0003f }.coerceAtLeast(first)
        val start = if (trimStart) (first - keep).coerceIn(0, maximum) else 0
        val end = if (trimEnd) (last + keep + 1).coerceIn((pcm.size - maximum).coerceAtLeast(start + 1), pcm.size) else pcm.size
        return pcm.copyOfRange(start, end)
    }

    private fun fade(pcm: FloatArray, fadeStart: Boolean, fadeEnd: Boolean) {
        val count = (SAMPLE_RATE / 200).coerceAtMost(pcm.size / 2)
        for (i in 0 until count) {
            val scale = i.toFloat() / count
            if (fadeStart) pcm[i] *= scale
            if (fadeEnd) pcm[pcm.lastIndex - i] *= scale
        }
    }

    private fun sha256(file: File): String = file.inputStream().use { stream ->
        val digest = MessageDigest.getInstance("SHA-256"); val buffer = ByteArray(65_536)
        while (true) { val count = stream.read(buffer); if (count < 0) break; digest.update(buffer, 0, count) }
        digest.digest().joinToString("") { "%02x".format(it.toInt() and 255) }
    }

    private fun <T> onMain(block: () -> T): T {
        val value = AtomicReference<T>(); instrumentation.runOnMainSync { value.set(block()) }; return value.get()
    }

    private fun await(label: String, condition: () -> Boolean) {
        val end = SystemClock.elapsedRealtime() + 10_000
        while (SystemClock.elapsedRealtime() < end) {
            if (onMain(condition)) return
            SystemClock.sleep(30)
        }
        fail(label)
    }

    companion object {
        private const val QA_PACKAGE = "com.learngraphicsprogramming.app.curricularqa"
        private const val SPANISH_VOICE = "ef_dora"
        private const val ENGLISH_VOICE = "af_heart"
        private const val SAMPLE_RATE = 24_000
    }
}
