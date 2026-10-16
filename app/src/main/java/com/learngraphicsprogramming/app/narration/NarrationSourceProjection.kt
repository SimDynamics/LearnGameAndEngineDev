package com.learngraphicsprogramming.app.narration

import com.learngraphicsprogramming.app.math.MathTextRenderer

/** Temporary offsets into the existing view. No curriculum or audio is stored here. */
internal object NarrationSourceProjection {
    data class Projection(val text: String, val owners: List<ReadingTextRange>,
                          val formulas: List<ReadingTextRange> = emptyList())
    /** Offsets only while joining existing puzzle labels; no Android rendering is duplicated. */
    data class Source(val text: String, val formulas: List<ReadingTextRange>) : CharSequence by text {
        override fun toString(): String = text
    }
    private data class Piece(val start: Int, val end: Int, val text: String)

    fun spanish(source: String, mode: String): Projection? = forLanguage(source, mode, NarrationLanguage.SPANISH)

    /** Typesetting removes authored math delimiters. Recover only that existing semantic intent
     * in a temporary normalization projection, retaining offsets into the displayed original. */
    fun mathematicalSource(source: CharSequence): Projection {
        val formulas = formulaRanges(source)
        if (formulas.isEmpty()) return Projection(source.toString(), source.indices.map { ReadingTextRange(it, it + 1) })
        val text = StringBuilder()
        val owners = mutableListOf<ReadingTextRange>()
        var cursor = 0
        fun append(end: Int) {
            while (cursor < end) {
                text.append(source[cursor])
                owners += ReadingTextRange(cursor, cursor + 1)
                cursor++
            }
        }
        formulas.forEach { range ->
            append(range.start)
            text.append("\\(")
            repeat(2) { owners += ReadingTextRange(0, 0) }
            append(range.endExclusive)
            text.append("\\)")
            repeat(2) { owners += ReadingTextRange(0, 0) }
        }
        append(source.length)
        return Projection(text.toString(), owners)
    }

    fun formulaRanges(source: CharSequence): List<ReadingTextRange> =
        if (source is Source) source.formulas else MathTextRenderer.formulaRanges(source)
            .sortedBy { it.first }.map { ReadingTextRange(it.first, it.last + 1) }

    fun forLanguage(source: CharSequence, mode: String, language: NarrationLanguage,
                    restoreFormulaIntent: Boolean = true): Projection? = runCatching {
        val initial = if (restoreFormulaIntent) mathematicalSource(source)
            else Projection(source.toString(), source.indices.map { ReadingTextRange(it, it + 1) })
        val technical = when (language) {
            NarrationLanguage.SPANISH -> stage(initial) { observe -> SpanishTechnicalSpeech.prepareForReading(initial.text, mode, observe).text }
            NarrationLanguage.FRENCH -> stage(initial) { observe -> FrenchTechnicalSpeech.prepareForReading(initial.text, mode, observe) }
            else -> initial
        }
            ?: return null
        if (language == NarrationLanguage.AMERICAN_ENGLISH || language == NarrationLanguage.BRITISH_ENGLISH)
            return stage(technical) { observe -> MultilingualMathSpeech.textObserved(technical.text, mode, language, observe) }
        var mathematical: Projection? = null
        val restored = ManualEnglishSpeech.protect(technical.text) { masked ->
            val maskedOwners = mutableListOf<ReadingTextRange>()
            val originals = mutableMapOf<Char, String>()
            var cursor = 0
            fun plain(end: Int) {
                for (index in cursor until end) if (technical.text[index] != ManualEnglishSpeech.PREPARED) {
                    check(masked[maskedOwners.size] == technical.text[index])
                    maskedOwners += technical.owners[index]
                }
            }
            ManualEnglishSpeech.spans(technical.text).forEach { span ->
                plain(span.start)
                val marker = masked[maskedOwners.size]
                originals[marker] = technical.text.substring(span.start, span.endExclusive)
                maskedOwners += owner(technical.owners.subList(span.start, span.endExclusive))
                cursor = span.endExclusive
            }
            plain(technical.text.length)
            check(maskedOwners.size == masked.length)
            val math = stage(Projection(masked, maskedOwners)) { observe ->
                MultilingualMathSpeech.textObserved(masked, mode, language, observe)
            } ?: error("Unverifiable math provenance")
            val output = StringBuilder()
            val owners = mutableListOf<ReadingTextRange>()
            if (ManualEnglishSpeech.isPrepared(technical.text)) {
                output.append(ManualEnglishSpeech.PREPARED)
                owners += ReadingTextRange(0, 0)
            }
            math.text.forEachIndexed { index, symbol ->
                val value = originals[symbol] ?: symbol.toString()
                output.append(value)
                repeat(value.length) { owners += math.owners[index] }
            }
            mathematical = Projection(output.toString(), owners)
            math.text
        }
        mathematical?.takeIf { it.text == restored }
    }.getOrNull()

    /** Observe replacements in the real normalizer, then project its whitespace-only cleanup. */
    private fun stage(input: Projection, transform: ((Int, Int, String) -> Unit) -> String): Projection? {
        val pieces = mutableListOf<Piece>()
        val result = transform { start, end, text -> pieces += Piece(start, end, text) }
        if (pieces.isEmpty()) return input.takeIf { it.text == result }
        val raw = StringBuilder()
        val origins = mutableListOf<ReadingTextRange>()
        pieces.forEach { piece ->
            if (piece.start !in 0..input.text.length || piece.end !in piece.start..input.text.length) return null
            raw.append(piece.text)
            if (piece.text == input.text.substring(piece.start, piece.end)) {
                origins += input.owners.subList(piece.start, piece.end)
            } else {
                val source = owner(input.owners.subList(piece.start, piece.end))
                repeat(piece.text.length) { origins += source }
            }
        }
        var cursor = 0
        val cleaned = mutableListOf<ReadingTextRange>()
        result.forEach { symbol ->
            if (symbol.isWhitespace()) cleaned += ReadingTextRange(0, 0)
            else {
                while (cursor < raw.length && raw[cursor].isWhitespace()) cursor++
                if (cursor == raw.length || raw[cursor] != symbol) return null
                cleaned += origins[cursor++]
            }
        }
        if (raw.substring(cursor).any { !it.isWhitespace() }) return null
        return Projection(result, cleaned)
    }

    fun owner(ranges: List<ReadingTextRange>): ReadingTextRange {
        val visible = ranges.filter { it.endExclusive > it.start }
        return if (visible.isEmpty()) ReadingTextRange(0, 0)
        else ReadingTextRange(visible.minOf { it.start }, visible.maxOf { it.endExclusive })
    }
}
