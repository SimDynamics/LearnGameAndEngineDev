package com.learngraphicsprogramming.app.narration.phonemes

import ai.onnxruntime.OnnxJavaType
import ai.onnxruntime.OnnxTensor
import ai.onnxruntime.OrtEnvironment
import ai.onnxruntime.OrtSession
import ai.onnxruntime.TensorInfo
import android.os.Looper
import com.learngraphicsprogramming.app.narration.EnglishAuxResources
import com.learngraphicsprogramming.app.narration.KokoroModelManifest
import java.io.File
import java.nio.LongBuffer
import java.security.MessageDigest
import java.util.concurrent.atomic.AtomicLong
import org.json.JSONObject

/** The original Misaki FallbackNetwork, exported locally as two FP32 BART ONNX graphs. */
class EnglishMisakiBartG2P(directory: File, british: Boolean = false) : AutoCloseable {
    private val environment: OrtEnvironment
    private val encoder: OrtSession
    private val decoder: OrtSession
    private val graphemes: String
    private val phonemes: String
    private var closed = false
    private val cancellationGeneration = AtomicLong()
    private val runLock = Any()
    @Volatile private var activeRun: OrtSession.RunOptions? = null

    init {
        requireWorker()
        var createdEncoder: OrtSession? = null
        var createdDecoder: OrtSession? = null
        try {
            val root = directory.canonicalFile
            val variant = if (british) "gb" else "us"
            fun file(name: String): File = File(root, name).also {
                require(it.canonicalFile.parentFile == root) { "Invalid Misaki BART resource path" }
            }
            val manifestFile = file("en_${variant}_bart.json")
            val required = setOf("en_${variant}_bart.json", "en_${variant}_bart_encoder.onnx", "en_${variant}_bart_decoder.onnx")
            val pinned = EnglishAuxResources.artifacts.filter { it.fileName in required }
            require(pinned.map { it.fileName }.toSet() == required && pinned.all { KokoroModelManifest.verify(file(it.fileName), it) }) {
                "Misaki BART exported resources do not match their pinned SHA-256"
            }
            require(manifestFile.isFile && manifestFile.length() in 1L..16_384L) { "Misaki BART is not installed" }
            val manifest = JSONObject(manifestFile.readText(Charsets.UTF_8))
            EnglishMisakiBartResources.validateManifest(manifest, british)
            graphemes = manifest.getString("grapheme_chars")
            phonemes = manifest.getString("phoneme_chars")
            val encoderFile = file("en_${variant}_bart_encoder.onnx")
            val decoderFile = file("en_${variant}_bart_decoder.onnx")
            verifyGraph(encoderFile, manifest.getJSONObject("encoder"))
            verifyGraph(decoderFile, manifest.getJSONObject("decoder"))
            environment = OrtEnvironment.getEnvironment()
            OrtSession.SessionOptions().use { options ->
                options.setIntraOpNumThreads(2)
                options.setInterOpNumThreads(1)
                options.setOptimizationLevel(OrtSession.SessionOptions.OptLevel.ALL_OPT)
                createdEncoder = environment.createSession(encoderFile.absolutePath, options)
                createdDecoder = environment.createSession(decoderFile.absolutePath, options)
            }
            encoder = checkNotNull(createdEncoder)
            decoder = checkNotNull(createdDecoder)
            validateSession(encoder, mapOf("input_ids" to OnnxJavaType.INT64, "attention_mask" to OnnxJavaType.INT64), "hidden_states")
            validateSession(decoder, mapOf("decoder_input_ids" to OnnxJavaType.INT64,
                "encoder_hidden_states" to OnnxJavaType.FLOAT, "encoder_attention_mask" to OnnxJavaType.INT64), "logits")
        } catch (error: Exception) {
            runCatching { createdDecoder?.close() }
            runCatching { createdEncoder?.close() }
            throw EnglishMisakiException("Could not load Misaki BART: ${error.message}")
        } catch (_: UnsatisfiedLinkError) {
            runCatching { createdDecoder?.close() }
            runCatching { createdEncoder?.close() }
            throw EnglishMisakiException("ONNX Runtime is unavailable for Misaki BART")
        }
    }

    /** Matches FallbackNetwork(input_token.text); casing and unknown graphemes are preserved. */
    @Synchronized
    fun phonemizeWord(word: String, isCurrent: () -> Boolean = { true }): String {
        requireWorker()
        if (closed) throw EnglishMisakiException("Misaki BART is closed")
        val generation = cancellationGeneration.get()
        fun checkCurrent() {
            if (cancellationGeneration.get() != generation || Thread.currentThread().isInterrupted || !isCurrent()) {
                throw InterruptedException("Misaki BART generation was cancelled")
            }
        }
        checkCurrent()
        val ids = MisakiBartTokens.encode(word, graphemes)
        val mask = LongArray(ids.size) { 1L }
        try {
            OnnxTensor.createTensor(environment, LongBuffer.wrap(ids), longArrayOf(1, ids.size.toLong())).use { input ->
                OnnxTensor.createTensor(environment, LongBuffer.wrap(mask), longArrayOf(1, mask.size.toLong())).use { attention ->
                    OrtSession.RunOptions().use { options ->
                        synchronized(runLock) { checkCurrent(); activeRun = options }
                        try {
                            checkCurrent()
                            encoder.run(mapOf("input_ids" to input, "attention_mask" to attention), options).use { encoded ->
                                checkCurrent()
                                val hidden = encoded.get("hidden_states").orElse(null) as? OnnxTensor
                                    ?: throw EnglishMisakiException("Misaki BART encoder returned no hidden states")
                                require(hidden.info.type == OnnxJavaType.FLOAT && hidden.info.shape.contentEquals(longArrayOf(1, ids.size.toLong(), 128))) {
                                    "Misaki BART encoder output has invalid dimensions"
                                }
                                val generated = MisakiBartTokens.generate { prefix ->
                                    checkCurrent()
                                    OnnxTensor.createTensor(environment, LongBuffer.wrap(prefix), longArrayOf(1, prefix.size.toLong())).use { history ->
                                        decoder.run(mapOf("decoder_input_ids" to history,
                                            "encoder_hidden_states" to hidden, "encoder_attention_mask" to attention), options).use { decoded ->
                                            checkCurrent()
                                            val logits = decoded.get("logits").orElse(null) as? OnnxTensor
                                                ?: throw EnglishMisakiException("Misaki BART decoder returned no logits")
                                            require(logits.info.type == OnnxJavaType.FLOAT && logits.info.shape.contentEquals(longArrayOf(1, 63))) {
                                                "Misaki BART decoder output has invalid dimensions"
                                            }
                                            (logits.value as? Array<*>)?.singleOrNull() as? FloatArray
                                                ?: throw EnglishMisakiException("Misaki BART decoder returned invalid logits")
                                        }
                                    }
                                }
                                return MisakiBartTokens.decode(generated, phonemes).also {
                                    checkCurrent()
                                    if (it.isBlank()) throw EnglishMisakiException("Misaki BART produced no phonemes")
                                }
                            }
                        } finally { synchronized(runLock) { activeRun = null } }
                    }
                }
            }
        } catch (error: InterruptedException) { throw error }
        catch (error: EnglishMisakiException) { checkCurrent(); throw error }
        catch (error: Exception) { checkCurrent(); throw EnglishMisakiException("Misaki BART inference failed: ${error.message}") }
    }

    /** May run on the UI thread while the worker is inside either ORT graph. */
    fun cancelCurrentRun() {
        synchronized(runLock) {
            cancellationGeneration.incrementAndGet()
            runCatching { activeRun?.setTerminate(true) }
        }
    }

    @Synchronized
    override fun close() {
        requireWorker()
        if (closed) return
        cancelCurrentRun()
        closed = true
        try { decoder.close() } finally { encoder.close() }
        // The ORT environment is shared with the main Kokoro model.
    }

    private fun validateSession(session: OrtSession, inputs: Map<String, OnnxJavaType>, output: String) {
        require(session.inputNames == inputs.keys && session.outputNames == setOf(output)) { "Invalid Misaki BART graph signature" }
        inputs.forEach { (name, expected) ->
            val info = session.inputInfo[name]?.info as? TensorInfo
                ?: throw EnglishMisakiException("Invalid Misaki BART input: $name")
            require(info.type == expected) { "Invalid Misaki BART input type: $name" }
            require(info.shape.size == if (expected == OnnxJavaType.FLOAT) 3 else 2) { "Invalid Misaki BART input rank: $name" }
        }
    }

    private fun verifyGraph(file: File, metadata: JSONObject) {
        require(metadata.getString("name") == file.name && file.isFile && file.length() == metadata.getLong("bytes") &&
            file.length() in 1L..16_000_000L) { "Misaki BART graph is missing or incomplete: ${file.name}" }
        val digest = MessageDigest.getInstance("SHA-256")
        file.inputStream().buffered().use { input ->
            val buffer = ByteArray(64 * 1024)
            while (true) {
                if (Thread.currentThread().isInterrupted) throw InterruptedException()
                val count = input.read(buffer)
                if (count < 0) break
                digest.update(buffer, 0, count)
            }
        }
        val hash = digest.digest().joinToString("") { "%02x".format(it.toInt() and 255) }
        require(hash == metadata.getString("sha256")) { "Misaki BART graph SHA-256 does not match: ${file.name}" }
    }

    private fun requireWorker() {
        if (Looper.myLooper() == Looper.getMainLooper()) throw EnglishMisakiException("Misaki BART must run outside the interface thread")
    }
}

object EnglishMisakiBartResources {
    const val misakiRevision = "fba1236595f2d2bf21d414ba6e57d25256afada3"
    const val americanRevision = "a5631b285d18d59483c32c0c3379cb9fac924f4b"
    const val britishRevision = "d8357d5067fa26a5c34134d6bbcf4bbf000c0ac8"
    const val americanWeightSha256 = "dc4a02e62d4fcb4bb4097ecf00db89b8e1a12a549a52ab6adfbba220b80a55c5"
    const val britishWeightSha256 = "4994f474bb6f4584076a4e98189caaec11aa3f773a0d82d5bc263a03dd07e703"
    const val americanGraphemes = "____AIOWYbdfhijklmnpstuvwz'-.BCDEFGHJKLMNPQRSTUVXZacegoqrxy"
    const val britishGraphemes = "____AIQWYabdfhijklmnpstuvwz'-.BCDEFGHJKLMNOPRSTUVXZcegoqrxy"
    const val americanPhonemes = "____AIOWYbdfhijklmnpstuvwzæðŋɑɔəɛɜɡɪɹɾʃʊʌʒʔʤʧˈˌθᵊᵻ"
    const val britishPhonemes = "____AIQWYabdfhijklmnpstuvwzðŋɑɒɔəɛɜɡɪɹʃʊʌʒʤʧˈˌːθᵊ"

    internal fun validateManifest(manifest: JSONObject, british: Boolean) {
        require(manifest.getInt("format") == 1 && manifest.getString("license") == "Apache-2.0" &&
            manifest.getString("misaki_revision") == misakiRevision && manifest.getString("transformers") == "4.51.2" &&
            manifest.getString("source_revision") == if (british) britishRevision else americanRevision) { "Unsupported Misaki BART manifest" }
        require(manifest.getString("source_sha256") == if (british) britishWeightSha256 else americanWeightSha256) { "Unknown Misaki BART source weights" }
        require(manifest.getString("grapheme_chars") == if (british) britishGraphemes else americanGraphemes) { "Invalid Misaki BART grapheme table" }
        require(manifest.getString("phoneme_chars") == if (british) britishPhonemes else americanPhonemes) { "Invalid Misaki BART phoneme table" }
        require(manifest.getInt("maximum_generated_tokens") == MisakiBartTokens.maximumGeneratedTokens &&
            manifest.getInt("maximum_input_tokens") == 64 && manifest.getInt("vocab_size") == 63 && manifest.getInt("opset") == 17) { "Unsupported Misaki BART decoding settings" }
    }
}

/** The character tables and greedy generation contract of Transformers 4.51.2. */
internal object MisakiBartTokens {
    // generate() with no explicit length allows twenty new tokens after the decoder start.
    const val maximumGeneratedTokens = 21

    fun encode(word: String, table: String): LongArray {
        val codePoints = word.codePoints().toArray()
        if (codePoints.isEmpty() || codePoints.size > 62) throw EnglishMisakiException("Misaki BART requires a word of 1 to 62 characters")
        val lookup = table.mapIndexed { index, symbol -> symbol to index.toLong() }.toMap()
        return longArrayOf(1) + codePoints.map { if (it <= Char.MAX_VALUE.code) lookup[it.toChar()] ?: 3L else 3L }.toLongArray() + longArrayOf(2)
    }

    fun decode(ids: LongArray, table: String): String = buildString {
        for (id in ids) if (id > 3) table.getOrNull(id.toInt())?.let(::append)
    }

    fun generate(nextLogits: (LongArray) -> FloatArray): LongArray {
        val result = mutableListOf(1L)
        while (result.size < maximumGeneratedTokens) {
            val logits = nextLogits(result.toLongArray())
            if (logits.size != 63 || logits.any { !it.isFinite() }) throw EnglishMisakiException("Misaki BART returned invalid logits")
            // ForcedEOSTokenLogitsProcessor overrides argmax on the final permitted step.
            val token = if (result.size == maximumGeneratedTokens - 1) 2L else {
                var best = 0
                for (index in 1 until logits.size) if (logits[index] > logits[best]) best = index
                best.toLong()
            }
            result += token
            if (token == 2L) break
        }
        return result.toLongArray()
    }
}
