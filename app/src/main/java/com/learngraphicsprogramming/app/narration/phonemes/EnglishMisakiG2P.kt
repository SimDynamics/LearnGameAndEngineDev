package com.learngraphicsprogramming.app.narration.phonemes

import org.json.JSONObject
import java.io.File
import java.io.InputStream
import java.security.DigestInputStream
import java.security.MessageDigest
import java.text.Normalizer
import java.util.Locale
import java.util.concurrent.atomic.AtomicLong
import com.learngraphicsprogramming.app.narration.NarrationSpokenWords
import com.learngraphicsprogramming.app.narration.NarrationLanguage
import com.learngraphicsprogramming.app.narration.EnglishTechnicalSpeech

class EnglishMisakiException(message: String) : IllegalArgumentException(message)

/** The Penn Treebank tags used by Misaki's spaCy frontend; no alternate G2P is invoked. */
fun interface MisakiPartOfSpeechTagger {
    fun tag(words: List<String>): List<String?>
}

/**
 * Native port of Misaki's English Lexicon and right-to-left phonetic context, for Kokoro v1.
 * The original spaCy POS frontend resolves homographs; unknown words use the original BART
 * fallback. Both are separately installed resources with pinned integrity checks.
 * Missing resources, unsupported input and cancellation are reported explicitly.
 */
class EnglishMisakiG2P(
    private val openResource: (String) -> InputStream,
    private val partOfSpeech: MisakiPartOfSpeechTagger? = null,
    private val neuralDirectory: File? = null
) : AutoCloseable {
    constructor(directory: File) : this({ name ->
        val root = directory.canonicalFile
        val file = File(root, name).canonicalFile
        if (file.parentFile != root) throw EnglishMisakiException("Invalid Misaki dictionary path")
        file.inputStream()
    }, neuralDirectory = directory)

    private val american by lazy { loadLexicon(false) }
    private val british by lazy { loadLexicon(true) }
    private val nativePos = lazy {
        neuralDirectory?.takeIf { File(it, "spacy_pos.json").isFile && File(it, "spacy_pos.onnx").isFile }
            ?.let(::EnglishMisakiSpacyPos)
    }
    private val americanBart = lazy { loadBart(false) }
    private val britishBart = lazy { loadBart(true) }
    @Volatile private var closed = false
    private val cancellationGeneration = AtomicLong()

    /** Cancels a current sentence without touching sessions owned by a future sentence. */
    fun cancelCurrentRun() {
        cancellationGeneration.incrementAndGet()
        if (nativePos.isInitialized()) nativePos.value?.cancelCurrentRun()
        if (americanBart.isInitialized()) americanBart.value?.cancelCurrentRun()
        if (britishBart.isInitialized()) britishBart.value?.cancelCurrentRun()
    }

    private fun loadBart(british: Boolean): EnglishMisakiBartG2P? {
        val variant = if (british) "gb" else "us"
        return neuralDirectory?.takeIf { File(it, "en_${variant}_bart.json").isFile }
            ?.let { EnglishMisakiBartG2P(it, british) }
    }

    @Synchronized override fun close() {
        if (closed) return
        cancelCurrentRun()
        closed = true
        try { if (nativePos.isInitialized()) nativePos.value?.close() }
        finally {
            try { if (americanBart.isInitialized()) americanBart.value?.close() }
            finally { if (britishBart.isInitialized()) britishBart.value?.close() }
        }
    }

    @Synchronized
    fun phonemize(text: String, british: Boolean = false, isCurrent: () -> Boolean = { true }): String {
        return phonemizeImpl(text, british, isCurrent)
    }

    internal data class EnglishWordPhoneRange(val word: String, val sourceStart: Int, val sourceEndExclusive: Int,
        val start: Int, val endExclusive: Int)
    internal data class EnglishPhoneTrace(val phonemes: String, val words: List<EnglishWordPhoneRange>)
    internal data class PhonePiece(val sourceStart: Int, val sourceEndExclusive: Int, val start: Int, val endExclusive: Int)

    /** Uses the same contextual POS, retokenization, lexicon and original BART run as synthesis. */
    @Synchronized
    internal fun traceWords(text: String, british: Boolean = false, isCurrent: () -> Boolean = { true }): EnglishPhoneTrace? {
        // Offsets must refer to the exact input, not an untracked compatibility rewrite.
        if (text.isBlank() || text.length > 32_768 || Normalizer.normalize(text, Normalizer.Form.NFKC) != text) return null
        val pieces = mutableListOf<PhonePiece>()
        val phones = phonemizeImpl(text, british, isCurrent) { token, start, end ->
            pieces += PhonePiece(token.sourceStart, token.sourceEndExclusive, start, end)
        }
        return traceResolved(text, phones, pieces)
    }

    private fun phonemizeImpl(text: String, british: Boolean, isCurrent: () -> Boolean,
        onPiece: ((MisakiWord, Int, Int) -> Unit)? = null): String {
        if (closed) throw EnglishMisakiException("Misaki frontend is closed")
        val generation = cancellationGeneration.get()
        fun checkCurrent() {
            if (cancellationGeneration.get() != generation || Thread.currentThread().isInterrupted || !isCurrent()) {
                throw InterruptedException("Misaki sentence was cancelled")
            }
        }
        checkCurrent()
        try {
            if (text.isBlank()) return ""
            require(text.length <= 100_000) { "English text exceeds the phonemizer limit" }
            val lexicon = if (british) this.british else american
            checkCurrent()
            val pos = if (partOfSpeech == null) nativePos.value else null
            checkCurrent()
            if (neuralDirectory != null && partOfSpeech == null && pos == null) {
                throw EnglishMisakiException("Install Misaki's verified spaCy POS resources for contextual English pronunciation")
            }
            val tokens = tokenize(Normalizer.normalize(text, Normalizer.Form.NFKC), pos, onPiece != null) { checkCurrent() }
            checkCurrent()
            val tags = partOfSpeech?.tag(tokens.map { it.text })
            checkCurrent()
            if (tags != null && tags.size != tokens.size) {
                throw EnglishMisakiException("Misaki POS returned an invalid token count")
            }
            tokens.forEachIndexed { index, token ->
                checkCurrent()
                token.tag = tags?.get(index) ?: token.tag
            }
            val fallback: (String) -> String = { word ->
                checkCurrent()
                val model = if (british) britishBart.value else americanBart.value
                checkCurrent()
                model?.phonemizeWord(word) { checkCurrent(); true }
                    ?: throw EnglishMisakiException("Misaki has no pronunciation for '$word'; install its original BART resource")
            }
            if (pos != null) return MisakiEnglishPipeline.speakObserved(tokens, lexicon, fallback, { checkCurrent() }, onPiece)
            var context = PhoneticContext()
            for (token in tokens.asReversed()) {
                checkCurrent()
                token.phonemes = token.override ?: when {
                    token.text in PUNCTUATION -> token.text
                    token.text == "–" || token.text == "-" -> "—"
                    token.text in listOf("$", "£", "€") -> ""
                    token.text.all { it in "'‘’_" } -> ""
                    else -> lexicon.tryPronounce(token, context) ?: fallback(token.text)
                }
                context = nextContext(context, token)
            }
            val observed = if (onPiece == null) null else mutableListOf<Triple<MisakiWord, Int, Int>>()
            val output = buildString {
                tokens.forEach { token ->
                    checkCurrent()
                    val start = length
                    append(token.phonemes?.replace('ɾ', 'T')?.replace('ʔ', 't').orEmpty())
                    if (length > start) observed?.add(Triple(token, start, length))
                    append(token.whitespace)
                }
            }
            val prefix = output.indexOfFirst { !it.isWhitespace() }.coerceAtLeast(0)
            observed?.forEach { (word, start, end) -> onPiece?.invoke(word, start - prefix, end - prefix) }
            return output.trim()
        } catch (error: InterruptedException) { throw error }
        catch (error: Exception) { checkCurrent(); throw error }
    }

    private fun loadLexicon(british: Boolean): MisakiLexicon {
        val prefix = if (british) "gb" else "us"
        fun read(name: String): JSONObject {
            val metadata = EnglishMisakiResources.dictionaries.getValue(name)
            val digest = MessageDigest.getInstance("SHA-256")
            val bytes = openResource(name).use { input ->
                DigestInputStream(input, digest).buffered().use { stream ->
                    val data = ByteArray(metadata.bytes.toInt())
                    var count = 0
                    while (count < data.size) {
                        val read = stream.read(data, count, data.size - count)
                        if (read <= 0) break
                        count += read
                    }
                    if (count != data.size || stream.read() != -1) {
                        throw EnglishMisakiException("Misaki dictionary size does not match: $name")
                    }
                    data
                }
            }
            val actual = digest.digest().joinToString("") { "%02x".format(it.toInt() and 0xff) }
            if (actual != metadata.sha256) {
                throw EnglishMisakiException("Misaki dictionary integrity check failed: $name")
            }
            return JSONObject(bytes.toString(Charsets.UTF_8))
        }
        return MisakiLexicon(read("${prefix}_gold.json"), read("${prefix}_silver.json"), british)
    }

    private fun tokenize(text: String, pos: EnglishMisakiSpacyPos? = null, trackSource: Boolean = false,
        checkCurrent: () -> Unit = {}): MutableList<MisakiWord> {
        val tokenizer: ((String) -> List<SpacyTaggedToken>)? = pos?.let { engine ->
            { source -> engine.tokenizeAndTag(source) { checkCurrent(); true } }
        }
        return tokenizeUsing(text, tokenizer, trackSource, checkCurrent)
    }

    /** Shares source binding with the native POS frontend, including its space tokens. */
    internal fun tokenizeUsing(text: String, tokenizer: ((String) -> List<SpacyTaggedToken>)?,
        trackSource: Boolean, checkCurrent: () -> Unit = {}): MutableList<MisakiWord> {
        val prepared = StringBuilder()
        val sourceStarts = if (trackSource) mutableListOf<Int>() else null
        val sourceEnds = if (trackSource) mutableListOf<Int>() else null
        fun appendSource(start: Int, end: Int) {
            prepared.append(text.substring(start, end))
            if (sourceStarts != null && sourceEnds != null) for (index in start until end) { sourceStarts += index; sourceEnds += index + 1 }
        }
        val overrides = mutableMapOf<IntRange, String>()
        fun appendTechnical(piece: EnglishTechnicalSpeech.Replacement) {
            fun appendMapped(value: String) {
                prepared.append(value)
                if (sourceStarts != null && sourceEnds != null) repeat(value.length) {
                    sourceStarts += piece.start; sourceEnds += piece.endExclusive
                }
            }
            // Insert only missing separators. spaCy represents a second ASCII space
            // as a separate SPACE token; an artificial duplicate must never be sent
            // through BART as if it were an unknown word.
            if (prepared.isNotEmpty() && !prepared.last().isWhitespace()) appendMapped(" ")
            val start = prepared.length
            appendMapped(piece.spoken)
            if (piece.phonemes != null) overrides[start until prepared.length] = piece.phonemes
            if (text.getOrNull(piece.endExclusive)?.let { it.isLetterOrDigit() || it == '_' } == true)
                appendMapped(" ")
        }
        val links = LINK.findAll(text).toList()
        val replacements = EnglishTechnicalSpeech.replacements(text)
        val pieces = (links.map { it.range.first to it } + replacements.map { it.start to it }).sortedBy { it.first }
        var last = 0
        for ((_, piece) in pieces) {
            checkCurrent()
            when (piece) {
                is MatchResult -> {
                    appendSource(last, piece.range.first)
                    val start = prepared.length
                    prepared.append(piece.groupValues[1])
                    if (sourceStarts != null && sourceEnds != null) repeat(piece.groupValues[1].length) {
                        sourceStarts += piece.range.first; sourceEnds += piece.range.last + 1
                    }
                    val phonemes = piece.groupValues[2]
                    if (phonemes.startsWith('/') && phonemes.endsWith('/')) overrides[start until prepared.length] = phonemes.trim('/')
                    last = piece.range.last + 1
                }
                is EnglishTechnicalSpeech.Replacement -> {
                    appendSource(last, piece.start)
                    appendTechnical(piece)
                    last = piece.endExclusive
                }
            }
        }
        appendSource(last, text.length)
        val normalized = prepared.toString().replace('’', '\'').replace('‘', '\'')
        val result = mutableListOf<MisakiWord>()
        var quoteOpen = true
        var sourceOffset = 0
        val words = tokenizer?.invoke(normalized)?.map { it.text to (it.whitespace to it.tag) }
            ?: TOKEN.findAll(normalized).map { match -> match.value to (normalized.substring(match.range.last + 1).takeWhile { it.isWhitespace() } to null) }.toList()
        for ((raw, attributes) in words) {
            checkCurrent()
            val start = normalized.indexOf(raw, sourceOffset)
            if (start < sourceOffset) throw EnglishMisakiException("Misaki POS tokenizer did not align with the source")
            sourceOffset = start + raw.length
            if (raw.isBlank()) {
                // Preserve authored whitespace as separation/prosody, never phonemes.
                // This also covers genuine newlines and tabs emitted by native spaCy.
                result.lastOrNull()?.let { it.whitespace += raw + attributes.first }
                continue
            }
            val override = overrides.entries.firstOrNull { start in it.key }
            val forced = override?.let { if (start == it.key.first) it.value else "" }
            val word = if (raw == "\"") {
                (if (quoteOpen) "“" else "”").also { quoteOpen = !quoteOpen }
            } else raw
            result += MisakiWord(word, attributes.first, tag = attributes.second, override = forced,
                sourceStart = sourceStarts?.get(start) ?: -1, sourceEndExclusive = sourceEnds?.get(sourceOffset - 1) ?: -1)
        }
        result.forEachIndexed { index, token ->
            if (token.text in listOf("$", "£", "€") && index + 1 < result.size && NUMBER.matches(result[index + 1].text)) {
                result[index + 1].currency = token.text
            }
        }
        return result
    }

    private fun nextContext(previous: PhoneticContext, token: MisakiWord): PhoneticContext {
        val phoneme = token.phonemes.orEmpty().firstOrNull {
            it in MisakiStress.VOWELS || it in CONSONANTS || it.toString() in NON_QUOTE_PUNCTUATION
        }
        val futureVowel = when {
            phoneme == null -> previous.futureVowel
            phoneme.toString() in NON_QUOTE_PUNCTUATION -> null
            else -> phoneme in MisakiStress.VOWELS
        }
        return PhoneticContext(futureVowel, token.text == "to" || token.text == "To" ||
            (token.text == "TO" && token.tag in listOf("TO", "IN")))
    }

    companion object {
        /** Indivisible dictionary compounds stay whole instead of inventing internal phone times. */
        internal fun traceResolved(text: String, phonemes: String, pieces: List<PhonePiece>): EnglishPhoneTrace? {
            val lexical = NarrationSpokenWords.technicalAtoms(text, NarrationLanguage.AMERICAN_ENGLISH)
            if (lexical.isEmpty() || pieces.isEmpty()) return null
            val parent = IntArray(lexical.size) { it }
            fun root(index: Int): Int { var next = index; while (parent[next] != next) next = parent[next]; return next }
            val owners = pieces.map { piece ->
                if (piece.sourceStart < 0 || piece.sourceEndExclusive > text.length ||
                    piece.sourceStart >= piece.sourceEndExclusive || piece.start < 0 ||
                    piece.start >= piece.endExclusive || piece.endExclusive > phonemes.length) return null
                lexical.indices.filter { index -> lexical[index].start < piece.sourceEndExclusive && lexical[index].endExclusive > piece.sourceStart }
                    .also { indices -> indices.drop(1).forEach { parent[root(it)] = root(indices.first()) } }
            }
            val groups = lexical.indices.groupBy(::root).values.sortedBy { it.first() }
            val ranges = groups.map { indices ->
                val matching = pieces.indices.filter { piece -> owners[piece].any { it in indices } }
                if (matching.isEmpty()) return null
                val start = matching.minOf { pieces[it].start }
                val end = matching.maxOf { pieces[it].endExclusive }
                val sourceStart = lexical[indices.first()].start
                val sourceEnd = lexical[indices.last()].endExclusive
                EnglishWordPhoneRange(text.substring(sourceStart, sourceEnd).lowercase(Locale.ROOT), sourceStart, sourceEnd, start, end)
            }
            if (ranges.zipWithNext().any { (left, right) -> left.sourceEndExclusive > right.sourceStart || left.endExclusive > right.start }) return null
            return EnglishPhoneTrace(phonemes, ranges)
        }

        private val LINK = Regex("\\[([^]]+)]\\(([^)]*)\\)")
        internal val NUMBER = Regex("-?(?:[0-9]+(?:[,.][0-9]+)*|\\.[0-9]+)(?:st|nd|rd|th|s|'s|ed|'d|ing)?")
        private val TOKEN = Regex("(?:[A-Za-z]\\.){2,}|-?(?:[0-9]+(?:[,.][0-9]+)*|\\.[0-9]+)(?:st|nd|rd|th|s|'s|ed|'d|ing)?|[\\p{L}]+(?:'[\\p{L}]+)*(?:-[\\p{L}]+(?:'[\\p{L}]+)*)*|[^\\s]")
        private val PUNCTUATION = ";:,.!?—…()\"“”".map { it.toString() }.toSet()
        private val NON_QUOTE_PUNCTUATION = PUNCTUATION - setOf("\"", "“", "”")
        private const val CONSONANTS = "bdfhjklmnpstvwzðŋɡɹɾʃʒʤʧθ"
    }
}

internal data class MisakiWord(
    val text: String,
    var whitespace: String = "",
    var tag: String? = null,
    val override: String? = null,
    var currency: String? = null,
    var phonemes: String? = null,
    var stress: Double? = null,
    var preSpace: Boolean = false,
    var alias: String? = null,
    var isHead: Boolean = true,
    var numFlags: String = "",
    var sourceStart: Int = -1,
    var sourceEndExclusive: Int = -1
)

internal data class PhoneticContext(val futureVowel: Boolean? = null, val futureTo: Boolean = false)

object EnglishMisakiResources {
    const val revision = "fba1236595f2d2bf21d414ba6e57d25256afada3"
    const val source = "https://github.com/hexgrad/misaki"
    const val license = "Apache-2.0"
    data class Dictionary(val bytes: Long, val sha256: String) {
        fun url(name: String) = "https://raw.githubusercontent.com/hexgrad/misaki/$revision/misaki/data/$name"
    }
    val dictionaries = mapOf(
        "us_gold.json" to Dictionary(3_000_469L, "dc414872a49a28ae6c141463d502fd945f3b2fde040484fdc47d00cc4612686f"),
        "us_silver.json" to Dictionary(3_099_517L, "de8f67be911bb6c659187b4a65fd966b6a30e56350e0f790d763210b053ac475"),
        "gb_gold.json" to Dictionary(2_838_552L, "29e62f4b60261c88f7f3c2c7811ca3825978948090b72d2b27d565b729282f71"),
        "gb_silver.json" to Dictionary(3_663_898L, "48131e2d92ccc41655f4543e87e0f938e71463eb5a54be7f0693bb712ebb6bce")
    )
}

internal object MisakiStress {
    const val VOWELS = "AIOQWYaiuæɑɒɔəɛɜɪʊʌᵻ"

    fun apply(phonemes: String?, stress: Double?): String? {
        if (phonemes == null || stress == null) return phonemes
        return when {
            stress < -1 -> phonemes.replace("ˈ", "").replace("ˌ", "")
            stress == -1.0 || (stress in listOf(0.0, -0.5) && 'ˈ' in phonemes) ->
                phonemes.replace("ˌ", "").replace('ˈ', 'ˌ')
            stress in listOf(0.0, 0.5, 1.0) && phonemes.none { it in "ˈˌ" } -> restress(phonemes, 'ˌ')
            stress >= 1 && 'ˈ' !in phonemes && 'ˌ' in phonemes -> phonemes.replace('ˌ', 'ˈ')
            stress > 1 && phonemes.none { it in "ˈˌ" } -> restress(phonemes, 'ˈ')
            else -> phonemes
        }
    }

    private fun restress(phonemes: String, mark: Char): String {
        val vowel = phonemes.indexOfFirst { it in VOWELS }
        if (vowel < 0) return phonemes
        return phonemes.substring(0, vowel) + mark + phonemes.substring(vowel)
    }
}
