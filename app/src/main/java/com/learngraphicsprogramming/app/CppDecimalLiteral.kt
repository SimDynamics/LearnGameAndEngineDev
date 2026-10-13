package com.learngraphicsprogramming.app

import java.math.BigInteger

/**
 * Parses one unsuffixed decimal int/double literal, retaining its mathematical decimal value
 * before C++ approximates a floating-point literal in binary. Signs are allowed only in exponents.
 * Decimal digits and their separators follow C++ grammar; other bases and integer types are outside
 * this curriculum profile. No floating conversion, rounding or expansion of powers of ten is used.
 */
object CppDecimalLiteral {
    /** Bounds parser memory/work by trimmed UTF-16 input length, not by exponent magnitude. */
    const val MAX_LENGTH = 16_384

    /** A technical limit must not be reported as an incorrect curricular answer. */
    class ResourceLimitException : IllegalStateException(
        "C++ decimal literal exceeds the parser limit of $MAX_LENGTH characters"
    )

    /** Canonical significand * 10^exponent; only zero has a zero significand. */
    data class Value internal constructor(val significand: String, val exponent: BigInteger)

    private val zero = Value("0", BigInteger.ZERO)
    private const val maximumInt = "2147483647" // Android's 32-bit C++ int profile.

    /** Returns null for invalid student syntax; exceeding MAX_LENGTH throws a technical error. */
    fun parse(answer: String): Value? {
        val text = answer.trim()
        if (text.length > MAX_LENGTH) throw ResourceLimitException()
        if (text.isEmpty()) return null

        val cursor = Cursor(text)
        val integral = cursor.digits()
        val hasPoint = cursor.consume('.')
        val fraction = if (hasPoint) cursor.digits() else ""
        if (integral.isEmpty() && fraction.isEmpty()) return null

        val hasExponent = cursor.consume('e') || cursor.consume('E')
        var exponent = BigInteger.ZERO
        if (hasExponent) {
            val negative = if (cursor.consume('-')) true else {
                cursor.consume('+')
                false
            }
            val exponentDigits = cursor.digits()
            if (exponentDigits.isEmpty()) return null
            exponent = BigInteger(exponentDigits)
            if (negative) exponent = exponent.negate()
        }
        // A complete single token is required: no suffix, comment, expression or internal space.
        if (!cursor.atEnd()) return null

        if (!hasPoint && !hasExponent) {
            // A leading zero starts octal integer syntax in C++; only the single token 0 is allowed.
            if (integral.startsWith('0') && integral.length != 1) return null
            // A larger unsuffixed decimal integer has type long/long long rather than int.
            if (integral.length > maximumInt.length ||
                (integral.length == maximumInt.length && integral > maximumInt)) return null
        }

        val digits = integral + fraction
        val first = digits.indexOfFirst { it != '0' }
        if (first == -1) return zero
        var last = digits.lastIndex
        while (digits[last] == '0') last--
        val trailingZeros = digits.lastIndex - last
        val decimalShift = trailingZeros.toLong() - fraction.length.toLong()
        return Value(digits.substring(first, last + 1), exponent + BigInteger.valueOf(decimalShift))
    }

    private class Cursor(private val text: String) {
        private var index = 0

        fun consume(character: Char): Boolean {
            if (text.getOrNull(index) != character) return false
            index++
            return true
        }

        fun atEnd(): Boolean = index == text.length

        /** Separators are permitted only between two ASCII decimal digits. */
        fun digits(): String {
            if (text.getOrNull(index)?.isDecimalDigit() != true) return ""
            val result = StringBuilder()
            while (index < text.length) {
                val character = text[index]
                if (character.isDecimalDigit()) {
                    result.append(character)
                    index++
                } else if (character == '\'' &&
                    text.getOrNull(index + 1)?.isDecimalDigit() == true) {
                    index++
                } else {
                    break
                }
            }
            return result.toString()
        }
    }

    private fun Char.isDecimalDigit(): Boolean = this in '0'..'9'
}
