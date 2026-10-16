package com.learngraphicsprogramming.app.narration

import java.io.File
import java.util.Locale
import javax.xml.parsers.DocumentBuilderFactory
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class NoteNarrationTextTest {
    private val app = listOf(File("."), File("app")).first { File(it, "src/main/assets").isDirectory }

    /** Read the existing resource used by addNote, including its actual line breaks. */
    private fun template(language: NarrationLanguage): String {
        val directory = when (language) {
            NarrationLanguage.SPANISH -> "values"
            NarrationLanguage.FRENCH -> "values-fr"
            else -> "values-en"
        }
        val nodes = DocumentBuilderFactory.newInstance().newDocumentBuilder()
            .parse(File(app, "src/main/res/$directory/lesson_ui_strings.xml")).getElementsByTagName("string")
        return (0 until nodes.length).map(nodes::item).single {
            it.attributes.getNamedItem("name").nodeValue == "lesson_note"
        }.textContent.replace("\\n", "\n")
    }

    private fun note(body: String, mode: String = "auto") = JSONObject().put("type", "note")
        .put("text", body).put("mathMode", mode)

    private fun lesson(block: JSONObject) = JSONObject().put("pages", JSONArray()
        .put(JSONObject().put("blocks", JSONArray().put(block))))

    private fun instructions(language: NarrationLanguage): (String) -> String = { type ->
        if (type == "note") String.format(Locale.ROOT, template(language), "") else ""
    }

    @Test fun publishedMathematicsNoteAddsItsVisibleHeadingAndRetainsTheExactExistingBodySpeech() {
        val file = File(app, "src/main/assets/content/es/math_physics/math_course_01/numeros_y_operaciones/lesson_magnitudes_y_numeracion.json")
        val source = JSONObject(file.readText())
        val original = source.toString()
        val page = source.getJSONArray("pages").getJSONObject(3)
        val blocks = page.getJSONArray("blocks")
        val index = (0 until blocks.length()).single { blocks.getJSONObject(it).optString("type") == "note" }
        val published = blocks.getJSONObject(index)
        assertTrue(published.getString("text").startsWith("Al escribir en un campo"))
        val one = lesson(published)
        val oldBody = LessonNarrationText.page(one, 0, NarrationLanguage.SPANISH, forKokoro = true)
        val current = LessonNarrationScript.page(one, 0, NarrationLanguage.SPANISH,
            forKokoro = true, instruction = instructions(NarrationLanguage.SPANISH))
        assertEquals("Nota", ManualEnglishSpeech.strip(current.texts.first()))
        assertEquals("Existing compatible body recordings keep their exact text/key", oldBody, current.texts.drop(1))
        assertTrue(current.origins.all { it.blockIndex == 0 && !it.feedback })
        assertTrue(current.gates.isEmpty())
        assertEquals(original, source.toString())
    }

    @Test fun theWholeNativeNoteHasWordMappingsIncludingItsHeadingInAllFourLanguages() {
        val examples = listOf(
            NarrationLanguage.SPANISH to "Al escribir en un campo que ya muestra la unidad, introduce solo el número.",
            NarrationLanguage.AMERICAN_ENGLISH to "When the field already shows the unit, enter only the number.",
            NarrationLanguage.BRITISH_ENGLISH to "When the field already shows the unit, enter only the number.",
            NarrationLanguage.FRENCH to "Quand le champ affiche déjà l’unité, saisissez seulement le nombre."
        )
        examples.forEach { (language, body) -> verifyMapping(language, body, mathematical = false) }
    }

    @Test fun mathematicalAndCppNoteBodiesKeepTheirOwnNormalizationAndProgressiveOwnership() {
        val examples = listOf(
            NarrationLanguage.SPANISH to ("Calcula \\(6 × 100 = 600\\); conserva la unidad." to "La línea #include <iostream> prepara std::cout."),
            NarrationLanguage.AMERICAN_ENGLISH to ("Calculate \\(6 × 100 = 600\\); keep the unit." to "The #include <iostream> line prepares std::cout."),
            NarrationLanguage.BRITISH_ENGLISH to ("Calculate \\(6 × 100 = 600\\); keep the unit." to "The #include <iostream> line prepares std::cout."),
            NarrationLanguage.FRENCH to ("Calculez \\(6 × 100 = 600\\) ; conservez l’unité." to "La ligne #include <iostream> prépare std::cout.")
        )
        examples.forEach { (language, bodies) ->
            verifyMapping(language, bodies.first, mathematical = true)
            verifyMapping(language, bodies.second, mathematical = false)
        }
    }

    private fun verifyMapping(language: NarrationLanguage, body: String, mathematical: Boolean) {
        val format = template(language)
        val prefix = String.format(Locale.ROOT, format, "")
        val source = lesson(note(body))
        val oldBody = LessonNarrationText.page(source, 0, language, forKokoro = true)
        val script = LessonNarrationScript.page(source, 0, language,
            forKokoro = true, instruction = instructions(language))
        assertEquals("The body must retain its original pronunciation/cache identity in $language", oldBody, script.texts.drop(1))
        val full = String.format(Locale.ROOT, format, body)
        val shown = if (mathematical) formulaSource(full) else full
        val parts = NarrationSegments.sentences(script.texts)
        val mappings = NarrationWordMappings.forView(0, shown, parts.indices.associateWith { parts[it] }, language)
        assertEquals("No block fallback is needed for the complete native note in $language", parts.indices.toSet(), mappings.keys)
        val ranges = mappings.values.flatMap { it.displayed }
        assertTrue("Every existing glyph keeps an owner in $language", ranges.size > 4)
        assertEquals(shown.indices.toSet(), ranges.flatMap { it.start until it.endExclusive }.toSet())
        assertTrue("The heading is independent of the still-pending body", mappings.getValue(0).displayed.all { it.endExclusive <= prefix.length })
        assertTrue("Body words remain independently progressive", mappings.filterKeys { it != 0 }.values.flatMap { it.displayed }.distinct().size > 3)
    }

    /** Pure offsets for this fixture's explicit formula. Android QA separately checks the
     * MathFormulaSpan; the JVM has no implementation of SpannableStringBuilder. */
    private fun formulaSource(full: String): NarrationSourceProjection.Source {
        val opening = full.indexOf("\\(")
        val closing = full.indexOf("\\)", opening + 2)
        require(opening >= 0 && closing > opening + 2)
        val shown = full.substring(0, opening) + full.substring(opening + 2, closing) + full.substring(closing + 2)
        return NarrationSourceProjection.Source(shown, listOf(ReadingTextRange(opening, closing - 2)))
    }
}
