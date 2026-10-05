package com.learngraphicsprogramming.app.math

/** A bounded mathematical LaTeX subset, deliberately independent of TeX execution. */
object MathLatexParser {
    sealed interface Result {
        data class Success(val expression: MathExpression.Sequence) : Result
        data class Failure(val message: String, val start: Int, val end: Int) : Result
    }

    fun parse(source: String): Result = parse(source, content = false)

    /** Content may contain prose around formulas. Its literal spaces are layout content;
     * answer parsing deliberately keeps ignoring mathematical source whitespace. */
    fun parseContent(source: String): Result = parse(source, content = true)

    private fun parse(source: String, content: Boolean): Result {
        if (source.length > 32_768) return Result.Failure("Expression is too long", 0, source.length)
        return try {
            val prepared = if (content) source else source.trim()
            val parser = Parser(prepared.removeSurrounding("\\(", "\\)").removeSurrounding("\\[", "\\]"), content)
            Result.Success(parser.parse())
        } catch (failure: ParseFailure) {
            Result.Failure(failure.message ?: "Invalid LaTeX", failure.position.coerceIn(0, source.length), (failure.position + 1).coerceIn(0, source.length))
        } catch (_: IllegalArgumentException) {
            Result.Failure("Invalid mathematical structure", 0, source.length)
        }
    }

    private class ParseFailure(message: String, val position: Int) : RuntimeException(message)
    private data class Stop(val close: Char? = null, val right: Boolean = false, val environment: Boolean = false)

    private class Parser(private val source: String, private val content: Boolean) {
        private var index = 0
        private var depth = 0
        private var nodes = 0
        private var contentContext = false

        fun parse(): MathExpression.Sequence {
            val result = sequence(Stop())
            whitespace()
            if (index != source.length) fail("Unexpected closing delimiter")
            return result
        }

        private fun sequence(stop: Stop, preserveContent: Boolean = content && depth == 0): MathExpression.Sequence {
            if (++depth > 64) fail("Expression nesting is too deep")
            val previousContext = contentContext
            contentContext = preserveContent
            val result = mutableListOf<MathExpression>()
            while (true) {
                val whitespaceStart = index
                whitespace()
                if (contentContext && index > whitespaceStart) {
                    result.add(MathExpression.Symbol(source.substring(whitespaceStart, index)))
                }
                if (index >= source.length || isStop(stop)) break
                if (++nodes > 4_096) fail("Expression contains too many nodes")
                if (source.startsWith("\\,", index) || source.startsWith("\\;", index) || source.startsWith("\\!", index) || source.startsWith("\\ ", index)) {
                    if (contentContext && source[index + 1] != '!') {
                        result.add(MathExpression.Symbol(when (source[index + 1]) { ',' -> "\u2009"; ';' -> "\u2002"; else -> " " }))
                    }
                    index += 2
                    continue
                }
                val value = scripts(atom(stop))
                if (value is MathExpression.Sequence) result.addAll(value.children) else result.add(value)
            }
            contentContext = previousContext
            depth--
            return MathExpression.Sequence(result)
        }

        private fun isStop(stop: Stop): Boolean =
            (stop.close != null && source[index] == stop.close) ||
                (stop.right && source.startsWith("\\right", index)) ||
                (stop.environment && (source[index] == '&' || source.startsWith("\\\\", index) || source.startsWith("\\end", index)))

        private fun atom(stop: Stop): MathExpression {
            if (index >= source.length) fail("Expected an expression")
            if (contentContext && source[index].isLetter()) {
                val start = index
                var end = index
                while (end < source.length && source[end].isLetter()) end++
                val word = source.substring(start, end)
                if (word.length > 1 && word != "pi") {
                    index = end
                    return MathExpression.Symbol(word)
                }
            }
            return when (val char = source[index]) {
                '{' -> requiredGroup()
                '(' -> plainGroup('(', ')', MathDelimiter.PARENTHESES)
                '[' -> plainGroup('[', ']', MathDelimiter.BRACKETS)
                '|' -> plainGroup('|', '|', MathDelimiter.ABSOLUTE)
                '\\' -> command(stop)
                '+', '-', '−', '*', '/', '×', '÷', '·' -> { index++; MathExpression.Operator(char.toString()) }
                '=', '<', '>', '≤', '≥', '≠', '≈', '→' -> { index++; MathExpression.Relation(char.toString()) }
                'π', '∞', '∂', '∪', '∩', '∈', '%' -> { index++; MathExpression.Symbol(char.toString()) }
                'e' -> { index++; MathExpression.Symbol("e") }
                '}', ')', ']', '^', '_', '&' -> fail("Unexpected '$char'")
                else -> when {
                    char.isDigit() || char == '.' && index + 1 < source.length && source[index + 1].isDigit() -> number()
                    source.startsWith("pi", index) -> { index += 2; MathExpression.Symbol("π") }
                    char.toString() in MathLatexSerializer.symbols -> { index++; MathExpression.Symbol(char.toString()) }
                    char.isLetter() -> { index++; MathExpression.Variable(char.toString()) }
                    char == ',' || char == ':' || char == ';' || char == '!' -> { index++; MathExpression.Symbol(char.toString()) }
                    contentContext -> { index++; MathExpression.Symbol(char.toString()) }
                    else -> fail("Unsupported character '$char'")
                }
            }
        }

        private fun number(): MathExpression.Number {
            val start = index
            while (index < source.length && source[index].isDigit()) index++
            if (index < source.length && (source[index] == '.' || source[index] == ',') && index + 1 < source.length && source[index + 1].isDigit()) {
                index++
                while (index < source.length && source[index].isDigit()) index++
            }
            return MathExpression.Number(source.substring(start, index).replace(',', '.'))
        }

        private fun scripts(base: MathExpression): MathExpression {
            var upper: MathExpression.Sequence? = null
            var lower: MathExpression.Sequence? = null
            while (true) {
                val beforeWhitespace = index
                whitespace()
                val char = source.getOrNull(index)
                if (char != '^' && char != '_') {
                    // The next content sequence iteration owns this prose spacing.
                    if (contentContext) index = beforeWhitespace
                    break
                }
                index++
                val bound = argument()
                if (char == '^') { if (upper != null) fail("Duplicate exponent"); upper = bound }
                else { if (lower != null) fail("Duplicate subscript"); lower = bound }
            }
            return if (upper == null && lower == null) base else MathExpression.Scripts(asSequence(base), upper, lower)
        }

        private fun argument(): MathExpression.Sequence {
            whitespace()
            if (index >= source.length) fail("Missing command argument")
            return if (source[index] == '{') requiredGroup() else asSequence(atom(Stop()))
        }

        private fun requiredGroup(): MathExpression.Sequence {
            whitespace()
            expect('{')
            val result = sequence(Stop(close = '}'))
            expect('}')
            return result
        }

        private fun plainGroup(open: Char, close: Char, delimiter: MathDelimiter): MathExpression.Group {
            expect(open)
            val result = sequence(Stop(close = close), preserveContent = contentContext)
            expect(close)
            return MathExpression.Group(result, delimiter)
        }

        private fun command(stop: Stop): MathExpression {
            val start = index
            val name = readCommand()
            val value = when (name) {
                "frac", "dfrac", "tfrac" -> derivativeOrFraction(requiredGroup(), requiredGroup())
                "sqrt" -> {
                    whitespace()
                    val rootIndex = if (source.getOrNull(index) == '[') { index++; sequence(Stop(close = ']')).also { expect(']') } } else null
                    MathExpression.Root(requiredGroup(), rootIndex)
                }
                "left" -> leftGroup()
                "int", "sum", "prod" -> {

                    val bounds =
                        operatorBounds()

                    MathExpression.LargeOperator(
                        kind =
                            when (name) {
                                "int" ->
                                    MathLargeOperator.INTEGRAL

                                "sum" ->
                                    MathLargeOperator.SUM

                                else ->
                                    MathLargeOperator.PRODUCT
                            },

                        lower =
                            bounds.second,

                        upper =
                            bounds.first
                    )
                }
                "lim" -> {
                    val bounds = operatorBounds()
                    if (bounds.first != null) fail("Limits only support a lower condition")
                    whitespace()
                    val body = if (source.getOrNull(index) == '{') requiredGroup() else sequence(stop)
                    MathExpression.Limit(body, bounds.second ?: MathExpression.Sequence())
                }
                "begin" -> environment(readTextGroup())
                "mathrm", "text" -> MathExpression.Symbol(readTextGroup())
                "quad", "qquad" -> if (contentContext) MathExpression.Symbol(if (name == "quad") "\u2003" else "\u2003\u2003") else MathExpression.Sequence()
                "operatorname" -> function(readTextGroup())
                "cdot", "times" -> MathExpression.Operator("×")
                "div" -> MathExpression.Operator("÷")
                "le", "leq" -> MathExpression.Relation("≤")
                "ge", "geq" -> MathExpression.Relation("≥")
                "ne", "neq" -> MathExpression.Relation("≠")
                "to", "rightarrow" -> MathExpression.Relation("→")
                "approx" -> MathExpression.Relation("≈")
                "%" -> MathExpression.Symbol("%")
                "{" -> MathExpression.Symbol("{")
                "}" -> MathExpression.Symbol("}")
                else -> {
                    if (name in MathLatexSerializer.functions) function(name)
                    else MathLatexSerializer.symbols.entries.firstOrNull { it.value == "\\$name" }?.let { MathExpression.Symbol(it.key) }
                        ?: throw ParseFailure("Unsupported LaTeX command \\$name", start)
                }
            }
            // Empty command terminators are syntax, not mathematical nodes.
            if (value is MathExpression.Symbol || value is MathExpression.Relation || value is MathExpression.Operator) {
                if (source.startsWith("{}", index)) index += 2
            }
            return value
        }

        private fun function(name: String): MathExpression.Function {
            if (name.isBlank() || !name.all { it.isLetter() }) fail("Invalid function name")
            whitespace()
            val base = if (source.getOrNull(index) == '_') { index++; argument() } else null
            whitespace()
            val argument = when (source.getOrNull(index)) {
                '{' -> requiredGroup()
                '(' -> plainGroup('(', ')', MathDelimiter.PARENTHESES).content
                else -> argument()
            }
            return MathExpression.Function(name, argument, base)
        }

        private fun operatorBounds(): Pair<MathExpression.Sequence?, MathExpression.Sequence?> {
            var upper: MathExpression.Sequence? = null
            var lower: MathExpression.Sequence? = null
            while (true) {
                whitespace()
                val char = source.getOrNull(index)
                if (char != '^' && char != '_') break
                index++
                if (char == '^') { if (upper != null) fail("Duplicate upper bound"); upper = argument() }
                else { if (lower != null) fail("Duplicate lower bound"); lower = argument() }
            }
            return upper to lower
        }

        private fun leftGroup(): MathExpression.Group {
            whitespace()
            val opening = delimiter()
            val kind = when (opening) { "(" -> MathDelimiter.PARENTHESES; "[" -> MathDelimiter.BRACKETS; "{" -> MathDelimiter.BRACES; "|" -> MathDelimiter.ABSOLUTE; else -> fail("Unsupported delimiter") }
            val content = sequence(Stop(right = true))
            if (!source.startsWith("\\right", index)) fail("Missing \\right delimiter")
            readCommand()
            whitespace()
            val closing = delimiter()
            val expected = when (opening) { "(" -> ")"; "[" -> "]"; "{" -> "}"; else -> "|" }
            if (closing != expected) fail("Mismatched delimiters")
            return MathExpression.Group(content, kind)
        }

        private fun delimiter(): String {
            if (source.getOrNull(index) == '\\') return when (val command = readCommand()) { "lvert", "rvert", "vert" -> "|"; "lbrace" -> "{"; "rbrace" -> "}"; else -> command }
            return source.getOrNull(index)?.also { index++ }?.toString() ?: fail("Missing delimiter")
        }

        private fun environment(name: String): MathExpression {
            if (name !in setOf("matrix", "pmatrix", "bmatrix", "Bmatrix", "cases")) fail("Unsupported environment '$name'")
            val rows = mutableListOf<List<MathExpression.Sequence>>()
            var row = mutableListOf<MathExpression.Sequence>()
            while (true) {
                row.add(sequence(Stop(environment = true)))
                if (index >= source.length) fail("Missing environment end")
                when {
                    source[index] == '&' -> index++
                    source.startsWith("\\\\", index) -> { index += 2; rows.add(row); row = mutableListOf() }
                    source.startsWith("\\end", index) -> {
                        readCommand()
                        if (readTextGroup() != name) fail("Mismatched environment end")
                        rows.add(row)
                        break
                    }
                    else -> fail("Invalid matrix separator")
                }
            }
            if (rows.size > 32 || rows.any { it.size > 32 }) fail("Matrix exceeds supported dimensions")
            if (name == "cases") {
                if (rows.any { it.size != 2 }) fail("Each case needs an expression and a condition")
                return MathExpression.Cases(rows.map { MathExpression.Case(it[0], it[1]) })
            }
            if (rows.any { it.size != rows.first().size }) fail("Matrix rows must have the same number of columns")
            return MathExpression.Matrix(rows)
        }

        private fun derivativeOrFraction(numerator: MathExpression.Sequence, denominator: MathExpression.Sequence): MathExpression {
            fun isD(node: MathExpression?) = node == MathExpression.Symbol("d")
            val first = numerator.children.firstOrNull()
            val d = denominator.children.firstOrNull()
            val order = (first as? MathExpression.Scripts)?.takeIf { it.base.children.singleOrNull() == MathExpression.Symbol("d") && it.lower == null }?.upper
            if ((!isD(first) && order == null) || !isD(d) || denominator.children.size < 2) return MathExpression.Fraction(numerator, denominator)
            val remaining = denominator.children.drop(1)
            val variable = if (remaining.size == 1 && remaining[0] is MathExpression.Scripts) {
                val script = remaining[0] as MathExpression.Scripts
                if (script.upper != order || script.lower != null) return MathExpression.Fraction(numerator, denominator)
                script.base
            } else if (order == null) MathExpression.Sequence(remaining) else return MathExpression.Fraction(numerator, denominator)
            return MathExpression.Derivative(MathExpression.Sequence(numerator.children.drop(1)), variable, order)
        }

        private fun readCommand(): String {
            expect('\\')
            val start = index
            if (index >= source.length) fail("Incomplete LaTeX command")
            if (source[index].isLetter()) while (index < source.length && source[index].isLetter()) index++ else index++
            return source.substring(start, index)
        }

        private fun readTextGroup(): String {
            whitespace()
            expect('{')
            val start = index
            while (index < source.length && source[index] != '}') {
                if (source[index] == '{' || source[index] == '\\') fail("Nested text macros are unsupported")
                index++
            }
            val text = source.substring(start, index)
            expect('}')
            return text
        }

        private fun asSequence(value: MathExpression) = value as? MathExpression.Sequence ?: MathExpression.Sequence(listOf(value))
        private fun whitespace() { while (index < source.length && source[index].isWhitespace()) index++ }
        private fun expect(char: Char) { whitespace(); if (source.getOrNull(index) != char) fail("Expected '$char'"); index++ }
        private fun fail(message: String): Nothing = throw ParseFailure(message, index)
    }

}
