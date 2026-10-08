package com.learngraphicsprogramming.app.narration.phonemes

/** French branch of FluidAudio RomanceNumberNormalizer (Apache-2.0), adapted to Kotlin. */
internal object FrenchNumberNormalizer {
    private val group = "[.\\u00A0\\u202F ]"
    private val number = Regex("\\d{1,3}(?:$group\\d{3})+(?:,\\d+)?(?:[\\u00A0\\u202F ]?%)?|\\d+(?:[,.]\\d+)?(?:[\\u00A0\\u202F ]?%)?")
    private val units = listOf("zéro", "un", "deux", "trois", "quatre", "cinq", "six", "sept", "huit", "neuf", "dix", "onze", "douze", "treize", "quatorze", "quinze", "seize", "dix-sept", "dix-huit", "dix-neuf")
    private val tens = listOf("", "", "vingt", "trente", "quarante", "cinquante", "soixante")

    fun normalize(text: String): String = number.replace(text) { read(it.value) }

    private fun read(token: String): String {
        val percentage = token.endsWith('%')
        val body = if (percentage) token.dropLast(1).trimEnd() else token
        var integer = body
        var fraction: String? = null
        if (',' in body) {
            integer = body.substringBefore(','); fraction = body.substringAfter(',')
        } else if (!Regex("^\\d{1,3}(?:$group\\d{3})+$").matches(body) && '.' in body) {
            integer = body.substringBefore('.'); fraction = body.substringAfter('.')
        }
        val digits = integer.filter { it.isDigit() }
        val value = digits.toLongOrNull()
        var words = if (value == null || value >= 1_000_000_000_000L) spellDigits(digits) else cardinal(value)
        if (!fraction.isNullOrEmpty()) {
            val zeros = fraction.takeWhile { it == '0' }.length
            val rest = fraction.drop(zeros)
            val decimal = MutableList(zeros) { "zéro" }
            if (rest.isNotEmpty()) {
                val numericRest = rest.toLongOrNull()
                decimal += if (numericRest == null || numericRest >= 1_000_000_000_000L) spellDigits(rest) else cardinal(numericRest)
            }
            words += " virgule " + decimal.joinToString(" ")
        }
        if (percentage) words += " pour cent"
        return words
    }

    private fun spellDigits(digits: String): String = digits.map { digit ->
        digit.digitToIntOrNull()?.let { units[it] } ?: throw FrenchG2PException("Chiffre français non pris en charge")
    }.joinToString(" ")

    internal fun cardinal(n: Long): String {
        require(n >= 0 && n < 1_000_000_000_000L)
        for ((factor, singular, plural) in listOf(Triple(1_000_000_000L, "un milliard", "milliards"), Triple(1_000_000L, "un million", "millions"))) {
            if (n >= factor) {
                val count = n / factor
                val rest = n % factor
                val head = if (count == 1L) singular else cardinal(count) + " " + plural
                return if (rest == 0L) head else head + " " + cardinal(rest)
            }
        }
        if (n >= 1000) {
            val thousands = n / 1000
            val rest = n % 1000
            val head = if (thousands == 1L) "mille" else belowThousand(thousands.toInt()) + " mille"
            return if (rest == 0L) head else head + if (thousands == 1L && rest == 1L) " et un" else " " + belowThousand(rest.toInt())
        }
        return belowThousand(n.toInt())
    }

    private fun belowThousand(n: Int): String {
        val hundreds = n / 100
        val rest = n % 100
        val parts = mutableListOf<String>()
        if (hundreds == 1) parts += "cent"
        else if (hundreds > 1) parts += units[hundreds] + if (rest == 0) " cents" else " cent"
        if (rest > 0 || n == 0) parts += belowHundred(rest)
        return parts.joinToString(" ")
    }
    private fun belowHundred(n: Int): String {
        if (n < 20) return units[n]
        val unit = n % 10
        return when (n / 10) {
            7 -> if (unit == 1) "soixante et onze" else "soixante-" + units[10 + unit]
            8 -> if (unit == 0) "quatre-vingts" else "quatre-vingt-" + units[unit]
            9 -> "quatre-vingt-" + units[10 + unit]
            else -> if (unit == 0) tens[n / 10] else if (unit == 1) tens[n / 10] + " et un" else tens[n / 10] + "-" + units[unit]
        }
    }
}
