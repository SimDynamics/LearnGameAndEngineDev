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
            require(metadata.get("version") == 1)
            require(metadata.keys().asSequence().toSet() == setOf("version", "origin", "current"))
            val original = metadata.getJSONArray("origin")
            val current = metadata.getJSONArray("current")
            val signatures = linkedMapOf<String, String>()
            val origin = (0 until original.length()).map { p ->
                val page = original.getJSONObject(p)
                require(page.keys().asSequence().toSet() == setOf("id", "blocks"))
                val blocks = page.getJSONArray("blocks")
                LessonStatePage(strictString(page, "id"), (0 until blocks.length()).map { b ->
                    val block = blocks.getJSONObject(b)
                    require(block.keys().asSequence().toSet() == setOf("id", "sha256"))
                    val id = strictString(block, "id")
                    val digest = strictString(block, "sha256")
                    require(Regex("[0-9a-f]{64}").matches(digest))
                    require(signatures.put(id, digest) == null)
                    id
                })
            }
            val positions = (0 until current.length()).map { p ->
                val page = current.getJSONObject(p)
                require(page.keys().asSequence().toSet() == setOf("id", "blocks"))
                val blocks = page.getJSONArray("blocks")
                LessonStatePage(strictString(page, "id"), (0 until blocks.length()).map { b -> (blocks.get(b) as? String ?: throw IllegalArgumentException("Non-string identity")) })
            }
            val layout = StableLessonStateLayout(origin, positions)
            val pages = lesson.getJSONArray("pages")
            require(pages.length() == positions.size)
            positions.forEachIndexed { p, position ->
                val blocks = pages.getJSONObject(p).optJSONArray("blocks") ?: JSONArray()
                require(blocks.length() == position.blocks.size)
                position.blocks.forEachIndexed { b, id ->
                    // A matching ID must not transfer an answer to a changed problem.
                    require(stateFingerprint(blocks.getJSONObject(b)) == signatures.getValue(id))
                }
            }
            return layout
        } catch (error: LessonStateLayoutException) {
            throw error
        } catch (_: Exception) {
            throw LessonStateLayoutException("Invalid or stale lesson state layout; keep saved data")
        }
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
