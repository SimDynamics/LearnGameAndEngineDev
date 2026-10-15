package com.learngraphicsprogramming.app.narration

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

/** Listening probe only: no production pronunciation, cache, preferences or playback changes. */
@RunWith(AndroidJUnit4::class)
class FrenchCoutPronunciationProbeDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val app = instrumentation.targetContext

    @Test fun exportsTwoCompleteFrenchSentencesWithExplicitUsNamespaceAndCoutCandidates() {
        assertEquals(QA_PACKAGE, app.packageName)
        assertEquals(QA_PACKAGE, BuildConfig.APPLICATION_ID)
        assertTrue("Run only in LDPlayer", Build.SUPPORTED_ABIS.any { it == "x86" || it == "x86_64" })
        assertNotEquals(Looper.getMainLooper(), Looper.myLooper())
        val external = requireNotNull(app.getExternalFilesDir(null)).canonicalFile
        val root = File(external, "kokoro/${KokoroModelManifest.revision}").canonicalFile
        val artifacts = KokoroModelManifest.sharedArtifacts + KokoroModelManifest.resources(NarrationLanguage.FRENCH) +
            requireNotNull(KokoroVoiceCatalog.find(VOICE)).artifact
        artifacts.forEach { assertTrue("Use the existing pinned resource without downloads: ${it.fileName}",
            KokoroModelManifest.verify(File(root, it.fileName), it)) }
        val before = artifacts.associate { it.fileName to (File(root, it.fileName).length() to File(root, it.fileName).lastModified()) }
        val frontend = FrenchFluidG2P(root, File(root, "tokenizer.json"))
        val vocabulary = KokoroOnnxInputs.loadVocabulary(File(root, "tokenizer.json"))
        val main = ManualEnglishPronunciations.phones.getValue("main")
        val namespace = ManualEnglishPronunciations.phones.getValue("stdexcept").substringBefore(' ')
        val coutA = ManualEnglishPronunciations.phones.getValue("cout")
        val coutB = coutA.replace("W", "aʊ")
        assertEquals("The namespace comes from the existing reviewed US standard reading", "stˈændəɹd", namespace)
        assertNotEquals("The second probe expands only the selected diphthong before French G2P", coutA, coutB)
        val masked = "La fonction \uE100 utilise \uE101 \uE102 pour afficher le résultat."
        val exportRoot = File(external, "french-manual-qa").apply { check(isDirectory || mkdir()) }.canonicalFile
        val output = File(exportRoot, "probe-${UUID.randomUUID()}").apply { check(mkdir()) }.canonicalFile
        assertEquals(exportRoot, output.parentFile)
        val samples = JSONArray()
        val report = JSONObject().put("source", SOURCE).put("voice", VOICE).put("package", app.packageName)
            .put("device", Build.MODEL).put("scope", "LDPlayer complete-utterance listening probe, not a production change")
            .put("namespaceUsPhones", namespace).put("mainSharedUsPhones", main)
            .put("modelRevision", KokoroModelManifest.revision).put("samples", samples)
            .put("naturalnessAccepted", false).put("cacheWritten", false)
        fun persist() = File(output, "probe.json").writeText(report.toString(2))
        persist()
        val owner = Any()
        var firstPhones: String? = null
        try {
            for ((name, cout) in listOf("Ashared" to coutA, "Bipa" to coutB)) {
                val foreign = linkedMapOf('\uE100' to main, '\uE101' to namespace, '\uE102' to cout)
                val phones = frontend.phonemizeWithForeignTokens(masked, foreign)
                assertTrue(phones.contains(main) && phones.contains(namespace) && phones.contains(cout))
                assertFalse("No private annotation enters the model", phones.any { it in '\uE000'..'\uF8FF' })
                KokoroOnnxInputs.tokenize(phones, vocabulary)
                assertEquals("A full French sentence remains one model invocation", 1, KokoroPhonemeChunks.split(phones).size)
                firstPhones?.let { original ->
                    val originalIndex = original.indexOf(coutA)
                    val candidateIndex = phones.indexOf(coutB)
                    assertEquals("The French context and main/std phones are identical before cout",
                        original.substring(0, originalIndex), phones.substring(0, candidateIndex))
                    assertEquals("The rest of the French sentence is identical after cout",
                        original.substring(originalIndex + coutA.length), phones.substring(candidateIndex + coutB.length))
                }
                if (firstPhones == null) firstPhones = phones
                var calls = 0
                val started = SystemClock.elapsedRealtime()
                val pcm = KokoroEngineCoordinator.withEngine(owner, root, { false }, foreground = false) { engine ->
                    calls++
                    engine.synthesizePhonemes(phones, VOICE, 1f)
                }
                assertEquals(1, calls)
                assertTrue(pcm.size >= 2_400 && pcm.all { it.isFinite() })
                val rms = sqrt(pcm.sumOf { it.toDouble() * it } / pcm.size)
                val peak = pcm.maxOf { abs(it) }
                assertTrue("The candidate produces real nonsilent audio", rms > .0001 && peak > .001f)
                val file = File(output, "$name.wav")
                NarrationWavWriter.write(file, pcm)
                val headerBytes = ByteArray(44).also { bytes -> file.inputStream().use { java.io.DataInputStream(it).readFully(bytes) } }
                val header = ByteBuffer.wrap(headerBytes).order(ByteOrder.LITTLE_ENDIAN)
                assertEquals("RIFF", String(headerBytes, 0, 4, Charsets.US_ASCII))
                assertEquals("WAVE", String(headerBytes, 8, 4, Charsets.US_ASCII))
                assertEquals(1, header.getShort(22).toInt())
                assertEquals(24_000, header.getInt(24))
                assertEquals(16, header.getShort(34).toInt())
                assertEquals(pcm.size * 2L + 44L, file.length())
                samples.put(JSONObject().put("id", name).put("wav", file.name).put("wavSha256", sha256(file))
                    .put("coutPhones", cout).put("namespacePhones", namespace).put("phonemes", phones)
                    .put("inferenceCalls", calls).put("inferenceMillis", SystemClock.elapsedRealtime() - started)
                    .put("sampleRate", 24_000).put("sampleCount", pcm.size).put("durationSeconds", pcm.size / 24_000.0)
                    .put("rms", rms).put("peak", peak.toDouble()))
                persist()
            }
            report.put("status", "exported-listening-pending")
            persist()
        } finally {
            KokoroEngineCoordinator.cancel(owner)
            KokoroEngineCoordinator.releaseIfIdle()
            assertEquals("Installed model resources are not modified", before,
                artifacts.associate { it.fileName to (File(root, it.fileName).length() to File(root, it.fileName).lastModified()) })
        }
    }

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
