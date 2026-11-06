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
    private val readings = setOf("auto","natural","technical")
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
        try {
            val declaration = lesson.opt("textSemantics")
            if (lesson.has("textSemantics")) {
                require(declaration is JSONObject && keys(declaration) == setOf("version","fields") && integer(declaration.get("version")) == 1) {
                    "Invalid curricular text semantics"
                }
                val fields = declaration.getJSONArray("fields")
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
                    val hash = MessageDigest.getInstance("SHA-256").digest(source.toByteArray(Charsets.UTF_8))
                        .joinToString("") { "%02x".format(it.toInt() and 255) }
                    require(field.get("sourceSha256") is String && field.getString("sourceSha256") == hash) { "Stale semantic field source" }
                    val parsed = parse(source,JSONObject().put("spans",field.getJSONArray("spans")))
                    val prior = values.put(source,parsed)
                    require(prior == null || prior == parsed) { "Shared source object has conflicting semantic owners" }
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
    /** One semantic frontend serves direct plans and anticipatory extraction. */
    fun projection(field: Field, language: NarrationLanguage): NarrationSourceProjection.Projection {
        val output = StringBuilder().append(ManualEnglishSpeech.PREPARED)
        val owners = mutableListOf(ReadingTextRange(0,0))
        fun append(start: Int, end: Int, role: String, reading: String) {
            if (start == end) return
            val text = field.source.substring(start,end)
            val technical = reading == "technical" || reading == "auto" && role in setOf("cpp","string")
            val math = reading == "auto" && role == "math"
            val piece = when {
                technical -> NarrationSourceProjection.semanticCode(text,language)
                math -> NarrationSourceProjection.forLanguage(text,"inline",language) ?: error("Unprojectable mathematical semantic span")
                else -> {
                    val shown = StringBuilder(); val origins = mutableListOf<ReadingTextRange>(); var cursor = 0
                    while (cursor < text.length) {
                        // Existing inline delimiters are hidden presentation,
                        // never a reason to re-enter technical inference.
                        if (text[cursor] == '`' || text.startsWith("**",cursor)) {
                            cursor += if (text[cursor] == '`') 1 else 2; continue
                        }
                        val width = Character.charCount(text.codePointAt(cursor)); val source = ReadingTextRange(cursor,cursor+width)
                        repeat(width) { shown.append(text[cursor+it]); origins += source }; cursor += width
                    }
                    NarrationSourceProjection.Projection(shown.toString(),origins)
                }
            }
            val prefix = if (piece.text.startsWith(ManualEnglishSpeech.PREPARED)) 1 else 0
            output.append(piece.text.substring(prefix))
            owners += piece.owners.drop(prefix).map { if (it.endExclusive > it.start) ReadingTextRange(it.start+start,it.endExclusive+start) else it }
        }
        var cursor = 0
        for (span in field.spans) {
            append(cursor,span.start,"prose","natural");append(span.start,span.end,span.role,span.reading);cursor=span.end
        }
        append(cursor,field.source.length,"prose","natural")
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
