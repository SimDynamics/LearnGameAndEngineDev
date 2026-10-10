package com.learngraphicsprogramming.app.narration

/** Segmentos acotados sin cortar pares UTF-16 ni exceder el límite del motor. */
internal object NarrationSegments {
    fun prepare(texts: List<String>, maxLength: Int): List<String> {
        require(maxLength >= 2)
        return texts.flatMap { split(it.trim(), maxLength) }
    }

    /**
     * Keeps paragraphs in their original order, but synthesizes complete
     * sentences early. No words are deleted, even for decimals like 3.14.
     * Very long sentences remain bounded for language-specific phonemizers;
     * Kokoro's actual 510-phoneme limit is enforced after G2P.
     */
    fun sentences(texts: List<String>): List<String> =
        texts.flatMap { paragraph ->
            val source = paragraph.trim()
            if (source.isEmpty()) return@flatMap emptyList()
            val result = mutableListOf<String>()
            var start = 0
            source.forEachIndexed { index, symbol ->
                if (symbol !in ".!?;" ||
                    (symbol == '.' && index > 0 && index + 1 < source.length &&
                        source[index - 1].isDigit() && source[index + 1].isDigit()) ||
                    (index + 1 < source.length && !source[index + 1].isWhitespace())
                ) return@forEachIndexed
                val sentence = source.substring(start, index + 1).trim()
                if (sentence.isNotEmpty()) result += sentence
                start = index + 1
            }
            if (start < source.length) {
                val remainder = source.substring(start).trim()
                if (remainder.isNotEmpty()) result += remainder
            }
            result.flatMap { split(it, 2_000) }
        }

    private fun split(text: String, maxLength: Int): List<String> {
        val result = mutableListOf<String>()
        var remaining = text
        while (remaining.length > maxLength) {
            var end = maxLength
            if (remaining[end - 1].isHighSurrogate() && remaining[end].isLowSurrogate()) end--
            val sentenceEnd = (end - 1 downTo 0).firstOrNull { candidate ->
                val symbol = remaining[candidate]
                symbol in ".!?;" && !(symbol == '.' && candidate > 0 &&
                    candidate + 1 < remaining.length && remaining[candidate - 1].isDigit() &&
                    remaining[candidate + 1].isDigit())
            } ?: -1
            val whitespace = remaining.substring(0, end).indexOfLast { it.isWhitespace() }
            end = when {
                sentenceEnd >= end / 2 -> sentenceEnd + 1
                whitespace >= end / 2 -> whitespace
                else -> end
            }
            result += remaining.substring(0, end).trim()
            remaining = remaining.substring(end).trimStart()
        }
        if (remaining.isNotBlank()) result += remaining
        return result.filter { it.isNotBlank() }
    }
}
