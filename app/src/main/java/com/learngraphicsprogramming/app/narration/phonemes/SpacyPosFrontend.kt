package com.learngraphicsprogramming.app.narration.phonemes

import java.util.regex.Pattern
import java.util.Locale
import org.json.JSONObject

/** Original spaCy token boundaries, trailing ASCII space and model-predicted Penn tag. */
data class SpacyTaggedToken(val text: String, val whitespace: String, val tag: String)

internal data class SpacyPosToken(
    val text: String,
    var whitespace: String = "",
    val normOverride: String? = null,
    val start: Int = 0
)

/** Port of spaCy 3.8.7 tokenizer/lexical attributes, using the exported 3.8.0 rules. */
internal class SpacyPosFrontend(json: JSONObject) {
    val labels: List<String>
    private val seeds: IntArray
    private val rows: IntArray
    private val symbols: Map<String, Long>
    private val lowercase: Map<Int, String>
    private val shapeRanges: List<ShapeRange>
    private val whitespace: Set<Int>
    private val normLookup: Map<Long, String>
    private val baseNorms: Map<String, String>
    private val rules: Map<String, List<RulePart>>
    private val prefix: Pattern?
    private val suffix: Pattern?
    private val infix: Pattern?
    private val tokenMatch: Pattern?
    private val urlMatch: Pattern?

    init {
        require(json.getInt("format") == 1 && json.getString("pipeline") == "en_core_web_sm" && json.getString("pipelineVersion") == "3.8.0") { "Formato POS spaCy no compatible" }
        fun ints(name: String): IntArray = json.getJSONArray(name).let { array -> IntArray(array.length()) { array.getInt(it) } }
        val labelArray = json.getJSONArray("labels")
        labels = List(labelArray.length()) { labelArray.getString(it) }
        require(labels.size == 50 && labels.toSet().size == 50)
        seeds = ints("seeds")
        rows = ints("rows")
        require(seeds.contentEquals(intArrayOf(8, 9, 10, 11, 12, 13)))
        require(rows.contentEquals(intArrayOf(5000, 1000, 2500, 2500, 50, 50)))
        symbols = json.getJSONObject("symbols").let { object_ -> object_.keys().asSequence().associateWith { object_.getLong(it) } }
        lowercase = json.getJSONObject("lowercase").let { object_ -> object_.keys().asSequence().associate { it.toInt() to object_.getString(it) } }
        shapeRanges = json.getJSONArray("shapeRanges").let { array -> List(array.length()) { index ->
            val range = array.getJSONArray(index)
            ShapeRange(range.getInt(0), range.getInt(1), range.getString(2))
        } }
        require(shapeRanges.zipWithNext().all { (first, second) -> first.end < second.start })
        whitespace = ints("whitespace").toSet()
        normLookup = json.getJSONObject("lexemeNorm").let { object_ -> object_.keys().asSequence().associate { it.toULong().toLong() to object_.getString(it) } }
        baseNorms = json.getJSONObject("baseNorms").let { object_ -> object_.keys().asSequence().associateWith { object_.getString(it) } }
        rules = json.getJSONObject("rules").let { object_ -> object_.keys().asSequence().associateWith { word ->
            val array = object_.getJSONArray(word)
            List(array.length()) { index ->
                val part = array.getJSONObject(index)
                RulePart(part.getString("text"), if (part.isNull("norm")) null else part.getString("norm"))
            }.also { parts -> require(parts.joinToString("") { it.text } == word) }
        } }
        fun regex(name: String): Pattern? {
            if (json.isNull(name)) return null
            val pattern = json.getJSONObject(name)
            var flags = 0
            if (pattern.getBoolean("ignoreCase")) flags = flags or Pattern.CASE_INSENSITIVE or Pattern.UNICODE_CASE
            if (pattern.getBoolean("multiline")) flags = flags or Pattern.MULTILINE
            if (pattern.getBoolean("dotAll")) flags = flags or Pattern.DOTALL
            // Android always uses Unicode character classes and rejects the Java UCC flag.
            if (System.getProperty("java.vm.name").orEmpty().contains("Dalvik").not()) flags = flags or Pattern.UNICODE_CHARACTER_CLASS
            // Python's eight-digit code-point escape and strict end anchor use different
            // spellings in Java/Android regex; translate syntax while retaining the rule.
            val source = Regex("""\\U([0-9a-fA-F]{8})""").replace(pattern.getString("source")) { match ->
                "\\x{${match.groupValues[1].toInt(16).toString(16)}}"
            }.replace("\\Z", "\\z")
            return Pattern.compile(source, flags)
        }
        prefix = regex("prefix"); suffix = regex("suffix"); infix = regex("infix")
        tokenMatch = regex("tokenMatch"); urlMatch = regex("urlMatch")
    }

    fun pretokenized(words: List<String>): List<SpacyPosToken> {
        require(words.size <= MAX_TOKENS && words.all { it.isNotEmpty() && it.length <= 1024 }) { "Entrada POS demasiado larga o vacía" }
        return words.mapIndexed { index, word -> SpacyPosToken(word, if (index < words.lastIndex) " " else "") }
    }

    fun tokenize(text: String): List<SpacyPosToken> {
        require(text.length <= 100_000) { "Texto POS demasiado largo" }
        val output = mutableListOf<SpacyPosToken>()
        var start = 0
        while (start < text.length) {
            val space = isSpace(text.codePointAt(start))
            var end = start
            while (end < text.length && isSpace(text.codePointAt(end)) == space) end += Character.charCount(text.codePointAt(end))
            var runStart = start
            if (space && output.isNotEmpty() && text[start] == ' ') {
                output.last().whitespace = " "
                runStart++
            }
            if (runStart < end) {
                val run = text.substring(runStart, end)
                if (space) output += rules[run]?.let { ruleTokens(it, runStart) } ?: listOf(SpacyPosToken(run, start = runStart))
                else splitAffixes(run, runStart, output)
            }
            require(output.size <= MAX_TOKENS) { "Texto POS tiene demasiados tokens" }
            start = end
        }
        return applySpecialSpans(output, text)
    }

    private fun splitAffixes(original: String, origin: Int, output: MutableList<SpacyPosToken>) {
        var word = original
        var position = origin
        val prefixes = mutableListOf<SpacyPosToken>()
        val suffixes = mutableListOf<SpacyPosToken>()
        var lastLength = -1
        while (word.isNotEmpty() && word.length != lastLength) {
            if (matches(tokenMatch, word) || word in rules) break
            lastLength = word.length
            val pre = prefix?.matcher(word)?.let { if (it.find()) it.end() - it.start() else 0 } ?: 0
            if (pre > 0 && word.substring(pre) in rules) {
                prefixes += SpacyPosToken(word.substring(0, pre), start = position)
                word = word.substring(pre); position += pre
                break
            }
            val afterPrefix = word.substring(pre)
            val suf = suffix?.matcher(afterPrefix)?.let { if (it.find()) it.end() - it.start() else 0 } ?: 0
            if (suf > 0 && word.dropLast(suf) in rules) {
                suffixes += SpacyPosToken(word.takeLast(suf), start = position + word.length - suf)
                word = word.dropLast(suf)
                break
            }
            if (pre > 0) {
                prefixes += SpacyPosToken(word.substring(0, pre), start = position)
                word = word.substring(pre); position += pre
            }
            if (suf > 0 && suf <= word.length) {
                suffixes += SpacyPosToken(word.takeLast(suf), start = position + word.length - suf)
                word = word.dropLast(suf)
            }
        }
        output += prefixes
        if (word.isNotEmpty()) {
            val special = rules[word]
            when {
                special != null -> output += ruleTokens(special, position)
                matches(tokenMatch, word) || matches(urlMatch, word) -> output += SpacyPosToken(word, start = position)
                else -> {
                    val matcher = infix?.matcher(word)
                    var previous = 0
                    if (matcher != null) while (matcher.find()) {
                        if (matcher.start() == 0) continue
                        if (matcher.start() > previous) output += SpacyPosToken(word.substring(previous, matcher.start()), start = position + previous)
                        if (matcher.end() > matcher.start()) output += SpacyPosToken(word.substring(matcher.start(), matcher.end()), start = position + matcher.start())
                        previous = matcher.end()
                    }
                    if (previous < word.length) output += SpacyPosToken(word.substring(previous), start = position + previous)
                }
            }
        }
        output += suffixes.asReversed()
    }

    private fun ruleTokens(parts: List<RulePart>, origin: Int): List<SpacyPosToken> {
        var offset = origin
        return parts.map { part -> SpacyPosToken(part.text, normOverride = part.norm, start = offset).also { offset += part.text.length } }
    }

    private fun applySpecialSpans(tokens: List<SpacyPosToken>, text: String): List<SpacyPosToken> {
        val candidates = mutableListOf<SpecialSpan>()
        tokens.forEachIndexed { index, token ->
            var span = ""
            for (end in index until minOf(tokens.size, index + 32)) {
                if (end > index) span += tokens[end - 1].whitespace
                span += tokens[end].text
                val parts = rules[span]
                if (parts != null && end > index && text.substring(token.start, tokens[end].start + tokens[end].text.length) == span) candidates += SpecialSpan(index, end + 1, parts)
                if (span.length > 256) break
            }
        }
        val owned = BooleanArray(tokens.size)
        val selected = mutableMapOf<Int, SpecialSpan>()
        candidates.sortedWith(compareByDescending<SpecialSpan> { it.end - it.start }.thenBy { it.start }).forEach { span ->
            if ((span.start until span.end).none { owned[it] }) {
                (span.start until span.end).forEach { owned[it] = true }
                selected[span.start] = span
            }
        }
        val output = mutableListOf<SpacyPosToken>()
        var index = 0
        while (index < tokens.size) {
            val span = selected[index]
            if (span == null) { output += tokens[index++]; continue }
            val replacement = ruleTokens(span.parts, tokens[index].start)
            replacement.last().whitespace = tokens[span.end - 1].whitespace
            output += replacement
            index = span.end
        }
        return output
    }

    fun features(token: SpacyPosToken): LongArray {
        val codepoints = token.text.codePoints().toArray()
        require(codepoints.isNotEmpty()) { "Token POS vacío" }
        val norm = token.normOverride ?: normLookup[stringId(token.text)] ?: baseNorms[token.text] ?: lowercase(token.text)
        val prefix = String(Character.toChars(codepoints.first()))
        val suffix = String(codepoints.takeLast(3).toIntArray(), 0, minOf(3, codepoints.size))
        val shape = if (codepoints.size >= 100) "LONG" else buildString {
            var last = ""
            var repetitions = 0
            codepoints.forEach { codepoint ->
                val symbol = shape(codepoint) ?: String(Character.toChars(codepoint))
                repetitions = if (symbol == last) repetitions + 1 else 0
                last = symbol
                if (repetitions < 4) append(symbol)
            }
        }
        return longArrayOf(stringId(norm), stringId(prefix), stringId(suffix), stringId(shape), if (token.whitespace == " ") 1L else 0L, if (codepoints.all { isSpace(it) }) 1L else 0L)
    }

    fun keys(tokens: List<SpacyPosToken>): LongArray {
        require(tokens.size in 1..MAX_TOKENS)
        val output = LongArray(tokens.size * 6 * 4)
        tokens.forEachIndexed { tokenIndex, token ->
            features(token).forEachIndexed { featureIndex, feature ->
                val hashes = SpacyHash.hashEmbed(feature, seeds[featureIndex])
                repeat(4) { hashIndex -> output[tokenIndex * 24 + featureIndex * 4 + hashIndex] = (hashes[hashIndex].toLong() and 0xffff_ffffL) % rows[featureIndex] }
            }
        }
        return output
    }

    private fun stringId(value: String): Long = if (value.isEmpty()) 0L else symbols[value] ?: SpacyHash.stringHash(value)
    private fun lowercase(value: String): String {
        // Unicode lowercasing has one context-sensitive rule: Greek final sigma.
        // Leave only uppercase sigma for the platform's Unicode context algorithm;
        // every other scalar uses the exact Python Unicode mappings in the export.
        val mapped = buildString { value.codePoints().forEach { codepoint ->
            append(if (codepoint == 0x03a3) "Σ" else lowercase[codepoint] ?: String(Character.toChars(codepoint)))
        } }
        return if ('Σ' in mapped) mapped.lowercase(Locale.ROOT) else mapped
    }
    private fun isSpace(codepoint: Int): Boolean = codepoint in whitespace
    private fun shape(codepoint: Int): String? {
        var first = 0
        var last = shapeRanges.lastIndex
        while (first <= last) {
            val middle = (first + last) ushr 1
            val range = shapeRanges[middle]
            when { codepoint < range.start -> last = middle - 1; codepoint > range.end -> first = middle + 1; else -> return range.kind }
        }
        return null
    }
    private fun matches(pattern: Pattern?, value: String): Boolean = pattern?.matcher(value)?.lookingAt() == true
    private data class ShapeRange(val start: Int, val end: Int, val kind: String)
    private data class RulePart(val text: String, val norm: String?)
    private data class SpecialSpan(val start: Int, val end: Int, val parts: List<RulePart>)
    companion object { const val MAX_TOKENS = 4096 }
}

/** Public-domain MurmurHash64A plus Thinc 8.3.11's uint64 hash specialization. */
internal object SpacyHash {
    fun stringHash(value: String): Long {
        val bytes = value.toByteArray(Charsets.UTF_8)
        val multiplier = 0xc6a4a7935bd1e995UL.toLong()
        var hash = 1L xor (bytes.size.toLong() * multiplier)
        var offset = 0
        while (offset + 8 <= bytes.size) {
            var key = 0L
            repeat(8) { key = key or ((bytes[offset + it].toLong() and 255L) shl (it * 8)) }
            key *= multiplier; key = key xor (key ushr 47); key *= multiplier
            hash = hash xor key; hash *= multiplier
            offset += 8
        }
        if (offset < bytes.size) {
            for (index in offset until bytes.size) hash = hash xor ((bytes[index].toLong() and 255L) shl ((index - offset) * 8))
            hash *= multiplier
        }
        hash = hash xor (hash ushr 47); hash *= multiplier; hash = hash xor (hash ushr 47)
        return hash
    }

    fun hashEmbed(value: Long, seed: Int): IntArray {
        // Although named MurmurHash3_x86_128_uint64 in numpy_ops.pyx, the actual
        // CPU implementation uses the x64 constants and returns four uint32 halves.
        var first = value * 0x87c37b91114253d5UL.toLong()
        first = java.lang.Long.rotateLeft(first, 31)
        first *= 0x4cf5ad432745937fL
        first = first xor seed.toLong() xor 8L
        var second = seed.toLong() xor 8L
        first += second; second += first
        first = mix(first); second = mix(second)
        first += second; second += first
        return intArrayOf(first.toInt(), (first ushr 32).toInt(), second.toInt(), (second ushr 32).toInt())
    }

    private fun mix(value: Long): Long {
        var hash = value
        hash = hash xor (hash ushr 33); hash *= 0xff51afd7ed558ccdUL.toLong()
        hash = hash xor (hash ushr 33); hash *= 0xc4ceb9fe1a85ec53UL.toLong()
        return hash xor (hash ushr 33)
    }
}
