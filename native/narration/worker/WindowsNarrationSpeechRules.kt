package com.learngraphicsprogramming.app.narration

/** Windows reading refinements, applied by the generated host frontend only.
 * The displayed source, Android implementation and stored recordings stay intact. */
internal object WindowsNarrationSpeechRules {
    // Historical pronunciation is request scoped. B42 is the intermediate
    // displayed-record policy; ORIGINAL precedes it. Neither archive may use
    // refinements added to the current policy.
    enum class OutputDataPolicy { CURRENT, B42, ORIGINAL }
    private val outputDataPolicy = ThreadLocal.withInitial { OutputDataPolicy.CURRENT }
    // Replay only: preserve the preceding frontend's exact implicit prose
    // pronunciation when authenticating an existing bookmark. New prose uses
    // its own words; explicit code and semantic spans have separate readers.
    private val previousProseInference = ThreadLocal.withInitial { false }
    fun <T> withPreviousProseInference(previous: Boolean, action: () -> T): T {
        val saved = previousProseInference.get()
        previousProseInference.set(previous)
        return try { action() } finally { previousProseInference.set(saved) }
    }
    fun keepPreviousProseInference(): Boolean = previousProseInference.get() || outputDataPolicy.get() != OutputDataPolicy.CURRENT
    // Anticipation must apply the same single sentence split as the visible
    // reader. Historical pack export retains the original extractor chunks.
    private val wholeReadingRegions = ThreadLocal.withInitial { false }
    fun keepWholeReadingRegions(): Boolean = wholeReadingRegions.get()
    fun <T> withWholeReadingRegions(action: () -> T): T {
        val saved = wholeReadingRegions.get()
        wholeReadingRegions.set(true)
        return try { action() } finally { wholeReadingRegions.set(saved) }
    }
    fun <T> withOutputDataPolicy(policy: OutputDataPolicy, action: () -> T): T {
        val saved = outputDataPolicy.get()
        outputDataPolicy.set(policy)
        return try { action() } finally { outputDataPolicy.set(saved) }
    }
    fun <T> withPreviousOutputDataPolicy(previous: Boolean, action: () -> T): T {
        return withOutputDataPolicy(if (previous) OutputDataPolicy.ORIGINAL else OutputDataPolicy.CURRENT, action)
    }
    private val implicitAssignment = Regex("[\\p{L}_][\\p{L}\\p{M}\\p{N}_]*[ \\t]*=(?!=)")
    private val implicitNumber = Regex("(?:0[xX][0-9a-fA-F]+|0[bB][01]+|(?:[0-9]+(?:'[0-9]+)*(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?)[uUlLfF]*")
    private val assignmentOperators = listOf("<<=", ">>=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "=")
    private val expressionOperators = listOf("<=>", "<<=", ">>=", "->*", ".*", "==", "!=", "<=", ">=", "&&", "||", "<<", ">>",
        "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "+", "-", "*", "/", "%", "<", ">", "&", "|", "^", "=", "?", ":")
    private val numericOperators = listOf("==", "!=", "<=", ">=", "+", "-", "*", "/", "%", "=", "<", ">")
    private val numericProtected = Regex("https?://\\S+|[A-Za-z]:[/\\\\]\\S+|(?<![\\p{L}\\p{N}\\\\])/\\S+|[A-Za-z_][\\w.-]*[/\\\\][\\w./\\\\-]+|\"(?:\\\\.|[^\"\\\\])*\"|'(?:\\\\.|[^'\\\\])*'")
    private val numericProseCue = Regex("(?iu)(?:\\b(?:fecha|día|dia|fechado|fracción|fraccion|fracciones|razón|razon|proporción|proporcion)(?:[ \\t]+(?:es|del|de|matemática|matematica))?[ \\t:]+(?:[−+-][ \\t]*)?)$")

    /** A typed C++ puzzle may contain bare numeric expressions, unlike ordinary
     * prose. Preserve every source boundary and use the canonical code reader;
     * no identifier, quoted value, path or explicit mathematical intent is inferred. */
    fun cppNumericExpressionEnd(text: String, start: Int): Int? {
        val initial = text.getOrNull(start) ?: return null
        if (initial !in '0'..'9' && initial !in "+-(.") return null
        if (start > 0 &&
                (Character.isUnicodeIdentifierPart(text.codePointBefore(start)) || text[start - 1] in ".\\\\/")) return null
        // Do not resume inside an excluded decimal or signed operand. The
        // complete expression was already considered at its original start.
        if (start > 1 && text[start - 1] == ',' && text[start - 2] in '0'..'9') return null
        if (start > 0 && text[start - 1] in "+-−") return null
        if (numericProtected.findAll(text).any { start in it.range } ||
                numericProseCue.containsMatchIn(text.substring((start - 96).coerceAtLeast(0), start))) return null
        fun expression(from: Int, depth: Int): Pair<Int,Int>? {
            if (depth > 16) return null
            fun operand(from: Int): Pair<Int,Int>? {
                var at = horizontal(text, from)
                while (text.getOrNull(at) in listOf('+', '-')) at = horizontal(text, at + 1)
                if (text.getOrNull(at) == '(') {
                    val end = expression(at + 1, depth + 1) ?: return null
                    val closing = horizontal(text, end.first)
                    return if (text.getOrNull(closing) == ')') closing + 1 to end.second else null
                }
                val number = implicitNumber.matchAt(text, at) ?: return null
                var end = number.range.last + 1
                // An undelimited final period is prose punctuation. Explicit
                // source literals such as `2.` keep the established code path.
                if (text[end - 1] == '.' && (end == text.length || text[end].isWhitespace())) end--
                if (end <= at || text.getOrNull(end)?.let { it == '_' || it.isLetterOrDigit() } == true) return null
                return end to 0
            }
            val initial = operand(from) ?: return null
            var end = initial.first
            var operators = initial.second
            while (end < text.length) {
                val next = horizontal(text, end)
                val operator = numericOperators.firstOrNull { text.startsWith(it, next) } ?: break
                val following = operand(next + operator.length) ?: break
                operators += 1 + following.second
                end = following.first
            }
            return end to operators
        }
        val result = expression(start, 0) ?: return null
        return result.first.takeIf { result.second > 0 }
    }

    private fun horizontal(text: String, start: Int): Int {
        var at = start
        while (text.getOrNull(at) == ' ' || text.getOrNull(at) == '\t') at++
        return at
    }

    private fun identifierEnd(text: String, start: Int): Int? {
        if (start >= text.length) return null
        var at = start
        var point = text.codePointAt(at)
        if (point != '_'.code && !Character.isUnicodeIdentifierStart(point)) return null
        at += Character.charCount(point)
        while (at < text.length) {
            point = text.codePointAt(at)
            if (point != '_'.code && !Character.isUnicodeIdentifierPart(point)) break
            at += Character.charCount(point)
        }
        return at
    }

    private fun quotedEnd(text: String, start: Int): Int? {
        val quote = text[start]
        var at = start + 1
        while (at < text.length) {
            if (text[at] == '\\') { at += 2; continue }
            if (text[at] == quote) return at + 1
            if (text[at] in "\r\n") return null
            at++
        }
        return null
    }

    private fun groupEnd(text: String, start: Int, angles: Boolean = false): Int? {
        val stack = mutableListOf<Char>()
        var at = start
        while (at < text.length) {
            val value = text[at]
            if (value == '\'' || value == '"') { at = quotedEnd(text, at) ?: return null; continue }
            when (value) {
                '(' -> stack += ')'
                '[' -> stack += ']'
                '{' -> stack += '}'
                '<' -> if (angles) stack += '>'
                ')', ']', '}', '>' -> if (value != '>' || angles) {
                    if (stack.isEmpty() || stack.removeAt(stack.lastIndex) != value) return null
                    if (stack.isEmpty()) return at + 1
                }
            }
            if (value in "\r\n" && angles) return null
            at++
        }
        return null
    }

    private fun operandEnd(text: String, start: Int): Int? {
        var at = horizontal(text, start)
        while (at < text.length) {
            val prefix = when {
                text.startsWith("++", at) || text.startsWith("--", at) -> 2
                text[at] in "+-!~*&" -> 1
                else -> 0
            }
            if (prefix == 0) break
            at = horizontal(text, at + prefix)
        }
        if (at >= text.length) return null
        val number = implicitNumber.matchAt(text, at)
        var end = when (text[at]) {
            '\'', '"' -> quotedEnd(text, at)
            '(', '[', '{' -> groupEnd(text, at)
            else -> number?.range?.last?.plus(1) ?: identifierEnd(text, at)
        } ?: return null
        var qualified = false
        while (end < text.length) {
            val next = horizontal(text, end)
            when {
                text.getOrNull(next) in listOf('(', '[', '{') -> {
                    if (number != null || proseCitationEnd(text, next) != null) return end
                    end = groupEnd(text, next) ?: return end
                }
                text.startsWith("++", next) || text.startsWith("--", next) -> end = next + 2
                text.startsWith("::", next) || text.startsWith("->", next) || text.getOrNull(next) == '.' -> {
                    qualified = qualified || text.startsWith("::", next)
                    val width = if (text[next] == '.') 1 else 2
                    // A sentence-ending dot followed by a space is prose,
                    // not member access to its following ordinary word.
                    val member = if (width == 1) next + 1 else horizontal(text, next + width)
                    end = identifierEnd(text, member) ?: return end
                }
                text.getOrNull(next) == '<' -> {
                    // Keep canonical qualified-template readings; a bare name
                    // needs a call, initializer or qualified member after <>.
                    // Otherwise < remains a binary comparison in the scanner.
                    val angle = groupEnd(text, next, angles = true) ?: return end
                    val after = horizontal(text, angle)
                    if (!qualified && text.getOrNull(after) !in listOf('(', '{') && !text.startsWith("::", after)) return end
                    end = angle
                }
                else -> return end
            }
        }
        return end
    }

    /** Bound implicit code by expression syntax, never a later prose semicolon.
     * A following bare word ("es", "lo", etc.) cannot extend a complete RHS.
     * Unicode operands and exact UTF-16 offsets are retained; explicit authored
     * code still goes through the unchanged canonical readCode implementation. */
    fun implicitExpressionEnd(text: String, start: Int): Int {
        var at = horizontal(text, start)
        assignmentOperators.firstOrNull { text.startsWith(it, at) }?.let { at += it.length }
        var end = operandEnd(text, at) ?: return start
        while (end < text.length) {
            val next = horizontal(text, end)
            if (text.getOrNull(next) == ';') return next + 1
            val operator = expressionOperators.firstOrNull { text.startsWith(it, next) } ?: return end
            end = operandEnd(text, next + operator.length) ?: return end
        }
        return end
    }

    /** Called only when the canonical statementAssignment detector matched.
     * Retain that established technical prefix, but exclude its prose tail. */
    fun implicitStatementAssignmentEnd(text: String, start: Int): Int? {
        val assignment = implicitAssignment.matchAt(text, start) ?: return null
        val operator = assignment.range.last
        val end = implicitExpressionEnd(text, operator)
        return end.takeIf { it > operator + 1 }
    }

    // A complete keyword statement supplies syntax, unlike a noun cue or a
    // matching spelling in a pronunciation table. Used only to retain English
    // terms inside actual source when filtering the old lexical selection.
    private val keywordStatement = Regex("(?:return|throw|break|continue)(?![\\p{L}\\p{M}\\p{N}_])")
    fun keywordStatementEnd(text: String, start: Int): Int? {
        val keyword = keywordStatement.matchAt(text, start) ?: return null
        val next = horizontal(text, keyword.range.last + 1)
        if (text.getOrNull(next) == ';') return next + 1
        if (keyword.value == "break" || keyword.value == "continue") return null
        val end = implicitExpressionEnd(text, next)
        return end.takeIf { it > next && text.getOrNull(it - 1) == ';' }
    }

    /** Only a prose-introduced displayed output value, never a code statement,
     * symbol explanation, URL, type qualification or a fenced source listing. */
    private val outputValueCue = Regex(
        "(?iu)(?:\\bsalida(?:\\s+(?:esperada|exacta|solicitada|del programa))?\\s*(?:es|será|:|=)|" +
        "\\bobjetivo(?:\\s+de\\s+(?:esta\\s+)?actividad)?\\s+es|" +
        "\\b(?:debe\\s+)?(?:mostrar|muestra|imprimir|imprime)|\\bresultado\\s+(?:esperado\\s+)?es)\\s*$")

    private val outputListCue = Regex(
        "(?iu)(?<![\\p{L}\\p{M}\\p{N}_])(?:muestra(?:n)?|mostrar(?:á|án)?|imprime(?:n)?|imprimir(?:á|án)?|" +
        "salida|consola|pantalla|prompt|aviso)(?![\\p{L}\\p{M}\\p{N}_])")
    private val promptTailCue = Regex(
        "(?iu)^[ \\t]*(?:puede[ \\t]+|sirve[ \\t]+para[ \\t]+)?(?:avisar|avisa|indicar|indica|pedir|pide|solicitar|solicita)\\b")
    private val explicitSourceCue = Regex(
        "(?iu)(?:\\b(?:signo|símbolo|simbolo|operador|sentencia|instrucción|instruccion)|" +
        "\\betiqueta[ \\t]+(?:C\\+\\+|de[ \\t]+(?:sentencia|código|codigo)))" +
        "(?:[ \\t]+(?:llamad[oa]|denominad[oa]))?[ \\t]*$")
    private val sourceLabelTail = Regex(
        "(?iu)^[ \\t]*(?:pertenece|corresponde)[ \\t]+(?:al|a[ \\t]+(?:el|un))[ \\t]+c[oó]digo\\b")
    private val exitCodePhrase = Regex("(?iu)\\bc[oó]digos?[ \\t]+de[ \\t]+salida\\b")
    private val recordEquality = Regex("(?<![=<>!+*/%&|^:-])=(?!=)")
    private val recordSourceStart = Regex("(?iu)^\\s*(?:const|unsigned|signed|int|double|float|bool|char|auto|void|return)\\b")
    private val recordSourceCue = Regex("(?iu)\\b(?:variable|expresi[oó]n|declaraci[oó]n|comando|llamada)\\s*$")
    private val recordSourceOperators = listOf("::", "://", "<<", ">>", "++", "--", "->")
    private val outputPhrase = Regex("[\\p{L}\\p{M}]+(?:[ \\t]+[\\p{L}\\p{M}]+)+[.,!?]?")
    private val reportLabel = Regex("(?:^|[;|])(?:[\\p{L}_][\\p{L}\\p{M}\\p{N}_]{1,}(?:[ \\t]+[\\p{L}_][\\p{L}\\p{M}\\p{N}_]*)*):(?!:)")
    private val currentOutputIntro = Regex(
        "(?iu)(?:\\binforme[ \\t]+(?:empieza|comienza)[ \\t]+(?:con|por)|" +
        "\\binforme[ \\t]+terminado[ \\t]+en|\\b(?:presenta|presentan|muestran)|" +
        "\\bmuestra[ \\t]*,[ \\t]*en[ \\t]+(?:ese[ \\t]+)?orden[ \\t]*,|" +
        "\\b(?:archivo|muestra)[ \\t]+contiene(?:[ \\t]+únicamente)?|" +
        "\\bcontenido[ \\t]+(?:será|es)|\\bcadena[ \\t]+(?:compuesta|resultante|obtenida)[ \\t]+(?:será|es))[ \\t]*$")
    private val currentOutputList = Regex(
        "(?iu)(?<![\\p{L}\\p{M}\\p{N}_])(?:informe[ \\t]+(?:empieza|comienza)[ \\t]+(?:con|por)|" +
        "presenta|presentan|muestran)(?![\\p{L}\\p{M}\\p{N}_])")

    /** One local prose sentence, with inline samples hidden from cue detection.
     * Its bounds are independent of display tint. A cue inside another sample
     * (for example a C++ name or an output word) must not classify this one. */
    private fun outputSentence(text: String, opening: Int, closing: Int): Pair<String,String> {
        val samples = Regex("`[^`]*`")
        // Mask complete samples BEFORE clipping. Otherwise the 384-unit left
        // edge can bisect a long earlier sample and expose its trailing word
        // (such as "consola") as a false prose cue for the next sample.
        fun masked(source: String) = samples.replace(source) { " ".repeat(it.value.length) }
        val before = masked(text.substring(0, opening))
        val after = masked(text.substring(closing))
        val begin = maxOf(before.indexOfLast { it in ".!?;\r\n" } + 1, opening - 384, 0)
        val boundary = after.indexOfFirst { it in ".!?;\r\n" }.let { if (it < 0) after.length else it }
        return before.substring(begin) to after.substring(0, minOf(boundary, 256))
    }

    fun isDisplayedOutputValue(text: String, opening: Int, value: String): Boolean {
        if (!value.any(Char::isLetter) || ':' !in value || "::" in value || "://" in value ||
                value.any { it in "\\\"'`{}[]();#<>=?" }) return false
        // The normalizer calls this only for a complete single-backtick span.
        // Keep data such as 7:3 and genuine source labels on the code path;
        // a local output/prompt sentence can additionally own a list of labels.
        val closing = opening + value.length + 2
        if (closing > text.length) return false
        val (before, after) = outputSentence(text, opening, closing)
        val prefix = text.substring((opening - 160).coerceAtLeast(0), opening)
        if (explicitSourceCue.containsMatchIn(prefix) || sourceLabelTail.containsMatchIn(after)) return false
        if (outputValueCue.containsMatchIn(prefix)) return true
        // An output label/string is not a C++ statement label. Conversely, the
        // ordinary name "exit code" must not manufacture an output-data cue.
        return outputListCue.containsMatchIn(exitCodePhrase.replace(before, " ")) || promptTailCue.containsMatchIn(after)
    }

    /** Structured output may contain =, brackets and field separators without
     * being an assignment. Retain its literal punctuation reader, including
     * numeric colons, but pronounce = as data instead of a C++ operation.
     * Ordinary labels keep isDisplayedOutputValue's existing pause/cache path. */
    fun isDisplayedOutputRecord(text: String, opening: Int, value: String): Boolean {
        if (outputDataPolicy.get() == OutputDataPolicy.ORIGINAL) return false
        if (b42DisplayedOutputRecord(text, opening, value)) return true
        if (outputDataPolicy.get() != OutputDataPolicy.CURRENT) return false
        // Keep already supported labels/phrases on their exact B42 path. In
        // particular, a label followed by 11 must not acquire another number
        // rendering merely because the current record grammar is broader.
        if (isDisplayedOutputValue(text, opening, value) || b42DisplayedOutputPhrase(text, opening, value)) return false
        if ((!recordEquality.containsMatchIn(value) && !reportLabel.containsMatchIn(value)) ||
                value.trimEnd().endsWith(';') || value.any { it in "\\\"'`" } ||
                recordSourceOperators.any(value::contains) || recordSourceStart.containsMatchIn(value)) return false
        return currentOutputContext(text, opening, value)
    }

    // Frozen B42 grammar: changing this path would lose the recording keys of
    // the intermediate version even when ORIGINAL still reconstructed safely.
    private fun b42DisplayedOutputRecord(text: String, opening: Int, value: String): Boolean {
        if (!recordEquality.containsMatchIn(value) ||
                value.trimEnd().endsWith(';') || value.any { it in "\\\"'`" } ||
                recordSourceOperators.any(value::contains) || recordSourceStart.containsMatchIn(value)) return false
        val closing = opening + value.length + 2
        if (closing > text.length) return false
        val (before, after) = outputSentence(text, opening, closing)
        val prefix = text.substring((opening - 160).coerceAtLeast(0), opening)
        if (explicitSourceCue.containsMatchIn(prefix) || sourceLabelTail.containsMatchIn(after) ||
                recordSourceCue.containsMatchIn(prefix)) return false
        return outputValueCue.containsMatchIn(exitCodePhrase.replace(prefix, " ")) ||
            outputListCue.containsMatchIn(exitCodePhrase.replace(before, " ")) || promptTailCue.containsMatchIn(after)
    }

    fun isDisplayedOutputPhrase(text: String, opening: Int, value: String): Boolean {
        if (outputDataPolicy.get() == OutputDataPolicy.ORIGINAL) return false
        if (b42DisplayedOutputPhrase(text, opening, value)) return true
        return outputDataPolicy.get() == OutputDataPolicy.CURRENT && outputPhrase.matches(value) &&
            currentOutputContext(text, opening, value)
    }

    private fun b42DisplayedOutputPhrase(text: String, opening: Int, value: String): Boolean {
        if (!outputPhrase.matches(value)) return false
        val closing = opening + value.length + 2
        if (closing > text.length) return false
        val (before, after) = outputSentence(text, opening, closing)
        val prefix = text.substring((opening - 160).coerceAtLeast(0), opening)
        if (explicitSourceCue.containsMatchIn(prefix) || recordSourceCue.containsMatchIn(prefix) ||
                sourceLabelTail.containsMatchIn(after)) return false
        return outputValueCue.containsMatchIn(exitCodePhrase.replace(prefix, " ")) ||
            outputListCue.containsMatchIn(exitCodePhrase.replace(before, " ")) || promptTailCue.containsMatchIn(after)
    }

    private fun currentOutputContext(text: String, opening: Int, value: String): Boolean {
        val closing = opening + value.length + 2
        if (closing > text.length) return false
        val (before, after) = outputSentence(text, opening, closing)
        // Masked sentence context prevents an output word inside an earlier
        // C++ sample from classifying this sample. Scope ends at real prose
        // punctuation, while punctuation inside samples remains owned data.
        val prefix = text.substring((opening - 160).coerceAtLeast(0), opening)
        if (explicitSourceCue.containsMatchIn(prefix) || recordSourceCue.containsMatchIn(prefix) ||
                sourceLabelTail.containsMatchIn(after)) return false
        val sentence = exitCodePhrase.replace(before, " ")
        return currentOutputIntro.containsMatchIn(prefix) || currentOutputList.containsMatchIn(sentence) ||
            outputValueCue.containsMatchIn(exitCodePhrase.replace(prefix, " ")) || outputListCue.containsMatchIn(sentence)
    }

    // Labels pause before their value. Numeric field separators such as 0:0
    // remain literal punctuation, even in the same displayed output record.
    fun isOutputLabelColon(source: String, index: Int): Boolean {
        if (outputDataPolicy.get() == OutputDataPolicy.ORIGINAL) return false
        if (source.getOrNull(index) != ':' || source.getOrNull(index - 1) == ':' || source.getOrNull(index + 1) == ':') return false
        var first = index
        while (first > 0 && (source[first - 1].isLetterOrDigit() || source[first - 1] == '_')) first--
        return first < index && source.substring(first, index).any(Char::isLetter) &&
            (first == 0 || source[first - 1].isWhitespace() || source[first - 1] in ";|([{«")
    }

    private val namedLetter = Regex("(?iu)(?<![\\p{L}\\p{M}\\p{N}_])(?:letra|carácter|caracter)[ \\t]+$")
    private val coordinatedNounPhrase = Regex("(?iu)[ \\t]+(?:el|la|los|las|un|una|unos|unas)[ \\t]+(?=\\p{L})")
    private val namedLetterClitic = Regex(
        "(?iu)[ \\t]+(?:lo|la|los|las)[ \\t]+(?:pued(?:o|es|e|en)|pod(?:emos|éis)|deb(?:o|es|e|emos|éis|en))" +
        "[ \\t]+[\\p{L}\\p{M}]+(?:ar|er|ir)(?=[ \\t.,;:!?]|$)")

    /** Exact historical inference, used only to authenticate old recordings.
     * New prose needs an explicit code/semantic span to name a code letter. */
    fun isNamedLetter(text: String, index: Int): Boolean {
        if (!keepPreviousProseInference()) return false
        if (text[index] !in 'a'..'z' && text[index] !in 'A'..'Z') return false
        // In prose, "un carácter y el intento" coordinates two noun phrases;
        // its y is not that character's name. Do not cross punctuation/lines
        // or affect uppercase Y, "letra y es", "letra y:" or explicit code.
        // A clitic plus modal/infinitive ("la letra y la puedes escribir")
        // instead refers back to the named letter; la is not an article there.
        if (text[index] == 'y' && coordinatedNounPhrase.matchAt(text, index + 1) != null &&
                namedLetterClitic.matchAt(text, index + 1) == null) return false
        val next = text.getOrNull(index + 1)
        if (next != null && (next.isLetterOrDigit() || next == '_' ||
                Character.getType(next) in setOf(Character.NON_SPACING_MARK.toInt(),
                    Character.COMBINING_SPACING_MARK.toInt(), Character.ENCLOSING_MARK.toInt()))) return false
        return namedLetter.containsMatchIn(text.substring((index - 64).coerceAtLeast(0), index))
    }

    private val parenthesisMention = Regex(
        "(?iu)(?<![\\p{L}\\p{M}\\p{N}_])(?:consume|consumir|espera|esperaba|esperado|devuelve|devolver|" +
        "conserva|conservar|obtiene|obtener|lee|leer|mismo|símbolo|simbolo|signo|token|carácter|caracter)[ \\t]+$")

    /** A token mentioned in C++ prose is not a punctuation pause. The caller
     * supplies the explicit C++ reading context; authored code, math and
     * citations have already been handled by their canonical paths. */
    fun isIsolatedCppParenthesisMention(text: String, index: Int): Boolean {
        if (text.getOrNull(index) !in listOf('(', ')') || index == 0 || !text[index - 1].isWhitespace()) return false
        val next = text.getOrNull(index + 1)
        if (next != null && !next.isWhitespace() && next !in ".,;:!?") return false
        if (!parenthesisMention.containsMatchIn(text.substring((index - 96).coerceAtLeast(0), index))) return false
        // Ambiguous balanced prose asides retain punctuation, including spaced
        // ones: "obtiene ( una respuesta opcional )" is not a token mention.
        if (text[index] == '(' && groupEnd(text, index) != null) return false
        if (text[index] == ')') {
            var opening = text.lastIndexOf('(', index - 1)
            while (opening >= 0 && index - opening <= 512) {
                if (groupEnd(text, opening) == index + 1) return false
                opening = text.lastIndexOf('(', opening - 1)
            }
        }
        return true
    }

    private fun isolatedEscapeBoundary(c: Char): Boolean =
        c.isWhitespace() || c in ".,;:!?()[]{}\"'«»“”‘’…/\\+-*=<>|&—–"

    /** Only the two standalone C++ symbol escapes belong to this case.
     * A backslash in a path, URL or a longer identifier is not an escape mention.
     * Source offsets stay unchanged for word-level highlighting. */
    fun isIsolatedSymbolEscape(text: String, index: Int): Boolean {
        if (text.getOrNull(index) != '\\' ||
            text.getOrNull(index + 1) !in setOf('"', '\\')) return false
        return (index == 0 || isolatedEscapeBoundary(text[index - 1])) &&
            (index + 2 == text.length || isolatedEscapeBoundary(text[index + 2]))
    }


    // A bounded prose citation is not a function argument list. Require an
    // author name and a bibliographical date, rather than hiding all brackets.
    private val citationEntry = Regex(
        """[ \t]*[\p{Lu}\p{Lt}][\p{L}\p{M}]+[\p{L}\p{M} \t.,'’&\-–]*,[ \t]*(?:[12][0-9]{3}[a-z]?|s\.[ \t]*f\.)(?:[ \t]*,[ \t]*(?:pp?\.|cap\.)[ \t]*[0-9]+(?:[–-][0-9]+)?)?[ \t]*"""
    )

    /** Exclusive end, only in prose. Call after explicit code/math/URL handling.
     * Adjacent calls, single-letter coordinates, unfinished groups and explicit
     * statement tails retain the canonical technical reading. */
    fun proseCitationEnd(text: String, index: Int): Int? {
        if (text.getOrNull(index) != '(' ||
            (index > 0 && !text[index - 1].isWhitespace())) return null
        val limit = minOf(text.length, index + 513)
        var closing = index + 1
        while (closing < limit && text[closing] != ')') {
            if (text[closing] in "(\r\n`\\\"{}[]") return null
            closing++
        }
        if (closing == limit) return null
        // A following C++ statement delimiter/body is not an author citation.
        var after = closing + 1
        while (after < text.length && text[after] in " \t") after++
        if (text.getOrNull(after)?.let { it in ";{[=+*/%<>" } == true) return null
        val entries = text.substring(index + 1, closing).split(';')
        if (entries.isEmpty() || entries.any { !citationEntry.matches(it) }) return null
        return closing + 1
    }

    /** Called only after technical/math preparation. Keep the visible punctuation
     * and use the same sentence boundary as a prose semicolon, without splitting
     * namespaces, calls, quoted symbols or URLs that still occur in the text. */
    fun isProsePause(text: String, index: Int): Boolean = when (text[index]) {
        ':' -> text.getOrNull(index - 1) != ':' && text.getOrNull(index + 1) != ':' &&
            (index + 1 == text.length || text[index + 1].isWhitespace()) &&
            !text.substring(text.lastIndexOf(' ', index - 1) + 1, index).contains("://")
        '(' -> index > 0 && text[index - 1].isWhitespace() &&
            text.getOrNull(index + 1)?.isLetter() == true
        else -> false
    }
}
