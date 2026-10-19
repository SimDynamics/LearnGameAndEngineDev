package com.learngraphicsprogramming.app

import com.learngraphicsprogramming.app.narration.LessonNarrationScript
import com.learngraphicsprogramming.app.narration.NarrationLanguage
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class LessonParagraphEmphasisTest {
    @Test fun absentMetadataPreservesEveryLegacyParagraph() {
        val source = "Una biblioteca reúne recursos. #include <iostream>\nstd::cout;"
        listOf(null, JSONObject.NULL, "**biblioteca**", JSONObject()).forEach {
            assertTrue(LessonParagraphEmphasis.ranges(source, it).isEmpty())
        }
    }

    @Test fun onlyExactDesignatedFragmentsAreSelectedWithoutMarkersOrNormalization() {
        val source = "Una biblioteca reúne recursos.\nComprueba std::cout y return 0; (sin cambiar nada)."
        val config = JSONArray().put(entry("biblioteca")).put(entry("Comprueba std::cout"))
        val ranges = LessonParagraphEmphasis.ranges(source, config)
        assertEquals(listOf("biblioteca", "Comprueba std::cout"), ranges.map { source.substring(it.start, it.endExclusive) })
        assertEquals(source.indexOf("Comprueba"), ranges[1].start)
        assertTrue(LessonParagraphEmphasis.ranges(source, JSONArray().put(entry("RECURSOS"))).isEmpty())
        assertTrue(LessonParagraphEmphasis.ranges(source, JSONArray().put(entry("reune"))).isEmpty())
    }

    @Test fun repeatedFragmentsRequireExplicitOneBasedOccurrence() {
        val source = "valor y valor y valor"
        assertTrue(LessonParagraphEmphasis.ranges(source, JSONArray().put(entry("valor"))).isEmpty())
        val config = JSONArray().put(entry("valor").put("occurrence", 2))
        assertEquals(listOf(LessonParagraphEmphasis.Range(8, 13)), LessonParagraphEmphasis.ranges(source, config))
        assertTrue(LessonParagraphEmphasis.ranges(source, JSONArray().put(entry("valor").put("occurrence", 4))).isEmpty())
    }

    @Test fun malformedDecorationNeverChangesOrSuppressesTheSource() {
        val source = "main y return 0;"
        val config = JSONArray().put(null).put("main").put(JSONObject()).put(entry(""))
            .put(entry(" ")).put(entry("absent"))
        for (value in listOf<Any>(0, -1, 1.5, "1", true)) config.put(entry("main").put("occurrence", value))
        config.put(entry("return 0;"))
        val ranges = LessonParagraphEmphasis.ranges(source, config)
        assertEquals(listOf("return 0;"), ranges.map { source.substring(it.start, it.endExclusive) })
        assertEquals("main y return 0;", source)
    }

    @Test fun overlappingOrDuplicateDesignationsRetainOriginalOffsets() {
        val source = "std::cout y std::cin"
        val config = JSONArray().put(entry("std::cout")).put(entry("std::cout"))
            .put(entry("cout")).put(entry("std::cin"))
        assertEquals(listOf(LessonParagraphEmphasis.Range(0, 9), LessonParagraphEmphasis.Range(5, 9),
            LessonParagraphEmphasis.Range(12, 20)), LessonParagraphEmphasis.ranges(source, config))
    }

    @Test fun utf16OffsetsNeverSplitSupplementaryCharacters() {
        val source = "A 📦 y café e\u0301"
        assertTrue(LessonParagraphEmphasis.ranges(source, JSONArray().put(entry("\uD83D"))).isEmpty())
        assertTrue(LessonParagraphEmphasis.ranges(source, JSONArray().put(entry("\uDCE6"))).isEmpty())
        val expected = listOf("📦", "café", "e\u0301")
        val ranges = LessonParagraphEmphasis.ranges(source, JSONArray().also { a -> expected.forEach { a.put(entry(it)) } })
        assertEquals(expected, ranges.map { source.substring(it.start, it.endExclusive) })
    }

    @Test fun decorationDoesNotChangeNarrationGatesOriginsOrAnyLanguageText() {
        val paragraph = JSONObject().put("type", "paragraph").put("text", "Comprueba #include <iostream> y std::cout.")
        val lesson = JSONObject().put("title", "Ejemplo").put("pages", JSONArray().put(JSONObject().put("blocks", JSONArray().put(paragraph))))
        for (language in NarrationLanguage.entries) {
            for (kokoro in listOf(false, true)) {
                paragraph.remove("emphasis")
                val before = LessonNarrationScript.page(lesson, 0, language, kokoro)
                paragraph.put("emphasis", JSONArray().put(entry("Comprueba")).put(entry("std::cout")))
                assertEquals(before, LessonNarrationScript.page(lesson, 0, language, kokoro))
            }
        }
        assertEquals("Comprueba #include <iostream> y std::cout.", paragraph.getString("text"))
    }

    private fun entry(text: String) = JSONObject().put("text", text)
}
