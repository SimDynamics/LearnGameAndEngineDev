package com.learngraphicsprogramming.app.narration

import com.learngraphicsprogramming.app.math.MathExpression
import com.learngraphicsprogramming.app.math.MathLatexParser
import java.text.Normalizer

/**
 * Reads residual technical notation at the French phonemizer boundary. Math expressions
 * already pass through MultilingualMathSpeech; this covers notation left in prose, code
 * identifiers and older durable queues. It never rewrites authored or displayed content.
 *
 * Existing Kokoro punctuation, French apostrophes/compound-word hyphens and opaque foreign
 * tokens are retained. Only notation that the French frontend could not synthesize changes.
 */
internal object FrenchSpeechSymbols {
    private val superscriptCharacters =
        "⁰¹²³⁴⁵⁶⁷⁸⁹⁺⁻⁼⁽⁾ᵃᵇᶜᵈᵉᶠᵍʰⁱʲᵏˡᵐⁿᵒᵖʳˢᵗᵘᵛʷˣʸᶻ" +
            "ᴬᴮᴰᴱᴳᴴᴵᴶᴷᴸᴹᴺᴼᴾᴿᵀᵁⱽᵂᵅᵝᵞᵟᵋᶿᶥᶲ"
    private val subscriptCharacters = "₀₁₂₃₄₅₆₇₈₉₊₋₌₍₎ₐₑₒₓₔₕₖₗₘₙₚₛₜᵢᵣᵤᵥᵦᵧᵨᵩᵪ"
    private val units = linkedMapOf(
        "dam" to "décamètre", "km" to "kilomètre", "hm" to "hectomètre",
        "dm" to "décimètre", "cm" to "centimètre", "mm" to "millimètre",
        "µm" to "micromètre", "μm" to "micromètre", "m" to "mètre",
        "kg" to "kilogramme", "mg" to "milligramme", "g" to "gramme",
        "mL" to "millilitre", "L" to "litre", "min" to "minute", "h" to "heure", "s" to "seconde"
    )
    private val unitPattern = units.keys.joinToString("|") { Regex.escape(it) }.let {
        Regex("(?<![\\p{L}\\p{N}_])($it)([²³]?)(?:\\s*/\\s*($it)([²³]?))?(?![\\p{L}\\p{N}_])")
    }
    private data class Reading(val endExclusive: Int, val words: String)

    private val operators = listOf(
        "<=>" to "comparaison à trois voies",
        "<<=" to "double chevron ouvrant égal",
        ">>=" to "double chevron fermant égal",
        "->*" to "flèche astérisque",
        "&&" to "et logique", "||" to "ou logique",
        "==" to "est égal à", "!=" to "est différent de",
        "<=" to "est inférieur ou égal à", ">=" to "est supérieur ou égal à",
        "<<" to "double chevron ouvrant", ">>" to "double chevron fermant",
        "++" to "plus plus", "--" to "moins moins",
        "+=" to "plus égal", "-=" to "moins égal", "*=" to "astérisque égal",
        "/=" to "barre oblique égal", "%=" to "pour cent égal",
        "&=" to "esperluette égal", "|=" to "barre verticale égal",
        "^=" to "accent circonflexe égal", "->" to "flèche",
        ".*" to "point astérisque"
    )
    private val symbols = mapOf(
        '+' to "plus", '=' to "égal", '/' to "barre oblique", '\\' to "barre oblique inverse",
        '<' to "inférieur à", '>' to "supérieur à", '_' to "trait de soulignement",
        '{' to "accolade ouvrante", '}' to "accolade fermante",
        '[' to "crochet ouvrant", ']' to "crochet fermant",
        '&' to "esperluette", '|' to "barre verticale", '#' to "dièse",
        '*' to "astérisque", '%' to "pour cent", '^' to "accent circonflexe",
        '~' to "tilde", '`' to "accent grave", '@' to "arobase",
        '$' to "dollar", '€' to "euro", '£' to "livre sterling",
        '−' to "moins", '–' to "tiret", '×' to "fois", '÷' to "divisé par",
        '≤' to "inférieur ou égal à", '≥' to "supérieur ou égal à",
        '≠' to "différent de", '≈' to "approximativement égal à",
        '°' to "degré", '§' to "section", '⁺' to "plus", '⁻' to "moins"
    )

    fun normalize(text: String): String = normalizeObserved(text)

    /** Exact source ownership of the same residual-symbol rewrite used for synthesis. */
    internal fun normalizeObserved(text: String, onPiece: ((Int, Int, String) -> Unit)? = null): String {
        val out = StringBuilder(text.length)
        val owners = if (onPiece == null) null else mutableListOf<Pair<Int, Int>>()
        fun record(start: Int, end: Int, rendered: String) {
            out.append(rendered)
            owners?.let { ranges -> repeat(rendered.length) { ranges += start to end } }
        }
        var index = 0
        var changed = false
        while (index < text.length) {
            val special = characterLiteral(text, index) ?: unitNotation(text, index) ?: script(text, index)
            if (special != null) {
                record(index, special.endExclusive, " ${special.words} ")
                index = special.endExclusive
                changed = true
                continue
            }
            val operator = operators.firstOrNull { text.startsWith(it.first, index) }
            if (operator != null) {
                record(index, index + operator.first.length, " ${operator.second} ")
                index += operator.first.length
                changed = true
                continue
            }
            val char = text[index]
            // Number normalization can turn an existing numeric range into a valid
            // compound token too. Keep that legacy reading; mathematical ASTs already
            // name subtraction before this boundary.
            val wordHyphen = char == '-' && text.getOrNull(index - 1)?.isLetterOrDigit() == true &&
                text.getOrNull(index + 1)?.isLetterOrDigit() == true
            val word = when {
                char == '-' && !wordHyphen -> "moins"
                char in "'‘’" && text.getOrNull(index - 1)?.isLetterOrDigit() != true -> "apostrophe"
                else -> symbols[char]
            }
            if (word == null) record(index, index + 1, char.toString())
            else {
                record(index, index + 1, " $word ")
                changed = true
            }
            index++
        }
        val result = if (!changed) text else out.toString().replace(Regex("[ \\t]+"), " ")
            .replace(Regex(" +([,.;:!?])"), "$1").trim()
        if (onPiece != null && owners != null) {
            var cursor = 0
            for (char in result) {
                while (cursor < out.length && out[cursor] != char && out[cursor].isWhitespace() &&
                    !(char.isWhitespace() && out[cursor].isWhitespace())) cursor++
                // Cleanup changes only whitespace; retaining every non-space glyph is mandatory.
                check(cursor < out.length && (out[cursor] == char || (char.isWhitespace() && out[cursor].isWhitespace())))
                val owner = owners[cursor]
                onPiece(owner.first, owner.second, char.toString())
                cursor++
            }
        }
        return result
    }

    private fun unitNotation(text: String, start: Int): Reading? {
        if (!text[start].isLetter() && text[start] != 'µ') return null
        val match = unitPattern.matchAt(text, start) ?: return null
        val (base, exponent, denominator, denominatorExponent) = match.destructured
        // Plain units retain their existing reading. Only formerly unsupported square/cubic
        // notation or a unit fraction requires this residual mathematical pronunciation.
        if (exponent.isEmpty() && denominator.isEmpty()) return null
        fun unit(name: String, power: String) = units.getValue(name) + when (power) {
            "²" -> " carré"
            "³" -> " cube"
            else -> ""
        }
        val words = unit(base, exponent) + if (denominator.isEmpty()) ""
            else " par " + unit(denominator, denominatorExponent)
        return Reading(match.range.last + 1, words)
    }

    private fun script(text: String, start: Int): Reading? {
        val subscript = text[start] in subscriptCharacters
        val characters = if (subscript) subscriptCharacters else superscriptCharacters
        if (text[start] !in characters) return null
        var end = start + 1
        while (end < text.length && text[end] in characters) end++
        val decoded = Normalizer.normalize(text.substring(start, end), Normalizer.Form.NFKC)
        if (!subscript && decoded in listOf("+", "-", "−")) {
            // An isolated superscript sign can denote a charge rather than an exponent.
            return Reading(end, if (decoded == "+") "plus" else "moins")
        }
        if (!subscript && decoded == "2") return Reading(end, "au carré")
        if (!subscript && decoded == "3") return Reading(end, "au cube")
        val expression = (MathLatexParser.parse(decoded) as? MathLatexParser.Result.Success)?.expression
        val words = expression?.let { MultilingualMathSpeech.speak(it, NarrationLanguage.FRENCH) }
            // Keep an explicit reading if a future script combines unsupported syntax;
            // neither a failed parse nor unfamiliar notation may silently disappear.
            ?: decoded.map { char ->
                when {
                    char.isLetter() -> MultilingualMathSpeech.speak(MathExpression.Variable(char.toString()), NarrationLanguage.FRENCH)
                    char.isDigit() -> char.toString()
                    else -> symbols[char] ?: char.toString()
                }
            }.joinToString(" ")
        val named = words.replace("ρ", "rhô").replace("χ", "khi").replace("ə", "schwa")
            .replace("ɛ", "e ouvert").replace("ι", "iota")
        val prefix = if (subscript) "indice" else "à la puissance"
        val bounded = expression?.children?.size?.let { it > 1 } == true
        return Reading(end, "$prefix $named" + if (bounded) {
            if (subscript) ", fin de l'indice" else ", fin de l'exposant"
        } else "")
    }

    private fun characterLiteral(text: String, start: Int): Reading? {
        if (text[start] !in "'‘’" || text.getOrNull(start - 1)?.isLetterOrDigit() == true) return null
        val closing = if (text[start] == '\'') '\'' else '’'
        var end = start + 1
        while (end < text.length) {
            if (text[end] == '\\' && end + 1 < text.length) { end += 2; continue }
            // French elisions inside quoted prose are not the closing quotation mark.
            if (text[end] == closing && !(text.getOrNull(end - 1)?.isLetter() == true &&
                    text.getOrNull(end + 1)?.isLetter() == true)) break
            end++
        }
        if (end >= text.length || text[end] != closing) return null
        val content = text.substring(start + 1, end)
        val words = when (content) {
            "" -> "texte vide"
            " ", "\\s" -> "espace"
            "\n", "\\n" -> "saut de ligne"
            "\t", "\\t" -> "tabulation"
            "\r", "\\r" -> "retour chariot"
            "\\0" -> "caractère nul"
            "\\'", "'" -> "apostrophe"
            "\\\"", "\"" -> "guillemet double"
            "\\\\", "\\" -> "barre oblique inverse"
            "!" -> "point d'exclamation"
            "?" -> "point d'interrogation"
            "." -> "point"
            "," -> "virgule"
            ";" -> "point virgule"
            ":" -> "deux points"
            "(" -> "parenthèse ouvrante"
            ")" -> "parenthèse fermante"
            "-" -> "moins"
            else -> if (content.length == 1 && content.single().isLetter()) {
                MultilingualMathSpeech.speak(MathExpression.Variable(content.lowercase()), NarrationLanguage.FRENCH)
            } else if (content.isBlank()) content.map { char -> when (char) {
                '\n' -> "saut de ligne"
                '\t' -> "tabulation"
                '\r' -> "retour chariot"
                else -> "espace"
            } }.joinToString(" ") else normalize(content)
        }
        return Reading(end + 1, "guillemet simple ouvrant $words guillemet simple fermant")
    }
}
