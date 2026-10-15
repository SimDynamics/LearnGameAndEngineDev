package com.learngraphicsprogramming.app.narration

import com.learngraphicsprogramming.app.narration.phonemes.SpanishFluidG2P

/** UTF-16 ranges refer to the exact spoken segment supplied to the controller. */
data class NarrationWordTiming(
    val start: Int,
    val endExclusive: Int,
    val startSample: Long,
    val endSample: Long
)

/** Metadata describes the exact immutable audio bytes, without another copy of lesson text. */
data class NarrationWordAlignment(
    val audioSha256: String,
    val sampleCount: Long,
    val words: List<NarrationWordTiming>
)

internal object NarrationWordTimingBuilder {
    const val SAMPLES_PER_FRAME = 600L
    const val MAX_SAMPLES = 24_000L * 120L

    fun build(
        text: String,
        audioSha256: String,
        sampleCount: Long,
        trace: SpanishFluidG2P.SpanishPhoneTrace,
        durations: LongArray
    ): NarrationWordAlignment? = buildChunked(text, audioSha256, sampleCount, trace,
        listOf(trace.phonemes), listOf(durations))

    /** Every model run contributes its own leading/trailing tokens to the concatenated WAV. */
    fun buildChunked(
        text: String,
        audioSha256: String,
        sampleCount: Long,
        trace: SpanishFluidG2P.SpanishPhoneTrace,
        chunks: List<String>,
        durations: List<LongArray>
    ): NarrationWordAlignment? {
        if (!audioSha256.matches(Regex("[a-f0-9]{64}")) || sampleCount !in 1L..MAX_SAMPLES ||
            chunks.isEmpty() || chunks.size != durations.size || chunks.size > 4096) return null
        data class Chunk(val start: Int, val end: Int, val sampleBase: Long, val frames: LongArray)
        val timedChunks = mutableListOf<Chunk>()
        var cursor = 0
        var totalSamples = 0L
        chunks.forEachIndexed { index, chunk ->
            val timing = durations[index]
            if (chunk.isBlank() || chunk.length > KokoroModelManifest.maximumPhonemeTokens ||
                timing.size != chunk.length + 2 || timing.any { it !in 1L..(MAX_SAMPLES / SAMPLES_PER_FRAME) }) return null
            val start = trace.phonemes.indexOf(chunk, cursor)
            if (start < cursor || trace.phonemes.substring(cursor, start).any { !it.isWhitespace() }) return null
            val frames = LongArray(timing.size + 1)
            timing.forEachIndexed { token, count -> frames[token + 1] = frames[token] + count }
            timedChunks += Chunk(start, start + chunk.length, totalSamples, frames)
            totalSamples += frames.last() * SAMPLES_PER_FRAME
            if (totalSamples > MAX_SAMPLES) return null
            cursor = start + chunk.length
        }
        if (trace.phonemes.substring(cursor).any { !it.isWhitespace() } || totalSamples != sampleCount) return null
        val visibleWords = NarrationSpokenWords.lexemes(text)
        if (visibleWords.size != trace.words.size || visibleWords.isEmpty()) return null
        val words = visibleWords.zip(trace.words).map { (visible, phone) ->
            if (visible.canonical != phone.word || phone.start < 0 || phone.endExclusive > trace.phonemes.length ||
                phone.start >= phone.endExclusive) return null
            val chunk = timedChunks.singleOrNull { phone.start >= it.start && phone.endExclusive <= it.end } ?: return null
            // Token zero is the actual leading model token; never subtract a guessed delay.
            NarrationWordTiming(visible.start, visible.endExclusive,
                chunk.sampleBase + chunk.frames[phone.start - chunk.start + 1] * SAMPLES_PER_FRAME,
                chunk.sampleBase + chunk.frames[phone.endExclusive - chunk.start + 1] * SAMPLES_PER_FRAME)
        }
        if (!valid(text, sampleCount, words)) return null
        return NarrationWordAlignment(audioSha256, sampleCount, words)
    }

    fun valid(text: String, sampleCount: Long, words: List<NarrationWordTiming>): Boolean {
        val actual = NarrationSpokenWords.lexemes(text)
        if (sampleCount !in 1L..MAX_SAMPLES || words.isEmpty() || words.size != actual.size) return false
        return words.indices.all { index ->
            val word = words[index]
            word.start == actual[index].start && word.endExclusive == actual[index].endExclusive &&
                word.startSample >= 0L && word.startSample < word.endSample && word.endSample <= sampleCount &&
                (index == 0 || words[index - 1].endSample <= word.startSample)
        }
    }
}
