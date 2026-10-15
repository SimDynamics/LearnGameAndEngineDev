package com.learngraphicsprogramming.app.narration

import java.io.ByteArrayOutputStream
import java.io.File

/**
 * Exact presentation samples for the mono Ogg/Opus files made by package_prerendered_opus.py.
 * RFC 7845 sections 4.2–4.5 define the audible origin after pre-skip and the final granule.
 * This deliberately excludes cropped, chained, multistream and damaged containers. It does
 * not derive word boundaries: the original Kokoro token durations must still match exactly.
 */
internal object KokoroOpusTimeline {
    data class Timeline(val preSkip48k: Int, val finalGranule48k: Long, val decodedSamples48k: Long) {
        val presentationSamples24k: Long get() = (finalGranule48k - preSkip48k) / 2L
    }

    private const val MAX_BYTES = 16 * 1024 * 1024
    private const val MAX_PACKET_BYTES = 65_536
    private val checksumTable = IntArray(256) { value ->
        var remainder = value shl 24
        repeat(8) { remainder = (remainder shl 1) xor if (remainder < 0) 0x04c11db7 else 0 }
        remainder
    }

    fun read(file: File, isCancelled: () -> Boolean = { false }): Timeline? {
        val length = file.length()
        if (!file.isFile || length !in 64L..MAX_BYTES.toLong()) return null
        if (isCancelled()) throw InterruptedException("Opus metadata cancelled")
        val bytes = file.inputStream().use { input ->
            val output = ByteArrayOutputStream(length.toInt())
            val buffer = ByteArray(16 * 1024)
            while (true) {
                if (isCancelled()) throw InterruptedException("Opus metadata cancelled")
                val size = input.read(buffer)
                if (size < 0) break
                if (output.size() + size > MAX_BYTES) return null
                output.write(buffer, 0, size)
            }
            output.toByteArray()
        }
        return parse(bytes, isCancelled)
    }

    internal fun parse(bytes: ByteArray, isCancelled: () -> Boolean = { false }): Timeline? {
        if (bytes.size !in 64..MAX_BYTES) return null
        fun u8(at: Int) = bytes[at].toInt() and 255
        fun u32(at: Int) = (0..3).fold(0L) { value, i -> value or (u8(at + i).toLong() shl (i * 8)) }
        fun i64(at: Int) = (0..7).fold(0L) { value, i -> value or (u8(at + i).toLong() shl (i * 8)) }
        var cursor = 0
        var serial = -1L
        var pageNumber = 0L
        var packetNumber = 0
        var preSkip = -1
        var decoded = 0L
        var lastPacketSamples = 0L
        var previousGranule = 0L
        var ended = false
        val packet = ByteArrayOutputStream()
        while (cursor < bytes.size) {
            if (isCancelled()) throw InterruptedException("Opus metadata cancelled")
            if (ended || pageNumber > 4096 || bytes.size - cursor < 27 ||
                String(bytes, cursor, 4, Charsets.US_ASCII) != "OggS" || u8(cursor + 4) != 0) return null
            val flags = u8(cursor + 5)
            val granule = i64(cursor + 6)
            val pageSerial = u32(cursor + 14)
            val sequence = u32(cursor + 18)
            val checksum = u32(cursor + 22)
            val segments = u8(cursor + 26)
            if (segments == 0 || flags and 0xf8 != 0 || sequence != pageNumber ||
                (pageNumber == 0L && flags != 2) || (pageNumber > 0 && flags and 2 != 0) ||
                (flags and 1 != 0) != (packet.size() != 0) || bytes.size - cursor < 27 + segments) return null
            if (pageNumber == 0L) serial = pageSerial else if (serial != pageSerial) return null
            val headerEnd = cursor + 27 + segments
            val bodySize = (0 until segments).sumOf { u8(cursor + 27 + it) }
            val pageEnd = headerEnd + bodySize
            if (pageEnd > bytes.size) return null
            var crc = 0
            for (at in cursor until pageEnd) {
                val byte = if (at in (cursor + 22)..(cursor + 25)) 0 else u8(at)
                crc = (crc shl 8) xor checksumTable[((crc ushr 24) xor byte) and 255]
            }
            if ((crc.toLong() and 0xffffffffL) != checksum) return null
            val beforePackets = packetNumber
            val beforeDecoded = decoded
            var body = headerEnd
            for (index in 0 until segments) {
                val size = u8(cursor + 27 + index)
                if (packet.size() + size > MAX_PACKET_BYTES) return null
                packet.write(bytes, body, size)
                body += size
                if (size == 255) continue
                val content = packet.toByteArray()
                packet.reset()
                when (packetNumber) {
                    0 -> {
                        if (pageNumber != 0L || index != segments - 1 || content.size != 19 ||
                            String(content, 0, 8, Charsets.US_ASCII) != "OpusHead" ||
                            content[8].toInt() != 1 || content[9].toInt() != 1 || content[18].toInt() != 0 ||
                            content[16].toInt() != 0 || content[17].toInt() != 0 ||
                            (12..15).fold(0L) { value, i -> value or ((content[i].toInt() and 255).toLong() shl ((i - 12) * 8)) } != 48_000L) return null
                        preSkip = (content[10].toInt() and 255) or ((content[11].toInt() and 255) shl 8)
                    }
                    1 -> if (!validTags(content) || index != segments - 1) return null
                    else -> {
                        val samples = packetSamples48k(content) ?: return null
                        lastPacketSamples = samples.toLong()
                        decoded += samples
                        if (decoded > NarrationWordTimingBuilder.MAX_SAMPLES * 2L + 65_536L) return null
                    }
                }
                packetNumber++
            }
            ended = flags and 4 != 0
            if (packetNumber <= 2) {
                if (ended || granule != if (packet.size() == 0) 0L else -1L) return null
            } else if (decoded == beforeDecoded) {
                if (granule != -1L || ended || packetNumber != beforePackets) return null
            } else {
                if (granule < 0L || granule <= previousGranule) return null
                if (ended) {
                    if (packet.size() != 0 || granule > decoded || granule <= decoded - lastPacketSamples) return null
                } else if (granule != decoded) return null // No cropped origin or unrecorded gaps.
                previousGranule = granule
            }
            cursor = pageEnd
            pageNumber++
        }
        val audible = previousGranule - preSkip
        if (!ended || packetNumber < 3 || preSkip < 0 || audible <= 0L || audible % 2L != 0L ||
            audible / 2L > NarrationWordTimingBuilder.MAX_SAMPLES) return null
        return Timeline(preSkip, previousGranule, decoded)
    }

    private fun validTags(packet: ByteArray): Boolean {
        if (packet.size < 16 || String(packet, 0, 8, Charsets.US_ASCII) != "OpusTags") return false
        fun u32(at: Int): Long = (0..3).fold(0L) { value, i ->
            value or ((packet[at + i].toInt() and 255).toLong() shl (i * 8))
        }
        val vendor = u32(8)
        if (vendor > packet.size - 16) return false
        var cursor = 12 + vendor.toInt()
        val comments = u32(cursor)
        cursor += 4
        if (comments > 256) return false
        repeat(comments.toInt()) {
            if (cursor > packet.size - 4) return false
            val length = u32(cursor)
            cursor += 4
            if (length > packet.size - cursor) return false
            cursor += length.toInt()
        }
        return true // Optional padding after the comments has no timing semantics.
    }

    private fun packetSamples48k(packet: ByteArray): Int? {
        if (packet.size < 2) return null
        val toc = packet[0].toInt() and 255
        if (toc and 4 != 0) return null // The supplied Kokoro source is mono.
        val frameSamples = when {
            toc and 0x80 != 0 -> 120 shl ((toc ushr 3) and 3)
            toc and 0x60 == 0x60 -> if (toc and 8 != 0) 960 else 480
            else -> (480 shl ((toc ushr 3) and 3)).let { if (it == 3840) 2880 else it }
        }
        val frames = when (toc and 3) {
            0 -> 1
            1, 2 -> 2
            else -> packet[1].toInt() and 63
        }
        val samples = frameSamples * frames
        return samples.takeIf { frames in 1..48 && it in 120..5760 }
    }
}
