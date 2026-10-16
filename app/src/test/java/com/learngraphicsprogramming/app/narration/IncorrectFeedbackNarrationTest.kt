package com.learngraphicsprogramming.app.narration

import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class IncorrectFeedbackNarrationTest {
    @Test fun technicalAndMathematicalReviewsUseTheOrdinaryParagraphFrontEndInAllFourLocales() {
        val guides = listOf(
            "Revisa la línea #include <iostream> y std::cout; después considera std:: indica.",
            "Review #include <iostream>, std::cout << value; and std:: indicates a namespace.",
            "Réexamine #include <iostream> et std::cout; std:: indique un espace de noms.",
            "Comprueba \\(6 \\times 100 = 600\\) y \\(5006 = 5000 + 0 + 0 + 6\\)."
        )
        val original = guides.toList()
        for (language in NarrationLanguage.entries) for (forKokoro in listOf(false, true)) {
            val expected = LessonNarrationText.page(paragraphPage(guides), 0, language, forKokoro)
            val actual = IncorrectFeedbackNarration.prepare(guides, language, forKokoro)
            assertEquals("$language, Kokoro=$forKokoro must use the existing speech pipeline", expected, actual)
            assertTrue(actual.isNotEmpty())
            assertFalse(actual.joinToString(" ").contains("\\times"))
            assertFalse(actual.joinToString(" ").contains("\\("))
        }
        assertEquals("Extraction cannot rewrite the displayed feedback", original, guides)
    }

    @Test fun spanishTechnicalReviewsKeepTheManualEnglishAnnotationsAndDoNotInventDirective() {
        val raw = listOf("Revisa la línea #include <iostream> y std::cout.")
        val neural = IncorrectFeedbackNarration.prepare(raw, NarrationLanguage.SPANISH, true).joinToString(" ")
        assertTrue(ManualEnglishSpeech.isPrepared(neural))
        assertTrue(ManualEnglishSpeech.spans(neural).isNotEmpty())
        assertTrue(neural.contains("almohadilla"))
        assertFalse(neural.contains("directiva"))
        val android = IncorrectFeedbackNarration.prepare(raw, NarrationLanguage.SPANISH, false).joinToString(" ")
        assertFalse(ManualEnglishSpeech.isPrepared(android))
        assertTrue(ManualEnglishSpeech.spans(android).isEmpty())
        assertTrue(android.contains("almohadilla"))
    }

    @Test fun ordinaryFrenchWordsAndEmptyGuidanceDoNotAcquireTechnicalMetadataOrExtraContent() {
        assertTrue(IncorrectFeedbackNarration.prepare(listOf("", "\n\t"), NarrationLanguage.FRENCH, true).isEmpty())
        val source = listOf("La main droite reste ouverte; puis elle se ferme.")
        val result = IncorrectFeedbackNarration.prepare(source, NarrationLanguage.FRENCH, true).joinToString(" ")
        assertTrue(ManualEnglishSpeech.spans(result).isEmpty())
        assertTrue(result.contains(source.single()))
        assertFalse(result.contains("Correct"))
    }

    private fun paragraphPage(texts: List<String>) = JSONObject().put("pages", JSONArray().put(
        JSONObject().put("blocks", JSONArray(texts.map { JSONObject().put("type", "paragraph").put("text", it) }))))
}
