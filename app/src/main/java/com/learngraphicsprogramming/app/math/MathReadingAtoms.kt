package com.learngraphicsprogramming.app.math

/** Verified source ownership for the flat leaves already drawn by the mathematical layout. */
internal object MathReadingAtoms {
    data class Atom(val start: Int, val endExclusive: Int, val expression: MathExpression)

    private val number = Regex("(?:\\d+(?:[.,]\\d+)?|\\.\\d+)")

    /**
     * The parser may reorder or structurally transform complex mathematical notation. Those
     * expressions deliberately have no flat projection. A caller can still track the surrounding
     * prose and retain the complete formula as one source-owned unit.
     */
    fun flat(source: String, expression: MathExpression.Sequence): List<Atom>? {
        val result = mutableListOf<Atom>()
        var cursor = 0
        for (child in expression.children) {
            val literal = when (child) {
                is MathExpression.Number -> child.value
                is MathExpression.Variable -> child.name
                is MathExpression.Symbol -> child.value
                is MathExpression.Operator -> child.value
                is MathExpression.Relation -> child.value
                else -> return null
            }
            // parseContent retains its literal spaces as Symbol leaves; ordinary answer-style
            // parsing drops them. Preserve each case without assigning a guessed source offset.
            val retainsWhitespace = child is MathExpression.Symbol && literal.isNotEmpty() && literal.all(Char::isWhitespace)
            if (!retainsWhitespace) while (cursor < source.length && source[cursor].isWhitespace()) cursor++
            val start = cursor
            val width = when {
                literal.isEmpty() -> return null
                child is MathExpression.Number -> {
                    val match = number.matchAt(source, cursor) ?: return null
                    if (match.value.replace(',', '.') != literal) return null
                    match.value.length
                }
                child is MathExpression.Symbol && literal == "π" && source.startsWith("pi", cursor) -> 2
                source.startsWith(literal, cursor) -> literal.length
                else -> return null
            }
            cursor += width
            if (!boundary(source, start) || !boundary(source, cursor)) return null
            result += Atom(start, cursor, child)
        }
        while (cursor < source.length && source[cursor].isWhitespace()) cursor++
        return result.takeIf { cursor == source.length }
    }

    private fun boundary(source: String, offset: Int): Boolean = offset == 0 || offset == source.length ||
        !(source[offset - 1].isHighSurrogate() && source[offset].isLowSurrogate())
}
