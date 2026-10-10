package com.learngraphicsprogramming.app.narration

import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class NarrationWavWriterTest {
    @Test fun emitsPlayableMonoPcm16HeaderAndClippedSamples() {
        val file = Files.createTempFile("narration-wave", ".wav").toFile()
        try {
            NarrationWavWriter.write(file, floatArrayOf(-2f, 0f, 0.5f, 2f))
            val bytes = file.readBytes()
            val data = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
            assertEquals(52, bytes.size)
            assertEquals("RIFF", String(bytes, 0, 4, Charsets.US_ASCII))
            assertEquals(44, data.getInt(4))
            assertEquals("WAVE", String(bytes, 8, 4, Charsets.US_ASCII))
            assertEquals(1, data.getShort(20).toInt())
            assertEquals(1, data.getShort(22).toInt())
            assertEquals(24_000, data.getInt(24))
            assertEquals(48_000, data.getInt(28))
            assertEquals(16, data.getShort(34).toInt())
            assertEquals("data", String(bytes, 36, 4, Charsets.US_ASCII))
            assertEquals(8, data.getInt(40))
            assertEquals(-32_767, data.getShort(44).toInt())
            assertEquals(0, data.getShort(46).toInt())
            assertEquals(16_383, data.getShort(48).toInt())
            assertEquals(32_767, data.getShort(50).toInt())
        } finally { assertTrue(file.delete()) }
    }

    @Test fun mergesMultipleNeuralRunsIntoOneContinuousWaveform() {
        val file = Files.createTempFile("kokoro-continuous-paragraph", ".wav").toFile()
        try {
            NarrationWavWriter.writeChunks(
                file,
                listOf(floatArrayOf(-0.5f, 0f), floatArrayOf(0.5f, 1f))
            )
            val bytes = file.readBytes()
            val buffer = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
            assertEquals(44 + 4 * 2, bytes.size)
            assertEquals(8, buffer.getInt(40))
            assertEquals(-16_383, buffer.getShort(44).toInt())
            assertEquals(0, buffer.getShort(46).toInt())
            assertEquals(16_383, buffer.getShort(48).toInt())
            assertEquals(32_767, buffer.getShort(50).toInt())
        } finally { assertTrue(file.delete()) }
    }

    @Test fun rejectsInvalidModelSamplesAndEmptyAudio() {
        val file = Files.createTempFile("narration-wave-invalid", ".wav").toFile()
        try {
            for (samples in listOf(floatArrayOf(Float.NaN), floatArrayOf(Float.POSITIVE_INFINITY), floatArrayOf())) {
                var rejected = false
                try { NarrationWavWriter.write(file, samples) } catch (_: IllegalArgumentException) { rejected = true }
                assertTrue(rejected)
            }
        } finally { assertTrue(file.delete()) }
    }
}
