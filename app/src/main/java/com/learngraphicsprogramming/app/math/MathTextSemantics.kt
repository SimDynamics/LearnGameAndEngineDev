package com.learngraphicsprogramming.app.math

import com.learngraphicsprogramming.app.narration.SpanishMathSpeech

/** Mathematical source ownership for decoration only; no rendering or narration is changed. */
internal object MathTextSemantics {
    data class Range(val start: Int, val endExclusive: Int)

    private const val MATHEMATICAL = "\u0000mathematical-source\u0000"
    private const val NUMBER = "[−+-]?[0-9]+(?:[.,][0-9]+)?"
    private val standaloneNumber = Regex(NUMBER)
    // Enumerations remain plain prose around their numbers (12 y 4).
    private val integerEnumeration = Regex("[−-]?[0-9]+(?:\\s*,\\s*[−-]?[0-9]+)*\\s+y\\s+[−-]?[0-9]+")

    // A mathematical quantity owns its number and any adjacent measurement/counting
    // unit. Ordinary prose is not decorated just because it mentions a common noun.
    // These are spellings for decoration only; no text or audio is rewritten.
    private val measurementUnits = setOf(
        "mm", "cm", "dm", "m", "km", "nm", "µm", "μm",
        "mm²", "cm²", "m²", "km²", "mm³", "cm³", "m³",
        "mg", "g", "kg", "t", "ml", "cl", "dl", "l", "hl",
        "ms", "s", "min", "h", "km/h", "m/s", "°c", "°f", "%",
        "milímetro", "milímetros", "centímetro", "centímetros",
        "metro", "metros", "kilómetro", "kilómetros", "kilometro", "kilometros",
        "gramo", "gramos", "kilogramo", "kilogramos", "litro", "litros",
        "segundo", "segundos", "minuto", "minutos", "hora", "horas",
        "día", "días", "semana", "semanas", "mes", "meses", "año", "años",
        "pieza", "piezas", "unidad", "unidades", "decena", "decenas",
        "centena", "centenas", "millar", "millares",
        "caja", "cajas", "paquete", "paquetes", "bloque", "bloques",
        "listón", "listones", "objeto", "objetos", "elemento", "elementos",
        "moneda", "monedas", "peso", "pesos", "dólar", "dólares", "euro", "euros",
        "millimeter", "millimeters", "millimetre", "millimetres",
        "centimeter", "centimeters", "centimetre", "centimetres",
        "meter", "meters", "metre", "metres", "kilometer", "kilometers",
        "kilometre", "kilometres", "gram", "grams", "kilogram", "kilograms",
        "liter", "liters", "litre", "litres", "second", "seconds",
        "minute", "minutes", "hour", "hours", "day", "days",
        "week", "weeks", "month", "months", "year", "years",
        "piece", "pieces", "unit", "units", "item", "items",
        "box", "boxes", "object", "objects", "block", "blocks",
        "dollar", "dollars", "pound", "pounds",
        "millimètre", "millimètres", "centimètre", "centimètres",
        "mètre", "mètres", "kilomètre", "kilomètres", "gramme", "grammes",
        "kilogramme", "kilogrammes", "seconde", "secondes",
        "heure", "heures", "jour", "jours", "semaine", "semaines",
        "mois", "an", "ans", "année", "années",
        "pièce", "pièces", "unité", "unités", "boîte", "boîtes",
        "objet", "objets", "bloc", "blocs"
    )
    private val unitToken = Regex("""[\p{L}\p{M}µμ°%]+(?:[²³])?(?:/[\p{L}\p{M}]+)?""")
    private val explicitUnitLabel = Regex(
        """(?iu)\b(?:unidad(?:es)?|unit(?:s)?|unité(?:s)?|medida|measure|mesure)\s*:\s*([\p{L}\p{M}µμ°%]+(?:[²³])?(?:/[\p{L}\p{M}]+)?)"""
    )
    private val cppVersion = Regex("""(?i)(?<![\p{L}\p{N}_])c\+\+\d+(?:\.\d+)?""")
    // A renderer/speech fragment may end before the relation in 6 × 100 = 600.
    // Decoration owns that complete numeric chain, without changing either fragment.
    private val numericChain = Regex("$NUMBER(?:\\s*[×÷·+−*/=<>≤≥≠≈-]\\s*$NUMBER)+")
    // Keep the established detector's code, URL, path and date exclusions. Explicit
    // formulas remain entirely owned by that detector, including parse failures.
    private val protectedSource = Regex(
        "https?://\\S+|[A-Za-z]:[/\\\\]\\S+|(?<![\\p{L}\\p{N}\\\\])/\\S+|" +
            "[A-Za-z_][\\w.-]*[/\\\\][\\w./\\\\-]+|\\d{1,4}/\\d{1,2}/\\d{1,4}|" +
            "```[\\s\\S]*?```|`[^`]*`|\\\\\\([\\s\\S]*?\\\\\\)|\\\\\\[[\\s\\S]*?\\\\\\]|" +
            "\\$\\$[\\s\\S]*?\\$\\$|\\$[^$]*\\$"
    )

    /** Balanced formatting delimiters only; callers preserve formula-owned source spans. */
    fun wrappers(source: CharSequence): List<Range> {
        val text = source.toString()
        val result = mutableListOf<Range>()
        fun escaped(position: Int): Boolean {
            var index = position - 1
            while (index >= 0 && text[index] == '\\') index--
            return (position - index - 1) % 2 != 0
        }
        var cursor = 0
        while (cursor < text.length) {
            if (text[cursor] != '`' || escaped(cursor)) { cursor++; continue }
            var openingEnd = cursor + 1
            while (openingEnd < text.length && text[openingEnd] == '`') openingEnd++
            val width = openingEnd - cursor
            if (width > 3) { cursor = openingEnd; continue }
            val delimiter = "`".repeat(width)
            var closing = text.indexOf(delimiter, openingEnd)
            while (closing >= 0 && (escaped(closing) || text.getOrNull(closing - 1) == '`' ||
                    text.getOrNull(closing + width) == '`')) closing = text.indexOf(delimiter, closing + width)
            if (closing >= 0) {
                val body = text.substring(openingEnd, closing)
                if (body.isNotBlank() && (width == 3 || body.none { it == '\n' || it == '\r' })) {
                    result += Range(cursor, openingEnd)
                    result += Range(closing, closing + width)
                    cursor = closing + width
                    continue
                }
            }
            cursor = openingEnd
        }
        return result
    }

    /**
     * Uses the established bounded detector and AST parser, with a language-neutral visitor.
     * Offsets are UTF-16 into the supplied text, including any existing explicit delimiters.
     * For an already typeset TextView, callers also retain its existing formula-span ranges:
     * those spans own their source even when typesetting removed the explicit delimiters.
     */
    fun ranges(source: CharSequence, mode: String = "auto"): List<Range> {
        val text = source.toString()
        if (text.isBlank() || mode == "text") return emptyList()
        val found = mutableListOf<Range>()
        if (mode != "auto" || !integerEnumeration.matches(text.trim())) {
            SpanishMathSpeech.renderMathText(text, mode, { MATHEMATICAL }, "") { start, end, rendered ->
                if (rendered.trim() != MATHEMATICAL) return@renderMathText
                var first = start
                var last = end
                while (first < last && text[first].isWhitespace()) first++
                while (last > first && text[last - 1].isWhitespace()) last--
                if (first < last) found += Range(first, last)
            }
        }
        if (mode == "auto") {
            val protected = (protectedSource.findAll(text).map { it.range } +
                cppVersion.findAll(text).map { it.range }).toList()
            fun protectedOverlap(first: Int, end: Int): Boolean =
                protected.any { it.first < end && it.last >= first }
            numericChain.findAll(text).forEach { candidate ->
                val first = candidate.range.first
                val end = candidate.range.last + 1
                if (safeNumericBoundary(text, first - 1) && safeNumericBoundary(text, end) &&
                    !protectedOverlap(first, end) &&
                    MathLatexParser.parse(candidate.value) is MathLatexParser.Result.Success) {
                    found += Range(first, end)
                }
            }

            // A bare number is mathematical too; a single numeral must not require
            // an equation sign. Attach units, but never absorb prose or punctuation.
            standaloneNumber.findAll(text).forEach { match ->
                val first = match.range.first
                val end = match.range.last + 1
                if (safeNumericBoundary(text, first - 1) &&
                    safeNumericBoundary(text, end) && !protectedOverlap(first, end)) {
                    val withUnit = followingUnitEnd(text, end)
                    found += Range(first, withUnit.takeIf { !protectedOverlap(first, it) } ?: end)
                }
            }
            // Some puzzles display a unit separately: "Unidad: cm".
            explicitUnitLabel.findAll(text).forEach { match ->
                val unit = match.groups[1] ?: return@forEach
                val first = unit.range.first
                val end = unit.range.last + 1
                if (unit.value.lowercase() in measurementUnits &&
                    safeNumericBoundary(text, end) && !protectedOverlap(first, end)) {
                    found += Range(first, end)
                }
            }
        }
        val result = mutableListOf<Range>()
        found.sortedWith(compareBy<Range> { it.start }.thenBy { it.endExclusive }).forEach { range ->
            val previous = result.lastOrNull()
            if (previous != null && range.start <= previous.endExclusive) {
                result[result.lastIndex] = previous.copy(endExclusive = maxOf(previous.endExclusive, range.endExclusive))
            } else result += range
        }
        return result
    }

    private fun followingUnitEnd(text: String, from: Int): Int {
        var cursor = from
        // A quantity never crosses a line break to borrow an unrelated label.
        while (cursor < text.length && text[cursor] in " \t\u00a0\u202f") cursor++
        val match = unitToken.matchAt(text, cursor) ?: return from
        val end = match.range.last + 1
        return if (match.value.lowercase() in measurementUnits &&
            safeNumericBoundary(text, end)) end else from
    }

    private fun safeNumericBoundary(text: String, index: Int): Boolean {
        if (index !in text.indices) return true
        if (text[index].isLetterOrDigit() || text[index] in "/\\_:") return false
        // A sentence stop is outside the expression; a decimal/version continuation
        // must not let a suffix of an ordinary identifier become a new expression.
        return text[index] != '.' ||
            !(text.getOrNull(index - 1)?.isDigit() == true && text.getOrNull(index + 1)?.isDigit() == true)
    }
}
