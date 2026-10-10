package com.learngraphicsprogramming.app.narration.phonemes

import com.learngraphicsprogramming.app.narration.SpanishMathSpeech
import java.io.File
import java.text.Normalizer
import java.util.Locale
import org.json.JSONObject

/**
 * Kotlin adaptation of FluidInference/FluidAudio SpanishG2P.swift, Apache-2.0.
 * Pinned source: 0c0f113e8db4b862b19a99ce9fd7c9da73324f5f.
 * Changes: JVM Unicode handling, bounded lexicon loading, Spanish number/math normalization,
 * and explicit validation against the Kokoro v1 token alphabet. No eSpeak code is included.
 * See assets/licenses/FluidAudio-SpanishG2P.txt for the retained license and attribution.
 */
object SpanishFluidG2P {
    const val sourceRevision = "0c0f113e8db4b862b19a99ce9fd7c9da73324f5f"
    const val lexiconRevision = "d93c5f3eed11847fb81bd6d6a76665e133d7ca77"
    const val lexiconUrl = "https://huggingface.co/FluidInference/kokoro-82m-coreml/resolve/$lexiconRevision/es_lexicon_cache.json"
    const val lexiconBytes = 3_894_233L
    const val lexiconSha256 = "0c35e45f0384dde2fcb0e38b9a041246172f6e89491b1f26d1d8c1c9bb1d4317"

    /** Rule-based Spanish frontend plus an optional verified upstream exceptions dictionary. */
    fun phonemize(text: String, lexicon: Map<String, String> = emptyMap()): String {
        require(text.length <= 32_768) { "El texto español excede el límite de fonetización" }
        if (text.isBlank()) return ""
        val normalized = normalizeText(text)
        // Kokoro's published tokenizer removes these opening punctuation marks. Keep the
        // supported closing question/exclamation punctuation and all word stress/pauses.
        val phonemes = phonemizeNormalized(normalized, lexicon).filterNot { it == '¿' || it == '¡' }
        require(phonemes.isNotBlank()) { "El texto no contiene palabras españolas narrables" }
        require(phonemes.all { it in kokoroAlphabet && it != '$' }) {
            "La fonetización española produjo un símbolo no compatible con Kokoro"
        }
        return phonemes
    }

    /** Load once on the background synthesis worker, after the store verifies size and SHA-256. */
    fun loadLexicon(file: File): Map<String, String> {
        require(file.isFile && file.length() in 1L..8_000_000L) { "Diccionario español no válido" }
        val payload = JSONObject(file.readText(Charsets.UTF_8))
        val lower = payload.getJSONObject("lower")
        require(lower.length() in 1..100_000) { "Tamaño del diccionario español no válido" }
        val output = HashMap<String, String>(lower.length())
        lower.keys().forEach { word ->
            val phones = lower.getJSONArray(word)
            require(word.length in 1..128 && phones.length() in 1..512) { "Entrada de diccionario español no válida" }
            val pronunciation = buildString {
                for (index in 0 until phones.length()) append(phones.getString(index))
            }
            require(pronunciation.all { it in kokoroAlphabet && it != '$' }) { "Fonema de diccionario español desconocido" }
            output[word.lowercase(Locale.ROOT)] = pronunciation
        }
        // The pinned Spanish cache has no case-sensitive entries. Never silently ignore a
        // future resource's additional spelling contract while presenting it as compatible.
        require((payload.optJSONObject("caseSensitive")?.length() ?: 0) == 0) {
            "Esta versión del diccionario español requiere reglas de mayúsculas no soportadas"
        }
        return output
    }

    internal fun normalizeText(text: String): String {
        // In authored code announcements C is the language name, not the consonant /k/.
        val namedCode = text.replace(Regex("(?<![\\p{L}\\p{N}])C\\+\\+(?!\\+)", RegexOption.IGNORE_CASE), "ce más más")
            .replace(Regex("(?<![\\p{L}\\p{N}])C\\s+más\\s+más\\b", RegexOption.IGNORE_CASE), "ce más más")
        val math = SpanishMathSpeech.text(namedCode)
        // In authored lesson titles, " · " separates clauses (for example,
        // "Prueba de navegación 1 · Contenido provisional"). This symbol is
        // NOT in Kokoro's token alphabet. Preserve the pause as a comma;
        // arithmetic middot remains spoken as multiplication ("por").
        val separator = math.replace(Regex("\\s+·\\s+(?=\\p{Lu})"), ", ")
        val words = numberRuns.replace(separator) { match -> " ${SpanishMathSpeech.text(match.value)} " }
        return words.replace("%", " por ciento ").replace("=", " es igual a ")
            .replace("+", " más ").replace("×", " por ").replace("·", " por ")
            .replace("÷", " dividido entre ")
            .replace("/", " barra ").replace("\\", " barra inversa ")
            .replace("[", "(").replace("]", ")").replace("-", "—")
            .replace(Regex("\\s+"), " ").trim()
    }

    /** Exact upstream rule pipeline; input numbers must already have been verbalized. */
    internal fun phonemizeNormalized(text: String, lexicon: Map<String, String> = emptyMap()): String {
        val normalized = nfc(text).replace('«', '“').replace('»', '”').replace('\'', ' ')
        val tokens = tokenize(normalized)
        val items = tokens.mapIndexed { index, token ->
            if (!token.word) Item(Kind.PUNCTUATION, token.text, Stress.PRIMARY) else {
                val lower = token.text.lowercase(Locale.ROOT)
                var stress = when (lower) {
                    in unstressedWords -> Stress.NONE
                    in secondaryWords -> Stress.SECONDARY
                    else -> Stress.PRIMARY
                }
                val next = tokens.getOrNull(index + 1)?.text ?: "."
                if (stress != Stress.PRIMARY && next in pauseMarks) stress = Stress.PRIMARY
                Item(if (isAcronym(token.text)) Kind.ACRONYM else Kind.WORD, lower, stress)
            }
        }
        val output = mutableListOf<Token>()
        var previous: Char? = null
        for (item in items) {
            if (item.kind == Kind.PUNCTUATION) {
                output.add(Token(item.text, false))
                if (item.text in pauseMarks) previous = null
                continue
            }
            val entry = if (item.kind != Kind.ACRONYM && item.stress == Stress.PRIMARY) lexicon[item.text] else null
            val word = when {
                item.kind == Kind.ACRONYM -> spell(item.text)
                entry != null -> entry
                item.text == "y" -> "i"
                else -> phonemizeWord(item.text, item.stress)
            }
            val phonemes = applyAllophones(word, previous, firstPhoneOnly = entry != null)
            val bare = stripStress(phonemes)
            val last = output.lastOrNull()
            val first = bare.firstOrNull()
            if (previous != null && last?.word == true && first != null) {
                val prior = when {
                    last.text.endsWith('n') && first in "bp" -> last.text.dropLast(1) + "m"
                    last.text.endsWith('n') && first in "ɡx" -> last.text.dropLast(1) + "ŋ"
                    last.text.endsWith('d') && first in "aeiou" -> last.text.dropLast(1) + "ð"
                    else -> last.text
                }
                output[output.lastIndex] = last.copy(text = prior)
            }
            output.add(Token(phonemes, true))
            bare.lastOrNull()?.let { previous = it }
        }
        return join(output)
    }

    internal enum class Stress(val mark: String) { PRIMARY("ˈ"), SECONDARY("ˌ"), NONE("") }
    private enum class Kind { WORD, ACRONYM, PUNCTUATION }
    private enum class SegmentKind { CONSONANT, VOWEL, GLIDE }
    private data class Item(val kind: Kind, val text: String, val stress: Stress)
    private data class Token(val text: String, val word: Boolean)
    private data class Segment(var phone: String, var kind: SegmentKind, val accented: Boolean = false)
    private data class Syllable(val onset: List<Int>, val nucleus: List<Int>, var coda: List<Int> = emptyList())

    internal fun phonemizeWord(word: String, stress: Stress = Stress.PRIMARY): String {
        val normalized = nfc(word).lowercase(Locale.ROOT)
        if (stress == Stress.PRIMARY && normalized.length > 6 && normalized.endsWith("mente")) {
            return phonemizeWord(normalized.dropLast(5), Stress.PRIMARY) + "mˈente"
        }
        val segments = segment(foldForeign(normalized))
        val syllables = syllabify(segments) ?: return segments.joinToString("") { it.phone }
        val writtenAccent = syllables.indexOfFirst { syllable -> syllable.nucleus.any { segments[it].accented } }
        val stressed = when {
            writtenAccent >= 0 -> writtenAccent
            syllables.size <= 1 -> 0
            normalized.lastOrNull()?.let { it in "aeiouns" } == true -> syllables.size - 2
            else -> syllables.lastIndex
        }
        return buildString {
            syllables.forEachIndexed { index, syllable ->
                val onset = syllable.onset.joinToString("") { segments[it].phone }
                val (glides, nucleus) = nucleus(syllable, segments)
                var core = nucleus
                var mark = ""
                if (index == stressed) {
                    mark = stress.mark
                    if (stress == Stress.PRIMARY && core == "e" &&
                        syllable.coda.firstOrNull()?.let { segments[it].phone == "n" } == true &&
                        (syllable.coda.size >= 2 || syllables.getOrNull(index + 1)?.onset?.isNotEmpty() == true)) core = "ɛ"
                } else if (stress == Stress.PRIMARY && index < stressed - 1 && index % 2 == 0) mark = "ˌ"
                append(onset).append(glides).append(mark).append(core)
                append(syllable.coda.joinToString("") { segments[it].phone })
            }
        }
    }

    private fun segment(word: String): MutableList<Segment> {
        val output = mutableListOf<Segment>()
        var index = 0
        fun next(offset: Int = 1) = word.getOrNull(index + offset)
        fun consonant(phone: String) { output.add(Segment(phone, SegmentKind.CONSONANT)) }
        while (index < word.length) {
            when (val letter = word[index]) {
                in "aeoáéó" -> output.add(Segment(stripAccent(letter).toString(), SegmentKind.VOWEL, letter in "áéó"))
                in "iíuúü" -> output.add(Segment(stripAccent(letter).toString(),
                    if (letter in "íú") SegmentKind.VOWEL else SegmentKind.GLIDE, letter in "íú"))
                'y' -> when {
                    index + 1 == word.length && output.lastOrNull()?.phone == "u" -> {
                        output.last().kind = SegmentKind.VOWEL
                        consonant("j")
                    }
                    next() in vowelLetters -> consonant("ʝ")
                    else -> output.add(Segment("i", SegmentKind.GLIDE))
                }
                'c' -> when {
                    next() == 'h' -> { consonant("ʧ"); index++ }
                    next()?.let { it in "eiéí" } == true -> consonant("θ")
                    else -> consonant("k")
                }
                'q' -> { consonant("k"); if (next() == 'u') index++ }
                'g' -> when {
                    index + 1 == word.length && output.lastOrNull()?.phone == "n" -> output.last().phone = "ŋ"
                    next()?.let { it in "eiéí" } == true -> consonant("x")
                    next() == 'u' && next(2)?.let { it in "eiéí" } == true -> { consonant("ɡ"); index++ }
                    else -> consonant("ɡ")
                }
                'j' -> consonant("x")
                'h' -> if (index > 0 && next() != null) consonant("")
                'l' -> if (next() == 'l') { consonant("ʎ"); index++ } else consonant("l")
                'r' -> when {
                    next() == 'r' -> { consonant("r"); index++ }
                    index == 0 || output.lastOrNull()?.phone in setOf("n", "l", "s") -> consonant("r")
                    else -> consonant("ɾ")
                }
                'ñ' -> consonant("ɲ")
                'v' -> consonant("b")
                'z' -> consonant("θ")
                'x' -> { consonant("k"); consonant("s") }
                else -> consonant(letter.toString())
            }
            index++
        }
        return output
    }

    private fun syllabify(segments: MutableList<Segment>): List<Syllable>? {
        for (index in segments.indices) {
            if (segments[index].kind != SegmentKind.GLIDE) continue
            val previousVocalic = segments.getOrNull(index - 1)?.kind?.let { it != SegmentKind.CONSONANT } == true
            val nextVocalic = segments.getOrNull(index + 1)?.kind?.let { it != SegmentKind.CONSONANT } == true
            if (!previousVocalic && !nextVocalic) segments[index].kind = SegmentKind.VOWEL
        }
        for (index in 1 until segments.lastIndex) {
            if (segments[index].kind != SegmentKind.GLIDE || segments[index + 1].kind != SegmentKind.VOWEL) continue
            val liquid = segments[index - 1].phone
            if (liquid in setOf("l", "r", "ɾ") &&
                (index == 1 || segments[index - 2].kind == SegmentKind.CONSONANT || liquid == "r")) segments[index].kind = SegmentKind.VOWEL
        }
        for (index in 0 until segments.lastIndex) {
            if (segments[index].kind == SegmentKind.GLIDE && segments[index + 1].kind == SegmentKind.GLIDE) segments[index + 1].kind = SegmentKind.VOWEL
        }
        val spans = mutableListOf<List<Int>>()
        var index = 0
        while (index < segments.size) {
            if (segments[index].kind == SegmentKind.CONSONANT) { index++; continue }
            var group = mutableListOf<Int>()
            while (index < segments.size && segments[index].kind != SegmentKind.CONSONANT) {
                if (segments[index].kind == SegmentKind.VOWEL && group.any { segments[it].kind == SegmentKind.VOWEL }) {
                    spans.add(group)
                    group = mutableListOf()
                }
                group.add(index++)
            }
            spans.add(group)
        }
        if (spans.isEmpty()) return null
        val syllables = mutableListOf<Syllable>()
        var previousEnd = -1
        for ((spanIndex, span) in spans.withIndex()) {
            val cluster = ((previousEnd + 1) until span.first()).toList()
            var onset = cluster
            if (spanIndex > 0 && cluster.size >= 2) {
                val pair = segments[cluster[cluster.lastIndex - 1]].phone + segments[cluster[cluster.lastIndex]].phone
                val split = if (pair in onsetClusters) cluster.size - 2 else cluster.size - 1
                onset = cluster.drop(split)
                syllables.last().coda = cluster.take(split)
            }
            syllables.add(Syllable(onset, span))
            previousEnd = span.last()
        }
        syllables.last().coda = ((previousEnd + 1) until segments.size).toList()
        return syllables
    }

    private fun nucleus(syllable: Syllable, segments: List<Segment>): Pair<String, String> {
        val parts = syllable.nucleus.map(segments::get)
        val rendered = parts.map { if (it.kind == SegmentKind.GLIDE) if (it.phone == "i") "j" else "w" else it.phone }
        if (parts.size >= 2 && parts.last().kind == SegmentKind.GLIDE && parts[parts.lastIndex - 1].kind == SegmentKind.VOWEL) {
            fallingDiphthongs[parts[parts.lastIndex - 1].phone + parts.last().phone]?.let {
                return rendered.dropLast(2).joinToString("") to it
            }
        }
        val vowelIndex = parts.indexOfFirst { it.kind == SegmentKind.VOWEL }.takeIf { it >= 0 } ?: parts.lastIndex
        return rendered.take(vowelIndex).joinToString("") to rendered.drop(vowelIndex).joinToString("")
    }

    private fun applyAllophones(phonemes: String, previous: Char?, firstPhoneOnly: Boolean): String = buildString {
        var last = previous
        var adjusted = false
        for ((index, scalar) in phonemes.withIndex()) {
            if (scalar in "ˈˌ" || firstPhoneOnly && adjusted) { append(scalar); continue }
            adjusted = true
            val next = phonemes.substring(index + 1).firstOrNull { it !in "ˈˌ" }
            val afterPauseOrNasal = last == null || last in "mnɲŋ"
            val coda = next == null || next !in vocalicPhones && next !in "ɾlwjr"
            val phone = when (scalar) {
                'p' -> if (next == 't') "pː" else "p"
                'b' -> if (!afterPauseOrNasal && next != 't') "β" else "b"
                'd' -> if (!afterPauseOrNasal && !coda) "ð" else "d"
                'ɡ' -> if (!afterPauseOrNasal && !coda) "ɣ" else "ɡ"
                'n' -> when { next?.let { it in "bpf" } == true -> "m"; next == 'ɡ' || next == 'x' -> "ŋ"; else -> "n" }
                else -> scalar.toString()
            }
            append(phone)
            last = phone.lastOrNull()
        }
    }

    private fun spell(acronym: String): String {
        val names = acronym.lowercase(Locale.ROOT).mapNotNull { letterNames[it] }.map { phonemizeWord(it) }
        return names.dropLast(1).joinToString("") { it.replace('ˈ', 'ˌ') } + names.lastOrNull().orEmpty()
    }

    private fun isAcronym(token: String): Boolean {
        val lower = token.lowercase(Locale.ROOT)
        if (token != token.uppercase(Locale.ROOT) || token == lower || lower.any { it !in letterNames }) return false
        if (token.length == 1) return lower.single() !in "aeiouy"
        return token.length <= 5 && lower.none { it in vowelLetters || it == 'y' }
    }

    private fun tokenize(text: String): List<Token> {
        val output = mutableListOf<Token>()
        val word = StringBuilder()
        for (char in text) {
            if (char.isLetter()) { word.append(char); continue }
            if (word.isNotEmpty()) { output.add(Token(word.toString(), true)); word.clear() }
            if (!char.isWhitespace() && !char.isDigit() && char != '_') output.add(Token(char.toString(), false))
        }
        if (word.isNotEmpty()) output.add(Token(word.toString(), true))
        return output
    }

    private fun join(items: List<Token>): String = buildString {
        for (item in items) {
            if (!item.word && item.text in attachLeft) {
                while (lastOrNull() == ' ') deleteCharAt(lastIndex)
                append(item.text).append(' ')
            } else if (!item.word && item.text in attachRight) append(item.text)
            else append(item.text).append(' ')
        }
    }.trim()

    private fun foldForeign(word: String): String = buildString {
        for (char in word) {
            if (char in spanishLetters) { append(char); continue }
            val base = Normalizer.normalize(char.toString(), Normalizer.Form.NFD).filterNot {
                Character.getType(it) in setOf(Character.NON_SPACING_MARK.toInt(), Character.COMBINING_SPACING_MARK.toInt(), Character.ENCLOSING_MARK.toInt())
            }
            if (base.isNotEmpty() && base.all { it in spanishLetters }) append(base)
        }
    }

    private fun nfc(text: String): String = Normalizer.normalize(text, Normalizer.Form.NFC)
    private fun stripStress(text: String): String = text.filterNot { it in "ˈˌ" }
    private fun stripAccent(char: Char): Char = when (char) { 'á' -> 'a'; 'é' -> 'e'; 'í' -> 'i'; 'ó' -> 'o'; 'ú', 'ü' -> 'u'; else -> char }

    private val numberRuns = Regex("[−-]?\\d+(?:[.,]\\d+)?")
    private val unstressedWords = setOf("de", "la", "el", "en", "que", "y", "los", "a", "las", "se", "del", "con", "su", "por", "o", "al", "sus", "si", "lo", "le", "sin", "tras", "e", "quien", "u", "nos", "mis", "me", "te", "mi", "tu", "tus", "les", "os")
    private val secondaryWords = setOf("para", "como", "entre", "pero", "cuando", "sobre", "desde", "hacia", "hasta", "quienes", "bajo", "aunque", "donde", "mientras", "porque", "nuestros", "nuestra", "nuestro", "nuestras", "ante", "cuanto")
    private val letterNames = mapOf('a' to "a", 'b' to "be", 'c' to "ce", 'd' to "de", 'e' to "e", 'f' to "efe", 'g' to "ge", 'h' to "ache", 'i' to "i", 'j' to "jota", 'k' to "ka", 'l' to "ele", 'm' to "eme", 'n' to "ene", 'ñ' to "eñe", 'o' to "o", 'p' to "pe", 'q' to "cu", 'r' to "ere", 's' to "ese", 't' to "te", 'u' to "u", 'v' to "uve", 'w' to "uvedoble", 'x' to "equis", 'y' to "igriega", 'z' to "zeta")
    private val fallingDiphthongs = mapOf("ai" to "I", "au" to "W", "ei" to "A", "oi" to "oɪ", "eu" to "eʊ", "ui" to "uj", "ou" to "oʊ")
    private val onsetClusters = setOf("pɾ", "bɾ", "tɾ", "dɾ", "kɾ", "ɡɾ", "fɾ", "pl", "bl", "kl", "ɡl", "fl", "tl")
    private val pauseMarks = setOf(",", ".", "!", "?", ";", ":", "—", "…", "(", ")", "«", "»", "\"", "“", "”")
    private val attachLeft = setOf(",", ".", "!", "?", ";", ":", "…", ")", "»", "”")
    private val attachRight = setOf("¿", "¡", "(", "«", "“")
    private val vowelLetters = "aeiouáéíóúü".toSet()
    private val spanishLetters = "abcdefghijklmnopqrstuvwxyzáéíóúüñ".toSet()
    private val vocalicPhones = "aeiouɛIAWOɪʊ".toSet()
    // Exact published Kokoro-82M-v1.0-ONNX tokenizer vocabulary, excluding padding.
    private val kokoroAlphabet = ";:,.!?—…\"()“” ̃ʣʥʦʨᵝꭧAIOQSTWYᵊabcdefhijklmnopqrstuvwxyzɑɐɒæβɔɕçɖðʤəɚɛɜɟɡɥɨɪʝɯɰŋɳɲɴøɸθœɹɾɻʁɽʂʃʈʧʊʋʌɣɤχʎʒʔˈˌːʰʲ↓→↗↘ᵻ".toSet()
}
