package com.learngraphicsprogramming.app.narration

import com.learngraphicsprogramming.app.math.MathPuzzleUiInstructions
import java.io.File
import java.util.Locale
import javax.xml.parsers.DocumentBuilderFactory
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class VisiblePuzzleLabelsTest {
    private val app = listOf(File("."), File("app")).first { File(it, "src/main/assets").isDirectory }

    private fun unitTemplate(language: NarrationLanguage): String {
        val directory = when (language) {
            NarrationLanguage.SPANISH -> "values"
            NarrationLanguage.FRENCH -> "values-fr"
            else -> "values-en"
        }
        val nodes = DocumentBuilderFactory.newInstance().newDocumentBuilder()
            .parse(File(app, "src/main/res/$directory/math_ui_strings.xml")).getElementsByTagName("string")
        return (0 until nodes.length).map(nodes::item).single {
            it.attributes.getNamedItem("name").nodeValue == "math_ui_unit"
        }.textContent
    }

    private fun ui(language: NarrationLanguage): (String) -> String = { key ->
        if (key == MathPuzzleUiInstructions.UNIT_TEMPLATE) unitTemplate(language) else ""
    }

    private fun lesson(block: JSONObject) = JSONObject().put("pages", JSONArray()
        .put(JSONObject().put("blocks", JSONArray().put(block))))

    @Test fun publishedFifthPageReadsTheCompleteUnitCaptionAndPreservesEveryExistingSourceField() {
        for (language in NarrationLanguage.entries) {
            val file = File(app, "src/main/assets/content/${language.resourceGroup}/math_physics/math_course_01/numeros_y_operaciones/lesson_magnitudes_y_numeracion.json")
            val original = JSONObject(file.readText())
            val before = original.toString()
            val block = original.getJSONArray("pages").getJSONObject(4).getJSONArray("blocks").getJSONObject(1)
            val response = block.getJSONArray("responses").getJSONObject(0)
            assertEquals("cm", response.getString("unit"))
            val caption = String.format(Locale.ROOT, unitTemplate(language), "cm")
            val resolver = ui(language)
            assertEquals(caption, MathPuzzleUiInstructions.textUnit(response.getString("type"), response, resolver))
            assertTrue(MathPuzzleUiInstructions.visible(response.getString("type"), response, resolver).contains(caption))
            val current = LessonNarrationText.page(lesson(block), 0, language, true, resolver)
            val preceding = LessonNarrationText.page(lesson(block), 0, language, true, false, resolver)
            assertEquals("Only the actual changed caption requires new audio in $language", preceding.dropLast(1), current.dropLast(1))
            assertEquals("cm", ManualEnglishSpeech.strip(preceding.last()))
            // The existing speech normalizer removes the French space before a colon;
            // the native caption and the next test's glyph owners retain that space.
            val spokenCaption = if (language == NarrationLanguage.FRENCH) caption.replace(" :", ":") else caption
            assertEquals(spokenCaption, ManualEnglishSpeech.strip(current.last()))
            assertEquals(3, current.size)
            assertEquals(before, original.toString())
        }
    }

    @Test fun theUnitHeadingAndItsUnitKeepIndependentGlyphOwnersInAllFourLanguages() {
        for (language in NarrationLanguage.entries) {
            val caption = String.format(Locale.ROOT, unitTemplate(language), "cm")
            val source = JSONObject().put("type", "math_numeric_input").put("prompt", "Observe.")
                .put("label", "Measure").put("unit", "cm").put("hint", "DO_NOT_READ_THE_HINT")
                .put("validator", JSONObject().put("expected", "DO_NOT_READ_THE_EXPECTED_VALUE"))
            val texts = LessonNarrationText.page(lesson(source), 0, language, true, ui(language))
            val parts = NarrationSegments.sentences(texts)
            val mapping = NarrationPuzzleWordMappings.forViews(8,
                listOf(80 to "Observe.", 81 to "Measure", 82 to caption),
                parts.indices.associateWith { parts[it] }, language)
            assertEquals(parts.indices.toSet(), mapping.keys)
            val owned = mapping.values.flatMap { item -> item.displayed.indices.filter { item.targets!![it] == 82 }
                .map { item.displayed[it] } }
            assertTrue("The heading must not wait for the unit to finish in $language", owned.distinct().size >= 2)
            assertEquals(caption.indices.toSet(), owned.flatMap { it.start until it.endExclusive }.toSet())
            assertFalse(ManualEnglishSpeech.strip(texts.joinToString(" ")).contains("DO_NOT_READ"))
        }
    }

    @Test fun addingThePublishedUnitCaptionRelocatesOnlyItsExistingBoundedPagePosition() {
        for (language in NarrationLanguage.entries) {
            val file = File(app, "src/main/assets/content/${language.resourceGroup}/math_physics/math_course_01/numeros_y_operaciones/lesson_magnitudes_y_numeracion.json")
            val original = JSONObject(file.readText())
            fun presentation(complete: Boolean) = LessonNarrationScript.presentedPageVersioned(
                original, 4, language, true, ui(language), complete) { _, block -> block }
            val before = presentation(false)
            val after = presentation(true)
            for (engine in NarrationEngine.entries) {
                fun parts(script: LessonNarrationScript) = if (engine == NarrationEngine.ANDROID)
                    NarrationSegments.prepare(script.texts, 250) else NarrationSegments.sentences(script.texts)
                val oldParts = parts(before)
                val newParts = parts(after)
                val unchanged = oldParts.indices.first { index -> oldParts[index] in newParts && index > 0 }
                val point = NarrationResumePoint(unchanged, 1_537)
                val history = NarrationResumeHistory(listOf(
                    NarrationResumeEntry("newer-page", NarrationResumePoint(2, 200)),
                    NarrationResumeEntry("old-page-five", point),
                    NarrationResumeEntry("older-finished-page", NarrationResumePoint(3, 400, finished = true))))
                val moved = NarrationResumePolicy.relocate(history, "old-page-five", "new-page-five") {
                    NarrationPresentationRemap.remap(it, before, after, engine)
                }
                assertEquals(listOf("newer-page", "new-page-five", "older-finished-page"), moved.entries.map { it.key })
                assertEquals(history.entries.first(), moved.entries.first())
                assertEquals(history.entries.last(), moved.entries.last())
                val restored = moved.entries[1].point
                assertEquals(oldParts[unchanged], newParts[restored.segment])
                assertEquals(1_537L, restored.positionMs)
                val end = requireNotNull(NarrationPresentationRemap.remap(
                    NarrationResumePoint(oldParts.lastIndex, 2_719, finished = true), before, after, engine))
                assertEquals(NarrationResumePoint(newParts.lastIndex, 2_719, finished = true), end)
            }
        }
    }

    @Test fun tableFieldsReadTheirVisibleFixedLabelsAndUnitsThenEveryAppendedExtraField() {
        val source = JSONObject("""{"type":"math_puzzle","prompt":"Completa.","responses":[
            {"type":"math_multi_input","label":"Datos","layout":"table","headers":["Referencia"],
             "rows":[[{"field":"known"}]],"fields":[
              {"id":"known","type":"fixed","label":"Medida conocida","text":"dos","unit":"cm"},
              {"id":"extra","type":"numeric","label":"Resultado adicional","unit":"m","hint":"HIDDEN_HINT"},
              {"id":"extra_fixed","type":"fixed","label":"Constante","text":"pi","unit":"rad"}],
             "validator":{"expected":"HIDDEN_EXPECTED"}}]}""")
        val before = source.toString()
        assertEquals(listOf("Completa.", "Datos", "Referencia", "Medida conocida dos cm", "Resultado adicional m", "Constante pi rad"),
            LessonNarrationText.page(lesson(source), 0))
        assertEquals("The preceding presentation remains reconstructible for page-position recovery",
            listOf("Completa.", "Datos", "Referencia", "dos"),
            LessonNarrationText.page(lesson(source), 0, NarrationLanguage.SPANISH, false, false) { "" })
        assertEquals(before, source.toString())
    }

    @Test fun partialInlineTemplatesKeepTheirExistingBlankSemanticsAndReadAppendedFixedTextWithoutHiddenLabels() {
        val source = JSONObject("""{"type":"math_equation_fill_blanks","prompt":"Completa.",
            "segments":[{"text":"5 + "},{"field":"answer"},{"text":" = 10"}],"fields":[
             {"id":"answer","type":"numeric","label":"resultado","hint":"HIDDEN_HINT"},
             {"id":"extra","type":"fixed","label":"HIDDEN_INLINE_LABEL","text":"Constante","unit":"metros"}],
             "validator":{"expected":"HIDDEN_EXPECTED"}}""")
        assertEquals(listOf("Completa.", "cinco más resultado es igual a diez", "Constante metros"),
            LessonNarrationText.page(lesson(source), 0))
    }

    @Test fun richRepresentationsReadOnlyRenderedContentAndVisibleFallbacksRespectExplicitEmptyLabels() {
        val source = JSONObject("""{"type":"math_puzzle","prompt":"Observa.","representations":[
            {"type":"text","text":"","value":"HIDDEN_TEXT_OVERRIDE"},
            {"type":"rich_answer_option","label":"HIDDEN_REP_LABEL","explanation":"HIDDEN_REP_EXPLANATION","content":[
              {"text":"Texto visible sin tipo"},
              {"type":"math_formula","latex":"","text":"HIDDEN_FORMULA_OVERRIDE"},
              {"type":"text","value":"Texto por value"}]}],"responses":[
             {"type":"math_multiple_choice_single","label":"Escoge","options":[
              {"id":"Tarjeta"},{"id":"private_id","label":"","text":"HIDDEN_EMPTY_OVERRIDE"}],
              "validator":{"expected":"private_id"}}]}""")
        assertEquals(listOf("Observa.", "Texto visible sin tipo", "Texto por value", "Escoge", "Opción 1.", "Tarjeta", "Opción 2."),
            LessonNarrationText.page(lesson(source), 0))
    }
}
