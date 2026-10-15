package com.learngraphicsprogramming.app.narration

import ai.onnxruntime.OnnxJavaType
import ai.onnxruntime.OnnxTensor
import ai.onnxruntime.OrtEnvironment
import ai.onnxruntime.OrtSession
import ai.onnxruntime.TensorInfo
import android.os.Looper
import java.io.File
import java.io.RandomAccessFile
import java.nio.FloatBuffer
import java.nio.LongBuffer
import java.security.MessageDigest

/**
 * Reads the duration predictor already used by the pinned Kokoro model. No waveform is produced:
 * existing recordings, synthesis providers, cache identities and playback remain untouched.
 * The small derived graph contains only original ancestor nodes/weights, copied from a verified
 * installation into disposable application cache. Construction itself does no inference or I/O.
 */
internal class KokoroDurationModel(root: File, cacheDirectory: File) : AutoCloseable {
    private val root = root.canonicalFile
    private val cacheDirectory = cacheDirectory.canonicalFile
    private var session: OrtSession? = null
    private var vocabulary: Map<Char, Long>? = null
    private val verifiedVoices = mutableSetOf<String>()
    private var closed = false
    private val runLock = Any()
    private var activeRun: OrtSession.RunOptions? = null
    private var generation = 0L

    init {
        require(generateSequence(this.cacheDirectory) { it.parentFile }.none { it == this.root }) {
            "Duration derivatives belong to application cache, never the installed model directory"
        }
    }

    @Synchronized
    fun durations(phonemes: String, voice: String, isCancelled: () -> Boolean = { false }): LongArray {
        check(Looper.myLooper() != Looper.getMainLooper()) { "Duration inference belongs to a worker" }
        check(!closed) { "Duration model is closed" }
        val epoch = synchronized(runLock) { generation }
        fun cancelled() {
            if (isCancelled() || Thread.currentThread().isInterrupted ||
                synchronized(runLock) { epoch != generation }) throw InterruptedException("Duration alignment cancelled")
        }
        cancelled()
        val current = session ?: open(::cancelled).also { session = it }
        val selected = KokoroVoiceCatalog.find(voice) ?: error("Unknown Kokoro voice")
        if (voice !in verifiedVoices) {
            require(KokoroModelManifest.verify(File(root, selected.artifact.fileName), selected.artifact)) {
                "Duration alignment requires the verified synthesis voice"
            }
            verifiedVoices += voice
        }
        cancelled()
        val ids = KokoroOnnxInputs.tokenize(phonemes, requireNotNull(vocabulary))
        val style = KokoroOnnxInputs.readStyle(File(root, selected.artifact.fileName), phonemes.length)
        val environment = OrtEnvironment.getEnvironment()
        OnnxTensor.createTensor(environment, LongBuffer.wrap(ids), longArrayOf(1, ids.size.toLong())).use { input ->
            OnnxTensor.createTensor(environment, FloatBuffer.wrap(style), longArrayOf(1, 256)).use { embedding ->
                OnnxTensor.createTensor(environment, FloatBuffer.wrap(floatArrayOf(1f)), longArrayOf(1)).use { speed ->
                    OrtSession.RunOptions().use { options ->
                        synchronized(runLock) { cancelled(); activeRun = options }
                        try {
                            current.run(mapOf("input_ids" to input, "style" to embedding, "speed" to speed), options).use { result ->
                                cancelled()
                                val tensor = result.get(KokoroDurationGraph.OUTPUT).orElse(null) as? OnnxTensor
                                    ?: error("Kokoro duration output is missing")
                                val info = tensor.info
                                require(info.type == OnnxJavaType.INT64 &&
                                    info.shape.contentEquals(longArrayOf(1, ids.size.toLong()))) {
                                    "Invalid Kokoro duration shape"
                                }
                                val values = (tensor.value as? Array<*>)?.singleOrNull() as? LongArray
                                    ?: error("Invalid Kokoro duration values")
                                require(values.size == ids.size && values.all { it in 1L..MAX_FRAMES } &&
                                    values.sum() <= MAX_FRAMES) { "Invalid Kokoro duration range" }
                                return values
                            }
                        } finally {
                            synchronized(runLock) { activeRun = null }
                        }
                    }
                }
            }
        }
    }

    private fun open(checkCancellation: () -> Unit): OrtSession {
        KokoroModelManifest.sharedArtifacts.forEach { artifact ->
            checkCancellation()
            require(KokoroModelManifest.verify(File(root, artifact.fileName), artifact)) {
                "Duration alignment requires the pinned verified Kokoro installation"
            }
        }
        vocabulary = KokoroOnnxInputs.loadVocabulary(File(root, "tokenizer.json"))
        checkCancellation()
        val graph = synchronized(derivationLock) {
            check(cacheDirectory.isDirectory || cacheDirectory.mkdirs()) { "Cannot create duration cache" }
            val destination = File(cacheDirectory, "kokoro-duration-${KokoroModelManifest.revision}-v1.onnx")
            if (!KokoroDurationGraph.matchesPinnedDerivative(destination, checkCancellation)) {
                val temporary = File.createTempFile("duration-", ".part", cacheDirectory)
                try {
                    KokoroDurationGraph.derive(File(root, "model.onnx"), temporary, checkCancellation)
                    require(KokoroDurationGraph.matchesPinnedDerivative(temporary, checkCancellation)) {
                        "Derived duration graph does not match the pinned predictor"
                    }
                    checkCancellation()
                    check(temporary.renameTo(destination)) { "Cannot publish duration graph in cache" }
                } finally { temporary.delete() }
            }
            destination
        }
        checkCancellation()
        val result = OrtSession.SessionOptions().use { options ->
            options.setIntraOpNumThreads(Runtime.getRuntime().availableProcessors().coerceIn(1, 2))
            options.setInterOpNumThreads(1)
            options.setOptimizationLevel(OrtSession.SessionOptions.OptLevel.ALL_OPT)
            options.addConfigEntry("session.intra_op.allow_spinning", "0")
            OrtEnvironment.getEnvironment().createSession(graph.absolutePath, options)
        }
        try {
            require(result.inputNames == setOf("input_ids", "style", "speed") &&
                result.outputNames == setOf(KokoroDurationGraph.OUTPUT)) { "Unexpected duration graph contract" }
            require((result.outputInfo[KokoroDurationGraph.OUTPUT]?.info as? TensorInfo)?.type == OnnxJavaType.INT64)
            checkCancellation()
            return result
        } catch (error: Throwable) {
            result.close()
            throw error
        }
    }

    fun cancelCurrentRun() {
        synchronized(runLock) {
            generation++
            runCatching { activeRun?.setTerminate(true) }
        }
    }

    @Synchronized
    override fun close() {
        if (!closed) {
            closed = true
            session?.close()
            session = null
        }
    }

    companion object {
        /** The model decoder emits exactly 600 samples per predicted frame at 24 kHz. */
        const val SAMPLES_PER_FRAME = 600
        private const val MAX_FRAMES = 4_800L // Existing synthesis limit: 120 seconds.
        private val derivationLock = Any()
    }
}

/** Minimal streaming protobuf selection; tensor payloads are never copied into Java byte arrays. */
internal object KokoroDurationGraph {
    const val OUTPUT = "/encoder/Cast_output_0"
    private const val PINNED_BYTES = 55_819_977L
    private const val PINNED_SHA256 = "2d99233f10cb7dbec0a9de3983614ee44eaf06ccd6d5a2f1c1022839288a51f3"
    private data class Field(val number: Int, val wire: Int, val start: Long, val data: Long, val end: Long)
    private data class Node(val field: Field, val inputs: List<String>, val outputs: List<String>)

    fun matchesPinnedDerivative(file: File, checkCancellation: () -> Unit = {}): Boolean {
        if (!file.isFile || file.length() != PINNED_BYTES) return false
        val digest = MessageDigest.getInstance("SHA-256")
        file.inputStream().buffered().use { input ->
            val buffer = ByteArray(64 * 1024)
            while (true) {
                checkCancellation()
                val count = input.read(buffer)
                if (count < 0) break
                digest.update(buffer, 0, count)
            }
        }
        return digest.digest().joinToString("") { "%02x".format(it.toInt() and 255) } == PINNED_SHA256
    }

    /** Caller verifies the source's manifest SHA-256 before derivation. Kept pure for fixture tests. */
    fun derive(source: File, destination: File, checkCancellation: () -> Unit = {}) {
        require(source.canonicalFile != destination.canonicalFile) { "Cannot rewrite an installed model" }
        RandomAccessFile(source, "r").use { input ->
            val top = fields(input, 0, input.length(), checkCancellation)
            val graph = top.single { it.number == 7 && it.wire == 2 }
            val graphFields = fields(input, graph.data, graph.end, checkCancellation)
            require(graphFields.none { it.number == 15 }) { "Sparse initializers are not supported" }
            val nodes = graphFields.filter { it.number == 1 }.map { field ->
                val members = fields(input, field.data, field.end, checkCancellation)
                Node(field, members.filter { it.number == 1 }.map { string(input, it) },
                    members.filter { it.number == 2 }.map { string(input, it) })
            }
            require(nodes.any { OUTPUT in it.outputs }) { "Pinned duration predictor is missing" }
            val needed = mutableSetOf(OUTPUT)
            val selectedNodes = mutableSetOf<Long>()
            nodes.asReversed().forEach { node ->
                if (node.outputs.any { it in needed }) {
                    selectedNodes += node.field.start
                    needed += node.inputs.filter { it.isNotEmpty() }
                }
            }
            val inputsAndWeights = mutableSetOf<String>()
            val selected = graphFields.filter { field ->
                when (field.number) {
                    1 -> field.start in selectedNodes
                    5, 11 -> {
                        val nameField = fields(input, field.data, field.end, checkCancellation)
                            .single { it.number == if (field.number == 5) 8 else 1 }
                        val name = string(input, nameField)
                        if (name in needed) { inputsAndWeights += name; true } else false
                    }
                    12, 13 -> false // Replace outputs; discard obsolete shape annotations.
                    else -> true
                }
            }
            val resolved = inputsAndWeights + nodes.filter { it.field.start in selectedNodes }.flatMap { it.outputs }
            require(needed.all { it in resolved }) { "Duration graph has unresolved dependencies" }
            val output = durationOutput()
            val graphSize = selected.sumOf { it.end - it.start } + output.size
            destination.outputStream().buffered().use { result ->
                val buffer = ByteArray(64 * 1024)
                fun copy(field: Field) {
                    input.seek(field.start)
                    var remaining = field.end - field.start
                    while (remaining > 0) {
                        checkCancellation()
                        val count = minOf(buffer.size.toLong(), remaining).toInt()
                        input.readFully(buffer, 0, count)
                        result.write(buffer, 0, count)
                        remaining -= count
                    }
                }
                top.forEach { field ->
                    if (field == graph) {
                        result.write(varint(7L shl 3 or 2L))
                        result.write(varint(graphSize))
                        selected.forEach(::copy)
                        result.write(output)
                    } else copy(field)
                }
            }
        }
    }

    private fun fields(input: RandomAccessFile, start: Long, end: Long, checkCancellation: () -> Unit): List<Field> {
        require(start in 0..end && end <= input.length()) { "Invalid protobuf bounds" }
        input.seek(start)
        val result = mutableListOf<Field>()
        while (input.filePointer < end) {
            checkCancellation()
            require(result.size < 100_000) { "Unexpected protobuf field count" }
            val fieldStart = input.filePointer
            val tag = readVarint(input, end)
            val number = (tag ushr 3).toInt()
            val wire = (tag and 7).toInt()
            require(number > 0) { "Invalid protobuf field" }
            val data: Long
            val fieldEnd: Long
            when (wire) {
                0 -> { data = input.filePointer; readVarint(input, end); fieldEnd = input.filePointer }
                1 -> { data = input.filePointer; fieldEnd = data + 8 }
                2 -> { val length = readVarint(input, end); data = input.filePointer
                    require(length >= 0 && length <= end - data) { "Invalid protobuf length" }
                    fieldEnd = data + length }
                5 -> { data = input.filePointer; fieldEnd = data + 4 }
                else -> error("Unsupported protobuf wire type")
            }
            require(fieldEnd in data..end) { "Truncated protobuf field" }
            result += Field(number, wire, fieldStart, data, fieldEnd)
            input.seek(fieldEnd)
        }
        return result
    }

    private fun string(input: RandomAccessFile, field: Field): String {
        require(field.wire == 2 && field.end - field.data <= 4096) { "Invalid protobuf name" }
        input.seek(field.data)
        val bytes = ByteArray((field.end - field.data).toInt())
        input.readFully(bytes)
        return bytes.toString(Charsets.UTF_8)
    }

    private fun readVarint(input: RandomAccessFile, end: Long): Long {
        var value = 0L
        for (shift in 0..63 step 7) {
            require(input.filePointer < end) { "Truncated protobuf varint" }
            val byte = input.readUnsignedByte()
            require(shift < 63 || byte <= 1) { "Invalid protobuf varint" }
            value = value or ((byte and 127).toLong() shl shift)
            if (byte < 128) return value
        }
        error("Invalid protobuf varint")
    }

    private fun varint(value: Long): ByteArray {
        require(value >= 0)
        var remaining = value
        val result = java.io.ByteArrayOutputStream()
        while (remaining >= 128) {
            result.write(((remaining and 127) or 128).toInt()); remaining = remaining ushr 7
        }
        result.write(remaining.toInt())
        return result.toByteArray()
    }

    private fun message(number: Int, bytes: ByteArray) =
        varint(number.toLong() shl 3 or 2L) + varint(bytes.size.toLong()) + bytes

    private fun durationOutput(): ByteArray {
        // ValueInfo{name, type.tensor_type{elem_type:INT64, shape:[1,num_tokens]}}.
        val dimensions = message(1, byteArrayOf(8, 1)) + message(1, message(2, "num_tokens".toByteArray()))
        val tensorType = byteArrayOf(8, 7) + message(2, dimensions)
        return message(12, message(1, OUTPUT.toByteArray()) + message(2, message(1, tensorType)))
    }
}
