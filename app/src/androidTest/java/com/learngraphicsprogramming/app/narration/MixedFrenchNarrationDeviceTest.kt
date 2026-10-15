package com.learngraphicsprogramming.app.narration

import android.content.Context
import android.content.ContextWrapper
import android.os.Build
import android.os.Looper
import android.os.SystemClock
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.BuildConfig
import com.learngraphicsprogramming.app.narration.phonemes.FrenchFluidG2P
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.security.MessageDigest
import java.util.UUID
import kotlin.math.abs
import kotlin.math.sqrt
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

/** Real continuous French Kokoro utterances; production audio, preferences and curriculum are untouched. */
@RunWith(AndroidJUnit4::class)
class MixedFrenchNarrationDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val app = instrumentation.targetContext

    @Test fun continuousFrenchVoiceUsesOnlyReviewedEnglishTermsAndKeepsCompatibleCacheEntries() {
        assertEquals("Never run against the ordinary or Work application", QA_PACKAGE, app.packageName)
        assertEquals(QA_PACKAGE, BuildConfig.APPLICATION_ID)
        assertTrue("This acceptance is specifically for the dedicated LDPlayer emulator",
            Build.SUPPORTED_ABIS.any { it == "x86" || it == "x86_64" } &&
                (Build.MODEL.contains("V2307", true) || Build.FINGERPRINT.contains("generic", true) ||
                    Build.FINGERPRINT.contains("emulator", true)))
        assertNotEquals(Looper.getMainLooper(), Looper.myLooper())
        val external = requireNotNull(app.getExternalFilesDir(null)).canonicalFile
        val root = InstrumentationRegistry.getArguments().getString("modelRoot")?.let(::File)?.canonicalFile
            ?: File(external, "kokoro/${KokoroModelManifest.revision}").canonicalFile
        assertTrue("Read only QA-owned installed resources",
            root.path.startsWith(external.path + File.separator) || root.path.startsWith(app.filesDir.canonicalPath + File.separator))
        val artifacts = KokoroModelManifest.sharedArtifacts + KokoroModelManifest.resources(NarrationLanguage.FRENCH) +
            requireNotNull(KokoroVoiceCatalog.find(VOICE)).artifact
        val before = artifacts.associate { artifact ->
            val file = File(root, artifact.fileName)
            assertTrue("An existing verified resource is required; this test never downloads: ${artifact.fileName}",
                KokoroModelManifest.verify(file, artifact))
            artifact.fileName to fingerprint(file)
        }
        val exportRoot = File(external, "french-manual-qa").apply { check(isDirectory || mkdir()) }.canonicalFile
        val output = File(exportRoot, "run-${UUID.randomUUID()}").apply { check(mkdir()) }.canonicalFile
        assertEquals(exportRoot, output.parentFile)
        val sandbox = File(app.cacheDir, "french-manual-cache-${UUID.randomUUID()}").apply { check(mkdir()) }.canonicalFile
        assertEquals(app.cacheDir.canonicalFile, sandbox.parentFile)
        val isolated = object : ContextWrapper(app) {
            override fun getApplicationContext(): Context = this
            override fun getFilesDir(): File = sandbox
        }
        val samples = JSONArray()
        val manifest = JSONObject().put("package", app.packageName).put("device", Build.MODEL)
            .put("sdk", Build.VERSION.SDK_INT).put("abis", JSONArray(Build.SUPPORTED_ABIS.toList()))
            .put("scope", "LDPlayer native Android with its ARM translation; human listening is a separate acceptance")
            .put("voice", VOICE).put("sampleRate", 24_000).put("modelRevision", KokoroModelManifest.revision)
            .put("modelRoot", root.absolutePath).put("outputDirectory", output.absolutePath)
            .put("sandboxCache", sandbox.absolutePath).put("naturalnessAccepted", false)
            .put("samples", samples)
        fun persist() = File(output, "samples.json").writeText(manifest.toString(2))
        persist()
        val frontend = FrenchFluidG2P(root, File(root, "tokenizer.json"))
        val vocabulary = KokoroOnnxInputs.loadVocabulary(File(root, "tokenizer.json"))
        val owner = Any()
        val rendered = linkedMapOf<Sentence, Pair<String, File>>()
        try {
            val alreadyCompatible = JSONArray()
            for ((term, source) in listOf("string" to "Le type string.", "break" to "L’instruction break.",
                "set" to "Le conteneur set.", "flush" to "La fonction flush.")) {
                val prepared = FrenchTechnicalSpeech.prepareForKokoro(source)
                val previous = frontend.phonemize(source)
                assertTrue("The pinned French dictionary already has the reviewed US phones: $term",
                    previous.contains(ManualEnglishPronunciations.phones.getValue(term)))
                assertTrue("Already compatible prose must not request replacement recordings", ManualEnglishSpeech.spans(prepared).isEmpty())
                assertEquals(previous, MixedFrenchG2P.phonemizePrepared(prepared, frontend))
                assertEquals(legacyKey("fr", VOICE, source), KokoroNarrationCache.key("fr", VOICE, prepared))
                alreadyCompatible.put(term)
            }
            manifest.put("alreadyCompatibleUsPhonesAndKeys", alreadyCompatible)
            for (sentence in SENTENCES) {
                val prepared = FrenchTechnicalSpeech.prepareForKokoro(sentence.text)
                val selected = ManualEnglishSpeech.spans(prepared)
                assertEquals("Only the explicitly reviewed contextual terms are selected", sentence.terms,
                    selected.map { it.entry.id })
                assertEquals("Annotations never replace or duplicate the original authored sentence",
                    sentence.text, ManualEnglishSpeech.strip(prepared))
                val phones = MixedFrenchG2P.phonemizePrepared(prepared, frontend)
                if (sentence.terms.isEmpty()) {
                    assertEquals("Ordinary French, including ambiguous words, keeps the exact existing phonemes",
                        frontend.phonemize(sentence.text), phones)
                    assertEquals("An unchanged French pronunciation retains its legacy cache address",
                        legacyKey("fr", VOICE, sentence.text), KokoroNarrationCache.key("fr", VOICE, prepared))
                } else {
                    selected.forEach { term ->
                        assertTrue("The reviewed Misaki phones are included byte-for-byte for ${term.entry.id}",
                            phones.contains(ManualEnglishPronunciations.phonemes(term.entry)))
                    }
                    assertNotEquals("Changed technical pronunciation cannot reuse incompatible prior audio",
                        legacyKey("fr", VOICE, sentence.text), KokoroNarrationCache.key("fr", VOICE, prepared))
                }
                val chunks = KokoroPhonemeChunks.split(phones)
                assertEquals("Each complete sample is one continuous model invocation, without voice splicing", 1, chunks.size)
                KokoroOnnxInputs.tokenize(phones, vocabulary)
                var calls = 0
                val started = SystemClock.elapsedRealtime()
                val pcm = KokoroEngineCoordinator.withEngine(owner, root, { false }, foreground = false) { engine ->
                    calls++
                    engine.synthesizePhonemes(phones, VOICE, 1f)
                }
                assertEquals("One complete utterance is synthesized once", 1, calls)
                assertTrue("Actual model PCM is finite and nonempty", pcm.size >= 2_400 && pcm.all { it.isFinite() })
                val rms = sqrt(pcm.sumOf { it.toDouble() * it } / pcm.size)
                val peak = pcm.maxOf { abs(it) }.toDouble()
                assertTrue("Actual French audio is not silence", rms > .0001 && peak > .001)
                val clipped = pcm.count { abs(it) >= 1f }.toDouble() / pcm.size
                assertTrue("The full sentence does not suffer pervasive clipping", clipped < .001)
                val wav = File(output, "${sentence.id}.wav")
                NarrationWavWriter.write(wav, pcm)
                verifyWave(wav, pcm.size)
                rendered[sentence] = prepared to wav
                samples.put(JSONObject().put("id", sentence.id).put("source", sentence.text)
                    .put("selectedTerms", JSONArray(sentence.terms)).put("phonemes", phones)
                    .put("voice", VOICE).put("inferenceCalls", calls)
                    .put("inferenceMillis", SystemClock.elapsedRealtime() - started)
                    .put("sampleCount", pcm.size).put("durationSeconds", pcm.size / 24_000.0)
                    .put("rms", rms).put("peak", peak).put("clippedFraction", clipped)
                    .put("wav", wav.name).put("wavSha256", sha256(wav))
                    .put("cacheKey", KokoroNarrationCache.key("fr", VOICE, prepared)))
                persist()
            }

            val ordinary = SENTENCES.first { it.terms.isEmpty() }
            val ordinaryAudio = requireNotNull(rendered[ordinary]).second
            val oldNormal = requireNotNull(KokoroNarrationCache.store(isolated, "fr", VOICE, ordinary.text, ordinaryAudio))
            val normalHash = sha256(oldNormal)
            val technical = SENTENCES.first()
            val (prepared, mixedAudio) = requireNotNull(rendered[technical])
            val cacheRoot = File(sandbox, "kokoro-narration-cache")
            val oldTechnical = File(cacheRoot, "${legacyKey("fr", VOICE, technical.text)}.wav")
            assertFalse(oldTechnical.exists())
            ordinaryAudio.copyTo(oldTechnical)
            val technicalSentinelHash = sha256(oldTechnical)
            val unrelated = File(cacheRoot, "unrelated-existing-file.txt").apply { writeText("Preserve existing independent QA cache files.") }
            val unrelatedHash = sha256(unrelated)
            val newTechnical = requireNotNull(KokoroNarrationCache.store(isolated, "fr", VOICE, prepared, mixedAudio))
            assertNotEquals(oldTechnical, newTechnical)
            assertEquals(oldTechnical, KokoroNarrationCache.lookup(isolated, "fr", VOICE, technical.text))
            assertEquals(newTechnical, KokoroNarrationCache.lookup(isolated, "fr", VOICE, prepared))
            assertEquals(oldNormal, KokoroNarrationCache.lookup(isolated, "fr", VOICE,
                requireNotNull(rendered[ordinary]).first))
            KokoroNarrationCache.prune(isolated)
            assertEquals("Compatible normal French audio is unchanged", normalHash, sha256(oldNormal))
            assertEquals("An incompatible legacy key is preserved independently, never deleted", technicalSentinelHash, sha256(oldTechnical))
            assertEquals("Unrelated cache files remain untouched", unrelatedHash, sha256(unrelated))
            assertEquals(sha256(mixedAudio), sha256(newTechnical))
            val spanish = "Las fracciones representan la misma cantidad."
            assertEquals("The French extension preserves unchanged Spanish cache identities",
                legacyKey("es", "ef_dora", spanish), KokoroNarrationCache.key("es", "ef_dora", spanish))
            val english = "Equivalent fractions represent the same quantity."
            assertEquals("The French extension preserves English cache identities",
                legacyKey("en-US", "af_heart", english), KokoroNarrationCache.key("en-US", "af_heart", english))
            manifest.put("status", "native-synthesis-and-isolated-cache-verified-listening-pending")
                .put("cacheVerification", JSONObject().put("unchangedFrenchKey", oldNormal.nameWithoutExtension)
                    .put("legacyTechnicalKey", oldTechnical.nameWithoutExtension)
                    .put("newTechnicalKey", newTechnical.nameWithoutExtension)
                    .put("legacyTechnicalSentinel", "Valid synthetic cache sentinel, not a historical pronunciation recording")
                    .put("normalSha256Preserved", normalHash).put("legacyTechnicalSha256Preserved", technicalSentinelHash)
                    .put("unrelatedSha256Preserved", unrelatedHash))
            persist()
        } finally {
            KokoroEngineCoordinator.cancel(owner)
            KokoroEngineCoordinator.releaseIfIdle()
            val after = artifacts.associate { it.fileName to fingerprint(File(root, it.fileName)) }
            manifest.put("installedResourcesUnchanged", before == after)
            persist()
            assertEquals("Existing model, French lexicon and voice bytes and timestamps are preserved", before, after)
        }
    }

    private fun verifyWave(file: File, samples: Int) {
        val bytes = ByteArray(44).also { header -> file.inputStream().use { java.io.DataInputStream(it).readFully(header) } }
        val header = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        assertEquals("RIFF", String(header.array(), 0, 4, Charsets.US_ASCII))
        assertEquals("WAVE", String(header.array(), 8, 4, Charsets.US_ASCII))
        assertEquals(1, header.getShort(20).toInt())
        assertEquals(1, header.getShort(22).toInt())
        assertEquals(24_000, header.getInt(24))
        assertEquals(16, header.getShort(34).toInt())
        assertEquals(samples * 2, header.getInt(40))
        assertEquals(samples * 2L + 44L, file.length())
    }
    private fun fingerprint(file: File) = listOf(file.length().toString(), file.lastModified().toString(), sha256(file))
    private fun sha256(file: File): String {
        val digest = MessageDigest.getInstance("SHA-256")
        file.inputStream().buffered().use { input ->
            val bytes = ByteArray(64 * 1024)
            while (true) { val size = input.read(bytes); if (size < 0) break; digest.update(bytes, 0, size) }
        }
        return digest.digest().joinToString("") { "%02x".format(it) }
    }
    private fun legacyKey(language: String, voice: String, text: String) = MessageDigest.getInstance("SHA-256")
        .digest(listOf("kokoro-wav-v1", KokoroModelManifest.revision, language, voice, text)
            .joinToString("\u0000").toByteArray(Charsets.UTF_8)).joinToString("") { "%02x".format(it) }
    private data class Sentence(val id: String, val text: String, val terms: List<String>)

    companion object {
        private const val QA_PACKAGE = "com.learngraphicsprogramming.app.curricularqa"
        private const val VOICE = "ff_siwis"
        private val SENTENCES = listOf(
            Sentence("01-include-iostream", "La directive include permet d'inclure la bibliothèque iostream.", listOf("include", "iostream")),
            Sentence("02-main-cout", "La fonction main utilise std::cout pour afficher le résultat.", listOf("main", "cout")),
            Sentence("03-return", "Le mot-clé return termine la fonction et transmet sa valeur.", listOf("return")),
            Sentence("04-main-double-ordinary", "La main reste ouverte pendant une double lecture.", emptyList()),
            Sentence("05-public-continue-long-ordinary", "Le public continue la lecture pendant un long moment.", emptyList())
        )
    }
}
