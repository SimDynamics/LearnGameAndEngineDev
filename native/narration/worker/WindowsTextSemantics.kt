package lgaed.narration.windows

import com.learngraphicsprogramming.app.narration.*
import org.json.JSONArray
import org.json.JSONObject
import java.security.MessageDigest
import java.util.IdentityHashMap

/** Request-local source provenance; no strings are classified by their value. */
internal object WindowsTextSemantics {
    data class Span(val start: Int, val end: Int, val text: String, val role: String, val reading: String)
    data class Field(val source: String, val spans: List<Span>)
    private val roles = setOf("prose","cpp","string","data","math","output","citation")
    private val readings = setOf("auto","natural","technical","characters")
    private val bound = ThreadLocal<IdentityHashMap<String,Field>?>()
    private val authored = ThreadLocal<IdentityHashMap<String,Boolean>?>()

    private fun keys(value: JSONObject) = value.keys().asSequence().toSet()
    private fun integer(value: Any?): Int {
        require(value is Int || value is Long) { "Semantic positions must be exact integers" }
        val result = (value as Number).toLong()
        require(result in 0..Int.MAX_VALUE.toLong()) { "Semantic position is outside its field" }
        return result.toInt()
    }
    private fun boundary(source: String, at: Int): Boolean = at in 0..source.length &&
        !(at > 0 && at < source.length && source[at - 1].isHighSurrogate() && source[at].isLowSurrogate())
    fun parse(source: String, value: JSONObject): Field {
        require(keys(value) == setOf("spans")) { "Unknown semantic field property" }
        require(source.toByteArray(Charsets.UTF_8).size <= 8 * 1024 * 1024) { "Semantic source is too large" }
        var scalar = 0
        while (scalar < source.length) {
            require(!source[scalar].isLowSurrogate()) { "Invalid semantic Unicode source" }
            if (source[scalar].isHighSurrogate()) {
                require(scalar + 1 < source.length && source[scalar + 1].isLowSurrogate()) { "Invalid semantic Unicode source" }
                scalar++
            }
            scalar++
        }
        val spans = value.getJSONArray("spans")
        require(spans.length() <= 100000) { "Too many semantic spans" }
        var previous = 0
        return Field(source,(0 until spans.length()).map { index ->
            val item = spans.getJSONObject(index)
            require(keys(item).all { it in setOf("start","end","text","role","reading") } &&
                keys(item).containsAll(setOf("start","end","text","role"))) { "Invalid semantic span properties" }
            val start = integer(item.get("start")); val end = integer(item.get("end"))
            val text = item.get("text") as? String ?: error("Semantic selection must be text")
            val role = item.get("role") as? String ?: error("Semantic role must be text")
            val reading = if (item.has("reading")) item.get("reading") as? String ?: error("Semantic reading must be text") else "auto"
            require(start >= previous && start < end && boundary(source,start) && boundary(source,end)
                && source.substring(start,end) == text && role in roles && reading in readings) { "Invalid exact semantic source range" }
            previous = end; Span(start,end,text,role,reading)
        })
    }
    fun lookup(source: String): Field? = bound.get()?.get(source)
    fun <T> withCurriculum(lesson: JSONObject, action: () -> T): T {
        val saved = bound.get(); val savedAuthored = authored.get();val values = IdentityHashMap<String,Field>()
        val publicSources = IdentityHashMap<String,Boolean>()
        fun bind(field: JSONObject, source: String) {
            val hash = MessageDigest.getInstance("SHA-256").digest(source.toByteArray(Charsets.UTF_8))
                .joinToString("") { "%02x".format(it.toInt() and 255) }
            require(field.get("sourceSha256") is String && field.getString("sourceSha256") == hash) { "Stale semantic field source" }
            val parsed = parse(source,JSONObject().put("spans",field.getJSONArray("spans")))
            val prior = values.put(source,parsed)
            require(prior == null || prior == parsed) { "Shared source object has conflicting semantic owners" }
        }
        try {
            val declaration = lesson.opt("textSemantics")
            if (lesson.has("textSemantics")) {
                require(declaration is JSONObject && keys(declaration).containsAll(setOf("version","fields")) &&
                        keys(declaration).all { it in setOf("version","fields","rootFields") } && integer(declaration.get("version")) == 1) {
                    "Invalid curricular text semantics"
                }
                val fields = declaration.getJSONArray("fields")
                val roots = WindowsSemanticFields.lesson(lesson)
                for (source in roots.values) publicSources[source] = true
                if (declaration.has("rootFields")) {
                    val rootFields = declaration.getJSONArray("rootFields")
                    val selected = mutableSetOf<String>()
                    require(fields.length() + rootFields.length() <= 100000) { "Too many curricular semantic fields" }
                    for (index in 0 until rootFields.length()) {
                        val field = rootFields.getJSONObject(index)
                        require(keys(field) == setOf("path","sourceSha256","spans")) { "Invalid root semantic field" }
                        val path = field.getString("path")
                        require(selected.add(path)) { "Duplicate root semantic field" }
                        val source = roots[path] ?: error("A root semantic pointer must own a supported public field")
                        bind(field,source)
                    }
                }
                if (fields.length() == 0) {bound.set(values);authored.set(publicSources);return action()}
                val layout = lesson.getJSONObject("stateLayout")
                require(integer(layout.get("version")) == 2) { "Semantic fields require their stable v2 owners" }
                val pages = lesson.getJSONArray("pages"); val current = layout.getJSONArray("current")
                val owners = mutableMapOf<String,JSONObject>()
                for (p in 0 until pages.length()) {
                    val blocks = pages.getJSONObject(p).getJSONArray("blocks")
                    val ids = current.getJSONObject(p).getJSONArray("blocks")
                    require(blocks.length() == ids.length()) { "Invalid semantic block ownership" }
                    for (b in 0 until blocks.length()) require(owners.put(ids.getString(b),blocks.getJSONObject(b)) == null) { "Duplicate semantic owner" }
                }
                val inventories = owners.mapValues { (_,block) -> WindowsSemanticFields.public(block) }
                for (inventory in inventories.values) for (source in inventory.values) publicSources[source] = true
                val seen = mutableSetOf<Pair<String,String>>()
                require(fields.length() <= 100000) { "Too many curricular semantic fields" }
                for (index in 0 until fields.length()) {
                    val field = fields.getJSONObject(index)
                    require(keys(field) == setOf("block","path","sourceSha256","spans")) { "Invalid curricular semantic field" }
                    val owner = field.getString("block"); val path = field.getString("path")
                    require(seen.add(owner to path) && path.startsWith('/') && path.length <= 4096) { "Invalid semantic field pointer" }
                    val source = inventories[owner]?.get(path) ?: error("A semantic pointer must own a supported public field")
                    bind(field,source)
                }
                for ((owner,block) in owners) {
                    val group = WindowsSemanticFields.feedbackOrigins(block,inventories.getValue(owner))
                    val selected = seen.filter { it.first == owner }.map { it.second }.toSet()
                    require(group.intersect(selected).isEmpty() || selected.containsAll(group)) { "Math feedback requires its complete semantic source group" }
                }
            }
            bound.set(values);authored.set(publicSources);return action()
        } finally {
            if (saved == null) bound.remove() else bound.set(saved)
            if (savedAuthored == null) authored.remove() else authored.set(savedAuthored)
        }
    }
    /** Preserve exact field provenance through the extractor's existing joins. */
    fun compose(parts: List<String>, separator: String): String {
        val result = parts.joinToString(separator)
        val values = bound.get() ?: return result
        if (parts.none { values.containsKey(it) }) return result
        require(parts.none { it.isNotBlank() && authored.get()?.containsKey(it) == true && !values.containsKey(it) }) {
            "A composed public field requires its complete semantic source group"
        }
        val spans = mutableListOf<Span>(); var offset = 0
        for ((index, part) in parts.withIndex()) {
            values[part]?.spans?.forEach { spans += it.copy(start=it.start+offset,end=it.end+offset) }
            offset += part.length + if (index + 1 < parts.size) separator.length else 0
        }
        values[result] = Field(result,spans); return result
    }
    /** Format the one visible unit placeholder without losing its source owner. */
    fun unitCaption(template: String, unit: String): String {
        val placeholders = Regex("%(?:1\\\$)?s").findAll(template).toList()
        require(placeholders.size == 1) { "A unit caption requires its one typed source placeholder" }
        val token = placeholders.single()
        val prefix = String.format(java.util.Locale.ROOT,template.substring(0,token.range.first))
        val suffix = String.format(java.util.Locale.ROOT,template.substring(token.range.last+1))
        return compose(listOf(prefix,unit,suffix),"")
    }
    /** Each entry is a separate RichEdit (or the inline field's own caption
     * region); joining them would manufacture recordings the reader never asks for. */
    fun nativeFieldSources(response: JSONObject): List<String> {
        val fields = response.optJSONArray("fields")
        val definitions = linkedMapOf<String,JSONObject>()
        for (index in 0 until (fields?.length() ?: 0)) {
            val field = fields!!.getJSONObject(index); definitions[field.getString("id")] = field
        }
        val result = mutableListOf<String>(); val used = mutableSetOf<String>()
        fun text(value: JSONObject, key: String) { (value.opt(key) as? String)?.takeIf(String::isNotEmpty)?.let(result::add) }
        fun field(id: String, inline: Boolean) {
            val value = definitions[id] ?: error("A displayed math field has no definition")
            require(used.add(id)) { "A mathematical template repeats the same field" }
            val fixed = value.optString("type","numeric") == "fixed"
            if (!inline || !fixed) text(value,"label")
            if (fixed) text(value,"text")
            text(value,"unit")
        }
        val segments = response.optJSONArray("segments")
        val rows = response.optJSONArray("rows")?.takeIf { response.optString("layout") == "table" && it.length() > 0 }
        when {
            segments != null -> for (index in 0 until segments.length()) {
                val segment = segments.getJSONObject(index)
                if (segment.has("field")) field(segment.getString("field"),true) else text(segment,"text")
            }
            rows != null -> {
                val headers = response.optJSONArray("headers")
                for (index in 0 until (headers?.length() ?: 0)) (headers!!.opt(index) as? String)?.let(result::add)
                for (rowIndex in 0 until rows.length()) {
                    val cells = rows.getJSONArray(rowIndex)
                    for (index in 0 until cells.length()) when (val cell = cells.get(index)) {
                        is JSONObject -> field(cell.getString("field"),false)
                        is String -> result += cell
                        is Number -> result += cell.toString()
                    }
                }
            }
            else -> definitions.keys.forEach { field(it,false) }
        }
        definitions.keys.filter { it !in used }.forEach { field(it,segments != null) }
        return result
    }
    private val feedbackLanguages = ThreadLocal<Pair<String,String>?>()
    fun <T> withNativeFeedback(uiLanguage: String, contentLanguage: String, action: () -> T): T {
        val previous = feedbackLanguages.get(); feedbackLanguages.set(uiLanguage to contentLanguage)
        return try { action() } finally {
            if (previous == null) feedbackLanguages.remove() else feedbackLanguages.set(previous)
        }
    }
    /** The actual successful native feedback is one region. Preparation omits
     * only its decoration icon, exactly like the direct plan frontend. */
    fun nativeCorrectFeedbackSpeech(block: JSONObject): String {
        val (uiLanguage,contentLanguage) = feedbackLanguages.get() ?: error("Missing native feedback locale owners")
        val mathematical = block.optString("type").startsWith("math_")
        val original = if (mathematical) block.optJSONObject("feedback")?.opt("correct") as? String
            else block.opt("explanation") as? String
        val raw = original.orEmpty()
        val trim = if (mathematical) 0 else raw.indexOfFirst { it != '\r' && it != '\n' }.let { if (it < 0) raw.length else it }
        val included = if (mathematical) raw.isNotBlank() else trim < raw.length
        val heading = if (mathematical) MathLocaleResources.resolve("math_ui_correct_heading",emptyList(),contentLanguage)
            else "✓ " + if (uiLanguage.startsWith("es")) "Correcto" else "Correct"
        val prefix = heading + if (included) { if (mathematical) "\n" else "\n\n" } else ""
        val full = prefix + if (included) raw.substring(trim) else ""
        val icon = IncorrectFeedbackNarration.verdictIconPrefix(full)
        val speech = IncorrectFeedbackNarration.visibleSpeech(full)
        val descriptor = original?.let(::lookup)?.takeIf { !mathematical || included }
        if (descriptor != null) {
            val offset = prefix.length - icon
            val spans = if (!included) emptyList() else descriptor.spans.mapNotNull { span ->
                val first = maxOf(span.start,trim)
                if (first >= span.end) null else span.copy(start=offset+first-trim,end=offset+span.end-trim,text=raw.substring(first,span.end))
            }
            bound.get()?.set(speech,Field(speech,spans))
        }
        return speech
    }
    /** Match rich_text's complete inline delimiters over the original field,
     * before a semantic range can divide either delimiter or its contents. */
    private fun hiddenPresentation(field: Field): BooleanArray {
        val source = field.source
        val mathematics = BooleanArray(source.length)
        val links = BooleanArray(source.length)
        fun protect(first: Int, last: Int) { for (at in first until last) mathematics[at] = true }
        fun space(char: Char) = char in " \t\r\n\u000b\u000c\u00a0\u202f"
        var cursor = 0
        while (cursor < source.length) {
            var opening = when {
                source.startsWith("\\(",cursor) -> "\\("
                source.startsWith("\\[",cursor) -> "\\["
                source.startsWith("$$",cursor) -> "$$"
                source[cursor] == '$' -> "$"
                else -> ""
            }
            if (opening.isEmpty()) { cursor++; continue }
            var closing = when (opening) { "\\(" -> "\\)"; "\\[" -> "\\]"; else -> opening }
            var end = source.indexOf(closing,cursor + opening.length)
            if (opening == "$$" && end < 0) { opening = "$"; closing = "$"; end = source.indexOf('$',cursor + 1) }
            if (end < 0 || opening == "$" && (end == cursor + 1 ||
                    (cursor + 1 until end).any { source[it] == '\r' || source[it] == '\n' })) { cursor++; continue }
            protect(cursor,end + closing.length); cursor = end + closing.length
        }
        // Native mathematical spans protect their own notation even when the
        // author selects a natural rather than an automatic mathematical voice.
        for (span in field.spans) if (span.role == "math") protect(span.start,span.end)
        cursor = 0
        while (cursor < source.length) {
            if (!source.regionMatches(cursor,"http://",0,7,true) &&
                !source.regionMatches(cursor,"https://",0,8,true) &&
                !source.regionMatches(cursor,"www.",0,4,true)) { cursor++; continue }
            var end = cursor
            while (end < source.length && !space(source[end]) && source[end] != '`') end++
            for (at in cursor until end) links[at] = true
            cursor = end
        }
        val hidden = BooleanArray(source.length)
        cursor = 0
        while (cursor < source.length) {
            if (source[cursor] != '`' || mathematics[cursor] || links[cursor]) { cursor++; continue }
            val delimiter = if (source.startsWith("```",cursor)) "```" else "`"
            val end = source.indexOf(delimiter,cursor + delimiter.length)
            if (end < 0) { cursor += delimiter.length; continue }
            if ((cursor + delimiter.length until end).any { !space(source[it]) } &&
                (cursor until end).none { mathematics[it] }) {
                for (at in cursor until cursor + delimiter.length) hidden[at] = true
                for (at in end until end + delimiter.length) hidden[at] = true
            }
            cursor = end + delimiter.length
        }
        return hidden
    }
    /** One semantic frontend serves direct plans and anticipatory extraction. */
    fun projection(field: Field, language: NarrationLanguage): NarrationSourceProjection.Projection {
        val hidden = hiddenPresentation(field)
        data class Input(val mode: Int, var whitespaceGap: Boolean = false, val text: StringBuilder = StringBuilder(),
                         val owners: MutableList<ReadingTextRange> = mutableListOf())
        val inputs = mutableListOf<Input>()
        fun appendSource(start: Int, end: Int, role: String, reading: String, gap: Boolean = false) {
            val mode = when {
                reading == "characters" -> 3
                reading == "technical" || reading == "auto" && role in setOf("cpp","string") -> 1
                reading == "auto" && role == "math" -> 2
                else -> 0
            }
            var cursor = start
            while (cursor < end) {
                if (hidden[cursor]) { cursor++; continue }
                val width = Character.charCount(field.source.codePointAt(cursor))
                if (mode == 1 && inputs.size >= 2) {
                    val between = inputs.last()
                    val previous = inputs[inputs.lastIndex - 1]
                    if (previous.mode == 1 && between.mode == 0 && between.whitespaceGap && between.text.all(Char::isWhitespace)) {
                        previous.text.append(between.text); previous.owners += between.owners
                        inputs.removeAt(inputs.lastIndex)
                    }
                }
                val input = inputs.lastOrNull()?.takeIf { it.mode == mode } ?: Input(mode,gap).also(inputs::add)
                if (!gap) input.whitespaceGap = false
                val owner = ReadingTextRange(cursor,cursor + width)
                repeat(width) { input.text.append(field.source[cursor + it]); input.owners += owner }
                cursor += width
            }
        }
        var cursor = 0
        for (span in field.spans) {
            appendSource(cursor,span.start,"prose","natural",gap=true)
            appendSource(span.start,span.end,span.role,span.reading); cursor = span.end
        }
        appendSource(cursor,field.source.length,"prose","natural",gap=true)
        val output = StringBuilder().append(ManualEnglishSpeech.PREPARED)
        val owners = mutableListOf(ReadingTextRange(0,0))
        val boundedCharacters = field.spans.any { it.reading == "characters" }
        var preparedBytes = 3 // The existing prepared sentinel is one UTF-8 scalar.
        var previousMode = 0
        fun wordStart(char: Char) = char.isLetterOrDigit() || char.isSurrogate() || char == '_' || char == ManualEnglishSpeech.START
        fun wordEnd(char: Char) = char.isLetterOrDigit() || char.isSurrogate() || char == '_' || char == ManualEnglishSpeech.END
        for (input in inputs) {
            val source = input.text.toString()
            val piece = when (input.mode) {
                1 -> NarrationSourceProjection.semanticCode(source,language)
                2 -> NarrationSourceProjection.forLanguage(source,"inline",language) ?: error("Unprojectable mathematical semantic span")
                3 -> WindowsCharacterSpeech.projection(source,language)
                else -> NarrationSourceProjection.Projection(source,input.owners)
            }
            val prefix = if (piece.text.startsWith(ManualEnglishSpeech.PREPARED)) 1 else 0
            val body = piece.text.substring(prefix)
            if (body.isEmpty()) continue
            // Separate spoken words introduced at a mixed semantic boundary;
            // unchanged prose and its punctuation keep their exact spelling.
            val separate = output.length > 1 && (previousMode != 0 || input.mode != 0) && wordEnd(output.last()) && wordStart(body.first())
            if (boundedCharacters) {
                val extra = body.toByteArray(Charsets.UTF_8).size + if (separate) 1 else 0
                require(preparedBytes <= 8 * 1024 * 1024 - extra) { "Character reading exceeds the bounded prepared-text size" }
                preparedBytes += extra
            }
            if (separate) {
                output.append(' '); owners += ReadingTextRange(0,0)
            }
            output.append(body)
            owners += if (input.mode == 0) input.owners else piece.owners.drop(prefix).map { owner ->
                if (owner.endExclusive <= owner.start) ReadingTextRange(0,0) else {
                    require(owner.start >= 0 && owner.endExclusive <= input.owners.size) { "Invalid semantic preparation source range" }
                    ReadingTextRange(input.owners[owner.start].start,input.owners[owner.endExclusive - 1].endExclusive)
                }
            }
            previousMode = input.mode
        }
        return NarrationSourceProjection.Projection(output.toString(),owners)
    }
    fun prepared(field: Field, language: NarrationLanguage): String = projection(field,language).text
    /** Anticipation only: the ordinary extractor never reads compiler oracles.
     * Opt in solely for public guide fields actually present in the overlay. */
    fun compilerGuides(lesson: JSONObject, page: Int, uiLanguage: String): List<Field> {
        val result = mutableListOf<Field>()
        fun append(source: Any?, correct: Boolean) {
            if (source !is String) return
            val field = lookup(source) ?: return
            var trim = 0
            while (trim < source.length && source[trim] in "\r\n") trim++
            val body = source.substring(trim)
            val verdict = if (uiLanguage.startsWith("es")) {if (correct) "Correcto" else "Incorrecto"}
                else if (correct) "Correct" else "Incorrect"
            val prefix = (if (correct) "✓ " else "✗ ") + verdict + if (body.isEmpty()) "" else "\n\n"
            result += Field(prefix+body,field.spans.mapNotNull { span ->
                val first = maxOf(span.start,trim)
                if (first >= span.end) null else span.copy(start=prefix.length+first-trim,end=prefix.length+span.end-trim,text=source.substring(first,span.end))
            })
        }
        val blocks = lesson.optJSONArray("pages")?.optJSONObject(page)?.optJSONArray("blocks") ?: return result
        for (index in 0 until blocks.length()) {
            val block = blocks.optJSONObject(index) ?: continue
            if (block.optString("type") != "cpp_compiler") continue
            append(block.optJSONObject("compileCheck")?.opt("explanation"),true)
            val cases = block.optJSONObject("outputCheck")?.optJSONArray("cases") ?: continue
            for (caseIndex in 0 until cases.length()) {
                val case = cases.optJSONObject(caseIndex) ?: continue
                append(case.opt("explanation"),true)
                val reviews = case.optJSONArray("reviewFeedback") ?: continue
                for (reviewIndex in 0 until reviews.length()) append(reviews.optJSONObject(reviewIndex)?.opt("text"),false)
            }
        }
        return result
    }
}
