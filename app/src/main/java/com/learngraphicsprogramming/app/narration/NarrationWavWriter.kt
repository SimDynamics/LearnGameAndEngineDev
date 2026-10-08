package com.learngraphicsprogramming.app.narration

import java.io.File
import java.io.OutputStream

/** Streaming PCM16 WAV: only the current model waveform is retained in memory. */
internal object NarrationWavWriter {
    fun write(file: File, samples: FloatArray, sampleRate: Int = 24_000) {
        require(sampleRate in 1..Int.MAX_VALUE / 2 && samples.isNotEmpty() && samples.size <= (Int.MAX_VALUE - 36) / 2)
        file.outputStream().buffered().use { output ->
            fun littleInt(value: Int) { repeat(4) { output.write(value ushr (it * 8) and 0xff) } }
            fun littleShort(value: Int) { output.write(value and 0xff); output.write(value ushr 8 and 0xff) }
            output.ascii("RIFF"); littleInt(36 + samples.size * 2); output.ascii("WAVE")
            output.ascii("fmt "); littleInt(16); littleShort(1); littleShort(1)
            littleInt(sampleRate); littleInt(sampleRate * 2); littleShort(2); littleShort(16)
            output.ascii("data"); littleInt(samples.size * 2)
            samples.forEach { sample ->
                require(sample.isFinite()) { "Kokoro produced non-finite PCM" }
                littleShort((sample.coerceIn(-1f, 1f) * 32_767f).toInt())
            }
        }
    }
    private fun OutputStream.ascii(value: String) = write(value.toByteArray(Charsets.US_ASCII))
}
