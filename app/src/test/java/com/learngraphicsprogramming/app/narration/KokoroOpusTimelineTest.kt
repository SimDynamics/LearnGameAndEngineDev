package com.learngraphicsprogramming.app.narration

import java.io.File
import org.junit.Assert.*
import org.junit.Test

class KokoroOpusTimelineTest {
    private val pack = File(System.getProperty("translation.resources") ?: "src/main/res")
        .absoluteFile.parentFile.resolve("assets/narration/prerendered")

    @Test fun bundledPackHasExactUnroundedPresentationSamples() {
        val files = pack.listFiles().orEmpty().filter { it.extension == "ogg" }
        assertTrue("The existing verified release pack is the regression fixture", files.isNotEmpty())
        files.forEach { file ->
            val timeline = requireNotNull(KokoroOpusTimeline.read(file)) { file.name }
            assertEquals(file.name, 312, timeline.preSkip48k)
            assertTrue(timeline.finalGranule48k <= timeline.decodedSamples48k)
            assertTrue(timeline.presentationSamples24k in 1..NarrationWordTimingBuilder.MAX_SAMPLES)
            assertEquals("Real Kokoro output consists of whole model frames: ${file.name}",
                0L, timeline.presentationSamples24k % NarrationWordTimingBuilder.SAMPLES_PER_FRAME)
        }
        assertEquals(60_000L, KokoroOpusTimeline.read(pack.resolve(
            "09fcddf2081b4fd20060bd189cf9ef7d54ba6f68ba75962d54fa5b6d9f64d6cd.ogg"))?.presentationSamples24k)
        assertEquals(253_800L, KokoroOpusTimeline.read(pack.resolve(
            "0a65bcfe2a4051acc78a463dab7e557aeda94d8a84c7e76a3ef9ca6a8fce1b41.ogg"))?.presentationSamples24k)
    }

    @Test fun rejectsCorruptionTruncationTrailingDataAndChainedOrForeignStreams() {
        val original = fixture()
        assertNull(KokoroOpusTimeline.parse(original.copyOf(original.size - 1)))
        assertNull(KokoroOpusTimeline.parse(original + 0))
        assertNull(KokoroOpusTimeline.parse(original + original))
        assertNull(KokoroOpusTimeline.parse(original.copyOf().apply { this[lastIndex - 30] = (this[lastIndex - 30].toInt() xor 1).toByte() }))
        assertNull(KokoroOpusTimeline.parse(editPage(original, 2) { start, _ -> this[start + 14] = 77 }))
        assertNull(KokoroOpusTimeline.parse(editPage(original, 2) { start, _ -> this[start + 18] = 19 }))
    }

    @Test fun rejectsUnsupportedHeadersAndNonzeroPresentationOrigins() {
        val original = fixture()
        for ((field, value) in listOf(8 to 2, 9 to 2, 16 to 1, 18 to 1, 12 to 0)) {
            assertNull("Unsupported OpusHead field $field", KokoroOpusTimeline.parse(editPage(original, 0) { start, _ ->
                val body = start + 27 + (this[start + 26].toInt() and 255)
                this[body + field] = value.toByte()
            }))
        }
        val offsets = pageOffsets(original)
        if (offsets.size > 3) {
            assertNull("A cropped timeline cannot borrow ordinary origin zero", KokoroOpusTimeline.parse(
                editPage(original, 2) { start, _ -> this[start + 6] = (this[start + 6].toInt() xor 2).toByte() }))
        }
        assertNull("Odd half-samples cannot be rounded into a Kokoro boundary", KokoroOpusTimeline.parse(
            editPage(original, offsets.lastIndex) { start, _ -> this[start + 6] = (this[start + 6].toInt() xor 1).toByte() }))
        assertNull("End of stream is required", KokoroOpusTimeline.parse(
            editPage(original, offsets.lastIndex) { start, _ -> this[start + 5] = 0 }))
    }

    @Test fun rejectsInvalidTagsAudioPacketsAndUnfinishedPacketLacing() {
        val original = fixture()
        assertNull(KokoroOpusTimeline.parse(editPage(original, 1) { start, _ ->
            val body = start + 27 + (this[start + 26].toInt() and 255)
            // Oversized unsigned vendor length must never overflow or index outside the packet.
            for (i in 8..11) this[body + i] = 0xff.toByte()
        }))
        assertNull(KokoroOpusTimeline.parse(editPage(original, 2) { start, _ ->
            val body = start + 27 + (this[start + 26].toInt() and 255)
            this[body] = 0xff.toByte() // Stereo with invalid zero-frame code 3.
            this[body + 1] = 0
        }))
        assertNull(KokoroOpusTimeline.parse(editPage(original, 2) { start, _ ->
            this[start + 5] = 1 // Unmatched continued-packet flag.
        }))
    }

    @Test fun cancellationPropagatesRatherThanStartingDurationInference() {
        try {
            KokoroOpusTimeline.parse(fixture()) { true }
            fail("Cancellation must stop optional metadata work")
        } catch (_: InterruptedException) { }
    }

    private fun fixture() = pack.resolve(
        "0a65bcfe2a4051acc78a463dab7e557aeda94d8a84c7e76a3ef9ca6a8fce1b41.ogg").readBytes()

    private fun pageOffsets(bytes: ByteArray): List<Int> {
        val result = mutableListOf<Int>()
        var at = 0
        while (at < bytes.size) {
            result += at
            val segments = bytes[at + 26].toInt() and 255
            at += 27 + segments + (0 until segments).sumOf { bytes[at + 27 + it].toInt() and 255 }
        }
        return result
    }

    private fun editPage(original: ByteArray, index: Int, edit: ByteArray.(Int, Int) -> Unit): ByteArray {
        val result = original.copyOf()
        val offsets = pageOffsets(result)
        val start = offsets[index]
        val end = offsets.getOrNull(index + 1) ?: result.size
        result.edit(start, end)
        for (i in 22..25) result[start + i] = 0
        var crc = 0
        for (i in start until end) {
            crc = crc xor ((result[i].toInt() and 255) shl 24)
            repeat(8) { crc = (crc shl 1) xor if (crc < 0) 0x04c11db7 else 0 }
        }
        for (i in 0..3) result[start + 22 + i] = (crc ushr (i * 8)).toByte()
        return result
    }
}
