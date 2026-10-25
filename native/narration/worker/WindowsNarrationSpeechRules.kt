package com.learngraphicsprogramming.app.narration

/** Windows reading refinements, applied by the generated host frontend only.
 * The displayed source, Android implementation and stored recordings stay intact. */
internal object WindowsNarrationSpeechRules {
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
