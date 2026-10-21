package com.learngraphicsprogramming.app

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject

/** Purpose-built test documents, never replacements for the curricular assets. */
internal object LocalizedStableLessonQaFixture {
    const val COURSE = "localized_state_fixture"
    const val UNIT = "localized_state_fixture__order"
    const val LESSON = "localized_state_fixture__order__localized"
    const val ROUTE = "programming"
    const val ADDITION = "original_execution_qa"
    const val REAL_LESSON = "cpp_fundamentals__programas_y_ejecucion__primer_programa_y_salida"
    const val REAL_PATH = "programming/cpp_fundamentals/programas_y_ejecucion/lesson_primer_programa_y_salida.json"
    val languages = listOf("es", "en", "fr")

    fun copy(json: JSONObject) = JSONObject(json.toString())

    fun realFamily(context: Context): Map<String, JSONObject> = languages.associateWith { language ->
        JSONObject(context.assets.open("content/$language/$REAL_PATH").bufferedReader().use { it.readText() })
    }

    fun minimalFamily(): Map<String, JSONObject> = languages.associateWith(::minimalOriginal)

    fun minimalOriginal(language: String): JSONObject {
        val counts = if (language == "es") listOf(5, 5, 6, 4, 5, 9) else listOf(5, 5, 6, 4, 5, 4)
        val pages = JSONArray()
        counts.forEachIndexed { page, count ->
            val blocks = JSONArray()
            repeat(count) { block ->
                blocks.put(JSONObject().put("type", if (block == 0) "heading" else "paragraph")
                    .put("text", "QA $language ${page + 1}.${block + 1}."))
            }
            pages.put(JSONObject().put("blocks", blocks))
        }
        fun put(page: Int, block: Int, value: JSONObject) {
            pages.getJSONObject(page).getJSONArray("blocks").put(block, value)
        }
        fun choice(id: String) = JSONObject().put("type", "multiple_choice").put("id", id)
            .put("question", "QA $language choice $id?").put("options", JSONArray(listOf("A", "B"))).put("correctIndex", 0)
        fun prediction(id: String) = JSONObject().put("type", "output_prediction").put("id", id)
            .put("question", "QA $language prediction?").put("code", "std::cout << 7;")
            .put("answers", JSONArray(listOf("7")))
        fun fill(id: String) = JSONObject().put("type", "fill_code").put("id", id)
            .put("question", "QA $language fill $id?").put("answers", JSONArray(listOf("<<")))
        put(0, 4, choice("first_choice"))
        put(1, 3, prediction("first_prediction")); put(1, 4, fill("first_fill"))
        put(2, 3, choice("entry_choice")); put(2, 4, fill("operator_fill")); put(2, 5, fill("terminator_fill"))
        put(3, 2, JSONObject().put("type", "order_code").put("id", "ordered_code")
            .put("question", "QA $language order?").put("fragments", JSONArray(listOf("int a = 1;", "a++;")))
            .put("correctOrder", JSONArray(listOf(0, 1))))
        put(3, 3, choice("return_choice"))
        val compiler = JSONObject().put("type", "cpp_compiler").put("initialCode", "int main() { return 0; }\n")
        put(4, 2, copy(compiler))
        put(4, 3, if (language == "es") prediction("late_prediction") else
            JSONObject().put("type", "note").put("text", "QA $language note, not a prediction."))
        put(4, 4, choice("last_choice"))
        if (language == "es") put(5, 6, copy(compiler))
        return JSONObject().put("id", LESSON).put("number", 1).put("title", "Localized state QA")
            .put("pages", pages)
    }

    fun origin(original: JSONObject): JSONArray = JSONArray().also { result ->
        val pages = original.getJSONArray("pages")
        repeat(pages.length()) { page ->
            val blocks = pages.getJSONObject(page).getJSONArray("blocks")
            val entries = JSONArray()
            repeat(blocks.length()) { block -> entries.put(JSONObject().put("id", blockId(page, block))
                .put("sha256", LessonStateLayoutJson.stateFingerprint(blocks.getJSONObject(block)))) }
            result.put(JSONObject().put("id", pageId(page)).put("blocks", entries))
        }
    }

    /** Mirrors a verified authoring result; does not relax the production parser. */
    fun mapped(family: Map<String, JSONObject>, language: String = "es", secondPermutation: Boolean = false): JSONObject {
        val original = family.getValue(language)
        if (original.has("stateLayout")) return movePublished(original, secondPermutation)
        val result = copy(original)
        val pages = result.getJSONArray("pages")
        val current = JSONArray()
        repeat(pages.length()) { page ->
            current.put(JSONObject().put("id", pageId(page)).put("blocks", JSONArray().also { ids ->
                repeat(pages.getJSONObject(page).getJSONArray("blocks").length()) { ids.put(blockId(page, it)) }
            }))
        }
        val fifth = pages.getJSONObject(4).getJSONArray("blocks")
        val oldParagraph = copy(fifth.getJSONObject(1))
        val paragraph = copy(oldParagraph).put("text", oldParagraph.getString("text") + " QA editorial clarification.")
        val addedCompiler = copy(fifth.getJSONObject(2)).apply {
            // An independent activity retains one objective, distinct from the existing editor.
            optJSONObject("outputCheck")?.let { check ->
                val first = check.getJSONArray("cases").getJSONObject(0)
                first.put("outputs", JSONArray(listOf("Zona inicial")))
                first.remove("explanation"); first.remove("reviewFeedback")
            }
        }
        pages.getJSONObject(4).put("blocks", JSONArray().put(fifth.getJSONObject(0))
            .put(fifth.getJSONObject(3)).put(paragraph).put(addedCompiler)
            .put(fifth.getJSONObject(2)).put(fifth.getJSONObject(4)))
        current.getJSONObject(4).put("blocks", JSONArray(listOf(blockId(4, 0), blockId(4, 3),
            blockId(4, 1), ADDITION, blockId(4, 2), blockId(4, 4))))
        if (secondPermutation) {
            val firstPage = pages.getJSONObject(4); val secondPage = pages.getJSONObject(5)
            pages.put(4, secondPage); pages.put(5, firstPage)
            val firstMap = current.getJSONObject(4); val secondMap = current.getJSONObject(5)
            current.put(4, secondMap); current.put(5, firstMap)
        }
        val familyMetadata = JSONObject()
        languages.forEach { locale ->
            val source = copy(family.getValue(locale)).apply { remove("stateLayout") }
            familyMetadata.put(locale, JSONObject().put("sha256", LessonStateLayoutJson.fingerprint(source))
                .put("origin", origin(source)))
        }
        val metadata = JSONObject().put("version", 2).put("locale", language).put("origin", origin(original))
            .put("current", current).put("family", familyMetadata)
            .put("additions", JSONArray().put(JSONObject().put("id", ADDITION)
                .put("sha256", LessonStateLayoutJson.stateFingerprint(addedCompiler))
                .put("storage", JSONObject().put("page", 6).put("block", 0)).put("progressPage", pageId(4))))
            .put("editorial", JSONArray().put(JSONObject().put("id", blockId(4, 1))
                .put("beforeSha256", LessonStateLayoutJson.fingerprint(oldParagraph))
                .put("afterSha256", LessonStateLayoutJson.fingerprint(paragraph))))
        return result.put("stateLayout", metadata)
    }

    /** A later publication moves the actual published layout, never rebases its historical family. */
    private fun movePublished(original: JSONObject, secondPermutation: Boolean): JSONObject {
        requireNotNull(LessonStateLayoutJson.read(original))
        val result = copy(original)
        if (secondPermutation) {
            val pages = result.getJSONArray("pages")
            val current = result.getJSONObject("stateLayout").getJSONArray("current")
            val fifth = pages.getJSONObject(4); val sixth = pages.getJSONObject(5)
            pages.put(4, sixth); pages.put(5, fifth)
            val fifthRecord = current.getJSONObject(4); val sixthRecord = current.getJSONObject(5)
            current.put(4, sixthRecord); current.put(5, fifthRecord)
        }
        requireNotNull(LessonStateLayoutJson.read(result))
        return result
    }

    fun pageId(page: Int) = "page_%04d".format(java.util.Locale.ROOT, page)
    fun blockId(page: Int, block: Int) = "block_%04d_%04d".format(java.util.Locale.ROOT, page, block)
}
