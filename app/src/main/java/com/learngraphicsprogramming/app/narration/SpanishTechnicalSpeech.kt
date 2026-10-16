package com.learngraphicsprogramming.app.narration

/** Reads visible technical notation before math extraction, without rewriting lesson content. */
object SpanishTechnicalSpeech {
    data class Preparation(val text: String, val changed: Boolean)

    fun prepare(text: String, mathMode: String = "auto"): Preparation =
        prepareImpl(text, mathMode, emptyMap())

    /** Select English readings while the original code context and whole identifiers remain visible. */
    internal fun prepareForKokoro(text: String, mathMode: String = "auto"): Preparation {
        if (text.isBlank()) return Preparation(text, false)
        val annotations = ManualEnglishSpeech.spans(text)
        if (ManualEnglishSpeech.isPrepared(text) || annotations.isNotEmpty()) {
            return Preparation(if (ManualEnglishSpeech.isPrepared(text)) text else ManualEnglishSpeech.PREPARED + text, false)
        }
        val selected = ManualEnglishTerms.select(text, mathMode).associateBy { it.start }
        val prepared = prepareImpl(text, mathMode, selected)
        return prepared.copy(text = ManualEnglishSpeech.PREPARED + prepared.text)
    }

    /** Optional source provenance for presentation; the established preparation remains authoritative. */
    internal fun prepareForReading(text: String, mathMode: String = "auto",
                                   onPiece: (Int, Int, String) -> Unit): Preparation {
        if (text.isBlank() || ManualEnglishSpeech.isPrepared(text) || ManualEnglishSpeech.spans(text).isNotEmpty()) {
            val result = prepareForKokoro(text, mathMode)
            onPiece(0, text.length, result.text)
            return result
        }
        onPiece(0, 0, ManualEnglishSpeech.PREPARED.toString())
        val selected = ManualEnglishTerms.select(text, mathMode).associateBy { it.start }
        val result = prepareImpl(text, mathMode, selected, onPiece)
        return result.copy(text = ManualEnglishSpeech.PREPARED + result.text)
    }

    private fun prepareImpl(
        text: String,
        mathMode: String,
        selected: Map<Int, ManualEnglishTerms.Match>,
        onPiece: ((Int, Int, String) -> Unit)? = null
    ): Preparation {
        if (text.isBlank()) return Preparation(text, false)
        val forcedMath = mathMode == "inline" || mathMode == "block"
        // Ordinary formulas already have an AST reader. A technical reading must not
        // replace its grouping, fractions, variables, units or operator precedence.
        val pureMath = !forcedMath && pureFormula.matches(text.trim()) &&
            !Regex("[A-Za-z]{2,}").containsMatchIn(text) && text.any(Char::isLetterOrDigit) &&
            !contextSignals.containsMatchIn(text) && !technicalNumber.containsMatchIn(text)
        val output = StringBuilder(text.length)
        var changed = false
        var index = 0
        while (index < text.length) {
            val sourceStart = index
            val outputStart = output.length
            var observedChildren = false
            try {
            if (text[index] == '`') {
                val delimiter = if (text.startsWith("```", index)) "```" else "`"
                val end = text.indexOf(delimiter, index + delimiter.length)
                if (end >= 0) {
                    val code = text.substring(index + delimiter.length, end)
                    val regexPattern = regexCue.containsMatchIn(text.substring((index - 96).coerceAtLeast(0), index))
                    output.append("código, ")
                    if (onPiece != null && !regexPattern) {
                        onPiece(index, index + delimiter.length, "código, ")
                        output.append(readCode(code, index + delimiter.length, selected, onPiece))
                        onPiece(end, end + delimiter.length, ", fin de código")
                        observedChildren = true
                    } else output.append(if (regexPattern) readLiteral(code) else readCode(code, index + delimiter.length, selected))
                    output.append(", fin de código")
                    index = end + delimiter.length
                    changed = true
                    continue
                }
            }
            val explicitMathEnd = mathEnd(text, index)
            if (explicitMathEnd != null) {
                output.append(text, index, explicitMathEnd)
                index = explicitMathEnd
                continue
            }
            if (forcedMath || pureMath) {
                output.append(text[index++])
                continue
            }
            // Colons, hashes and operator-looking path characters inside a URL
            // do not describe C++ operations.
            url.matchAt(text, index)?.let { match ->
                output.append(match.value)
                index = match.range.last + 1
            }?.also { continue }
            // The existing frontend already reads the language name. Its two
            // plus signs are not an increment operation or a new cache reading.
            if (text.regionMatches(index, "C++", 0, 3, ignoreCase = true) &&
                (index == 0 || !text[index - 1].isLetterOrDigit()) && text.getOrNull(index + 3) != '+') {
                output.append(text, index, index + 3)
                index += 3
                continue
            }

            section.matchAt(text, index)?.let { match ->
                output.append("sección ").append(match.groupValues[1].split('.').joinToString(" punto ", transform = ::digits))
                if (match.groupValues[2].isNotEmpty()) output.append(" a ").append(
                    match.groupValues[2].split('.').joinToString(" punto ", transform = ::digits))
                index = match.range.last + 1
                changed = true
            }?.also { continue }
            ordinal.matchAt(text, index)?.let { match ->
                val value = match.groupValues[1].toIntOrNull()
                val feminine = match.groupValues[2] == "ª"
                val name = ordinalNames[value]
                output.append(if (name == null) "ordinal ${digits(match.groupValues[1])}" else
                    if (feminine) name.dropLast(1) + "a" else name)
                index = match.range.last + 1
                changed = true
            }?.also { continue }
            numericRange.matchAt(text, index)?.let { match ->
                output.append(digits(match.groupValues[1])).append(" a ").append(digits(match.groupValues[2]))
                index = match.range.last + 1
                changed = true
            }?.also { continue }

            val technicalEnd = codeEnd(text, index)
            if (technicalEnd != null) {
                output.append(readCode(text.substring(index, technicalEnd), index, selected, onPiece))
                observedChildren = onPiece != null
                index = technicalEnd
                changed = true
                continue
            }
            selected[index]?.let { match ->
                output.append(ManualEnglishSpeech.encode(match.entry, code = false))
                index = match.endExclusive
            }?.also { continue }
            val operator = proseOperators.firstOrNull { text.startsWith(it.first, index) }
            if (operator != null) {
                output.append(' ').append(when {
                    operator.first == "--" && cliOptionAt(text, index) -> "guion guion"
                    operator.first == "++" && compilerPlus.containsMatchIn(text.substring(0, index)) -> "más más"
                    else -> operator.second
                }).append(' ')
                index += operator.first.length
                changed = true
                continue
            }
            val symbol = when (text[index]) {
                '{' -> "abre llave"
                '}' -> "cierra llave"
                '[' -> "abre corchete"
                ']' -> "cierra corchete"
                '&' -> "ampersand"
                '|' -> "barra vertical"
                '#' -> "almohadilla"
                '_' -> "guion bajo"
                '*' -> if (multiplicationAt(text, index)) "por" else "asterisco"
                '!' -> if ((index == 0 || text[index - 1].isWhitespace() || text[index - 1] in "(=,;{?:&|") &&
                    text.getOrNull(index + 1)?.let { it.isLetterOrDigit() || it in "_(!" } == true) "negación lógica" else null
                ';' -> if (semicolonMention.containsMatchIn(text.substring((index - 96).coerceAtLeast(0), index))) "punto y coma" else null
                '§' -> "sección"
                '–' -> "guion"
                else -> null
            }
            if (symbol != null) {
                output.append(' ').append(symbol).append(' ')
                changed = true
            } else output.append(text[index])
            index++
            } finally {
                if (onPiece != null && !observedChildren && (index > sourceStart || output.length > outputStart)) {
                    onPiece(sourceStart, index, output.substring(outputStart))
                }
            }
        }
        return Preparation(if (changed) output.toString().replace(Regex("[ \\t]+"), " ")
            .replace(Regex(" +([,.;:!?])"), "$1").trim()
            else if (selected.isNotEmpty()) output.toString() else text,
            changed || selected.isNotEmpty())
    }

    private fun codeEnd(text: String, index: Int): Int? {
        if (index > 0 && (text[index - 1].isLetterOrDigit() || text[index - 1] == '_')) return null
        if (text[index] == '$') cmakeVariable.matchAt(text, index)?.let { variable ->
            var end = variable.range.last + 1
            while (true) end = cmakeVariable.matchAt(text, end)?.range?.last?.plus(1) ?: return end
        }
        val directive = directive.matchAt(text, index)
        if (directive != null) {
            var end = directive.range.last + 1
            if (directive.groupValues[1] == "include") {
                val next = skipSpaces(text, end)
                if (text.getOrNull(next) == '<') text.indexOf('>', next + 1).takeIf { it >= 0 }?.let { end = it + 1 }
                else if (text.getOrNull(next) == '"') quotedEnd(text, next)?.let { end = it }
            }
            return end
        }
        header.matchAt(text, index)?.let { return it.range.last + 1 }
        // The namespace can be mentioned on its own before prose, as in "std:: indica".
        // Do not consume the following ordinary word as a C++ identifier.
        namespacePrefix.matchAt(text, index)?.let { return it.range.last + 1 }
        technicalNumber.matchAt(text, index)?.let { return it.range.last + 1 }
        if (text[index] == '{' || text[index] == '[') {
            val closing = if (text[index] == '{') '}' else ']'
            balancedEnd(text, index, text[index], closing)?.let { return it }
        }
        val qualified = qualifiedName.matchAt(text, index)
        val identifier = technicalIdentifier.matchAt(text, index)
        val call = (keywordCall.matchAt(text, index) ?: callName.matchAt(text, index))?.takeIf {
            val name = it.groupValues[1]
            name !in mathFunctions && (name.length > 1 || name in codeKeywords)
        }
        val declaration = declaration.matchAt(text, index)
        val initialEnd = when {
            qualified != null -> qualified.range.last + 1
            identifier != null -> identifier.range.last + 1
            call != null -> call.range.last + 1
            declaration != null -> declaration.range.last + 1
            else -> null
        }
        if (initialEnd != null) {
            var end = initialEnd
            // Match only balanced local suffixes. Adjacent explanatory prose remains prose.
            var next = skipSpaces(text, end)
            if ((qualified != null || text.substring(index, end) in templateNames) && text.getOrNull(next) == '<') balancedEnd(text, next, '<', '>')?.let {
                end = it
                next = skipSpaces(text, it)
            }
            if (text.getOrNull(next) in listOf('(', '{', '[')) {
                val opening = text[next]
                val closing = when (opening) { '(' -> ')'; '{' -> '}'; else -> ']' }
                balancedEnd(text, next, opening, closing)?.let { end = it; next = skipSpaces(text, it) }
            }
            if (text.getOrNull(next) == ';') return next + 1
            if (assignment.matchAt(text, next) != null) return expressionEnd(text, next)
            return end
        }
        // A compound update is unambiguously code; a bare a=b remains mathematics
        // unless a statement terminator makes its technical context explicit.
        update.matchAt(text, index)?.let { return expressionEnd(text, it.range.last + 1) }
        compactComparison.matchAt(text, index)?.let { return expressionEnd(text, it.range.last + 1) }
        statementAssignment.matchAt(text, index)?.let { return it.range.last + 1 }
        if (text[index] == '\\' && text.getOrNull(index + 1) in escapeLetters &&
            text.getOrNull(index + 2)?.isLetter() != true) return index + 2
        if (text[index] == '\'' || text[index] == '"') {
            val quoteEnd = quotedEnd(text, index) ?: return null
            val inside = text.substring(index + 1, quoteEnd - 1)
            if (text[index] == '\'' && (inside.length == 1 || inside.startsWith('\\')) ||
                text[index] == '"' && (inside.contains('\\') || inside.any { it in "{}[]&|#*_%" })) return quoteEnd
        }
        return null
    }

    private fun expressionEnd(text: String, index: Int): Int {
        var end = index
        while (end < text.length) {
            val char = text[end]
            if (char == ';') return end + 1
            if (char in "\n:!?" || char == '.' && text.getOrNull(end + 1)?.isDigit() != true) break
            if (char == '\'' || char == '"') {
                val quoteEnd = quotedEnd(text, end) ?: break
                end = quoteEnd
                continue
            }
            // Stop at a prose conjunction after the operands instead of reading
            // the remainder of the explanatory sentence as source code.
            if (end > index && proseJoiner.matchAt(text, end) != null) break
            end++
        }
        return end
    }

    private fun balancedEnd(text: String, start: Int, opening: Char, closing: Char): Int? {
        var depth = 0
        var index = start
        while (index < text.length) {
            if (text[index] == '\'' || text[index] == '"') {
                index = quotedEnd(text, index) ?: return null
                continue
            }
            if (text[index] == opening) depth++
            if (text[index] == closing && --depth == 0) return index + 1
            if (text[index] == '\n' && opening == '<') return null
            index++
        }
        return null
    }

    private fun quotedEnd(text: String, start: Int): Int? {
        val quote = text[start]
        var index = start + 1
        while (index < text.length) {
            if (text[index] == '\\') { index += 2; continue }
            if (text[index] == quote) return index + 1
            if (text[index] == '\n') return null
            index++
        }
        return null
    }

    private fun mathEnd(text: String, index: Int): Int? {
        // A CMake expansion is source notation, even when another expansion
        // follows it. Its dollar signs must not become paired math delimiters.
        if (text[index] == '$') cmakeVariable.matchAt(text, index)?.let { variable ->
            val closing = text.indexOf('$', variable.range.last + 1)
            // Preserve the existing explicit-math boundary for ${x}+1$ or
            // ${x}^2$. Another ${name} is an opening expansion, not its closer.
            if (closing < 0 || cmakeVariable.matchAt(text, closing) != null) return null
        }
        val delimiters = when {
            text.startsWith("\\(", index) -> "\\(" to "\\)"
            text.startsWith("\\[", index) -> "\\[" to "\\]"
            text.startsWith("$$", index) -> "$$" to "$$"
            text.startsWith("$", index) -> "$" to "$"
            else -> return null
        }
        val end = text.indexOf(delimiters.second, index + delimiters.first.length)
        return if (end < 0) null else end + delimiters.second.length
    }

    private fun readCode(
        source: String,
        baseOffset: Int = 0,
        selected: Map<Int, ManualEnglishTerms.Match> = emptyMap(),
        onPiece: ((Int, Int, String) -> Unit)? = null
    ): String {
        val result = mutableListOf<String>()
        var index = 0
        var angleDepth = 0
        var previousIdentifier = ""
        var qualified = false
        var include = header.matches(source.trim())
        var observedOutput = false
        while (index < source.length) {
            val sourceStart = index
            val resultStart = result.size
            try {
            val char = source[index]
            if (char.isWhitespace()) { index++; continue }
            if (char == '$' && cmakeVariable.matchAt(source, index) != null) {
                result += "dólar"
                index++
                continue
            }
            if (char == '\'' || char == '"') {
                val end = quotedEnd(source, index)
                if (end != null) {
                    val name = if (char == '\'') "comilla simple" else "comilla doble"
                    result += "abre $name"
                    result += readLiteral(source.substring(index + 1, end - 1))
                    result += "cierra $name"
                    index = end
                    continue
                }
            }
            if (source.startsWith("C++", index) || source.startsWith("c++", index)) {
                result += "ce más más"; index += 3; continue
            }
            val directive = directive.matchAt(source, index)
            if (directive != null) {
                val name = directive.groupValues[1]
                val match = selected[baseOffset + requireNotNull(directive.groups[1]).range.first]
                val spokenName = if (match?.endExclusive == baseOffset + requireNotNull(directive.groups[1]).range.last + 1)
                    ManualEnglishSpeech.encode(match.entry) else name
                result += if (name == "include") "almohadilla $spokenName" else "directiva almohadilla $spokenName"
                include = name == "include"
                index = directive.range.last + 1
                continue
            }
            codeNumber.matchAt(source, index)?.let { match ->
                result += readNumber(match.value)
                index = match.range.last + 1
                previousIdentifier = ""
            }?.also { continue }
            codeWord.matchAt(source, index)?.let { match ->
                val selectedIdentifier = selected[baseOffset + match.range.first]
                result += if (selectedIdentifier?.endExclusive == baseOffset + match.range.last + 1)
                    ManualEnglishSpeech.encode(selectedIdentifier.entry) else readIdentifier(match.value)
                previousIdentifier = match.value
                index = match.range.last + 1
            }?.also { continue }
            if (char == '<' && !source.startsWith("<<", index) && !source.startsWith("<=", index) &&
                (include || qualified || previousIdentifier in templateNames)) {
                result += "abre ángulo"; angleDepth++; include = false; qualified = false; previousIdentifier = ""; index++; continue
            }
            if (char == '>' && angleDepth > 0 && !source.startsWith(">=", index)) {
                result += "cierra ángulo"; angleDepth--; index++; continue
            }
            val operator = codeOperators.firstOrNull { source.startsWith(it.first, index) }
            if (operator != null) {
                result += when {
                    operator.first == "&&" && previousIdentifier in typeNames -> "doble ampersand"
                    operator.first == "--" && cliOptionAt(source, index) -> "guion guion"
                    operator.first == "++" && previousIdentifier in setOf("clang", "g") -> "más más"
                    else -> operator.second
                }
                qualified = operator.first == "::"
                if (!qualified) previousIdentifier = ""
                index += operator.first.length
                continue
            }
            if (char == '\\') {
                result += "barra inversa"
                if (source.getOrNull(index + 1) in escapeLetters) {
                    result += readIdentifier(source[index + 1].toString()); index += 2
                } else index++
                continue
            }
            val name = when (char) {
                '*' -> if (previousIdentifier !in typeNames && multiplicationAt(source, index)) "por" else "asterisco"
                '!' -> "negación lógica"
                '<' -> "menor que"
                '>' -> "mayor que"
                else -> codeSymbols[char]
            }
            result += name ?: char.toString() // Unknown notation reaches the strict G2P unchanged.
            qualified = false
            previousIdentifier = ""
            index++
            } finally {
                if (onPiece != null && index > sourceStart) {
                    val added = result.subList(resultStart, result.size).filter(String::isNotEmpty)
                        .joinToString(" ")
                    val spoken = if (observedOutput && added.isNotEmpty()) " $added" else added
                    onPiece(baseOffset + sourceStart, baseOffset + index, spoken)
                    if (added.isNotEmpty()) observedOutput = true
                }
            }
        }
        return result.filter(String::isNotEmpty).joinToString(" ")
    }

    private fun readLiteral(source: String): String = buildList {
        var index = 0
        while (index < source.length) {
            val char = source[index]
            if (char == '$' && cmakeVariable.matchAt(source, index) != null) {
                add("dólar"); index++; continue
            }
            if (char == '\\') {
                add("barra inversa")
                if (index + 1 < source.length) {
                    val next = source[index + 1]
                    add(codeSymbols[next] ?: readIdentifier(next.toString()))
                    index += 2
                } else index++
                continue
            }
            if (char == ' ') { add("espacio"); index++; continue }
            codeWord.matchAt(source, index)?.let { match ->
                add(readIdentifier(match.value)); index = match.range.last + 1
            }?.also { continue }
            if (char.isDigit()) add(digitNames[char.digitToInt()])
            else add(codeSymbols[char] ?: char.toString())
            index++
        }
    }.joinToString(" ")

    private fun readNumber(source: String): String {
        if (source.contains('\'')) return source.map {
            if (it == '\'') "apóstrofo" else if (it.isDigit()) digitNames[it.digitToInt()] else readIdentifier(it.toString())
        }.joinToString(" ")
        if (source.startsWith("0x", true) || source.startsWith("0b", true)) return source.map {
            if (it.isDigit()) digitNames[it.digitToInt()] else readIdentifier(it.toString())
        }.joinToString(" ")
        val parts = decimalLiteral.matchEntire(source) ?: return source.map {
            if (it.isDigit()) digitNames[it.digitToInt()] else readIdentifier(it.toString())
        }.joinToString(" ")
        return buildList {
            add(digits(parts.groupValues[1]))
            if (parts.groupValues[2].isNotEmpty()) {
                add("punto")
                add(parts.groupValues[2].drop(1).map { digitNames[it.digitToInt()] }.joinToString(" "))
            }
            if (parts.groupValues[3].isNotEmpty()) {
                add("e")
                for (char in parts.groupValues[3].drop(1)) add(when (char) {
                    '+' -> "más"; '-' -> "menos"; else -> digitNames[char.digitToInt()]
                })
            }
            if (parts.groupValues[4].isNotEmpty()) add(parts.groupValues[4].map {
                readIdentifier(it.toString())
            }.joinToString(" "))
        }.filter(String::isNotEmpty).joinToString(" ")
    }

    /** Recover the same spoken spelling when an internal English annotation is stripped. */
    internal fun legacyIdentifier(identifier: String): String = readIdentifier(identifier)

    private fun readIdentifier(identifier: String): String {
        if (identifier == "std") return "ese te de"
        if (identifier.length == 1) return letterNames[identifier.lowercase()[0]] ?: identifier
        return identifierPieces.findAll(identifier).joinToString(" ") { match ->
            when {
                match.value == "_" -> "guion bajo"
                match.value.all(Char::isDigit) -> digits(match.value)
                match.value.length == 1 -> letterNames[match.value.lowercase()[0]] ?: match.value
                else -> match.value
            }
        }.ifEmpty { identifier }
    }

    private fun digits(value: String): String {
        val speech = if (value.length > 1 && value.startsWith('0') || value.toLongOrNull() == null) null
            else SpanishMathSpeech.text(value, "block")
        // The math number visitor deliberately leaves very large values literal.
        // Technical notation still needs a complete reading before strict G2P.
        return if (speech != null && speech.none(Char::isDigit)) speech else value.map {
            if (it == '-' || it == '−') "menos" else digitNames[it.digitToInt()]
        }.joinToString(" ")
    }

    private fun multiplicationAt(text: String, index: Int): Boolean {
        val left = operandBefore.find(text.substring((index - 128).coerceAtLeast(0), index))?.value.orEmpty()
        val right = operandAfter.find(text.substring(index + 1, (index + 129).coerceAtMost(text.length)))?.groupValues?.get(1).orEmpty()
        return left.isNotEmpty() && right.isNotEmpty() && left !in typeNames &&
            (left.any(Char::isDigit) || right.any(Char::isDigit) || left.length == 1 && right.length == 1)
    }

    private fun skipSpaces(text: String, index: Int): Int {
        var next = index
        while (text.getOrNull(next)?.isWhitespace() == true) next++
        return next
    }

    private fun cliOptionAt(text: String, index: Int): Boolean {
        val before = text.substring((index - 160).coerceAtLeast(0), index)
        val after = text.substring(index + 2, (index + 96).coerceAtMost(text.length))
        return cliCue.containsMatchIn(before) || cliTail.containsMatchIn(after)
    }

    private val section = Regex("§\\s*(\\d+(?:\\.\\d+)*)(?:\\s*[–-]\\s*(\\d+(?:\\.\\d+)*))?(?![\\p{L}\\p{N}])")
    private val ordinal = Regex("(?<![\\p{L}\\p{N}])(\\d{1,2})\\.([ªº])(?!\\p{L})")
    private val numericRange = Regex("(?<![\\p{L}\\p{N}.])([−-]?\\d+)\\s*(?:–|\\.\\.)\\s*([−-]?\\d+)(?![\\p{L}\\p{N}]|\\.\\d)")
    private val pureFormula = Regex("[\\d\\s.,;:a-zA-Zα-ωπ∞=<>≤≥≠≈→+−*/×÷·()\\[\\]{}|?²³⁴⁵⁶⁷⁸⁹⁰¹^_%!√-]+")
    private val contextSignals = Regex("::|<=>|<<=|>>=|&&|\\|\\||==|!=|\\+\\+|--|[+*/%&|^-]=|\\.\\.|(?<![\\p{L}\\p{N}_])!(?=[A-Za-z_(])")
    private val url = Regex("(?:https?://|www\\.)\\S+", RegexOption.IGNORE_CASE)
    // In prose a semicolon is usually a pause. Name it only when syntax makes
    // it the object of an explanation; code and quoted characters use readCode.
    private val semicolonMention = Regex("(?i)(?:\\b(?:con|sin|mediante)|\\b(?:añad(?:as|e|ir)|agreg(?:ues|a|ar)|pongas|pon|escrib(?:as|e|ir)|insert(?:es|a|ar)|quit(?:es|a|ar)|elimin(?:es|a|ar))|\\b(?:signo|símbolo|carácter|operador))\\s+$")
    private val cliCue = Regex("(?iu)(?:\\b(?:cmake|ctest|clang(?:\\+\\+)?|gcc|g\\+\\+|ninja|git|gradle(?:w)?)(?![\\p{L}\\p{N}_])[^;!?\\n]*|\\b(?:opción|argumento|parámetro)\\s*[`\"'«]*)$")
    private val cliTail = Regex("(?iu)^[A-Za-z][A-Za-z0-9-]*[`\"'»]?\\s+(?:es\\s+)?(?:una?\\s+)?(?:opción|argumento|parámetro)(?=\\s|[.,:;!?]|$)")
    private val regexCue = Regex("(?iu)(?:expresión\\s+regular|patrón\\s+(?:regular|regex)|regex)\\s*[:=]?\\s*$")
    private val compilerPlus = Regex("(?i)(?<![\\p{L}\\p{N}_])(?:clang|g)$")
    private val directive = Regex("#\\s*(include|ifndef|ifdef|define|endif|pragma|undef|elif|else|if)\\b")
    private val cmakeVariable = Regex("\\$\\{[A-Za-z_][A-Za-z_0-9]*\\}")
    private val header = Regex("<(?:iostream|string|string_view|vector|array|sstream|stdexcept|limits|iomanip|ios|utility|algorithm|cmath|cstdint|cassert|format|fstream|optional|map|set|memory)>")
    private val qualifiedName = Regex("[A-Za-z_][A-Za-z_0-9]*(?:::[A-Za-z_][A-Za-z_0-9]*)+")
    private val namespacePrefix = Regex("std\\s*::(?=\\s|$|[.,;!?)}\\]])")
    private val technicalIdentifier = Regex("(?:[A-Za-z][A-Za-z0-9]*_[A-Za-z_0-9]*|_+[A-Za-z_0-9]+)")
    private val callName = Regex("([A-Za-z_][A-Za-z_0-9]*)(?=\\()")
    private val keywordCall = Regex("(for|while|if|switch|catch|main)\\s*(?=\\()")
    private val declaration = Regex("(?:(?:const|unsigned|signed|long|short)\\s+)*(?:int|double|float|bool|char|auto|void|long|short)(?:\\s*[*&]+(?:\\s*[A-Za-z_][A-Za-z_0-9]*)?|\\s+[A-Za-z_][A-Za-z_0-9]*(?=\\s*[({=;]))")
    private val assignment = Regex("(?:<<=|>>=|[+*/%&|^-]=|=(?!=))")
    private val update = Regex("[A-Za-z_][A-Za-z_0-9]*\\s*(?:<<=|>>=|[+*/%&|^-]=)")
    private val statementAssignment = Regex("[A-Za-z_][A-Za-z_0-9]*\\s*=(?!=)[^;\\n]*;")
    private val compactComparison = Regex("[A-Za-z_][A-Za-z_0-9]*(?:==|!=|&&|\\|\\||<<|>>)")
    private val codeWord = Regex("[A-Za-z_][A-Za-z_0-9]*")
    private val identifierPieces = Regex("[A-Za-z]+|\\d+|_")
    private val codeNumber = Regex("(?:0[xX][0-9a-fA-F]+|0[bB][01]+|\\d+(?:'\\d+)*(?:\\.\\d*)?(?:[eE][+-]?\\d+)?)[uUlLfF]*")
    private val technicalNumber = Regex("(?<![\\p{L}\\p{N}_])(?:0[xX][0-9a-fA-F]+[uUlL]*|0[bB][01]+[uUlL]*|\\d+(?:\\.\\d+)?[eE][+-]?\\d+[fF]?|\\d+(?:\\.\\d+)?[uUlLfF]+)(?![\\p{L}\\p{N}_])")
    private val decimalLiteral = Regex("(\\d+)(\\.\\d*)?([eE][+-]?\\d+)?([uUlLfF]*)")
    private val operandBefore = Regex("[A-Za-z_0-9]+(?=\\s*$)")
    private val operandAfter = Regex("^\\s*([A-Za-z_0-9]+)")
    private val proseJoiner = Regex("\\s+(?:y|o|pero|porque|cuando|para|sin|con)\\s+")
    private val escapeLetters = setOf('n', 't', 'r', '0', 'a', 'b', 'f', 'v')
    private val typeNames = setOf("int", "double", "float", "bool", "char", "auto", "void", "long", "short", "unsigned", "signed")
    private val codeKeywords = setOf("for", "while", "if", "switch", "catch", "main")
    private val mathFunctions = setOf("sin", "cos", "tan", "cot", "sec", "csc", "arcsin", "arccos", "arctan", "sinh", "cosh", "tanh", "log", "ln", "exp", "sqrt", "abs")
    private val templateNames = setOf("vector", "array", "map", "set", "optional", "numeric_limits", "static_cast", "dynamic_cast", "reinterpret_cast", "const_cast")
    private val ordinalNames = mapOf(1 to "primero", 2 to "segundo", 3 to "tercero", 4 to "cuarto", 5 to "quinto", 6 to "sexto", 7 to "séptimo", 8 to "octavo", 9 to "noveno", 10 to "décimo")
    private val digitNames = listOf("cero", "uno", "dos", "tres", "cuatro", "cinco", "seis", "siete", "ocho", "nueve")
    private val letterNames = mapOf('a' to "a", 'b' to "be", 'c' to "ce", 'd' to "de", 'e' to "e", 'f' to "efe", 'g' to "ge", 'h' to "ache", 'i' to "i", 'j' to "jota", 'k' to "ka", 'l' to "ele", 'm' to "eme", 'n' to "ene", 'o' to "o", 'p' to "pe", 'q' to "cu", 'r' to "ere", 's' to "ese", 't' to "te", 'u' to "u", 'v' to "uve", 'w' to "uve doble", 'x' to "equis", 'y' to "i griega", 'z' to "zeta")
    // Longest tokens first. Neutral names remain deliberate for symbols whose
    // role needs type information (streams/shifts, pointers/references, arrows).
    private val codeOperators = listOf(
        "<=>" to "comparación de tres vías",
        "<<=" to "asignación con desplazamiento a la izquierda",
        ">>=" to "asignación con desplazamiento a la derecha",
        "->*" to "acceso a miembro mediante puntero a miembro",
        "::" to "operador de resolución de ámbito",
        "&&" to "y lógico", "||" to "o lógico",
        "*=" to "multiplica y asigna", "+=" to "suma y asigna", "-=" to "resta y asigna",
        "/=" to "divide y asigna", "%=" to "resto y asignación",
        "&=" to "asignación con y bit a bit", "|=" to "asignación con o bit a bit",
        "^=" to "asignación con o exclusivo bit a bit",
        "==" to "comparación de igualdad", "!=" to "distinto de",
        "<=" to "menor o igual que", ">=" to "mayor o igual que",
        "<<" to "doble menor que", ">>" to "doble mayor que",
        "++" to "incremento", "--" to "decremento", "->" to "flecha",
        ".*" to "acceso a miembro mediante puntero a miembro"
    )
    private val proseOperators = codeOperators.filter { it.first !in setOf("->", "->*", ".*") }
    private val codeSymbols = mapOf('{' to "abre llave", '}' to "cierra llave", '[' to "abre corchete", ']' to "cierra corchete", '(' to "abre paréntesis", ')' to "cierra paréntesis", '&' to "ampersand", '|' to "barra vertical", '#' to "almohadilla", '_' to "guion bajo", '=' to "asigna", '+' to "más", '-' to "menos", '/' to "barra", '%' to "resto", ':' to "dos puntos", ';' to "punto y coma", ',' to "coma", '.' to "punto", '!' to "admiración", '?' to "interrogación", '^' to "acento circunflejo", '~' to "tilde", '\'' to "comilla simple", '"' to "comilla doble", '\\' to "barra inversa", '*' to "asterisco", '<' to "menor que", '>' to "mayor que")
}
