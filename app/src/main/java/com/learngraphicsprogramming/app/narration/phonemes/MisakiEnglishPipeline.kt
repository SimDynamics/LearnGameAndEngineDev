package com.learngraphicsprogramming.app.narration.phonemes

/** Exact retokenization, compound resolution and right-to-left context from pinned en.py. */
internal object MisakiEnglishPipeline {
    private data class Cluster(val tokens: MutableList<MisakiWord>, val grouped: Boolean)
    // Explicit Unicode digit classes retain Python regex semantics on both Java and Android ICU.
    private val subtokens = Regex("^['‘’]+|\\p{Lu}(?=\\p{Lu}\\p{Ll})|(?:^-)?(?:\\p{Nd}?[,.]?\\p{Nd})+|[-_]+|['‘’]{2,}|\\p{L}*?(?:['‘’]\\p{L})*?\\p{Ll}(?=\\p{Lu})|\\p{L}+(?:['‘’]\\p{L})*|[^-_\\p{L}'‘’\\p{Nd}]|['‘’]+$")
    private const val junk = "',-._‘’/"
    private const val punctuation = ";:,.!?—…\"“”"
    private val punctuationTags = setOf(".", ",", "-LRB-", "-RRB-", "``", "\"\"", "''", ":", "$", "#", "NFP")
    private val punctuationPhones = mapOf("-LRB-" to "(", "-RRB-" to ")", "``" to "“", "\"\"" to "”", "''" to "”")
    private const val consonants = "bdfhjklmnpstvwzðŋɡɹɾʃʒʤʧθ"

    fun speak(tokens: List<MisakiWord>, lexicon: MisakiLexicon, fallback: ((String) -> String)?): String {
        return speakCancellable(tokens, lexicon, fallback) {}
    }

    fun speakCancellable(tokens: List<MisakiWord>, lexicon: MisakiLexicon, fallback: ((String) -> String)?, checkCurrent: () -> Unit): String {
        return speakObserved(tokens, lexicon, fallback, checkCurrent, null)
    }

    /** Observes the final resolved phones; it never re-pronounces isolated words. */
    fun speakObserved(tokens: List<MisakiWord>, lexicon: MisakiLexicon, fallback: ((String) -> String)?,
        checkCurrent: () -> Unit, onPiece: ((MisakiWord, Int, Int) -> Unit)?): String {
        checkCurrent()
        val groups = retokenize(tokens, checkCurrent)
        var context = PhoneticContext()
        for (cluster in groups.asReversed()) {
            checkCurrent()
            val words = cluster.tokens
            if (!cluster.grouped || words.size == 1) {
                val word = words.single()
                if (word.phonemes == null) word.phonemes = lexicon.tryPronounce(word, context) ?: missing(word.text, fallback)
                context = context(context, word.phonemes, word)
                continue
            }
            var left = 0
            var right = words.size
            var useFallback = false
            while (left < right) {
                checkCurrent()
                val current = words.subList(left, right)
                val merged = if (current.any { it.phonemes != null || it.alias != null }) null else merge(current)
                val spoken = merged?.let { lexicon.tryPronounce(it, context) }
                if (spoken != null) {
                    words[left].phonemes = spoken
                    words[left].sourceEndExclusive = words[right - 1].sourceEndExclusive
                    for (index in left + 1 until right) words[index].phonemes = ""
                    context = context(context, spoken, checkNotNull(merged))
                    right = left
                    left = 0
                } else if (left + 1 < right) left++
                else {
                    right--
                    val word = words[right]
                    if (word.phonemes == null) {
                        if (word.text.all { it in junk }) word.phonemes = ""
                        else if (fallback != null) { useFallback = true; break }
                        else throw EnglishMisakiException("Misaki has no verified pronunciation for '${word.text}'; install the original BART resource")
                    }
                    left = 0
                }
            }
            if (useFallback) {
                words.first().phonemes = missing(merge(words).text, fallback)
                words.first().sourceEndExclusive = words.last().sourceEndExclusive
                for (index in 1 until words.size) words[index].phonemes = ""
            } else resolve(words)
        }
        val observed = if (onPiece == null) null else mutableListOf<Triple<MisakiWord, Int, Int>>()
        val output = buildString {
            for (cluster in groups) {
                checkCurrent()
                val word = merge(cluster.tokens)
                val phones = (word.phonemes ?: throw EnglishMisakiException("Unresolved Misaki token: ${word.text}"))
                    .replace('ɾ', 'T').replace('ʔ', 't')
                val clusterStart = length
                append(phones)
                if (onPiece != null) {
                    var local = 0
                    for (part in cluster.tokens) {
                        val partPhones = part.phonemes.orEmpty().replace('ɾ', 'T').replace('ʔ', 't')
                        if (part.preSpace && local > 0 && !phones[local - 1].isWhitespace() && partPhones.isNotEmpty()) local++
                        if (partPhones.isNotEmpty()) observed?.add(Triple(part, clusterStart + local, clusterStart + local + partPhones.length))
                        local += partPhones.length
                    }
                    check(local == phones.length) { "Misaki phoneme provenance did not match its resolved cluster" }
                }
                append(word.whitespace)
            }
        }
        val prefix = output.indexOfFirst { !it.isWhitespace() }.coerceAtLeast(0)
        observed?.forEach { (word, start, end) -> onPiece?.invoke(word, start - prefix, end - prefix) }
        return output.trim()
    }

    private fun missing(word: String, fallback: ((String) -> String)?): String = fallback?.invoke(word)
        ?: throw EnglishMisakiException("Misaki has no verified pronunciation for '$word'; install the original BART resource")

    private fun retokenize(tokens: List<MisakiWord>, checkCurrent: () -> Unit): MutableList<Cluster> {
        val output = mutableListOf<Cluster>()
        var currency: String? = null
        for ((index, original) in tokens.withIndex()) {
            checkCurrent()
            val parts = if (original.override != null || original.alias != null) mutableListOf(original.copy(phonemes = original.override)) else
                subtokens.findAll(original.text).map { original.copy(text = it.value, whitespace = "", phonemes = null,
                    currency = null, isHead = true, preSpace = false,
                    sourceStart = original.sourceStart.takeIf { start -> start >= 0 }?.plus(it.range.first) ?: -1,
                    sourceEndExclusive = original.sourceStart.takeIf { start -> start >= 0 }?.plus(it.range.last + 1) ?: -1) }.toMutableList()
            if (parts.isEmpty()) continue
            parts.last().whitespace = original.whitespace
            for ((partIndex, word) in parts.withIndex()) {
                val tag = word.tag
                when {
                    word.phonemes != null -> Unit
                    tag == "$" && word.text in setOf("$", "£", "€") -> { currency = word.text; word.phonemes = "" }
                    tag == ":" && word.text in listOf("-", "–") -> word.phonemes = "—"
                    tag in punctuationTags && word.text.any { it.lowercaseChar() !in 'a'..'z' } ->
                        word.phonemes = punctuationPhones[tag] ?: word.text.filter { it in punctuation }
                    currency != null -> {
                        if (tag != "CD") currency = null
                        else if (partIndex + 1 == parts.size && (index + 1 == tokens.size || tokens[index + 1].tag != "CD")) word.currency = currency
                    }
                    partIndex > 0 && partIndex < parts.lastIndex && word.text == "2" &&
                        parts[partIndex - 1].text.last().isLetter() && parts[partIndex + 1].text.first().isLetter() -> word.alias = "to"
                }
                if (word.phonemes != null || word.alias != null) output += Cluster(mutableListOf(word), false)
                else if (output.lastOrNull()?.let { it.grouped && it.tokens.last().whitespace.isEmpty() } == true) {
                    word.isHead = false
                    output.last().tokens += word
                }
                else output += Cluster(mutableListOf(word), word.whitespace.isEmpty())
            }
        }
        return output
    }

    private fun merge(words: List<MisakiWord>): MisakiWord {
        val text = words.dropLast(1).joinToString("") { it.text + it.whitespace } + words.last().text
        val phones = if (words.all { it.phonemes != null }) buildString {
            for (word in words) {
                if (word.preSpace && isNotEmpty() && !last().isWhitespace() && !word.phonemes.isNullOrEmpty()) append(' ')
                append(word.phonemes)
            }
        } else null
        val tag = words.maxBy { word -> word.text.sumOf { if (it == it.lowercaseChar()) 1 else 2 } }.tag
        return MisakiWord(text, words.last().whitespace, tag, currency = words.mapNotNull { it.currency }.maxOrNull(),
            phonemes = phones, stress = words.mapNotNull { it.stress }.distinct().singleOrNull(),
            isHead = words.first().isHead, numFlags = words.flatMap { it.numFlags.toList() }.toSortedSet().joinToString(""),
            sourceStart = words.first().sourceStart, sourceEndExclusive = words.last().sourceEndExclusive)
    }

    private fun resolve(words: List<MisakiWord>) {
        val text = merge(words).text
        val space = ' ' in text || '/' in text || text.filter { it !in junk }.map { if (it.isLetter()) 0 else if (it.isDigit()) 1 else 2 }.toSet().size > 1
        for ((index, word) in words.withIndex()) {
            if (word.phonemes == null) {
                if (index == words.lastIndex && word.text.length == 1 && word.text.single() in punctuation && word.text !in listOf("\"", "“", "”")) word.phonemes = word.text
                else if (word.text.all { it in junk }) word.phonemes = ""
            } else if (index > 0) word.preSpace = space
        }
        if (space) return
        val indices = words.indices.filter { !words[it].phonemes.isNullOrEmpty() }
        if (indices.size == 2 && words[indices.first()].text.length == 1) {
            val index = indices.last()
            words[index].phonemes = MisakiStress.apply(words[index].phonemes, -0.5)
        } else if (indices.size >= 2 && indices.count { 'ˈ' in words[it].phonemes.orEmpty() } > (indices.size + 1) / 2) {
            indices.sortedWith(compareBy<Int> { 'ˈ' in words[it].phonemes.orEmpty() }.thenBy { weight(words[it].phonemes.orEmpty()) }.thenBy { it })
                .take(indices.size / 2).forEach { words[it].phonemes = MisakiStress.apply(words[it].phonemes, -0.5) }
        }
    }

    private fun weight(value: String) = value.sumOf { if (it in "AIOQWYʤʧ") 2 else 1 }
    private fun context(previous: PhoneticContext, phones: String?, token: MisakiWord): PhoneticContext {
        val first = phones.orEmpty().firstOrNull { it in MisakiStress.VOWELS || it in consonants || it in punctuation && it !in "\"“”" }
        val vowel = when { first == null -> previous.futureVowel; first in punctuation -> null; else -> first in MisakiStress.VOWELS }
        return PhoneticContext(vowel, token.text in listOf("to", "To") || token.text == "TO" && token.tag in listOf("TO", "IN"))
    }
}
