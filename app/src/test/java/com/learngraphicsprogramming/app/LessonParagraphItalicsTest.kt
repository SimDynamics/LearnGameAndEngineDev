package com.learngraphicsprogramming.app

import com.learngraphicsprogramming.app.narration.LessonNarrationScript
import com.learngraphicsprogramming.app.narration.NarrationLanguage
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class LessonParagraphItalicsTest {
    @Test fun omittedStyleKeepsBoldAndExplicitStylesSelectOnlyExactSourceFragments() {
        val source = "Una idea y un título; ambos se conservan."
        val ranges = LessonParagraphEmphasis.styledRanges(source, JSONArray()
            .put(entry("idea")).put(entry("título", "italic")).put(entry("ambos", "bold_italic")))
        assertEquals(listOf("idea", "título", "ambos"), ranges.map { source.substring(it.start, it.endExclusive) })
        assertEquals(listOf(LessonParagraphEmphasis.Style.BOLD, LessonParagraphEmphasis.Style.ITALIC,
            LessonParagraphEmphasis.Style.BOLD_ITALIC), ranges.map { it.style })
        assertEquals(LessonParagraphEmphasis.styledRanges(source, JSONArray().put(entry("idea"))),
            LessonParagraphEmphasis.styledRanges(source, JSONArray().put(entry("idea", "bold"))))
    }

    @Test fun repeatedBibliographicTitleKeepsBothExplicitOccurrences() {
        val title = "Working Draft: Programming Languages — C++"
        val source = "$title, s. f.; y $title, s. f."
        assertTrue(LessonParagraphEmphasis.styledRanges(source, JSONArray().put(entry(title, "italic"))).isEmpty())
        val ranges = LessonParagraphEmphasis.styledRanges(source, JSONArray()
            .put(entry(title, "italic").put("occurrence", 1))
            .put(entry(title, "italic").put("occurrence", 2)))
        assertEquals(2, ranges.size)
        assertEquals(listOf(title, title), ranges.map { source.substring(it.start, it.endExclusive) })
        assertEquals(listOf(0, source.lastIndexOf(title)), ranges.map { it.start })
    }

    @Test fun overlappingBoldAndItalicKeepBothStylesWithoutDuplicatingTheSameDecoration() {
        val source = "std::cout conserva su texto."
        val config = JSONArray().put(entry("std::cout")).put(entry("cout", "italic"))
            .put(entry("cout", "italic")).put(entry("std::cout", "italic"))
        val ranges = LessonParagraphEmphasis.styledRanges(source, config)
        assertEquals(3, ranges.size)
        assertEquals(setOf(LessonParagraphEmphasis.Style.BOLD, LessonParagraphEmphasis.Style.ITALIC),
            ranges.filter { it.start == 0 }.map { it.style }.toSet())
        assertEquals(listOf(LessonParagraphEmphasis.Range(0, 9), LessonParagraphEmphasis.Range(5, 9)),
            LessonParagraphEmphasis.ranges(source, config))
    }

    @Test fun unsupportedOrMalformedStyleDoesNotSilentlyBecomeBold() {
        val source = "Un título sin formato inventado."
        val invalid = listOf<Any>("ITALIC", "italics", "", "normal", 1, true, JSONObject.NULL, JSONArray())
        invalid.forEach { value ->
            assertTrue("Invalid style $value is ignored", LessonParagraphEmphasis.styledRanges(source,
                JSONArray().put(entry("título").put("style", value))).isEmpty())
        }
        assertEquals(source, "Un título sin formato inventado.")
    }

    @Test fun italicOffsetsDoNotSplitUtf16PairsOrNormalizeSourceCharacters() {
        val source = "📘 Café e\u0301 C++\nhttps://ejemplo.invalid/"
        val config = JSONArray().put(entry("\uD83D", "italic")).put(entry("\uDCD8", "italic"))
            .put(entry("📘", "italic")).put(entry("Café e\u0301", "bold_italic"))
            .put(entry("C++\nhttps://ejemplo.invalid/", "italic"))
        val ranges = LessonParagraphEmphasis.styledRanges(source, config)
        assertEquals(listOf("📘", "Café e\u0301", "C++\nhttps://ejemplo.invalid/"),
            ranges.map { source.substring(it.start, it.endExclusive) })
        assertEquals(listOf(0, 3, source.indexOf("C++")), ranges.map { it.start })
    }

    @Test fun noteDecorationMatchesOnlyItsOriginalBodyAndKeepsLocalizedVisibleOffsets() {
        val body = "Nota y título; Nota conserva su segunda aparición."
        val config = JSONArray().put(entry("Nota", "italic").put("occurrence", 2))
        for (label in listOf("Nota\n\n", "Note\n\n", "Remarque\n\n")) {
            val visible = label + body
            val ranges = LessonParagraphEmphasis.styledRanges(visible, config,
                LessonParagraphEmphasis.Range(label.length, visible.length))
            assertEquals(1, ranges.size)
            assertEquals(label.length + body.lastIndexOf("Nota"), ranges.single().start)
            assertEquals("Nota", visible.substring(ranges.single().start, ranges.single().endExclusive))
        }
    }

    @Test fun invalidSourceScopeNeverCutsCharactersOrStylesTheNoteHeader() {
        val source = "Nota\n\n📘 título"
        val config = JSONArray().put(entry("Nota", "italic")).put(entry("título", "italic"))
        val valid = LessonParagraphEmphasis.styledRanges(source, config, LessonParagraphEmphasis.Range(6, source.length))
        assertEquals(listOf("título"), valid.map { source.substring(it.start, it.endExclusive) })
        listOf(LessonParagraphEmphasis.Range(-1, source.length), LessonParagraphEmphasis.Range(8, 7),
            LessonParagraphEmphasis.Range(0, source.length + 1), LessonParagraphEmphasis.Range(7, source.length),
            LessonParagraphEmphasis.Range(0, 7)).forEach { scope ->
            assertTrue(LessonParagraphEmphasis.styledRanges(source, config, scope).isEmpty())
        }
    }

    @Test fun everyStyleLeavesAllLanguageNarrationOriginsAndGatesExactlyUnchanged() {
        val text = "Working Draft: Programming Languages — C++, s. f. Consulta std::cout."
        val paragraph = JSONObject().put("type", "paragraph").put("text", text)
        val note = JSONObject().put("type", "note").put("text", text)
        val lesson = JSONObject().put("id", "style_qa").put("title", "Título original").put("pages",
            JSONArray().put(JSONObject().put("blocks", JSONArray().put(paragraph).put(note))))
        for (language in NarrationLanguage.entries) for (kokoro in listOf(false, true)) {
            paragraph.remove("emphasis")
            note.remove("emphasis")
            val before = LessonNarrationScript.page(lesson, 0, language, kokoro)
            paragraph.put("emphasis", JSONArray().put(entry("Working Draft: Programming Languages — C++", "italic"))
                .put(entry("Consulta", "bold_italic")).put(entry("std::cout")))
            note.put("emphasis", paragraph.get("emphasis"))
            assertEquals(before, LessonNarrationScript.page(lesson, 0, language, kokoro))
        }
        assertEquals(text, paragraph.getString("text"))
    }

    private fun entry(text: String, style: String? = null) = JSONObject().put("text", text).apply {
        if (style != null) put("style", style)
    }
}
