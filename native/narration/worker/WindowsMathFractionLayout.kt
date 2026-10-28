package lgaed.narration.windows

import com.learngraphicsprogramming.app.math.MathLatexParser
import com.learngraphicsprogramming.app.math.MathExpression
import com.learngraphicsprogramming.app.math.MathTextSemantics
import com.learngraphicsprogramming.app.math.WindowsMathFragment
import com.learngraphicsprogramming.app.math.WindowsMathTypesetting

/**
 * Native OfficeMath must own a complete fraction before RichEdit can keep its
 * numerator and denominator together. The original detector can end a relation
 * at its numerator (2 ≥ 5/2), or protect a small algebraic quotient as a path.
 * Repair only those bounded source intervals; do not change the shared detector,
 * answer parser, curriculum, or explicit/literal formula policy.
 */
internal object WindowsMathFractionLayout {
    private const val number = """[0-9]+(?:[.,][0-9]+)?"""
    private const val monomial = """(?:[0-9]+(?:[.,][0-9]+)?)?[A-Za-z]"""
    private const val binomial = """\([A-Za-z]{1,2}[ \t]*[+−-][ \t]*[A-Za-z]{1,2}\)"""
    private const val quotient = """[−-]?(?:(?:""" + number + "|" + monomial + ")/" + number +
        "|" + binomial + """/[A-Za-z])"""
    private const val atom = "(?:" + quotient + "|[−-]?(?:" + number + "|" + monomial + "))"
    private const val operation = """[+−×÷·*-]"""
    private const val relation = """[=<>≤≥≠≈]"""
    // A bare x/2 remains an ambiguous relative path. Only an adjoining bounded
    // arithmetic relation permits that local exception to path protection.
    // Ordinary words and prose conjunctions cannot extend these contexts.
    private val beforeRelation = Regex("""(?<![\p{L}\p{N}_/\\:])""" + atom +
        "(?:[ \\t]*" + operation + "[ \\t]*" + atom + ")*[ \\t]*" + relation +
        "[ \\t]*(?:" + atom + "[ \\t]*" + operation + "[ \\t]*)*$")
    private val afterRelation = Regex("^(?:[ \\t]*" + operation + "[ \\t]*" + atom +
        ")*[ \\t]*" + relation + "[ \\t]*" + atom + """(?![\p{L}\p{N}_/\\:])""")
    private val fractions = Regex(quotient)
    private val numericOperandBeforeSign = Regex("""(?<![\p{L}\p{N}_./\\:])[−-]?""" +
        number + "(?:/" + number + ")?[ \\t]*$")

    private data class Fraction(val start: Int, val end: Int, val slash: Int)
    private data class Unit(val start: Int, val end: Int, val original: WindowsMathFragment? = null,
                            val fraction: Fraction? = null)

    private fun localFalsePath(source: String, range: MathTextSemantics.Range, fraction: Fraction): Boolean {
        if (range.start >= fraction.start && range.endExclusive <= fraction.end) return true
        // The absolute-path heuristic includes terminal prose punctuation in
        // "(av − cu)/D.". Only this suffix of the slash-owned false path may
        // extend beyond the quotient; a filename extension or child may not.
        return range.start == fraction.slash && range.endExclusive > fraction.end &&
            source.substring(fraction.end, range.endExclusive).all { it in ".,;!?)]}»”’" }
    }

    private fun boundary(source: String, at: Int): Boolean =
        at !in source.indices || !(source[at].isLetterOrDigit() || source[at] in "_/\\:")

    private fun binaryMinus(source: String, at: Int, original: List<WindowsMathFragment>): Boolean {
        val prefix = source.substring(maxOf(0, at - 160), at)
        // A complete preceding numeric operand establishes subtraction even
        // when the canonical arithmetic regex did not accept ASCII '-'.
        if (numericOperandBeforeSign.containsMatchIn(prefix)) return true
        // Otherwise require an operand already owned by an original math unit.
        // A preceding prose word, including the standalone 'a' in "igual a",
        // cannot turn a unary sign into subtraction.
        return original.any { fragment ->
            if (fragment.literal || at <= fragment.start || at >= fragment.end) false
            else {
                val left = source.substring(fragment.start, at).trimEnd()
                val parsed = MathLatexParser.parse(left)
                if (parsed !is MathLatexParser.Result.Success) false
                else parsed.expression.children.lastOrNull().let {
                    it != null && it !is MathExpression.Operator && it !is MathExpression.Relation
                }
            }
        }
    }

    private fun candidates(source: String, original: List<WindowsMathFragment>): List<Fraction> =
        fractions.findAll(source).mapNotNull { match ->
        var start = match.range.first
        val end = match.range.last + 1
        // Resolve the sign before testing its boundary: in 2-3/4 the regex
        // found -3/4, but rejecting that range would also skip the inner 3/4.
        // The binary operator stays in its original source position outside
        // the new fraction; a genuine unary minus remains in the numerator.
        if (source[start] in "−-" && binaryMinus(source, start, original)) start++
        if (!boundary(source, start - 1) || !boundary(source, end)) return@mapNotNull null
        val spelling = source.substring(start, end)
        val algebraic = spelling.any { it.isLetter() }
        if (algebraic && !beforeRelation.containsMatchIn(source.substring(maxOf(0, start - 160), start)) &&
            !afterRelation.containsMatchIn(source.substring(end, minOf(source.length, end + 160))))
            return@mapNotNull null
        Fraction(start, end, start + spelling.indexOf('/'))
    }.toList()

    private fun normalize(source: String, start: Int, end: Int, fractions: List<Fraction>): String {
        val formula = StringBuilder()
        var at = start
        for (fraction in fractions.sortedBy { it.start }) {
            formula.append(source, at, fraction.start)
            var numerator = source.substring(fraction.start, fraction.slash)
            // These balanced outer parentheses are the grouping syntax of the
            // slash notation; \frac's numerator group now owns that same role.
            if (numerator.startsWith('(') && numerator.endsWith(')'))
                numerator = numerator.substring(1, numerator.length - 1)
            formula.append("\\frac{").append(numerator)
                .append("}{").append(source, fraction.slash + 1, fraction.end).append('}')
            at = fraction.end
        }
        formula.append(source, at, end)
        // Reuse the exact original coordinates/scripts/operator normalization;
        // AST serialization would rewrite visible × and Unicode minus.
        return WindowsMathTypesetting.normalizeExpression(formula.toString())
    }

    fun render(source: String, mode: String): List<WindowsMathFragment> {
        val original = WindowsMathTypesetting.render(source, mode).fragments.toList()
        if (mode != "auto") return original
        val candidates = candidates(source, original)
        if (candidates.isEmpty()) return original

        // Mask only the slash of a context-confirmed algebraic fraction, using
        // one UTF-16 unit. Re-running the unchanged exclusions removes its false
        // local path match, while enclosing URLs, code, dates, explicit formulas
        // and real drive/absolute/relative paths retain their original owners.
        val masked = source.toCharArray()
        for (fraction in candidates)
            if (source.substring(fraction.start, fraction.end).any { it.isLetter() })
                masked[fraction.slash] = '∕'
        val originalExclusions = MathTextSemantics.automaticMathExclusions(source)
        val excluded = MathTextSemantics.automaticMathExclusions(String(masked))
        val accepted = candidates.filter { fraction ->
            excluded.none { it.start < fraction.end && it.endExclusive > fraction.start } &&
                // Never weaken an enclosing path merely because changing its
                // slash made the path regex stop matching. Only the false path
                // wholly inside this complete quotient may lose protection.
                originalExclusions.none { it.start < fraction.end && it.endExclusive > fraction.start &&
                    !localFalsePath(source, it, fraction) } &&
                original.none { it.literal && it.start < fraction.end && it.end > fraction.start }
        }
        if (accepted.isEmpty()) return original

        val units = (original.map { Unit(it.start, it.end, original = it) } +
            accepted.map { Unit(it.start, it.end, fraction = it) }).sortedWith(compareBy<Unit> { it.start }.thenBy { it.end })
        val result = mutableListOf<WindowsMathFragment>()
        var index = 0
        while (index < units.size) {
            val start = units[index].start
            var end = units[index].end
            val group = mutableListOf(units[index++])
            while (index < units.size && units[index].start < end) {
                end = maxOf(end, units[index].end)
                group += units[index++]
            }
            val repairs = group.mapNotNull { it.fraction }
            if (repairs.isEmpty()) result += group.mapNotNull { it.original }
            else {
                val formula = normalize(source, start, end, repairs)
                // A bounded repair must still be accepted by the shared parser.
                // Preserve the original display on an unsupported expression.
                if (MathLatexParser.parse(formula) is MathLatexParser.Result.Success)
                    result += WindowsMathFragment(start, end, formula, false)
                else result += group.mapNotNull { it.original }
            }
        }
        return result
    }
}
