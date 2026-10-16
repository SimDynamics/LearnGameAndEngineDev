package com.learngraphicsprogramming.app.narration

import com.learngraphicsprogramming.app.math.MathPuzzleUiInstructions
import java.io.File
import javax.xml.parsers.DocumentBuilderFactory
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class MathPuzzleInstructionsTest {
    private val app = listOf(File("."), File("app")).first { File(it, "src/main/assets").isDirectory }

    private fun strings(directory: String = "values"): (String) -> String {
        val xml = DocumentBuilderFactory.newInstance().newDocumentBuilder()
            .parse(File(app, "src/main/res/$directory/math_ui_strings.xml"))
        val values = xml.getElementsByTagName("string")
        val text = (0 until values.length).associate { index ->
            val entry = values.item(index)
            entry.attributes.getNamedItem("name").nodeValue to entry.textContent
        }
        val keys = mapOf(
            "math_multiple_choice_single" to "math_ui_select_answer",
            "math_multiple_choice_multi" to "math_ui_select_all_answers",
            "math_ordering_drag" to "math_ui_ordering_help",
            "math_matching_drag" to "math_ui_matching_help",
            MathPuzzleUiInstructions.TILES to "math_ui_tiles",
            MathPuzzleUiInstructions.TARGETS to "math_ui_targets",
            MathPuzzleUiInstructions.EDIT_COORDINATES to "math_ui_edit_coordinates",
            MathPuzzleUiInstructions.POSITION to "math_ui_position",
            MathPuzzleUiInstructions.INTERVAL_DIRECTION to "math_ui_interval_direction",
            MathPuzzleUiInstructions.AUXILIARY_TOOL to "math_ui_auxiliary_tool",
            MathPuzzleUiInstructions.UNIT_TEMPLATE to "math_ui_unit"
        )
        return { key -> keys[key]?.let(text::get).orEmpty() }
    }

    private fun lesson(blocks: String) = JSONObject("""{"pages":[{"blocks":[$blocks]}]}""")

    @Test fun actualPageReadsTheVisibleOrderingInstructionBeforeAnyCardInAllInterfaceLanguages() {
        val source = File(app, "src/main/assets/content/es/math_physics/math_course_01/numeros_y_operaciones/lesson_magnitudes_y_numeracion.json").readText()
        val original = JSONObject(source)
        val order = original.getJSONArray("pages").getJSONObject(2).getJSONArray("blocks")
            .let { blocks -> (0 until blocks.length()).map(blocks::getJSONObject) }
            .first { it.optString("prompt").contains("Ordena de menor a mayor") }
        val one = lesson(order.toString())
        for (locale in listOf("values", "values-en", "values-fr")) {
            val ui = strings(locale)
            val spoken = LessonNarrationText.page(one, 0, NarrationLanguage.SPANISH, ui)
            val secondary = ui("math_ordering_drag")
            assertEquals("Exact visible instruction from $locale must follow its prompt", secondary, spoken[1])
            assertEquals(1, spoken.count { it == secondary })
            assertEquals("Cards follow their displayed initialOrder, independently of the ascending solution",
                listOf("ochocientos cuarenta", "ochocientos cuatro", "cuatrocientos ochenta", "cuatrocientos ocho"),
                spoken.drop(2))
            assertFalse(spoken.joinToString(" ").contains("expected"))
        }
        assertEquals(JSONObject(source).toString(), original.toString())
    }

    @Test fun authoredResponseLabelsReplaceTheNativeFallbackRatherThanAddingAnotherInstruction() {
        val ui = strings()
        val source = lesson("""{"type":"math_puzzle","prompt":"Elige.","responses":[
            {"type":"math_multiple_choice_single","label":"Compara las representaciones.",
             "options":[{"id":"a","label":"Una tarjeta"}],"validator":{"expected":"HIDDEN"}},
            {"type":"math_ordering_drag","label":"","items":[{"id":"b","label":"Otra tarjeta"}]}]}""")
        val spoken = LessonNarrationText.page(source, 0, NarrationLanguage.SPANISH, ui)
        assertTrue(spoken.contains("Compara las representaciones."))
        assertFalse(spoken.contains(ui("math_multiple_choice_single")))
        assertFalse(spoken.contains(ui("math_ordering_drag")))
        assertFalse(spoken.joinToString(" ").contains("HIDDEN"))
    }

    @Test fun matchingHelpAndSectionHeadingsFollowTheirActualVisualOrderWithoutAnswerMetadata() {
        val ui = strings()
        val source = lesson("""{"type":"math_matching_drag","prompt":"Relaciona.",
            "sources":[{"id":"SECRET_SOURCE","label":"Origen"}],
            "targets":[{"id":"SECRET_TARGET","label":"Destino"}],
            "validator":{"expected":{"SECRET_SOURCE":"SECRET_TARGET"}},
            "feedback":{"correct":"HIDDEN_SUCCESS"}}""")
        assertEquals(listOf("Relaciona.", ui("math_matching_drag"), ui(MathPuzzleUiInstructions.TILES),
            "Origen", ui(MathPuzzleUiInstructions.TARGETS), "Destino"),
            LessonNarrationText.page(source, 0, NarrationLanguage.SPANISH, ui))
    }

    @Test fun nativeGraphAndIntervalInstructionsAreIncludedForResponsesAndInteractiveTools() {
        val ui = strings()
        val source = lesson("""{"type":"math_puzzle","prompt":"Representa.",
            "tools":[{"type":"math_coordinate_graph","interactive":true,"config":{"value":123456}}],
            "responses":[{"type":"math_geometry_builder","label":"Dibuja.","validator":{"expected":"HIDDEN"}},
             {"type":"math_number_line","label":"Marca.","value":123456,"validator":{"expected":123456}}]}""")
        assertEquals(listOf("Representa.", ui(MathPuzzleUiInstructions.AUXILIARY_TOOL),
            ui(MathPuzzleUiInstructions.EDIT_COORDINATES), "Dibuja.", ui(MathPuzzleUiInstructions.EDIT_COORDINATES),
            "Marca.", ui(MathPuzzleUiInstructions.POSITION), ui(MathPuzzleUiInstructions.INTERVAL_DIRECTION)),
            LessonNarrationText.page(source, 0, NarrationLanguage.SPANISH, ui))
    }
}
