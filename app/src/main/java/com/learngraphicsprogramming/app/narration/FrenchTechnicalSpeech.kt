package com.learngraphicsprogramming.app.narration

/** Internal provenance over the original French source; no translated or curricular text is rewritten. */
internal object FrenchTechnicalSpeech {
    fun prepareForKokoro(text: String, mathMode: String = "auto"): String {
        if (text.isBlank() || ManualEnglishSpeech.isPrepared(text)) return text
        if (ManualEnglishSpeech.spans(text).isNotEmpty()) return ManualEnglishSpeech.PREPARED + text
        val selected = ManualEnglishTerms.selectFrench(text, mathMode)
        if (selected.isEmpty()) return text
        return buildString(text.length + selected.size * 8) {
            append(ManualEnglishSpeech.PREPARED)
            var cursor = 0
            selected.forEach { match ->
                append(text, cursor, match.start)
                // French fallbacks retain the exact original spelling, never Spanish code names.
                append(ManualEnglishSpeech.encode(match.entry, code = false))
                cursor = match.endExclusive
            }
            append(text, cursor, text.length)
        }
    }
}
