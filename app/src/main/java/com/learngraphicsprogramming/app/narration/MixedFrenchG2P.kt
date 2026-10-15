package com.learngraphicsprogramming.app.narration

import com.learngraphicsprogramming.app.narration.phonemes.FrenchFluidG2P

/** The existing French sentence frontend, with the same reviewed US phones used in Spanish. */
internal object MixedFrenchG2P {
    private val namespaceBefore = Regex("(?<![\\p{L}\\p{N}_])std\\s*::\\s*$")

    // The shared alias for stdexcept is "standard except"; reuse its reviewed first word.
    internal val namespaceUsPhones: String get() = ManualEnglishPronunciations.phones.getValue("stdexcept").substringBefore(' ')
    internal fun englishPhones(entry: ManualEnglishTerms.Entry): String = ManualEnglishPronunciations.phonemes(entry)
    internal fun namespacePhonesBefore(text: String, span: ManualEnglishSpeech.Span): String? =
        namespaceUsPhones.takeIf { namespaceBefore.containsMatchIn(text.substring(0, span.start)) }
    internal fun needsTermBoundary(text: String, span: ManualEnglishSpeech.Span): Boolean {
        if (span.entry.id != "cout") return false
        val next = text.substring(span.endExclusive).dropWhile { it.isWhitespace() || it == '`' }.firstOrNull()
        return next?.isLetterOrDigit() == true || next == ManualEnglishSpeech.START
    }

    fun phonemizePrepared(text: String, french: FrenchFluidG2P): String {
        // Durable queues created before this feature retain their original audio/cache contract.
        if (!text.startsWith(ManualEnglishSpeech.PREPARED)) return french.phonemize(FrenchSpeechSymbols.normalize(text))
        val selected = ManualEnglishSpeech.spans(text)
        if (selected.isEmpty()) return french.phonemize(FrenchSpeechSymbols.normalize(ManualEnglishSpeech.strip(text)))
        require(selected.size <= 6_000) { "Trop de termes techniques dans une phrase" }
        val occupied = text.toHashSet()
        val foreign = linkedMapOf<Char, String>()
        var next = 0xE100
        fun markerFor(phones: String): Char {
            while (next <= 0xF8FF && next.toChar() in occupied) next++
            require(next <= 0xF8FF) { "Trop de termes techniques dans une phrase" }
            return next++.toChar().also { foreign[it] = phones }
        }
        var last = 0
        val masked = buildString {
            for (span in selected) {
                append(text.substring(last, span.start).replace(ManualEnglishSpeech.PREPARED.toString(), ""))
                append(markerFor(englishPhones(span.entry)))
                // A model-native prosodic boundary separates the technical name from prose.
                // It changes no word/voice/rate and never creates or splices another recording.
                if (needsTermBoundary(text, span)) append(',')
                last = span.endExclusive
            }
            append(text.substring(last).replace(ManualEnglishSpeech.PREPARED.toString(), ""))
        }
        // These are notation around selected identifiers, never a second content source.
        // The qualified namespace and selected identifiers retain their reviewed US readings.
        val marker = "[\\uE100-\\uF8FF]"
        val notation = masked.replace(Regex("(?<![\\p{L}\\p{N}_])std\\s*::\\s*(?=$marker)")) {
            "${markerFor(namespaceUsPhones)} "
        }
            .replace(Regex("#\\s*(?=$marker)"), "dièse ")
            .replace(Regex("<\\s*($marker)\\s*>"), "chevron ouvrant $1 chevron fermant")
            .replace("`", "")
        return french.phonemizeWithForeignTokens(FrenchSpeechSymbols.normalize(notation), foreign)
    }
}
