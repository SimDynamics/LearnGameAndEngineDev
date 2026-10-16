package com.learngraphicsprogramming.app.narration

/** Presentation-only C++ ranges over the original text, shared by all programming lessons. */
internal object ProgrammingTechnicalRanges {
    data class Range(val start: Int, val endExclusive: Int)
    data class Analysis(val fragments: List<Range>, val wrappers: List<Range>)

    private val identifier = "[\\p{L}_][\\p{L}\\p{M}\\p{N}_]*"
    private val url = Regex("(?i)(?:https?://|www\\.)[^\\s`]+")
    private val math = Regex("""\\\([\s\S]*?\\\)|\\\[[\s\S]*?\\\]|[$][$][\s\S]*?[$][$]|[$][^$\r\n]+[$]""")
    private val include = Regex("(?<![\\p{L}\\p{N}_])#\\s*include\\b(?:[ \\t]*(?:<[^>\\r\\n]+>|\"(?:\\\\.|[^\"\\\\\\r\\n])*\"))?")
    private val directive = Regex("(?<![\\p{L}\\p{N}_])#\\s*(?:define|undef|ifdef|ifndef|endif|elif|else|pragma|error|warning)\\b(?:[ \\t]+$identifier)?")
    private val qualified = Regex("(?<![\\p{L}\\p{M}\\p{N}_])std[ \\t]*::(?:$identifier(?:::$identifier)*)?")
    private val languageName = Regex("(?<![\\p{L}\\p{N}_])[Cc]\\+\\+(?:\\d+)?(?![\\p{L}\\p{N}_])")
    private val declaration = Regex("(?<![\\p{L}\\p{N}_])(?:(?:const|constexpr|static|volatile|unsigned|signed)\\s+)*(?:auto|bool|char|char8_t|char16_t|char32_t|wchar_t|short|int|long|float|double|size_t)(?:\\s+|(?=[*&]))(?:[*&][ \\t]*)*$identifier")
    private val returnStatement = Regex("(?<![\\p{L}\\p{N}_])return\\s+(?:[−+-]?\\d+(?:[.]\\d+)?|true|false|nullptr|\"(?:\\\\.|[^\"\\\\])*\")")
    private val call = Regex("(?<![\\p{L}\\p{N}_])($identifier)\\(")
    private val compoundExpression = Regex("(?<![\\p{L}\\p{N}_])(?:$identifier|\\d+)\\s*(?:&&|\\|\\||==|!=|<<|>>|[+*/%&|^-]=)\\s*(?:$identifier|[+-]?\\d+(?:[.]\\d+)?)(?:[ \\t]*;)?")
    private val assignment = Regex("(?<![\\p{L}\\p{N}_])$identifier[ \\t]*=(?!=)")
    private val control = Regex("(?<![\\p{L}\\p{N}_])(?:if|for|while|switch|catch)[ \\t]*\\(")
    private val namedType = Regex("std[ \\t]*::(?:vector|array|string|string_view|map|set|optional|pair|size_t)$")
    private val declarator = Regex("(?:[ \\t]+|(?=[*&]))(?:[*&][ \\t]*)*$identifier")
    private val englishRole = Regex("(?i)\\b(?:function|method|operator|keyword|namespace|type|class|header|library|directive|identifier|variable|object|pointer|reference|entry point|body of|returned by)\\s+(?:(?:named|called)\\s+)?$")
    private val englishRoleAfter = Regex("(?i)^\\s+(?:function|method|operator|keyword|type|header|directive|identifier)\\b|^\\s+is\\s+(?:the|an?)\\s+entry point\\b")
    private val proseJoiner = Regex("(?i)\\s+(?:y|o|pero|porque|and|or|but|because|which|et|ou|mais|car|puis|qui|que)\\b")
    private val symbolCue = Regex("(?iu)\\b(?:símbolo|signo|carácter|operador|marca|symbol|sign|character|operator|token|symbole|signe|caractère|opérateur)s?\\s*$")
    private val symbolListCue = Regex("(?iu)\\b(?:símbolo|signo|carácter|operador|marca|symbol|sign|character|operator|token|symbole|signe|caractère|opérateur)s?\\b[^.!?;\\r\\n]{0,64}$")
    private val distinctiveEnglish = setOf("iostream", "cout", "cin", "cerr", "endl", "nullptr", "constexpr", "noexcept", "size_t", "bool", "int", "wchar_t")
    private val excludedCalls = setOf("sin", "cos", "tan", "log", "ln", "sqrt", "exp", "f", "g", "h")

    fun analyze(source: CharSequence, language: NarrationLanguage): Analysis {
        val text = source.toString()
        if (text.isEmpty()) return Analysis(emptyList(), emptyList())
        val mathematical = math.findAll(text).map { it.range }.toList()
        val links = url.findAll(text).map { it.range }.toList()
        val fragments = mutableListOf<Range>()
        val wrappers = mutableListOf<Range>()
        val englishNotation by lazy { EnglishTechnicalSpeech.replacements(text) }
        fun overlaps(start: Int, end: Int, ranges: List<IntRange>) = ranges.any { it.first < end && it.last + 1 > start }
        fun add(start: Int, end: Int, explicit: Boolean = false) {
            if (start < 0 || end > text.length || start >= end || overlaps(start, end, mathematical) ||
                (!explicit && overlaps(start, end, links)) || text.substring(start, end).isBlank()) return
            fragments += Range(start, end)
        }

        var cursor = 0
        while (cursor < text.length) {
            if (text[cursor] != '`' || overlaps(cursor, cursor + 1, mathematical + links)) { cursor++; continue }
            val delimiter = if (text.startsWith("```", cursor)) "```" else "`"
            val end = text.indexOf(delimiter, cursor + delimiter.length)
            if (end < 0) { cursor += delimiter.length; continue }
            if (text.substring(cursor + delimiter.length, end).isNotBlank() && !overlaps(cursor, end, mathematical)) {
                add(cursor + delimiter.length, end, explicit = true)
                wrappers += Range(cursor, cursor + delimiter.length)
                wrappers += Range(end, end + delimiter.length)
            }
            cursor = end + delimiter.length
        }

        include.findAll(text).forEach { add(it.range.first, it.range.last + 1) }
        directive.findAll(text).forEach { add(it.range.first, it.range.last + 1) }
        languageName.findAll(text).forEach { add(it.range.first, it.range.last + 1) }
        qualified.findAll(text).forEach { match -> add(match.range.first, suffixEnd(text, match.range.last + 1)) }
        EnglishTechnicalSpeech.qualifiedOutputMatches(text).forEach { add(it.start, suffixEnd(text, it.endExclusive)) }

        val selected = when (language) {
            NarrationLanguage.FRENCH -> ManualEnglishTerms.selectFrench(text)
            else -> ManualEnglishTerms.select(text)
        }
        selected.forEach { match ->
            val before = text.substring((match.start - 100).coerceAtLeast(0), match.start)
            val after = text.substring(match.endExclusive, (match.endExclusive + 100).coerceAtMost(text.length))
            val english = language == NarrationLanguage.AMERICAN_ENGLISH || language == NarrationLanguage.BRITISH_ENGLISH
            if (!english || match.entry.id in distinctiveEnglish || '_' in match.entry.spelling ||
                englishRole.containsMatchIn(before) || englishRoleAfter.containsMatchIn(after) ||
                after.startsWith('(') || (before.endsWith('<') && after.startsWith('>'))) {
                add(match.start, suffixEnd(text, match.endExclusive))
            }
        }

        // Existing normalizers establish which punctuation is a technical mention.
        // They are observed only: no pronunciation, lesson text or cached audio changes.
        if (language == NarrationLanguage.SPANISH) {
            SpanishTechnicalSpeech.prepareForReading(text) { start, end, spoken ->
                if (start < end && spoken != text.substring(start, end) &&
                    text.substring(start, end).any { it in "#{}[];&|_^~\\<>+*/%=-!" }) add(start, end)
            }
        } else if (language == NarrationLanguage.FRENCH) {
            FrenchSpeechSymbols.resolvedOperatorReadings(text).filter { it.words != null }.forEach { add(it.start, it.endExclusive) }
            englishNotation.filter { text.substring(it.start, it.endExclusive) in setOf("#", "{", "}", "[", "]") }
                .forEach { add(it.start, it.endExclusive) }
        } else {
            englishNotation.forEach { part ->
                val symbol = text.substring(part.start, part.endExclusive)
                val before = text.substring((part.start - 64).coerceAtLeast(0), part.start)
                if (part.phonemes != null || symbol in setOf("#", "{", "}", "[", "]", "::", "++", "--", "&&", "||", "==", "!=", "<<", ">>", "->", ".*", "->*") ||
                    "operator" in part.spoken || part.spoken == "semicolon" || symbolCue.containsMatchIn(before)) add(part.start, part.endExclusive)
            }
        }
        // Metalinguistic sign lists have a shared visual meaning even when a
        // language's normalizer leaves that single glyph as ordinary punctuation.
        englishNotation.forEach { part ->
            val symbol = text.substring(part.start, part.endExclusive)
            if (symbol.none { it.isLetterOrDigit() } && symbol != ";" &&
                symbolListCue.containsMatchIn(text.substring((part.start - 80).coerceAtLeast(0), part.start))) add(part.start, part.endExclusive)
        }

        declaration.findAll(text).forEach { match ->
            val prefix = text.substring(0, match.range.first).substringAfterLast('\n').trimEnd()
            val weak = match.value.substringBefore(' ') in setOf("long", "short", "double", "float")
            val next = skipSpaces(text, match.range.last + 1)
            if (text.getOrNull(next) !in listOf('=', ';', '{', '(', '[', ',')) return@forEach
            if (!weak || prefix.isEmpty() || prefix.last() in ":;{`" || englishRole.containsMatchIn(prefix) || text.getOrNull(next) in listOf('=', '{', '(', '*', '&')) {
                val end = statementEnd(text, match.range.last + 1)
                if (end != null) add(match.range.first, end)
            }
        }
        compoundExpression.findAll(text).forEach { add(it.range.first, it.range.last + 1) }
        assignment.findAll(text).forEach { match ->
            statementEnd(text, match.range.last + 1)?.let { add(match.range.first, it) }
        }
        control.findAll(text).forEach { match ->
            val condition = balancedEnd(text, match.range.last, '(', ')') ?: return@forEach
            val body = skipSpaces(text, condition)
            val end = if (text.getOrNull(body) == '{') balancedEnd(text, body, '{', '}') else statementEnd(text, body)
            if (end != null) add(match.range.first, end)
        }
        returnStatement.findAll(text).forEach { match -> add(match.range.first,
            if (text.getOrNull(skipSpaces(text, match.range.last + 1)) == ';') skipSpaces(text, match.range.last + 1) + 1 else match.range.last + 1) }
        call.findAll(text).filter { it.groupValues[1] !in excludedCalls }.forEach { match ->
            val end = balancedEnd(text, match.range.last, '(', ')') ?: return@forEach
            val next = skipSpaces(text, end)
            // A terminator is decisive for unknown function names; selected library
            // names and calls inside explicit code already provide their own seed.
            if (text.getOrNull(next) == ';') add(match.range.first, next + 1)
        }

        val merged = merge(text, fragments)
        val clean = merged.flatMap { fragment ->
            var start = fragment.start
            val parts = mutableListOf<Range>()
            wrappers.filter { it.start < fragment.endExclusive && it.endExclusive > fragment.start }.sortedBy { it.start }.forEach { wrapper ->
                if (start < wrapper.start) parts += Range(start, wrapper.start)
                start = maxOf(start, wrapper.endExclusive)
            }
            if (start < fragment.endExclusive) parts += Range(start, fragment.endExclusive)
            parts
        }.filter { text.substring(it.start, it.endExclusive).isNotBlank() }
        return Analysis(clean, wrappers.sortedBy { it.start })
    }

    private fun suffixEnd(text: String, initial: Int): Int {
        var end = initial
        var next = skipSpaces(text, end)
        if (text.getOrNull(next) == '<') balancedEnd(text, next, '<', '>')?.let { end = it; next = skipSpaces(text, end) }
        val isNamedType = namedType.containsMatchIn(text.substring((initial - 64).coerceAtLeast(0), initial))
        if (isNamedType) declarator.matchAt(text, end)?.let { match ->
            val following = skipSpaces(text, match.range.last + 1)
            if (text.getOrNull(following) in listOf('=', ';', '{', '('))
                statementEnd(text, match.range.last + 1)?.let { return it }
        }
        while (text.getOrNull(next) in listOf('(', '[', '{') && (next == end || text[next] == '{')) {
            val open = text[next]
            val close = when (open) { '(' -> ')'; '[' -> ']'; else -> '}' }
            end = balancedEnd(text, next, open, close) ?: break
            next = skipSpaces(text, end)
        }
        if (text.getOrNull(next) == ';') return next + 1
        if (listOf("<<", ">>", "+=", "-=", "*=", "/=", "=", "++", "--").any { text.startsWith(it, next) })
            statementEnd(text, next)?.let { return it }
        return end
    }

    private fun statementEnd(text: String, start: Int): Int? {
        var index = start
        val limit = minOf(text.length, start + 512)
        while (index < limit) {
            if (text[index] == ';') return index + 1
            val memberDot = text[index] == '.' && (text.getOrNull(index + 1)?.isDigit() == true ||
                (text.getOrNull(index - 1)?.let { it.isLetterOrDigit() || it in "_)]" } == true &&
                    text.getOrNull(index + 1)?.let { it.isLetterOrDigit() || it == '_' } == true))
            if (text[index] in "\r\n!?" || (text[index] == '.' && !memberDot) ||
                (text[index].isWhitespace() && proseJoiner.matchAt(text, index) != null)) return null
            if (text[index] == '"' || text[index] == '\'') {
                val quote = text[index++]
                while (index < limit && text[index] != quote) { if (text[index] == '\\') index++; index++ }
                if (index >= limit) return null
            }
            if (text[index] in "([{<") {
                val opening = text[index]
                val close = when (text[index]) { '(' -> ')'; '[' -> ']'; '{' -> '}'; else -> '>' }
                val balanced = balancedEnd(text, index, text[index], close)
                if (balanced != null) {
                    if (opening == '{') {
                        val next = skipSpaces(text, balanced)
                        return if (text.getOrNull(next) == ';') next + 1 else balanced
                    }
                    index = balanced
                    continue
                }
            }
            index++
        }
        return null
    }

    private fun balancedEnd(text: String, start: Int, opening: Char, closing: Char): Int? {
        var depth = 0
        var index = start
        val limit = minOf(text.length, start + 512)
        while (index < limit) {
            if (text[index] in "\"'") {
                val quote = text[index++]
                while (index < limit && text[index] != quote) { if (text[index] == '\\') index++; index++ }
                if (index >= limit) return null
            } else {
                if (text[index] == opening) depth++
                if (text[index] == closing && --depth == 0) return index + 1
                if (opening == '<' && text[index] in "\r\n") return null
            }
            index++
        }
        return null
    }

    private fun skipSpaces(text: String, start: Int): Int {
        var index = start
        while (index < text.length && text[index] in " \t") index++
        return index
    }

    private fun merge(text: String, ranges: List<Range>): List<Range> {
        val result = mutableListOf<Range>()
        ranges.sortedWith(compareBy<Range> { it.start }.thenBy { it.endExclusive }).forEach { range ->
            val previous = result.lastOrNull()
            if (previous != null && (range.start <= previous.endExclusive || text.substring(previous.endExclusive, range.start).all { it in " \t" }))
                result[result.lastIndex] = Range(previous.start, maxOf(previous.endExclusive, range.endExclusive))
            else result += range
        }
        return result
    }
}
