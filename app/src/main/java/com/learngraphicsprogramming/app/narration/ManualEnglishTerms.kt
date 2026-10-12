package com.learngraphicsprogramming.app.narration

/**
 * Reviewed English readings for Spanish C++ lessons. This is a finite allowlist, not
 * language detection. Add an entry and regenerate its bundled Misaki pronunciation;
 * no rule in the phoneme engine needs to change when a new technical name is added.
 *
 * The initial inventory came from the Spanish Programming JSON on work/mejoras-app:
 * primeros programas (include/iostream/cout), tipos, flujos, colecciones, excepciones,
 * formatos and generacion de muestras. Course content is never rewritten.
 *
 * Spelling is case-sensitive and must occupy a complete identifier. Unknown names,
 * string/character literals and mathematics retain their original Spanish reading.
 * Ambiguous Spanish words require one of the explicit syntactic/nominal contexts below.
 */
object ManualEnglishTerms {
    enum class Context { PROSE, CODE, QUALIFIED, MEMBER, HEADER, CALL, TYPE_CUE, DIRECTIVE_CUE }

    data class Entry(
        val id: String,
        val spelling: String,
        val englishText: String,
        /** Penn Treebank tags, in the order of the words in the English alias. */
        val tags: List<String>,
        val contexts: Set<Context>
    )

    data class Match(val start: Int, val endExclusive: Int, val entry: Entry)

    private val distinctive = Context.entries.toSet()
    private val typeContext = setOf(Context.CODE, Context.QUALIFIED, Context.HEADER,
        Context.CALL, Context.TYPE_CUE)
    private val componentContext = setOf(Context.QUALIFIED, Context.HEADER, Context.TYPE_CUE)
    private val directiveContext = setOf(Context.DIRECTIVE_CUE)
    private val callableContext = setOf(Context.CODE, Context.QUALIFIED, Context.CALL, Context.TYPE_CUE)
    private val memberContext = setOf(Context.QUALIFIED, Context.MEMBER, Context.TYPE_CUE)

    private fun term(spelling: String, englishText: String = spelling, tags: String = "NN",
        contexts: Set<Context> = distinctive) = Entry(spelling, spelling, englishText,
        tags.split(' '), contexts)

    /**
     * Aliases state how an abbreviated name is read, rather than guessing its language.
     * int/enum/struct/undef/pragma are absent from the pinned US dictionaries: explicit
     * Misaki override strings keep the actual abbreviated names and never invoke BART.
     */
    val entries: List<Entry> = listOf(
        term("include", tags = "VB"),
        term("iostream", "I O stream", "PRP NN NN"),
        term("cout", "see out", "VB RP"),
        term("cin", "see in", "VB IN"),
        term("main"), term("return", tags = "VB"), term("double", tags = "JJ"), term("string"),
        term("int", "[int](/ˈɪnt/)", contexts = typeContext),
        term("char", contexts = typeContext), term("bool"), term("float"), term("void"),
        term("long", tags = "JJ"), term("short", tags = "JJ"), term("signed", tags = "JJ"),
        term("unsigned", tags = "JJ"), term("const", tags = "JJ"),
        term("constexpr", "const expression", "JJ NN"),
        term("auto", contexts = typeContext),
        term("true", tags = "JJ"), term("false", tags = "JJ"),
        term("if", tags = "IN"), term("else", tags = "RB"),
        term("for", tags = "IN", contexts = setOf(Context.CODE, Context.CALL, Context.TYPE_CUE)),
        term("while", tags = "IN"), term("do", tags = "VB", contexts = typeContext),
        term("switch", tags = "VB"), term("case", contexts = setOf(Context.CODE, Context.TYPE_CUE)),
        term("break", tags = "VB"),
        term("continue", tags = "VB"), term("default"), term("try", tags = "VB"),
        term("catch", tags = "VB"), term("throw", tags = "VB"),
        term("class"), term("struct", "[struct](/stɹˈʌkt/)"),
        term("enum", "[enum](/ˈinʌm/)"), term("public", tags = "JJ"),
        term("private", tags = "JJ"), term("protected", tags = "JJ"),
        term("virtual", tags = "JJ"), term("override", tags = "VB"),
        term("final", tags = "JJ", contexts = setOf(Context.TYPE_CUE)),
        term("using", tags = "VBG"), term("namespace", "name space", "NN NN"),
        term("template"), term("typename", "type name", "NN NN"),
        term("static_cast", "static cast", "JJ NN"),
        term("dynamic_cast", "dynamic cast", "JJ NN"),
        term("reinterpret_cast", "reinterpret cast", "VB NN"),
        term("const_cast", "const cast", "JJ NN"),
        term("nullptr", "null pointer", "JJ NN"), term("noexcept", "no except", "DT IN"),
        term("new", tags = "JJ"), term("delete", tags = "VB"),
        term("define", tags = "VB", contexts = directiveContext),
        term("ifdef", "if defined", "IN VBN", directiveContext),
        term("ifndef", "if not defined", "IN RB VBN", directiveContext),
        term("endif", "end if", "NN IN", directiveContext),
        term("elif", "else if", "RB IN", directiveContext),
        term("undef", "[undef](/ʌndɪfˈAn/)", "VB", directiveContext),
        term("pragma", "[pragma](/pɹˈæɡmə/)", "NN", directiveContext),
        term("vector", contexts = componentContext),
        term("array", contexts = componentContext), term("map", contexts = componentContext),
        term("set", contexts = componentContext), term("optional", contexts = componentContext),
        term("string_view", "string view", "NN NN"),
        term("size_t", "size T", "NN NN"),
        term("numeric_limits", "numeric limits", "JJ NNS"),
        term("getline", "get line", "VB NN"), term("size"),
        term("empty", tags = "JJ"), term("push_back", "push back", "VB RP"),
        term("pop_back", "pop back", "VB RP"), term("begin", tags = "VB"),
        term("end", contexts = callableContext),
        term("at", tags = "IN", contexts = callableContext),
        term("front"), term("back"), term("data", contexts = memberContext),
        term("reserve", tags = "VB", contexts = memberContext),
        term("resize", "re size", "NN NN"), term("clear", tags = "VB"),
        term("insert", tags = "VB"), term("erase", tags = "VB"),
        term("find", tags = "VB"), term("sort", tags = "VB"), term("swap", tags = "VB"),
        term("move", tags = "VB"), term("pair"),
        term("istream", "I stream", "PRP NN"), term("ostream", "O stream", "NN NN"),
        term("ifstream", "I F stream", "PRP NN NN"),
        term("ofstream", "O F stream", "NN NN NN"),
        term("istringstream", "I string stream", "PRP NN NN"),
        term("ostringstream", "O string stream", "NN NN NN"),
        term("sstream", "S stream", "NN NN"), term("ios", "I O S", "PRP NN NN"),
        term("fstream", "F stream", "NN NN"),
        term("iomanip", "I O manipulate", "PRP NN VB"),
        term("stdexcept", "standard except", "JJ IN"),
        term("cctype", "C C type", "NN NN NN"),
        term("cstdio", "C standard I O", "NN JJ PRP NN"),
        term("cstddef", "C standard definition", "NN JJ NN"),
        term("cstdint", "C standard [int](/ˈɪnt/)", "NN JJ NN"),
        term("cassert", "C assert", "NN VB"), term("algorithm"), term("limits"),
        term("utility"), term("cmath", "C math", "NN NN"),
        term("memory", contexts = componentContext), term("random"),
        term("format", tags = "VB"), term("fixed", tags = "JJ"),
        term("scientific", tags = "JJ"), term("defaultfloat", "default float", "NN NN"),
        term("hex"), term("oct", "octal", "JJ"), term("dec", "decimal", "JJ"),
        term("left", tags = "JJ"), term("right", tags = "JJ"),
        term("setw", "set W", "VB NN"), term("setfill", "set fill", "VB NN"),
        term("setprecision", "set precision", "VB NN"), term("endl", "end L", "NN NN"),
        term("cerr", "C error", "NN NN"), term("clog", "C log", "NN NN"),
        term("flush", tags = "VB"), term("ws", "W S", "NN NN"),
        term("fail", tags = "VB"), term("good", tags = "JJ"), term("bad", tags = "JJ"),
        term("eof", "E O F", "NN NN NN"), term("EOF", "E O F", "NN NN NN"),
        term("close", tags = "VB"), term("get", tags = "VB", contexts = callableContext),
        term("str", "S T R", "NN NN NN", callableContext),
        term("what", tags = "WP", contexts = callableContext),
        term("seed"), term("reset", tags = "VB"),
        term("mt19937", "M T one nine nine three seven", "NN NN CD CD CD CD CD"),
        term("uniform_int_distribution", "uniform [int](/ˈɪnt/) distribution", "JJ NN NN"),
        term("uniform_real_distribution", "uniform real distribution", "JJ JJ NN"),
        term("random_device", "random device", "JJ NN"),
        term("result_type", "result type", "NN NN"),
        term("exception"), term("runtime_error", "runtime error", "NN NN"),
        term("logic_error", "logic error", "NN NN"),
        term("invalid_argument", "invalid argument", "JJ NN"),
        term("out_of_range", "out of range", "RP IN NN"),
        term("bad_alloc", "bad allocation", "JJ NN"), term("assert", tags = "VB"),
        term("isalpha", "is alpha", "VBZ NN"), term("isdigit", "is digit", "VBZ NN"),
        term("isspace", "is space", "VBZ NN"),
        term("tolower", "to lower", "TO JJ"), term("toupper", "to upper", "TO JJ")
    ).also { table ->
        require(table.map { it.id }.distinct().size == table.size)
        require(table.map { it.spelling }.distinct().size == table.size)
        require(table.all { it.tags.isNotEmpty() && it.contexts.isNotEmpty() })
    }

    private val bySpelling = entries.associateBy(Entry::spelling)
    private val identifier = Regex("(?<![\\p{L}\\p{M}\\p{N}_])[\\p{L}_][\\p{L}\\p{M}\\p{N}_]*")
    private val url = Regex("https?://[^\\s<>]+")
    private val nominalCue = Regex("(?:tipo(?:s| de dato)?|contenedor|clase|cabecera|biblioteca|" +
        "palabra (?:clave|reservada)|especificador|modificador)\\s+(?:denominad[oa]\\s+|llamad[oa]\\s+)?$")
    private val directiveCue = Regex("(?:#\\s*|directiva\\s+(?:almohadilla\\s+)?)$")
    private val declarationTail = Regex("^\\s+[*&]*\\s*[\\p{L}_][\\p{L}\\p{N}_]*\\s*(?:[;=({,)]|$)")
    private val autoDeclarationTail = Regex("^\\s+[*&]*\\s*[\\p{L}_][\\p{L}\\p{N}_]*\\s*[=({]")
    private val typeNames = setOf("int", "char", "auto", "bool", "double", "float", "void", "string",
        "unsigned", "signed", "short", "long", "const")
    private val templateNames = setOf("vector", "array", "map", "set", "optional", "numeric_limits")
    private val qualifiedCue = Regex("(?<![\\p{L}\\p{M}\\p{N}_])std::(?:[\\p{L}_][\\p{L}\\p{N}_]*::)*$")
    private val templateTail = Regex("^<\\s*[\\p{L}_][\\p{L}\\p{N}_:]*(?:\\s*[,>])")
    private val templatePrefix = Regex("(?:vector|array|map|set|optional|static_cast|dynamic_cast|const_cast|reinterpret_cast)<$")
    private val finalBefore = Regex("(?:override|\\))\\s+$")
    private val finalAfter = Regex("^\\s*(?:[;{]|=\\s*0)")
    private val inferredTypeAfter = Regex("^\\s+(?:deduce|conserva)\\b[^.!?]{0,40}\\btipo\\b")
    private val conversionBefore = Regex("(?:convertir\\w*|conversión|deducción)\\b[^.!?]{0,40}\\s(?:a|con|de)\\s+$")
    private val namedTypeAfter = Regex("^\\s+(?:es|puede ser)\\s+(?:una palabra|un tipo)\\b")
    private val caseNounBefore = Regex("(?:un|cada|ningún|ningun|ese|el)\\s+$")
    private val caseLabelAfter = Regex("^\\s+(?:[+−-]?\\d+|[\\p{L}_][\\p{L}\\p{N}_]*|'[^']')\\s*:")
    private val loopNounBefore = Regex("(?<![\\p{L}\\p{M}\\p{N}_])(?:bucle|ciclo|sentencia|estructura|" +
        "instrucción|recorrido|encabezado de|un|el|cada|del|su|en)\\s+$")
    private val pairedLoopAfter = Regex("^\\s+(?:y|o)\\s+while\\b")
    private val pairedLoopBefore = Regex("\\bwhile\\s+(?:y|o|en lugar de)\\s+$")
    private val rawLiteralPrefix = Regex("(?<![\\p{L}\\p{M}\\p{N}_])(?:u8|u|U|L)?R$")
    private val memberCallBefore = Regex("[\\p{L}\\p{M}\\p{N}_)\\]]\\s*(?:\\.|->)\\s*$")
    private val memberNounBefore = Regex("(?:miembro|método)\\s+$")

    /** Offsets refer to the original source, before technical notation is verbalized. */
    fun select(source: String, mathMode: String = "auto"): List<Match> {
        if (source.isBlank() || mathMode == "inline" || mathMode == "block") return emptyList()
        val (protected, code) = masks(source)
        return identifier.findAll(source).mapNotNull { token ->
            val entry = bySpelling[token.value] ?: return@mapNotNull null
            val start = token.range.first
            val end = token.range.last + 1
            if ((start until end).any { protected[it] }) return@mapNotNull null
            if (Context.PROSE in entry.contexts) return@mapNotNull Match(start, end, entry)
            val before = source.substring((start - 100).coerceAtLeast(0), start)
            val after = source.substring(end, (end + 100).coerceAtMost(source.length))
            val contexts = mutableSetOf<Context>()
            if (code[start]) contexts += Context.CODE
            if (qualifiedCue.containsMatchIn(before)) contexts += Context.QUALIFIED
            if (memberCallBefore.containsMatchIn(before) && after.trimStart().startsWith('(')) contexts += Context.MEMBER
            if (before.endsWith('<') && after.startsWith('>')) contexts += Context.HEADER
            if (after.trimStart().startsWith('(')) contexts += Context.CALL
            if (nominalCue.containsMatchIn(before.lowercase()) || templateContext(entry, before, after) ||
                typeCue(entry, before, after)) contexts += Context.TYPE_CUE
            if (directiveCue.containsMatchIn(before)) contexts += Context.DIRECTIVE_CUE
            if (contexts.any { it in entry.contexts }) Match(start, end, entry) else null
        }.toList()
    }

    private fun templateContext(entry: Entry, before: String, after: String): Boolean {
        if (entry.spelling in templateNames && templateTail.containsMatchIn(after)) return true
        return entry.spelling in typeNames && before.endsWith('<') &&
            templatePrefix.containsMatchIn(before)
    }

    private fun typeCue(entry: Entry, before: String, after: String): Boolean = when (entry.spelling) {
        "final" -> finalBefore.containsMatchIn(before) && finalAfter.containsMatchIn(after)
        "case" -> caseNounBefore.containsMatchIn(before.lowercase()) || caseLabelAfter.containsMatchIn(after)
        "for" -> loopNounBefore.containsMatchIn(before.lowercase()) || pairedLoopBefore.containsMatchIn(before) ||
            pairedLoopAfter.containsMatchIn(after)
        "data", "reserve" -> memberNounBefore.containsMatchIn(before.lowercase())
        "auto", "int", "char" ->
            (if (entry.spelling == "auto") autoDeclarationTail else declarationTail).containsMatchIn(after) || after.startsWith('&') ||
            after.startsWith('*') || inferredTypeAfter.containsMatchIn(after) ||
            conversionBefore.containsMatchIn(before.lowercase()) || namedTypeAfter.containsMatchIn(after)
        "do" -> after.trimStart().startsWith('{') || after.startsWith("-while") || after.startsWith("-cuerpo-while")
        else -> false
    }

    private fun masks(source: String): Pair<BooleanArray, BooleanArray> {
        val protected = BooleanArray(source.length)
        val code = BooleanArray(source.length)
        fun mask(from: Int, to: Int) { for (i in from until to) protected[i] = true }
        var index = 0
        while (index < source.length) {
            val mathDelimiter = when {
                source.startsWith("\\(", index) -> "\\(" to "\\)"
                source.startsWith("\\[", index) -> "\\[" to "\\]"
                source.startsWith("$$", index) -> "$$" to "$$"
                source[index] == '$' -> "$" to "$"
                else -> null
            }
            if (mathDelimiter != null) {
                val closing = source.indexOf(mathDelimiter.second, index + mathDelimiter.first.length)
                val end = if (closing < 0) source.length else closing + mathDelimiter.second.length
                mask(index, end); index = end; continue
            }
            if (source[index] == '`') {
                val delimiter = if (source.startsWith("```", index)) "```" else "`"
                val closing = source.indexOf(delimiter, index + delimiter.length)
                val end = if (closing < 0) source.length else closing
                for (i in index + delimiter.length until end) code[i] = true
                maskQuotes(source, index + delimiter.length, end, protected)
                index = if (closing < 0) source.length else closing + delimiter.length
                continue
            }
            if (source[index] == '"' || source[index] == '\'') {
                // Conservatively treat straight quoted content as literal data even
                // when its surrounding code was supplied as prose rather than Markdown.
                if (source[index] == '\'' && source.getOrNull(index - 1)?.isDigit() == true &&
                    source.getOrNull(index + 1)?.isDigit() == true) { index++; continue }
                val end = quoteEnd(source, index, source.length)
                mask(index, end); index = end; continue
            }
            index++
        }
        url.findAll(source).forEach { mask(it.range.first, it.range.last + 1) }
        return protected to code
    }

    private fun maskQuotes(source: String, from: Int, to: Int, protected: BooleanArray) {
        var index = from
        while (index < to) {
            if (source[index] == '"' || source[index] == '\'') {
                if (source[index] == '\'' && source.getOrNull(index - 1)?.isDigit() == true &&
                    source.getOrNull(index + 1)?.isDigit() == true) { index++; continue }
                val end = quoteEnd(source, index, to)
                for (i in index until end) protected[i] = true
                index = end
            } else index++
        }
    }

    private fun quoteEnd(source: String, start: Int, limit: Int): Int {
        if (source[start] == '"' && rawLiteralPrefix.containsMatchIn(
                source.substring((start - 4).coerceAtLeast(0), start))) {
            val opening = source.indexOf('(', start + 1)
            if (opening < 0 || opening >= limit || opening - start - 1 > 16) return limit
            val delimiter = source.substring(start + 1, opening)
            if (delimiter.any { it in " ()\\\t\r\n\u000B\u000C" }) return limit
            val closingText = ")$delimiter\""
            val closing = source.indexOf(closingText, opening + 1)
            return if (closing < 0 || closing + closingText.length > limit) limit else closing + closingText.length
        }
        var index = start + 1
        while (index < limit) {
            if (source[index] == '\\') { index += 2; continue }
            if (source[index] == source[start]) return index + 1
            index++
        }
        return limit
    }
}
