package com.learngraphicsprogramming.app

/** Keeps the existing whitespace tolerance for code, without changing C++ literal contents. */
object CppCodeAnswerNormalizer {
    private val outsideWhitespace = Regex("\\s+")
    private val rawPrefixes = listOf("u8R\"", "uR\"", "UR\"", "LR\"", "R\"")

    /** A fragment inserted between the template's quotes is literal content already. */
    fun normalizeInline(template: String, answers: List<String>): List<String> {
        val literalHoles = literalHoleContexts(template, "[]")
        return answers.mapIndexed { index, answer ->
            if (literalHoles.getOrNull(index) == true) answer else normalize(answer)
        }
    }

    /** fill_code reuses one response in every ___; any literal position makes it significant. */
    fun normalizeFill(code: String, answer: String): String =
        if (literalHoleContexts(code, "___").any { it }) answer else normalize(answer)

    fun normalize(text: String): String {
        val result = StringBuilder(text.length)
        val pendingWhitespace = StringBuilder()
        var index = 0
        var commentEnd = 0
        while (index < text.length) {
            val character = text[index]
            if (character.isWhitespace()) {
                pendingWhitespace.append(character)
                index++
                continue
            }
            // Delay trimming until we know this whitespace is outside a literal.
            if (result.isNotEmpty()) {
                result.append(outsideWhitespace.replace(pendingWhitespace, " "))
            }
            pendingWhitespace.setLength(0)

            if (index >= commentEnd) {
                if (text.startsWith("//", index)) {
                    commentEnd = text.indexOf('\n', index + 2).takeIf { it >= 0 } ?: text.length
                } else if (text.startsWith("/*", index)) {
                    commentEnd = text.indexOf("*/", index + 2).takeIf { it >= 0 }
                        ?.plus(2) ?: text.length
                } else {
                    val rawEnd = rawLiteralEnd(text, index)
                    val tokenEnd = when {
                        rawEnd != null -> rawEnd
                        character == '\"' || character == '\'' -> quotedLiteralEnd(text, index)
                        character.isDigit() || (character == '.' &&
                            text.getOrNull(index + 1)?.isDigit() == true) -> numberEnd(text, index)
                        else -> index + 1
                    }
                    result.append(text, index, tokenEnd)
                    index = tokenEnd
                    continue
                }
            }
            // Quotes in comments do not start literals; comments retain the old tolerance.
            result.append(character)
            index++
        }
        return result.toString()
    }

    private fun literalHoleContexts(template: String, marker: String): List<Boolean> {
        val literalRanges = literalRanges(template)
        val result = mutableListOf<Boolean>()
        var position = template.indexOf(marker)
        while (position >= 0) {
            result.add(literalRanges.any { position in it })
            position = template.indexOf(marker, position + marker.length)
        }
        return result
    }

    internal fun literalRanges(text: String): List<IntRange> {
        val ranges = mutableListOf<IntRange>()
        var index = 0
        while (index < text.length) {
            val character = text[index]
            when {
                text.startsWith("//", index) ->
                    index = text.indexOf('\n', index + 2).takeIf { it >= 0 } ?: text.length
                text.startsWith("/*", index) ->
                    index = text.indexOf("*/", index + 2).takeIf { it >= 0 }
                        ?.plus(2) ?: text.length
                else -> {
                    val rawEnd = rawLiteralEnd(text, index)
                    if (rawEnd != null || character == '\"' || character == '\'') {
                        val end = rawEnd ?: quotedLiteralEnd(text, index)
                        ranges.add(index until end)
                        index = end
                    } else if (character.isDigit() || (character == '.' &&
                        text.getOrNull(index + 1)?.isDigit() == true)) {
                        index = numberEnd(text, index)
                    } else {
                        index++
                    }
                }
            }
        }
        return ranges
    }

    private fun quotedLiteralEnd(text: String, start: Int): Int {
        val quote = text[start]
        var index = start + 1
        while (index < text.length) {
            when (text[index]) {
                '\\' -> index += 2
                quote -> return index + 1
                else -> index++
            }
        }
        // Do not turn incomplete or non-compilable literals into an accepted spelling.
        return text.length
    }

    private fun rawLiteralEnd(text: String, start: Int): Int? {
        if (start > 0 && isIdentifierPart(text[start - 1])) return null
        val prefix = rawPrefixes.firstOrNull { text.startsWith(it, start) } ?: return null
        val delimiterStart = start + prefix.length
        var bodyStart = delimiterStart
        while (bodyStart < text.length && text[bodyStart] != '(') {
            val character = text[bodyStart]
            if (bodyStart - delimiterStart >= 16 || character.isWhitespace() ||
                character == ')' || character == '\\' || character.code !in 0x21..0x7e) {
                return null
            }
            bodyStart++
        }
        if (bodyStart >= text.length) return null
        val delimiter = text.substring(delimiterStart, bodyStart)
        val closing = ")$delimiter\""
        val closingStart = text.indexOf(closing, bodyStart + 1)
        return if (closingStart >= 0) closingStart + closing.length else text.length
    }

    /** Consume a preprocessing number so its digit-separator apostrophes are not quotes. */
    private fun numberEnd(text: String, start: Int): Int {
        var index = start + 1
        while (index < text.length) {
            val character = text[index]
            when {
                isIdentifierPart(character) || character == '.' -> index++
                character == '\'' && text.getOrNull(index + 1)?.let(::isIdentifierPart) == true ->
                    index += 2
                (character == '+' || character == '-') && text[index - 1] in "eEpP" -> index++
                else -> return index
            }
        }
        return index
    }

    private fun isIdentifierPart(character: Char): Boolean =
        character.isLetterOrDigit() || character == '_'
}
