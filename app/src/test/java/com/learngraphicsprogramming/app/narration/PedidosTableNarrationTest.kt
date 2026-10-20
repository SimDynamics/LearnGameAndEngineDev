package com.learngraphicsprogramming.app.narration

import java.io.File
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class PedidosTableNarrationTest {
    private fun lesson(block: JSONObject) = JSONObject().put("pages", JSONArray()
        .put(JSONObject().put("blocks", JSONArray().put(block))))

    private fun paragraph(text: String) = lesson(JSONObject().put("type", "paragraph").put("text", text))

    private fun published(): JSONObject {
        val resources = File(System.getProperty("translation.resources") ?: "src/main/res").absoluteFile
        return JSONObject(requireNotNull(resources.parentFile).resolve(
            "assets/content/es/programming/cpp_fundamentals/datos_propios_y_estado_valido/" +
                "lesson_construir_y_comprobar_un_modelo_de_pedidos.json").readText())
    }

    /** Same 24 passive native labels captured from the actual page: prompt, three
     * headings, five rows with three labels, five dropdown labels. No answer data. */
    private fun publishedNativeLabels(block: JSONObject): List<Pair<Int, CharSequence>> = buildList {
        add(4 to block.getString("prompt"))
        var target = Int.MIN_VALUE
        fun label(value: String) { if (value.isNotBlank()) add(target++ to value) }
        val responses = block.getJSONArray("responses")
        for (index in 0 until responses.length()) {
            val response = responses.getJSONObject(index)
            label(response.optString("label"))
            val definitions = response.getJSONArray("fields")
            val fields = (0 until definitions.length()).map(definitions::getJSONObject).associateBy { it.getString("id") }
            if (response.optString("layout") == "table") {
                val headers = response.getJSONArray("headers")
                repeat(headers.length()) { label(headers.getString(it)) }
                val rows = response.getJSONArray("rows")
                repeat(rows.length()) { rowIndex ->
                    val cells = rows.getJSONArray(rowIndex)
                    repeat(cells.length()) { cellIndex ->
                        val cell = cells.optJSONObject(cellIndex)
                        label(if (cell?.has("field") == true) fields.getValue(cell.getString("field")).getString("label")
                            else cells.getString(cellIndex))
                    }
                }
            } else fields.values.forEach { label(it.getString("label")) }
        }
    }

    private fun assertCompleteGlyphOwnership(views: List<Pair<Int, CharSequence>>, mappings: Map<Int, NarrationWordMapping>) {
        assertEquals(views.map { it.first }.toSet(), mappings.values.flatMap { it.targets!! }.toSet())
        for ((target, source) in views) {
            val owned = mappings.values.flatMap { mapping -> mapping.displayed.indices
                .filter { mapping.targets!![it] == target }.flatMap { index ->
                    mapping.displayed[index].let { it.start until it.endExclusive }
                } }.toSet()
            assertEquals("Every actual glyph has its own monotone spoken owner: $source", source.indices.toSet(), owned)
        }
    }

    @Test fun theActualPedidosPageMapsAllTwentyFourNativeLabelsIncludingEveryTechnicalRow() {
        val source = published()
        val original = source.toString()
        val block = source.getJSONArray("pages").getJSONObject(1).getJSONArray("blocks").getJSONObject(4)
        val views = publishedNativeLabels(block)
        assertEquals(24, views.size)
        val script = LessonNarrationScript.page(source, 1, NarrationLanguage.SPANISH, true)
        val parts = NarrationSegments.sentences(script.texts)
        val plan = requireNotNull(script.readingPlan(NarrationEngine.KOKORO))
        val segments = parts.indices.filter { plan.origins[it] == NarrationTextOrigin(4) }.associateWith { parts[it] }
        val mappings = NarrationPuzzleWordMappings.forViews(4, views, segments, NarrationLanguage.SPANISH)
        assertEquals("No real cell needs a guessed/partial fallback", segments.keys, mappings.keys)
        assertCompleteGlyphOwnership(views, mappings)
        assertEquals(original, source.toString())
    }

    @Test fun technicalTableCellsUseTheirStandaloneSpeechInAllFourLanguagesWithoutInventingSeparators() {
        for (language in NarrationLanguage.entries) {
            val cells = listOf("Cell label", "std::cout", "return 0;")
            val block = JSONObject().put("type", "math_puzzle").put("prompt", "Observe.")
                .put("responses", JSONArray().put(JSONObject().put("type", "math_multi_input").put("layout", "table")
                    .put("rows", JSONArray().put(JSONArray().put(JSONObject().put("field", "value"))
                        .put(cells[1]).put(cells[2])))
                    .put("fields", JSONArray().put(JSONObject().put("id", "value").put("type", "numeric").put("label", cells[0])))))
            val source = lesson(block)
            val original = source.toString()
            val expected = (listOf("Observe.") + cells).flatMap {
                LessonNarrationText.page(paragraph(it), 0, language, true)
            }
            val actual = LessonNarrationText.page(source, 0, language, true)
            assertEquals("Each original cell keeps the existing normalizer and pronunciation in $language", expected, actual)
            val parts = NarrationSegments.sentences(actual)
            val mappings = NarrationPuzzleWordMappings.forViews(8, (listOf("Observe.") + cells)
                .mapIndexed { index, cell -> (70 + index) to cell }, parts.indices.associateWith { parts[it] }, language)
            assertEquals(parts.indices.toSet(), mappings.keys)
            assertCompleteGlyphOwnership((listOf("Observe.") + cells).mapIndexed { index, cell -> (70 + index) to cell }, mappings)
            assertEquals(original, source.toString())
        }
    }

    @Test fun ordinaryAndAuthoredTechnicalSemicolonsRemainIndependentInsideDifferentCells() {
        for (language in NarrationLanguage.entries) {
            val ordinary = when (language) {
                NarrationLanguage.SPANISH -> "Primero abre la ventana; después ciérrala."
                NarrationLanguage.FRENCH -> "Ouvre la fenêtre ; puis ferme-la."
                else -> "Open the window; then close it."
            }
            val technical = "`int x = 7;`"
            val block = JSONObject().put("type", "math_multi_input").put("layout", "table")
                .put("rows", JSONArray().put(JSONArray().put(ordinary).put(technical)))
                .put("fields", JSONArray().put(JSONObject().put("id", "unused").put("type", "numeric").put("label", "Extra")))
            val actual = LessonNarrationText.page(lesson(block), 0, language, true)
            val ordinarySpeech = LessonNarrationText.page(paragraph(ordinary), 0, language, true)
            val technicalSpeech = LessonNarrationText.page(paragraph(technical), 0, language, true)
            assertEquals(ordinarySpeech + technicalSpeech + LessonNarrationText.page(paragraph("Extra"), 0, language, true), actual)
            val plain = ManualEnglishSpeech.strip(ordinarySpeech.joinToString(" ")).lowercase()
            assertFalse(plain.contains("punto y coma") || plain.contains("point-virgule") || plain.contains("semicolon"))
            if (language == NarrationLanguage.SPANISH)
                assertTrue("A real C++ statement still pronounces its authored terminator", technicalSpeech.joinToString(" ").contains("punto y coma"))
            if (language == NarrationLanguage.AMERICAN_ENGLISH || language == NarrationLanguage.BRITISH_ENGLISH) {
                val terminator = technical.indexOf(';')
                assertTrue("The explicit code terminator keeps its exact original source atom",
                    EnglishTechnicalSpeech.replacements(technical).any {
                        it.start == terminator && it.endExclusive == terminator + 1 && it.spoken == "semicolon"
                    })
                assertTrue("The prose separator keeps its ordinary punctuation reading",
                    EnglishTechnicalSpeech.replacements(ordinary).none { it.spoken == "semicolon" })
            }
        }
    }
}
