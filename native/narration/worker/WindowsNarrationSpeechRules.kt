package com.learngraphicsprogramming.app.narration

/** Windows reading refinements, applied by the generated host frontend only.
 * The displayed source, Android implementation and stored recordings stay intact. */
internal object WindowsNarrationSpeechRules {
    /** Only a prose-introduced displayed output value, never a code statement,
     * symbol explanation, URL, type qualification or a fenced source listing. */
    private val outputValueCue = Regex(
        "(?iu)(?:\\bsalida(?:\\s+(?:esperada|exacta|solicitada|del programa))?\\s*(?:es|será|:|=)|" +
        "\\bobjetivo(?:\\s+de\\s+(?:esta\\s+)?actividad)?\\s+es|" +
        "\\b(?:debe\\s+)?(?:mostrar|muestra|imprimir|imprime)|\\bresultado\\s+(?:esperado\\s+)?es)\\s*$")

    fun isDisplayedOutputValue(text: String, opening: Int, value: String): Boolean {
        if (!value.any(Char::isLetter) || ':' !in value || "::" in value || "://" in value ||
                value.any { it in "\\\"'`{}[]();#<>=?" }) return false
        return outputValueCue.containsMatchIn(text.substring((opening - 160).coerceAtLeast(0), opening))
    }

    private val namedLetter = Regex("(?iu)(?<![\\p{L}\\p{M}\\p{N}_])(?:letra|carácter|caracter)[ \\t]+$")

    /** A named ASCII letter is one technical token; its following colon is prose.
     * Quotes, code and formulas are handled first by the canonical normalizer. */
    fun isNamedLetter(text: String, index: Int): Boolean {
        if (text[index] !in 'a'..'z' && text[index] !in 'A'..'Z') return false
        val next = text.getOrNull(index + 1)
        if (next != null && (next.isLetterOrDigit() || next == '_' ||
                Character.getType(next) in setOf(Character.NON_SPACING_MARK.toInt(),
                    Character.COMBINING_SPACING_MARK.toInt(), Character.ENCLOSING_MARK.toInt()))) return false
        return namedLetter.containsMatchIn(text.substring((index - 64).coerceAtLeast(0), index))
    }

    private fun isolatedEscapeBoundary(c: Char): Boolean =
        c.isWhitespace() || c in ".,;:!?()[]{}\"'«»“”‘’…/\\+-*=<>|&—–"

    /** Only the two standalone C++ symbol escapes belong to this case.
     * A backslash in a path, URL or a longer identifier is not an escape mention.
     * Source offsets stay unchanged for word-level highlighting. */
    fun isIsolatedSymbolEscape(text: String, index: Int): Boolean {
        if (text.getOrNull(index) != '\\' ||
            text.getOrNull(index + 1) !in setOf('"', '\\')) return false
        return (index == 0 || isolatedEscapeBoundary(text[index - 1])) &&
            (index + 2 == text.length || isolatedEscapeBoundary(text[index + 2]))
    }


    // A bounded prose citation is not a function argument list. Require an
    // author name and a bibliographical date, rather than hiding all brackets.
    private val citationEntry = Regex(
        """[ \t]*[\p{Lu}\p{Lt}][\p{L}\p{M}]+[\p{L}\p{M} \t.,'’&\-–]*,[ \t]*(?:[12][0-9]{3}[a-z]?|s\.[ \t]*f\.)(?:[ \t]*,[ \t]*(?:pp?\.|cap\.)[ \t]*[0-9]+(?:[–-][0-9]+)?)?[ \t]*"""
    )

    /** Exclusive end, only in prose. Call after explicit code/math/URL handling.
     * Adjacent calls, single-letter coordinates, unfinished groups and explicit
     * statement tails retain the canonical technical reading. */
    fun proseCitationEnd(text: String, index: Int): Int? {
        if (text.getOrNull(index) != '(' ||
            (index > 0 && !text[index - 1].isWhitespace())) return null
        val limit = minOf(text.length, index + 513)
        var closing = index + 1
        while (closing < limit && text[closing] != ')') {
            if (text[closing] in "(\r\n`\\\"{}[]") return null
            closing++
        }
        if (closing == limit) return null
        // A following C++ statement delimiter/body is not an author citation.
        var after = closing + 1
        while (after < text.length && text[after] in " \t") after++
        if (text.getOrNull(after)?.let { it in ";{[=+*/%<>" } == true) return null
        val entries = text.substring(index + 1, closing).split(';')
        if (entries.isEmpty() || entries.any { !citationEntry.matches(it) }) return null
        return closing + 1
    }

    /** Called only after technical/math preparation. Keep the visible punctuation
     * and use the same sentence boundary as a prose semicolon, without splitting
     * namespaces, calls, quoted symbols or URLs that still occur in the text. */
    fun isProsePause(text: String, index: Int): Boolean = when (text[index]) {
        ':' -> text.getOrNull(index - 1) != ':' && text.getOrNull(index + 1) != ':' &&
            (index + 1 == text.length || text[index + 1].isWhitespace()) &&
            !text.substring(text.lastIndexOf(' ', index - 1) + 1, index).contains("://")
        '(' -> index > 0 && text[index - 1].isWhitespace() &&
            text.getOrNull(index + 1)?.isLetter() == true
        else -> false
    }
}
