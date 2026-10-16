package com.learngraphicsprogramming.app.math

import com.learngraphicsprogramming.app.narration.SpanishMathSpeech

/** Mathematical source ownership for decoration only; no rendering or narration is changed. */
internal object MathTextSemantics {
    data class Range(val start: Int, val endExclusive: Int)

    private const val MATHEMATICAL = "\u0000mathematical-source\u0000"
    // The renderer deliberately leaves a prose enumeration as ordinary text.
    private val integerEnumeration = Regex("[−-]?[0-9]+(?:\\s*,\\s*[−-]?[0-9]+)*\\s+y\\s+[−-]?[0-9]+")
    private const val NUMBER = "[−+-]?[0-9]+(?:[.,][0-9]+)?"
    // A renderer/speech fragment may end before the relation in 6 × 100 = 600.
    // Decoration owns that complete numeric chain, without changing either fragment.
    private val numericChain = Regex("$NUMBER(?:\\s*[×÷·+−*/=<>≤≥≠≈-]\\s*$NUMBER)+")
    // Keep the established detector's code, URL, path and date exclusions. Explicit
    // formulas remain entirely owned by that detector, including parse failures.
    private val protectedSource = Regex(
        "https?://\\S+|[A-Za-z]:[/\\\\]\\S+|(?<![\\p{L}\\p{N}\\\\])/\\S+|" +
            "[A-Za-z_][\\w.-]*[/\\\\][\\w./\\\\-]+|\\d{1,4}/\\d{1,2}/\\d{1,4}|" +
            "```[\\s\\S]*?```|`[^`]*`|\\\\\\([\\s\\S]*?\\\\\\)|\\\\\\[[\\s\\S]*?\\\\\\]|" +
            "\\$\\$[\\s\\S]*?\\$\\$|\\$[^$]*\\$"
    )

    /** Balanced formatting delimiters only; callers preserve formula-owned source spans. */
    fun wrappers(source: CharSequence): List<Range> {
        val text = source.toString()
        val result = mutableListOf<Range>()
        fun escaped(position: Int): Boolean {
            var index = position - 1
            while (index >= 0 && text[index] == '\\') index--
            return (position - index - 1) % 2 != 0
        }
        var cursor = 0
        while (cursor < text.length) {
            if (text[cursor] != '`' || escaped(cursor)) { cursor++; continue }
            var openingEnd = cursor + 1
            while (openingEnd < text.length && text[openingEnd] == '`') openingEnd++
            val width = openingEnd - cursor
            if (width > 3) { cursor = openingEnd; continue }
            val delimiter = "`".repeat(width)
            var closing = text.indexOf(delimiter, openingEnd)
            while (closing >= 0 && (escaped(closing) || text.getOrNull(closing - 1) == '`' ||
                    text.getOrNull(closing + width) == '`')) closing = text.indexOf(delimiter, closing + width)
            if (closing >= 0) {
                val body = text.substring(openingEnd, closing)
                if (body.isNotBlank() && (width == 3 || body.none { it == '\n' || it == '\r' })) {
                    result += Range(cursor, openingEnd)
                    result += Range(closing, closing + width)
                    cursor = closing + width
                    continue
                }
            }
            cursor = openingEnd
        }
        return result
    }

    /**
     * Uses the established bounded detector and AST parser, with a language-neutral visitor.
     * Offsets are UTF-16 into the supplied text, including any existing explicit delimiters.
     * For an already typeset TextView, callers also retain its existing formula-span ranges:
     * those spans own their source even when typesetting removed the explicit delimiters.
     */
    fun ranges(source: CharSequence, mode: String = "auto"): List<Range> {
        val text = source.toString()
        if (text.isBlank() || mode == "text" ||
            (mode == "auto" && integerEnumeration.matches(text.trim()))) return emptyList()
        val found = mutableListOf<Range>()
        SpanishMathSpeech.renderMathText(text, mode, { MATHEMATICAL }, "") { start, end, rendered ->
            if (rendered.trim() != MATHEMATICAL) return@renderMathText
            var first = start
            var last = end
            while (first < last && text[first].isWhitespace()) first++
            while (last > first && text[last - 1].isWhitespace()) last--
            if (first < last) found += Range(first, last)
        }
        if (mode == "auto") {
            val protected = protectedSource.findAll(text).map { it.range }.toList()
            numericChain.findAll(text).forEach { candidate ->
                val first = candidate.range.first
                val end = candidate.range.last + 1
                if (safeNumericBoundary(text, first - 1) && safeNumericBoundary(text, end) &&
                    protected.none { it.first < end && it.last >= first } &&
                    MathLatexParser.parse(candidate.value) is MathLatexParser.Result.Success) {
                    found += Range(first, end)
                }
            }
        }
        val result = mutableListOf<Range>()
        found.sortedWith(compareBy<Range> { it.start }.thenBy { it.endExclusive }).forEach { range ->
            val previous = result.lastOrNull()
            if (previous != null && range.start <= previous.endExclusive) {
                result[result.lastIndex] = previous.copy(endExclusive = maxOf(previous.endExclusive, range.endExclusive))
            } else result += range
        }
        return result
    }

    private fun safeNumericBoundary(text: String, index: Int): Boolean {
        if (index !in text.indices) return true
        if (text[index].isLetterOrDigit() || text[index] in "/\\_:") return false
        // A sentence stop is outside the expression; a decimal/version continuation
        // must not let a suffix of an ordinary identifier become a new expression.
        return text[index] != '.' ||
            !(text.getOrNull(index - 1)?.isDigit() == true && text.getOrNull(index + 1)?.isDigit() == true)
    }
}
