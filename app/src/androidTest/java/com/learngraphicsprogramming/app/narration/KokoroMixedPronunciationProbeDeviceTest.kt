package com.learngraphicsprogramming.app.narration

import android.app.Activity
import android.app.Application
import android.app.KeyguardManager
import android.content.Context
import android.content.Intent
import android.media.AudioAttributes
import android.media.AudioFocusRequest
import android.media.AudioManager
import android.media.MediaPlayer
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.PowerManager
import android.os.SystemClock
import android.view.WindowManager
import androidx.lifecycle.Lifecycle
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.AppearanceSettingsRepository
import com.learngraphicsprogramming.app.BuildConfig
import com.learngraphicsprogramming.app.CoursesActivity
import com.learngraphicsprogramming.app.narration.phonemes.EnglishMisakiG2P
import com.learngraphicsprogramming.app.narration.phonemes.EnglishMisakiResources
import com.learngraphicsprogramming.app.narration.phonemes.MisakiPartOfSpeechTagger
import com.learngraphicsprogramming.app.narration.phonemes.SpanishFluidG2P
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.security.MessageDigest
import java.util.Locale
import java.util.UUID
import java.util.zip.ZipFile
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference
import kotlin.math.abs
import kotlin.math.sqrt
import org.json.JSONArray
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/**
 * Listening probes only. Mixed pronunciations use manually selected temporary overrides,
 * never language detection. The stress-placement trial is also test-only, not a production fix.
 * Fresh WAVs remain in a new QA export directory; production caches/resources are never written.
 */
@RunWith(AndroidJUnit4::class)
class KokoroMixedPronunciationProbeDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val preferences = linkedMapOf<String, Map<String, *>>()
    private var originalPreferenceFiles = emptySet<String>()
    private var preferencesCaptured = false
    private var guardRegistered = false
    private var scenario: ActivityScenario<CoursesActivity>? = null
    private var originalVolume: Int? = null
    private var originalMuted = false
    private lateinit var output: File
    private val document = JSONObject()
    private val sampleRecords = JSONArray()
    private val termRecords = JSONArray()
    private val screenGuard = object : Application.ActivityLifecycleCallbacks {
        override fun onActivityCreated(activity: Activity, state: Bundle?) {
            if (activity is CoursesActivity && activity.packageName == QA_PACKAGE)
                activity.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        }
        override fun onActivityStarted(activity: Activity) = Unit
        override fun onActivityResumed(activity: Activity) = Unit
        override fun onActivityPaused(activity: Activity) = Unit
        override fun onActivityStopped(activity: Activity) = Unit
        override fun onActivitySaveInstanceState(activity: Activity, state: Bundle) = Unit
        override fun onActivityDestroyed(activity: Activity) = Unit
    }

    @Before fun requireQaThenPreserveItsPreferencesAndAcquireAFocusedHost() {
        requireQa()
        requireAwakeUnlocked()
        originalPreferenceFiles = preferenceFiles()
        originalPreferenceFiles.forEach { name ->
            preferences[name] = context.getSharedPreferences(name, Context.MODE_PRIVATE).all
                .mapValues { (_, value) -> if (value is Set<*>) value.toSet() else value }
        }
        preferencesCaptured = true
        // CoursesActivity writes QA navigation preferences; suppress unrelated QA narration.
        assertTrue(context.getSharedPreferences("lesson_narration_settings", Context.MODE_PRIVATE)
            .edit().putBoolean("enabled", false).commit())
        (context.applicationContext as Application).registerActivityLifecycleCallbacks(screenGuard)
        guardRegistered = true
        scenario = ActivityScenario.launch<CoursesActivity>(Intent(context, CoursesActivity::class.java)
            .putExtra("routeId", AppearanceSettingsRepository.ROUTE_PROGRAMMING))
        awaitFocusedHost()
        val external = requireNotNull(context.getExternalFilesDir(null)).canonicalFile
        val exports = File(external, "pronunciation-probes").canonicalFile
        assertEquals(external, exports.parentFile)
        assertTrue(exports.isDirectory || exports.mkdirs())
        output = File(exports, "run-" + UUID.randomUUID()).canonicalFile
        assertEquals(exports, output.parentFile)
        assertFalse("Never overwrite an earlier probe", output.exists())
        assertTrue(output.mkdir())
        document.put("schema", 1).put("package", context.packageName)
            .put("voice", VOICE).put("rate", 1.0).put("sampleRate", 24_000)
            .put("runDirectory", output.absolutePath).put("status", "preparing-phonemes")
            .put("productionIntegration", false).put("naturalnessAccepted", false)
            .put("cacheWrites", false).put("terms", termRecords).put("samples", sampleRecords)
        persist()
    }

    @After fun restoreOnlyQaPreferencesAndThePlaybackVolume() {
        // After also runs if Before rejects the original package.
        if (context.packageName != QA_PACKAGE || BuildConfig.APPLICATION_ID != QA_PACKAGE ||
            context.applicationContext.packageName != QA_PACKAGE) return
        try {
            scenario?.close()
            scenario = null
            instrumentation.waitForIdleSync()
        } finally {
            try {
                if (guardRegistered) {
                    (context.applicationContext as Application).unregisterActivityLifecycleCallbacks(screenGuard)
                    guardRegistered = false
                }
                if (preferencesCaptured) restorePreferences()
            } finally {
                originalVolume?.let { volume ->
                    val audio = context.getSystemService(AudioManager::class.java)
                    audio.setStreamVolume(AudioManager.STREAM_MUSIC, volume, 0)
                    audio.adjustStreamVolume(AudioManager.STREAM_MUSIC,
                        if (originalMuted) AudioManager.ADJUST_MUTE else AudioManager.ADJUST_UNMUTE, 0)
                }
            }
        }
    }

    @Test fun nativeExplorationExportsSpanishAndManuallyMixedPronunciationSamples() {
        assertNotEquals("ONNX and dictionary work must stay off main", Looper.getMainLooper(), Looper.myLooper())
        try {
            val store = KokoroModelStore(context)
            val root = try { store.directory().canonicalFile } finally { store.close() }
            val resource = KokoroModelManifest.resources(NarrationLanguage.SPANISH).single()
            assertTrue(KokoroModelManifest.verify(File(root, resource.fileName), resource))
            val spanish = SpanishFluidG2P.loadLexicon(File(root, resource.fileName))
            val vocabulary = KokoroOnnxInputs.loadVocabulary(File(root, "tokenizer.json"))
            document.put("modelRevision", KokoroModelManifest.revision)
                .put("spanishLexiconSha256", resource.sha256).put("spanishLexiconContainsImprime", spanish.containsKey("imprime"))
                .put("englishRevision", EnglishMisakiResources.revision)
                .put("englishResources", JSONArray(ENGLISH_DICTIONARIES)).put("englishNeuralResources", false)
            persist()
            val tags = mapOf("include" to "VB", "i" to "PRP", "o" to "NN", "stream" to "NN",
                "main" to "NN", "return" to "VB", "double" to "JJ", "string" to "NN",
                "see" to "VB", "out" to "RP")
            val englishPhones = linkedMapOf<String, String>()
            EnglishMisakiG2P(openResource = { name ->
                require(name in ENGLISH_DICTIONARIES) { "Only the installed US gold/silver dictionaries may be opened" }
                val file = File(root, name).canonicalFile
                require(file.parentFile == root && file.isFile) { "Missing installed dictionary: $name" }
                file.inputStream()
            }, partOfSpeech = MisakiPartOfSpeechTagger { words ->
                words.map { tags.getValue(it.lowercase(Locale.ROOT)) }
            }, neuralDirectory = null).use { english ->
                // This finite list is the ONLY input sent to Misaki; never a Spanish sentence.
                for (term in listOf("include", "I O stream", "main", "return", "double", "string", "see out")) {
                    val phones = english.phonemize(term, british = false)
                    KokoroPhonemeChunks.split(phones).forEach { KokoroOnnxInputs.tokenize(it, vocabulary) }
                    englishPhones[term] = phones
                    termRecords.put(JSONObject().put("term", term).put("language", "en-US").put("phonemes", phones)
                        .put("provenance", "verified Misaki US gold/silver; explicit finite POS tags; no neural fallback"))
                    persist()
                }
            }
            val imprimePhones = SpanishFluidG2P.phonemize("imprime", spanish)
            termRecords.put(JSONObject().put("term", "imprime").put("language", "es").put("phonemes", imprimePhones)
                .put("provenance", "production SpanishFluidG2P with verified Spanish dictionary"))
            persist()
            assertEquals("Spanish imprime keeps its stressed /i/", "impɾˈime", imprimePhones)
            assertFalse(imprimePhones.contains("aɪ"))
            assertFalse("Uppercase I is an English diphthong token", imprimePhones.contains('I'))

            // The QA application cannot discover the original package through visibility
            // queries. Read its public installed APK using the exact path obtained from adb
            // pm path; do not add cross-package queries or touch its private application data.
            val originalApk = File(requireNotNull(InstrumentationRegistry.getArguments()
                .getString("originalApkPath")) { "Supply the original installed base APK path" }).canonicalFile
            require(originalApk.path.startsWith("/data/app/") && originalApk.name == "base.apk" && originalApk.isFile)
            @Suppress("DEPRECATION")
            val archive = context.packageManager.getPackageArchiveInfo(originalApk.path, 0)
            assertEquals("The public archive belongs to the reported application", ORIGINAL_PACKAGE, archive?.packageName)
            fun readInstalledLesson(): ByteArray = ZipFile(originalApk).use { zip ->
                zip.getInputStream(requireNotNull(zip.getEntry("assets/" + LESSON_ASSET))).use { it.readBytes() }
            }
            val installedBytes = readInstalledLesson()
            val installedLesson = JSONObject(installedBytes.toString(Charsets.UTF_8))
            assertEquals(LESSON_ID, installedLesson.getString("id"))
            val candidates = NarrationSegments.sentences(LessonNarrationText.page(installedLesson, 0, NarrationLanguage.SPANISH))
                .filter { Regex("\\bimprime\\b", RegexOption.IGNORE_CASE).containsMatchIn(it) }
            document.put("originalAsset", JSONObject().put("package", ORIGINAL_PACKAGE).put("path", LESSON_ASSET)
                .put("lessonId", LESSON_ID).put("page", 1).put("sha256", digest(installedBytes))
                .put("imprimeSentences", JSONArray(candidates)))
            persist()
            assertTrue("Reported installed U1L2 page1 actually contains imprime", candidates.isNotEmpty())
            val installedSentence = candidates.first()
            val currentBytes = context.assets.open(LESSON_ASSET).use { it.readBytes() }
            val currentLesson = JSONObject(currentBytes.toString(Charsets.UTF_8))
            assertEquals(LESSON_ID, currentLesson.getString("id"))
            val block = currentLesson.getJSONArray("pages").getJSONObject(0).getJSONArray("blocks").getJSONObject(3)
            assertEquals("paragraph", block.getString("type"))
            val currentRaw = block.getString("text")
            val includeRaw = NarrationSegments.sentences(listOf(currentRaw)).single { it.contains("#include <iostream>") }
            document.put("currentAsset", JSONObject().put("path", LESSON_ASSET).put("lessonId", LESSON_ID)
                .put("page", 1).put("block", 4).put("sha256", digest(currentBytes)).put("rawBlock", currentRaw))
            document.put("originalCacheComparison", JSONObject().put("raw", installedSentence).put("language", "es")
                .put("voice", VOICE).put("cacheKey", KokoroNarrationCache.key("es", VOICE, installedSentence))
                .put("cacheReadOrWritePerformed", false))
            persist()

            val samples = mutableListOf<Sample>()
            fun add(id: String, raw: String, overrides: Map<String, String> = emptyMap(),
                provenance: String = "explicit listening sample") {
                require("imprime" !in overrides && "vector" !in overrides)
                val spoken = SpanishTechnicalSpeech.prepare(raw).text
                overrides.keys.forEach { term ->
                    require(Regex("\\b" + Regex.escape(term) + "\\b").containsMatchIn(spoken)) {
                        "Manual override must appear in this prepared sample: $term"
                    }
                }
                val phones = SpanishFluidG2P.phonemize(raw, spanish + overrides)
                val parts = KokoroPhonemeChunks.split(phones)
                assertEquals(phones, parts.joinToString(""))
                parts.forEach { KokoroOnnxInputs.tokenize(it, vocabulary) }
                if (Regex("\\bimprime\\b", RegexOption.IGNORE_CASE).containsMatchIn(spoken))
                    assertTrue("imprime stays Spanish in $id", phones.contains(imprimePhones))
                val metadata = JSONObject().put("id", id).put("raw", raw).put("spoken", spoken).put("phonemes", phones)
                    .put("variant", if (overrides.isEmpty()) "production-spanish-baseline" else "manual-mixed-pilot")
                    .put("cacheKey", KokoroNarrationCache.key("es", VOICE, raw))
                    .put("cacheKeyPurpose", "baseline comparison only; pilot never stored under this key")
                    .put("manualOverrides", JSONObject(overrides)).put("provenance", provenance)
                    .put("phonemeChunks", JSONArray(parts)).put("status", "phonemes-ready")
                sampleRecords.put(metadata)
                samples += Sample(id, raw, phones, metadata)
                persist()
            }
            add("01-installed-imprime", installedSentence, provenance = "exact first matching sentence from original installed U1L2 page1")
            add("02-imprime-result", "El programa imprime el resultado.")
            add("03-imprime-isolated", "imprime")
            add("04-include-baseline", includeRaw, provenance = "exact current U1L2 p1b4 sentence; source unchanged")
            add("05-include-pilot", includeRaw, mapOf("include" to englishPhones.getValue("include"),
                "iostream" to englishPhones.getValue("I O stream")), "same p1b4 raw sentence; two manually selected token overrides")
            val coutRaw = "El programa imprime mediante std::cout."
            add("06-cout-baseline", coutRaw)
            add("07-cout-pilot", coutRaw, mapOf("cout" to englishPhones.getValue("see out")),
                "same raw sentence; manually selected cout only; imprime remains Spanish")
            val tick = 96.toChar()
            add("08-imprime-function", "La función " + tick + "imprime()" + tick + " muestra el resultado.")
            add("09-vector-spanish-control", "Un vector contiene valores.")
            val keywordRaw = "main y return son nombres del código; double y string también."
            add("10-keywords-baseline", keywordRaw)
            add("11-keywords-pilot", keywordRaw, listOf("main", "return", "double", "string")
                .associateWith { englishPhones.getValue(it) }, "same raw sentence; four explicitly selected code terms only")
            add("12-identifier-spanish-control", "La función " + tick + "imprime_total()" + tick + " conserva su nombre.")
            val baseline = samples.first()
            assertEquals("One stress-placement substitution only", 1, Regex(Regex.escape(imprimePhones)).findAll(baseline.phonemes).count())
            val manualPhones = baseline.phonemes.replace(imprimePhones, "imˈpɾime")
            assertEquals(baseline.phonemes.filterNot { it == 'ˈ' }, manualPhones.filterNot { it == 'ˈ' })
            KokoroPhonemeChunks.split(manualPhones).forEach { KokoroOnnxInputs.tokenize(it, vocabulary) }
            val stressMetadata = JSONObject().put("id", "13-installed-manual-stress").put("raw", baseline.raw)
                .put("spoken", baseline.metadata.getString("spoken")).put("phonemes", manualPhones)
                .put("variant", "manual-stress-placement-only").put("sourceSampleId", baseline.id)
                .put("sourcePhonemes", baseline.phonemes).put("cacheKey", baseline.metadata.getString("cacheKey"))
                .put("cacheKeyPurpose", "baseline comparison only; manual phones never stored under this key")
                .put("provenance", "test-only equivalent stress moved from vowel to syllable onset: impɾˈime -> imˈpɾime; NOT a production fix")
                .put("status", "phonemes-ready")
            samples += Sample("13-installed-manual-stress", baseline.raw, manualPhones, stressMetadata)
            sampleRecords.put(stressMetadata)
            assertEquals(13, samples.size)
            assertEquals(samples[3].raw, samples[4].raw)
            assertEquals(samples[5].raw, samples[6].raw)
            assertEquals(samples[9].raw, samples[10].raw)
            addPostG2pSample(samples, samples[3], "14-include-post-g2p", listOf(
                Injection("include", "include", "inklˈuðe", "ɪnklˈud"),
                Injection("iostream", "I O stream", "jˌostɾeˈam", "ˌI ˈO stɹˈim")
            ), englishPhones, vocabulary)
            addPostG2pSample(samples, samples[9], "15-keywords-post-g2p", listOf(
                Injection("main", "main", "mˈIn", "mˈAn"),
                Injection("return", "return", "rˈetuɾn", "ɹətˈɜɹn"),
                Injection("double", "double", "dˈoʊβle", "dˈʌbᵊl"),
                Injection("string", "string", "stɾˈiŋ", "stɹˈɪŋ")
            ), englishPhones, vocabulary)
            assertEquals(15, samples.size)
            val arguments = InstrumentationRegistry.getArguments()
            val sampleArgument = arguments.getString("probeSampleIds")
            val playbackArgument = arguments.getString("probePlaybackIds")
            val selected = parseSampleIds("probeSampleIds", sampleArgument, samples)
                ?: samples.take(13)
            val defaultPlayback = listOf("01-installed-imprime", "13-installed-manual-stress", "05-include-pilot")
                .mapNotNull { id -> selected.singleOrNull { it.id == id } }
            val postG2pPair = listOf("14-include-post-g2p", "15-keywords-post-g2p")
            val playback = parseSampleIds("probePlaybackIds", playbackArgument, samples)
                ?: if (selected.map { it.id }.toSet() == postG2pPair.toSet())
                    postG2pPair.map { id -> selected.single { it.id == id } }
                else defaultPlayback
            require(playback.all { sample -> selected.any { it.id == sample.id } }) {
                "probePlaybackIds must be a subset of the synthesized probeSampleIds"
            }
            document.put("selection", JSONObject()
                .put("probeSampleIds", sampleArgument ?: JSONObject.NULL)
                .put("probePlaybackIds", playbackArgument ?: JSONObject.NULL)
                .put("synthesisIds", JSONArray(selected.map { it.id }))
                .put("playbackIds", JSONArray(playback.map { it.id }))
                .put("defaultSynthesis", "original thirteen samples")
                .put("defaultPlayback", "01,13,05 when selected; selecting only 14,15 plays that pair"))
            samples.forEach { sample ->
                val synthesized = selected.any { it.id == sample.id }
                sample.metadata.put("synthesisSelected", synthesized)
                    .put("playbackSelected", playback.any { it.id == sample.id })
                if (!synthesized) sample.metadata.put("status", "not-selected")
            }
            document.put("status", "phonemes-ready-before-inference")
            persist()

            val lease = NarrationWorkPriority.beginInteractive()
            try {
                // An owned CPU ONNX instance: no TTS, provider fallback or production cache.
                KokoroOnnxEngine(root, preferAccelerator = false, cpuThreads = 2).use { engine ->
                    selected.forEach { sample ->
                        awaitFocusedHost()
                        sample.metadata.put("status", "synthesizing")
                        persist()
                        val chunks = KokoroPhonemeChunks.split(sample.phonemes).map { engine.synthesizePhonemes(it, VOICE, 1f) }
                        val count = chunks.sumOf { it.size.toLong() }
                        assertTrue("Native waveform has nontrivial duration", count >= 2_400)
                        assertTrue("PCM is finite", chunks.all { part -> part.all { it.isFinite() } })
                        val rms = sqrt(chunks.sumOf { part -> part.sumOf { it.toDouble() * it } } / count)
                        val peak = chunks.maxOf { part -> part.maxOf { abs(it) } }
                        assertTrue("Native waveform is not silent: " + sample.id, rms > 0.0001 && peak > 0.001f)
                        val wav = File(output, sample.id + ".wav").canonicalFile
                        assertEquals(output, wav.parentFile)
                        assertFalse("Never overwrite a recording", wav.exists())
                        NarrationWavWriter.writeChunks(wav, chunks)
                        assertWav(wav, count)
                        sample.metadata.put("status", "wav-ready").put("wav", wav.name)
                            .put("wavSha256", digest(wav.readBytes())).put("samples", count)
                            .put("durationSeconds", count / 24_000.0).put("rms", rms).put("peak", peak.toDouble())
                        persist()
                    }
                }
            } finally { lease.close() }
            assertEquals("Original installed source bytes stay intact", digest(installedBytes),
                digest(readInstalledLesson()))
            assertEquals("QA source bytes stay intact", digest(currentBytes), digest(context.assets.open(LESSON_ASSET).use { it.readBytes() }))
            document.put("status", "wav-ready-before-playback")
            persist()
            if (playback.isNotEmpty()) playSamples(playback)
            document.put("status", "exported-and-played").put("naturalnessAccepted", false)
            persist()
        } catch (error: Throwable) {
            document.put("status", "failed").put("failure", error.toString())
            if (::output.isInitialized) persist()
            throw error
        }
    }

    private data class Sample(val id: String, val raw: String, val phonemes: String, val metadata: JSONObject)

    private data class Injection(
        val term: String, val englishTerm: String, val baselinePhones: String, val englishPhones: String
    )

    private fun addPostG2pSample(
        samples: MutableList<Sample>, source: Sample, id: String, injections: List<Injection>,
        englishPhones: Map<String, String>, vocabulary: Map<Char, Long>
    ) {
        // Finite, named spans of these two known samples only: no language/token detector.
        assertEquals("production-spanish-baseline", source.metadata.getString("variant"))
        val spans = injections.map { injection ->
            assertEquals("The manually named source term occurs once: " + injection.term, 1,
                Regex("\\b" + Regex.escape(injection.term) + "\\b")
                    .findAll(source.metadata.getString("spoken")).count())
            assertEquals("Pinned English dictionary phones: " + injection.englishTerm,
                injection.englishPhones, englishPhones.getValue(injection.englishTerm))
            val matches = Regex(Regex.escape(injection.baselinePhones)).findAll(source.phonemes).toList()
            assertEquals("Known Spanish phone span occurs exactly once: " + injection.term, 1, matches.size)
            injection to matches.single().range
        }.sortedBy { it.second.first }
        spans.zipWithNext().forEach { (left, right) ->
            require(left.second.last < right.second.first) { "Manual phone spans must not overlap" }
        }
        val records = JSONArray()
        val inverse = mutableListOf<Triple<Int, Int, String>>()
        var cursor = 0
        val phones = buildString {
            spans.forEach { (injection, range) ->
                append(source.phonemes.substring(cursor, range.first))
                val start = length
                append(englishPhones.getValue(injection.englishTerm))
                val end = length
                assertEquals("English phones are inserted intact, after Spanish G2P",
                    injection.englishPhones, substring(start, end))
                inverse += Triple(start, end, injection.baselinePhones)
                records.put(JSONObject().put("term", injection.term).put("englishTerm", injection.englishTerm)
                    .put("sourceStart", range.first).put("sourceEndExclusive", range.last + 1)
                    .put("baselinePhones", injection.baselinePhones).put("englishPhones", injection.englishPhones)
                    .put("injectedStart", start).put("injectedEndExclusive", end))
                cursor = range.last + 1
            }
            append(source.phonemes.substring(cursor))
        }
        var reconstructed = phones
        inverse.asReversed().forEach { (start, end, baselinePhones) ->
            reconstructed = reconstructed.replaceRange(start, end, baselinePhones)
        }
        assertEquals("All source phones outside the named spans stay exact", source.phonemes, reconstructed)
        inverse.forEachIndexed { index, (start, end, _) ->
            assertEquals(spans[index].first.englishPhones, phones.substring(start, end))
        }
        val parts = KokoroPhonemeChunks.split(phones)
        assertEquals("Chunking keeps exact injected phones", phones, parts.joinToString(""))
        parts.forEach { KokoroOnnxInputs.tokenize(it, vocabulary) }
        val metadata = JSONObject().put("id", id).put("raw", source.raw)
            .put("spoken", source.metadata.getString("spoken")).put("phonemes", phones)
            .put("variant", "manual-post-g2p-english-injection").put("sourceSampleId", source.id)
            .put("sourcePhonemes", source.phonemes).put("injections", records)
            .put("cacheKey", source.metadata.getString("cacheKey"))
            .put("cacheKeyPurpose", "baseline comparison only; injected phones never stored under this key")
            .put("provenance", "test-only finite named spans; English dictionary phones inserted AFTER complete Spanish G2P; no subsequent phonemization, production detector or integration")
            .put("phonemeChunks", JSONArray(parts)).put("status", "phonemes-ready")
        samples += Sample(id, source.raw, phones, metadata)
        sampleRecords.put(metadata)
        persist()
    }

    private fun parseSampleIds(name: String, argument: String?, samples: List<Sample>): List<Sample>? {
        if (argument == null) return null
        require(argument.isNotBlank()) { "$name must name at least one sample" }
        val byId = samples.associateBy { it.id }
        val byNumber = samples.associateBy { it.id.substringBefore('-') }
        require(byId.size == samples.size && byNumber.size == samples.size)
        val selected = argument.split(',').map { requested ->
            val id = requested.trim()
            require(id.isNotEmpty()) { "Empty sample in $name" }
            byId[id] ?: byNumber[id] ?: error("Unknown sample in $name: $id")
        }
        require(selected.map { it.id }.distinct().size == selected.size) { "Duplicate sample in $name" }
        return selected
    }

    private fun playSamples(samples: List<Sample>) {
        awaitFocusedHost()
        val audio = context.getSystemService(AudioManager::class.java)
        originalVolume = audio.getStreamVolume(AudioManager.STREAM_MUSIC)
        originalMuted = audio.isStreamMute(AudioManager.STREAM_MUSIC)
        audio.adjustStreamVolume(AudioManager.STREAM_MUSIC, AudioManager.ADJUST_UNMUTE, 0)
        audio.setStreamVolume(AudioManager.STREAM_MUSIC, maxOf(1, audio.getStreamMaxVolume(AudioManager.STREAM_MUSIC) / 3), 0)
        val attributes = AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA)
            .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH).build()
        val focusFailure = AtomicReference<String?>()
        val listener = AudioManager.OnAudioFocusChangeListener { change ->
            if (change < 0) focusFailure.compareAndSet(null, "Audio focus changed: $change")
        }
        val request = if (Build.VERSION.SDK_INT >= 26) AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN_TRANSIENT)
            .setAudioAttributes(attributes).setAcceptsDelayedFocusGain(false)
            .setOnAudioFocusChangeListener(listener, Handler(Looper.getMainLooper())).build() else null
        @Suppress("DEPRECATION")
        val granted = if (request != null) audio.requestAudioFocus(request)
            else audio.requestAudioFocus(listener, AudioManager.STREAM_MUSIC, AudioManager.AUDIOFOCUS_GAIN_TRANSIENT)
        assertEquals("QA host must obtain actual audio focus", AudioManager.AUDIOFOCUS_REQUEST_GRANTED, granted)
        try {
            samples.forEach { sample ->
                awaitFocusedHost()
                val completed = CountDownLatch(1)
                val failure = AtomicReference<String?>()
                var player: MediaPlayer? = null
                try {
                    instrumentation.runOnMainSync {
                        player = MediaPlayer().apply {
                            setAudioAttributes(attributes)
                            setDataSource(File(output, sample.id + ".wav").absolutePath)
                            setOnErrorListener { _, what, extra ->
                                failure.set("MediaPlayer error $what/$extra"); completed.countDown(); true
                            }
                            setOnCompletionListener { media ->
                                sample.metadata.put("playbackDurationMs", media.duration).put("playbackPositionMs", media.currentPosition)
                                completed.countDown()
                            }
                            prepare()
                            assertTrue(duration > 0)
                            start()
                            assertTrue("Local MediaPlayer starts", isPlaying)
                        }
                    }
                    val deadline = SystemClock.uptimeMillis() + 60_000
                    while (!completed.await(100, TimeUnit.MILLISECONDS) && SystemClock.uptimeMillis() < deadline) {
                        requireAwakeUnlocked()
                        assertNull(focusFailure.get())
                        assertNull(failure.get())
                    }
                    assertEquals("Local sample completes playback", 0L, completed.count)
                    assertNull(failure.get())
                    assertNull(focusFailure.get())
                    assertTrue(sample.metadata.getInt("playbackDurationMs") > 0)
                    sample.metadata.put("played", true)
                    persist()
                } finally { instrumentation.runOnMainSync { player?.release() } }
            }
        } finally {
            @Suppress("DEPRECATION")
            if (request != null) audio.abandonAudioFocusRequest(request) else audio.abandonAudioFocus(listener)
        }
    }

    private fun assertWav(file: File, expectedSamples: Long) {
        val bytes = file.readBytes()
        assertEquals(44L + expectedSamples * 2, bytes.size.toLong())
        assertEquals("RIFF", String(bytes, 0, 4, Charsets.US_ASCII))
        assertEquals("WAVE", String(bytes, 8, 4, Charsets.US_ASCII))
        val buffer = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        assertEquals(1, buffer.getShort(20).toInt())
        assertEquals(1, buffer.getShort(22).toInt())
        assertEquals(24_000, buffer.getInt(24))
        assertEquals(16, buffer.getShort(34).toInt())
        assertEquals(expectedSamples * 2, buffer.getInt(40).toLong())
        var energy = 0.0
        for (index in 44 until bytes.size step 2) {
            val value = buffer.getShort(index).toDouble() / 32_768.0
            energy += value * value
        }
        assertTrue("Exported PCM16 WAV is not silent", sqrt(energy / expectedSamples) > 0.0001)
    }

    private fun awaitFocusedHost() {
        requireQa()
        val host = requireNotNull(scenario)
        val deadline = SystemClock.uptimeMillis() + 10_000
        do {
            requireAwakeUnlocked()
            var focused = false
            if (host.state == Lifecycle.State.RESUMED) host.onActivity { activity ->
                assertEquals(QA_PACKAGE, activity.packageName)
                assertEquals(AppearanceSettingsRepository.ROUTE_PROGRAMMING, activity.intent.getStringExtra("routeId"))
                val decor = activity.window.decorView
                focused = activity.hasWindowFocus() && decor.isAttachedToWindow && decor.windowToken != null && decor.isShown
            }
            if (focused) return
            SystemClock.sleep(50)
        } while (SystemClock.uptimeMillis() < deadline)
        fail("Existing QA CoursesActivity must acquire a resumed focused window without relaunch or security changes")
    }

    private fun requireQa() {
        assertEquals(QA_PACKAGE, context.packageName)
        assertEquals(QA_PACKAGE, BuildConfig.APPLICATION_ID)
        assertEquals(QA_PACKAGE, context.applicationContext.packageName)
    }
    private fun requireAwakeUnlocked() {
        assertTrue("Wake display before probe", context.getSystemService(PowerManager::class.java).isInteractive)
        val keyguard = context.getSystemService(KeyguardManager::class.java)
        assertFalse("Probe never dismisses visual keyguard", keyguard.isKeyguardLocked)
        assertFalse("Probe never unlocks credentials", keyguard.isDeviceLocked)
    }
    private fun preferenceFiles() = File(context.applicationInfo.dataDir, "shared_prefs").listFiles().orEmpty()
        .filter { it.name.endsWith(".xml") }.map { it.name.removeSuffix(".xml") }.toSet()
    private fun restorePreferences() {
        requireQa()
        (preferenceFiles() + preferences.keys).forEach { name ->
            if (name !in originalPreferenceFiles) {
                assertTrue("Remove only preferences created by this QA probe: $name", context.deleteSharedPreferences(name))
            } else {
                val prefs = context.getSharedPreferences(name, Context.MODE_PRIVATE)
                val snapshot = preferences.getValue(name)
                if (prefs.all != snapshot) {
                    val editor = prefs.edit().clear()
                    snapshot.forEach { (key, value) ->
                        when (value) {
                            is String -> editor.putString(key, value)
                            is Int -> editor.putInt(key, value)
                            is Long -> editor.putLong(key, value)
                            is Float -> editor.putFloat(key, value)
                            is Boolean -> editor.putBoolean(key, value)
                            is Set<*> -> editor.putStringSet(key, value.filterIsInstance<String>().toSet())
                            else -> error("Unsupported QA preference $name/$key")
                        }
                    }
                    assertTrue(editor.commit())
                    assertEquals(snapshot, prefs.all)
                }
            }
        }
    }
    private fun persist() {
        requireQa()
        val target = File(output, "samples.json").canonicalFile
        assertEquals(output, target.parentFile)
        target.writeText(document.toString(2) + "\n", Charsets.UTF_8)
    }
    private fun digest(bytes: ByteArray) = MessageDigest.getInstance("SHA-256").digest(bytes)
        .joinToString("") { "%02x".format(it.toInt() and 0xff) }

    companion object {
        private const val QA_PACKAGE = "com.learngraphicsprogramming.app.curricularqa"
        private const val ORIGINAL_PACKAGE = "com.learngraphicsprogramming.app"
        private const val VOICE = "ef_dora"
        private const val LESSON_ID = "cpp_fundamentals__programas_y_ejecucion__primer_programa_y_salida"
        private const val LESSON_ASSET = "content/es/programming/cpp_fundamentals/programas_y_ejecucion/lesson_primer_programa_y_salida.json"
        private val ENGLISH_DICTIONARIES = listOf("us_gold.json", "us_silver.json")
    }
}
