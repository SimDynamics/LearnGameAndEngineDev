package lgaed.narration.windows

import com.learngraphicsprogramming.app.narration.*
import com.learngraphicsprogramming.app.narration.phonemes.*
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.RandomAccessFile
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.file.Files
import java.nio.file.StandardCopyOption
import java.security.MessageDigest
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.Executors
import java.util.concurrent.Future
import java.util.concurrent.atomic.AtomicBoolean

private fun sha256(bytes: ByteArray): String = MessageDigest.getInstance("SHA-256").digest(bytes)
    .joinToString("") { "%02x".format(it.toInt() and 255) }
private fun sha256(file: File): String {
    val digest = MessageDigest.getInstance("SHA-256")
    file.inputStream().buffered().use { source ->
        val bytes = ByteArray(1024 * 1024)
        while (true) { val count = source.read(bytes); if (count < 0) break; digest.update(bytes,0,count) }
    }
    return digest.digest().joinToString("") { "%02x".format(it.toInt() and 255) }
}
private fun language(request: JSONObject): NarrationLanguage =
    NarrationLanguage.fromTag(request.getString("language")) ?: error("Unsupported narration language")
// Mirrors Android NarrationStorageBudget without evicting valid audio. This
// runs only for a cache miss; a reduced limit cannot prevent cached playback.
private fun requireAudioSpace(cache: File, request: JSONObject, nextBytes: Long) {
    val used = cache.listFiles().orEmpty().asSequence()
        .filter { it.isFile && (it.extension == "wav" || it.extension == "ogg") }
        .sumOf { it.length() }
    val reserve = 1_073_741_824L
    val free = cache.usableSpace
    val maximum = (if (Long.MAX_VALUE - used < free) Long.MAX_VALUE else used + free)
        .minus(reserve).coerceAtLeast(0)
    val limit = request.optLong("audioBudgetBytes", maximum).coerceAtLeast(0).coerceAtMost(maximum)
    check(free > reserve && nextBytes.coerceAtLeast(0) <= limit - used && limit > used) {
        "Narration audio storage limit reached; existing recordings have been preserved"
    }
}
private data class Segment(val source: String, val prepared: String, val spoken: List<ReadingTextRange>,
                           val displayed: List<ReadingTextRange>)
private class Request(val id: String) {
    val cancelled = AtomicBoolean(false)
    val calculatorReplied = AtomicBoolean(false)
    @Volatile var calculatorTask: Future<*>? = null
}

/** Historical export is explicit; the active catalog and reader preferences do not select it. */
private object HistoricalDoraPack {
    private const val UNIT_PREFIX = "programming/cpp_fundamentals/cpp_fundamentals__intro_programming/"
    private val lessons = listOf(
        "cpp_fundamentals__intro_programming__what_is_programming",
        "cpp_fundamentals__intro_programming__first_contact_cpp",
        "cpp_fundamentals__intro_programming__program_structure"
    )

    fun manifest(assets: File): JSONObject {
        val entries = JSONArray()
        val seen = mutableSetOf<String>()
        for (lessonId in lessons) {
            val name = lessonId.substringAfterLast("__")
            val path = "content/es/programming/cpp_fundamentals/intro_programming/lesson_$name.json"
            val lesson = JSONObject(File(assets,path).readText(Charsets.UTF_8))
            check(lesson.getString("id") == lessonId) { "Incorrect historical voice-pack lesson identity: $path" }
            val pages = lesson.getJSONArray("pages")
            check(pages.length() > 0) { "Historical voice-pack lesson has no pages: $path" }
            for (page in 0 until pages.length()) {
                // Match FirstUnitNarrationPack: malformed page data is an export error.
                pages.getJSONObject(page)
                val texts = LessonNarrationText.page(lesson,page,NarrationLanguage.SPANISH) { type ->
                    when (type) {
                        "order_code", "drag_fill_code" -> WindowsNarrationInstructions.resolve(type,"es")
                        else -> ""
                    }
                }
                for (sentence in NarrationSegments.sentences(texts)) {
                    val hash = KokoroNarrationCache.key("es","ef_dora",sentence)
                    if (seen.add(hash)) entries.put(JSONObject()
                        .put("hash",hash).put("language","es").put("voice","ef_dora")
                        .put("lesson",lessonId).put("location","$UNIT_PREFIX$lessonId/${page + 1}"))
                }
            }
        }
        check(entries.length() > 0) { "Historical Spanish/Dora voice pack has no narration" }
        return JSONObject().put("format",1).put("course","cpp_fundamentals")
            .put("unit","intro_programming").put("lessons",JSONArray(lessons))
            .put("uniqueSentences",entries.length()).put("entries",entries)
    }
}

private class Backend(private val resources: File, private val englishResources: File,
                      private val assets: File, private val originalWavs: File?, private val ffmpeg: String?,
                      private val profile: String) {
    private val plans = object : LinkedHashMap<String,Segment>() {
        override fun removeEldestEntry(eldest: MutableMap.MutableEntry<String,Segment>?): Boolean = size > 4096
    }
    private var spanish: Map<String,String>? = null
    private var french: FrenchFluidG2P? = null
    @Volatile private var english: EnglishMisakiG2P? = null
    @Volatile private var engine: KokoroOnnxEngine? = null
    // Two lazy CPU profiles, serialized on the existing inference lane. A
    // foreground/background transition must not repeatedly reload the model.
    private val engineProfiles = LinkedHashMap<Int,KokoroOnnxEngine>(2,0.75f,true)
    @Volatile private var duration: KokoroDurationModel? = null
    private var durationDirectory: File? = null
    @Volatile var active: Request? = null

    private fun planKey(language: NarrationLanguage, source: String, prepared: String) =
        sha256("${language.tag}\u0000$source\u0000$prepared".toByteArray(Charsets.UTF_8))
    private fun strippedRanges(segment: Segment): List<ReadingTextRange> {
        val text = segment.prepared
        val boundaries = IntArray(text.length + 1)
        val annotations = ManualEnglishSpeech.spans(text).associateBy { it.start }
        val visible = StringBuilder()
        var cursor = 0
        while (cursor < text.length) {
            boundaries[cursor] = visible.length
            val span = annotations[cursor]
            if (span != null) {
                for (index in span.start until span.endExclusive) boundaries[index] = visible.length
                visible.append(span.legacyText)
                cursor = span.endExclusive
            } else {
                if (text[cursor] != ManualEnglishSpeech.PREPARED) visible.append(text[cursor])
                cursor++
            }
            boundaries[cursor] = visible.length
        }
        check(visible.toString() == ManualEnglishSpeech.strip(text)) { "Source annotation display contract changed" }
        return segment.spoken.map { ReadingTextRange(boundaries[it.start],boundaries[it.endExclusive]) }
    }
    private fun readingMode(request: JSONObject): String {
        val mode = request.optString("mathMode","auto")
        val context = request.optString("readingContext","auto")
        require(mode in setOf("auto","text","inline","block")) { "Invalid mathematical reading mode" }
        require(context in setOf("auto","cpp","cpp_previous_parentheses")) { "Invalid reading context" }
        // The previous policy is a private bookmark-compatibility request,
        // never an authored mathMode or a curriculum/background context.
        return if (mode == "auto" && context in setOf("cpp","cpp_previous_parentheses")) context else mode
    }
    private fun planned(source: String, language: NarrationLanguage, mode: String,
                        semantics: WindowsTextSemantics.Field? = null, originalSemantics: Boolean = false): List<Pair<IntRange,Segment>> {
        require(source.length in 1..32768 && '\u0000' !in source) { "Narration text must contain 1–32768 original UTF-16 characters" }
        require(mode in setOf("auto","text","inline","block","cpp","cpp_previous_parentheses")) { "Invalid mathematical reading mode" }
        // The Android feedback presentation omits only its leading native
        // verdict mark. Keep that exact rule and map all speech back onto the
        // original visible text, including the mark with the first real word.
        val iconPrefix = IncorrectFeedbackNarration.verdictIconPrefix(source)
        val speechSource = IncorrectFeedbackNarration.visibleSpeech(source)
        // Explicit ranges always own the untouched field. A native feedback
        // icon is outside authored fields and must not shift its declaration.
        require(semantics == null || semantics.source == source) { "Semantic source differs from the visible field" }
        val speechSemantics = semantics?.let { field -> WindowsTextSemantics.Field(speechSource,
            field.spans.filter { it.end > iconPrefix }.map { span ->
                val start = (span.start - iconPrefix).coerceAtLeast(0);val end = span.end - iconPrefix
                span.copy(start=start,end=end,text=speechSource.substring(start,end))
            }) }
        val projection = if (speechSemantics != null) {
            if (originalSemantics) WindowsLegacyTextSemantics.projection(speechSemantics,language)
            else WindowsTextSemantics.projection(speechSemantics,language)
        }
            else NarrationSourceProjection.forLanguage(speechSource,mode,language)
            ?: error("The existing frontend cannot safely project this text")
        val prepared = NarrationSegments.sentences(listOf(projection.text))
        val mappings = if (semantics == null) NarrationWordMappings.forBlock(0,speechSource,prepared.withIndex().associate { it.index to it.value },language)
            else emptyMap()
        fun original(range: ReadingTextRange) = ReadingTextRange(
            if (range.start == 0) 0 else range.start + iconPrefix, range.endExclusive + iconPrefix)
        var cursor = 0
        return prepared.mapIndexed { index, sentence ->
            val body = sentence.removePrefix(ManualEnglishSpeech.PREPARED.toString())
            val start = projection.text.indexOf(body,cursor)
            check(start >= cursor) { "Sentence split no longer matches original normalization" }
            cursor = start + body.length
            val owner = original(NarrationSourceProjection.owner(projection.owners.subList(start,cursor)))
            val mapping = if (semantics == null) mappings[index]?.let { value -> value.copy(displayed = value.displayed.map(::original)) }
                else {
                    val prefix = if (sentence.startsWith(ManualEnglishSpeech.PREPARED)) 1 else 0
                    val words = NarrationSpokenWords.technicalAtoms(sentence,language)
                    NarrationWordMapping(0,words.map { ReadingTextRange(it.start,it.endExclusive) },words.map {
                        original(NarrationSourceProjection.owner(projection.owners.subList(start + it.start - prefix,start + it.endExclusive - prefix)))
                    })
                }
            val first = mapping?.displayed?.minOfOrNull { it.start } ?: owner.start
            val last = mapping?.displayed?.maxOfOrNull { it.endExclusive } ?: owner.endExclusive
            require(first in 0 until source.length && last in first + 1..source.length) { "Sentence has no original visible owner" }
            val segment = Segment(source.substring(first,last),sentence,mapping?.spoken.orEmpty(),
                mapping?.displayed?.map { ReadingTextRange(it.start - first,it.endExclusive - first) }.orEmpty())
            if (!originalSemantics) plans[planKey(language,segment.source,segment.prepared)] = segment
            (first until last) to segment
        }
    }
    fun plan(request: JSONObject): JSONObject {
        require(!request.has("semanticProjectionPolicy") || request.opt("semanticProjectionPolicy") == "original" && request.has("textSemantics")) {
            "Invalid private semantic projection policy"
        }
        require(!request.has("previousOutputDataPolicy") || request.opt("previousOutputDataPolicy") is Boolean) {
            "Invalid previous output-data policy"
        }
        require(!request.has("outputDataPolicy") || request.opt("outputDataPolicy") is String) {
            "Invalid output-data policy"
        }
        require(!request.has("outputDataPolicy") || !request.has("previousOutputDataPolicy")) {
            "Conflicting output-data policies"
        }
        val policy = when (request.optString("outputDataPolicy", "")) {
            "" -> if (request.optBoolean("previousOutputDataPolicy",false))
                WindowsNarrationSpeechRules.OutputDataPolicy.ORIGINAL else WindowsNarrationSpeechRules.OutputDataPolicy.CURRENT
            "current" -> WindowsNarrationSpeechRules.OutputDataPolicy.CURRENT
            "b42" -> WindowsNarrationSpeechRules.OutputDataPolicy.B42
            "original" -> WindowsNarrationSpeechRules.OutputDataPolicy.ORIGINAL
            else -> error("Invalid output-data policy")
        }
        require(!request.has("outputDataPolicy") || request.getString("outputDataPolicy").isNotEmpty()) {
            "Invalid output-data policy"
        }
        val items = WindowsNarrationSpeechRules.withOutputDataPolicy(policy) {
            val source = request.getString("text")
            val explicit = if (request.has("textSemantics"))
                WindowsTextSemantics.parse(source,request.getJSONObject("textSemantics")) else null
            planned(source,language(request),readingMode(request),explicit,request.has("semanticProjectionPolicy"))
        }
        return JSONObject().put("segments",JSONArray(items.map { (range,segment) ->
            JSONObject().put("text",segment.source).put("preparedText",segment.prepared)
                .put("spokenText",ManualEnglishSpeech.strip(segment.prepared))
                .put("spokenRanges",JSONArray(strippedRanges(segment).map { JSONObject().put("start",it.start).put("end",it.endExclusive) }))
                .put("displayedRanges",JSONArray(segment.displayed.map { JSONObject().put("start",it.start).put("end",it.endExclusive) }))
                .put("sourceStart",range.first).put("sourceEnd",range.last + 1)
                .put("key",KokoroNarrationCache.key(language(request).tag,
                    request.optString("voice",language(request).defaultVoice),segment.prepared))
        }))
    }
    fun identity(request: JSONObject): JSONObject {
        val texts = request.getJSONArray("texts")
        require(texts.length() in 1..100000)
        val engine = request.optString("engine","KOKORO")
        require(engine in setOf("KOKORO","ANDROID")) { "Unsupported original narration identity provider" }
        return JSONObject().put("key",NarrationResumePolicy.key(request.getString("page"),language(request).tag,
            engine,request.getString("voice"),(0 until texts.length()).map { texts.getString(it) }))
    }
    fun curriculumPlan(request: JSONObject): JSONObject {
        val lang = language(request)
        val uiLanguage = request.optString("uiLanguage",lang.tag)
        val context = request.optString("readingContext","auto")
        require(context in setOf("auto","cpp")) { "Invalid reading context" }
        // This parsed request-local projection changes no authored or stored
        // metadata. Only public block type/language/mode select C++ reading.
        val lesson = JSONObject(request.getString("lessonJson"))
        run {
            val types = setOf("multiple_choice","fill_code","inline_fill_code","output_prediction","order_code","drag_fill_code")
            val pages = lesson.optJSONArray("pages")
            for (page in 0 until (pages?.length() ?: 0)) {
                val blocks = pages?.optJSONObject(page)?.optJSONArray("blocks") ?: continue
                for (index in 0 until blocks.length()) {
                    val block = blocks.optJSONObject(index) ?: continue
                    val mode = block.optString("mathMode","auto")
                    require(mode in setOf("auto","text","inline","block")) { "Invalid mathematical reading mode" }
                    if (context == "cpp" && block.optString("type") in types && block.optString("language") in setOf("","cpp","c++") &&
                            mode == "auto") block.put("mathMode","cpp")
                }
            }
        }
        val texts = WindowsTextSemantics.withCurriculum(lesson) {
            val script = WindowsNarrationSpeechRules.withWholeReadingRegions {
                LessonNarrationScript.page(lesson,request.getInt("page"),
                    lang,forKokoro=MixedSpanishG2P.productionEnabled) { WindowsNarrationInstructions.resolve(it,uiLanguage) }
            }
            script.texts + WindowsTextSemantics.compilerGuides(lesson,request.getInt("page"),uiLanguage).flatMap { field ->
                planned(field.source,lang,"auto",field).map { it.second.prepared }
            }
        }
        val segments = NarrationSegments.sentences(texts).map { prepared ->
            // Background preparation uses the canonical already-normalized script.
            // It writes audio only. The visible reader retains its own exact
            // source projection and derives timings against that unchanged audio.
            val segment = Segment(ManualEnglishSpeech.strip(prepared),prepared,emptyList(),emptyList())
            plans[planKey(lang,segment.source,prepared)] = segment
            JSONObject().put("text",segment.source).put("preparedText",prepared)
                .put("key",KokoroNarrationCache.key(lang.tag,request.getString("voice"),prepared))
        }
        return JSONObject().put("segments",JSONArray(segments))
    }
    fun firstUnitManifest(): JSONObject = JSONObject().put("manifest",HistoricalDoraPack.manifest(assets))
    fun cancel(request: Request) {
        request.cancelled.set(true)
        if (active === request) { engine?.cancelCurrentRun(); duration?.cancelCurrentRun(); english?.cancelCurrentRun() }
    }
    private fun current(request: Request) {
        if (request.cancelled.get() || Thread.currentThread().isInterrupted) throw InterruptedException("Narration request cancelled")
    }
    private fun sampleCount(file: File): Long? = runCatching {
        if (!file.isFile || file.length() !in 46L..(44 + NarrationWordTimingBuilder.MAX_SAMPLES * 2)) return@runCatching null
        val raw = ByteArray(44); RandomAccessFile(file,"r").use { it.readFully(raw) }
        val header = ByteBuffer.wrap(raw).order(ByteOrder.LITTLE_ENDIAN)
        fun mark(index: Int) = String(raw,index,4,Charsets.US_ASCII)
        if (mark(0) != "RIFF" || mark(8) != "WAVE" || mark(12) != "fmt " || mark(36) != "data" ||
            header.getInt(16) != 16 || header.getShort(20).toInt() != 1 || header.getShort(22).toInt() != 1 ||
            header.getInt(24) != 24000 || header.getInt(28) != 48000 || header.getShort(32).toInt() != 2 ||
            header.getShort(34).toInt() != 16 || header.getInt(4).toLong() + 8 != file.length() ||
            header.getInt(40).toLong() + 44 != file.length() || header.getInt(40) % 2 != 0) return@runCatching null
        header.getInt(40).toLong() / 2
    }.getOrNull()
    private fun validTimings(rows: JSONArray, sourceLength: Int, samples: Long): Boolean = runCatching {
        if (rows.length() !in 1..4096) return@runCatching false
        var previousStart = -1
        var previousTime = -1.0
        repeat(rows.length()) { index ->
            val row = rows.getJSONObject(index)
            val start = row.getInt("start"); val end = row.getInt("end")
            val from = row.getDouble("startMs"); val to = row.getDouble("endMs")
            if (start !in 0 until sourceLength || end !in start + 1..sourceLength || start < previousStart ||
                !from.isFinite() || !to.isFinite() || from < 0 || to <= from || to > samples / 24.0 || from < previousTime)
                return@runCatching false
            previousStart = start; previousTime = from
        }
        true
    }.getOrDefault(false)
    private fun directory(request: JSONObject): File {
        val default = File(System.getenv("LOCALAPPDATA") ?: error("LOCALAPPDATA is required"),"LearnGameAndEngineDev/$profile/cache/kokoro")
        val result = File(request.optString("outputDirectory",default.path)).canonicalFile
        for (protected in listOf(resources,englishResources,assets).map { it.canonicalFile })
            require(generateSequence(result) { it.parentFile }.none { it == protected }) { "Narration cache cannot overwrite installed resources" }
        check(result.isDirectory || result.mkdirs()) { "Cannot create private narration cache" }
        return result
    }
    private fun reuse(target: File, key: String, job: Request): String? {
        if (sampleCount(target) != null) return "cache"
        check(!target.exists()) { "Existing narration cache failed validation; it was left untouched" }
        val original = originalWavs?.resolve("$key.wav")
        val partial = File(target.parentFile,".${target.name}-${job.id.hashCode()}.part")
        try {
            if (original != null && sampleCount(original) != null) {
                original.inputStream().use { source -> partial.outputStream().use { source.copyTo(it) } }
                current(job); Files.move(partial.toPath(),target.toPath()); return "existing-recording"
            }
            val packaged = File(assets,"narration/prerendered/$key.ogg")
            if (!packaged.isFile || ffmpeg.isNullOrBlank()) return null
            val command = listOf(ffmpeg,"-nostdin","-hide_banner","-loglevel","error","-i",packaged.path,
                "-map_metadata","-1","-ac","1","-ar","24000","-c:a","pcm_s16le","-f","wav","-fflags","+bitexact","-flags:a","+bitexact",partial.path)
            val process = ProcessBuilder(command).redirectError(ProcessBuilder.Redirect.INHERIT).start()
            while (!process.waitFor(50,java.util.concurrent.TimeUnit.MILLISECONDS)) {
                if (job.cancelled.get()) { process.destroyForcibly(); current(job) }
            }
            check(process.exitValue() == 0 && sampleCount(partial) != null) { "Cannot decode the compatible bundled Opus recording" }
            current(job); Files.move(partial.toPath(),target.toPath()); return "packaged-recording"
        } finally { partial.delete() }
    }
    private data class Phones(val text: String, val sources: List<NarrationSourcePhones>)
    private fun phones(text: String, language: NarrationLanguage, job: Request): Phones {
        current(job)
        return when (language) {
            NarrationLanguage.SPANISH -> {
                val lexicon = spanish ?: run {
                    val artifact = KokoroModelManifest.resources(language).single()
                    val path = File(resources,artifact.fileName)
                    check(KokoroModelManifest.verify(path,artifact)) { "Spanish dictionary failed SHA-256" }
                    SpanishFluidG2P.loadLexicon(path).also { spanish = it }
                }
                val actual = MixedSpanishG2P.phonemizePrepared(text,lexicon)
                val trace = MixedSpanishG2P.traceWordsPrepared(text,lexicon)
                val lexical = NarrationSpokenWords.lexemes(text)
                val owners = if (trace != null && trace.phonemes == actual && lexical.size == trace.words.size)
                    lexical.zip(trace.words).map { (source,phone) -> NarrationSourcePhones(source.start,source.endExclusive,phone.start,phone.endExclusive) }
                    else emptyList()
                Phones(actual,owners)
            }
            NarrationLanguage.FRENCH -> {
                val frontend = french ?: FrenchFluidG2P(resources,File(resources,"tokenizer.json")).also { french = it }
                val actual = MixedFrenchG2P.phonemizePrepared(text,frontend)
                val trace = MixedFrenchG2P.traceWordsPrepared(text,frontend)
                Phones(actual,if (trace?.phonemes == actual) trace.words.map {
                    NarrationSourcePhones(it.sourceStart,it.sourceEndExclusive,it.start,it.endExclusive) } else emptyList())
            }
            else -> {
                val frontend = english ?: EnglishMisakiG2P({ name ->
                    val file = File(resources,name).canonicalFile
                    require(file.parentFile == resources.canonicalFile); file.inputStream()
                },neuralDirectory = englishResources).also { english = it }
                val british = language == NarrationLanguage.BRITISH_ENGLISH
                val actual = frontend.phonemize(text,british) { !job.cancelled.get() }
                val trace = frontend.traceWords(text,british) { !job.cancelled.get() }
                Phones(actual,if (trace?.phonemes == actual) trace.words.map {
                    NarrationSourcePhones(it.sourceStart,it.sourceEndExclusive,it.start,it.endExclusive) } else emptyList())
            }
        }
    }
    fun prepare(request: JSONObject, job: Request): JSONObject {
        active = job
        try {
            val language = language(request)
            val source = request.getString("text")
            val mode = readingMode(request)
            val audioOnly = request.optBoolean("audioOnly",false)
            val selected = if (request.has("preparedText")) {
                plans[planKey(language,source,request.getString("preparedText"))]
                    ?: error("Prepared text has no current original-source plan; request plan first")
            } else planned(source,language,mode).singleOrNull()?.second
                ?: error("Request plan first when a block contains several sentences")
            val voice = request.optString("voice",language.defaultVoice)
            require(KokoroVoiceCatalog.find(voice)?.language == language) { "Voice does not belong to the selected language" }
            val key = KokoroNarrationCache.key(language.tag,voice,selected.prepared)
            val cache = directory(request)
            val audio = File(cache,"$key.wav")
            var origin = reuse(audio,key,job)
            val cacheHit = origin != null
            current(job)
            if (!cacheHit) requireAudioSpace(cache, request, 4L * 1024 * 1024)
            val sidecar = File(cache,"$key-${sha256(source.toByteArray(Charsets.UTF_8))}.timings.json")
            val cachedSamples = if (cacheHit) sampleCount(audio) else null
            val cachedAudioHash = if (cacheHit) sha256(audio) else null
            val cachedTimings = if (cachedSamples != null && cachedAudioHash != null) runCatching {
                if (!sidecar.isFile) return@runCatching null
                val old = JSONObject(sidecar.readText(Charsets.UTF_8))
                old.getJSONArray("timings").takeIf { old.optString("audioSha256") == cachedAudioHash
                    && old.optLong("sampleCount") == cachedSamples && validTimings(it,source.length,cachedSamples) }
            }.getOrNull() else null
            // Compatible recordings remain playable without installed synthesis
            // resources. Missing optional alignment never invalidates that audio.
            // Valid audio plus exact existing timings need no G2P or neural work.
            val phone = if ((audioOnly && cacheHit) || cachedTimings != null) null else if (cacheHit) runCatching { phones(selected.prepared,language,job) }
                .onFailure { if (it !is InterruptedException) System.err.println("Cached Kokoro audio needs no new synthesis: ${it.message}") }.getOrNull()
                else phones(selected.prepared,language,job)
            val chunks = phone?.let { KokoroPhonemeChunks.split(it.text) }.orEmpty()
            if (!cacheHit) {
                val threads = request.optInt("threads",Runtime.getRuntime().availableProcessors()).coerceIn(1,Runtime.getRuntime().availableProcessors())
                engine = engineProfiles.getOrPut(threads) {
                    if(engineProfiles.size >= 2) {
                        val oldest=engineProfiles.entries.first();oldest.value.close();engineProfiles.remove(oldest.key)
                    }
                    KokoroOnnxEngine(resources,preferAccelerator = false,cpuThreads = threads)
                }
                val generated = chunks.map { chunk -> current(job); engine!!.synthesizePhonemes(chunk,voice,1f) { job.cancelled.get() } }
                current(job)
                requireAudioSpace(cache, request, 44L + generated.sumOf { it.size.toLong() } * 2L)
                val partial = File(cache,".$key-${job.id.hashCode()}.part")
                try {
                    NarrationWavWriter.writeChunks(partial,generated)
                    check(sampleCount(partial) != null) { "Generated Kokoro audio failed PCM validation" }
                    current(job); Files.move(partial.toPath(),audio.toPath())
                } finally { partial.delete() }
                origin = "synthesized"
            }
            val samples = requireNotNull(cachedSamples ?: sampleCount(audio)) { "Narration audio has no valid sample count" }
            val audioHash = cachedAudioHash ?: sha256(audio)
            val timings = if(audioOnly) JSONArray() else cachedTimings ?: runCatching {
                if (phone == null || phone.sources.isEmpty() || selected.spoken.isEmpty()) return@runCatching JSONArray()
                val derived = File(cache,"duration-model")
                if (duration == null || durationDirectory != derived) {
                    duration?.close(); duration = KokoroDurationModel(resources,derived); durationDirectory = derived
                }
                val durations = chunks.map { current(job); duration!!.durations(it,voice) { job.cancelled.get() } }
                val alignment = NarrationWordTimingBuilder.buildSourceChunked(selected.prepared,audioHash,samples,
                    phone.text,phone.sources,chunks,durations,language) ?: return@runCatching JSONArray()
                if (alignment.words.size != selected.spoken.size || alignment.words.indices.any {
                    alignment.words[it].start != selected.spoken[it].start || alignment.words[it].endExclusive != selected.spoken[it].endExclusive
                }) return@runCatching JSONArray()
                val result = JSONArray()
                alignment.words.forEachIndexed { index,word ->
                    val shown = selected.displayed[index]
                    result.put(JSONObject().put("start",shown.start).put("end",shown.endExclusive)
                        .put("startMs",word.startSample / 24.0).put("endMs",word.endSample / 24.0))
                }
                current(job)
                val data = JSONObject().put("audioSha256",audioHash).put("sampleCount",samples).put("timings",result)
                val partial = File(cache,".${sidecar.name}-${job.id.hashCode()}.part")
                try {
                    partial.writeText(data.toString(),Charsets.UTF_8); current(job)
                    Files.move(partial.toPath(),sidecar.toPath(),StandardCopyOption.REPLACE_EXISTING)
                } finally { partial.delete() }
                result
            }.onFailure { if (it !is InterruptedException) System.err.println("Optional exact word alignment unavailable: ${it.message}") }.getOrElse { JSONArray() }
            current(job)
            return JSONObject().put("key",key).put("wav",audio.canonicalPath).put("sampleRate",24000)
                .put("sampleCount",samples).put("durationMs",samples / 24.0).put("sourceText",source)
                .put("spokenText",ManualEnglishSpeech.strip(selected.prepared)).put("timings",timings)
                .put("alignmentAvailable",timings.length() > 0).put("cacheHit",cacheHit).put("audioOrigin",origin)
                .put("provider",if (engine?.usesXnnpack == true) "Kokoro/ONNX Runtime XNNPACK Windows" else "Kokoro/ONNX Runtime CPU Windows")
        } finally { active = null }
    }
    fun close() { engineProfiles.values.forEach { it.close() };engineProfiles.clear();engine=null;duration?.close();english?.close() }
}

fun main(arguments: Array<String>) {
    val options = arguments.toList().chunked(2).associate { require(it.size == 2); it[0] to it[1] }
    val backend = Backend(File(options.getValue("--resources")),File(options.getValue("--english-resources")),
        File(options.getValue("--assets")),options["--original-wavs"]?.let(::File),options["--ffmpeg"],
        (options["--profile"] ?: "Codex").also { require(it in setOf("Codex","Work")) })
    val requests = ConcurrentHashMap<String,Request>()
    val writer = System.out.bufferedWriter(Charsets.UTF_8)
    fun respond(id: String, command: String, payload: JSONObject) = synchronized(writer) {
        writer.write(payload.put("id",id).put("command",command).toString()); writer.newLine(); writer.flush()
    }
    fun cancelRequest(job: Request) {
        backend.cancel(job)
        // Only the calculator uses Java interruption, exactly as its Android
        // activity. Kokoro keeps its existing cooperative RunOptions cancellation.
        job.calculatorTask?.cancel(true)
    }
    val executor = Executors.newSingleThreadExecutor { work -> Thread(work,"windows-kokoro-worker") }
    // Structured editing must not queue behind a neural run. These lanes own
    // only the canonical math model/evaluator; inference stays on its original
    // executor with the existing cancellation and CPU policy.
    val editorExecutor = Executors.newSingleThreadExecutor { work -> Thread(work,"windows-math-editor") }
    val calculatorExecutor = Executors.newSingleThreadExecutor { work -> Thread(work,"windows-math-calculator") }
    System.`in`.bufferedReader(Charsets.UTF_8).use { input ->
        while (true) {
            val line = input.readLine() ?: break
            var replyId = "invalid"
            var replyCommand = "invalid"
            try {
                require(line.length <= 2_000_000) { "Narration request is too large" }
                val request = JSONObject(line)
                val id = request.get("id").toString(); require(id.length in 1..128)
                val command = request.getString("command")
                replyId = id; replyCommand = command
                require(command == "plan" || (!request.has("previousOutputDataPolicy") && !request.has("outputDataPolicy") && !request.has("textSemantics") && !request.has("semanticProjectionPolicy"))) {
                    "Previous output-data policy is restricted to bookmark planning"
                }
                if (command == "cancel") {
                    val target = request.optString("targetId",id)
                    requests[target]?.let(::cancelRequest)
                    respond(id,command,JSONObject().put("cancelled",true)); continue
                }
                if (command == "shutdown") {
                    requests.values.forEach(::cancelRequest)
                    respond(id,command,JSONObject().put("shutdown",true)); break
                }
                val job = Request(id)
                require(requests.putIfAbsent(id,job) == null) { "Duplicate active narration request id" }
                val lane=when(command){"math_editor","math_keyboard"->editorExecutor;"math_calculate"->calculatorExecutor;else->executor}
                fun reply(payload: JSONObject) {
                    if(command!="math_calculate"||job.calculatorReplied.compareAndSet(false,true))respond(id,command,payload)
                }
                val work=Runnable {
                    try {
                        if (job.cancelled.get()) throw InterruptedException("Narration request cancelled")
                        val result = when (command) {
                            "plan" -> backend.plan(request)
                            "curriculum_plan" -> backend.curriculumPlan(request)
                            "first_unit_manifest" -> backend.firstUnitManifest()
                            "identity" -> backend.identity(request)
                            "prepare" -> backend.prepare(request,job)
                            "describe_math" -> WindowsMathBridge.describe(request)
                            "validate_math" -> WindowsMathBridge.validate(request,line)
                            "math_editor" -> WindowsMathBridge.editor(request)
                            "math_keyboard" -> WindowsMathBridge.keyboard(request)
                            "math_calculate" -> WindowsMathBridge.calculate(request)
                            else -> error("Unknown narration command: $command")
                        }
                        if(command=="math_calculate"&&job.cancelled.get())throw InterruptedException("Mathematical calculation cancelled")
                        reply(result)
                    } catch (error: Throwable) {
                        reply(JSONObject().put("error",error.message ?: error.javaClass.simpleName)
                            .put("cancelled",job.cancelled.get() || error is InterruptedException))
                    } finally { requests.remove(id,job) }
                }
                if(command=="math_calculate") {
                    // Input dispatch assigns this Future before reading a later
                    // cancel line, including when the task is still queued.
                    val task=WindowsCalculatorTask(work,
                        cancelled={reply(JSONObject().put("error","Mathematical calculation cancelled").put("cancelled",true))},
                        completed={requests.remove(id,job)})
                    job.calculatorTask=task
                    calculatorExecutor.execute(task)
                } else lane.submit(work)
            } catch (error: Throwable) { respond(replyId,replyCommand,JSONObject().put("error",error.message ?: "Invalid narration request")) }
        }
    }
    requests.values.forEach(::cancelRequest)
    executor.submit { backend.close() }
    executor.shutdown()
    editorExecutor.shutdown();calculatorExecutor.shutdown()
    editorExecutor.awaitTermination(30,java.util.concurrent.TimeUnit.SECONDS)
    calculatorExecutor.awaitTermination(30,java.util.concurrent.TimeUnit.SECONDS)
    executor.awaitTermination(30,java.util.concurrent.TimeUnit.SECONDS)
}
