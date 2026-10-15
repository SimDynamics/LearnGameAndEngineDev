package com.learngraphicsprogramming.app.narration.phonemes

import android.os.Looper
import android.util.JsonReader
import java.io.File
import java.text.Normalizer
import java.util.Locale
import org.json.JSONObject

/** Explicit French word pronunciation; independent of audio, voices and distribution. */
fun interface FrenchWordG2P {
    fun phonemizeWord(word: String): String?
}

class FrenchG2PException(message: String, cause: Throwable? = null) : IllegalStateException(message, cause)

/**
 * Kotlin port of FluidAudio FrenchG2P/FrenchPhonology, Apache-2.0.
 * Upstream revision: 0c0f113e8db4b862b19a99ce9fd7c9da73324f5f.
 * Modified for Android: explicit unknown-word errors, bounded fallback cache,
 * French number normalization and strict Kokoro vocabulary validation.
 * This frontend never downloads files and never runs eSpeak.
 */
class FrenchFluidG2P private constructor(
    private val lexicon: FrenchLexicon,
    private val fallback: FrenchWordG2P?,
    private val vocabulary: Set<Char>?
) {
    constructor(resourcesDirectory: File, kokoroTokenizer: File, fallback: FrenchWordG2P? = FrenchRuleBasedG2P) : this(
        loadLexicon(resourcesDirectory), fallback, loadVocabulary(kokoroTokenizer)
    )

    private val fallbackCache = object : LinkedHashMap<String, String>(256, 0.75f, true) {
        override fun removeEldestEntry(eldest: MutableMap.MutableEntry<String, String>?): Boolean = size > 256
    }

    /** Must be called on a worker. The verified dictionary always precedes the OOV rules. */
    @Synchronized fun phonemize(text: String): String = phonemizeImpl(text, emptyMap())

    /**
     * Runtime-only provenance: selected English phones replace reserved single-character markers.
     * All French words still traverse the same complete-sentence frontend. Foreign phones are
     * opaque to French stress, spelling, adaptation and liaison; no audio is split or produced here.
     */
    @Synchronized internal fun phonemizeWithForeignTokens(text: String, foreignTokens: Map<Char, String>): String {
        foreignTokens.forEach { (marker, phones) ->
            if (marker !in '\uE100'..'\uF8FF' || phones.isBlank() || phones.length > 4096 ||
                phones.any { it == '$' || it.isISOControl() || it.isSurrogate() || it in '\uE000'..'\uF8FF' }) {
                throw FrenchG2PException("Fragmento fonético extranjero no válido")
            }
        }
        if (text.any { it in '\uE100'..'\uF8FF' && it !in foreignTokens }) {
            throw FrenchG2PException("Marcador fonético extranjero sin resolver")
        }
        return phonemizeImpl(text, foreignTokens)
    }

    private fun phonemizeImpl(text: String, foreignTokens: Map<Char, String>): String {
        if (text.length > 16_384) throw FrenchG2PException("El texto francés es demasiado largo")
        if (text.isBlank()) return ""
        val normalized = FrenchNumberNormalizer.normalize(text)
        val phonemes = FrenchPhonology.phonemize(normalized, lexicon::contains, foreignTokens) { word ->
            FrenchPhonology.citationOverrides[word]?.let { FrenchPronunciation.Raw(it) }
                ?: lexicon.lookup(word)?.let { FrenchPronunciation.Cached(it, word in lexicon.hAspire) }
                ?: if (word.all { it.isLetter() }) {
                    val raw = fallbackCache[word] ?: fallback?.phonemizeWord(word)?.takeIf { it.isNotBlank() }
                        ?.also { fallbackCache[word] = it }
                        ?: throw FrenchG2PException("No se pudo obtener una pronunciación francesa para «$word»")
                    // The installed h-aspiré list also applies to absent inflections.
                    FrenchPronunciation.Raw(if (word in lexicon.hAspire && !raw.startsWith("ʼ")) "ʼ$raw" else raw)
                } else null
        }
        vocabulary?.let { allowed ->
            val invalid = phonemes.firstOrNull { it !in allowed }
            if (invalid != null) throw FrenchG2PException("Fonema francés incompatible con Kokoro: U+${invalid.code.toString(16)}")
        }
        return phonemes
    }

    companion object {
        private fun loadLexicon(directory: File): FrenchLexicon {
            check(Looper.myLooper() != Looper.getMainLooper()) { "Los recursos franceses se cargan fuera del hilo principal" }
            val file = File(directory, FrenchFluidResources.lexiconFileName)
            if (file.canonicalFile.parentFile != directory.canonicalFile) throw FrenchG2PException("Directorio francés no válido")
            FrenchFluidResources.requireVerifiedLexicon(file)
            try {
                val lower = HashMap<String, String>()
                val caseSensitive = HashMap<String, String>()
                val aspirated = HashSet<String>()
                // The full 250k-entry JSON cache is optional and read incrementally;
                // retaining a second JSONObject tree needlessly exhausts Android heaps.
                JsonReader(file.reader(Charsets.UTF_8)).use { reader ->
                    fun rows(destination: MutableMap<String, String>) {
                        reader.beginObject()
                        while (reader.hasNext()) {
                            if (Thread.currentThread().isInterrupted) throw InterruptedException()
                            val word = reader.nextName()
                            reader.beginArray()
                            val phones = buildString { while (reader.hasNext()) append(reader.nextString()) }
                            reader.endArray()
                            if (word.length !in 1..256 || phones.length !in 1..1024) throw FrenchG2PException("Entrada francesa no válida")
                            destination[word] = phones
                        }
                        reader.endObject()
                    }
                    reader.beginObject()
                    while (reader.hasNext()) when (reader.nextName()) {
                        "lower" -> rows(lower)
                        "caseSensitive" -> rows(caseSensitive)
                        "hAspire" -> {
                            reader.beginArray()
                            while (reader.hasNext()) aspirated += reader.nextString().lowercase(Locale.ROOT)
                            reader.endArray()
                        }
                        else -> reader.skipValue()
                    }
                    reader.endObject()
                }
                if (lower.isEmpty()) throw FrenchG2PException("Diccionario francés vacío")
                return FrenchLexicon(lower, caseSensitive, aspirated)
            } catch (error: Exception) {
                throw FrenchG2PException("El diccionario francés no se pudo leer", error)
            }
        }

        private fun loadVocabulary(file: File): Set<Char> {
            try {
                val vocab = JSONObject(file.readText(Charsets.UTF_8)).getJSONObject("model").getJSONObject("vocab")
                return vocab.keys().asSequence().filter { it.length == 1 && vocab.getInt(it) != 0 }
                    .map { it.single() }.toSet().also {
                        if (it.isEmpty()) throw FrenchG2PException("Vocabulario de Kokoro vacío")
                    }
            } catch (error: Exception) {
                throw FrenchG2PException("No se pudo leer el vocabulario de Kokoro", error)
            }
        }

        internal fun fromLexicon(
            entries: Map<String, String>, hAspire: Set<String> = emptySet(),
            fallback: FrenchWordG2P? = null, vocabulary: Set<Char>? = null
        ) = FrenchFluidG2P(FrenchLexicon(entries, emptyMap(), hAspire), fallback, vocabulary)
    }
}

internal data class FrenchLexicon(
    val lower: Map<String, String>, val caseSensitive: Map<String, String>, val hAspire: Set<String>
) {
    fun lookup(word: String): String? = caseSensitive[word] ?: lower[word.lowercase(Locale.ROOT)]
    fun contains(word: String) = lookup(word) != null
}

internal sealed class FrenchPronunciation {
    data class Cached(val phones: String, val hAspire: Boolean) : FrenchPronunciation()
    data class Raw(val phones: String) : FrenchPronunciation()
}

/** Sentence-level rules translated from the selected FluidAudio FrenchPhonology. */
internal object FrenchPhonology {
    enum class Stress { PRIMARY, SECONDARY, NONE }
    data class Token(val text: String, val isWord: Boolean, val spaceBefore: Boolean)
    private data class Item(
        val isWord: Boolean, val phones: String, val word: String = "", val hAspire: Boolean = false,
        val spaceBefore: Boolean = false, val foreign: Boolean = false
    )

    fun phonemize(text: String, isLexiconEntry: (String) -> Boolean,
                  foreignTokens: Map<Char, String> = emptyMap(), lookup: (String) -> FrenchPronunciation?): String {
        val normalized = Normalizer.normalize(text, Normalizer.Form.NFC).replace('’', '\'').replace('«', '“').replace('»', '”')
        val tokens = tokenize(normalized, foreignTokens.keys)
        val items = mutableListOf<Item>()
        var skip = 0
        tokens.forEachIndexed { index, token ->
            if (skip > 0) { skip--; return@forEachIndexed }
            if (!token.isWord) { items += Item(false, token.text, spaceBefore = token.spaceBefore); return@forEachIndexed }
            val foreign = token.text.singleOrNull()?.let(foreignTokens::get)
            if (foreign != null) {
                items += Item(true, foreign, spaceBefore = token.spaceBefore, foreign = true)
                return@forEachIndexed
            }
            val phrase = phrases.firstOrNull { (words, _) ->
                index + words.size <= tokens.size && words.indices.all {
                    tokens[index + it].isWord && tokens[index + it].text.lowercase(Locale.ROOT) == words[it]
                }
            }
            if (phrase != null) {
                items += Item(true, phrase.second, tokens[index + phrase.first.size - 1].text.lowercase(Locale.ROOT), spaceBefore = token.spaceBefore)
                skip = phrase.first.size - 1
                return@forEachIndexed
            }
            val trailingApostrophe = token.text.endsWith("'")
            val lower = token.text.lowercase(Locale.ROOT).trimEnd('\'')
            if (isSpelledAcronym(token.text)) {
                items += Item(true, spell(lower), lower, spaceBefore = token.spaceBefore)
                return@forEachIndexed
            }
            var prefix = ""
            var core = lower
            if (trailingApostrophe && lower in elisionPrefixes) {
                prefix = elisionPrefixes.getValue(lower); core = ""
            } else if ('\'' in lower && !isLexiconEntry(lower)) {
                val parts = lower.split('\'', limit = 2)
                elisionPrefixes[parts[0]]?.let { prefix = it; core = parts.getOrElse(1) { "" } }
            }
            val parts = core.split('\'', limit = 2)
            val classWord = if (parts.size == 2 && parts[0] in elisionPrefixes) parts[1] else core
            var stress = when (classWord) {
                in unstressedWords -> Stress.NONE
                in secondaryWords -> Stress.SECONDARY
                else -> Stress.PRIMARY
            }
            if (stress != Stress.PRIMARY && tokens.getOrNull(index + 1)?.text.orEmpty().ifEmpty { "." } in pauseMarks) stress = Stress.PRIMARY
            var hAspire = false
            var phones: String
            if ('-' in core && stress == Stress.PRIMARY) {
                val hyphenParts = core.split('-').filter { it.isNotEmpty() }
                val resolved = hyphenParts.mapIndexed { j, word -> hyphenPartPhonemes(word, j == 0, lookup) }
                hAspire = resolved.firstOrNull()?.second ?: false
                phones = buildString {
                    hyphenParts.forEachIndexed { j, word ->
                        var piece = resolved[j].first
                        if (j + 1 < resolved.size && !resolved[j + 1].second && startsWithVowel(resolved[j + 1].first)) piece = applyLiaison(word, piece)
                        append(piece)
                    }
                }
            } else {
                val resolved = if (core.isEmpty()) "" to false else wordPhonemes(core, stress, lookup)
                phones = resolved.first; hAspire = resolved.second
            }
            if (prefix.isNotEmpty()) phones = mapToKokoro(prefix, "") + phones
            items += Item(true, phones, classWord, hAspire, token.spaceBefore)
        }
        val out = items.mapIndexed { index, item ->
            val next = items.getOrNull(index + 1)
            if (item.isWord && !item.foreign && next?.isWord == true && !next.foreign &&
                !next.hAspire && startsWithVowel(next.phones))
                item.copy(phones = applyLiaison(item.word, item.phones)) else item
        }
        val result = StringBuilder()
        var pendingSpace = false
        var lastWasWord = false
        out.forEach { item ->
            pendingSpace = pendingSpace || item.spaceBefore
            if (item.phones.isNotEmpty()) {
                if (result.isNotEmpty() && (pendingSpace || (item.isWord && lastWasWord))) result.append(' ')
                result.append(item.phones); pendingSpace = false; lastWasWord = item.isWord
            }
        }
        return result.toString()
    }

    private fun hyphenPartPhonemes(word: String, isFirst: Boolean, lookup: (String) -> FrenchPronunciation?): Pair<String, Boolean> {
        if (!isFirst) {
            if (word == "t") return "t" to false
            reducedClitics[word]?.let { return it to false }
            if (word in unstressedClitics) return wordPhonemes(word, Stress.NONE, lookup)
            if (word == "moi" || word == "toi") return wordPhonemes(word, Stress.SECONDARY, lookup)
        }
        return wordPhonemes(word, if (isFirst && word in unstressedWords) Stress.NONE else Stress.PRIMARY, lookup)
    }

    private fun wordPhonemes(word: String, stress: Stress, lookup: (String) -> FrenchPronunciation?): Pair<String, Boolean> = when (val hit = lookup(word)) {
        is FrenchPronunciation.Cached ->
            (if (stress == Stress.PRIMARY && 'ˈ' in hit.phones) hit.phones else addStress(stripStress(hit.phones), stress)) to hit.hAspire
        is FrenchPronunciation.Raw -> addStress(mapToKokoro(hit.phones, word), stress) to hit.phones.startsWith("ʼ")
        null -> {
            if ('-' !in word && '\'' !in word) throw FrenchG2PException("No se pudo fonetizar «$word»")
            val joined = word.split('-', '\'').filter { it.isNotEmpty() }.joinToString("") { wordPhonemes(it, Stress.NONE, lookup).first }
            addStress(joined, stress) to false
        }
    }

    fun mapToKokoro(raw: String, word: String): String {
        var p = raw.replace('g', 'ɡ').replace('ɥ', 'y').replace("ʼ", "").replace("‿", "")
        if (Regex("b[tsc]").containsMatchIn(word)) p = Regex("p(?=[tsk])").replaceFirst(p, "b")
        var chars = p.toMutableList()
        if (chars.size >= 2 && word.length >= 3 && word[1] == 'e' && word[0] !in "aeiouy" && word[2] !in "aeiouysxz" &&
            (word.length == 3 || word[2] != word[3]) && chars[0] in consonants && chars[1] in consonants && chars[1] !in onsetSeconds) chars.add(1, 'ə')
        p = chars.joinToString("")
        Regex("e(ll|ss|tt|nn|mm|rr)").findAll(word).forEach { match ->
            val group = match.groupValues[1]
            val phone = if (group == "rr") "ʁ" else group.take(1)
            p = p.replaceFirst("e$phone", "ɛ$phone")
        }
        chars = p.toMutableList()
        val last = lastVowelIndex(chars)
        val doubleO = Regex("o(mm|nn|pp|ll|tt|rr|ff|cc)").containsMatchIn(word)
        chars.indices.forEach { i ->
            val next = chars.getOrNull(i + 1)
            if (chars[i] == 'ɔ' && next != '\u0303' && i < last) {
                val closed = i + 2 < chars.size && next in consonants && chars[i + 2] in consonants && chars[i + 2] !in onsetSeconds
                if (!closed && !doubleO) chars[i] = 'o'
            } else if (chars[i] == 'œ' && next != '\u0303') {
                if (i < last || (next != null && next in "vptb" && 'œ' !in word)) chars[i] = 'ø'
            }
        }
        var k = 2
        while (k < chars.size - 2) {
            if (chars[k] == 'ə' && chars[k - 1] in consonants && chars[k - 2] in vowels && chars[k + 1] in consonants && chars[k + 2] in vowels) chars.removeAt(k)
            k++
        }
        p = chars.joinToString("")
        p = Regex("ɲ(?!$)").replace(p, "nj")
        p = Regex("(?<=[aeɛ])ɲ$").replace(p, "nj")
        p = Regex("(?<=[bdfɡkpstv][lʁ])ij(?=[aeiouyøœɛɔɑə])").replace(p, "i")
        if (Regex("[ôû][^aeiouyéèêàâîôû]*e?s?$").containsMatchIn(word)) {
            chars = p.toMutableList()
            val lastVowel = lastVowelIndex(chars)
            if (lastVowel >= 0 && chars[lastVowel] in "oy") { chars.add(lastVowel + 1, 'ː'); p = chars.joinToString("") }
        }
        return p
    }

    private fun stripStress(phones: String) = phones.filter { it != 'ˈ' && it != 'ˌ' }
    private fun lastVowelIndex(chars: List<Char>): Int {
        val last = chars.indexOfLast { it in vowels }
        if (last >= 0 && chars[last] == 'ə') return chars.take(last).indexOfLast { it in vowels }.takeIf { it >= 0 } ?: last
        return last
    }
    fun addStress(phones: String, stress: Stress): String {
        if (stress == Stress.NONE) return phones
        val index = lastVowelIndex(phones.toList())
        if (index < 0) return phones
        return phones.substring(0, index) + (if (stress == Stress.PRIMARY) "ˈ" else "ˌ") + phones.substring(index)
    }
    fun applyLiaison(originalWord: String, phones: String): String {
        val word = originalWord.replace("'", "")
        val letter = word.lastOrNull() ?: return phones
        val last = phones.lastOrNull { it != 'ˈ' && it != 'ˌ' } ?: return phones
        return when (letter) {
            's', 'x', 'z' -> when {
                last == 's' || last == 'z' -> if (word in voicedFinalS) phones.dropLast(1) + "z" else if (word.endsWith("es")) phones + "z" else phones
                word in sLiaisonWords -> phones + "z"
                Regex("(is|us|és)$").containsMatchIn(word) -> phones
                else -> phones + "z"
            }
            'n' -> if (word in nasalLiaisonWords && last == '\u0303') phones + "n" else phones
            't', 'd' -> when {
                word.endsWith("ent") && last in consonants -> phones + "t"
                last == 't' || last == 'd' -> phones
                word in tLiaisonWords || Regex("(ait|aient|eut|ont)$").containsMatchIn(word) -> phones + "t"
                else -> phones
            }
            'f' -> if (word == "neuf" && last == 'f') phones.dropLast(1) + "v" else phones
            'p' -> if (word in setOf("trop", "beaucoup") && last != 'p') phones + "p" else phones
            else -> phones
        }
    }
    private fun startsWithVowel(phones: String): Boolean = phones.firstOrNull { it != 'ˈ' && it != 'ˌ' }?.let { it in vowels || it == 'j' || it == 'w' } ?: false

    private fun tokenize(text: String, foreignMarkers: Set<Char> = emptySet()): List<Token> {
        val tokens = mutableListOf<Token>()
        var i = 0
        var sawSpace = false
        while (i < text.length) {
            val ch = text[i]
            if (ch.isWhitespace() || Character.isSpaceChar(ch)) { sawSpace = true; i++; continue }
            if (ch in foreignMarkers) {
                tokens += Token(ch.toString(), true, sawSpace)
                sawSpace = false; i++; continue
            }
            if (ch.isLetter()) {
                var j = i
                while (j < text.length && text[j].isLetter()) j++
                while (j + 1 < text.length && text[j] in "'-" && text[j + 1].isLetter()) {
                    j++; while (j < text.length && text[j].isLetter()) j++
                }
                if (j < text.length && text[j] == '\'') j++
                tokens += Token(text.substring(i, j), true, sawSpace); sawSpace = false; i = j; continue
            }
            if (!ch.isDigit() && ch != '_') { tokens += Token(ch.toString(), false, sawSpace); sawSpace = false }
            else throw FrenchG2PException("Nombre o símbolo francés sin normalizar: $ch")
            i++
        }
        return tokens
    }
    fun isSpelledAcronym(token: String): Boolean {
        if (token != token.uppercase(Locale.ROOT) || token == token.lowercase(Locale.ROOT)) return false
        if (token.length == 1) return token.lowercase(Locale.ROOT).single() in letterNames && token != "A" && token != "Y"
        return token.length <= 5 && token.none { it in "AEIOUYÀÂÉÈÊËÎÏÔÛÙÜ" }
    }
    private fun spell(word: String): String {
        val names = word.mapNotNull { letterNames[it] }
        if (names.isEmpty()) throw FrenchG2PException("Sigla francesa sin pronunciación")
        return names.dropLast(1).joinToString("") { addStress(it, Stress.SECONDARY) } + addStress(names.last(), Stress.PRIMARY)
    }

    private val phrases = listOf(
        listOf("tout", "le", "monde") to "tulmˈɔ̃d", listOf("parce", "que") to "paʁskˌə",
        listOf("est-ce", "que") to "ɛs kə", listOf("c'est-à-dire") to "sɛtadˈiʁ", listOf("etc") to "ɛtseteʁˈa"
    )
    private val unstressedWords = "de les la et le des à un est du en une que qui a il pour dans plus sur au ce ou se qu leur aux par comme mais autres vous ne tout y ils on leurs peu tous son elle cette car ces sa autre elles toute ses chaque sans sous nous entre très nos dont où vos tel quand lors vers eu toutes tels près non celles furent me ceux notre quel cet telle quelle je ni mes chez ça quant dès votre faut sommes l d j m n s t c jusqu lorsqu puisqu te tu mon ma ton ta pas contre".split(' ').toSet()
    private val secondaryWords = "avec aussi plusieurs cela ainsi beaucoup après plupart quelques encore soit assez parmi toujours afin presque alors avant mieux durant tandis rien depuis comment quelque néanmoins jamais loin autour toutefois lorsque puisque lui".split(' ').toSet()
    val citationOverrides = mapOf("est" to "ɛ", "plus" to "ply", "tous" to "tu", "un" to "œ̃", "six" to "sis", "dix" to "dis", "fils" to "fis", "y" to "i", "os" to "ɔs", "ai" to "e", "es" to "ɛ", "as" to "a", "eu" to "y", "eus" to "y", "eut" to "y", "août" to "ut", "ces" to "se", "pays" to "pɛi")
    private val letterNames = mapOf('a' to "a", 'b' to "be", 'c' to "se", 'd' to "de", 'e' to "ə", 'f' to "ɛf", 'g' to "ʒe", 'h' to "aʃ", 'i' to "i", 'j' to "ʒi", 'k' to "ka", 'l' to "ɛl", 'm' to "ɛm", 'n' to "ɛn", 'o' to "o", 'p' to "pe", 'q' to "ky", 'r' to "ɛʁ", 's' to "ɛs", 't' to "te", 'u' to "y", 'v' to "ve", 'w' to "dubləve", 'x' to "iks", 'y' to "iɡʁɛk", 'z' to "zɛd")
    private val elisionPrefixes = mapOf("jusqu" to "ʒysk", "lorsqu" to "lɔʁsk", "puisqu" to "pɥisk", "quoiqu" to "kwak", "qu" to "k", "l" to "l", "d" to "d", "j" to "ʒ", "m" to "m", "n" to "n", "s" to "s", "t" to "t", "c" to "s")
    private val nasalLiaisonWords = "en on un bien son mon ton aucun rien bon certain plein lun".split(' ').toSet()
    private val tLiaisonWords = "tout sont ont fait peut doit vont font dont quand grand petit avant pendant devant était est soit furent sept huit vingt fort trop".split(' ').toSet()
    private val voicedFinalS = setOf("six", "dix")
    private val sLiaisonWords = "plus nous vous sous dans pas mais très jamais trois moins puis depuis alors toujours après dès chez assez".split(' ').toSet()
    private val reducedClitics = mapOf("je" to "ʒ", "ce" to "s")
    private val unstressedClitics = "il ils on vous nous tu le la les en y lui leur".split(' ').toSet()
    private val vowels = "aeiouyøœɛɔɑəɜ".toSet()
    private val consonants = "bdfɡklmnpstvzʃʒʁjwɲŋ".toSet()
    private val onsetSeconds = "ʁlj".toSet()
    private val pauseMarks = setOf(",", ".", "!", "?", ";", ":", "—", "…", "(", ")", "«", "»", "\"", "“", "”")
}
