package com.learngraphicsprogramming.app.narration

import com.learngraphicsprogramming.app.math.MathDelimiter
import com.learngraphicsprogramming.app.math.MathExpression
import com.learngraphicsprogramming.app.math.MathLargeOperator
import java.util.Locale

/** Each language visits the mathematical tree directly, sharing only import/detection syntax. */
object MultilingualMathSpeech {
    fun speak(expression: MathExpression, language: NarrationLanguage): String =
        if (language == NarrationLanguage.SPANISH) SpanishMathSpeech.speak(expression)
        else Visitor(language).speak(expression)

    fun text(text: String, mathMode: String = "auto", language: NarrationLanguage = NarrationLanguage.SPANISH): String {
        if (language == NarrationLanguage.SPANISH) return SpanishMathSpeech.text(text, mathMode)
        val visitor = Visitor(language)
        return SpanishMathSpeech.renderMathText(text, mathMode, visitor::speak,
            if (language == NarrationLanguage.FRENCH) "expression mathématique illisible" else "unreadable mathematical expression")
    }

    /** Carries the exact parser replacement boundaries to the visual reading projection. */
    internal fun textObserved(text: String, mathMode: String = "auto",
                              language: NarrationLanguage = NarrationLanguage.SPANISH,
                              onPiece: (Int, Int, String) -> Unit): String {
        if (language == NarrationLanguage.SPANISH) return SpanishMathSpeech.textObserved(text, mathMode, onPiece)
        val visitor = Visitor(language)
        return SpanishMathSpeech.renderMathText(text, mathMode, visitor::speak,
            if (language == NarrationLanguage.FRENCH) "expression mathématique illisible" else "unreadable mathematical expression",
            granularReading = true, onPiece = onPiece)
    }

    private class Visitor(private val language: NarrationLanguage) {
        private val french = language == NarrationLanguage.FRENCH
        private fun word(english: String, french: String): String = if (this.french) french else english

        fun speak(expression: MathExpression): String = clean(when (expression) {
            is MathExpression.Sequence -> sequence(expression)
            is MathExpression.Number -> number(expression.value)
            is MathExpression.Variable -> letter(expression.name)
            is MathExpression.Symbol -> symbol(expression.value)
            is MathExpression.Operator -> operator(expression.value)
            is MathExpression.Relation -> relation(expression.value)
            is MathExpression.Fraction -> fraction(expression)
            is MathExpression.Scripts -> {
                val base = if (expression.base.children.size <= 1) speak(expression.base)
                    else "${word("base", "base")}, ${speak(expression.base)}; ${word("end of base", "fin de la base")}"
                val lower = expression.lower?.let { " ${word("subscript", "indice")} ${speak(it)}" }.orEmpty()
                val upper = expression.upper?.let {
                    when (integerValue(it)) {
                        2L -> " ${word("squared", "au carré")}"
                        3L -> " ${word("cubed", "au cube")}"
                        else -> " ${word("to the power of", "à la puissance")} ${bounded(it, word("exponent", "l'exposant"))}"
                    }
                }.orEmpty()
                "$base$lower$upper"
            }
            is MathExpression.Root -> {
                val index = expression.index
                val degree = index?.let(::integerValue)
                val name = when {
                    index == null || degree == 2L -> word("square root", "racine carrée")
                    degree == 3L -> word("cube root", "racine cubique")
                    else -> "${word("root of index", "racine d'indice")} ${speak(index)}"
                }
                "$name ${word("of", "de")} ${bounded(expression.radicand, word("root", "la racine"))}"
            }
            is MathExpression.Group -> {
                if (expression.delimiter == MathDelimiter.ABSOLUTE) {
                    "${word("absolute value of", "valeur absolue de")} ${speak(expression.content)}, ${word("end of absolute value", "fin de la valeur absolue") }"
                } else {
                    val name = when (expression.delimiter) {
                        MathDelimiter.PARENTHESES -> word("parenthesis", "la parenthèse")
                        MathDelimiter.BRACKETS -> word("bracket", "le crochet")
                        else -> word("brace", "l'accolade")
                    }
                    "${word("open", "ouvre")} $name, ${speak(expression.content)}, ${word("close", "ferme")} $name"
                }
            }
            is MathExpression.Function -> {
                val name = (if (french) frenchFunctions else englishFunctions)[expression.name]
                    ?: "${word("function", "fonction")} ${letter(expression.name)}"
                val base = expression.base?.let { " ${word("in base", "en base")} ${speak(it)}" }.orEmpty()
                "$name$base ${word("of", "de")} ${bounded(expression.argument, word("argument", "l'argument"))}"
            }
            is MathExpression.LargeOperator -> {
                val name = when (expression.kind) {
                    MathLargeOperator.INTEGRAL -> word("integral", "intégrale")
                    MathLargeOperator.SUM -> word("summation", "somme")
                    MathLargeOperator.PRODUCT -> word("product", "produit")
                }
                val lower = expression.lower?.let { " ${word("from", "de")} ${speak(it)}" }.orEmpty()
                val upper = expression.upper?.let { " ${word("to", "à")} ${speak(it)}" }.orEmpty()
                "$name$lower$upper, ${word("of", "de") }"
            }
            is MathExpression.Limit -> "${word("limit of", "limite de")} ${speak(expression.body)}, ${word("as", "lorsque")} ${speak(expression.condition)}"
            is MathExpression.Derivative -> {
                val order = expression.order?.let { " ${word("of order", "d'ordre")} ${speak(it)}" }.orEmpty()
                "${word("derivative", "dérivée")}$order ${word("of", "de")} ${speak(expression.body)}, ${word("with respect to", "par rapport à")} ${speak(expression.variable)}"
            }
            is MathExpression.Matrix -> "${word("matrix", "matrice")}. " + expression.rows.mapIndexed { index, row ->
                "${word("Row", "Ligne")} ${integer((index + 1).toLong())}: ${row.joinToString("; ", transform = ::speak)}"
            }.joinToString(". ")
            is MathExpression.Cases -> "${word("piecewise expression", "expression par morceaux")}. " + expression.rows.joinToString("; ") {
                "${speak(it.expression)}, ${word("if", "si")} ${speak(it.condition)}"
            }
        })

        private fun sequence(expression: MathExpression.Sequence): String {
            if (expression.children.isEmpty()) return word("blank", "case vide")
            val children = mutableListOf<MathExpression>()
            for (child in expression.children) {
                val previous = children.lastOrNull()
                if (child is MathExpression.Number && previous is MathExpression.Number) children[children.lastIndex] = MathExpression.Number(previous.value + child.value)
                else children.add(child)
            }
            return buildString {
                children.forEachIndexed { index, child ->
                    if (index > 0) append(if (operand(children[index - 1]) && operand(child)) " ${word("times", "fois")} " else " ")
                    append(speak(child))
                }
            }
        }

        private fun operand(node: MathExpression): Boolean = when (node) {
            is MathExpression.Number, is MathExpression.Variable, is MathExpression.Fraction,
            is MathExpression.Root, is MathExpression.Group, is MathExpression.Scripts,
            is MathExpression.Function, is MathExpression.Matrix -> true
            is MathExpression.Symbol -> node.value in greekEnglish || node.value in setOf("π", "e", "∞")
            else -> false
        }

        private fun fraction(expression: MathExpression.Fraction): String {
            val numerator = integerValue(expression.numerator)
            val denominator = integerValue(expression.denominator)
            val name = (if (french) frenchDenominators else englishDenominators)[denominator]
            if (numerator != null && name != null) {
                val singular = numerator == 1L || numerator == -1L
                val ending = when {
                    singular || french && name.endsWith('s') -> name
                    !french && denominator == 2L -> "halves"
                    else -> name + "s"
                }
                return "${integer(numerator)} $ending"
            }
            if (simple(expression.numerator) && simple(expression.denominator)) {
                return "${speak(expression.numerator)} ${word("over", "sur")} ${speak(expression.denominator)}"
            }
            return "${word("fraction with numerator", "fraction de numérateur")}, ${speak(expression.numerator)}; ${word("and denominator", "et de dénominateur")}, ${speak(expression.denominator)}, ${word("end of fraction", "fin de la fraction")}"
        }

        private fun simple(value: MathExpression.Sequence): Boolean = value.children.size == 1 && value.children.single().let {
            it is MathExpression.Number || it is MathExpression.Variable || it is MathExpression.Symbol
        }
        private fun bounded(value: MathExpression.Sequence, name: String): String = if (simple(value)) speak(value)
            else ", ${speak(value)}, ${word("end of", "fin de")} $name"
        private fun integerValue(value: MathExpression.Sequence): Long? = value.children.joinToString("") {
            when (it) { is MathExpression.Number -> it.value; is MathExpression.Operator -> if (it.value in setOf("-", "−", "+")) it.value.replace('−', '-') else "?"; else -> "?" }
        }.toLongOrNull()

        private fun number(value: String): String {
            val normalized = value.replace(',', '.')
            if (normalized.startsWith('-') || normalized.startsWith('−')) return "${word("minus", "moins")} ${number(normalized.substring(1))}"
            val parts = normalized.split('.', limit = 2)
            val whole = parts[0].toLongOrNull()?.let(::integer) ?: parts[0]
            return if (parts.size == 1) whole else "$whole ${word("point", "virgule")} ${parts[1].map { integer((it.digitToIntOrNull() ?: 0).toLong()) }.joinToString(" ")}"
        }

        private fun integer(value: Long): String {
            if (value < 0 && value != Long.MIN_VALUE) return "${word("minus", "moins")} ${integer(-value)}"
            if (value < 0 || value >= 1_000_000_000_000L) return value.toString().map { if (it == '-') word("minus", "moins") else integer(it.digitToInt().toLong()) }.joinToString(" ")
            if (french) return frenchInteger(value)
            if (value < 20) return englishSmall[value.toInt()]
            if (value < 100) return englishTens[(value / 10).toInt()] + if (value % 10 == 0L) "" else " ${integer(value % 10)}"
            if (value < 1_000) return "${integer(value / 100)} hundred" + if (value % 100 == 0L) "" else
                (if (language == NarrationLanguage.BRITISH_ENGLISH) " and " else " ") + integer(value % 100)
            val unit = if (value < 1_000_000) 1_000L else if (value < 1_000_000_000) 1_000_000L else 1_000_000_000L
            val name = if (unit == 1_000L) "thousand" else if (unit == 1_000_000L) "million" else "billion"
            return "${integer(value / unit)} $name" + if (value % unit == 0L) "" else " ${integer(value % unit)}"
        }

        private fun frenchInteger(value: Long): String {
            if (value < 20) return frenchSmall[value.toInt()]
            if (value < 70) {
                val ten = frenchTens[(value / 10).toInt()]
                return ten + when (value % 10) { 0L -> ""; 1L -> " et un"; else -> "-${integer(value % 10)}" }
            }
            if (value < 80) return "soixante" + if (value == 71L) " et onze" else "-${integer(value - 60)}"
            if (value < 100) return "quatre-vingt" + if (value == 80L) "s" else "-${integer(value - 80)}"
            if (value < 1_000) {
                val head = if (value / 100 == 1L) "cent" else "${integer(value / 100)} cent"
                return head + if (value % 100 == 0L) { if (value / 100 > 1) "s" else "" } else " ${integer(value % 100)}"
            }
            val unit = if (value < 1_000_000) 1_000L else if (value < 1_000_000_000) 1_000_000L else 1_000_000_000L
            val count = value / unit
            val name = if (unit == 1_000L) "mille" else if (unit == 1_000_000L) "million" else "milliard"
            // Cent and vingt lose their plural before the numeral mille, but retain it before million.
            val countText = integer(count).let {
                if (unit == 1_000L && (it.endsWith("cents") || it.endsWith("vingts"))) it.dropLast(1) else it
            }
            val head = if (unit == 1_000L && count == 1L) name else "$countText $name${if (unit != 1_000L && count > 1L) "s" else ""}"
            return head + if (value % unit == 0L) "" else " ${integer(value % unit)}"
        }

        private fun letter(value: String): String = if (language == NarrationLanguage.BRITISH_ENGLISH && value.equals("z", ignoreCase = true)) "zed"
            else (if (french) frenchLetters else englishLetters)[value.lowercase(Locale.ROOT)]
                ?: (if (french) greekFrench else greekEnglish)[value] ?: value
        private fun operator(value: String): String = when (value) {
            "+" -> word("plus", "plus")
            "-", "−" -> word("minus", "moins")
            "*", "×", "·" -> word("times", "fois")
            "/", "÷" -> word("divided by", "divisé par")
            "%" -> word("percent", "pour cent")
            "!" -> word("factorial", "factorielle")
            "nCr" -> word("combinations of", "combinaisons de")
            "nPr" -> word("permutations of", "permutations de")
            else -> value
        }
        private fun relation(value: String): String = when (value) {
            "=" -> word("equals", "est égal à")
            "<" -> word("is less than", "est inférieur à")
            ">" -> word("is greater than", "est supérieur à")
            "≤" -> word("is less than or equal to", "est inférieur ou égal à")
            "≥" -> word("is greater than or equal to", "est supérieur ou égal à")
            "≠" -> word("is not equal to", "est différent de")
            "≈" -> word("is approximately equal to", "est approximativement égal à")
            "→" -> word("tends to", "tend vers")
            else -> value
        }
        private fun symbol(value: String): String = when (value) {
            "π" -> "pi"
            "∞" -> word("infinity", "infini")
            "∂" -> word("partial derivative", "dérivée partielle")
            "∪" -> word("union", "union")
            "∩" -> word("intersection", "intersection")
            "∈" -> word("belongs to", "appartient à")
            "?" -> word("blank", "case vide")
            ":" -> word("to", "à")
            "×", "÷" -> operator(value)
            "≤", "≥", "≠", "≈" -> relation(value)
            else -> (if (french) greekFrench else greekEnglish)[value] ?: value
        }
    }

    private fun clean(value: String): String = value.replace(Regex("\\s+"), " ").replace(Regex("\\s+([,.;:!?])"), "$1").trim()
    private val englishSmall = listOf("zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten", "eleven", "twelve", "thirteen", "fourteen", "fifteen", "sixteen", "seventeen", "eighteen", "nineteen")
    private val englishTens = listOf("", "", "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety")
    private val frenchSmall = listOf("zéro", "un", "deux", "trois", "quatre", "cinq", "six", "sept", "huit", "neuf", "dix", "onze", "douze", "treize", "quatorze", "quinze", "seize", "dix-sept", "dix-huit", "dix-neuf")
    private val frenchTens = listOf("", "", "vingt", "trente", "quarante", "cinquante", "soixante")
    private val englishDenominators = mapOf(2L to "half", 3L to "third", 4L to "quarter", 5L to "fifth", 6L to "sixth", 7L to "seventh", 8L to "eighth", 9L to "ninth", 10L to "tenth", 100L to "hundredth", 1_000L to "thousandth")
    private val frenchDenominators = mapOf(2L to "demi", 3L to "tiers", 4L to "quart", 5L to "cinquième", 6L to "sixième", 7L to "septième", 8L to "huitième", 9L to "neuvième", 10L to "dixième", 100L to "centième", 1_000L to "millième")
    private val englishLetters = "ay bee see dee ee ef gee aitch eye jay kay el em en oh pee cue ar ess tee you vee double-you ex why zee".split(' ').let { names -> ('a'..'z').mapIndexed { index, char -> char.toString() to names[index] }.toMap() }
    private val frenchLetters = "a bé cé dé e effe gé ache i ji ka elle emme enne o pé ku erre esse té u vé double-vé ix i-grec zède".split(' ').let { names -> ('a'..'z').mapIndexed { index, char -> char.toString() to names[index] }.toMap() }
    private val greekEnglish = mapOf("α" to "alpha", "β" to "beta", "γ" to "gamma", "δ" to "delta", "ε" to "epsilon", "θ" to "theta", "λ" to "lambda", "μ" to "mu", "σ" to "sigma", "φ" to "phi", "ω" to "omega", "Δ" to "delta", "Σ" to "sigma", "Ω" to "omega")
    private val greekFrench = greekEnglish + mapOf("β" to "bêta", "θ" to "thêta")
    private val englishFunctions = mapOf("sin" to "sine", "cos" to "cosine", "tan" to "tangent", "sec" to "secant", "csc" to "cosecant", "cot" to "cotangent", "arcsin" to "arcsine", "arccos" to "arccosine", "arctan" to "arctangent", "sinh" to "hyperbolic sine", "cosh" to "hyperbolic cosine", "tanh" to "hyperbolic tangent", "log" to "logarithm", "ln" to "natural logarithm", "exp" to "exponential")
    private val frenchFunctions = mapOf("sin" to "sinus", "cos" to "cosinus", "tan" to "tangente", "sec" to "sécante", "csc" to "cosécante", "cot" to "cotangente", "arcsin" to "arcsinus", "arccos" to "arccosinus", "arctan" to "arctangente", "sinh" to "sinus hyperbolique", "cosh" to "cosinus hyperbolique", "tanh" to "tangente hyperbolique", "log" to "logarithme", "ln" to "logarithme naturel", "exp" to "exponentielle")
}
