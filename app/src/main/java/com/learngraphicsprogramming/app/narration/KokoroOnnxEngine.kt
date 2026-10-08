package com.learngraphicsprogramming.app.narration

import ai.onnxruntime.OnnxJavaType
import ai.onnxruntime.OnnxTensor
import ai.onnxruntime.OrtEnvironment
import ai.onnxruntime.OrtException
import ai.onnxruntime.OrtSession
import ai.onnxruntime.TensorInfo
import android.os.Looper
import java.io.File
import java.io.IOException
import java.io.RandomAccessFile
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.FloatBuffer
import java.nio.LongBuffer
import org.json.JSONObject

class KokoroSynthesisException(message: String, cause: Throwable? = null) :
    IllegalStateException(message, cause)

/**
 * Concrete inference for the pinned complete ONNX graph, without a grapheme-to-phoneme engine.
 * All operations, including construction/close, belong to a background worker. A caller must
 * obtain Kokoro-compatible phonemes from the selected G2P implementation before invoking it.
 * Neither this class nor its constructor downloads files.
 */
class KokoroOnnxEngine(directory: File) : AutoCloseable {
    private val root: File
    private val environment: OrtEnvironment
    private val session: OrtSession
    private val vocabulary: Map<Char, Long>
    private var closed = false
    private val verifiedVoices = mutableSetOf<String>()
    private val runLock = Any()
    private var activeRun: OrtSession.RunOptions? = null
    private var cancellationGeneration = 0L // Accessed only while holding runLock.

    init {
        requireWorker()
        var createdSession: OrtSession? = null
        try {
            root = directory.canonicalFile
            if (!root.isDirectory) throw KokoroSynthesisException("El modelo Kokoro no está instalado")
            // Stream SHA-256 from disk; do not duplicate the 325 MB model into a Java byte array.
            KokoroModelManifest.sharedArtifacts.forEach { artifact ->
                if (!KokoroModelManifest.verify(ownedFile(artifact.fileName), artifact)) {
                    throw KokoroSynthesisException("Falta un archivo verificado de Kokoro: ${artifact.fileName}")
                }
            }
            vocabulary = KokoroOnnxInputs.loadVocabulary(ownedFile("tokenizer.json"))
            environment = OrtEnvironment.getEnvironment()
            OrtSession.SessionOptions().use { options ->
                options.setIntraOpNumThreads(2)
                options.setInterOpNumThreads(1)
                options.setOptimizationLevel(OrtSession.SessionOptions.OptLevel.ALL_OPT)
                createdSession = environment.createSession(ownedFile("model.onnx").absolutePath, options)
            }
            val modelSession = createdSession ?: throw KokoroSynthesisException("No se pudo cargar Kokoro")
            validateInputs(modelSession)
            session = modelSession
        } catch (error: Exception) {
            createdSession?.close()
            if (error is InterruptedException) Thread.currentThread().interrupt()
            throw if (error is KokoroSynthesisException) error else
                KokoroSynthesisException("No se pudo abrir el modelo Kokoro: ${error.message ?: "error de archivos o inferencia"}", error)
        } catch (error: UnsatisfiedLinkError) {
            createdSession?.close()
            throw KokoroSynthesisException("ONNX Runtime no está disponible para este dispositivo", error)
        }
    }

    /**
     * Returns mono PCM float samples at 24 kHz. This is a low-level phoneme API, not text TTS.
     * Input provenance is the G2P caller's responsibility: IPA can also contain ordinary letters.
     * Unknown symbols and input over 510 tokens are rejected, never silently dropped/truncated.
     */
    @Synchronized
    fun synthesizePhonemes(
        phonemes: String,
        voice: String,
        rate: Float,
        isCancelled: () -> Boolean = { false }
    ): FloatArray {
        requireWorker()
        if (closed) throw KokoroSynthesisException("El motor Kokoro está cerrado")
        val generation = synchronized(runLock) {
            if (isCancelled()) throw InterruptedException("La solicitud Kokoro se canceló")
            cancellationGeneration
        }
        try {
            val selectedVoice = KokoroVoiceCatalog.find(voice)
                ?: throw KokoroSynthesisException("Voz Kokoro no válida: $voice")
            if (voice !in verifiedVoices) {
                if (!KokoroModelManifest.verify(ownedFile("$voice.bin"), selectedVoice.artifact)) {
                    throw KokoroSynthesisException("La voz Kokoro no está instalada o no supera SHA-256: $voice")
                }
                verifiedVoices.add(voice)
            }
            checkCancellation(generation)
            if (!rate.isFinite() || rate !in 0.5f..2f) throw KokoroSynthesisException("Velocidad de voz no válida")
            val ids = KokoroOnnxInputs.tokenize(phonemes, vocabulary)
            val style = KokoroOnnxInputs.readStyle(ownedFile("$voice.bin"), ids.size - 2)
            checkCancellation(generation)
            OnnxTensor.createTensor(environment, LongBuffer.wrap(ids), longArrayOf(1L, ids.size.toLong())).use { input ->
                OnnxTensor.createTensor(environment, FloatBuffer.wrap(style), longArrayOf(1L, 256L)).use { embedding ->
                    OnnxTensor.createTensor(environment, FloatBuffer.wrap(floatArrayOf(rate)), longArrayOf(1L)).use { speed ->
                        OrtSession.RunOptions().use { options ->
                            synchronized(runLock) {
                                // A stop can occur during voice/style preparation, or before this
                                // method entered. Check the request and epoch before publishing
                                // the run; later cancellation sees these same options under the lock.
                                val requestCancelled = isCancelled()
                                if (requestCancelled || cancellationGeneration != generation) {
                                    throw InterruptedException("La solicitud Kokoro se canceló antes de la inferencia")
                                }
                                activeRun = options
                            }
                            try {
                                session.run(mapOf("input_ids" to input, "style" to embedding, "speed" to speed), options).use { output ->
                                    val waveform = output.get("waveform").orElse(null) as? OnnxTensor
                                        ?: throw KokoroSynthesisException("Kokoro no produjo un tensor de audio")
                                    return extractPcm(waveform)
                                }
                            } finally { synchronized(runLock) { activeRun = null } }
                        }
                    }
                }
            }
        } catch (error: IOException) {
            throw KokoroSynthesisException("No se pudo leer la voz Kokoro", error)
        } catch (error: OrtException) {
            throw KokoroSynthesisException("La inferencia Kokoro falló: ${error.message}", error)
        } catch (error: IllegalArgumentException) {
            throw KokoroSynthesisException(error.message ?: "Entrada fonética no válida", error)
        }
    }

    /** ORT termination is thread-safe and can be requested while its worker is inside run(). */
    fun cancelCurrentRun() {
        synchronized(runLock) {
            cancellationGeneration++
            runCatching { activeRun?.setTerminate(true) }
        }
    }

    private fun checkCancellation(generation: Long) = synchronized(runLock) {
        if (cancellationGeneration != generation) throw InterruptedException("La preparación Kokoro se canceló")
    }

    @Synchronized
    override fun close() {
        requireWorker()
        if (!closed) {
            closed = true
            session.close()
        }
        // OrtEnvironment is a process-wide shared environment; do not close it here.
    }

    private fun ownedFile(name: String): File {
        val file = File(root, name)
        if (file.canonicalFile.parentFile != root) throw KokoroSynthesisException("Ruta de archivo Kokoro no válida")
        return file
    }

    private fun validateInputs(model: OrtSession) {
        val expected = mapOf("input_ids" to OnnxJavaType.INT64, "style" to OnnxJavaType.FLOAT, "speed" to OnnxJavaType.FLOAT)
        if (model.inputNames != expected.keys || "waveform" !in model.outputNames) {
            throw KokoroSynthesisException("El modelo Kokoro no tiene las entradas o la salida esperadas")
        }
        expected.forEach { (name, type) ->
            val info = model.inputInfo[name]?.info as? TensorInfo
                ?: throw KokoroSynthesisException("Entrada Kokoro no válida: $name")
            if (info.type != type) throw KokoroSynthesisException("Tipo de entrada Kokoro no válido: $name")
            val expectedRank = if (name == "speed") 1 else 2
            if (info.shape.size != expectedRank || (info.shape[0] != 1L && info.shape[0] != -1L)) {
                throw KokoroSynthesisException("Dimensiones de entrada Kokoro no válidas: $name")
            }
            if (name == "style" && info.shape[1] != 256L) {
                throw KokoroSynthesisException("El estilo de voz Kokoro debe tener 256 valores")
            }
        }
    }

    private fun extractPcm(tensor: OnnxTensor): FloatArray {
        val info = tensor.info
        val shape = info.shape
        if (info.type != OnnxJavaType.FLOAT || shape.size !in 1..2 ||
            (shape.size == 2 && shape[0] != 1L) || info.numElements !in 1L..MAX_OUTPUT_SAMPLES.toLong()) {
            throw KokoroSynthesisException("El audio Kokoro tiene tamaño o formato no válido")
        }
        // getValue creates a detached Java array: it remains usable after Result closes.
        // Avoid getFloatBuffer followed by a second full waveform copy.
        val value = tensor.value
        val samples = if (shape.size == 1) value as? FloatArray else
            (value as? Array<*>)?.singleOrNull() as? FloatArray
        samples ?: throw KokoroSynthesisException("No se pudo extraer el audio Kokoro")
        samples.indices.forEach { index ->
            val sample = samples[index]
            if (!sample.isFinite()) throw KokoroSynthesisException("Kokoro produjo audio no válido")
            samples[index] = sample.coerceIn(-1f, 1f)
        }
        return samples
    }

    companion object {
        const val sampleRate = 24_000
        private const val MAX_OUTPUT_SAMPLES = sampleRate * 120

        private fun requireWorker() {
            if (Looper.myLooper() == Looper.getMainLooper()) {
                throw KokoroSynthesisException("Kokoro debe ejecutarse fuera del hilo de interfaz")
            }
        }
    }
}

/** Pure preparation helpers, independently testable without models or an ONNX native library. */
internal object KokoroOnnxInputs {
    fun loadVocabulary(file: File): Map<Char, Long> {
        require(file.length() in 1L..64_000L) { "Vocabulario Kokoro no válido" }
        val json = JSONObject(file.readText(Charsets.UTF_8)).getJSONObject("model").getJSONObject("vocab")
        val vocabulary = mutableMapOf<Char, Long>()
        json.keys().forEach { key ->
            require(key.length == 1) { "El vocabulario debe contener símbolos fonéticos individuales" }
            val id = json.getLong(key)
            require(id in 0L..177L) { "Identificador fonético Kokoro no válido" }
            vocabulary[key.single()] = id
        }
        require(vocabulary['$'] == 0L && vocabulary['ˈ'] == 156L && vocabulary[' '] == 16L) {
            "El vocabulario no corresponde al modelo Kokoro fijado"
        }
        return vocabulary
    }

    fun tokenize(phonemes: String, vocabulary: Map<Char, Long>): LongArray {
        require(phonemes.isNotBlank()) { "La entrada fonética está vacía" }
        require(phonemes.length <= KokoroModelManifest.maximumPhonemeTokens) {
            "Divide la entrada fonética en fragmentos de hasta 510 símbolos"
        }
        val ids = LongArray(phonemes.length + 2)
        phonemes.forEachIndexed { index, symbol ->
            require(symbol != '$') { "El símbolo de padding no es un fonema" }
            ids[index + 1] = vocabulary[symbol]
                ?: throw IllegalArgumentException("Símbolo fonético desconocido: $symbol")
        }
        return ids // zero padding at the beginning/end, as required by the graph.
    }

    /** Matches Kokoro.js ONNX generate_from_ids: min(phonemeCount, 509), 256 float32. */
    fun styleRow(phonemeCount: Int): Int {
        require(phonemeCount in 1..KokoroModelManifest.maximumPhonemeTokens) { "Longitud fonética no válida" }
        return phonemeCount.coerceAtMost(509)
    }

    fun readStyle(file: File, phonemeCount: Int): FloatArray {
        require(file.isFile && file.length() == 510L * 256L * 4L) { "Archivo de voz Kokoro incompleto" }
        val bytes = ByteArray(256 * 4)
        RandomAccessFile(file, "r").use { input ->
            input.seek(styleRow(phonemeCount).toLong() * bytes.size)
            input.readFully(bytes)
        }
        val floats = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN).asFloatBuffer()
        val style = FloatArray(256)
        floats.get(style)
        require(style.all { it.isFinite() }) { "El estilo de voz contiene valores no válidos" }
        return style
    }
}
