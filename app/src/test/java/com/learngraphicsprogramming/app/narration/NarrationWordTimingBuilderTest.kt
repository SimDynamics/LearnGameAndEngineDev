package com.learngraphicsprogramming.app.narration

import com.learngraphicsprogramming.app.narration.phonemes.SpanishFluidG2P
import org.junit.Assert.*
import org.junit.Test

class NarrationWordTimingBuilderTest {
    private val hash = "a".repeat(64)

    @Test fun indivisibleElisionsUseTheirObservedNativeRangeWithoutInventedSubwordDurations() {
        val source = "L'algorithme continues."
        val phones = "lalɡɔʁitm kəntˈɪnjuz."
        val second = source.indexOf("continues")
        val split = phones.indexOf(' ')
        val owners = listOf(NarrationSourcePhones(0, second - 1, 0, split),
            NarrationSourcePhones(second, source.length - 1, split + 1, phones.length - 1))
        val frames = LongArray(phones.length + 2) { (it % 5 + 1).toLong() }
        val samples = frames.sum() * 600L
        val actual = requireNotNull(NarrationWordTimingBuilder.buildSourceChunked(source, hash, samples,
            phones, owners, listOf(phones), listOf(frames)))
        assertEquals(3, actual.words.size)
        assertEquals(actual.words[0].startSample, actual.words[1].startSample)
        assertEquals(actual.words[0].endSample, actual.words[1].endSample)
        assertEquals(frames.take(split + 1).sum() * 600L, actual.words[1].endSample)
        assertTrue(actual.words[2].startSample > actual.words[1].endSample)
        assertTrue(NarrationWordTimingBuilder.valid(source, samples, actual.words))
        assertNull(NarrationWordTimingBuilder.buildSourceChunked(source, hash, samples + 1,
            phones, owners, listOf(phones), listOf(frames)))
        assertNull(NarrationWordTimingBuilder.buildSourceChunked(source, hash, samples,
            phones, owners.dropLast(1), listOf(phones), listOf(frames)))
        assertNull(NarrationWordTimingBuilder.buildSourceChunked(source, hash, samples,
            phones, owners + owners.last(), listOf(phones), listOf(frames)))
    }

    @Test fun curricularTemperatureScalesKeepWordTimingsWithoutChangingKokoroAudio() {
        val sentences = listOf(
            "Una temperatura de -3 °C describe un estado.",
            "Un aumento de 5 °C describe un cambio.",
            "La lectura de 32 °F también se conserva."
        )
        for (sentence in sentences) {
            val prepared = ManualEnglishSpeech.PREPARED + sentence
            val trace = requireNotNull(MixedSpanishG2P.traceWordsPrepared(prepared)) {
                "Visible temperature scales must have traceable phonemes: $sentence"
            }
            assertEquals(MixedSpanishG2P.phonemizePrepared(prepared, emptyMap()), trace.phonemes)
            val ticks = LongArray(trace.phonemes.length + 2) { 1L }
            val alignment = requireNotNull(NarrationWordTimingBuilder.build(
                prepared, hash, ticks.sum() * 600L, trace, ticks))
            assertTrue(NarrationWordTimingBuilder.valid(prepared, alignment.sampleCount, alignment.words))
            val displayed = alignment.words.map { prepared.substring(it.start, it.endExclusive) }
            assertTrue(displayed.any { it.equals("C", true) || it.equals("F", true) })
            assertTrue(displayed.any { it.any(Char::isDigit) })
            assertTrue(alignment.words.zipWithNext().all { (a, b) -> a.endSample <= b.startSample })
        }
    }

    @Test fun cppLanguageNameGroupsItsExistingThreeWordReadingWithoutChangingSentencePhones() {
        val source = "Podemos discutir un algoritmo antes de decidir todos los detalles de C++."
        val trace = requireNotNull(SpanishFluidG2P.traceWords(source))
        assertEquals(SpanishFluidG2P.phonemize(source), trace.phonemes)
        assertEquals(Regex("\\p{L}+").findAll(source).count(), trace.words.size)
        val cpp = trace.words.last()
        assertEquals("c", cpp.word)
        assertEquals(SpanishFluidG2P.phonemize("ce más más"), trace.phonemes.substring(cpp.start, cpp.endExclusive))
        val durations = LongArray(trace.phonemes.length + 2) { (it % 4 + 1).toLong() }
        val text = ManualEnglishSpeech.PREPARED + source
        val alignment = requireNotNull(NarrationWordTimingBuilder.build(text, hash,
            durations.sum() * 600L, trace, durations))
        assertTrue(NarrationWordTimingBuilder.valid(text, alignment.sampleCount, alignment.words))
        assertEquals("C", text.substring(alignment.words.last().start, alignment.words.last().endExclusive))
        assertEquals(durations.take(cpp.start + 1).sum() * 600L, alignment.words.last().startSample)
        assertEquals(durations.take(cpp.endExclusive + 1).sum() * 600L, alignment.words.last().endSample)
    }

    @Test fun cppTracingIsExplicitAndOtherUnmappedExpansionsStillFailConservatively() {
        for (source in listOf("Lee C++, luego c++ y continúa.", "C++.", "(C++), después.")) {
            val trace = requireNotNull(SpanishFluidG2P.traceWords(source))
            assertEquals(SpanishFluidG2P.phonemize(source), trace.phonemes)
        }
        for (source in listOf("a++", "C+++", "C++20", "x = 2", "x + y", "C++ y 2")) {
            assertNull(source, SpanishFluidG2P.traceWords(source))
        }
    }

    @Test fun realTokenFrameCountsIncludeTheLeadingTokenWithoutInventedDelay() {
        val text = "La vida."
        val trace = requireNotNull(SpanishFluidG2P.traceWords(text))
        val durations = LongArray(trace.phonemes.length + 2) { (it + 1).toLong() }
        val sampleCount = durations.sum() * 600L
        val alignment = requireNotNull(NarrationWordTimingBuilder.build(text, hash, sampleCount, trace, durations))
        assertEquals(hash, alignment.audioSha256)
        assertEquals(sampleCount, alignment.sampleCount)
        assertEquals(600L, alignment.words.first().startSample)
        trace.words.zip(alignment.words).forEach { (phone, word) ->
            assertEquals(durations.take(phone.start + 1).sum() * 600L, word.startSample)
            assertEquals(durations.take(phone.endExclusive + 1).sum() * 600L, word.endSample)
        }
    }

    @Test fun utf16RangesDescribeTheExactPreparedSourceIncludingItsPrefix() {
        val prose = "La vida, la vida."
        val text = ManualEnglishSpeech.PREPARED + prose
        val trace = requireNotNull(SpanishFluidG2P.traceWords(prose))
        val durations = LongArray(trace.phonemes.length + 2) { 1L }
        val alignment = requireNotNull(NarrationWordTimingBuilder.build(text, hash, durations.sum() * 600L, trace, durations))
        assertEquals(1, alignment.words.first().start)
        assertEquals(listOf("La", "vida", "la", "vida"), alignment.words.map { text.substring(it.start, it.endExclusive) })
        assertTrue(alignment.words[0].endExclusive < alignment.words[2].start)
        assertTrue(alignment.words[0].endSample < alignment.words[2].startSample)
    }

    @Test fun canonicalAccentsMapBackToTheUnmodifiedSourceOffsets() {
        val source = "E\u0301l leyó."
        val trace = requireNotNull(SpanishFluidG2P.traceWords(source))
        val durations = LongArray(trace.phonemes.length + 2) { 1L }
        val alignment = requireNotNull(NarrationWordTimingBuilder.build(source, hash, durations.sum() * 600L, trace, durations))
        assertEquals("E\u0301l", source.substring(alignment.words[0].start, alignment.words[0].endExclusive))
        assertEquals(3, alignment.words[0].endExclusive)
    }

    @Test fun differentAudioLengthsAndInvalidDurationsHaveNoAlignment() {
        val text = "La vida."
        val trace = requireNotNull(SpanishFluidG2P.traceWords(text))
        val durations = LongArray(trace.phonemes.length + 2) { 1L }
        val sampleCount = durations.sum() * 600L
        assertNull(NarrationWordTimingBuilder.build(text, hash, sampleCount + 1L, trace, durations))
        assertNull(NarrationWordTimingBuilder.build(text, hash, sampleCount, trace, durations.dropLast(1).toLongArray()))
        assertNull(NarrationWordTimingBuilder.build(text, hash, sampleCount, trace, durations.copyOf().apply { this[1] = 0L }))
        assertNull(NarrationWordTimingBuilder.build(text, hash, sampleCount, trace, durations.copyOf().apply { this[1] = -1L }))
        assertNull(NarrationWordTimingBuilder.build(text, hash, sampleCount, trace, durations.copyOf().apply { this[1] = Long.MAX_VALUE }))
    }

    @Test fun changedWordsUnknownBytesAndOverlappingRangesFailClosed() {
        val text = "La vida."
        val trace = requireNotNull(SpanishFluidG2P.traceWords(text))
        val durations = LongArray(trace.phonemes.length + 2) { 1L }
        val sampleCount = durations.sum() * 600L
        assertNull(NarrationWordTimingBuilder.build("La casa.", hash, sampleCount, trace, durations))
        assertNull(NarrationWordTimingBuilder.build(text, "invalid", sampleCount, trace, durations))
        assertFalse(NarrationWordTimingBuilder.valid(text, sampleCount, listOf(
            NarrationWordTiming(0, 2, 600L, 2400L), NarrationWordTiming(3, 7, 1800L, 3000L))))
        assertFalse(NarrationWordTimingBuilder.valid(text, sampleCount, listOf(
            NarrationWordTiming(0, 2, 600L, 1200L), NarrationWordTiming(3, 8, 1800L, 3000L))))
    }

    @Test fun concatenatedChunksRetainEveryActualPaddingTokenAndPermitOnlyTrimmedWhitespace() {
        val text = "Uno dos tres."
        val trace = requireNotNull(SpanishFluidG2P.traceWords(text))
        val cut = trace.words[1].start
        val chunks = listOf(trace.phonemes.substring(0, cut).trimEnd(), trace.phonemes.substring(cut))
        val durations = chunks.mapIndexed { index, chunk -> LongArray(chunk.length + 2) { (index + 2).toLong() } }
        val sampleCount = durations.sumOf { it.sum() } * 600L
        val alignment = requireNotNull(NarrationWordTimingBuilder.buildChunked(text, hash, sampleCount, trace, chunks, durations))
        assertEquals(1200L, alignment.words[0].startSample)
        assertEquals(durations[0].sum() * 600L + 1800L, alignment.words[1].startSample)
        // No division of the complete duration: this gap is precisely first EOS plus second BOS.
        assertEquals(3000L, alignment.words[1].startSample - alignment.words[0].endSample)
        assertEquals(listOf("Uno", "dos", "tres"), alignment.words.map { text.substring(it.start, it.endExclusive) })
    }

    @Test fun longParagraphUsesTheSameChunkBoundariesAsAudioSynthesis() {
        val text = ("La lectura conserva la posición y avanza con claridad ".repeat(12)).trim() + "."
        val trace = requireNotNull(SpanishFluidG2P.traceWords(text))
        val chunks = KokoroPhonemeChunks.split(trace.phonemes)
        assertTrue(chunks.size > 1)
        val durations = chunks.mapIndexed { index, chunk -> LongArray(chunk.length + 2) { (index + 1).toLong() } }
        val sampleCount = durations.sumOf { it.sum() } * 600L
        val alignment = requireNotNull(NarrationWordTimingBuilder.buildChunked(text, hash, sampleCount, trace, chunks, durations))
        assertEquals(trace.words.size, alignment.words.size)
        assertTrue(NarrationWordTimingBuilder.valid(text, sampleCount, alignment.words))
        assertTrue(alignment.words.last().startSample > durations[0].sum() * 600L)
    }

    @Test fun crossedWordsMissingPhonemesChangedOrderOrChunkTimingMismatchAreRejected() {
        val text = "Uno dos tres."
        val trace = requireNotNull(SpanishFluidG2P.traceWords(text))
        fun build(chunks: List<String>, timing: List<LongArray> = chunks.map { LongArray(it.length + 2) { 1L } },
                  sampleCount: Long = timing.sumOf { it.sum() } * 600L) =
            NarrationWordTimingBuilder.buildChunked(text, hash, sampleCount, trace, chunks, timing)
        val withinWord = trace.words[0].start + 1
        assertNull(build(listOf(trace.phonemes.substring(0, withinWord), trace.phonemes.substring(withinWord))))
        val first = trace.phonemes.substring(0, trace.words[0].endExclusive)
        val second = trace.phonemes.substring(trace.words[1].start)
        assertNull(build(listOf(first, second.drop(1)))) // Dropped phoneme, not whitespace.
        assertNull(build(listOf(second, first)))
        assertNull(build(listOf(first, second), emptyList()))
        val timing = listOf(LongArray(first.length + 2) { 1L }, LongArray(second.length + 2) { 1L })
        assertNull(build(listOf(first, second), timing, timing.sumOf { it.sum() } * 600L + 1L))
        assertNull(build(listOf(first, second), timing.map { it.dropLast(1).toLongArray() }))
    }
}
