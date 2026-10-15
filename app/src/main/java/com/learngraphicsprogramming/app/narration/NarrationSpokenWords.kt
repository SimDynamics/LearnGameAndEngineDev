package com.learngraphicsprogramming.app.narration

import java.text.Normalizer
import java.util.Locale

/** Lexical offsets into existing speech; pronunciation annotations are one indivisible atom. */
internal object NarrationSpokenWords {
    data class Lexeme(val start: Int, val endExclusive: Int, val canonical: String)
    private val words = Regex("\\p{L}[\\p{L}\\p{M}]*|[−-]?\\d+(?:[.,]\\d+)?")

    fun lexemes(text: CharSequence): List<Lexeme> {
        val source = text.toString()
        val annotations = runCatching { ManualEnglishSpeech.spans(source) }.getOrNull() ?: return emptyList()
        if (annotations.isEmpty()) return plain(source, 0, source.length)
        val result = mutableListOf<Lexeme>()
        var cursor = 0
        annotations.forEach { span ->
            result += plain(source, cursor, span.start)
            result += Lexeme(span.start, span.endExclusive, canonical(span.entry.spelling))
            cursor = span.endExclusive
        }
        result += plain(source, cursor, source.length)
        return result
    }

    private fun plain(text: String, start: Int, end: Int): List<Lexeme> =
        words.findAll(text.substring(start, end)).map {
            Lexeme(start + it.range.first, start + it.range.last + 1, canonical(it.value))
        }.toList()

    private fun canonical(value: String) =
        Normalizer.normalize(value, Normalizer.Form.NFC).lowercase(Locale.ROOT)
}
