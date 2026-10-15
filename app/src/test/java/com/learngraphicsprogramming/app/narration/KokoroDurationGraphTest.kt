package com.learngraphicsprogramming.app.narration

import java.io.ByteArrayOutputStream
import java.io.File
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class KokoroDurationGraphTest {
    @get:Rule val temporary = TemporaryFolder()

    @Test fun copiesOnlyThePredictorAncestorsAndTheirExactWeightsWithoutChangingSource() {
        val source = temporary.newFile("source.onnx")
        val weights = ByteArray(512 * 1024) { (it % 251).toByte() }
        val graph = node(listOf("input_ids", "style"), listOf("encoded")) +
            node(listOf("encoded", "duration_weight", "speed"), listOf(KokoroDurationGraph.OUTPUT)) +
            node(listOf(KokoroDurationGraph.OUTPUT, "decoder_weight"), listOf("waveform")) +
            field(5, tensor("duration_weight", weights)) +
            field(5, tensor("decoder_weight", "UNNEEDED_DECODER_WEIGHT".toByteArray())) +
            input("input_ids") + input("style") + input("speed") +
            field(12, field(1, "waveform".toByteArray())) +
            field(13, field(1, "OBSOLETE_SHAPE_ANNOTATION".toByteArray()))
        val original = byteArrayOf(8, 9) + field(7, graph) + field(8, byteArrayOf(16, 20))
        source.writeBytes(original)
        val output = temporary.newFile("duration.onnx")
        KokoroDurationGraph.derive(source, output)
        val actual = output.readBytes()
        assertArrayEquals(original, source.readBytes())
        assertTrue(actual.containsSequence(weights))
        assertTrue(actual.containsSequence("duration_weight".toByteArray()))
        assertTrue(actual.containsSequence(KokoroDurationGraph.OUTPUT.toByteArray()))
        assertTrue(actual.containsSequence("input_ids".toByteArray()))
        assertFalse(actual.containsSequence("decoder_weight".toByteArray()))
        assertFalse(actual.containsSequence("waveform".toByteArray()))
        assertFalse(actual.containsSequence("OBSOLETE_SHAPE_ANNOTATION".toByteArray()))
        val second = temporary.newFile("duration-again.onnx")
        KokoroDurationGraph.derive(source, second)
        assertArrayEquals(actual, second.readBytes())
    }

    @Test fun rejectsMissingDependenciesAndPredictorsInsteadOfInventingDurations() {
        val source = temporary.newFile("missing.onnx")
        val output = temporary.newFile("output.onnx")
        source.writeBytes(field(7, node(listOf("absent"), listOf(KokoroDurationGraph.OUTPUT))))
        assertThrows(IllegalArgumentException::class.java) { KokoroDurationGraph.derive(source, output) }
        source.writeBytes(field(7, node(emptyList(), listOf("waveform"))))
        assertThrows(IllegalArgumentException::class.java) { KokoroDurationGraph.derive(source, output) }
    }

    @Test fun rejectsTruncatedWireDataAndRefusesToOverwriteItsSource() {
        val source = temporary.newFile("invalid.onnx")
        val output = temporary.newFile("output.onnx")
        source.writeBytes(byteArrayOf(58, 50, 10)) // graph declares more bytes than the file contains.
        assertThrows(IllegalArgumentException::class.java) { KokoroDurationGraph.derive(source, output) }
        assertThrows(IllegalArgumentException::class.java) { KokoroDurationGraph.derive(source, source) }
    }

    @Test fun cancellationStopsDerivationWithoutTouchingTheInstalledModel() {
        val source = temporary.newFile("cancel.onnx")
        val bytes = field(7, node(listOf("input_ids"), listOf(KokoroDurationGraph.OUTPUT)) + input("input_ids"))
        source.writeBytes(bytes)
        assertThrows(InterruptedException::class.java) {
            KokoroDurationGraph.derive(source, temporary.newFile("cancelled.onnx")) { throw InterruptedException() }
        }
        assertArrayEquals(bytes, source.readBytes())
    }

    @Test fun arbitraryOrDamagedFilesCannotBeAcceptedAsThePinnedDerivative() {
        assertFalse(KokoroDurationGraph.matchesPinnedDerivative(File(temporary.root, "absent.onnx")))
        val arbitrary = temporary.newFile("not-the-predictor.onnx")
        arbitrary.writeText("not the verified duration predictor")
        assertFalse(KokoroDurationGraph.matchesPinnedDerivative(arbitrary))
    }

    private fun node(inputs: List<String>, outputs: List<String>): ByteArray = field(1,
        inputs.fold(byteArrayOf()) { all, name -> all + field(1, name.toByteArray()) } +
            outputs.fold(byteArrayOf()) { all, name -> all + field(2, name.toByteArray()) })
    private fun tensor(name: String, data: ByteArray) = field(8, name.toByteArray()) + field(9, data)
    private fun input(name: String) = field(11, field(1, name.toByteArray()))
    private fun field(number: Int, bytes: ByteArray) = varint((number.toLong() shl 3) or 2) + varint(bytes.size.toLong()) + bytes
    private fun varint(value: Long): ByteArray {
        var number = value
        val bytes = ByteArrayOutputStream()
        while (number >= 128) { bytes.write(((number and 127) or 128).toInt()); number = number ushr 7 }
        bytes.write(number.toInt())
        return bytes.toByteArray()
    }
    private fun ByteArray.containsSequence(needle: ByteArray): Boolean =
        indices.any { start -> start + needle.size <= size && needle.indices.all { this[start + it] == needle[it] } }
}
