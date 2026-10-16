package com.learngraphicsprogramming.app.narration

import com.learngraphicsprogramming.app.math.MathAnswer
import com.learngraphicsprogramming.app.math.MathPuzzleFeedback
import com.learngraphicsprogramming.app.math.MathResponseSpec
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

    @Test fun aCompleteVisibleIncorrectTranscriptKeepsItsVerdictGenericGuideAndOnlySelectedDetails() {
        val locales = listOf(
            Triple(NarrationLanguage.SPANISH, "Incorrecto.", "Revisa la relación antes de volver a intentarlo."),
            Triple(NarrationLanguage.AMERICAN_ENGLISH, "Incorrect.", "Review the relation before trying again."),
            Triple(NarrationLanguage.BRITISH_ENGLISH, "Incorrect.", "Review the relation before trying again."),
            Triple(NarrationLanguage.FRENCH, "Incorrect.", "Réexaminez la relation avant de réessayer.")
        )
        for ((language, verdict, generic) in locales) for (forKokoro in listOf(false, true)) {
            val detail = when (language) {
                NarrationLanguage.SPANISH -> "Compara \\(6 × 100 = 600\\) y la instrucción std::cout de #include <iostream>."
                NarrationLanguage.FRENCH -> "Comparez \\(6 × 100 = 600\\) et l’instruction std::cout de #include <iostream>."
                else -> "Compare \\(6 × 100 = 600\\) and the std::cout instruction from #include <iostream>."
            }
            val absent = "NEVER_SPOKEN_ABSENT_REVIEW"
            val global = "NEVER_SPOKEN_GLOBAL_SOLUTION"
            val response = MathResponseSpec("PRIVATE_RESPONSE_ID", "math_multiple_choice_single",
                JSONObject().put("options", JSONArray()
                    .put(JSONObject().put("id", "selected").put("label", "Selected").put("reviewFeedback", detail))
                    .put(JSONObject().put("id", "absent").put("label", "Other").put("reviewFeedback", absent))),
                JSONObject().put("expected", JSONArray().put("absent")))
            val reviews = MathPuzzleFeedback.selectedReviews(response, MathAnswer.Choice(setOf("selected")), generic)
            val guidance = MathPuzzleFeedback.incorrectGuidance(true, global, reviews, generic)
            assertEquals(listOf(detail), guidance)
            val visible = "✗ $verdict\n$generic\n${guidance.single()}"
            val unchanged = visible
            val expected = LessonNarrationText.page(paragraphPage(listOf(visible.drop(2))), 0, language, forKokoro)
            val actual = IncorrectFeedbackNarration.prepare(listOf(visible), language, forKokoro)
            assertEquals("Only the icon is decoration; no visible sentence disappears in $language", expected, actual)
            val spoken = ManualEnglishSpeech.strip(actual.joinToString(" "))
            assertTrue(spoken.startsWith(verdict))
            assertTrue(spoken.contains(generic))
            assertFalse(spoken.contains("✗"))
            assertFalse(spoken.contains(absent))
            assertFalse(spoken.contains(global))
            assertFalse(spoken.contains(response.id))
            assertEquals(unchanged, visible)
        }
    }

    @Test fun verdictMarksAreRemovedOnlyAtTheNativeLeadingPosition() {
        assertEquals("Incorrecto.\nRevisa.", IncorrectFeedbackNarration.visibleSpeech("✗ Incorrecto.\nRevisa."))
        assertEquals("Revisa el signo ✗ escrito aquí.", IncorrectFeedbackNarration.visibleSpeech("Revisa el signo ✗ escrito aquí."))
        assertEquals("✓ Correcto.", IncorrectFeedbackNarration.visibleSpeech("✓ Correcto."))
        assertEquals("✗Incorrecto.", IncorrectFeedbackNarration.visibleSpeech("✗Incorrecto."))
        assertEquals("", IncorrectFeedbackNarration.visibleSpeech("✗ "))
    }

    @Test fun theCompleteUnmarkedNativeMathReviewKeepsItsOrdinarySemicolon() {
        val review = "Revisa la igualdad 6 × 100 = 600 y la descomposición 6000 + 300 + 8 = 6308; " +
            "cada símbolo conserva el significado de la expresión y después debes comparar los precios."
        val visible = "✗ Revisa tu respuesta e inténtalo de nuevo.\nRespuesta 1: $review"
        for (kokoro in listOf(false, true)) {
            val prepared = IncorrectFeedbackNarration.prepare(listOf(visible), NarrationLanguage.SPANISH, kokoro)
            assertEquals(LessonNarrationText.page(paragraphPage(listOf(visible.drop(2))), 0,
                NarrationLanguage.SPANISH, kokoro), prepared)
            val spoken = ManualEnglishSpeech.strip(prepared.joinToString(" "))
            assertTrue(spoken.startsWith("Revisa tu respuesta e inténtalo de nuevo."))
            assertTrue(spoken.contains("Respuesta"))
            assertTrue(spoken.contains("cada símbolo conserva el significado"))
            assertFalse("The semicolon separates prose clauses, not technical code", spoken.contains("punto y coma"))
            assertFalse(spoken.contains("✗"))
        }
    }

    @Test fun theCompleteNativeMathReviewHasAnExactContinuousKokoroPhoneTraceForEverySentence() {
        val review = "Revisa la igualdad 6 × 100 = 600 y la descomposición 6000 + 300 + 8 = 6308; " +
            "cada símbolo conserva el significado de la expresión y después debes comparar los precios."
        val visible = "✗ Revisa tu respuesta e inténtalo de nuevo.\nRespuesta 1: $review"
        val parts = NarrationSegments.sentences(IncorrectFeedbackNarration.prepare(listOf(visible),
            NarrationLanguage.SPANISH, true))
        assertTrue(parts.size >= 2)
        parts.forEachIndexed { index, part ->
            val trace = MixedSpanishG2P.traceWordsPrepared(part)
            assertNotNull("Native review segment $index needs an observational trace: ${ManualEnglishSpeech.strip(part)}", trace)
            assertEquals(MixedSpanishG2P.phonemizePrepared(part, emptyMap()), trace!!.phonemes)
            assertEquals(NarrationSpokenWords.lexemes(part).map { it.canonical }, trace.words.map { it.word })
        }
    }

    @Test fun contextualResidualRelationsKeepManualEnglishPhonesAndOriginalSourceWordIdentity() {
        val term = ManualEnglishTerms.entries.single { it.spelling == "return" }
        val prepared = ManualEnglishSpeech.PREPARED + "Revisa tres por dos = 6 y " +
            ManualEnglishSpeech.encode(term) + " conserva la salida; después compara el resultado."
        val trace = MixedSpanishG2P.traceWordsPrepared(prepared)
        assertNotNull("Contextual technical normalization must not discard the surrounding word trace", trace)
        assertEquals(MixedSpanishG2P.phonemizePrepared(prepared, emptyMap()), trace!!.phonemes)
        assertEquals(NarrationSpokenWords.lexemes(prepared).map { it.canonical }, trace.words.map { it.word })
        val english = trace.words.single { it.word == "return" }
        assertEquals("The trace only observes the reviewed English phones", ManualEnglishPronunciations.phonemes(term),
            trace.phonemes.substring(english.start, english.endExclusive))
        assertTrue(trace.words.zipWithNext().all { (left, right) -> left.endExclusive <= right.start })
    }

    private fun paragraphPage(texts: List<String>) = JSONObject().put("pages", JSONArray().put(
        JSONObject().put("blocks", JSONArray(texts.map { JSONObject().put("type", "paragraph").put("text", it) }))))
}
