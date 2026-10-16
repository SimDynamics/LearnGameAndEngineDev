package com.learngraphicsprogramming.app.narration

import com.learngraphicsprogramming.app.math.MathPuzzleTypes
import com.learngraphicsprogramming.app.CodeExerciseLanguage
import org.json.JSONArray
import org.json.JSONObject

/** Extracts only authored visible content. Validator/feedback trees are never traversed. */
object LessonNarrationText {
    private const val MAX_SEGMENT_LENGTH = 700

    /** Preserve callers that supplied the instruction callback positionally. */
    fun page(lesson: JSONObject, pageIndex: Int, language: NarrationLanguage,
             visibleInstruction: (String) -> String): List<String> =
        page(lesson, pageIndex, language, false, visibleInstruction)

    fun page(
        lesson: JSONObject,
        pageIndex: Int,
        language: NarrationLanguage = NarrationLanguage.SPANISH,
        forKokoro: Boolean = false,
        visibleInstruction: (String) -> String = { "" }
    ): List<String> =
        Extractor(language, forKokoro, visibleInstruction).page(lesson, pageIndex)

    private class Extractor(
        private val speechLanguage: NarrationLanguage,
        private val forKokoro: Boolean,
        private val visibleInstruction: (String) -> String
    ) {
    private fun localized(spanish: String, english: String, french: String): String = when (speechLanguage) {
        NarrationLanguage.SPANISH -> spanish
        NarrationLanguage.FRENCH -> french
        else -> english
    }

    private val blank: String get() = localized("hueco", "blank", "case vide")

    fun page(lesson: JSONObject, pageIndex: Int): List<String> {
        val page = lesson.optJSONArray("pages")?.optJSONObject(pageIndex) ?: return emptyList()
        val output = mutableListOf<String>()
        if (pageIndex == 0) append(output, lesson.string("title"))
        objects(page.optJSONArray("blocks")).forEach { block(output, it) }
        return output.flatMap(::segments)
    }

    private fun block(output: MutableList<String>, block: JSONObject) {
        val type = block.string("type")
        when (type) {
            "heading", "paragraph", "note" -> append(output, block.string("text"), block.mode())
            "code" -> output.add(localized("Ejemplo de código", "Example of", "Exemple de code") +
                " ${codeLanguage(block.string("language"))}" + localized(".", " code.", "."))
            "cpp_compiler" -> output.add(localized("Editor de código C más más.", "C plus plus code editor.", "Éditeur de code C plus plus."))
            "multiple_choice" -> {
                append(output, block.string("question"))
                options(output, block.optJSONArray("options"))
            }
            "fill_code", "inline_fill_code", "output_prediction", "order_code", "drag_fill_code" -> {
                append(output, block.string("question"))
                // The order/drag exercises show an additional instruction
                // below the heading, built from Android UI string resources.
                // Its absence from the JSON is NOT a reason to skip reading it.
                append(output, visibleInstruction(type))
                // Templates, source code, expected output and solutions remain on screen.
                val language = if (type == "order_code") CodeExerciseLanguage.order(block).contentName
                    else block.string("language")
                output.add(localized("Ejercicio de código", "Code exercise in", "Exercice de code") + " ${codeLanguage(language)}.")
            }
            "math_puzzle" -> {
                append(output, block.string("prompt").ifBlank { block.string("question") }, block.mode())
                objects(block.optJSONArray("representations")).forEach { representation(output, it) }
                objects(block.optJSONArray("tools")).forEach {
                    if (it.optBoolean("interactive", false)) append(output, it.string("label"))
                    else representation(output, it)
                }
                objects(block.optJSONArray("responses")).forEach { response(output, it) }
            }
            else -> if (type in MathPuzzleTypes.responseTypes) {
                append(output, block.string("prompt").ifBlank { block.string("question") }, block.mode())
                objects(block.optJSONArray("representations")).forEach { representation(output, it) }
                response(output, block)
            }
        }
    }

    private fun response(output: MutableList<String>, response: JSONObject) {
        append(output, response.string("label"), response.mode())
        val fields = objects(response.optJSONArray("fields")).associateBy { it.string("id") }
        val segments = response.optJSONArray("segments")
        val table = response.optJSONArray("rows").takeIf { response.string("layout") == "table" }
        when {
            segments != null -> {
                val content = (0 until segments.length()).mapNotNull { index ->
                    val segment = segments.optJSONObject(index) ?: return@mapNotNull null
                    if (segment.has("field")) fields[segment.string("field")]?.let(::fieldText) ?: blank
                    else segment.string("text")
                }.joinToString(" ")
                append(output, content, "inline")
            }
            table != null -> {
                strings(output, response.optJSONArray("headers"))
                for (rowIndex in 0 until table.length()) {
                    val cells = table.optJSONArray(rowIndex) ?: continue
                    val content = (0 until cells.length()).map { index ->
                        val cell = cells.optJSONObject(index)
                        if (cell?.has("field") == true) fields[cell.string("field")]?.let(::fieldText) ?: blank
                        else scalar(cells.opt(index))
                    }.joinToString("; ")
                    append(output, content)
                }
            }
            else -> fields.values.forEach { append(output, fieldText(it), it.mode()) }
        }
        // Lists are visible choices/cards; their IDs and expected order have no speech meaning.
        options(output, response.optJSONArray("options"))
        for (key in listOf("items", "sources", "targets")) {
            objects(response.optJSONArray(key)).forEach { rich(output, it) }
        }
        objects(response.optJSONArray("parameters")).forEach { append(output, it.string("label")) }
        append(output, response.string("unit"))
    }

    private fun fieldText(field: JSONObject): String = if (field.string("type") == "fixed") {
        field.string("text")
    } else field.string("label").ifBlank { blank } + field.string("unit").let { if (it.isBlank()) "" else " $it" }

    private fun options(output: MutableList<String>, options: JSONArray?) {
        if (options == null) return
        for (index in 0 until options.length()) {
            val option = options.optJSONObject(index)
            val prefix = localized("Opción", "Option", "Option") + " ${index + 1}."
            output.add(prefix)
            if (option != null) rich(output, option) else append(output, scalar(options.opt(index)))
        }
    }

    private fun rich(output: MutableList<String>, item: JSONObject) {
        val content = item.optJSONArray("content")
        if (content == null || content.length() == 0) {
            append(output, item.string("label").ifBlank { item.string("text") }, item.mode())
        } else {
            for (index in 0 until content.length()) {
                val itemSpec = content.optJSONObject(index)
                if (itemSpec != null) representation(output, itemSpec)
                else append(output, scalar(content.opt(index)))
            }
        }
        // MathWidgetUi.rich draws this explanation immediately, unlike block feedback.
        append(output, item.string("explanation"), item.mode())
    }

    private fun representation(output: MutableList<String>, spec: JSONObject) {
        val config = spec.optJSONObject("config") ?: spec
        when (spec.string("type")) {
            "text" -> append(output, config.string("text").ifBlank { config.string("value") }, config.mode())
            "math_formula" -> {
                val source = listOf("latex", "text", "formula", "value").firstNotNullOfOrNull {
                    config.string(it).takeIf(String::isNotBlank)
                }.orEmpty()
                append(output, source, "block")
            }
            "math_table" -> {
                strings(output, config.optJSONArray("headers"))
                val rows = config.optJSONArray("rows") ?: return
                for (index in 0 until rows.length()) strings(output, rows.optJSONArray(index))
            }
            "rich_answer_option" -> rich(output, config)
            "geometry_2d_diagram", "geometry_3d_diagram" -> {
                append(output, config.string("label"))
                // The renderer draws at most three labels; hidden geometric coordinates are omitted.
                strings(output, config.optJSONArray("labels"), 3)
            }
            "venn_diagram" -> {
                for (key in listOf("universeLabel", "leftLabel", "leftOnly", "intersection", "rightLabel", "rightOnly", "outside")) {
                    append(output, config.string(key))
                }
            }
            "math_coordinate_graph", "coordinate_graph", "math_number_line" -> {
                // Values may deliberately be hidden (e.g. showValueLabel=false in Course 01).
                append(output, config.string("label"))
                append(output, config.string("description"))
            }
        }
    }

    private fun append(output: MutableList<String>, source: String, mathMode: String = "auto") {
        // Recognize quoted/embedded C++ before math can reinterpret its operators.
        // Visible lesson content remains unchanged; only its spoken representation differs.
        val contextual = if (speechLanguage == NarrationLanguage.SPANISH) {
            if (forKokoro) SpanishTechnicalSpeech.prepareForKokoro(source, mathMode).text
            else SpanishTechnicalSpeech.prepare(source, mathMode).text
        } else if (forKokoro && speechLanguage == NarrationLanguage.FRENCH) {
            FrenchTechnicalSpeech.prepareForKokoro(source, mathMode)
        } else source
        val spoken = if (forKokoro && (speechLanguage == NarrationLanguage.SPANISH || speechLanguage == NarrationLanguage.FRENCH))
            ManualEnglishSpeech.protect(contextual) { MultilingualMathSpeech.text(it, mathMode, speechLanguage) }
        else MultilingualMathSpeech.text(contextual, mathMode, speechLanguage)
        if (spoken.isNotBlank()) output.add(spoken)
    }

    private fun strings(output: MutableList<String>, values: JSONArray?, limit: Int = Int.MAX_VALUE) {
        if (values == null) return
        val spoken = (0 until minOf(values.length(), limit)).map { scalar(values.opt(it)) }
            .filter(String::isNotBlank).joinToString("; ")
        append(output, spoken)
    }

    private fun objects(array: JSONArray?): List<JSONObject> = if (array == null) emptyList()
        else (0 until array.length()).mapNotNull(array::optJSONObject)

    private fun JSONObject.string(key: String): String = opt(key) as? String ?: ""
    private fun JSONObject.mode(): String = string("mathMode").ifBlank { "auto" }
    private fun scalar(value: Any?): String = when (value) {
        is String, is Number -> value.toString()
        else -> ""
    }

    private fun codeLanguage(value: String): String = when (value.lowercase()) {
        "", "cpp", "c++" -> localized("C más más", "C plus plus", "C plus plus")
        "cmake" -> "CMake"
        else -> value
    }

    private fun segments(text: String): List<String> {
        if (text.startsWith(ManualEnglishSpeech.PREPARED)) {
            return ManualEnglishSpeech.segment(text, ::segmentsPlain)
        }
        return segmentsPlain(text)
    }

    private fun segmentsPlain(text: String): List<String> {
        val output = mutableListOf<String>()
        var remaining = text.trim()
        while (remaining.length > MAX_SEGMENT_LENGTH) {
            val window = remaining.take(MAX_SEGMENT_LENGTH + 1)
            val sentenceEnd = Regex("[.!?;](?:\\s|$)").findAll(window)
                .lastOrNull { it.range.first < MAX_SEGMENT_LENGTH && it.range.last >= MAX_SEGMENT_LENGTH / 3 }?.range?.first?.plus(1)
            var cut = sentenceEnd ?: window.lastIndexOf(' ').takeIf { it > 0 } ?: MAX_SEGMENT_LENGTH
            if (cut > 0 && Character.isHighSurrogate(remaining[cut - 1])) cut--
            output.add(remaining.substring(0, cut).trim())
            remaining = remaining.substring(cut).trim()
        }
        if (remaining.isNotEmpty()) output.add(remaining)
        return output
    }
    }
}
