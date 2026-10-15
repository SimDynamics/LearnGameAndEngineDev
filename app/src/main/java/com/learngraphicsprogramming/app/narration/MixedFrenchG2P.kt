package com.learngraphicsprogramming.app.narration

import com.learngraphicsprogramming.app.narration.phonemes.FrenchFluidG2P

/** The existing French sentence frontend, with the same reviewed US phones used in Spanish. */
internal object MixedFrenchG2P {
    private val namespaceBefore = Regex("(?<![\\p{L}\\p{N}_])std\\s*::\\s*$")

    // The shared alias for stdexcept is "standard except"; reuse its reviewed first word.
    internal val namespaceUsPhones: String get() = ManualEnglishPronunciations.phones.getValue("stdexcept").substringBefore(' ')
    internal const val namespaceFrenchLetters = "ˈɛs tˈe dˈe"
    internal fun englishPhones(entry: ManualEnglishTerms.Entry): String = ManualEnglishPronunciations.phonemes(entry)
    internal fun namespacePhonesBefore(text: String, span: ManualEnglishSpeech.Span): String? =
        (if (span.entry.id == "cout") namespaceFrenchLetters else namespaceUsPhones)
            .takeIf { namespaceBefore.containsMatchIn(text.substring(0, span.start)) }
    internal fun needsTermBoundary(text: String, span: ManualEnglishSpeech.Span): Boolean {
        if (span.entry.id != "cout") return false
        val next = text.substring(span.endExclusive).dropWhile { it.isWhitespace() || it == '`' }.firstOrNull()
        return next?.isLetterOrDigit() == true || next == ManualEnglishSpeech.START
    }

    fun phonemizePrepared(text: String, french: FrenchFluidG2P): String {
        val input = prepareInput(text, observe = false)
        return french.phonemizeWithForeignTokens(input.text.text, input.foreign)
    }

    /** Exact source offsets for the same continuous French/English phoneme utterance. */
    internal fun traceWordsPrepared(text: String, french: FrenchFluidG2P): FrenchFluidG2P.FrenchPhoneTrace? = runCatching {
        val input = prepareInput(text, observe = true)
        val trace = french.traceWords(input.text.text, input.foreign) ?: return@runCatching null
        val words = mutableListOf<FrenchFluidG2P.FrenchWordPhoneRange>()
        for (word in trace.words) {
            val source = input.text.owners!!.subList(word.sourceStart, word.sourceEndExclusive)
            val start = source.minOf { it.first }
            val end = source.maxOf { it.second }
            val previous = words.lastOrNull()
            if (previous != null && start < previous.sourceEndExclusive) {
                val mergedEnd = maxOf(end, previous.sourceEndExclusive)
                words[words.lastIndex] = previous.copy(word = text.substring(previous.sourceStart, mergedEnd),
                    sourceEndExclusive = mergedEnd, endExclusive = word.endExclusive)
            } else words += word.copy(word = text.substring(start, end), sourceStart = start, sourceEndExclusive = end)
        }
        if (trace.phonemes != phonemizePrepared(text, french)) return@runCatching null
        FrenchFluidG2P.FrenchPhoneTrace(trace.phonemes, words)
    }.getOrNull()

    private data class Owned(val text: String, val owners: List<Pair<Int, Int>>?) {
        fun slice(start: Int, end: Int) = Owned(text.substring(start, end), owners?.subList(start, end))
        fun replace(regex: Regex, replacement: (MatchResult) -> Owned): Owned {
            val output = StringBuilder()
            val ranges = owners?.let { mutableListOf<Pair<Int, Int>>() }
            fun append(part: Owned) { output.append(part.text); ranges?.addAll(part.owners!!) }
            var cursor = 0
            regex.findAll(text).forEach { match ->
                append(slice(cursor, match.range.first)); append(replacement(match)); cursor = match.range.last + 1
            }
            append(slice(cursor, text.length))
            return Owned(output.toString(), ranges)
        }
        fun rendered(start: Int, end: Int, value: String): Owned {
            val range = owners?.subList(start, end)?.let { it.minOf { range -> range.first } to it.maxOf { range -> range.second } }
            return Owned(value, range?.let { List(value.length) { range } })
        }
    }
    private data class Input(val text: Owned, val foreign: Map<Char, String>)

    private fun prepareInput(text: String, observe: Boolean): Input {
        fun raw(start: Int, end: Int) = Owned(text.substring(start, end),
            if (observe) (start until end).map { it to it + 1 } else null)
        fun symbols(input: Owned): Owned {
            if (!observe) return Owned(FrenchSpeechSymbols.normalize(input.text), null)
            val owners = mutableListOf<Pair<Int, Int>>()
            val normalized = FrenchSpeechSymbols.normalizeObserved(input.text) { start, end, rendered ->
                val source = input.owners!!.subList(start, end)
                val range = source.minOf { it.first } to source.maxOf { it.second }
                repeat(rendered.length) { owners += range }
            }
            return Owned(normalized, owners)
        }
        // Durable queues created before this feature retain their original audio/cache contract.
        if (!text.startsWith(ManualEnglishSpeech.PREPARED)) return Input(symbols(raw(0, text.length)), emptyMap())
        val selected = ManualEnglishSpeech.spans(text)
        if (selected.isEmpty()) {
            val original = raw(0, text.length)
            return Input(symbols(original.replace(Regex(ManualEnglishSpeech.PREPARED.toString())) { Owned("", if (observe) emptyList() else null) }), emptyMap())
        }
        require(selected.size <= 6_000) { "Trop de termes techniques dans une phrase" }
        val occupied = text.toHashSet()
        val foreign = linkedMapOf<Char, String>()
        val namespaces = mutableMapOf<Char, String>()
        var next = 0xE100
        fun markerFor(phones: String): Char {
            while (next <= 0xF8FF && next.toChar() in occupied) next++
            require(next <= 0xF8FF) { "Trop de termes techniques dans une phrase" }
            return next++.toChar().also { foreign[it] = phones }
        }
        var last = 0
        val maskedOwners = if (observe) mutableListOf<Pair<Int, Int>>() else null
        val maskedText = buildString {
            fun appendPlain(start: Int, end: Int) {
                for (index in start until end) if (text[index] != ManualEnglishSpeech.PREPARED) {
                    append(text[index]); maskedOwners?.add(index to index + 1)
                }
            }
            for (span in selected) {
                appendPlain(last, span.start)
                val marker = markerFor(englishPhones(span.entry))
                namespacePhonesBefore(text, span)?.let { namespaces[marker] = it }
                append(marker); maskedOwners?.add(span.start to span.endExclusive)
                // A model-native prosodic boundary separates the technical name from prose.
                // It changes no word/voice/rate and never creates or splices another recording.
                if (needsTermBoundary(text, span)) { append(','); maskedOwners?.add(span.start to span.endExclusive) }
                last = span.endExclusive
            }
            appendPlain(last, text.length)
        }
        val masked = Owned(maskedText, maskedOwners)
        // These are notation around selected identifiers, never a second content source.
        // The selected identifiers retain their reviewed US readings; std::cout uses
        // the requested French letter names for its namespace prefix.
        val marker = "[\\uE100-\\uF8FF]"
        val namespaceNotation = masked.replace(Regex("(?<![\\p{L}\\p{N}_])std\\s*::\\s*(?=$marker)")) {
            masked.rendered(it.range.first, it.range.last + 1,
                "${markerFor(namespaces[masked.text[it.range.last + 1]] ?: namespaceUsPhones)} ")
        }
        val directives = namespaceNotation.replace(Regex("#\\s*(?=$marker)")) {
            namespaceNotation.rendered(it.range.first, it.range.last + 1, "dièse ")
        }
        val headers = directives.replace(Regex("<\\s*($marker)\\s*>")) { match ->
            val captured = match.groups[1]!!
            val before = directives.rendered(match.range.first, match.range.first + 1, "chevron ouvrant ")
            val middle = directives.slice(captured.range.first, captured.range.last + 1)
            val after = directives.rendered(match.range.last, match.range.last + 1, " chevron fermant")
            Owned(before.text + middle.text + after.text,
                if (observe) before.owners!! + middle.owners!! + after.owners!! else null)
        }
        val notation = headers.replace(Regex("`")) { Owned("", if (observe) emptyList() else null) }
        return Input(symbols(notation), foreign)
    }
}
