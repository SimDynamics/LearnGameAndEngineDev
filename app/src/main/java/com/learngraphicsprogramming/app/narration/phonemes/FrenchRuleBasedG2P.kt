package com.learngraphicsprogramming.app.narration.phonemes

import java.text.Normalizer
import java.util.Locale

/**
 * French-only orthographic fallback for words absent from the installed French lexicon.
 *
 * Adapted from DotNetG2P.French's GraphemeToPhonemeRules, FrenchOrthography and
 * NasalVowelizer (Copyright 2026 ayutaz, Apache-2.0), revision
 * b5f886f4566afdb6a1503eadbd5a45154881a83c. See french_rule_g2p_notice.txt.
 * Modified for Kotlin/Kokoro: no syllabifier, lexical replacements or language detection;
 * corrected closed-syllable e, final -ail/-eil, inflections and hiatus handling.
 *
 * The existing sentence frontend owns elision, liaison, stress and IPA adaptation. This
 * class returns unstressed citation IPA only. Orthography cannot resolve every proper
 * name or homograph; verified dictionary pronunciations always take precedence.
 */
object FrenchRuleBasedG2P : FrenchWordG2P {
    private data class Phone(var value: String, val canGlide: Boolean = true)

    override fun phonemizeWord(word: String): String? {
        if (word.length !in 1..256 || word.any { !it.isLetter() && !isCombiningMark(it) }) return null
        val normalized = frenchSpelling(word) ?: return null
        irregular[normalized]?.let { return it }
        // Plural -s/-x are silent. Retain the spelling otherwise: it determines the
        // pronunciation of the preceding consonant and the existing frontend's liaison.
        val lower = if (normalized.length > 2 && normalized.last() in "sx") normalized.dropLast(1) else normalized
        irregular[lower]?.let { return it }
        val phones = mutableListOf<Phone>()
        var index = 0
        fun emit(value: String, consumed: Int = 1, canGlide: Boolean = true) {
            if (value.isNotEmpty()) phones += Phone(value, canGlide)
            index += consumed
        }
        while (index < lower.length) {
            val i = index
            val remaining = lower.substring(i)
            val c = lower[i]
            val next = lower.getOrNull(i + 1)
            val after = lower.getOrNull(i + 2)
            // Longest written units, then terminal inflections, then contextual letters.
            when {
                remaining.startsWith("eaux") -> emit("o", 4)
                remaining.startsWith("eau") -> emit("o", 3)
                remaining in setOf("aient", "ais", "ait") -> emit("ɛ", remaining.length)
                remaining in setOf("ée", "ées", "és", "er", "ez") -> emit("e", remaining.length)
                remaining in setOf("et", "ets") -> emit("ɛ", remaining.length)
                remaining == "ment" -> emit("mɑ̃", 4)
                // Adjectival -jacent is not the silent third-person verb inflection.
                remaining == "ent" && lower.endsWith("jacent") -> emit("ɑ̃", 3)
                remaining == "ent" && i > 0 -> emit("", 3)
                remaining == "tion" -> emit(if (lower.endsWith("stion")) "tjɔ̃" else "sjɔ̃", 4)
                remaining == "ssion" -> emit("sjɔ̃", 5)
                remaining == "sion" -> emit("zjɔ̃", 4)
                remaining in setOf("tiel", "tielle") -> emit("sjɛl", remaining.length)
                remaining in setOf("iel", "ielle") -> emit("jɛl", remaining.length)
                remaining in setOf("ouil", "ouille") -> emit("uj", remaining.length)
                remaining in setOf("ueil", "ueille", "euil", "euille") -> emit("œj", remaining.length)
                remaining in setOf("ail", "aille") -> emit("aj", remaining.length)
                remaining in setOf("eil", "eille") -> emit("ɛj", remaining.length)
                remaining == "ille" -> emit("ij", 4)
                // French hexa- keeps /ɛɡza/, including before a following consonant.
                i == 0 && remaining.startsWith("hexa") -> emit("ɛɡza", 4)
                remaining.startsWith("sch") -> emit("ʃ", 3)
                remaining.startsWith("ch") -> emit("ʃ", 2)
                remaining.startsWith("sh") -> emit("ʃ", 2)
                remaining.startsWith("ph") -> emit("f", 2)
                remaining.startsWith("th") -> emit("t", 2)
                remaining.startsWith("gn") -> emit("ɲ", 2)
                remaining == "guë" -> emit("ɡy", 3)
                remaining.startsWith("qua") && (lower.startsWith("équat") || lower.startsWith("aquat") || lower.startsWith("quadr")) -> emit("kw", 2)
                remaining.startsWith("qu") -> emit("k", 2)
                remaining.startsWith("gu") && after != null && after in frontVowels && after !in "ïë" -> emit("ɡ", 2)
                remaining.startsWith("ge") && after != null && after in "aouàâôùû" -> emit("ʒ", 2)
                remaining.startsWith("sc") && after != null && after in frontVowels -> emit("s", 2)
                remaining.startsWith("ill") && (i == 0 || lower[i - 1] in vowels) -> emit("j", 3)
                remaining.startsWith("oi") -> emit("wa", 2)
                remaining.startsWith("oy") && after != null && after in vowels -> emit("waj", 2)
                remaining.startsWith("ay") && after != null && after in vowels -> emit("ɛj", 2)
                remaining.startsWith("ou") -> emit("u", 2)
                remaining.startsWith("au") -> emit("o", 2)
                (remaining.startsWith("ain") || remaining.startsWith("aim") ||
                    remaining.startsWith("ein") || remaining.startsWith("eim")) && nasalContext(lower, i + 2) -> emit("ɛ̃", 3)
                remaining.startsWith("ien") && nasalContext(lower, i + 2) -> emit(if (remaining.startsWith("ience")) "jɑ̃" else "jɛ̃", 3)
                remaining.startsWith("ai") || remaining.startsWith("ei") -> emit("ɛ", 2)
                remaining.startsWith("oeu") -> emit(if (closedSyllable(lower, i + 3)) "œ" else "ø", 3)
                remaining.startsWith("eu") || remaining.startsWith("œu") -> emit(if (closedSyllable(lower, i + 2)) "œ" else "ø", 2)
                c in nasalVowels && next != null && next in "nm" && nasalContext(lower, i + 1) -> emit(when (c) {
                    'o', 'ô' -> "ɔ̃"
                    'i', 'î', 'y' -> "ɛ̃"
                    'u', 'û' -> "œ̃"
                    else -> "ɑ̃"
                }, 2)
                c == 'e' -> when {
                    i == lower.lastIndex -> emit(if (phones.none { p -> p.value.any { it in ipaVowels } }) "ə" else "")
                    next in vowels -> emit("")
                    closedSyllable(lower, i + 1) -> emit("ɛ")
                    else -> emit("ə")
                }
                c in "aàâ" -> emit("a")
                c == 'é' -> emit("e")
                c in "èêë" -> emit("ɛ", canGlide = false)
                c in "iîï" -> emit("i", canGlide = c != 'ï')
                c == 'o' -> emit(if (closedSyllable(lower, i + 1)) "ɔ" else "o")
                c == 'ô' -> emit("o")
                c in "uùûü" -> emit("y", canGlide = c != 'ü')
                c == 'y' -> emit(if (next != null && next in vowels) "j" else "i")
                c == 'æ' -> emit("e")
                c == 'œ' -> emit("œ")
                c == 'ç' -> emit("s")
                c == 'ñ' -> emit("ɲ")
                c == 'c' -> emit(when {
                    next == 'c' && after != null && after in frontVowels -> "k"
                    next != null && next in frontVowels -> "s"
                    else -> "k"
                }, if (next == 'c' && (after == null || after !in frontVowels)) 2 else 1)
                c == 'g' -> emit(when {
                    next == null -> ""
                    next in frontVowels -> "ʒ"
                    else -> "ɡ"
                }, if (next == 'g') 2 else 1)
                c == 's' -> emit(when {
                    next == null -> ""
                    next == 's' -> "s"
                    i > 0 && lower[i - 1] in vowels && next in vowels -> "z"
                    else -> "s"
                }, if (next == 's') 2 else 1)
                c == 'x' -> {
                    if (i == 1 && lower[0] == 'e' && next != null && next in vowels) {
                        if (phones.lastOrNull()?.value == "ə") phones.last().value = "ɛ"
                        emit("ɡz")
                    } else emit(when {
                        next == null -> ""
                        next == 'c' && after != null && after in frontVowels -> "k"
                        else -> "ks"
                    })
                }
                c == 'h' -> emit("")
                c == 'j' -> emit("ʒ", if (next == c) 2 else 1)
                c == 'r' -> emit("ʁ", if (next == c) 2 else 1)
                c == 'w' -> emit("w", if (next == c) 2 else 1)
                c == 'z' -> emit(if (next == null) "" else "z", if (next == c) 2 else 1)
                c == 't' || c == 'd' || c == 'p' -> {
                    val clusterFinalT = c == 't' && i == lower.lastIndex && i > 0 && lower[i - 1] in "cp"
                    val assibilatedT = c == 't' && next == 'i' && after != null && after in "ao" &&
                        (i == 0 || lower[i - 1] !in "sx")
                    emit(when {
                        i == lower.lastIndex && !clusterFinalT -> ""
                        assibilatedT -> "s"
                        else -> c.toString()
                    }, if (next == c) 2 else 1)
                }
                c in "bfklmnv" -> emit(c.toString(), if (next == c) 2 else 1)
                c == 'q' -> emit("k")
                else -> return null
            }
        }
        // Preserve diaeresis hiatus. Do not turn final u+silent e into a glide.
        for (i in 0 until phones.lastIndex) {
            val current = phones[i]
            val next = phones[i + 1]
            if (current.canGlide && next.value.firstOrNull() in ipaVowels) current.value = when (current.value) {
                "i" -> "j"
                "y" -> "ɥ"
                "u" -> "w"
                else -> current.value
            }
        }
        return phones.joinToString("") { it.value }.takeIf { ipa -> ipa.any { it in ipaVowels } }
            // A consonant-only identifier must remain audible, including all-silent h.
            ?: lower.mapNotNull(letterNames::get).joinToString(" ").takeIf { it.isNotBlank() }
    }

    private fun nasalContext(word: String, nasalIndex: Int): Boolean {
        val next = word.getOrNull(nasalIndex + 1) ?: return true
        if (next in vowels || next == word[nasalIndex]) return false
        if (next == 'h' && word.getOrNull(nasalIndex + 2) in vowels) return false
        return true
    }

    /**
     * Preserve French accents, and approximate unfamiliar Latin name spellings in French.
     * This is a fixed orthographic map, never a language detector or a claim to reproduce
     * the name's native accent. In particular Köppe remains audible with French /œ/.
     * NFD is deliberately used instead of NFKD: math superscripts are not ordinary words.
     */
    private fun frenchSpelling(word: String): String? {
        val nfc = Normalizer.normalize(word, Normalizer.Form.NFC).lowercase(Locale.ROOT)
        return buildString {
            for (letter in nfc) {
                if (isCombiningMark(letter)) {
                    if (isEmpty()) return null
                    continue
                }
                if (letter in letters) {
                    append(letter)
                    continue
                }
                val replacement = latinNameLetters[letter]
                if (replacement != null) {
                    append(replacement)
                    continue
                }
                val decomposed = Normalizer.normalize(letter.toString(), Normalizer.Form.NFD)
                val base = decomposed.firstOrNull() ?: return null
                if (base !in 'a'..'z' || decomposed.drop(1).any { !isCombiningMark(it) }) return null
                append(base)
            }
        }
    }

    private fun isCombiningMark(c: Char): Boolean = Character.getType(c) in setOf(
        Character.NON_SPACING_MARK.toInt(), Character.COMBINING_SPACING_MARK.toInt(), Character.ENCLOSING_MARK.toInt())

    private fun closedSyllable(word: String, afterVowel: Int): Boolean {
        val remaining = word.substring(afterVowel)
        if (remaining.isEmpty()) return false
        // Terminal mute e does not make the preceding syllable open: merci, correcte,
        // terminateur and supérieure differ from e.g. petit or heureux.
        if (remaining == "e") return false
        val audible = remaining.removeSuffix("e")
        if (audible.length == 1) return audible[0] in "crfl"
        if (audible.length >= 2 && audible[0] !in vowels && audible[1] !in vowels) {
            if (audible.take(2) in onsetClusters) return false
            return true
        }
        return false
    }

    private val vowels = "aeiouyàâéèêëîïôùûüæœ".toSet()
    private val nasalVowels = "aeiouyàâéèêîôû".toSet()
    private val frontVowels = "eiyéèêëîï".toSet()
    private val letters = ("abcdefghijklmnopqrstuvwxyzàâéèêëîïôùûüæœçñ").toSet()
    private val latinNameLetters = mapOf(
        'ö' to "œ", 'ø' to "œ", 'ä' to "è", 'ß' to "ss", 'ł' to "l", 'đ' to "d", 'ð' to "d",
        'þ' to "t", 'ħ' to "h", 'ı' to "i", 'ŋ' to "ng", 'š' to "ch", 'ž' to "j", 'č' to "tch"
    )
    private val ipaVowels = "aeiouyøœɛɔɑə".toSet()
    private val onsetClusters = setOf("bl", "br", "cl", "cr", "dr", "fl", "fr", "gl", "gr", "pl", "pr", "tr", "vr")

    // Small, explicit irregular inflection families. These are pronunciation rules for
    // French, never English term selection and never replacements for curriculum text.
    private val irregular = mapOf(
        "est" to "ɛ", "et" to "e", "es" to "ɛ", "sont" to "sɔ̃", "ont" to "ɔ̃", "font" to "fɔ̃",
        "fait" to "fɛ", "fais" to "fɛ", "femme" to "fam", "femmes" to "fam", "monsieur" to "məsjø",
        "œil" to "œj", "yeux" to "jø", "fils" to "fis", "six" to "sis", "dix" to "dis",
        "sept" to "sɛt", "huit" to "ɥit", "neuf" to "nœf", "neufs" to "nœf",
        "suffit" to "syfi", "suffisent" to "syfiz", "reçoit" to "ʁəswa", "reçois" to "ʁəswa",
        "reçoivent" to "ʁəswav", "reçu" to "ʁəsy", "reçue" to "ʁəsy", "reçus" to "ʁəsy", "reçues" to "ʁəsy",
        "vaut" to "vo", "vaux" to "vo", "valent" to "val", "vœu" to "vø", "vœux" to "vø",
        "fille" to "fij", "mille" to "mil", "ville" to "vil", "tranquille" to "tʁɑ̃kil",
        "hier" to "jɛʁ", "hiver" to "ivɛʁ", "cher" to "ʃɛʁ", "mer" to "mɛʁ", "fer" to "fɛʁ",
        "fier" to "fjɛʁ", "tiers" to "tjɛʁ", "papier" to "papje", "premier" to "pʁəmje",
        "présent" to "pʁezɑ̃", "absent" to "apsɑ̃", "parent" to "paʁɑ̃", "différent" to "difeʁɑ̃",
        "excellent" to "ɛksɛlɑ̃", "équivalent" to "ekivalɑ̃", "lent" to "lɑ̃", "vent" to "vɑ̃",
        "cent" to "sɑ̃", "dent" to "dɑ̃", "argent" to "aʁʒɑ̃", "récent" to "ʁesɑ̃",
        "scientifique" to "sjɑ̃tifik", "science" to "sjɑ̃s"
    )
    private val letterNames = mapOf(
        'a' to "a", 'b' to "be", 'c' to "se", 'd' to "de", 'e' to "ə", 'f' to "ɛf", 'g' to "ʒe",
        'h' to "aʃ", 'i' to "i", 'j' to "ʒi", 'k' to "ka", 'l' to "ɛl", 'm' to "ɛm", 'n' to "ɛn",
        'o' to "o", 'p' to "pe", 'q' to "ky", 'r' to "ɛʁ", 's' to "ɛs", 't' to "te", 'u' to "y",
        'v' to "ve", 'w' to "dubləve", 'x' to "iks", 'y' to "iɡʁɛk", 'z' to "zɛd"
    )
}
