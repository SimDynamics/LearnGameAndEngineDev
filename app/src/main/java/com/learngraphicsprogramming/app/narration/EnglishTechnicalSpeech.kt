package com.learngraphicsprogramming.app.narration

/** Scoped code pronunciation, observed over the original narration source. */
internal object EnglishTechnicalSpeech {
    /** An exact source atom; all words in a symbol's name retain this original range. */
    data class Replacement(val start: Int, val endExclusive: Int, val spoken: String, val phonemes: String? = null)

    data class QualifiedOutput(
        val start: Int,
        val endExclusive: Int,
        val stdStart: Int,
        val stdEndExclusive: Int,
        val coutStart: Int,
        val coutEndExclusive: Int
    )

    // These are Kokoro/Misaki's existing native phoneme symbols, not replacement text.
    // The reviewed C + out reading remains shared with Spanish and French.
    const val STD_PHONES = "ˈɛs tˈi dˈi"
    val coutPhones: String get() = ManualEnglishPronunciations.phones.getValue("cout")
    const val CACHE_REVISION = "english-technical-symbol-names-v3"

    private val qualified = Regex("(?<![\\p{L}\\p{M}\\p{N}_])(?<std>std)\\s*::\\s*(?<cout>cout)(?![\\p{L}\\p{M}\\p{N}_])")
    private val urls = Regex("(?:https?://|www\\.)\\S+", RegexOption.IGNORE_CASE)
    private val links = Regex("\\[([^]]+)]\\(([^)]*)\\)")
    private val standardNamespace = Regex("(?<![\\p{L}\\p{M}\\p{N}_])std(?=\\s*::)")
    private val operators = linkedMapOf(
        "<=>" to "three way comparison operator",
        "<<=" to "left shift equals", ">>=" to "right shift equals",
        "->*" to "arrow asterisk", "::" to "scope resolution operator",
        "##" to "double hash", "&&" to "logical and", "||" to "logical or",
        "==" to "equals equals", "!=" to "not equal to",
        "<=" to "less than or equal to", ">=" to "greater than or equal to",
        "<<" to "left shift", ">>" to "right shift",
        "++" to "plus plus", "--" to "minus minus",
        "+=" to "plus equals", "-=" to "minus equals", "*=" to "asterisk equals",
        "/=" to "slash equals", "%=" to "percent equals",
        "&=" to "ampersand equals", "|=" to "vertical bar equals",
        "^=" to "caret equals", "->" to "arrow", ".*" to "dot asterisk"
    )
    private val symbols = mapOf(
        '#' to "hash", '{' to "open brace", '}' to "close brace",
        '<' to "less than", '>' to "greater than",
        '[' to "open bracket", ']' to "close bracket",
        '=' to "equals", '+' to "plus", '*' to "asterisk", '/' to "slash",
        '\\' to "backslash", '%' to "percent", '&' to "ampersand",
        '|' to "vertical bar", '^' to "caret", '~' to "tilde", '_' to "underscore"
    )

    /**
     * The existing English frontend discards many SYM/NFP tokens before dictionary lookup.
     * Give only explicit technical notation ordinary English names before that tokenizer;
     * spelling, voice, POS, BART and the source text otherwise remain unchanged.
     */
    fun replacements(text: String): List<Replacement> {
        if (text.isEmpty()) return emptyList()
        val protected = (urls.findAll(text) + links.findAll(text)).map { it.range }.toList()
        fun available(start: Int, end: Int) = protected.none { it.first < end && it.last + 1 > start }
        val standards = standardNamespace.findAll(text).filter { available(it.range.first, it.range.last + 1) }
            .associateBy { it.range.first }
        val outputs = qualifiedOutputMatches(text).filter { available(it.start, it.endExclusive) }.associateBy { it.coutStart }
        val code = codePositions(text)
        val result = mutableListOf<Replacement>()
        var index = 0
        while (index < text.length) {
            protected.firstOrNull { index in it }?.let { index = it.last + 1; continue }
            standards[index]?.let {
                result += Replacement(index, it.range.last + 1, it.value, STD_PHONES)
                index = it.range.last + 1
                continue
            }
            outputs[index]?.let {
                result += Replacement(index, it.coutEndExclusive, text.substring(index, it.coutEndExclusive), coutPhones)
                index = it.coutEndExclusive
                continue
            }
            val operator = operators.entries.firstOrNull {
                text.startsWith(it.key, index) && (it.key != "--" || code[index] || decrementAt(text, index))
            }
            if (operator != null) {
                result += Replacement(index, index + operator.key.length, operator.value)
                index += operator.key.length
                continue
            }
            val character = text[index]
            // Ordinary hyphenated prose and contractions keep Misaki's native reading.
            // A prefix ! attached to an operand is C++ logical negation; sentence-final !
            // keeps its original prosody instead of being spoken as an operator.
            val name = when {
                character == '-' && (code[index] || quotedSymbol(text, index) || minusAt(text, index)) -> "minus"
                character == '!' && (code[index] || quotedSymbol(text, index) || negationAt(text, index)) -> "logical not"
                else -> symbols[character]
            }
            if (name != null) result += Replacement(index, index + 1, name)
            index++
        }
        return result
    }

    fun hasChangedSymbolPronunciation(text: String): Boolean = replacements(text).isNotEmpty()

    private fun codePositions(text: String): BooleanArray {
        val code = BooleanArray(text.length)
        var cursor = 0
        while (cursor < text.length) {
            if (text[cursor] != '`') { cursor++; continue }
            val delimiter = if (text.startsWith("```", cursor)) "```" else "`"
            val end = text.indexOf(delimiter, cursor + delimiter.length)
            if (end < 0) { cursor += delimiter.length; continue }
            for (index in cursor + delimiter.length until end) code[index] = true
            cursor = end + delimiter.length
        }
        return code
    }

    private fun quotedSymbol(text: String, index: Int): Boolean = when (text.getOrNull(index - 1)) {
        '\'' -> text.getOrNull(index + 1) == '\''
        '"' -> text.getOrNull(index + 1) == '"'
        '‘' -> text.getOrNull(index + 1) == '’'
        '“' -> text.getOrNull(index + 1) == '”'
        else -> false
    }

    private fun minusAt(text: String, index: Int): Boolean {
        if (text.trim() == "-") return true
        val before = text.getOrNull(index - 1)
        val after = text.getOrNull(index + 1)
        // A numeric sign/operation is explicit. Keep prose dashes, list markers and
        // hyphens such as well-known or FP-32 in Misaki's established punctuation path.
        if (after?.isDigit() == true && (before == null || before.isWhitespace() || before.isDigit() || before in "([=+*/%&|^<>!?:,;{")) return true
        val prefix = text.substring((index - 32).coerceAtLeast(0), index)
        val suffix = text.substring(index + 1, (index + 18).coerceAtMost(text.length))
        return Regex("(?i)\\b(?:unary|binary|operator|minus|negative)\\s*$").containsMatchIn(prefix) ||
            Regex("(?i)^\\s+(?:signs?|tokens?|operators?)\\b").containsMatchIn(suffix)
    }

    private fun negationAt(text: String, index: Int): Boolean {
        var start = index
        while (start > 0 && text[start - 1] == '!') start--
        var end = index + 1
        while (end < text.length && text[end] == '!') end++
        val before = text.getOrNull(start - 1)
        val operand = text.getOrNull(end)
        return (before == null || before.isWhitespace() || before in "([=,:&|?{;") &&
            operand?.let { it.isLetterOrDigit() || it in "(_" } == true
    }

    private fun decrementAt(text: String, index: Int): Boolean {
        if (text.trim() == "--") return true
        val before = text.getOrNull(index - 1)
        val after = text.getOrNull(index + 2)
        if (before?.isWhitespace() == true && after?.isWhitespace() == true) return false
        val prefix = before == null || before.isWhitespace() || before in "([=,:&|?{;"
        val suffix = after == null || after.isWhitespace() || after in ")],;}:<>=+*/%&|^"
        return prefix && after?.let { it.isLetterOrDigit() || it == '_' } == true ||
            suffix && before?.let { it.isLetterOrDigit() || it == '_' } == true
    }

    fun qualifiedOutputMatches(text: String): List<QualifiedOutput> {
        val protected = urls.findAll(text).map { it.range }.toList()
        return qualified.findAll(text).filter { match -> protected.none { match.range.first in it } }.map { match ->
            val std = match.groups["std"]!!.range
            val cout = match.groups["cout"]!!.range
            QualifiedOutput(match.range.first, match.range.last + 1,
                std.first, std.last + 1, cout.first, cout.last + 1)
        }.toList()
    }
}
