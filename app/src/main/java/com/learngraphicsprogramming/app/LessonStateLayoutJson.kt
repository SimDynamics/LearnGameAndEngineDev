package com.learngraphicsprogramming.app

import org.json.JSONArray
import org.json.JSONObject
import java.math.BigDecimal
import java.security.MessageDigest

/** Metadata lives at the lesson root, never inside a mathematical puzzle's fingerprint. */
internal object LessonStateLayoutJson {
    fun read(lesson: JSONObject): StableLessonStateLayout? {
        if (!lesson.has("stateLayout")) return null
        try {
            val metadata = lesson.getJSONObject("stateLayout")
            val version = metadata.get("version") as? Int ?: error("Invalid version")
            val expected = if (version == 1) setOf("version", "origin", "current") else
                setOf("version", "locale", "origin", "current", "family", "additions", "editorial")
            val actualFields = metadata.keys().asSequence().toSet()
            // A single optional, complete pre-format archive is independent of
            // storage ownership. All other unknown metadata still fails closed.
            require(version in 1..2 && expected.all { it in actualFields } &&
                actualFields.all { it in expected ||
                    (version == 2 && it in setOf(
                        "sourceFormatRevisions", "orderRefinements",
                        "choiceRevisions", "compilerRevisions")) })
            val (origin, signatures) = readOrigin(metadata.getJSONArray("origin"))
            val current = metadata.getJSONArray("current")
            val positions = (0 until current.length()).map { p ->
                val page = current.getJSONObject(p)
                require(page.keys().asSequence().toSet() == setOf("id", "blocks"))
                val blocks = page.getJSONArray("blocks")
                LessonStatePage(strictString(page, "id"), (0 until blocks.length()).map { b ->
                    blocks.get(b) as? String ?: error("Non-string identity") })
            }
            val family = linkedMapOf<String, LessonStateFamily>()
            val additions = mutableListOf<LessonStateAddition>()
            val editorial = linkedMapOf<String, String>()
            val editorialSeen = mutableMapOf<String, MutableSet<String>>()
            val locale = if (version == 2) strictString(metadata, "locale") else null
            if (version == 2) {
                val variants = metadata.getJSONObject("family")
                require(variants.length() in 1..3)
                variants.keys().forEach { language ->
                    val record = variants.getJSONObject(language)
                    require(record.keys().asSequence().toSet() == setOf("sha256", "origin"))
                    val (pages, hashes) = readOrigin(record.getJSONArray("origin"))
                    family[language] = LessonStateFamily(strictString(record, "sha256"), pages, hashes)
                }
                require(family[locale]?.signatures == signatures)
                val extra = metadata.getJSONArray("additions")
                require(extra.length() <= 100_000)
                for (index in 0 until extra.length()) {
                    val addition = extra.getJSONObject(index)
                    require(addition.keys().asSequence().toSet() == setOf("id", "sha256", "storage", "progressPage"))
                    val storage = addition.getJSONObject("storage")
                    require(storage.keys().asSequence().toSet() == setOf("page", "block"))
                    additions += LessonStateAddition(strictString(addition, "id"), strictString(addition, "sha256"),
                        LessonStatePosition(storage.get("page") as Int, storage.get("block") as Int),
                        strictString(addition, "progressPage"))
                }
                val changes = metadata.getJSONArray("editorial")
                require(changes.length() <= 100_000)
                for (index in 0 until changes.length()) {
                    val change = changes.getJSONObject(index)
                    require(change.keys().asSequence().toSet() == setOf("id", "beforeSha256", "afterSha256"))
                    val id = strictString(change, "id")
                    val before = strictString(change, "beforeSha256")
                    val after = strictString(change, "afterSha256")
                    require((id in signatures || additions.any { it.id == id }) &&
                        HASH.matches(before) && HASH.matches(after) && before != after)
                    val seen = editorialSeen.getOrPut(id) { linkedSetOf(before) }
                    require((editorial[id] == null || editorial[id] == before) && after !in seen)
                    seen += after
                    editorial[id] = after
                }
            }
            val expectedSignatures = signatures + additions.associate { it.id to it.sha256 }
            val pages = lesson.getJSONArray("pages")
            require(pages.length() == positions.size)
            val active = linkedMapOf<String, JSONObject>()
            positions.forEachIndexed { p, position ->
                val blocks = pages.getJSONObject(p).optJSONArray("blocks") ?: JSONArray()
                require(blocks.length() == position.blocks.size)
                position.blocks.forEachIndexed { b, id ->
                    require(active.put(id, blocks.getJSONObject(b)) == null)
                }
            }
            val orderOriginals = linkedMapOf<String, JSONObject>()
            if (version == 2 && metadata.has("orderRefinements")) {
                require(locale == "es")
                val archives = metadata.getJSONArray("orderRefinements")
                require(archives.length() <= 100_000)
                for (index in 0 until archives.length()) {
                    val entry = archives.getJSONObject(index)
                    require(entry.keys().asSequence().toSet() == setOf("id", "block"))
                    val id = strictString(entry, "id")
                    require(id in active && id !in orderOriginals)
                    val historical = entry.getJSONObject("block")
                    require(stateFingerprint(historical) == expectedSignatures.getValue(id) &&
                        editorialSeen[id]?.contains(fingerprint(historical)) == true)
                    orderOriginals[id] = historical
                }
            }
            val choiceOriginals = linkedMapOf<String, JSONObject>()
            if (version == 2 && metadata.has("choiceRevisions")) {
                require(locale == "es")
                val archives = metadata.getJSONArray("choiceRevisions")
                require(archives.length() <= 100_000)
                for (index in 0 until archives.length()) {
                    val entry = archives.getJSONObject(index)
                    require(entry.keys().asSequence().toSet() == setOf("id", "block"))
                    val id = strictString(entry, "id")
                    require(id in active && id !in choiceOriginals)
                    val historical = entry.getJSONObject("block")
                    require(stateFingerprint(historical) == expectedSignatures.getValue(id) &&
                        editorialSeen[id]?.contains(fingerprint(historical)) == true)
                    choiceOriginals[id] = historical
                }
            }
            val compilerOriginals = linkedMapOf<String, JSONObject>()
            val compilerReferences = linkedMapOf<String, JSONObject>()
            if (version == 2 && metadata.has("compilerRevisions")) {
                require(locale == "es")
                val revisions = metadata.getJSONArray("compilerRevisions")
                require(revisions.length() <= 100_000)
                for (index in 0 until revisions.length()) {
                    val entry = revisions.getJSONObject(index)
                    val fields = entry.keys().asSequence().toSet()
                    require(setOf("id", "block").all { it in fields } &&
                        fields.all { it in setOf("id", "block", "references") })
                    val id = strictString(entry, "id")
                    require(id in active && id !in compilerOriginals && id !in compilerReferences)
                    val old = entry.getJSONObject("block")
                    require(stateFingerprint(old) == expectedSignatures.getValue(id) &&
                        editorialSeen[id]?.contains(fingerprint(old)) == true)
                    compilerOriginals[id] = old
                    // Some reviewed compiler changes extend only stdout or
                    // the case list and have no separately revised reference.
                    val references = entry.optJSONArray("references") ?: JSONArray()
                    require(references.length() <= 100_000)
                    for (referenceIndex in 0 until references.length()) {
                        val reference = references.getJSONObject(referenceIndex)
                        require(reference.keys().asSequence().toSet() == setOf("id", "block"))
                        val referenceId = strictString(reference, "id")
                        require(referenceId in active && referenceId != id &&
                            referenceId !in compilerReferences && referenceId !in compilerOriginals)
                        val historical = reference.getJSONObject("block")
                        require(stateFingerprint(historical) ==
                            expectedSignatures.getValue(referenceId) &&
                            editorialSeen[referenceId]?.contains(fingerprint(historical)) == true)
                        compilerReferences[referenceId] = historical
                    }
                }
            }
            val originals = linkedMapOf<String, JSONObject>()
            if (version == 2 && metadata.has("sourceFormatRevisions")) {
                val archives = metadata.getJSONArray("sourceFormatRevisions")
                require(archives.length() <= 100_000)
                for (index in 0 until archives.length()) {
                    val entry = archives.getJSONObject(index)
                    require(entry.keys().asSequence().toSet() == setOf("id", "block"))
                    val id = strictString(entry, "id")
                    require(id in active && id !in originals)
                    val historical = entry.getJSONObject("block")
                    val ordered = if (id in orderOriginals)
                        CurricularOrderRefinement.project(historical, orderOriginals.getValue(id)).first
                    else historical
                    val chosen = if (id in choiceOriginals)
                        CurricularChoiceRevision.project(ordered, choiceOriginals.getValue(id))
                    else ordered
                    val expanded = if (id in compilerOriginals)
                        CurricularCompilerRevision.projectCompiler(chosen, compilerOriginals.getValue(id))
                    else if (id in compilerReferences)
                        CurricularCompilerRevision.projectReference(chosen, compilerReferences.getValue(id))
                    else chosen
                    require(stateFingerprint(expanded) == expectedSignatures.getValue(id))
                    require(editorialSeen[id]?.contains(fingerprint(historical)) == true)
                    originals[id] = historical
                }
            }
            val orderRefinements = linkedMapOf<String, CurricularOrderRefinement>()
            for ((id, block) in active) {
                // Restore only approved source formatting and original order
                // card groups when calculating the immutable historical digest.
                val formatted = originals[id]?.let { before ->
                    CurricularSourceFormatCompat.project(block, before)
                } ?: block
                val ordered = orderOriginals[id]?.let { before ->
                    val (original, revision) = CurricularOrderRefinement.project(formatted, before)
                    orderRefinements[id] = revision
                    original
                } ?: formatted
                val chosen = choiceOriginals[id]?.let { before ->
                    CurricularChoiceRevision.project(ordered, before)
                } ?: ordered
                val protected = compilerOriginals[id]?.let { before ->
                    CurricularCompilerRevision.projectCompiler(chosen, before)
                } ?: compilerReferences[id]?.let { before ->
                    CurricularCompilerRevision.projectReference(chosen, before)
                } ?: chosen
                require(stateFingerprint(protected) == expectedSignatures.getValue(id))
                editorial[id]?.let { require(fingerprint(block) == it) }
            }
            return StableLessonStateLayout(origin, positions, version, locale,
                family, additions, orderRefinements)
        } catch (error: LessonStateLayoutException) {
            throw error
        } catch (_: Exception) {
            throw LessonStateLayoutException("Invalid or stale lesson state layout; keep saved data")
        }
    }

    private val HASH = Regex("[0-9a-f]{64}")

    private fun readOrigin(original: JSONArray): Pair<List<LessonStatePage>, Map<String, String>> {
        require(original.length() in 1..2048)
        val signatures = linkedMapOf<String, String>()
        val pages = (0 until original.length()).map { p ->
            val page = original.getJSONObject(p)
            require(page.keys().asSequence().toSet() == setOf("id", "blocks"))
            val blocks = page.getJSONArray("blocks")
            require(blocks.length() <= 100_000)
            LessonStatePage(strictString(page, "id"), (0 until blocks.length()).map { b ->
                val block = blocks.getJSONObject(b)
                require(block.keys().asSequence().toSet() == setOf("id", "sha256"))
                val id = strictString(block, "id")
                val digest = strictString(block, "sha256")
                require(HASH.matches(digest) && signatures.put(id, digest) == null)
                id
            })
        }
        return pages to signatures
    }

    fun stateFingerprint(block: JSONObject): String = fingerprint(statePayload(block))

    private fun statePayload(block: JSONObject): JSONObject {
        val type = block.optString("type")
        val ignored = when (type) {
            "heading", "paragraph", "note" -> setOf("text", "emphasis")
            "multiple_choice", "fill_code", "output_prediction", "order_code",
            "drag_fill_code", "inline_fill_code" -> setOf("question", "explanation", "reviewFeedback")
            else -> emptySet()
        }
        val copy = JSONObject(block.toString())
        ignored.forEach(copy::remove)
        if (type == "cpp_compiler") {
            val cases = copy.optJSONObject("outputCheck")?.optJSONArray("cases")
            if (cases != null) for (index in 0 until cases.length()) {
                cases.optJSONObject(index)?.let { case ->
                    case.remove("explanation")
                    case.remove("reviewFeedback")
                }
            }
        }
        return copy
    }

    private fun strictString(value: JSONObject, key: String): String =
        value.get(key) as? String ?: throw IllegalArgumentException("Non-string metadata")

    /** Deterministic cross-language framing; independent of JSON object field order. */
    fun fingerprint(value: Any?): String {
        val digest = MessageDigest.getInstance("SHA-256")
        fun token(text: String) { digest.update(text.toByteArray(Charsets.UTF_8)) }
        fun string(text: String) {
            val bytes = text.toByteArray(Charsets.UTF_8)
            token("s${bytes.size}:")
            digest.update(bytes)
        }
        fun visit(node: Any?, depth: Int) {
            require(depth <= 64)
            when {
                node == null || node === JSONObject.NULL -> token("z")
                node is JSONObject -> {
                    token("{")
                    node.keys().asSequence().toList().sorted().forEach { key ->
                        string(key)
                        visit(node.get(key), depth + 1)
                    }
                    token("}")
                }
                node is JSONArray -> {
                    token("[")
                    for (index in 0 until node.length()) visit(node.get(index), depth + 1)
                    token("]")
                }
                node is String -> string(node)
                node is Boolean -> token(if (node) "t" else "f")
                node is Number -> {
                    val decimal = BigDecimal(node.toString()).stripTrailingZeros()
                    require(kotlin.math.abs(decimal.scale().toLong()) <= 10_000)
                    require(decimal.precision() <= 10_000)
                    token("n${decimal.toPlainString()};")
                }
                else -> throw IllegalArgumentException("Unsupported JSON value")
            }
        }
        visit(value, 0)
        return digest.digest().joinToString("") { "%02x".format(it) }
    }
}
