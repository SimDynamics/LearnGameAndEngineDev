package com.learngraphicsprogramming.app.narration

import com.learngraphicsprogramming.app.narration.phonemes.SpanishFluidG2P

/** Manually selected English phones in a full Spanish utterance, with one voice/inference.
 * No language classifier, no neural English download, and no phoneme-pattern replacement.
 */
object MixedSpanishG2P {
    // Original/A/B listening review accepted the continuous Spanish voice on 2026-10-11.
    const val productionEnabled = true

    data class Span(val text: String, val phonemes: String, val english: Boolean)

    /** Plain strings from older durable queues retain their original frontend/cache contract. */
    fun phonemizePrepared(text: String, lexicon: Map<String, String>): String {
        if (!text.startsWith(ManualEnglishSpeech.PREPARED)) return SpanishFluidG2P.phonemize(text, lexicon)
        return plan(text, lexicon, ManualEnglishPronunciations::phonemes).joinToString("") { it.phonemes }
    }

    /** Observational ownership of the exact continuous production frontend, never new audio. */
    internal fun traceWordsPrepared(
        text: String,
        lexicon: Map<String, String> = emptyMap()
    ): SpanishFluidG2P.SpanishPhoneTrace? = runCatching {
        val selected = ManualEnglishSpeech.spans(text)
        val trace = if (!text.startsWith(ManualEnglishSpeech.PREPARED) || selected.isEmpty()) {
            val prose = text.removePrefix(ManualEnglishSpeech.PREPARED.toString())
            SpanishFluidG2P.traceWords(prose, lexicon) ?: SpanishFluidG2P.traceWordsWithForeignTokens(
                prose, lexicon, emptyMap(), emptyMap())
        } else {
            require(selected.size <= 6_000)
            val foreign = linkedMapOf<Char, String>()
            val words = linkedMapOf<Char, String>()
            val occupied = text.toHashSet()
            var nextMarker = '\uE100'.code
            var last = 0
            // Exactly the production mask, retaining sentence context across languages.
            val masked = buildString {
                selected.forEach { span ->
                    append(text.substring(last, span.start).replace(ManualEnglishSpeech.PREPARED.toString(), ""))
                    while (nextMarker <= '\uF8FF'.code && nextMarker.toChar() in occupied) nextMarker++
                    require(nextMarker <= '\uF8FF'.code)
                    val marker = nextMarker++.toChar()
                    foreign[marker] = ManualEnglishPronunciations.phonemes(span.entry)
                    words[marker] = NarrationSpokenWords.lexemes(text.substring(span.start, span.endExclusive))
                        .single().canonical
                    append(marker)
                    last = span.endExclusive
                }
                append(text.substring(last).replace(ManualEnglishSpeech.PREPARED.toString(), ""))
            }
            SpanishFluidG2P.traceWordsWithForeignTokens(masked, lexicon, foreign, words)
        } ?: return@runCatching null
        if (trace.phonemes != phonemizePrepared(text, lexicon) ||
            trace.words.map { it.word } != NarrationSpokenWords.lexemes(text).map { it.canonical }) return@runCatching null
        trace
    }.getOrNull()

    fun phonemize(text: String, lexicon: Map<String, String>,
                  englishPhonemize: (ManualEnglishTerms.Entry) -> String): String =
        plan(text, lexicon, englishPhonemize).joinToString("") { it.phonemes }

    /** The same traced phones serve both the continuous-voice and experimental voice-splice probe. */
    fun plan(text: String, lexicon: Map<String, String>,
             englishPhonemize: (ManualEnglishTerms.Entry) -> String): List<Span> {
        val prepared = if (text.startsWith(ManualEnglishSpeech.PREPARED)) text else
            SpanishTechnicalSpeech.prepareForKokoro(text).text
        val selected = ManualEnglishSpeech.spans(prepared)
        if (selected.isEmpty()) return listOf(Span(ManualEnglishSpeech.strip(prepared),
            SpanishFluidG2P.phonemize(ManualEnglishSpeech.strip(prepared), lexicon), false))
        require(selected.size <= 6_000) { "Demasiados términos técnicos en una oración" }
        val foreign = linkedMapOf<Char, String>()
        val occupied = prepared.toHashSet()
        var nextMarker = '\uE100'.code
        var last = 0
        val masked = buildString {
            selected.forEach { span ->
                append(prepared.substring(last, span.start).replace(ManualEnglishSpeech.PREPARED.toString(), ""))
                while (nextMarker <= '\uF8FF'.code && nextMarker.toChar() in occupied) nextMarker++
                require(nextMarker <= '\uF8FF'.code) { "Demasiados términos técnicos en una oración" }
                val marker = nextMarker++.toChar()
                foreign[marker] = englishPhonemize(span.entry)
                append(marker)
                last = span.endExclusive
            }
            append(prepared.substring(last).replace(ManualEnglishSpeech.PREPARED.toString(), ""))
        }
        return SpanishFluidG2P.phonemizeWithForeignTokens(masked, lexicon, foreign).map {
            Span("", it.phonemes, it.foreign)
        }
    }
}
