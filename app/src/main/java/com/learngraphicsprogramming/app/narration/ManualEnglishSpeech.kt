package com.learngraphicsprogramming.app.narration

/** Internal pronunciation metadata. It never belongs to authored text or Android TTS input. */
internal object ManualEnglishSpeech {
    const val START = '\uE000'
    const val END = '\uE001'
    const val PREPARED = '\uE002'

    data class Span(val start: Int, val endExclusive: Int, val entry: ManualEnglishTerms.Entry, val code: Boolean = true) {
        val legacyText: String get() = if (code) SpanishTechnicalSpeech.legacyIdentifier(entry.spelling) else entry.spelling
    }

    private val entriesById: Map<String, ManualEnglishTerms.Entry> by lazy {
        require(ManualEnglishTerms.entries.all { it.id.length in 1..64 && it.id.matches(Regex("[A-Za-z][A-Za-z0-9_]*")) }) {
            "Manual English term IDs must remain atomic during sentence segmentation"
        }
        ManualEnglishTerms.entries.associateBy { it.id }.also {
            require(it.size == ManualEnglishTerms.entries.size) { "Duplicate manual English term ID" }
        }
    }

    fun encode(entry: ManualEnglishTerms.Entry, code: Boolean = true): String {
        require(entriesById[entry.id] == entry) { "Unknown manual English term" }
        return "$START${entry.id}${if (code) "" else "~p"}$END"
    }

    fun isPrepared(text: String): Boolean = PREPARED in text

    /** Keep an annotation intact when bounded text splitting has no whitespace to use. */
    fun safeBoundary(text: String, cut: Int): Int {
        require(cut in 0..text.length)
        val annotation = spans(text).firstOrNull { cut > it.start && cut < it.endExclusive } ?: return cut
        // In normal 700/2,000-character segments every annotation is shorter than the
        // limit. A caller with a smaller limit still keeps its first atom intact.
        return if (annotation.start > 0) annotation.start else annotation.endExclusive
    }

    /** Metadata has no spoken length: split the legacy text, then project its boundaries. */
    fun segment(text: String, splitVisible: (String) -> List<String>): List<String> {
        val prepared = text.startsWith(PREPARED)
        val source = text.removePrefix(PREPARED.toString())
        val annotations = spans(source)
        val visible = strip(source)
        val chunks = splitVisible(visible)
        if (annotations.isEmpty()) return chunks.map { if (prepared) PREPARED + it else it }
        val output = mutableListOf<String>()
        var sourceStart = 0
        var visibleStart = 0
        fun sourceOffset(cut: Int): Int {
            var raw = 0
            var spoken = 0
            for (span in annotations) {
                val plainLength = span.start - raw
                if (cut <= spoken + plainLength) return raw + cut - spoken
                spoken += plainLength
                val end = spoken + span.legacyText.length
                if (cut < end) {
                    // A deliberately tiny limit must still retain one whole technical term.
                    return if (span.start > sourceStart) span.start else span.endExclusive
                }
                if (cut == end) return span.endExclusive
                spoken = end
                raw = span.endExclusive
            }
            return raw + cut - spoken
        }
        for ((index, chunk) in chunks.withIndex()) {
            val found = visible.indexOf(chunk, visibleStart)
            require(found >= 0) { "Narration segmentation must preserve spoken text" }
            visibleStart = found + chunk.length
            val end = if (index == chunks.lastIndex) source.length else sourceOffset(visibleStart)
            if (end > sourceStart) {
                val part = source.substring(sourceStart, end).trim()
                if (part.isNotEmpty()) output += if (prepared) PREPARED + part else part
                sourceStart = end
            }
        }
        return output
    }

    /** A closed, validated vocabulary prevents user text from injecting arbitrary phonemes. */
    fun spans(text: String): List<Span> {
        val result = mutableListOf<Span>()
        var index = 0
        while (index < text.length) {
            when (text[index]) {
                START -> {
                    val end = text.indexOf(END, index + 1)
                    require(end >= 0 && text.indexOf(START, index + 1).let { it < 0 || it > end }) {
                        "Incomplete manual English pronunciation annotation"
                    }
                    val encodedId = text.substring(index + 1, end)
                    val prose = encodedId.endsWith("~p")
                    val entry = entriesById[if (prose) encodedId.removeSuffix("~p") else encodedId]
                        ?: throw IllegalArgumentException("Unknown manual English pronunciation annotation")
                    result += Span(index, end + 1, entry, code = !prose)
                    index = end + 1
                }
                END -> throw IllegalArgumentException("Unexpected manual English pronunciation delimiter")
                else -> index++
            }
        }
        return result
    }

    /** Recover the unchanged, legacy spoken spelling for fallbacks and compatible cache keys. */
    fun strip(text: String): String {
        val annotations = spans(text)
        val output = StringBuilder(text.length)
        var index = 0
        for (span in annotations) {
            appendWithoutPrepared(output, text, index, span.start)
            output.append(span.legacyText)
            index = span.endExclusive
        }
        appendWithoutPrepared(output, text, index, text.length)
        return output.toString()
    }

    /**
     * Normalize only Spanish text: each selected term becomes one opaque character.
     * Placeholders never collide with existing characters and are restored atomically.
     * The preparation sentinel is kept outside the transform so it cannot inhibit math parsing.
     */
    fun protect(text: String, transform: (String) -> String): String {
        val annotations = spans(text)
        val wasPrepared = isPrepared(text)
        val occupied = text.toHashSet()
        val placeholders = linkedMapOf<String, Char>()
        val originals = linkedMapOf<Char, String>()
        var nextPlaceholder = 0xE100
        fun placeholder(annotation: String): Char = placeholders.getOrPut(annotation) {
            while (nextPlaceholder <= 0xF8FF && nextPlaceholder.toChar() in occupied) nextPlaceholder++
            require(nextPlaceholder <= 0xF8FF) { "Too many manual English pronunciation placeholders" }
            val symbol = nextPlaceholder++.toChar()
            originals[symbol] = annotation
            symbol
        }
        val protected = StringBuilder(text.length)
        var index = 0
        for (span in annotations) {
            appendWithoutPrepared(protected, text, index, span.start)
            protected.append(placeholder(text.substring(span.start, span.endExclusive)))
            index = span.endExclusive
        }
        appendWithoutPrepared(protected, text, index, text.length)
        val transformed = transform(protected.toString())
        return buildString(transformed.length + if (wasPrepared) 1 else 0) {
            if (wasPrepared) append(PREPARED)
            transformed.forEach { symbol -> append(originals[symbol] ?: symbol.toString()) }
        }
    }

    private fun appendWithoutPrepared(output: StringBuilder, text: String, start: Int, end: Int) {
        for (index in start until end) if (text[index] != PREPARED) output.append(text[index])
    }
}
