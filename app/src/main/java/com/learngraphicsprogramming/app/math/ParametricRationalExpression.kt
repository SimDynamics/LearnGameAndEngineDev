package com.learngraphicsprogramming.app.math

import java.math.BigInteger

/**
 * Exact, bounded arithmetic in Q[declared parameters]. Equality uses a polynomial cross product,
 * never substitutions. Every division retains a nonzero obligation before any cancellation,
 * including divisions in zero terms or bases raised to zero.
 *
 * Supported notation: rational decimal constants, explicitly declared names, + - * /, grouping,
 * implicit multiplication and integer powers 0..8. Definitions and nonzero assumptions must be
 * polynomials (constant rational denominators are allowed). An obligation is certified using exact
 * polynomial division by the declared nonzero polynomials. A missing domain condition is proved
 * when setting one parameter to zero annihilates the divisor but none of the hypotheses. Other
 * undecided obligations produce a technical retry, never an incorrect algebra verdict.
 */
object ParametricRationalExpression {
    const val MAX_SOURCE_LENGTH = 4096
    const val MAX_PARAMETERS = 8
    const val MAX_DEFINITIONS = 16
    const val MAX_NONZERO = 16
    const val MAX_POWER = 8
    const val MAX_TOTAL_DEGREE = 64
    const val MAX_TERMS = 512
    const val MAX_DEPTH = 64
    const val MAX_COEFFICIENT_BITS = 4096
    const val MAX_OPERATIONS = 200_000
    const val MAX_LITERAL_DIGITS = 1024
    const val MAX_OBLIGATIONS = 128

    class UnsupportedException(val reason: String) : IllegalArgumentException(reason)
    class ResourceLimitException(val reason: String) : IllegalArgumentException(reason)

    enum class Comparison { CORRECT, VALUE_MISMATCH, DOMAIN_MISMATCH }

    /** Reduced exact rational; no floating representation is involved. */
    data class ExactRational internal constructor(val numerator: BigInteger, val denominator: BigInteger) {
        override fun toString(): String = if (denominator == BigInteger.ONE) numerator.toString()
            else "$numerator/$denominator"
    }

    /** All limits are inclusive and are checked before expanding expressions or converting digits. */
    fun compile(
        parameters: List<String>, definitions: Map<String, String>, nonzero: List<String>, expected: String
    ): Contract {
        guardSource(expected)
        definitions.values.forEach(::guardSource)
        nonzero.forEach(::guardSource)
        if (parameters.size > MAX_PARAMETERS || definitions.size > MAX_DEFINITIONS || nonzero.size > MAX_NONZERO)
            limit("contract_size")
        if (parameters.isEmpty() || parameters.distinct().size != parameters.size) unsupported("parameters")
        (parameters + definitions.keys).forEach {
            if (it.length > MAX_SOURCE_LENGTH) limit("identifier_length")
            if (!it.matches(Regex("[A-Za-z][A-Za-z0-9_]*"))) unsupported("identifier")
        }
        if (definitions.keys.any { it in parameters }) unsupported("definition_collision")
        val context = Context(parameters, definitions)
        val budget = Budget()
        context.resolveDefinitions(budget)
        val hypotheses = nonzero.map {
            val value = context.parse(it, budget)
            val polynomial = value.asPolynomial(budget) ?: unsupported("nonzero_polynomial")
            if (polynomial.isZero()) unsupported("zero_hypothesis")
            polynomial
        }
        val target = context.parse(expected, budget)
        for (obligation in target.obligations) {
            if (!certified(obligation, hypotheses, budget)) unsupported("expected_domain")
        }
        return Contract(context, hypotheses, target)
    }

    class Contract internal constructor(
        private val context: Context,
        private val hypotheses: List<Polynomial>,
        private val expected: Function
    ) {
        fun compare(answer: String): Comparison {
            guardSource(answer)
            val budget = Budget()
            val actual = context.parse(answer, budget)
            if (actual.obligations.any { it.isZero() }) return Comparison.DOMAIN_MISMATCH
            if (actual.numerator.times(expected.denominator, budget) !=
                expected.numerator.times(actual.denominator, budget)) return Comparison.VALUE_MISMATCH
            val undecided = actual.obligations.filter { !certified(it, hypotheses, budget) }
            for (obligation in undecided) {
                // On an infinite field, a product of nonzero polynomials has a rational nonzero
                // point. Thus this is an algebraic existence proof, not a sampled counterexample.
                if (context.parameters.indices.any { index ->
                    obligation.atZero(index).isZero() && hypotheses.all { !it.atZero(index).isZero() }
                }) return Comparison.DOMAIN_MISMATCH
            }
            if (undecided.isNotEmpty()) unsupported("unproved_domain")
            return Comparison.CORRECT
        }

        /** Exact regression witnesses only; compare() never calls this method. Null means undefined. */
        fun evaluate(expression: String, assignments: Map<String, String>): ExactRational? {
            guardSource(expression)
            assignments.values.forEach(::guardSource)
            if (assignments.keys != context.parameters.toSet()) unsupported("assignments")
            val budget = Budget()
            val constants = Context(emptyList(), emptyMap())
            val values = context.parameters.map { parameter ->
                val value = constants.parse(assignments.getValue(parameter), budget)
                val polynomial = value.asPolynomial(budget) ?: unsupported("assignment_value")
                polynomial.constant() ?: unsupported("assignment_value")
            }
            if (hypotheses.any { it.evaluate(values, budget).numerator.signum() == 0 }) unsupported("outside_domain")
            val actual = context.parse(expression, budget)
            if (actual.obligations.any { it.evaluate(values, budget).numerator.signum() == 0 }) return null
            val denominator = actual.denominator.evaluate(values, budget)
            if (denominator.numerator.signum() == 0) return null
            return divide(actual.numerator.evaluate(values, budget), denominator)
        }
    }

    internal class Context(val parameters: List<String>, private val definitions: Map<String, String>) {
        private val resolved = mutableMapOf<String, Function>()
        private val visiting = mutableSetOf<String>()
        private val names = (parameters + definitions.keys).sortedByDescending { it.length }

        fun resolveDefinitions(budget: Budget) { definitions.keys.forEach { resolve(it, budget) } }
        fun parse(source: String, budget: Budget): Function = Parser(source, this, names, budget).parse()

        fun resolve(name: String, budget: Budget): Function {
            val parameter = parameters.indexOf(name)
            if (parameter >= 0) return Function(Polynomial.variable(parameters.size, parameter))
            resolved[name]?.let { return it }
            val source = definitions[name] ?: unsupported("unknown_name")
            if (!visiting.add(name)) unsupported("definition_cycle")
            if (visiting.size > MAX_DEPTH) limit("definition_depth")
            try {
                val value = budget.nested { parse(source, budget) }
                val polynomial = value.asPolynomial(budget) ?: unsupported("definition_polynomial")
                return Function(polynomial).also { resolved[name] = it }
            } finally { visiting.remove(name) }
        }
    }

    internal class Budget {
        private var operations = 0
        private var depth = 0
        fun spend(count: Int = 1) {
            if (count > MAX_OPERATIONS - operations) limit("operations")
            operations += count
        }
        fun <T> nested(action: () -> T): T {
            if (++depth > MAX_DEPTH) limit("depth")
            try { return action() } finally { depth-- }
        }
    }

    private fun certified(value: Polynomial, hypotheses: List<Polynomial>, budget: Budget): Boolean {
        if (value.isZero()) return false
        if (value.constant() != null) return true
        var remainder = value
        for (hypothesis in hypotheses) {
            if (hypothesis.constant() != null) continue
            while (true) {
                val quotient = remainder.exactDivide(hypothesis, budget) ?: break
                remainder = quotient
                if (remainder.constant() != null) return true
            }
        }
        // A divisor of a known nonzero polynomial is itself nonzero, including a factor of D^2.
        return hypotheses.any { it.exactDivide(remainder, budget) != null }
    }

    internal data class Monomial(val powers: List<Int>) : Comparable<Monomial> {
        override fun compareTo(other: Monomial): Int {
            for (i in powers.indices) {
                val comparison = powers[i].compareTo(other.powers[i])
                if (comparison != 0) return comparison
            }
            return 0
        }
        fun times(other: Monomial): Monomial {
            val result = powers.indices.map { powers[it] + other.powers[it] }
            if (result.sum() > MAX_TOTAL_DEGREE) limit("degree")
            return Monomial(result)
        }
        fun quotient(other: Monomial): Monomial? {
            val result = powers.indices.map { powers[it] - other.powers[it] }
            return if (result.any { it < 0 }) null else Monomial(result)
        }
    }

    internal data class Polynomial(val dimensions: Int, val terms: Map<Monomial, ExactRational>) {
        fun isZero(): Boolean = terms.isEmpty()
        fun constant(): ExactRational? = if (terms.isEmpty()) ZERO else
            terms.entries.singleOrNull()?.takeIf { it.key.powers.all { power -> power == 0 } }?.value

        fun plus(other: Polynomial, budget: Budget): Polynomial {
            val result = terms.toMutableMap()
            for ((monomial, coefficient) in other.terms) {
                budget.spend()
                val value = add(result[monomial] ?: ZERO, coefficient)
                if (value.numerator.signum() == 0) result.remove(monomial) else result[monomial] = value
                if (result.size > MAX_TERMS) limit("terms")
            }
            return Polynomial(dimensions, result)
        }
        fun negate(): Polynomial = Polynomial(dimensions, terms.mapValues { negate(it.value) })
        fun minus(other: Polynomial, budget: Budget): Polynomial = plus(other.negate(), budget)

        fun times(other: Polynomial, budget: Budget): Polynomial {
            val result = mutableMapOf<Monomial, ExactRational>()
            for ((left, leftCoefficient) in terms) for ((right, rightCoefficient) in other.terms) {
                budget.spend()
                val monomial = left.times(right)
                val coefficient = add(result[monomial] ?: ZERO, multiply(leftCoefficient, rightCoefficient))
                if (coefficient.numerator.signum() == 0) result.remove(monomial) else result[monomial] = coefficient
                if (result.size > MAX_TERMS) limit("terms")
            }
            return Polynomial(dimensions, result)
        }
        fun power(exponent: Int, budget: Budget): Polynomial {
            var result = constant(dimensions, ONE)
            repeat(exponent) { result = result.times(this, budget) }
            return result
        }

        /** Exact division in Q[parameters], using a fixed admissible lexicographic monomial order. */
        fun exactDivide(divisor: Polynomial, budget: Budget): Polynomial? {
            if (divisor.isZero()) return null
            var remainder = this
            var quotient = constant(dimensions, ZERO)
            val divisorLead = divisor.terms.maxBy { it.key }
            while (!remainder.isZero()) {
                budget.spend()
                val lead = remainder.terms.maxBy { it.key }
                val monomial = lead.key.quotient(divisorLead.key) ?: return null
                val term = Polynomial(dimensions, mapOf(monomial to divide(lead.value, divisorLead.value)))
                quotient = quotient.plus(term, budget)
                remainder = remainder.minus(term.times(divisor, budget), budget)
            }
            return quotient
        }

        fun atZero(index: Int): Polynomial = Polynomial(dimensions, terms.filterKeys { it.powers[index] == 0 })
        fun evaluate(values: List<ExactRational>, budget: Budget): ExactRational {
            var result = ZERO
            for ((monomial, coefficient) in terms) {
                var term = coefficient
                for (i in values.indices) repeat(monomial.powers[i]) {
                    budget.spend()
                    term = multiply(term, values[i])
                }
                result = add(result, term)
            }
            return result
        }
        companion object {
            fun constant(dimensions: Int, value: ExactRational): Polynomial = Polynomial(dimensions,
                if (value.numerator.signum() == 0) emptyMap() else mapOf(Monomial(List(dimensions) { 0 }) to value))
            fun variable(dimensions: Int, index: Int): Polynomial = Polynomial(dimensions,
                mapOf(Monomial(List(dimensions) { if (it == index) 1 else 0 }) to ONE))
        }
    }

    internal data class Function(
        val numerator: Polynomial,
        val denominator: Polynomial = Polynomial.constant(numerator.dimensions, ONE),
        val obligations: List<Polynomial> = emptyList()
    ) {
        private fun obligations(other: Function): List<Polynomial> = (obligations + other.obligations).also {
            if (it.size > MAX_OBLIGATIONS) limit("obligations")
        }
        fun plus(other: Function, budget: Budget): Function = Function(
            numerator.times(other.denominator, budget).plus(other.numerator.times(denominator, budget), budget),
            denominator.times(other.denominator, budget), obligations(other))
        fun negate(): Function = copy(numerator = numerator.negate())
        fun times(other: Function, budget: Budget): Function = Function(numerator.times(other.numerator, budget),
            denominator.times(other.denominator, budget), obligations(other))
        fun dividedBy(other: Function, budget: Budget): Function {
            val checks = obligations(other) + other.numerator
            if (checks.size > MAX_OBLIGATIONS) limit("obligations")
            return Function(numerator.times(other.denominator, budget), denominator.times(other.numerator, budget), checks)
        }
        fun power(exponent: Int, budget: Budget): Function = Function(numerator.power(exponent, budget),
            denominator.power(exponent, budget), obligations)
        fun asPolynomial(budget: Budget): Polynomial? {
            if (obligations.any { it.constant() == null || it.constant()!!.numerator.signum() == 0 }) return null
            val divisor = denominator.constant() ?: return null
            if (divisor.numerator.signum() == 0) return null
            return numerator.times(Polynomial.constant(numerator.dimensions, divide(ONE, divisor)), budget)
        }
    }

    private class Parser(source: String, private val context: Context, private val names: List<String>, private val budget: Budget) {
        private val text: String
        private var index = 0
        init {
            guardSource(source)
            text = source.replace('−', '-').replace('×', '*').replace('·', '*').replace('÷', '/')
                .replace("²", "^2").replace("³", "^3")
            guardSource(text)
        }
        fun parse(): Function {
            if (text.isBlank()) unsupported("empty_expression")
            val result = sum()
            whitespace()
            if (index != text.length) unsupported("syntax")
            return result
        }
        private fun sum(): Function {
            var value = product()
            while (true) {
                value = when {
                    consume('+') -> value.plus(product(), budget)
                    consume('-') -> value.plus(product().negate(), budget)
                    else -> return value
                }
            }
        }
        private fun product(): Function {
            var value = unary()
            while (true) {
                value = when {
                    consume('*') -> value.times(unary(), budget)
                    consume('/') -> value.dividedBy(unary(), budget)
                    beginsAtom() -> value.times(unary(), budget)
                    else -> return value
                }
            }
        }
        private fun unary(): Function = nested {
            when {
                consume('+') -> unary()
                consume('-') -> unary().negate()
                else -> power()
            }
        }
        private fun power(): Function {
            val value = atom()
            if (!consume('^')) return value
            val grouped = consume('(')
            whitespace()
            val start = index
            while (text.getOrNull(index) in '0'..'9') index++
            val digits = text.substring(start, index)
            if (digits.isEmpty() || digits.length > 1 || digits[0] !in '0'..('0' + MAX_POWER)) unsupported("power")
            if (grouped && !consume(')')) unsupported("power")
            if (peek() == '^') unsupported("power")
            return value.power(digits[0] - '0', budget)
        }
        private fun atom(): Function {
            if (consume('(')) {
                val result = nested { sum() }
                if (!consume(')')) unsupported("group")
                return result
            }
            whitespace()
            val character = text.getOrNull(index) ?: unsupported("incomplete_expression")
            if (character in '0'..'9' || character == '.') return number()
            val name = names.firstOrNull { text.startsWith(it, index) } ?: unsupported("unknown_name")
            index += name.length
            budget.spend()
            return context.resolve(name, budget)
        }
        private fun number(): Function {
            val start = index
            var count = 0
            while (text.getOrNull(index) in '0'..'9') { index++; count++ }
            var fraction = 0
            if (text.getOrNull(index) == '.') {
                index++
                while (text.getOrNull(index) in '0'..'9') { index++; count++; fraction++ }
            }
            if (count == 0) unsupported("number")
            if (count > MAX_LITERAL_DIGITS) limit("literal_digits")
            val digits = text.substring(start, index).replace(".", "")
            val following = peek()
            if (following == '.' || following in '0'..'9') unsupported("number_separator")
            val value = rational(BigInteger(digits), BigInteger.TEN.pow(fraction))
            return Function(Polynomial.constant(context.parameters.size, value))
        }
        private fun beginsAtom(): Boolean {
            val next = peek() ?: return false
            return next == '(' || next == '.' || next in '0'..'9' || next in 'A'..'Z' || next in 'a'..'z'
        }
        private fun peek(): Char? { whitespace(); return text.getOrNull(index) }
        private fun consume(character: Char): Boolean {
            whitespace()
            if (text.getOrNull(index) != character) return false
            index++
            return true
        }
        private fun whitespace() { while (text.getOrNull(index)?.isWhitespace() == true) index++ }
        private fun <T> nested(action: () -> T): T = budget.nested(action)
    }

    private val ZERO = ExactRational(BigInteger.ZERO, BigInteger.ONE)
    private val ONE = ExactRational(BigInteger.ONE, BigInteger.ONE)
    private fun rational(numerator: BigInteger, denominator: BigInteger): ExactRational {
        if (denominator.signum() == 0) unsupported("zero_constant_divisor")
        if (numerator.signum() == 0) return ZERO
        val gcd = numerator.gcd(denominator)
        val sign = BigInteger.valueOf(denominator.signum().toLong())
        val n = numerator.divide(gcd).multiply(sign)
        val d = denominator.divide(gcd).multiply(sign)
        if (n.abs().bitLength() > MAX_COEFFICIENT_BITS || d.bitLength() > MAX_COEFFICIENT_BITS) limit("coefficient_bits")
        return ExactRational(n, d)
    }
    private fun add(a: ExactRational, b: ExactRational): ExactRational = rational(
        a.numerator * b.denominator + b.numerator * a.denominator, a.denominator * b.denominator)
    private fun negate(a: ExactRational): ExactRational = ExactRational(a.numerator.negate(), a.denominator)
    private fun multiply(a: ExactRational, b: ExactRational): ExactRational = rational(a.numerator * b.numerator, a.denominator * b.denominator)
    private fun divide(a: ExactRational, b: ExactRational): ExactRational = rational(a.numerator * b.denominator, a.denominator * b.numerator)
    private fun guardSource(source: String) { if (source.length > MAX_SOURCE_LENGTH) limit("source_length") }
    private fun unsupported(reason: String): Nothing = throw UnsupportedException(reason)
    private fun limit(reason: String): Nothing = throw ResourceLimitException(reason)
}
