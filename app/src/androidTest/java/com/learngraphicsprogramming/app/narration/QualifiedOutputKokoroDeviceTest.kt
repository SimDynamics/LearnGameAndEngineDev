package com.learngraphicsprogramming.app.narration

import android.os.Build
import android.os.Looper
import android.os.SystemClock
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.BuildConfig
import com.learngraphicsprogramming.app.narration.phonemes.EnglishMisakiG2P
import com.learngraphicsprogramming.app.narration.phonemes.FrenchFluidG2P
import com.learngraphicsprogramming.app.narration.phonemes.SpanishFluidG2P
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

/** Fresh complete utterances from the actual installed resources; no TTS, queue or cache writes. */
@RunWith(AndroidJUnit4::class)
class QualifiedOutputKokoroDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val app = instrumentation.targetContext

    @Test fun cmakeSyntaxCompilesUnderAndroidIcuAndKeepsSeparateSourceOwners() {
        assertEquals("Use only the isolated APK", QA_PACKAGE, app.packageName)
        val source = "Usa \${clientes} y \"\${cliente_principal}\"."
        val pieces = mutableListOf<Triple<Int, Int, String>>()
        val prepared = SpanishTechnicalSpeech.prepareForReading(source) { start, end, spoken ->
            pieces += Triple(start, end, spoken)
        }
        assertEquals(SpanishTechnicalSpeech.prepareForKokoro(source), prepared)
        assertEquals("Usa dólar abre llave clientes cierra llave y abre comilla doble dólar abre llave " +
            "cliente guion bajo principal cierra llave cierra comilla doble.", ManualEnglishSpeech.strip(prepared.text))
        assertTrue("The dollar is owned separately rather than by a whole paragraph", pieces.any {
            source.substring(it.first, it.second) == "$" && it.third.trim() == "dólar"
        })
        for (math in listOf("\${x}$", "\${x}+1$", "\${x}^2$", "\${x}^{2}$", "\${x} + 1$",
            "Calcula \${x}+1$ y \${y}^2$.", "Calcula \$x\$ y \$y\$.")) {
            assertEquals(SpanishTechnicalSpeech.Preparation(math, false), SpanishTechnicalSpeech.prepare(math))
        }
        assertEquals("dólar abre llave clientes cierra llave sin comillas frente a " +
            "dólar abre llave clientes cierra llave.",
            SpanishTechnicalSpeech.prepare("\${clientes} sin comillas frente a \${clientes}.").text)
    }

    @Test fun qualifiedOutputUsesNativeLetterNamesAndSpanishNotationWithoutVoiceSplicing() = verifySamples(SAMPLES)

    @Test fun technicalSymbolsUseContinuousNativeSpeechAndTheirOwnObservedWordTimes() = verifySamples(SYMBOL_SAMPLES)

    private fun verifySamples(samples: List<Sample>) {
        assertEquals("Use only the isolated APK", QA_PACKAGE, app.packageName)
        assertEquals(QA_PACKAGE, BuildConfig.APPLICATION_ID)
        assertTrue("Use only the explicitly authorized LDPlayer or Honor QA device",
            Build.MODEL == "CRT-LX3" || Build.SUPPORTED_ABIS.any { it == "x86" || it == "x86_64" })
        assertNotEquals(Looper.getMainLooper(), Looper.myLooper())
        val external = requireNotNull(app.getExternalFilesDir(null)).canonicalFile
        val root = InstrumentationRegistry.getArguments().getString("modelRoot")?.let(::File)?.canonicalFile
            ?: File(external, "kokoro/${KokoroModelManifest.revision}").canonicalFile
        assertTrue("Existing resources belong only to the isolated application",
            root.path.startsWith(external.path + File.separator) || root.path.startsWith(app.filesDir.canonicalPath + File.separator))
        val artifacts = (KokoroModelManifest.sharedArtifacts +
            samples.flatMap { KokoroModelManifest.resources(it.language) } + EnglishAuxResources.artifacts +
            samples.map { requireNotNull(KokoroVoiceCatalog.find(it.voice)).artifact }).distinctBy { it.fileName }
        val beforeResources = artifacts.associate { artifact ->
            val file = File(root, artifact.fileName)
            assertTrue("Real installed SHA-256 must match; this test never downloads: ${artifact.fileName}",
                KokoroModelManifest.verify(file, artifact))
            artifact.fileName to fingerprint(file)
        }
        val prefs = File(app.applicationInfo.dataDir, "shared_prefs")
        val beforePreferences = fingerprints(prefs)
        val cache = File(app.filesDir, "kokoro-narration-cache")
        val beforeAudio = inventory(cache)
        val parent = File(external, "qualified-output-kokoro").apply { check(isDirectory || mkdir()) }.canonicalFile
        val output = File(parent, "run-${UUID.randomUUID()}").apply { check(mkdir()) }.canonicalFile
        assertEquals(parent, output.parentFile)
        val rows = JSONArray()
        val report = JSONObject().put("package", app.packageName).put("device", Build.MODEL)
            .put("sdk", Build.VERSION.SDK_INT).put("abis", JSONArray(Build.SUPPORTED_ABIS.toList()))
            .put("scope", "${Build.MODEL} native ONNX synthesis; listening naturalness remains a separate assessment")
            .put("sampleRate", 24_000).put("modelRevision", KokoroModelManifest.revision)
            .put("audioCacheUsed", false).put("androidTtsUsed", false).put("modelsDownloaded", false)
            .put("samples", rows).put("status", "running")
        fun persist() = File(output, "samples.json").writeText(report.toString(2))
        persist()
        val english = EnglishMisakiG2P(root)
        val french = FrenchFluidG2P(root, File(root, "tokenizer.json"))
        val spanish = SpanishFluidG2P.loadLexicon(File(root,
            KokoroModelManifest.resources(NarrationLanguage.SPANISH).single().fileName))
        val vocabulary = KokoroOnnxInputs.loadVocabulary(File(root, "tokenizer.json"))
        val durationCache = File(app.cacheDir, "qualified-output-duration-${UUID.randomUUID()}").canonicalFile
        assertEquals("Disposable duration metadata stays inside this QA application's cache", app.cacheDir.canonicalFile,
            durationCache.parentFile)
        val durationModel = KokoroDurationModel(root, durationCache)
        val owner = Any()
        try {
            for (sample in samples) {
                val lesson = JSONObject().put("pages", JSONArray().put(JSONObject().put("blocks", JSONArray()
                    .put(JSONObject().put("type", "paragraph").put("text", sample.source)))))
                val original = lesson.toString()
                val source = LessonNarrationText.page(lesson, 0, sample.language, forKokoro = true)
                val segments = NarrationSegments.sentences(source)
                assertEquals("A complete test phrase must remain one continuous segment", 1, segments.size)
                val prepared = segments.single()
                val phones = when (sample.language) {
                    NarrationLanguage.SPANISH -> MixedSpanishG2P.phonemizePrepared(prepared, spanish)
                    NarrationLanguage.FRENCH -> MixedFrenchG2P.phonemizePrepared(prepared, french)
                    else -> english.phonemize(prepared, sample.language == NarrationLanguage.BRITISH_ENGLISH)
                }
                val spoken = ManualEnglishSpeech.strip(prepared)
                when (sample.id) {
                    "fr-cout" -> {
                        assertEquals(sample.source, spoken)
                        assertTrue("std uses all three French letter names", phones.contains(MixedFrenchG2P.namespaceFrenchLetters))
                        assertTrue("cout retains exactly the shared C + out phones", phones.contains(EnglishTechnicalSpeech.coutPhones))
                        assertFalse("std must not expand to standard", phones.contains(MixedFrenchG2P.namespaceUsPhones))
                    }
                    "en-us-cout", "en-gb-cout" -> {
                        assertEquals(sample.source, spoken)
                        assertTrue("std uses all three English letter names", phones.contains(EnglishTechnicalSpeech.STD_PHONES))
                        assertTrue("cout retains exactly the shared C + out phones", phones.contains(EnglishTechnicalSpeech.coutPhones))
                    }
                    "es-include" -> {
                        assertTrue(spoken, spoken.contains("almohadilla include"))
                        assertFalse("Do not add an unauthored directiva", Regex("(?i)\\bdirectiva\\b").containsMatchIn(spoken))
                        assertTrue("The include technical phones are unchanged", phones.contains(ManualEnglishPronunciations.phones.getValue("include")))
                    }
                    "es-directiva-include" -> {
                        assertTrue(spoken, spoken.contains("directiva almohadilla include"))
                        assertEquals("Keep the authored directiva once", 1, Regex("(?i)\\bdirectiva\\b").findAll(spoken).count())
                        assertTrue(phones.contains(ManualEnglishPronunciations.phones.getValue("include")))
                    }
                    "es-std-prefix" -> assertTrue("The bare namespace prefix is spelled rather than guessed: $spoken",
                        spoken.contains("ese te de doble dos puntos"))
                    "es-cmake-variable", "es-cmake-list" -> {
                        assertFalse("No unsupported dollar reaches the phonemizer", '$' in spoken)
                        assertTrue(spoken, spoken.contains("dólar abre llave"))
                        assertTrue(spoken, spoken.contains("cierra llave"))
                    }
                }
                assertEquals("The exact original visible lesson text stays unchanged", original, lesson.toString())
                assertEquals("No word-level recordings or voices may be spliced", 1, KokoroPhonemeChunks.split(phones).size)
                KokoroOnnxInputs.tokenize(phones, vocabulary)
                var inferenceCalls = 0
                val started = SystemClock.elapsedRealtime()
                val pcm = KokoroEngineCoordinator.withEngine(owner, root, { false }, foreground = false) { engine ->
                    inferenceCalls++
                    engine.synthesizePhonemes(phones, sample.voice, 1f)
                }
                assertEquals("One complete native synthesis invocation per phrase", 1, inferenceCalls)
                assertTrue("Production PCM is finite and nonempty", pcm.size >= 2_400 && pcm.all { it.isFinite() })
                val rms = sqrt(pcm.sumOf { it.toDouble() * it } / pcm.size)
                val peak = pcm.maxOf { abs(it) }.toDouble()
                assertTrue("Production PCM contains real speech, not silence", rms > .0001 && peak > .001)
                val wav = File(output, "${sample.id}.wav")
                NarrationWavWriter.write(wav, pcm)
                verifyWave(wav, pcm.size)
                // Observe the exact same full-context frontend. Never divide the
                // utterance uniformly or synthesize words independently for timing.
                val sources = when (sample.language) {
                    NarrationLanguage.SPANISH -> {
                        val trace = requireNotNull(MixedSpanishG2P.traceWordsPrepared(prepared, spanish)) {
                            "Spanish production phones must retain exact lexical ownership: ${sample.id}"
                        }
                        assertEquals(phones, trace.phonemes)
                        val lexemes = NarrationSpokenWords.lexemes(prepared)
                        assertEquals(lexemes.map { it.canonical }, trace.words.map { it.word })
                        lexemes.zip(trace.words).map { (source, phone) ->
                            NarrationSourcePhones(source.start, source.endExclusive, phone.start, phone.endExclusive)
                        }
                    }
                    NarrationLanguage.FRENCH -> {
                        val trace = requireNotNull(MixedFrenchG2P.traceWordsPrepared(prepared, french)) {
                            "French production phones must retain exact lexical ownership: ${sample.id}"
                        }
                        assertEquals(phones, trace.phonemes)
                        trace.words.map { NarrationSourcePhones(it.sourceStart, it.sourceEndExclusive, it.start, it.endExclusive) }
                    }
                    else -> {
                        val trace = requireNotNull(english.traceWords(prepared, sample.language == NarrationLanguage.BRITISH_ENGLISH)) {
                            "English production phones must retain exact lexical ownership: ${sample.id}"
                        }
                        assertEquals(phones, trace.phonemes)
                        trace.words.map { NarrationSourcePhones(it.sourceStart, it.sourceEndExclusive, it.start, it.endExclusive) }
                    }
                }
                val chunks = KokoroPhonemeChunks.split(phones)
                if (sample.id == "en-us-symbols" || sample.id == "en-gb-symbols") {
                    val british = sample.language == NarrationLanguage.BRITISH_ENGLISH
                    val hash = sources.single { prepared.substring(it.sourceStart, it.sourceEndExclusive) == "#" }
                    assertEquals("A named symbol cannot acquire a BART pronunciation for a whitespace token",
                        if (british) "hˈaʃ" else "hˈæʃ", phones.substring(hash.phoneStart, hash.phoneEndExclusive))
                    val namespaces = sources.filter { prepared.substring(it.sourceStart, it.sourceEndExclusive) == "std" }
                    assertEquals(2, namespaces.size)
                    namespaces.forEach { assertEquals("No extra sound may be prefixed to the three letter names",
                        EnglishTechnicalSpeech.STD_PHONES, phones.substring(it.phoneStart, it.phoneEndExclusive)) }
                    val output = sources.single { prepared.substring(it.sourceStart, it.sourceEndExclusive) == "cout" }
                    assertEquals(EnglishTechnicalSpeech.coutPhones, phones.substring(output.phoneStart, output.phoneEndExclusive))
                }
                val timings = NarrationCpuBudget.capBackground().use {
                    chunks.map { durationModel.durations(it, sample.voice) }
                }
                val wavHash = sha256(wav)
                val alignment = requireNotNull(NarrationWordTimingBuilder.buildSourceChunked(prepared, wavHash,
                    pcm.size.toLong(), phones, sources, chunks, timings, sample.language)) {
                    "Actual phoneme durations must match this exact native waveform sample count: ${sample.id}"
                }
                assertEquals(wavHash, alignment.audioSha256)
                assertEquals(pcm.size.toLong(), alignment.sampleCount)
                assertTrue(NarrationWordTimingBuilder.valid(prepared, alignment.sampleCount, alignment.words, sample.language))
                if (sample.id.endsWith("-symbols")) {
                    val atoms = NarrationSpokenWords.technicalAtoms(prepared, sample.language)
                    assertEquals(atoms.map { it.start to it.endExclusive }, alignment.words.map { it.start to it.endExclusive })
                    val expected = if (sample.language == NarrationLanguage.FRENCH) listOf("#", "::")
                        else listOf("#", "<", ">", "{", "}", "::")
                    for (symbol in expected) {
                        assertTrue("Every requested symbol has its own native observed time: $symbol", atoms.any {
                            prepared.substring(it.start, it.endExclusive) == symbol
                        })
                    }
                    val segmentsByIndex = segments.withIndex().associate { it.index to it.value }
                    val mapping = NarrationWordMappings.forBlock(0, sample.source, segmentsByIndex, sample.language).getValue(0)
                    assertEquals(alignment.words.map { ReadingTextRange(it.start, it.endExclusive) }, mapping.spoken)
                    assertTrue("Symbol ownership never covers an entire paragraph", mapping.displayed.all {
                        it.endExclusive - it.start < sample.source.length
                    })
                    assertTrue("The following ordinary word keeps a later native boundary", alignment.words.zipWithNext().any {
                        (left, right) -> prepared.substring(left.start, left.endExclusive) == "::" && left.endSample <= right.startSample
                    })
                }
                if (sample.id == "fr-fixed-phrase") {
                    val lexical = NarrationSpokenWords.lexemes(prepared)
                    val parce = alignment.words[lexical.indexOfFirst { it.canonical == "parce" }]
                    val que = alignment.words[lexical.indexOfFirst { it.canonical == "que" }]
                    assertTrue("The real French fixed phrase keeps parce and que at their observed separate phone boundaries",
                        parce.startSample < que.startSample && parce.endSample <= que.startSample)
                }
                val words = JSONArray()
                alignment.words.forEach { word -> words.put(JSONArray().put(word.start).put(word.endExclusive)
                    .put(word.startSample).put(word.endSample)) }
                rows.put(JSONObject().put("id", sample.id).put("language", sample.language.tag).put("voice", sample.voice)
                    .put("source", sample.source).put("spoken", spoken).put("phonemes", phones)
                    .put("inferenceCalls", inferenceCalls).put("inferenceMillis", SystemClock.elapsedRealtime() - started)
                    .put("sampleCount", pcm.size).put("durationSeconds", pcm.size / 24_000.0)
                    .put("rms", rms).put("peak", peak).put("wav", wav.name).put("wavSha256", wavHash)
                    .put("wordAlignmentSha256", alignment.audioSha256).put("wordAlignmentSampleCount", alignment.sampleCount)
                    .put("words", words).put("productionTracePhonesEqual", true).put("exactNativeDurationSamples", true)
                    .put("cacheKey", KokoroNarrationCache.key(sample.language.tag, sample.voice, prepared)))
                persist()
            }
            report.put("status", "native-phonemes-and-continuous-pcm-verified")
            persist()
        } finally {
            english.close()
            durationModel.close()
            KokoroEngineCoordinator.cancel(owner)
            KokoroEngineCoordinator.releaseIfIdle()
            assertEquals(app.cacheDir.canonicalFile, durationCache.parentFile)
            if (durationCache.exists()) check(durationCache.deleteRecursively()) { "Cannot remove this test's own disposable duration cache" }
            val afterResources = artifacts.associate { it.fileName to fingerprint(File(root, it.fileName)) }
            val afterPreferences = fingerprints(prefs)
            val afterAudio = inventory(cache)
            report.put("installedResourcesUnchanged", beforeResources == afterResources)
                .put("preferencesUnchanged", beforePreferences == afterPreferences)
                .put("existingAudioUnchanged", beforeAudio == afterAudio)
            persist()
            assertEquals("Preserve installed models, dictionaries, voices, bytes and timestamps", beforeResources, afterResources)
            assertEquals("Never change preferences, progress or reading positions", beforePreferences, afterPreferences)
            assertEquals("Never create, replace or touch existing production audio", beforeAudio, afterAudio)
        }
    }

    private fun verifyWave(file: File, samples: Int) {
        val bytes = ByteArray(44).also { header -> file.inputStream().use { java.io.DataInputStream(it).readFully(header) } }
        val header = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        assertEquals("RIFF", String(bytes, 0, 4, Charsets.US_ASCII))
        assertEquals("WAVE", String(bytes, 8, 4, Charsets.US_ASCII))
        assertEquals(1, header.getShort(20).toInt())
        assertEquals(1, header.getShort(22).toInt())
        assertEquals(24_000, header.getInt(24))
        assertEquals(16, header.getShort(34).toInt())
        assertEquals(samples * 2, header.getInt(40))
        assertEquals(samples * 2L + 44L, file.length())
    }
    private fun fingerprints(directory: File) = directory.listFiles().orEmpty().filter { it.isFile }.associate { it.name to fingerprint(it) }
    private fun inventory(directory: File) = directory.listFiles().orEmpty().filter { it.isFile }
        .associate { it.name to (it.length() to it.lastModified()) }
    private fun fingerprint(file: File) = listOf(file.length().toString(), file.lastModified().toString(), sha256(file))
    private fun sha256(file: File): String {
        val digest = MessageDigest.getInstance("SHA-256")
        file.inputStream().buffered().use { input ->
            val buffer = ByteArray(64 * 1024)
            while (true) { val size = input.read(buffer); if (size < 0) break; digest.update(buffer, 0, size) }
        }
        return digest.digest().joinToString("") { "%02x".format(it.toInt() and 255) }
    }
    private data class Sample(val id: String, val language: NarrationLanguage, val voice: String, val source: String)
    companion object {
        private const val QA_PACKAGE = "com.learngraphicsprogramming.app.realdownloadsqa"
        private val SYMBOL_SAMPLES = listOf(
            Sample("fr-symbols", NarrationLanguage.FRENCH, "ff_siwis", "La ligne #include utilise std::cout et :: indique la portée."),
            Sample("en-us-symbols", NarrationLanguage.AMERICAN_ENGLISH, "af_heart", "The #include <iostream> line contains { std::cout } and std:: indicates scope."),
            Sample("en-gb-symbols", NarrationLanguage.BRITISH_ENGLISH, "bf_emma", "The #include <iostream> line contains { std::cout } and std:: indicates scope."),
            Sample("es-symbol-regression", NarrationLanguage.SPANISH, "ef_dora", "La línea #include permite usar std::cout.")
        )
        private val SAMPLES = listOf(
            Sample("fr-cout", NarrationLanguage.FRENCH, "ff_siwis", "La ligne std::cout affiche le résultat."),
            Sample("en-us-cout", NarrationLanguage.AMERICAN_ENGLISH, "af_heart", "The expression std::cout displays the result."),
            Sample("en-gb-cout", NarrationLanguage.BRITISH_ENGLISH, "bf_emma", "The expression std::cout displays the result."),
            Sample("es-include", NarrationLanguage.SPANISH, "ef_dora", "La línea #include permite usar iostream."),
            Sample("es-directiva-include", NarrationLanguage.SPANISH, "ef_dora", "La directiva #include permite usar iostream."),
            Sample("es-std-prefix", NarrationLanguage.SPANISH, "ef_dora", "El prefijo std:: indica el espacio de nombres."),
            Sample("fr-fixed-phrase", NarrationLanguage.FRENCH, "ff_siwis", "Je continue parce que la lecture reste naturelle."),
            Sample("es-cmake-variable", NarrationLanguage.SPANISH, "ef_dora", "\${cliente_principal} obtiene el valor al procesar el comando."),
            Sample("es-cmake-list", NarrationLanguage.SPANISH, "ef_dora", "list(APPEND \${clientes} comprobar_cajas)")
        )
    }
}
