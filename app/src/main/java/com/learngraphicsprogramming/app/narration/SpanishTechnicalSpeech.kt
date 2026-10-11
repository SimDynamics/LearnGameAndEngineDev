package com.learngraphicsprogramming.app.narration

/** Reads visible technical notation before math extraction, without rewriting lesson content. */
object SpanishTechnicalSpeech {
    data class Preparation(val text: String, val changed: Boolean)

    fun prepare(text: String, mathMode: String = "auto"): Preparation {
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
            if (text[index] == '`') {
                val delimiter = if (text.startsWith("```", index)) "```" else "`"
                val end = text.indexOf(delimiter, index + delimiter.length)
                if (end >= 0) {
                    val code = text.substring(index + delimiter.length, end)
                    output.append("código, ").append(readCode(code)).append(", fin de código")
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
                output.append(readCode(text.substring(index, technicalEnd)))
                index = technicalEnd
                changed = true
                continue
            }
            val operator = proseOperators.firstOrNull { text.startsWith(it.first, index) }
            if (operator != null) {
                output.append(' ').append(operator.second).append(' ')
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
                '§' -> "sección"
                '–' -> "guion"
                else -> null
            }
            if (symbol != null) {
                output.append(' ').append(symbol).append(' ')
                changed = true
            } else output.append(text[index])
            index++
        }
        return Preparation(if (changed) output.toString().replace(Regex("[ \\t]+"), " ")
            .replace(Regex(" +([,.;:!?])"), "$1").trim() else text, changed)
    }

    private fun codeEnd(text: String, index: Int): Int? {
        if (index > 0 && (text[index - 1].isLetterOrDigit() || text[index - 1] == '_')) return null
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

    private fun readCode(source: String): String {
        val result = mutableListOf<String>()
        var index = 0
        var angleDepth = 0
        var previousIdentifier = ""
        var qualified = false
        var include = header.matches(source.trim())
        while (index < source.length) {
            val char = source[index]
            if (char.isWhitespace()) { index++; continue }
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
                result += "directiva almohadilla $name"
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
                result += readIdentifier(match.value)
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
                result += if (operator.first == "&&" && previousIdentifier in typeNames) "doble ampersand" else operator.second
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
                '<' -> "menor que"
                '>' -> "mayor que"
                else -> codeSymbols[char]
            }
            result += name ?: char.toString() // Unknown notation reaches the strict G2P unchanged.
            qualified = false
            previousIdentifier = ""
            index++
        }
        return result.filter(String::isNotEmpty).joinToString(" ")
    }

    private fun readLiteral(source: String): String = buildList {
        var index = 0
        while (index < source.length) {
            val char = source[index]
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

    private val section = Regex("§\\s*(\\d+(?:\\.\\d+)*)(?:\\s*[–-]\\s*(\\d+(?:\\.\\d+)*))?(?![\\p{L}\\p{N}])")
    private val ordinal = Regex("(?<![\\p{L}\\p{N}])(\\d{1,2})\\.([ªº])(?!\\p{L})")
    private val numericRange = Regex("(?<![\\p{L}\\p{N}.])([−-]?\\d+)\\s*(?:–|\\.\\.)\\s*([−-]?\\d+)(?![\\p{L}\\p{N}]|\\.\\d)")
    private val pureFormula = Regex("[\\d\\s.,;:a-zA-Zα-ωπ∞=<>≤≥≠≈→+−*/×÷·()\\[\\]{}|?²³⁴⁵⁶⁷⁸⁹⁰¹^_%!√-]+")
    private val contextSignals = Regex("&&|\\|\\||==|!=|\\+\\+|--|[+*/%&|^-]=|\\.\\.")
    private val directive = Regex("#\\s*(include|ifndef|ifdef|define|endif|pragma|undef|elif|else|if)\\b")
    private val header = Regex("<(?:iostream|string|string_view|vector|array|sstream|stdexcept|limits|iomanip|ios|utility|algorithm|cmath|cstdint|cassert|format|fstream|optional|map|set|memory)>")
    private val qualifiedName = Regex("[A-Za-z_][A-Za-z_0-9]*(?:::[A-Za-z_][A-Za-z_0-9]*)+")
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
    private val proseOperators = listOf("&&" to "y lógico", "||" to "o lógico", "*=" to "multiplica y asigna", "==" to "comparación de igualdad", "!=" to "distinto de", "<=" to "menor o igual que", ">=" to "mayor o igual que", "<<" to "doble menor que", ">>" to "doble mayor que", "++" to "incremento", "--" to "decremento")
    private val codeOperators = listOf("<<=" to "doble menor que e igual", ">>=" to "doble mayor que e igual", "::" to "doble dos puntos", "&&" to "y lógico", "||" to "o lógico", "*=" to "multiplica y asigna", "+=" to "suma y asigna", "-=" to "resta y asigna", "/=" to "divide y asigna", "%=" to "resto y asignación", "==" to "comparación de igualdad", "!=" to "distinto de", "<=" to "menor o igual que", ">=" to "mayor o igual que", "<<" to "doble menor que", ">>" to "doble mayor que", "++" to "incremento", "--" to "decremento", "->" to "flecha")
    private val codeSymbols = mapOf('{' to "abre llave", '}' to "cierra llave", '[' to "abre corchete", ']' to "cierra corchete", '(' to "abre paréntesis", ')' to "cierra paréntesis", '&' to "ampersand", '|' to "barra vertical", '#' to "almohadilla", '_' to "guion bajo", '=' to "asigna", '+' to "más", '-' to "menos", '/' to "barra", '%' to "resto", ':' to "dos puntos", ';' to "punto y coma", ',' to "coma", '.' to "punto", '!' to "admiración", '?' to "interrogación", '^' to "acento circunflejo", '~' to "tilde", '\'' to "comilla simple", '"' to "comilla doble", '\\' to "barra inversa", '*' to "asterisco", '<' to "menor que", '>' to "mayor que")
}
