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
            val editorialChains = mutableMapOf<String, MutableList<String>>()
            val returnArchives = mutableMapOf<String, MutableList<JSONObject>>()
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
                    val fields = change.keys().asSequence().toSet()
                    require(fields == setOf("id", "beforeSha256", "afterSha256") ||
                        fields == setOf("id", "beforeSha256", "afterSha256", "returnTo"))
                    val id = strictString(change, "id")
                    val before = strictString(change, "beforeSha256")
                    val after = strictString(change, "afterSha256")
                    require((id in signatures || additions.any { it.id == id }) &&
                        HASH.matches(before) && HASH.matches(after) && before != after)
                    val seen = editorialSeen.getOrPut(id) { linkedSetOf(before) }
                    val chain = editorialChains.getOrPut(id) { mutableListOf(before) }
                    require(chain.last() == before && (editorial[id] == null || editorial[id] == before))
                    if (after in seen) {
                        // A return is not a new editorial state. It must name a
                        // prior version of this owner AND authenticate the whole
                        // global editorial prefix, not only this owner's hashes.
                        val returning = change.getJSONObject("returnTo")
                        require(returning.keys().asSequence().toSet() ==
                            setOf("revision", "historySha256", "previousBlock"))
                        val revision = returning.get("revision") as? Int
                            ?: error("An editorial return revision must be an integer")
                        require(revision in 0 until chain.size - 1 && chain[revision] == after)
                        val prefix = JSONArray()
                        for (prior in 0 until index) prefix.put(changes.getJSONObject(prior))
                        val history = JSONObject().put("lessonId", strictString(lesson, "id"))
                            .put("locale", locale).put("editorial", prefix)
                        require(strictString(returning, "historySha256") == fingerprint(history))
                        val oldBlock = returning.getJSONObject("previousBlock")
                        require(fingerprint(oldBlock) == before)
                        returnArchives.getOrPut(id) { mutableListOf() }.add(oldBlock)
                    } else {
                        require(!change.has("returnTo"))
                    }
                    seen += after
                    chain += after
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
                val historical = CurricularMathLabelRevision.project(
                    lesson.optString("id"), id, protected)
                if (version == 2 && locale == "es" &&
                    requiredNonBlankStderr[lesson.optString("id")] == id) {
                    val check = block.optJSONObject("outputCheck")
                    val cases = check?.optJSONArray("cases")
                    require(cases?.length() == 1 &&
                        cases.getJSONObject(0).optString("standardError") == "nonBlank") {
                        "The published runtime stderr objective cannot be silently removed"
                    }
                }
                require(stateFingerprint(historical) == expectedSignatures.getValue(id)) {
                    "Protected response/code contract changed for $id"
                }
                // Restoring a previous wording must never restore another
                // owner's answer, draft, output objective or diagnostic.
                returnArchives[id]?.forEach { previous ->
                    require(stateFingerprint(previous) == expectedSignatures.getValue(id)) {
                        "An editorial return changed its protected historical owner"
                    }
                    if (previous.has("compileCheck")) {
                        require(block.has("compileCheck"))
                        val previousGoal = JSONObject(previous.getJSONObject("compileCheck").toString())
                            .apply { remove("explanation") }
                        val currentGoal = JSONObject(block.getJSONObject("compileCheck").toString())
                            .apply { remove("explanation") }
                        require(fingerprint(previousGoal) == fingerprint(currentGoal))
                    }
                }
                editorial[id]?.let { require(fingerprint(block) == it) }
            }
            return StableLessonStateLayout(origin, positions, version, locale,
                family, additions, orderRefinements)
        } catch (error: LessonStateLayoutException) {
            throw error
        } catch (error: Exception) {
            // Preserve the internal cause for test diagnostics without exposing
            // its details in the student-facing error message.
            throw LessonStateLayoutException(
                "Invalid or stale lesson state layout; keep saved data", error)
        }
    }

    private val reviewedIntentionalExits = mapOf(
        "d263ac4c841168cb7723bdc2daaeaf6ddc839c4830f17a26f6166ff919fb252c" to
            "Entrada invalida|9:4",
        "6f796c5c55683ac266ae3f83c54f935273c341c34da087161276c7e361524ae8" to
            "Error al abrir salida"
    )
    private const val reviewedCrtBefore =
        "b475e21c42a6b93cb635f5bfd5197b66b7c1107145a413f2fa9582ab142cda35"
    private const val reviewedCrtAfter =
        "53648380e15d1148a779de47c0f94dc08f63e2319f79bdc08e72d25a6992ed31"

    private fun withoutCompilerGuidance(block: JSONObject): JSONObject {
        val result = JSONObject(block.toString())
        result.optJSONObject("compileCheck")?.remove("explanation")
        val cases = result.optJSONObject("outputCheck")?.optJSONArray("cases")
        if (cases != null) for (index in 0 until cases.length()) {
            cases.getJSONObject(index).remove("explanation")
            cases.getJSONObject(index).remove("reviewFeedback")
        }
        return result
    }

    /** Only two published normal exits and one exact CRT spelling may project
     * back onto their historical compiler state fingerprints. Runtime goals
     * remain intact in the returned lesson and still require fresh evidence.
     */
    private fun projectClosedCompilerRuntime(block: JSONObject): JSONObject {
        val current = JSONObject(block.toString())
        if (block.optString("type") != "cpp_compiler" || !current.has("outputCheck")) return current
        val withoutGoal = JSONObject(current.toString()).apply { remove("outputCheck") }
        val intentional = reviewedIntentionalExits[fingerprint(withoutGoal)]
        if (intentional != null) {
            val goal = current.getJSONObject("outputCheck")
            require(goal.keys().asSequence().toSet() == setOf("cases"))
            val cases = goal.getJSONArray("cases")
            require(cases.length() == 1)
            val case = cases.getJSONObject(0)
            require(case.keys().asSequence().toSet() ==
                setOf("standardInput", "outputs", "exitCode", "explanation"))
            val outputs = case.getJSONArray("outputs")
            require(case.getString("standardInput").isEmpty() &&
                outputs.length() == 1 && outputs.getString(0) == intentional &&
                case.get("exitCode") is Int && case.getInt("exitCode") == 1 &&
                strictString(case, "explanation").isNotBlank())
            current.remove("outputCheck")
            return current
        }
        if (fingerprint(withoutCompilerGuidance(current)) != reviewedCrtAfter) return current
        val goal = current.getJSONObject("outputCheck")
        val cases = goal.getJSONArray("cases")
        require(cases.length() == 1)
        val case = cases.getJSONObject(0)
        require(case.getString("standardInput").isEmpty())
        val outputs = case.getJSONArray("outputs")
        require(outputs.length() == 2)
        val old = outputs.getString(0)
        val spelling = outputs.getString(1)
        require(old.toByteArray(Charsets.UTF_8).size == 802 &&
            old.count { it == '\n' } == 42 && old.count { it == '\r' } == 3 &&
            spelling == old.replace("\n", "\r\n") &&
            spelling.toByteArray(Charsets.UTF_8).size == 844)
        case.put("outputs", JSONArray().put(old))
        require(fingerprint(withoutCompilerGuidance(current)) == reviewedCrtBefore)
        return current
    }

    private val requiredNonBlankStderr = mapOf(
        "cpp_fundamentals__aleatoriedad_y_reproducibilidad__combinar_ensayos_generados_y_casos_elegidos"
            to "block_0004_0007",
        "cpp_fundamentals__excepciones_y_recuperacion__limites_comprobados_y_tipos_de_captura"
            to "block_0002_0008"
    )

    private data class ReviewedDiagnostic(val beforeSha256: String, val project: Boolean)

    // Closed, source-pinned Android projections of the published diagnostics.
    // These are the same reviewed before-hashes as the Windows curricular
    // validator; a new diagnostic is NOT generally an editorial field.
    private val reviewedDiagnostics = mapOf(
        "const-member-call" to ReviewedDiagnostic("05cdb3e534649bd7e3efd1e080e5199443deace1e074d56d45490a8ff32c4c27", false),
        "explicit-copy-initialization" to ReviewedDiagnostic("ac0365d7452a3a6ff52a0f6c306d01815a09196f9f49e2bc90cdd4ec5dbc9fe9", false),
        "explicit-argument-conversion" to ReviewedDiagnostic("9f7db3f3a1004a235926bc12e06e9d0bf1a93b707f3ca92f6de2876677fd9c58", false),
        "explicit-assignment" to ReviewedDiagnostic("3e8eb728eac0d493c864596ec2364fe6c69ff85ce01408dd0020cec36ad6b087", false),
        "swapped-named-arguments" to ReviewedDiagnostic("b35436bcbac5b5e98731d953fb50499d5e7d3ad66ed1ad88771d56e481a8c9ed", false),
        "u6-difference-unselected-definition" to ReviewedDiagnostic("d17f1d73d7217ea45e1468f6ed0d1a5ef3565ecce33410a8022660baff950eba", true),
        "u6-difference-conflicting-return" to ReviewedDiagnostic("e5b83315920efce2c1a6092721096ef596e2c58d7c96089ba01075bceb3986df", true),
        "u6-difference-missing-header" to ReviewedDiagnostic("3d0f8c6e8fc84ec145f1c9a6c79ec7d5fbe50d8a70294c9f6effc0b4d8eb755c", true),
        "u6-envelopes-missing-header" to ReviewedDiagnostic("ae23ef7cc266649cbad08efc8736916121dc404e132520a0c386ea73b23d7782", true),
        "u6-envelopes-missing-declaration" to ReviewedDiagnostic("1c69caf83414219854952076e951e9adb3f9b6168503f60113a2d858103ef650", true),
        "u6-envelopes-unselected-definitions" to ReviewedDiagnostic("b263e83bb0b912281964eb60261d33713e9cb6d5df046b375f5f008544c4a405", true),
        "u6-envelopes-duplicate-definition" to ReviewedDiagnostic("72ac435d8a7399bd39c91be5a72ae2034aeb9653a8a78ec45fa3f82e6ac9470e", true),
        "u6-series-unknown-call" to ReviewedDiagnostic("b84a7b928030171e03178f3300a1fd3dbbd86388ff998fcd8ddea9d247f9effe", true),
        "u6-series-unselected-definitions" to ReviewedDiagnostic("157dc0f18f12db33cc3a7787009c408bb067ea833544df42ebdc76e2e2c00e34", true),
        "u6-series-duplicate-definition" to ReviewedDiagnostic("26416e82df9c1bd708b840b6d940c79db644f69bcd2acecc56f595f23cf48d20", true),
        "u6-shelf-unselected-definition" to ReviewedDiagnostic("2c568ff3ab456b8111cb9a338fc9c0d6f9ccff45a1e1cd3f840cee872900f7e3", true),
        "u6-shelf-conflicting-return" to ReviewedDiagnostic("5b1315f399da68b066291bf96a7f1ed8cfd1aadf3964b2d2ca55cd1f6bae50ec", true)
    )

    private fun projectReviewedDiagnostic(block: JSONObject, copy: JSONObject) {
        if (!copy.has("compileCheck")) return
        require(block.optString("type") == "cpp_compiler")
        val check = copy.getJSONObject("compileCheck")
        val keys = check.keys().asSequence().toSet()
        require(keys == setOf("diagnostic") || keys == setOf("diagnostic", "explanation"))
        val spec = reviewedDiagnostics[strictString(check, "diagnostic")]
            ?: error("Unknown reviewed compiler diagnostic")
        if (check.has("explanation"))
            require(strictString(check, "explanation").isNotBlank())
        require(!copy.has("outputCheck") && !copy.has("cmake") && !copy.has("initialStandardInput"))
        if (spec.project) require(copy.has("projectFiles") && copy.has("sourceFiles"))
        else require(!copy.has("projectFiles") && !copy.has("sourceFiles"))
        copy.remove("compileCheck")
        require(fingerprint(copy) == spec.beforeSha256) {
            "Reviewed compiler diagnostic altered the original program or project"
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
            "image" -> setOf("alt", "caption")
            "multiple_choice", "fill_code", "output_prediction", "order_code",
            "drag_fill_code", "inline_fill_code" -> setOf("question", "explanation", "reviewFeedback")
            else -> emptySet()
        }
        val copy = if (type == "cpp_compiler")
            projectClosedCompilerRuntime(block)
        else JSONObject(block.toString())
        ignored.forEach(copy::remove)
        if (type == "cpp_compiler") {
            projectReviewedDiagnostic(block, copy)
            val cases = copy.optJSONObject("outputCheck")?.optJSONArray("cases")
            if (cases != null) for (index in 0 until cases.length()) {
                cases.optJSONObject(index)?.let { case ->
                    case.remove("explanation")
                    case.remove("reviewFeedback")
                    if (case.has("standardError")) {
                        require(case.get("standardError") == "nonBlank" &&
                            !case.has("standardErrorExact"))
                        case.remove("standardError")
                    }
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
