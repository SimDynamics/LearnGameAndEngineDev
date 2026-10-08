package com.learngraphicsprogramming.app.narration

import ai.onnxruntime.OrtException
import android.media.AudioAttributes
import android.media.MediaPlayer
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.narration.phonemes.EnglishMisakiG2P
import com.learngraphicsprogramming.app.narration.phonemes.EnglishMisakiBartG2P
import com.learngraphicsprogramming.app.narration.phonemes.EnglishMisakiSpacyPos
import com.learngraphicsprogramming.app.narration.phonemes.FrenchFluidG2P
import com.learngraphicsprogramming.app.narration.phonemes.SpanishFluidG2P
import java.io.DataOutputStream
import java.io.File
import java.security.MessageDigest
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicReference
import kotlin.math.abs
import kotlin.math.sqrt
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.After
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.json.JSONArray

/** Requires the expressly approved resources copied into the app's private external directory. */
@RunWith(AndroidJUnit4::class)
class KokoroChainDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private fun directory(): File {
        val store = KokoroModelStore(context)
        return try { store.directory() } finally { store.close() }
    }

    @Before fun importApprovedKnownFixtureFiles() = importFixture()

    @After fun exportKnownAudioEvidenceToReadablePrivateFiles() {
        val externalRoot = context.getExternalFilesDir(null)!!.canonicalFile
        val outputDirectory = context.getExternalFilesDir("kokoro-test-audio")!!.canonicalFile
        require(outputDirectory.parentFile == externalRoot)
        val privateRoot = context.filesDir.canonicalFile
        val exportedDirectory = File(privateRoot, "kokoro-test-audio")
        require(exportedDirectory.canonicalFile.parentFile == privateRoot)
        assertTrue(exportedDirectory.isDirectory || exportedDirectory.mkdirs())
        // Scoped storage blocks adb shell access to Android/data. Export only the artifacts
        // this test owns, so adb exec-out run-as can read their unmodified binary contents.
        val stems = listOf("ef_dora", "af_heart", "ff_siwis").flatMap { voice -> listOf(voice, "${voice}_math") } +
            listOf("af_heart_aux", "bf_emma_aux", "af_heart_pos")
        stems.forEach { stem ->
            listOf(".wav", ".phonemes.txt").forEach { extension ->
                val name = "$stem$extension"
                val source = File(outputDirectory, name)
                val destination = File(exportedDirectory, name)
                require(source.canonicalFile.parentFile == outputDirectory)
                require(destination.canonicalFile.parentFile == exportedDirectory.canonicalFile)
                if (source.isFile) {
                    source.copyTo(destination, overwrite = true)
                    assertEquals("Exported evidence must retain its exact size", source.length(), destination.length())
                    assertEquals("Exported evidence must retain its exact bytes", sha256(source), sha256(destination))
                }
            }
        }
    }

    private fun sha256(file: File): String {
        val digest = MessageDigest.getInstance("SHA-256")
        file.inputStream().buffered().use { input ->
            val buffer = ByteArray(65_536)
            while (true) {
                val count = input.read(buffer)
                if (count < 0) break
                digest.update(buffer, 0, count)
            }
        }
        return digest.digest().joinToString("") { "%02x".format(it.toInt() and 0xff) }
    }

    private fun importFixture() {
        val privateRoot = context.filesDir.canonicalFile
        val fixture = File(privateRoot, "kokoro_fixture/${KokoroModelManifest.revision}")
        require(fixture.canonicalFile == File(privateRoot, "kokoro_fixture/${KokoroModelManifest.revision}"))
        if (!fixture.isDirectory) return
        val root = directory()
        assertTrue(root.isDirectory || root.mkdirs())
        (KokoroModelManifest.artifacts + EnglishAuxResources.artifacts).distinctBy { it.fileName }.forEach { artifact ->
            val source = File(fixture, artifact.fileName)
            require(source.canonicalFile.parentFile == fixture.canonicalFile)
            if (!source.exists()) return@forEach
            assertTrue("Fixture ${artifact.fileName} must have its published SHA-256", KokoroModelManifest.verify(source, artifact))
            val destination = File(root, artifact.fileName)
            require(destination.canonicalFile.parentFile == root.canonicalFile)
            if (!KokoroModelManifest.verify(destination, artifact)) {
                val partial = File(root, "${artifact.fileName}.fixture-import.part")
                require(partial.canonicalFile.parentFile == root.canonicalFile)
                try {
                    source.copyTo(partial, overwrite = true)
                    assertTrue("Copied fixture must verify before installation", KokoroModelManifest.verify(partial, artifact))
                    if (destination.exists()) assertTrue(destination.delete())
                    assertTrue(partial.renameTo(destination))
                } finally { if (partial.exists()) partial.delete() }
            }
            assertTrue("Verified source fixture must be removed after installation", source.delete())
        }
        if (fixture.list()?.isEmpty() == true) fixture.delete()
    }

    @Test fun smallVoiceRemovalAndFixtureRecoveryPreserveTheSharedModel() {
        val root = directory()
        val voice = KokoroVoiceCatalog.find("em_santa")!!.artifact
        val target = File(root, voice.fileName)
        val fixture = File(context.filesDir, "kokoro_fixture/${KokoroModelManifest.revision}")
        assertTrue(fixture.isDirectory || fixture.mkdirs())
        val recovery = File(fixture, voice.fileName)
        require(recovery.canonicalFile.parentFile == fixture.canonicalFile)
        assertTrue(KokoroModelManifest.verify(target, voice))
        target.copyTo(recovery, overwrite = false)
        val store = KokoroModelStore(context)
        try {
            assertEquals(KokoroModelStatus.INSTALLED, inspect(store, listOf(voice)).status)
            store.remove(listOf(voice))
            assertEquals(KokoroModelStatus.NOT_INSTALLED, inspect(store, listOf(voice)).status)
            assertTrue("Shared FP32 graph must remain after one voice is removed", File(root, "model.onnx").isFile)
            assertEquals(KokoroModelStatus.INSTALLED, inspect(store, KokoroModelManifest.sharedArtifacts).status)
            importFixture()
            assertEquals(KokoroModelStatus.INSTALLED, inspect(store, listOf(voice)).status)
            assertTrue(KokoroModelManifest.verify(target, voice))
        } finally {
            store.close()
            if (recovery.exists()) importFixture()
        }
    }

    private fun inspect(store: KokoroModelStore, artifacts: List<KokoroModelManifest.Artifact>): KokoroModelState {
        val completed = CountDownLatch(1)
        var result: KokoroModelState? = null
        store.inspectArtifacts(artifacts) { result = it; completed.countDown() }
        assertTrue("Store must inspect installed resources", completed.await(45, TimeUnit.SECONDS))
        return result!!
    }

    @Test fun priorityVoicesProduceValidWavAndCompleteRealPlayback() {
        val root = directory()
        val spanish = SpanishFluidG2P.loadLexicon(File(root, "es_lexicon_cache.json"))
        val english = EnglishMisakiG2P(root)
        val french = FrenchFluidG2P(root, File(root, "tokenizer.json"))
        val cases = listOf(
            "ef_dora" to SpanishFluidG2P.phonemize("Las fracciones equivalentes representan la misma cantidad. Tres cuartos es igual a seis octavos.", spanish),
            "af_heart" to english.phonemize("Equivalent fractions represent the same quantity. Three quarters equals six eighths."),
            "ff_siwis" to french.phonemize("Les fractions équivalentes représentent la même quantité. Trois quarts sont égaux à six huitièmes.")
        )
        synthesizeAndPlayCases(root, cases)
    }

    @Test fun accentedNumericalMathSentencesProduceValidWavAndCompletePlayback() {
        val root = directory()
        val spanish = SpanishFluidG2P.loadLexicon(File(root, "es_lexicon_cache.json"))
        val english = EnglishMisakiG2P(root)
        val french = FrenchFluidG2P(root, File(root, "tokenizer.json"))
        val cases = listOf(
            "ef_dora" to SpanishFluidG2P.phonemize("El ángulo mide 45 grados. La raíz cuadrada de 25 es igual a 5.", spanish),
            "af_heart" to english.phonemize("The angle is 45 degrees. The square root of 25 equals 5."),
            "ff_siwis" to french.phonemize("Cet angle mesure 45 degrés. La racine carrée de 25 est égale à 5.")
        )
        cases.forEach { (voice, phonemes) ->
            assertTrue("$voice must verbalize the numerals before Kokoro inference", phonemes.none { it.isDigit() })
            assertTrue("$voice must retain sentence pauses", phonemes.count { it == '.' } == 2)
        }
        synthesizeAndPlayCases(root, cases, "_math")
    }

    private fun synthesizeAndPlayCases(root: File, cases: List<Pair<String, String>>, suffix: String = "") {
        val outputDirectory = context.getExternalFilesDir("kokoro-test-audio")!!
        assertTrue(outputDirectory.isDirectory || outputDirectory.mkdirs())
        KokoroOnnxEngine(root).use { engine ->
            cases.forEach { (voice, phonemes) ->
                assertTrue("$voice must use actual phonemes", phonemes.isNotBlank())
                val pcm = engine.synthesizePhonemes(phonemes, voice, 1f)
                assertValidPcm(voice, pcm)
                val file = File(outputDirectory, "$voice$suffix.wav")
                writeWav(file, pcm)
                File(outputDirectory, "$voice$suffix.phonemes.txt").writeText(phonemes, Charsets.UTF_8)
                assertEquals(44L + pcm.size * 2L, file.length())
                playThroughCompletion(file, pcm.size)
            }
        }
    }

    @Test fun nativeSpacyTokensAndTagsMatchTheOriginalPipelineGoldens() {
        val goldens = instrumentation.context.assets.open("narration/spacy_pos_goldens.json").bufferedReader(Charsets.UTF_8).use { JSONArray(it.readText()) }
        EnglishMisakiSpacyPos(directory()).use { pos ->
            repeat(goldens.length()) { index ->
                val golden = goldens.getJSONObject(index)
                val expected = golden.getJSONArray("tokens")
                val actual = pos.tokenizeAndTag(golden.getString("text"))
                assertEquals("spaCy token count in source case $index", expected.length(), actual.size)
                actual.forEachIndexed { tokenIndex, token ->
                    val source = expected.getJSONObject(tokenIndex)
                    assertEquals(source.getString("text"), token.text)
                    assertEquals(source.getString("whitespace"), token.whitespace)
                    assertEquals("spaCy native tag in source case $index token $tokenIndex", source.getString("tag"), token.tag)
                }
            }
        }
    }

    @Test fun originalMisakiBartDialectsProduceExactSourcePhonemesAndPlayableAudio() {
        val root = directory()
        val cases = listOf(
            Triple(false, "af_heart", "Wtˌɑfdˈɪkʃənˌɛɹi"),
            Triple(true, "bf_emma", "Wtˌɒfdɪktˈɪʃənəɹi")
        ).map { (british, voice, expected) ->
            val phonemes = EnglishMisakiBartG2P(root, british).use { it.phonemizeWord("outofdictionary") }
            assertEquals("The locally converted BART graph must match its original model", expected, phonemes)
            voice to phonemes
        }
        synthesizeAndPlayCases(root, cases, "_aux")
    }

    @Test fun contextualMisakiHomographUsesNativePosAndProducesPlayableAudio() {
        val root = directory()
        val text = "They will read it. Yesterday they read it."
        EnglishMisakiSpacyPos(root).use { pos ->
            assertEquals(listOf("VB", "VBD"), pos.tokenizeAndTag(text).filter { it.text == "read" }.map { it.tag })
        }
        val phonemes = EnglishMisakiG2P(root).use { it.phonemize(text) }
        assertTrue("Future and past read must use the dictionary entries selected by POS", phonemes.contains("ɹˈid") && phonemes.contains("ɹˈɛd"))
        synthesizeAndPlayCases(root, listOf("af_heart" to phonemes), "_pos")
    }

    @Test fun cancellingAnActiveNativeRunAllowsTheNextRunAndCloseRejectsReuse() {
        val root = directory()
        val spanish = SpanishFluidG2P.loadLexicon(File(root, "es_lexicon_cache.json"))
        val shortPhonemes = SpanishFluidG2P.phonemize("Las fracciones representan la misma cantidad.", spanish)
        val repetitions = (KokoroModelManifest.maximumPhonemeTokens / (shortPhonemes.length + 1)).coerceAtMost(8)
        val longPhonemes = List(repetitions) { shortPhonemes }.joinToString(" ")
        assertTrue("Cancellation must exercise a substantial valid inference input", longPhonemes.length in 200..KokoroModelManifest.maximumPhonemeTokens)
        val engine = KokoroOnnxEngine(root)
        var worker: Thread? = null
        var closed = false
        try {
            // A stale page can be cancelled before entry, including before voice preparation.
            // An invalid voice proves this path checks cancellation before validating inputs.
            try {
                engine.synthesizePhonemes(shortPhonemes, "cancelled-before-voice-validation", 1f) { true }
                fail("A request cancelled before entry must not reach voice validation or ORT")
            } catch (error: InterruptedException) {
                assertTrue(error.message.orEmpty().contains("canceló"))
            }
            // Deterministically cancel after preparation, before RunOptions is published. The
            // predicate stays false: only the epoch can prevent the otherwise idle run starting.
            val preparationChecks = AtomicInteger()
            try {
                engine.synthesizePhonemes(shortPhonemes, "ef_dora", 1f) {
                    if (preparationChecks.incrementAndGet() == 2) engine.cancelCurrentRun()
                    false
                }
                fail("Cancellation during preparation must prevent native inference")
            } catch (error: InterruptedException) {
                assertTrue(error.message.orEmpty().contains("antes de la inferencia"))
            }
            assertEquals("Both the entry and pre-run guards must be exercised", 2, preparationChecks.get())

            // A successful warm-up establishes model/voice validity before testing cancellation.
            assertValidPcm("ef_dora warm-up", engine.synthesizePhonemes(shortPhonemes, "ef_dora", 1f))
            val started = CountDownLatch(1)
            val finished = CountDownLatch(1)
            val failure = AtomicReference<Throwable?>()
            worker = Thread({
                try {
                    val checks = AtomicInteger()
                    engine.synthesizePhonemes(longPhonemes, "ef_dora", 0.5f) {
                        // The second predicate is under runLock. A cancellation from the test
                        // thread waits until these RunOptions have been published, so this case
                        // still requires an actual ORT termination rather than a preparation skip.
                        if (checks.incrementAndGet() == 2) started.countDown()
                        false
                    }
                } catch (error: Throwable) {
                    failure.set(error)
                } finally {
                    finished.countDown()
                }
            }, "Kokoro-native-cancellation-test").apply { start() }
            assertTrue("Native synthesis worker must start", started.await(5, TimeUnit.SECONDS))
            val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(20)
            // Keep requesting until the registered native run exits; completing normally does
            // not count as evidence of cancellation.
            while (finished.count > 0 && System.nanoTime() < deadline) {
                engine.cancelCurrentRun()
                finished.await(5, TimeUnit.MILLISECONDS)
            }
            assertTrue("An active ORT run must respond to cancellation", finished.await(5, TimeUnit.SECONDS))
            worker.join(1_000)
            val cancelled = failure.get()
            assertNotNull("Cancellation must abort a native run rather than merely skip an idle request", cancelled)
            assertTrue("Native cancellation must remain a controlled synthesis error", cancelled is KokoroSynthesisException)
            val nativeCause = cancelled!!.cause
            assertTrue("Cancellation must originate in ONNX Runtime", nativeCause is OrtException)
            assertTrue("ORT must report its termination flag", nativeCause!!.message.orEmpty().contains("terminate", ignoreCase = true))

            // RunOptions termination belongs to one run, so this same session must recover.
            assertValidPcm("ef_dora after cancellation", engine.synthesizePhonemes(shortPhonemes, "ef_dora", 1f))
            engine.close()
            closed = true
            try {
                engine.synthesizePhonemes(shortPhonemes, "ef_dora", 1f)
                fail("A closed Kokoro session must reject another inference")
            } catch (error: KokoroSynthesisException) {
                assertTrue(error.message.orEmpty().contains("cerrado"))
            }
        } finally {
            engine.cancelCurrentRun()
            worker?.join(5_000)
            if (!closed && worker?.isAlive != true) engine.close()
        }
    }

    @Test fun all32OfficialInstalledVoicesProduceNonSilentPcm() {
        val root = directory()
        val spanish = SpanishFluidG2P.loadLexicon(File(root, "es_lexicon_cache.json"))
        val english = EnglishMisakiG2P(root)
        val french = FrenchFluidG2P(root, File(root, "tokenizer.json"))
        val phrases = mapOf(
            NarrationLanguage.SPANISH to SpanishFluidG2P.phonemize("Las fracciones representan la misma cantidad.", spanish),
            NarrationLanguage.AMERICAN_ENGLISH to english.phonemize("Equivalent fractions represent the same quantity."),
            NarrationLanguage.BRITISH_ENGLISH to english.phonemize("Equivalent fractions represent the same quantity.", british = true),
            NarrationLanguage.FRENCH to french.phonemize("Les fractions représentent la même quantité.")
        )
        assertEquals(32, KokoroVoiceCatalog.voices.size)
        assertEquals(32, KokoroVoiceCatalog.voices.map { it.id }.toSet().size)
        KokoroOnnxEngine(root).use { engine ->
            KokoroVoiceCatalog.voices.forEach { voice ->
                assertTrue("${voice.id} must be installed and authentic", KokoroModelManifest.verify(File(root, voice.artifact.fileName), voice.artifact))
                assertValidPcm(voice.id, engine.synthesizePhonemes(phrases.getValue(voice.language), voice.id, 1f))
            }
        }
    }

    private fun assertValidPcm(voice: String, pcm: FloatArray) {
        assertTrue("$voice must contain speech-duration PCM", pcm.size > 2_400)
        assertTrue("$voice PCM must be finite", pcm.all { it.isFinite() })
        val maximum = pcm.maxOf { abs(it) }
        val rms = sqrt(pcm.sumOf { it.toDouble() * it.toDouble() } / pcm.size)
        assertTrue("$voice must contain a non-silent waveform", maximum > 0.005f && rms > 0.0005)
    }

    private fun writeWav(file: File, pcm: FloatArray) {
        DataOutputStream(file.outputStream().buffered()).use { output ->
            fun intLE(value: Int) = output.writeInt(Integer.reverseBytes(value))
            fun shortLE(value: Int) = output.writeShort(java.lang.Short.reverseBytes(value.toShort()).toInt())
            output.writeBytes("RIFF"); intLE(36 + pcm.size * 2); output.writeBytes("WAVE")
            output.writeBytes("fmt "); intLE(16); shortLE(1); shortLE(1)
            intLE(24_000); intLE(48_000); shortLE(2); shortLE(16)
            output.writeBytes("data"); intLE(pcm.size * 2)
            pcm.forEach { sample -> shortLE((sample.coerceIn(-1f, 1f) * 32767f).toInt()) }
        }
    }

    private fun playThroughCompletion(file: File, sampleCount: Int) {
        val prepared = CountDownLatch(1)
        val completed = CountDownLatch(1)
        val failure = AtomicInteger(0)
        var player: MediaPlayer? = null
        try {
            instrumentation.runOnMainSync {
                player = MediaPlayer().apply {
                    setAudioAttributes(AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA).setContentType(AudioAttributes.CONTENT_TYPE_SPEECH).build())
                    setDataSource(file.absolutePath)
                    setOnPreparedListener { prepared.countDown() }
                    setOnCompletionListener { completed.countDown() }
                    setOnErrorListener { _, what, _ -> failure.set(what); prepared.countDown(); completed.countDown(); true }
                    prepareAsync()
                }
            }
            assertTrue("MediaPlayer must prepare generated Kokoro audio", prepared.await(15, TimeUnit.SECONDS))
            assertEquals("WAV must decode without errors", 0, failure.get())
            instrumentation.runOnMainSync {
                val expectedMs = sampleCount * 1000L / 24_000
                assertTrue("WAV duration must match 24 kHz PCM", abs(player!!.duration.toLong() - expectedMs) <= 100)
                player!!.start()
            }
            val timeout = (sampleCount / 24_000L + 15L).coerceAtMost(60L)
            assertTrue("Generated Kokoro audio must finish real playback", completed.await(timeout, TimeUnit.SECONDS))
            assertEquals("Playback must complete without errors", 0, failure.get())
        } finally {
            instrumentation.runOnMainSync { player?.release() }
        }
    }
}
