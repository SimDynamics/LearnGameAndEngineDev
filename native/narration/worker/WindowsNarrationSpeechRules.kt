package com.learngraphicsprogramming.app.narration

/** Windows reading refinements, applied by the generated host frontend only.
 * The displayed source, Android implementation and stored recordings stay intact. */
internal object WindowsNarrationSpeechRules {
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

    /** Only a prose-introduced displayed output value, never a code statement,
     * symbol explanation, URL, type qualification or a fenced source listing. */
    private val outputValueCue = Regex(
        "(?iu)(?:\\bsalida(?:\\s+(?:esperada|exacta|solicitada|del programa))?\\s*(?:es|será|:|=)|" +
        "\\bobjetivo(?:\\s+de\\s+(?:esta\\s+)?actividad)?\\s+es|" +
        "\\b(?:debe\\s+)?(?:mostrar|muestra|imprimir|imprime)|\\bresultado\\s+(?:esperado\\s+)?es)\\s*$")

    fun isDisplayedOutputValue(text: String, opening: Int, value: String): Boolean {
        if (!value.any(Char::isLetter) || ':' !in value || "::" in value || "://" in value ||
                value.any { it in "\\\"'`{}[]();#<>=?" }) return false
        return outputValueCue.containsMatchIn(text.substring((opening - 160).coerceAtLeast(0), opening))
    }

    private val namedLetter = Regex("(?iu)(?<![\\p{L}\\p{M}\\p{N}_])(?:letra|carácter|caracter)[ \\t]+$")
    private val coordinatedNounPhrase = Regex("(?iu)[ \\t]+(?:el|la|los|las|un|una|unos|unas)[ \\t]+(?=\\p{L})")

    /** A named ASCII letter is one technical token; its following colon is prose.
     * Quotes, code and formulas are handled first by the canonical normalizer. */
    fun isNamedLetter(text: String, index: Int): Boolean {
        if (text[index] !in 'a'..'z' && text[index] !in 'A'..'Z') return false
        // In prose, "un carácter y el intento" coordinates two noun phrases;
        // its y is not that character's name. Do not cross punctuation/lines
        // or affect uppercase Y, "letra y es", "letra y:" or explicit code.
        if (text[index] == 'y' && coordinatedNounPhrase.matchAt(text, index + 1) != null) return false
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
