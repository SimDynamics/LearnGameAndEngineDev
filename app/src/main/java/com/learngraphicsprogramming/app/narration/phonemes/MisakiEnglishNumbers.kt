package com.learngraphicsprogramming.app.narration.phonemes

import java.math.BigInteger

/** Independent English number expansion; no num2words LGPL implementation is bundled. */
internal object MisakiEnglishNumbers {
    data class Expansion(val words: List<String>, val suffix: String? = null)
    private val small = listOf("zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine",
        "ten", "eleven", "twelve", "thirteen", "fourteen", "fifteen", "sixteen", "seventeen", "eighteen", "nineteen")
    private val tens = listOf("", "", "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety")
    private val scales = listOf("", "thousand", "million", "billion", "trillion", "quadrillion", "quintillion", "sextillion")
    private val ordinalExceptions = mapOf("one" to "first", "two" to "second", "three" to "third", "five" to "fifth",
        "eight" to "eighth", "nine" to "ninth", "twelve" to "twelfth")
    private val units = mapOf("$" to ("dollar" to "cent"), "£" to ("pound" to "pence"), "€" to ("euro" to "cent"))

    fun expand(input: String, currency: String? = null, isHead: Boolean = true, numFlags: String = ""): Expansion {
        val suffix = Regex("[a-z']+$").find(input)?.value
        var digits = if (suffix == null) input else input.dropLast(suffix.length)
        val words = mutableListOf<String>()
        val negative = digits.startsWith('-')
        if (negative) {
            words += "minus"
            digits = digits.drop(1)
        }
        digits = digits.replace(",", "")
        if (digits.length > 24) throw EnglishMisakiException("Misaki number is too large to narrate safely")
        val ordinal = suffix in setOf("st", "nd", "rd", "th")
        fun extend(source: List<String>, first: Boolean = true) {
            source.forEachIndexed { index, word ->
                when {
                    word == "and" && '&' !in numFlags -> if ('n' in numFlags && words.isNotEmpty()) words[words.lastIndex] += "~ən"
                    first && index == 0 && source.size > 1 && word == "one" && 'a' in numFlags -> words += "@schwa"
                    else -> words += word
                }
            }
        }
        fun extendNumber(value: String, first: Boolean = true) = extend(cardinal(value.toBigInteger()), first)
        when {
            ordinal && digits.all { it.isDigit() } -> extend(ordinal(BigInteger(digits)))
            !negative && digits.length == 4 && currency == null && digits.all { it.isDigit() } ->
                extend(year(digits.toInt()))
            !isHead && '.' !in digits -> {
                when {
                    digits.startsWith('0') || digits.length > 3 -> digits.forEach { extendNumber(it.toString(), false) }
                    digits.length == 3 && !digits.endsWith("00") -> {
                        extendNumber(digits.take(1))
                        if (digits[1] == '0') { words += "O"; extendNumber(digits.takeLast(1), false) }
                        else extendNumber(digits.takeLast(2), false)
                    }
                    else -> extendNumber(digits)
                }
            }
            digits.count { it == '.' } > 1 || !isHead -> {
                var first = true
                digits.split('.').filter { it.isNotEmpty() }.forEach { part ->
                    if (part.startsWith('0') || (part.length != 2 && part.drop(1).any { it != '0' })) {
                        part.forEach { extendNumber(it.toString(), false) }
                    } else extendNumber(part, first)
                    first = false
                }
            }
            currency in units && digits.count { it == '.' } <= 1 &&
                (!digits.contains('.') || digits.substringAfter('.').length <= 2 || digits.substringAfter('.').all { it == '0' }) -> {
                val unit = units.getValue(currency!!)
                val major = digits.substringBefore('.').ifEmpty { "0" }.toBigInteger()
                val fraction = if (digits.contains('.')) digits.substringAfter('.') else ""
                // Misaki interprets the decimal digits as the minor-unit integer (1.5 -> five cents).
                val minor = fraction.ifEmpty { "0" }.toBigInteger()
                if (major != BigInteger.ZERO || minor == BigInteger.ZERO) {
                    extend(cardinal(major))
                    words += if (major == BigInteger.ONE) unit.first else unit.first + "s"
                }
                if (minor != BigInteger.ZERO) {
                    if (major != BigInteger.ZERO) words += "and"
                    extend(cardinal(minor), major == BigInteger.ZERO)
                    words += if (unit.second == "pence" || minor == BigInteger.ONE) unit.second else unit.second + "s"
                }
            }
            digits.contains('.') -> {
                val integer = digits.substringBefore('.')
                if (integer.isNotEmpty()) extend(cardinal(integer.toBigInteger()))
                words += "point"
                // num2words receives float for a headed decimal; its trailing zeros are dropped.
                val fraction = if (integer.isEmpty()) digits.substringAfter('.') else digits.substringAfter('.').trimEnd('0').ifEmpty { "0" }
                fraction.forEach { extendNumber(it.toString()) }
            }
            else -> extend(cardinal(digits.toBigInteger()))
        }
        return Expansion(words, suffix.takeUnless { ordinal })
    }

    private fun cardinal(value: BigInteger): List<String> {
        if (value == BigInteger.ZERO) return listOf("zero")
        require(value.signum() >= 0)
        val groups = mutableListOf<List<String>>()
        var remaining = value
        var scale = 0
        while (remaining > BigInteger.ZERO) {
            if (scale >= scales.size) throw EnglishMisakiException("Misaki number exceeds the supported scales")
            val parts = remaining.divideAndRemainder(BigInteger.valueOf(1_000))
            val chunk = parts[1].toInt()
            if (chunk != 0) groups += underThousand(chunk) + if (scale == 0) emptyList() else listOf(scales[scale])
            remaining = parts[0]
            scale++
        }
        val ordered = groups.asReversed()
        return buildList {
            ordered.forEachIndexed { index, group ->
                if (index > 0 && group.size == 1 && group.single() in small.drop(1)) add("and")
                else if (index > 0 && group.size <= 2 && group.none { it in scales.drop(1) || it == "hundred" }) add("and")
                addAll(group)
            }
        }
    }

    private fun underThousand(number: Int): List<String> = buildList {
        var remaining = number
        if (remaining >= 100) {
            add(small[remaining / 100])
            add("hundred")
            remaining %= 100
            if (remaining > 0) add("and")
        }
        if (remaining >= 20) {
            add(tens[remaining / 10])
            remaining %= 10
        }
        if (remaining > 0) add(small[remaining])
    }

    private fun ordinal(number: BigInteger): List<String> {
        val words = cardinal(number).toMutableList()
        val last = words.last()
        words[words.lastIndex] = ordinalExceptions[last] ?: if (last.endsWith('y')) last.dropLast(1) + "ieth" else last + "th"
        return words
    }

    private fun year(number: Int): List<String> {
        if (number < 100 || number % 1_000 == 0 || number in 2_000..2_009) return cardinal(BigInteger.valueOf(number.toLong()))
        val first = number / 100
        val last = number % 100
        return underThousand(first) + when {
            last == 0 -> listOf("hundred")
            last < 10 -> listOf("oh") + underThousand(last)
            else -> underThousand(last)
        }
    }
}
