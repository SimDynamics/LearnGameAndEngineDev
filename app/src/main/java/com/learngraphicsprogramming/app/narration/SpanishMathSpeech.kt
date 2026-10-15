package com.learngraphicsprogramming.app.narration

import com.learngraphicsprogramming.app.math.MathDelimiter
import com.learngraphicsprogramming.app.math.MathExpression
import com.learngraphicsprogramming.app.math.MathLargeOperator
import com.learngraphicsprogramming.app.math.MathLatexParser

/** Speech is derived from the existing tree; it never edits lesson text or mathematical state. */
object SpanishMathSpeech {
    private val fraction = Regex("[−-]?\\d+(?:[.,]\\d+)?/\\d+(?:[.,]\\d+)?")
    private val superscripts = Regex("[⁰¹²³⁴⁵⁶⁷⁸⁹]+")
    private val power = Regex("[a-zA-Zπ]?(?:\\d+)?(?:\\^\\{?[−-]?\\d+\\}?|[²³⁴⁵⁶⁷⁸⁹⁰¹]+)")
    private val root = Regex("√(?:\\d+(?:[.,]\\d+)?|[a-zA-Zπ]|\\([^\\n()]+\\))")
    private val coordinates = Regex("\\([−-]?\\d+(?:[.,]\\d+)?,\\s*[−-]?\\d+(?:[.,]\\d+)?\\)")
    private val arithmetic = Regex("[−-]?\\d+(?:[.,]\\d+)?(?:\\s*[×÷+−*-]\\s*[−-]?\\d+(?:[.,]\\d+)?)+")
    private val relation = Regex("[xyab]\\s*[=<>≤≥≠≈]\\s*(?:[−-]?\\d+(?:[.,]\\d+)?[xyab]?|[xyab])(?:\\s*[+−-]\\s*(?:\\d+[xyab]?|[xyab]))*")
    private val wholeMath = Regex("[\\d\\s.,;:a-zA-Zα-ωπ∞=<>≤≥≠≈→+−*/×÷·()\\[\\]{}|?²³⁴⁵⁶⁷⁸⁹⁰¹^_%!√-]+")
    private val protectedToken = Regex("https?://\\S+|[A-Za-z]:[/\\\\]\\S+|(?<![\\p{L}\\p{N}\\\\])/\\S+|[A-Za-z_][\\w.-]*[/\\\\][\\w./\\\\-]+|\\d{1,4}/\\d{1,2}/\\d{1,4}")
    private val inlineCode = Regex("```[\\s\\S]*?```|`[^`]*`")

    fun speak(expression: MathExpression): String = clean(when (expression) {
        is MathExpression.Sequence -> sequence(expression)
        is MathExpression.Number -> number(expression.value)
        is MathExpression.Variable -> letter(expression.name)
        is MathExpression.Symbol -> symbol(expression.value)
        is MathExpression.Operator -> operator(expression.value)
        is MathExpression.Relation -> relation(expression.value)
        is MathExpression.Fraction -> fraction(expression)
        is MathExpression.Scripts -> scripts(expression)
        is MathExpression.Root -> {
            val indexExpression = expression.index
            val index = indexExpression?.let(::integerValue)
            val name = when {
                indexExpression == null || index == 2L -> "raíz cuadrada"
                index == 3L -> "raíz cúbica"
                else -> "raíz de índice ${speak(indexExpression)}"
            }
            "$name de ${bounded(expression.radicand, "raíz") }"
        }
        is MathExpression.Group -> if (expression.delimiter == MathDelimiter.ABSOLUTE) {
            "valor absoluto de ${speak(expression.content)}, fin del valor absoluto"
        } else {
            val delimiters = when (expression.delimiter) {
                MathDelimiter.PARENTHESES -> "abre paréntesis" to "cierra paréntesis"
                MathDelimiter.BRACKETS -> "abre corchete" to "cierra corchete"
                else -> "abre llave" to "cierra llave"
            }
            "${delimiters.first}, ${speak(expression.content)}, ${delimiters.second}"
        }
        is MathExpression.Function -> {
            val name = functions[expression.name] ?: "función ${letter(expression.name)}"
            val base = expression.base?.let { " en base ${speak(it)}" }.orEmpty()
            "$name$base de ${bounded(expression.argument, "argumento") }"
        }
        is MathExpression.LargeOperator -> {
            val name = when (expression.kind) {
                MathLargeOperator.INTEGRAL -> "integral"
                MathLargeOperator.SUM -> "sumatoria"
                MathLargeOperator.PRODUCT -> "productoria"
            }
            val lower = expression.lower?.let { " desde ${speak(it)}" }.orEmpty()
            val upper = expression.upper?.let { " hasta ${speak(it)}" }.orEmpty()
            "$name$lower$upper, de"
        }
        is MathExpression.Limit -> "límite de ${speak(expression.body)}, cuando ${speak(expression.condition)}"
        is MathExpression.Derivative -> {
            val order = expression.order?.let { " de orden ${speak(it)}" }.orEmpty()
            "derivada$order de ${speak(expression.body)}, respecto de ${speak(expression.variable)}"
        }
        is MathExpression.Matrix -> "matriz. " + expression.rows.mapIndexed { index, row ->
            "Fila ${integer((index + 1).toLong())}: ${row.joinToString("; ", transform = ::speak)}"
        }.joinToString(". ")
        is MathExpression.Cases -> "expresión por casos. " + expression.rows.joinToString("; ") {
            "${speak(it.expression)}, si ${speak(it.condition)}"
        }
    })

    /** auto recognizes explicit math and safe fragments. text preserves paths/code and plain prose. */
    fun text(text: String, mathMode: String = "auto"): String = renderMathText(
        text, mathMode, ::speak, "expresión matemática no interpretable"
    )

    /** Observes the existing replacements; source and emitted speech remain unchanged. */
    internal fun textObserved(text: String, mathMode: String = "auto",
                              onPiece: (Int, Int, String) -> Unit): String = renderMathText(
        text, mathMode, ::speak, "expresión matemática no interpretable", onPiece
    )

    /** Shared parsing boundary: callers provide an AST visitor, never translated Spanish speech. */
    internal fun renderMathText(
        text: String, mathMode: String, render: (MathExpression) -> String, unreadable: String,
        onPiece: ((Int, Int, String) -> Unit)? = null
    ): String {
        if (text.isBlank()) return ""
        if (mathMode == "text") {
            onPiece?.invoke(0, text.length, text)
            return clean(text)
        }
        val forced = mathMode == "inline" || mathMode == "block"
        val trimmed = text.trim()
        if (!trimmed.contains('\\') && !trimmed.contains('$') && protectedToken.find(trimmed) == null &&
            inlineCode.find(trimmed) == null && wholeMath.matches(trimmed) &&
            (forced || trimmed.length <= 100 && !Regex("[a-zA-Z]{2,}").containsMatchIn(trimmed))) {
            parse(trimmed, content = forced && Regex("[a-zA-Z]{2,}").containsMatchIn(trimmed))?.let {
                val spoken = render(it)
                onPiece?.invoke(0, text.length, spoken)
                return spoken
            }
        }
        if (forced && trimmed.contains('\\') && !trimmed.contains("\\(") &&
            !trimmed.contains("\\[") && !trimmed.contains('$')) {
            val spoken = parse(trimmed, content = true)?.let(render) ?: unreadable
            onPiece?.invoke(0, text.length, spoken)
            return spoken
        }
        val protected = (protectedToken.findAll(text) + inlineCode.findAll(text)).map { it.range }.toList()
        val output = StringBuilder()
        var index = 0
        var plainStart = 0
        fun observeReplacement(start: Int, end: Int, spoken: String) {
            if (onPiece != null) {
                if (plainStart < start) onPiece(plainStart, start, text.substring(plainStart, start))
                onPiece(start, end, spoken)
            }
            plainStart = end
        }
        while (index < text.length) {
            val token = protected.firstOrNull { index == it.first }
            if (token != null) {
                output.append(text.substring(token.first, token.last + 1))
                index = token.last + 1
                continue
            }
            val delimiters = delimitersAt(text, index)
            if (delimiters != null) {
                val end = text.indexOf(delimiters.second, index + delimiters.first.length)
                if (end >= index + delimiters.first.length) {
                    val source = text.substring(index + delimiters.first.length, end)
                    val spoken = parse(source, content = forced)?.let(render) ?: unreadable
                    output.append(' ').append(spoken).append(' ')
                    observeReplacement(index, end + delimiters.second.length, " $spoken ")
                    index = end + delimiters.second.length
                    continue
                }
            }
            val candidate = listOf(fraction, root, power, arithmetic, relation, coordinates).mapNotNull {
                it.matchAt(text, index)
            }.maxByOrNull { it.value.length }
            if (candidate != null && safeBoundary(text, index - 1) && safeBoundary(text, candidate.range.last + 1) &&
                protected.none { it.first <= candidate.range.last && it.last >= index }) {
                val parsed = parse(candidate.value)
                if (parsed != null) {
                    val spoken = render(parsed)
                    output.append(' ').append(spoken).append(' ')
                    observeReplacement(index, candidate.range.last + 1, " $spoken ")
                    index = candidate.range.last + 1
                    continue
                }
            }
            if (text[index] in "×÷≤≥≠≈π∞∈") {
                val spoken = render(MathExpression.Symbol(text[index].toString()))
                output.append(' ').append(spoken).append(' ')
                observeReplacement(index, index + 1, " $spoken ")
            }
            else output.append(text[index])
            index++
        }
        if (plainStart < text.length) onPiece?.invoke(plainStart, text.length, text.substring(plainStart))
        return clean(output.toString())
    }

    private fun parse(source: String, content: Boolean = false): MathExpression.Sequence? {
        // Slash fractions become structural fractions only after the caller has identified math.
        val preservedCoordinates = coordinates.replace(source) { it.value.replace(",", "\\mathrm{,}") }
        val fractions = fraction.replace(preservedCoordinates) {
            val parts = it.value.split('/', limit = 2)
            "\\frac{${parts[0]}}{${parts[1]}}"
        }
        val powers = superscripts.replace(fractions) { match ->
            "^{${match.value.map { "⁰¹²³⁴⁵⁶⁷⁸⁹".indexOf(it) }.joinToString("")}}"
        }
        val normalized = root.replace(powers) { match ->
            "\\sqrt{${match.value.substring(1).removeSurrounding("(", ")")}}"
        }
        val result = if (content) MathLatexParser.parseContent(normalized) else MathLatexParser.parse(normalized)
        if (result is MathLatexParser.Result.Success) return result.expression
        // Visible formula cards may contain a question mark or prose, which answer parsing rejects.
        if (!content) return (MathLatexParser.parseContent(normalized) as? MathLatexParser.Result.Success)?.expression
        return null
    }

    private fun sequence(expression: MathExpression.Sequence): String {
        if (expression.children.isEmpty()) return "hueco"
        val children = mutableListOf<MathExpression>()
        for (child in expression.children) {
            val previous = children.lastOrNull()
            if (child is MathExpression.Number && previous is MathExpression.Number) {
                children[children.lastIndex] = MathExpression.Number(previous.value + child.value)
            } else children.add(child)
        }
        return buildString {
            children.forEachIndexed { index, child ->
                if (index > 0) {
                    val previous = children[index - 1]
                    append(if (operand(previous) && operand(child)) " por " else " ")
                }
                append(speak(child))
            }
        }
    }

    private fun operand(node: MathExpression): Boolean = when (node) {
        is MathExpression.Number, is MathExpression.Variable, is MathExpression.Fraction,
        is MathExpression.Root, is MathExpression.Group, is MathExpression.Scripts,
        is MathExpression.Function, is MathExpression.Matrix -> true
        is MathExpression.Symbol -> node.value in greek.keys || node.value in setOf("π", "e", "∞")
        else -> false
    }

    private fun fraction(expression: MathExpression.Fraction): String {
        val numerator = integerValue(expression.numerator)
        val denominator = integerValue(expression.denominator)
        val name = denominators[denominator]
        if (numerator != null && name != null) {
            val top = apocopate(number(numerator.toString()))
            return "$top $name${if (numerator == 1L || numerator == -1L) "" else "s"}"
        }
        if (simple(expression.numerator) && simple(expression.denominator)) {
            return "${speak(expression.numerator)} sobre ${speak(expression.denominator)}"
        }
        return "fracción con numerador, ${speak(expression.numerator)}; y denominador, ${speak(expression.denominator)}, fin de fracción"
    }

    private fun scripts(expression: MathExpression.Scripts): String {
        val base = if (expression.base.children.size <= 1) speak(expression.base)
            else "base, ${speak(expression.base)}; fin de base"
        val lower = expression.lower?.let { " subíndice ${speak(it)}" }.orEmpty()
        val upper = expression.upper?.let {
            when (integerValue(it)) {
                2L -> " al cuadrado"
                3L -> " al cubo"
                else -> " elevado a ${bounded(it, "exponente") }"
            }
        }.orEmpty()
        return "$base$lower$upper"
    }

    private fun simple(sequence: MathExpression.Sequence): Boolean = sequence.children.size == 1 &&
        sequence.children.single().let { it is MathExpression.Number || it is MathExpression.Variable || it is MathExpression.Symbol }

    private fun bounded(sequence: MathExpression.Sequence, name: String): String =
        if (simple(sequence)) speak(sequence) else ", ${speak(sequence)}, fin de $name"

    private fun integerValue(sequence: MathExpression.Sequence): Long? = sequence.children.joinToString("") {
        when (it) {
            is MathExpression.Number -> it.value
            is MathExpression.Operator -> if (it.value in setOf("-", "−", "+")) it.value.replace('−', '-') else "?"
            else -> "?"
        }
    }.toLongOrNull()

    private fun number(value: String): String {
        val normalized = value.replace(',', '.')
        if (normalized.startsWith('-') || normalized.startsWith('−')) return "menos ${number(normalized.substring(1))}"
        val parts = normalized.split('.', limit = 2)
        val whole = parts[0].toLongOrNull()?.let(::integer) ?: parts[0]
        return if (parts.size == 1) whole else "$whole coma ${parts[1].map { digits[it.digitToIntOrNull() ?: 0] }.joinToString(" ")}"
    }

    private fun integer(value: Long): String {
        if (value < 0) return if (value == Long.MIN_VALUE) value.toString() else "menos ${integer(-value)}"
        if (value < 30) return small[value.toInt()]
        if (value < 100) return tens[(value / 10).toInt()] + if (value % 10 == 0L) "" else " y ${integer(value % 10)}"
        if (value == 100L) return "cien"
        if (value < 1_000) return hundreds[(value / 100).toInt()] + if (value % 100 == 0L) "" else " ${integer(value % 100)}"
        if (value < 1_000_000) {
            val head = if (value / 1_000 == 1L) "mil" else "${apocopate(integer(value / 1_000))} mil"
            return head + if (value % 1_000 == 0L) "" else " ${integer(value % 1_000)}"
        }
        if (value < 1_000_000_000_000) {
            val head = if (value / 1_000_000 == 1L) "un millón" else "${apocopate(integer(value / 1_000_000))} millones"
            return head + if (value % 1_000_000 == 0L) "" else " ${integer(value % 1_000_000)}"
        }
        return value.toString()
    }

    private fun apocopate(value: String): String = when {
        value.endsWith("veintiuno") -> value.dropLast(9) + "veintiún"
        value.endsWith("uno") -> value.dropLast(3) + "un"
        else -> value
    }

    private fun letter(value: String): String = letters[value.lowercase()] ?: greek[value] ?: value
    private fun operator(value: String): String = when (value) {
        "+" -> "más"
        "-", "−" -> "menos"
        "*", "×", "·" -> "por"
        "/", "÷" -> "dividido entre"
        "%" -> "por ciento"
        "!" -> "factorial"
        "nCr" -> "combinaciones de"
        "nPr" -> "permutaciones de"
        else -> symbol(value)
    }

    private fun relation(value: String): String = when (value) {
        "=" -> "es igual a"
        "<" -> "es menor que"
        ">" -> "es mayor que"
        "≤" -> "es menor o igual que"
        "≥" -> "es mayor o igual que"
        "≠" -> "es distinto de"
        "≈" -> "es aproximadamente igual a"
        "→" -> "tiende a"
        else -> value
    }

    private fun symbol(value: String): String = when (value) {
        "π" -> "pi"
        "∞" -> "infinito"
        "∂" -> "derivada parcial"
        "∪" -> "unión"
        "∩" -> "intersección"
        "∈" -> "pertenece a"
        "?" -> "hueco"
        ":" -> "a"
        "×", "÷" -> operator(value)
        "≤", "≥", "≠", "≈" -> relation(value)
        else -> greek[value] ?: value
    }

    private fun safeBoundary(text: String, index: Int): Boolean = index !in text.indices ||
        !text[index].isLetterOrDigit() && text[index] !in "/\\_:"

    private fun delimitersAt(value: String, index: Int): Pair<String, String>? = when {
        value.startsWith("\\(", index) -> "\\(" to "\\)"
        value.startsWith("\\[", index) -> "\\[" to "\\]"
        value.startsWith("$$", index) -> "$$" to "$$"
        value[index] == '$' -> "$" to "$"
        else -> null
    }

    private fun clean(value: String): String = value.replace(Regex("\\s+"), " ")
        .replace(Regex("\\s+([,.;:!?])"), "$1").trim()

    private val digits = listOf("cero", "uno", "dos", "tres", "cuatro", "cinco", "seis", "siete", "ocho", "nueve")
    private val small = digits + listOf("diez", "once", "doce", "trece", "catorce", "quince", "dieciséis", "diecisiete", "dieciocho", "diecinueve", "veinte", "veintiuno", "veintidós", "veintitrés", "veinticuatro", "veinticinco", "veintiséis", "veintisiete", "veintiocho", "veintinueve")
    private val tens = listOf("", "", "veinte", "treinta", "cuarenta", "cincuenta", "sesenta", "setenta", "ochenta", "noventa")
    private val hundreds = listOf("", "ciento", "doscientos", "trescientos", "cuatrocientos", "quinientos", "seiscientos", "setecientos", "ochocientos", "novecientos")
    private val denominators = mapOf(2L to "medio", 3L to "tercio", 4L to "cuarto", 5L to "quinto", 6L to "sexto", 7L to "séptimo", 8L to "octavo", 9L to "noveno", 10L to "décimo", 100L to "centésimo", 1_000L to "milésimo")
    private val letters = mapOf("a" to "a", "b" to "be", "c" to "ce", "d" to "de", "e" to "e", "f" to "efe", "g" to "ge", "h" to "hache", "i" to "i", "j" to "jota", "k" to "ka", "l" to "ele", "m" to "eme", "n" to "ene", "o" to "o", "p" to "pe", "q" to "cu", "r" to "erre", "s" to "ese", "t" to "te", "u" to "u", "v" to "uve", "w" to "doble uve", "x" to "equis", "y" to "ye", "z" to "zeta")
    private val greek = mapOf("α" to "alfa", "β" to "beta", "γ" to "gamma", "δ" to "delta", "ε" to "épsilon", "θ" to "theta", "λ" to "lambda", "μ" to "mu", "σ" to "sigma", "φ" to "fi", "ω" to "omega", "Δ" to "delta", "Σ" to "sigma", "Ω" to "omega")
    private val functions = mapOf("sin" to "seno", "cos" to "coseno", "tan" to "tangente", "sec" to "secante", "csc" to "cosecante", "cot" to "cotangente", "arcsin" to "arcoseno", "arccos" to "arcocoseno", "arctan" to "arcotangente", "sinh" to "seno hiperbólico", "cosh" to "coseno hiperbólico", "tanh" to "tangente hiperbólica", "log" to "logaritmo", "ln" to "logaritmo natural", "exp" to "exponencial")
}
