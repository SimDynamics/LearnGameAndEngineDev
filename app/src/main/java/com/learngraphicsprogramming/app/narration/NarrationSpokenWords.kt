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

    /**
     * Technical notation owns its actual frontend phone range too. Keep the ordinary
     * lexical definition unchanged for Spanish and old metadata; only the English
     * and French frontends explicitly opt into these additional source atoms.
     */
    fun technicalAtoms(text: CharSequence, language: NarrationLanguage): List<Lexeme> {
        val source = text.toString()
        val lexical = lexemes(source)
        if (language == NarrationLanguage.SPANISH || source.isEmpty()) return lexical
        val annotations = runCatching { ManualEnglishSpeech.spans(source) }.getOrNull() ?: return emptyList()
        val protected = annotations.map { it.start until it.endExclusive } +
            Regex("(?:https?://|www\\.)\\S+", RegexOption.IGNORE_CASE).findAll(source).map { it.range }.toList() +
            Regex("\\[([^]]+)]\\(([^)]*)\\)").findAll(source).map { it.range }.toList()
        val symbols = mutableListOf<ReadingTextRange>()
        fun add(start: Int, end: Int) {
            if (start !in source.indices || end !in (start + 1)..source.length ||
                source.substring(start, end).any { it.isLetterOrDigit() } ||
                protected.any { it.first < end && it.last + 1 > start } ||
                lexical.any { it.start < end && it.endExclusive > start }) return
            symbols += ReadingTextRange(start, end)
        }
        if (language == NarrationLanguage.FRENCH) {
            // Observe the production normalizer, rather than maintaining another
            // table of French names or guessing which punctuation is pronounced.
            val replacements = linkedMapOf<Pair<Int, Int>, StringBuilder>()
            FrenchSpeechSymbols.normalizeObserved(source) { start, finish, rendered ->
                replacements.getOrPut(start to finish) { StringBuilder() }.append(rendered)
            }
            replacements.forEach { (range, rendered) ->
                if (rendered.any { it.isLetter() } && rendered.toString().trim() != source.substring(range.first, range.second))
                    add(range.first, range.second)
            }
        } else {
            EnglishTechnicalSpeech.replacements(source).forEach { add(it.start, it.endExclusive) }
        }
        return (lexical + symbols.distinct().map { Lexeme(it.start, it.endExclusive,
            canonical(source.substring(it.start, it.endExclusive))) }).sortedBy { it.start }
    }

    private fun plain(text: String, start: Int, end: Int): List<Lexeme> =
        words.findAll(text.substring(start, end)).map {
            Lexeme(start + it.range.first, start + it.range.last + 1, canonical(it.value))
        }.toList()

    private fun canonical(value: String) =
        Normalizer.normalize(value, Normalizer.Form.NFC).lowercase(Locale.ROOT)
}
