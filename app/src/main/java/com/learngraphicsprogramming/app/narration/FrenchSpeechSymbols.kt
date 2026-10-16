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
 * tokens are retained. Unambiguous C++ compound operators use their technical names;
 * ambiguous standalone glyphs retain their neutral reading rather than guessing a role.
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
        "<<=" to "décalage à gauche et affectation",
        ">>=" to "décalage à droite et affectation",
        "->*" to "pointeur vers membre sur pointeur",
        "&&" to "et logique", "||" to "ou logique",
        "==" to "est égal à", "!=" to "est différent de",
        "<=" to "est inférieur ou égal à", ">=" to "est supérieur ou égal à",
        "<<" to "double chevron ouvrant", ">>" to "double chevron fermant",
        "++" to "incrémentation", "--" to "décrémentation",
        "+=" to "addition et affectation", "-=" to "soustraction et affectation", "*=" to "multiplication et affectation",
        "/=" to "division et affectation", "%=" to "modulo et affectation",
        "&=" to "et binaire et affectation", "|=" to "ou binaire et affectation",
        "^=" to "ou exclusif binaire et affectation", "->" to "flèche",
        ".*" to "pointeur vers membre sur objet", "::" to "opérateur de résolution de portée",
        "!" to "négation logique", ";" to "point-virgule"
    )
    // Literal data keeps its graphical spelling. These historical names also make
    // the exact changed occurrences available to selective audio compatibility.
    private val graphicalOperatorNames = mapOf(
        "<<=" to "double chevron ouvrant égal", ">>=" to "double chevron fermant égal",
        "->*" to "flèche astérisque", ".*" to "point astérisque", "::" to "double deux points",
        "+=" to "plus égal", "-=" to "moins égal", "*=" to "astérisque égal",
        "/=" to "barre oblique égal", "%=" to "pour cent égal", "&=" to "esperluette égal",
        "|=" to "barre verticale égal", "^=" to "accent circonflexe égal",
        "++" to "plus plus", "--" to "moins moins", "!" to "!", ";" to ";", "&&" to "et logique"
    )
    private val urls = Regex("(?i)(?:https?://|www\\.)[^\\s<>\"`]+")
    private val links = Regex("\\[[^]\\r\\n]*]\\([^()\\r\\n]*\\)")
    private val operatorCue = Regex("(?iu)(?:opérateurs?|operators?)\\s*$")
    private val semicolonNounCue = Regex("(?iu)(?:symbole|signe|caractère|terminateur|opérateur)\\s*$")
    private val semicolonTerminationCue = Regex("(?iu)(?:termin(?:e|er|ent|ée?|ées?|és?)|fin(?:it|ir|issent))\\s+par\\s*$")
    private val semicolonActionCue = Regex("(?iu)(?<![\\p{L}\\p{N}_])(?:ajout(?:e|er|ez|ons|ent)|mets|mettre|mettez|écris|écrire|écrivez|ins(?:ère|érer|érez)|place(?:r|z)?|supprim(?:e|er|ez)|retir(?:e|er|ez))(?:\\s+pas)?\\s*$")
    private val semicolonActionTail = Regex("(?iu)^(?:à|avant|après|dans|pour|entre|sur|comme|ici)(?=\\s|$|[.,:;!?])")
    private val leftIdentifier = Regex("[\\p{L}_][\\p{L}\\p{N}_]*$")
    private val rightIdentifier = Regex("[\\p{L}_][\\p{L}\\p{N}_]*")
    private val qualifiedStdOperand = Regex("std\\s*::")
    private val commandBeforeOption = Regex("(?i)(?<![\\p{L}\\p{N}_])(?:cmake|clang\\+\\+|clang|ctest|ninja|git|gcc|g\\+\\+|gradle)(?=\\s)[^;\\r\\n`]*$")
    private val optionCueBefore = Regex("(?i)(?<![\\p{L}\\p{N}_])(?:options?|arguments?)[\\s`«“\"':-]*$")
    private val optionCueAfter = Regex("(?i)^[\\p{L}\\p{N}_-]+[\\s`»”\"',:;-]*(?:(?:est|désigne|représente|is|as)\\s+)?(?:(?:une?|l['’]|an?)\\s*)?(?:option|argument)s?(?![\\p{L}\\p{N}_])")
    private val regexCue = Regex("(?i)(?<![\\p{L}\\p{N}_])(?:regex|expressions?\\s+régulières?)(?![\\p{L}\\p{N}_])")
    private val primitiveReference = Regex("(?<![\\p{L}\\p{N}_])(?:const\\s+|volatile\\s+)*(?:auto|bool|char|char8_t|char16_t|char32_t|wchar_t|short|int|long|float|double|signed|unsigned)(?:\\s+(?:short|long|int|double|char))*(?:\\s+(?:const|volatile))*\\s*$")
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

    internal data class ResolvedOperator(
        val start: Int, val endExclusive: Int, val symbol: String, val words: String?, val changed: Boolean
    )

    /** Decide context before foreign operands become opaque and code delimiters disappear. */
    internal fun resolvedOperatorReadings(text: String): List<ResolvedOperator> {
        val annotations = ManualEnglishSpeech.spans(text)
        val visible = StringBuilder(text.length)
        val owners = mutableListOf<Pair<Int, Int>>()
        fun plain(start: Int, end: Int) {
            for (index in start until end) if (text[index] != ManualEnglishSpeech.PREPARED) {
                visible.append(text[index]); owners += index to index + 1
            }
        }
        var cursor = 0
        annotations.forEach { span ->
            plain(cursor, span.start)
            visible.append(span.legacyText)
            repeat(span.legacyText.length) { owners += span.start to span.endExclusive }
            cursor = span.endExclusive
        }
        plain(cursor, text.length)
        val resolved = mutableListOf<ResolvedOperator>()
        normalizeImpl(visible.toString(), null, true, null) { operator ->
            val source = owners.subList(operator.start, operator.endExclusive)
            resolved += operator.copy(start = source.minOf { it.first }, endExclusive = source.maxOf { it.second })
        }
        return resolved
    }

    /** Original offsets and only the operator readings that differ from the previous audio. */
    internal fun changedOperatorReadings(text: String): List<Triple<Int, Int, String>> =
        resolvedOperatorReadings(text).filter { it.changed }.map { Triple(it.start, it.endExclusive, requireNotNull(it.words)) }

    /** Exact source ownership of the same residual-symbol rewrite used for synthesis. */
    internal fun normalizeObserved(text: String, onPiece: ((Int, Int, String) -> Unit)? = null): String =
        normalizeResolvedObserved(text, resolvedOperatorReadings(text), onPiece)

    /** Consume the original decisions after an existing caller projects their source owners. */
    internal fun normalizeResolvedObserved(text: String, operators: List<ResolvedOperator>,
        onPiece: ((Int, Int, String) -> Unit)? = null): String =
        normalizeImpl(text, onPiece, true, operators.associateBy { it.start }, null)

    private fun normalizeImpl(text: String, onPiece: ((Int, Int, String) -> Unit)?, technicalNames: Boolean,
        resolvedOperators: Map<Int, ResolvedOperator>?, onResolvedOperator: ((ResolvedOperator) -> Unit)?): String {
        val out = StringBuilder(text.length)
        val literals = if (technicalNames) quotedData(text) else BooleanArray(text.length) { true }
        val code = codeNotation(text)
        val annotations = ManualEnglishSpeech.spans(text).associateBy { it.start }
        val omitBackticks = text.startsWith(ManualEnglishSpeech.PREPARED) && annotations.isNotEmpty()
        val owners = if (onPiece == null) null else mutableListOf<Pair<Int, Int>>()
        fun record(start: Int, end: Int, rendered: String) {
            out.append(rendered)
            owners?.let { ranges -> repeat(rendered.length) { ranges += start to end } }
        }
        var index = 0
        var changed = false
        while (index < text.length) {
            val annotation = annotations[index]
            if (annotation != null) {
                record(index, annotation.endExclusive, text.substring(index, annotation.endExclusive))
                index = annotation.endExclusive
                continue
            }
            if (text[index] == ManualEnglishSpeech.PREPARED || (omitBackticks && text[index] == '`')) {
                index++
                changed = true
                continue
            }
            val special = characterLiteral(text, index) ?: unitNotation(text, index) ?: script(text, index)
            if (special != null) {
                record(index, special.endExclusive, " ${special.words} ")
                index = special.endExclusive
                changed = true
                continue
            }
            fun technical(symbol: String) = if (symbol == ";") {
                technicalNames && semicolonAt(text, index, code[index], literals[index])
            } else !literals[index] && when (symbol) {
                "++", "--" -> incrementAt(text, index, symbol, code[index])
                "!" -> negationAt(text, index, code[index])
                else -> true
            }
            val operator = operators.firstOrNull { text.startsWith(it.first, index) }
            if (operator != null) {
                val graphical = graphicalOperatorNames[operator.first]
                val semantic = technical(operator.first)
                val original = resolvedOperators?.get(index)
                if (original != null) check(original.symbol == operator.first && original.endExclusive == index + operator.first.length)
                val words = if (original != null) original.words else when {
                    operator.first in listOf("!", ";") && !semantic -> null
                    !semantic -> graphical ?: operator.second
                    operator.first in listOf(".*", "->*") && regularExpressionContext(text, index) -> graphical ?: operator.second
                    operator.first == "&&" && primitiveReference.containsMatchIn(text.substring(0, index)) -> "double esperluette"
                    else -> operator.second
                }
                val differs = original?.changed ?: (words != null && graphical != null && words != graphical)
                onResolvedOperator?.invoke(ResolvedOperator(index, index + operator.first.length, operator.first, words, differs))
                record(index, index + operator.first.length, if (words == null) operator.first else " $words ")
                index += operator.first.length
                if (words != null) changed = true
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

    /** Quote boundaries only; no type inference or reinterpretation of quoted C++ data. */
    private fun quotedData(text: String): BooleanArray {
        val protected = BooleanArray(text.length)
        var index = 0
        while (index < text.length) {
            val closing = when (text[index]) { '"' -> '"'; '“' -> '”'; '«' -> '»'; else -> null }
            if (closing == null) { index++; continue }
            var end = index + 1
            // A C++ raw string may contain unescaped quotation marks.
            val open = if (text[index] == '"' && text.getOrNull(index - 1) == 'R') text.indexOf('(', end) else -1
            val delimiter = if (open in end..minOf(text.length - 1, end + 16)) text.substring(end, open) else null
            if (delimiter != null && delimiter.none { it.isWhitespace() || it in "()\\" }) {
                val rawEnd = text.indexOf(")$delimiter\"", open + 1)
                end = if (rawEnd >= 0) rawEnd + delimiter.length + 2 else text.length
            } else {
                while (end < text.length) {
                    if (text[end] == '\\' && end + 1 < text.length) { end += 2; continue }
                    if (text[end++] == closing) break
                }
            }
            for (position in index until end) protected[position] = true
            index = end
        }
        for (range in (urls.findAll(text) + links.findAll(text)).map { it.range }) {
            for (position in range) protected[position] = true
        }
        return protected
    }

    private fun codeNotation(text: String): BooleanArray {
        val result = BooleanArray(text.length)
        var index = 0
        while (index < text.length) {
            if (text[index] != '`') { index++; continue }
            val delimiter = if (text.startsWith("```", index)) "```" else "`"
            val closing = text.indexOf(delimiter, index + delimiter.length)
            if (closing < 0) break
            for (position in index until closing + delimiter.length) result[position] = true
            index = closing + delimiter.length
        }
        return result
    }

    private fun incrementAt(text: String, start: Int, symbol: String, code: Boolean): Boolean {
        val before = text.substring(0, start)
        val previous = leftIdentifier.find(before.trimEnd())?.value
        // The language name is never an increment expression, including C++20/CLI.
        if (symbol == "++" && previous?.lowercase() in setOf("c", "clang", "g")) return false
        if (symbol == "--" && (commandBeforeOption.containsMatchIn(before) || optionCueBefore.containsMatchIn(before) ||
                optionCueAfter.containsMatchIn(text.substring(start + 2)))) return false
        if (code || operatorCue.containsMatchIn(before)) return true
        val next = rightIdentifier.matchAt(text, start + symbol.length)
        val prefixBoundary = before.lastOrNull()?.let { it.isWhitespace() || it in "([=,;{?:&|!+-*/%" } != false
        if (next != null && prefixBoundary) return true
        val after = text.substring(start + symbol.length).trimStart()
        // A short variable or a complete statement is a reliable postfix context.
        // A prose dash such as attention-- receives no new semantic interpretation.
        return previous != null && (previous.length == 1 || after.firstOrNull() in listOf(';', ')', ']', ','))
    }

    private fun regularExpressionContext(text: String, start: Int): Boolean {
        val before = text.substring(0, start).substringAfterLast(';').substringAfterLast('\n').takeLast(180)
        val after = text.substring(start + if (text.startsWith("->*", start)) 3 else 2)
            .substringBefore(';').substringBefore('\n').take(180)
        return regexCue.containsMatchIn(before) || regexCue.containsMatchIn(after)
    }

    private fun negationAt(text: String, start: Int, code: Boolean): Boolean {
        if (code || operatorCue.containsMatchIn(text.substring(0, start))) return true
        val before = text.getOrNull(start - 1)
        val after = text.getOrNull(start + 1)
        // Existing narration cleanup removes a prose space before !. A directly
        // qualified C++ operand remains unambiguous even in "teste!std::empty".
        // A preceding number still keeps its existing postfix factorial reading.
        if (before?.isDigit() != true && qualifiedStdOperand.matchAt(text, start + 1) != null) return true
        return (before == null || before.isWhitespace() || before in "([=,;{?:&|!+-*/%") &&
            after != null && (after.isLetterOrDigit() || after in "_(!")
    }

    private fun semicolonAt(text: String, start: Int, code: Boolean, literal: Boolean): Boolean {
        var left = start - 1
        var right = start + 1
        while (left >= 0 && text[left].isWhitespace()) left--
        while (right < text.length && text[right].isWhitespace()) right++
        val quote = if (left >= 0) when (text[left]) { '"' -> '"'; '“' -> '”'; '«' -> '»'; '`' -> '`'; else -> null } else null
        val quotedAlone = quote != null && text.getOrNull(right) == quote
        if (literal) return quotedAlone
        if (quotedAlone || code || text.trim() == ";") return true
        val before = text.substring(0, start)
        if (semicolonNounCue.containsMatchIn(before) || semicolonTerminationCue.containsMatchIn(before)) return true
        val after = text.substring(start + 1).trimStart()
        return semicolonActionCue.containsMatchIn(before) &&
            (after.isEmpty() || after.first() in ".,!?" || semicolonActionTail.containsMatchIn(after))
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
            } }.joinToString(" ") else normalizeImpl(content, null, false, null, null)
        }
        return Reading(end + 1, "guillemet simple ouvrant $words guillemet simple fermant")
    }
}
