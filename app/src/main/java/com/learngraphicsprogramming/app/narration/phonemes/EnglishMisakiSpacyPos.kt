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
import java.util.concurrent.atomic.AtomicLong
import org.json.JSONObject

/**
 * spaCy en_core_web_sm 3.8.0's original six HashEmbed tables, maxout CNN and tagger.
 * The optional local ONNX conversion retains the trained FP32 parameters. No POS
 * heuristic, alternate model or network request is used. Construct/run/close on a worker.
 */
class EnglishMisakiSpacyPos(directory: File) : MisakiPartOfSpeechTagger, AutoCloseable {
    private val environment: OrtEnvironment
    private val session: OrtSession
    private val frontend: SpacyPosFrontend
    private var closed = false
    private val cancellationGeneration = AtomicLong()
    private val runLock = Any()
    @Volatile private var activeRun: OrtSession.RunOptions? = null

    init {
        requireWorker()
        var createdSession: OrtSession? = null
        try {
            val root = directory.canonicalFile
            fun file(name: String): File = File(root, name).also {
                require(it.canonicalFile.parentFile == root) { "Ruta POS spaCy no válida" }
            }
            val required = setOf("spacy_pos.onnx", "spacy_pos.json")
            val pinned = EnglishAuxResources.artifacts.filter { it.fileName in required }
            require(pinned.map { it.fileName }.toSet() == required && pinned.all { KokoroModelManifest.verify(file(it.fileName), it) }) {
                "Los recursos POS spaCy no coinciden con su SHA-256 fijado"
            }
            val graph = pinned.single { it.fileName == "spacy_pos.onnx" }
            val metadata = file("spacy_pos.json")
            require(metadata.length() in 1L..4_000_000L && graph.bytes in 1L..10_000_000L) { "Recursos POS spaCy incompletos" }
            val json = JSONObject(metadata.readText(Charsets.UTF_8))
            require(json.getString("sourceWheelSha256") == SOURCE_WHEEL_SHA256 && json.getString("spacyVersion") == "3.8.7") { "Modelo POS spaCy de origen no compatible" }
            require(json.getString("graphSha256") == graph.sha256 && json.getLong("graphBytes") == graph.bytes) { "La metadata POS no corresponde a la gráfica fijada" }
            frontend = SpacyPosFrontend(json)
            environment = OrtEnvironment.getEnvironment()
            OrtSession.SessionOptions().use { options ->
                options.setIntraOpNumThreads(2)
                options.setInterOpNumThreads(1)
                options.setOptimizationLevel(OrtSession.SessionOptions.OptLevel.ALL_OPT)
                createdSession = environment.createSession(file("spacy_pos.onnx").absolutePath, options)
            }
            session = checkNotNull(createdSession)
            validateSignature(session)
        } catch (error: Exception) {
            runCatching { createdSession?.close() }
            if (error is InterruptedException) Thread.currentThread().interrupt()
            throw EnglishMisakiException("No se pudo abrir POS spaCy: ${error.message}")
        } catch (_: UnsatisfiedLinkError) {
            runCatching { createdSession?.close() }
            throw EnglishMisakiException("ONNX Runtime no está disponible para POS spaCy")
        }
    }

    /** Compatibility API: a spaCy Doc whose provided words have spaces between them. */
    @Synchronized
    override fun tag(words: List<String>): List<String?> {
        requireReady()
        if (words.isEmpty()) return emptyList()
        val generation = cancellationGeneration.get()
        val checkCurrent = { checkCurrent(generation) }
        checkCurrent()
        return infer(frontend.pretokenized(words), checkCurrent)
    }

    /** Use this text API in Misaki: SPACY attributes depend on actual trailing spaces. */
    @Synchronized
    fun tokenizeAndTag(text: String, isCurrent: () -> Boolean = { true }): List<SpacyTaggedToken> {
        requireReady()
        val generation = cancellationGeneration.get()
        val checkCurrent = { checkCurrent(generation, isCurrent) }
        checkCurrent()
        val tokens = frontend.tokenize(text)
        checkCurrent()
        if (tokens.isEmpty()) return emptyList()
        val tags = infer(tokens, checkCurrent)
        return tokens.mapIndexed { index, token -> SpacyTaggedToken(token.text, token.whitespace, tags[index]) }
    }

    private fun infer(tokens: List<SpacyPosToken>, checkCurrent: () -> Unit): List<String> {
        try {
            checkCurrent()
            val keys = frontend.keys(tokens)
            checkCurrent()
            OnnxTensor.createTensor(environment, LongBuffer.wrap(keys), longArrayOf(tokens.size.toLong(), 6L, 4L)).use { input ->
                OrtSession.RunOptions().use { options ->
                    synchronized(runLock) { checkCurrent(); activeRun = options }
                    try {
                        checkCurrent()
                        session.run(mapOf("keys" to input), options).use { result ->
                            checkCurrent()
                            val logits = result.get("logits").orElse(null) as? OnnxTensor
                                ?: throw EnglishMisakiException("POS spaCy no produjo etiquetas")
                            val info = logits.info
                            require(info.type == OnnxJavaType.FLOAT && info.shape.contentEquals(longArrayOf(tokens.size.toLong(), 50L))) { "Salida POS spaCy no válida" }
                            val rows = logits.value as? Array<*>
                                ?: throw EnglishMisakiException("Tensor POS spaCy no válido")
                            return rows.map { raw ->
                                checkCurrent()
                                val row = raw as? FloatArray ?: throw EnglishMisakiException("Fila POS spaCy no válida")
                                require(row.size == 50 && row.all { it.isFinite() }) { "Puntuaciones POS spaCy no válidas" }
                                var best = 0
                                for (index in 1 until row.size) if (row[index] > row[best]) best = index
                                frontend.labels[best]
                            }
                        }
                    } finally { synchronized(runLock) { activeRun = null } }
                }
            }
        } catch (error: InterruptedException) { throw error }
        catch (error: EnglishMisakiException) { checkCurrent(); throw error }
        catch (error: Exception) { checkCurrent(); throw EnglishMisakiException("La inferencia POS spaCy falló: ${error.message}") }
    }

    /** Thread-safe termination; cancellation is distinct from an unavailable resource. */
    fun cancelCurrentRun() {
        synchronized(runLock) {
            cancellationGeneration.incrementAndGet()
            runCatching { activeRun?.setTerminate(true) }
        }
    }

    private fun checkCurrent(generation: Long, isCurrent: () -> Boolean = { true }) {
        if (cancellationGeneration.get() != generation || Thread.currentThread().isInterrupted || !isCurrent()) {
            throw InterruptedException("Misaki POS generation was cancelled")
        }
    }

    @Synchronized
    override fun close() {
        requireWorker()
        if (!closed) {
            cancelCurrentRun()
            closed = true
            session.close()
        }
        // Shared process environment belongs to Kokoro and the other optional graphs too.
    }

    private fun requireReady() {
        requireWorker()
        if (closed) throw EnglishMisakiException("POS spaCy está cerrado")
    }

    private fun validateSignature(model: OrtSession) {
        require(model.inputNames == setOf("keys") && model.outputNames == setOf("logits")) { "Entradas POS spaCy no válidas" }
        val input = model.inputInfo.getValue("keys").info as? TensorInfo ?: error("Tensor de entrada POS no válido")
        val output = model.outputInfo.getValue("logits").info as? TensorInfo ?: error("Tensor de salida POS no válido")
        require(input.type == OnnxJavaType.INT64 && input.shape.contentEquals(longArrayOf(-1, 6, 4))) { "Dimensiones de entrada POS no válidas" }
        require(output.type == OnnxJavaType.FLOAT && output.shape.contentEquals(longArrayOf(-1, 50))) { "Dimensiones de salida POS no válidas" }
    }

    private fun requireWorker() {
        if (Looper.myLooper() == Looper.getMainLooper()) throw EnglishMisakiException("POS spaCy debe ejecutarse fuera del hilo de interfaz")
    }

    companion object {
        const val SOURCE_WHEEL_SHA256 = "1932429db727d4bff3deed6b34cfc05df17794f4a52eeb26cf8928f7c1a0fb85"
    }
}
