package com.learngraphicsprogramming.app.narration.phonemes

import org.json.JSONObject
import java.util.Locale

/** Adapted from hexgrad/misaki en.py at EnglishMisakiResources.revision (Apache-2.0). */
internal class MisakiLexicon(
    private val gold: JSONObject,
    private val silver: JSONObject,
    private val british: Boolean
) {
    fun pronounce(token: MisakiWord, context: PhoneticContext): String {
        return tryPronounce(token, context) ?: compound(token, context) ?: throw EnglishMisakiException(
            "Misaki has no verified pronunciation for '${token.text}'; the optional BART fallback is not installed"
        )
    }

    /** Returns null only for missing pronunciations; unresolved grammatical context is an error. */
    fun tryPronounce(token: MisakiWord, context: PhoneticContext): String? {
        if (token.tag == null && token.text in setOf("US", "AM", "AN", "IN", "TO", "OR", "IT", "ON")) {
            throw EnglishMisakiException("Misaki needs spaCy part-of-speech context for '${token.text}'")
        }
        val value = token.alias ?: token.text
        val stress = capitalizationStress(value)
        val result = word(value, token.tag, stress, context)
            ?: if (EnglishMisakiG2P.NUMBER.matches(value) && (token.isHead || !value.startsWith('-'))) number(token.copy(text = value), stress) else null
        return MisakiStress.apply(result, token.stress)
    }

    private fun word(raw: String, tag: String?, stress: Double?, context: PhoneticContext?): String? {
        val special = special(raw, tag, stress, context)
        if (special != null) return special
        var word = raw
        val lower = word.lowercase(Locale.US)
        if (word.length > 1 && word.replace("'", "").all { it.isLetter() } && word != lower &&
            (tag != "NNP" || word.length > 7) && entry(gold, word) == null && entry(silver, word) == null &&
            (word == word.uppercase(Locale.US) || word.drop(1) == word.drop(1).lowercase(Locale.US)) &&
            (known(lower) || hasInflectionStem(lower))
        ) word = lower
        if (known(word)) return lookup(word, tag, stress, context)
        if (word.endsWith("s'") && known(word.dropLast(2) + "'s")) return lookup(word.dropLast(2) + "'s", tag, stress, context)
        if (word.endsWith("'") && known(word.dropLast(1))) return lookup(word.dropLast(1), tag, stress, context)
        stemS(word)?.let { return plural(lookup(it, tag, stress, context)) }
        stemEd(word)?.let { return past(lookup(it, tag, stress, context)) }
        stemIng(word)?.let { return continuous(lookup(it, tag, stress ?: 0.5, context)) }
        return null
    }

    private fun special(word: String, tag: String?, stress: Double?, context: PhoneticContext?): String? {
        if (tag == "ADD" && word in listOf(".", "/")) return lookup(if (word == ".") "dot" else "slash", null, -0.5, context)
        SYMBOLS[word]?.let { return lookup(it, null, null, context) }
        if (word.trim('.').contains('.') && word.replace(".", "").all { it.isLetter() } &&
            word.split('.').maxOf { it.length } < 3
        ) return spellInitialism(word)
        return when {
            word == "a" || word == "A" -> if (tag == "DT") "ɐ" else "ˈA"
            word in listOf("am", "Am", "AM") -> when {
                tag?.startsWith("NN") == true -> spellInitialism(word)
                context?.futureVowel == null || word != "am" || (stress != null && stress > 0) ->
                    lookup("am", null, null, null)
                else -> "ɐm"
            }
            word in listOf("an", "An", "AN") ->
                if (word == "AN" && tag?.startsWith("NN") == true) spellInitialism(word) else "ɐn"
            word == "I" && tag == "PRP" -> "ˌI"
            word in listOf("by", "By", "BY") && parentTag(tag) == "ADV" -> "bˈI"
            word in listOf("to", "To") || (word == "TO" && tag in listOf("TO", "IN")) ->
                when (context?.futureVowel) { null -> lookup("to", null, null, null); false -> "tə"; true -> "tʊ" }
            word in listOf("in", "In") || (word == "IN" && tag != "NNP") ->
                (if (context?.futureVowel == null || tag != "IN") "ˈ" else "") + "ɪn"
            word in listOf("the", "The") || (word == "THE" && tag == "DT") ->
                if (context?.futureVowel == true) "ði" else "ðə"
            tag == "IN" && word.lowercase(Locale.US) in listOf("vs", "vs.") -> lookup("versus", null, null, context)
            word in listOf("used", "Used", "USED") && tag in listOf("VBD", "JJ") && context?.futureTo == true ->
                (entry(gold, "used") as? JSONObject)?.optString("VBD")
            else -> null
        }
    }

    private fun lookup(raw: String, tag: String?, stress: Double?, context: PhoneticContext?): String? {
        var word = raw
        var initialism = false
        if (word == word.uppercase(Locale.US) && entry(gold, word) == null) {
            word = word.lowercase(Locale.US)
            initialism = tag == "NNP"
        }
        var value = entry(gold, word)
        if (value == null && !initialism) value = entry(silver, word)
        val phonemes = when (value) {
            is String -> value
            is JSONObject -> {
                val key = when {
                    context != null && context.futureVowel == null && value.has("None") -> "None"
                    tag != null && value.has(tag) -> tag
                    tag != null && parentTag(tag)?.let { value.has(it) } == true -> parentTag(tag)!!
                    else -> "DEFAULT"
                }
                if (tag == null && key == "DEFAULT") {
                    val variants = value.keys().asSequence().filter { it !in listOf("None", "DEFAULT") }
                        .map { value.opt(it) }.filter { it is String }.toSet()
                    val default = value.opt("DEFAULT")
                    if (variants.any { it != default }) throw EnglishMisakiException(
                        "Misaki needs spaCy part-of-speech context for '$raw'; no inferred pronunciation was substituted"
                    )
                }
                value.opt(key) as? String
            }
            else -> null
        }
        if (phonemes == null || (initialism && 'ˈ' !in phonemes)) {
            return spellInitialism(word)
        }
        return MisakiStress.apply(phonemes, stress)
    }

    private fun spellInitialism(word: String): String? {
        val letters = word.filter { it.isLetter() }
        if (letters.isEmpty()) return null
        val pieces = letters.map { entry(gold, it.uppercaseChar().toString()) as? String ?: return null }
        val unstressed = MisakiStress.apply(pieces.joinToString(""), 0.0) ?: return null
        val lastStress = unstressed.lastIndexOf('ˌ')
        return if (lastStress < 0) unstressed else unstressed.substring(0, lastStress) + 'ˈ' + unstressed.substring(lastStress + 1)
    }

    private fun number(token: MisakiWord, stress: Double?): String? {
        val expanded = MisakiEnglishNumbers.expand(token.text, token.currency, token.isHead, token.numFlags)
        val pieces = expanded.words.map { numericWord ->
            if (numericWord == "@schwa") "ə"
            else if (numericWord in listOf("dollars", "pounds", "euros", "cents")) plural(lookup(numericWord.dropLast(1), null, null, null))
            else lookup(numericWord.removeSuffix("~ən"), null, if (numericWord == "point" || numericWord == "O") -2.0 else null, null)
                ?: throw EnglishMisakiException("Missing Misaki numeric vocabulary: $numericWord")
        }.mapIndexed { index, phones ->
            phones + if (expanded.words[index].endsWith("~ən")) "ən" else ""
        }
        val joined = pieces.joinToString(" ")
        val inflected = when (expanded.suffix) {
            "s", "'s" -> plural(joined)
            "ed", "'d" -> past(joined)
            "ing" -> continuous(joined)
            else -> joined
        }
        return MisakiStress.apply(inflected, stress)
    }

    private fun compound(token: MisakiWord, context: PhoneticContext): String? {
        val parts = token.text.replace(Regex("(?<=\\p{Ll})(?=\\p{Lu})|(?<=\\p{Lu})(?=\\p{Lu}\\p{Ll})"), "-")
            .split('-').filter { it.isNotEmpty() }
        if (parts.size < 2) return null
        val values = arrayOfNulls<String>(parts.size)
        var next = context
        for (index in parts.indices.reversed()) {
            val part = word(parts[index], token.tag, capitalizationStress(parts[index]), next) ?: return null
            values[index] = part
            val first = part.firstOrNull { it in MisakiStress.VOWELS || it in CONSONANTS }
            if (first != null) next = PhoneticContext(first in MisakiStress.VOWELS, parts[index] in listOf("to", "To"))
        }
        val stressed = values.indices.filter { values[it]?.contains('ˈ') == true }
        if (stressed.size > (parts.size + 1) / 2) {
            stressed.sortedBy { index -> values[index].orEmpty().sumOf { if (it in "AIOQWYʤʧ") 2 else 1 } }
                .take(parts.size / 2).forEach { values[it] = MisakiStress.apply(values[it], -0.5) }
        }
        return values.joinToString("") { it.orEmpty() }
    }

    private fun known(word: String): Boolean {
        if (entry(gold, word) != null || entry(silver, word) != null || word in SYMBOLS) return true
        if (word.isEmpty() || !word.all { it in 'A'..'Z' || it in 'a'..'z' }) return false
        if (word.length == 1) return true
        if (word == word.uppercase(Locale.US) && entry(gold, word.lowercase(Locale.US)) != null) return true
        return word.drop(1) == word.drop(1).uppercase(Locale.US)
    }

    private fun hasInflectionStem(word: String) = stemS(word) != null || stemEd(word) != null || stemIng(word) != null

    private fun stemS(word: String): String? = when {
        word.length < 3 || !word.endsWith('s') -> null
        !word.endsWith("ss") && known(word.dropLast(1)) -> word.dropLast(1)
        (word.endsWith("'s") || (word.length > 4 && word.endsWith("es") && !word.endsWith("ies"))) && known(word.dropLast(2)) -> word.dropLast(2)
        word.length > 4 && word.endsWith("ies") && known(word.dropLast(3) + "y") -> word.dropLast(3) + "y"
        else -> null
    }

    private fun stemEd(word: String): String? = when {
        word.length < 4 || !word.endsWith('d') -> null
        !word.endsWith("dd") && known(word.dropLast(1)) -> word.dropLast(1)
        word.length > 4 && word.endsWith("ed") && !word.endsWith("eed") && known(word.dropLast(2)) -> word.dropLast(2)
        else -> null
    }

    private fun stemIng(word: String): String? = when {
        word.length < 5 || !word.endsWith("ing") -> null
        word.length > 5 && known(word.dropLast(3)) -> word.dropLast(3)
        known(word.dropLast(3) + "e") -> word.dropLast(3) + "e"
        word.length > 5 && Regex("([bcdgklmnprstvxz])\\1ing$|cking$").containsMatchIn(word) && known(word.dropLast(4)) -> word.dropLast(4)
        else -> null
    }

    private fun plural(stem: String?): String? = when {
        stem.isNullOrEmpty() -> null
        stem.last() in "ptkfθ" -> stem + 's'
        stem.last() in "szʃʒʧʤ" -> stem + (if (british) "ɪ" else "ᵻ") + 'z'
        else -> stem + 'z'
    }

    private fun past(stem: String?): String? = when {
        stem.isNullOrEmpty() -> null
        stem.last() in "pkfθʃsʧ" -> stem + 't'
        stem.last() == 'd' -> stem + (if (british) "ɪ" else "ᵻ") + 'd'
        stem.last() != 't' -> stem + 'd'
        british || stem.length < 2 -> stem + "ɪd"
        stem[stem.lastIndex - 1] in US_TAUS -> stem.dropLast(1) + "ɾᵻd"
        else -> stem + "ᵻd"
    }

    private fun continuous(stem: String?): String? = when {
        stem.isNullOrEmpty() -> null
        british && stem.last() in "əː" -> null
        !british && stem.length > 1 && stem.last() == 't' && stem[stem.lastIndex - 1] in US_TAUS -> stem.dropLast(1) + "ɾɪŋ"
        else -> stem + "ɪŋ"
    }

    /** Mirrors grow_dictionary without keeping a second copy of 180,000 dictionary keys. */
    private fun entry(dictionary: JSONObject, word: String): Any? {
        if (dictionary.has(word)) return dictionary.opt(word).takeUnless { it == JSONObject.NULL }
        if (word.length < 2) return null
        val lower = word.lowercase(Locale.US)
        val capitalized = lower.replaceFirstChar { it.uppercaseChar() }
        val alias = when (word) { lower -> capitalized; capitalized -> lower; else -> return null }
        return dictionary.opt(alias).takeUnless { it == JSONObject.NULL }
    }

    private fun capitalizationStress(word: String): Double? = when {
        word == word.lowercase(Locale.US) -> null
        word == word.uppercase(Locale.US) -> 2.0
        else -> 0.5
    }

    private fun parentTag(tag: String?): String? = when {
        tag == null -> null
        tag.startsWith("VB") -> "VERB"
        tag.startsWith("NN") -> "NOUN"
        tag.startsWith("ADV") || tag.startsWith("RB") -> "ADV"
        tag.startsWith("ADJ") || tag.startsWith("JJ") -> "ADJ"
        else -> tag
    }

    companion object {
        private val SYMBOLS = mapOf("%" to "percent", "&" to "and", "+" to "plus", "@" to "at")
        private const val US_TAUS = "AIOWYiuæɑəɛɪɹʊʌ"
        private const val CONSONANTS = "bdfhjklmnpstvwzðŋɡɹɾʃʒʤʧθ"
    }
}
