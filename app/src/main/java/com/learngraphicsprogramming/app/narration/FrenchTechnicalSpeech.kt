package com.learngraphicsprogramming.app.narration

/** Internal provenance over the original French source; no translated or curricular text is rewritten. */
internal object FrenchTechnicalSpeech {
    internal const val SYMBOL_CACHE_REVISION = "french-technical-symbol-names-v1"

    /** Old recordings could predate residual symbol reading; scope also gains its own speech. */
    internal fun hasChangedSymbolPronunciation(text: String): Boolean =
        hasLegacySymbolRevision(text) || changedOperatorReadings(text).isNotEmpty()

    internal fun hasLegacySymbolRevision(text: String): Boolean =
        ManualEnglishSpeech.strip(text).let { '#' in it || "::" in it }

    internal fun changedOperatorReadings(text: String): List<Triple<Int, Int, String>> =
        FrenchSpeechSymbols.changedOperatorReadings(ManualEnglishSpeech.strip(text))

    fun prepareForKokoro(text: String, mathMode: String = "auto"): String = prepare(text, mathMode, null)

    /** Observe the same replacements; offsets remain derived from the displayed original. */
    fun prepareForReading(text: String, mathMode: String = "auto",
        onPiece: (Int, Int, String) -> Unit): String = prepare(text, mathMode, onPiece)

    private fun prepare(text: String, mathMode: String, onPiece: ((Int, Int, String) -> Unit)?): String {
        if (text.isBlank() || ManualEnglishSpeech.isPrepared(text)) {
            onPiece?.invoke(0, text.length, text)
            return text
        }
        if (ManualEnglishSpeech.spans(text).isNotEmpty()) {
            onPiece?.invoke(0, 0, ManualEnglishSpeech.PREPARED.toString())
            onPiece?.invoke(0, text.length, text)
            return ManualEnglishSpeech.PREPARED + text
        }
        val selected = ManualEnglishTerms.selectFrench(text, mathMode)
        if (selected.isEmpty()) {
            onPiece?.invoke(0, text.length, text)
            return text
        }
        return buildString(text.length + selected.size * 8) {
            append(ManualEnglishSpeech.PREPARED)
            onPiece?.invoke(0, 0, ManualEnglishSpeech.PREPARED.toString())
            var cursor = 0
            selected.forEach { match ->
                append(text, cursor, match.start)
                onPiece?.invoke(cursor, match.start, text.substring(cursor, match.start))
                // French fallbacks retain the exact original spelling, never Spanish code names.
                val encoded = ManualEnglishSpeech.encode(match.entry, code = false)
                append(encoded)
                onPiece?.invoke(match.start, match.endExclusive, encoded)
                cursor = match.endExclusive
            }
            append(text, cursor, text.length)
            onPiece?.invoke(cursor, text.length, text.substring(cursor))
        }
    }
}
