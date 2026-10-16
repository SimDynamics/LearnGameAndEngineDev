package com.learngraphicsprogramming.app.narration

import com.learngraphicsprogramming.app.narration.phonemes.SpanishFluidG2P
import java.io.File
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class MathFeedbackPunctuationTest {
    private fun paragraph(source: String, language: NarrationLanguage): String {
        val lesson = JSONObject().put("pages", JSONArray().put(JSONObject().put("blocks", JSONArray()
            .put(JSONObject().put("type", "paragraph").put("text", source)))))
        return LessonNarrationText.page(lesson, 0, language, forKokoro = true).single()
    }

    @Test fun realPlaceValueFeedbackKeepsTheSemicolonAsClausePunctuationThroughBothNormalizers() {
        val resources = File(System.getProperty("translation.resources") ?: "src/main/res")
        val assets = requireNotNull(resources.absoluteFile.parentFile).resolve("assets/content")
        for ((locale, language) in listOf("es" to NarrationLanguage.SPANISH,
            "en" to NarrationLanguage.AMERICAN_ENGLISH, "en" to NarrationLanguage.BRITISH_ENGLISH,
            "fr" to NarrationLanguage.FRENCH)) {
            val lesson = JSONObject(assets.resolve("$locale/math_physics/math_course_01/numeros_y_operaciones/lesson_magnitudes_y_numeracion.json").readText())
            val before = lesson.toString()
            val blocks = lesson.getJSONArray("pages").getJSONObject(2).getJSONArray("blocks")
            val source = (0 until blocks.length()).mapNotNull { blocks.getJSONObject(it)
                .optJSONObject("feedback")?.optString("correct") }.single { it.contains("6308") }
            val prepared = paragraph(source, language)
            val spoken = when (language) {
                NarrationLanguage.SPANISH -> SpanishFluidG2P.normalizeText(ManualEnglishSpeech.strip(prepared))
                NarrationLanguage.FRENCH -> FrenchSpeechSymbols.normalize(prepared)
                else -> prepared
            }
            assertTrue(language.tag, spoken.contains(';'))
            assertFalse("${language.tag}: $spoken", spoken.contains("punto y coma") || spoken.contains("point-virgule"))
            assertFalse("${language.tag}: $prepared", EnglishTechnicalSpeech.replacements(prepared).any { it.spoken == "semicolon" })
            if (language == NarrationLanguage.SPANISH) {
                assertEquals("seis mil más trescientos más ocho es igual a seis mil trescientos ocho; no hay decenas y se conserva esa posición con cero.", spoken)
                assertFalse(spoken, spoken.contains("asigna"))
            }
            assertEquals(before, lesson.toString())
        }
    }

    @Test fun numericalRelationsRemainMathematicalBeforeOrdinarySemicolons() {
        for (source in listOf("6000 + 300 + 8 = 6308; no hay decenas.",
            "6 × 100 = 600; esa posición vale cien.", "9 - 3 = 6; continúa.",
            "8 ÷ 2 = 4; continúa.", "12 / 3 = 4; continúa.")) {
            val prepared = paragraph(source, NarrationLanguage.SPANISH)
            val spoken = SpanishFluidG2P.normalizeText(ManualEnglishSpeech.strip(prepared))
            assertTrue(source, spoken.contains(';'))
            assertFalse("$source: $spoken", spoken.contains("punto y coma") || spoken.contains("asigna"))
            assertFalse("$source: $prepared", prepared.contains('=') || prepared.contains('<') || prepared.contains('>'))
        }
    }

    @Test fun previouslyCorrectSpanishEquationsKeepTheirExactPreparedTextKeysAndPhones() {
        for ((source, previous) in listOf(
            "5006 = 5000 + 0 + 0 + 6; queda completo." to "5006 = cinco mil más cero más cero más seis; queda completo.",
            "6 × 100 = 600. Continúa." to "seis por cien = 600. Continúa.",
            "8 = 8; compara." to "8 = 8; compara.",
            "2 + 1 < 4; continúa." to "dos más uno < 4; continúa.",
            "2 * 3 = 6; continúa." to "2 por 3 = 6; continúa."
        )) {
            val previousPrepared = ManualEnglishSpeech.PREPARED + previous
            val prepared = paragraph(source, NarrationLanguage.SPANISH)
            assertEquals(source, previousPrepared, prepared)
            assertEquals(source, KokoroNarrationCache.key("es", "ef_dora", previousPrepared),
                KokoroNarrationCache.key("es", "ef_dora", prepared))
            assertEquals(source, MixedSpanishG2P.phonemizePrepared(previousPrepared, emptyMap()),
                MixedSpanishG2P.phonemizePrepared(prepared, emptyMap()))
        }
    }

    @Test fun englishAndFrenchPreparedMathAndCompatibleCacheKeysDoNotChange() {
        val french = "6000 + 300 + 8 = 6308 ; il n'y a pas de dizaines et le zéro conserve cette position."
        val oldFrench = "six mille plus trois cents plus huit = 6308; il n'y a pas de dizaines et le zéro conserve cette position."
        val preparedFrench = paragraph(french, NarrationLanguage.FRENCH)
        assertEquals(oldFrench, preparedFrench)
        assertEquals(KokoroNarrationCache.key("fr", "ff_siwis", oldFrench),
            KokoroNarrationCache.key("fr", "ff_siwis", preparedFrench))
        assertEquals(FrenchSpeechSymbols.normalize(oldFrench), FrenchSpeechSymbols.normalize(preparedFrench))
        for ((language, voice) in listOf(NarrationLanguage.AMERICAN_ENGLISH to "af_heart",
            NarrationLanguage.BRITISH_ENGLISH to "bf_emma")) {
            val source = "6000 + 300 + 8 = 6308; there are no tens, so zero holds that place."
            val previous = "six thousand plus three hundred plus eight = 6308; there are no tens, so zero holds that place."
            val prepared = paragraph(source, language)
            assertEquals(language.tag, previous, prepared)
            assertEquals(language.tag, KokoroNarrationCache.key(language.tag, voice, previous),
                KokoroNarrationCache.key(language.tag, voice, prepared))
            assertEquals(EnglishTechnicalSpeech.replacements(previous), EnglishTechnicalSpeech.replacements(prepared))
        }
    }

    @Test fun explicitCppSemicolonsStillHaveTheirTechnicalNamesInEachLanguage() {
        for (source in listOf("No añadas ; a cualquier línea.", "El símbolo ; termina la instrucción.", "`x = 1;`")) {
            assertTrue(source, SpanishFluidG2P.normalizeText(ManualEnglishSpeech.strip(
                paragraph(source, NarrationLanguage.SPANISH))).contains("punto y coma"))
        }
        for (source in listOf("N'ajoute pas ; à toutes les lignes.", "Le symbole ; termine une instruction.", "`x = 1;`")) {
            assertTrue(source, FrenchSpeechSymbols.normalize(paragraph(source, NarrationLanguage.FRENCH)).contains("point-virgule"))
        }
        for (language in listOf(NarrationLanguage.AMERICAN_ENGLISH, NarrationLanguage.BRITISH_ENGLISH)) {
            for (source in listOf("Do not add ; to every line.", "The symbol ; ends the statement.", "`x = 1;`")) {
                assertTrue(source, EnglishTechnicalSpeech.replacements(paragraph(source, language)).any { it.spoken == "semicolon" })
            }
        }
    }

    @Test fun correctedMathSpeechUsesAnotherRecordingWithoutChangingCompatibleAudio() {
        val source = "6000 + 300 + 8 = 6308; no hay decenas y se conserva esa posición con cero."
        val formerlyPartial = "seis mil más trescientos más ocho = 6308; no hay decenas y se conserva esa posición con cero."
        val prepared = paragraph(source, NarrationLanguage.SPANISH)
        assertNotEquals(KokoroNarrationCache.key("es", "ef_dora", formerlyPartial),
            KokoroNarrationCache.key("es", "ef_dora", prepared))
        assertEquals("c2eff306eff398b3f2bb490015f375eb9cb6bef898010cd715ffbd5fd974bbc0",
            KokoroNarrationCache.key("es", "ef_dora", "La programación permite resolver problemas."))
    }

    @Test fun numericRelationsDoNotConsumeUrlsCodeIdentifiersOrExplicitTextMode() {
        val protected = "Usa `6000 + 300 + 8 = 6308;`, ```6 × 100 = 600;``` y " +
            "https://example.org/6000+300+8=6308; C:/6000+300+8=6308; " +
            "/datos/6+2=8; o total6000+300=6300; y 8=8contador."
        assertEquals(protected, SpanishMathSpeech.text(protected))
        assertEquals(protected, MultilingualMathSpeech.text(protected, language = NarrationLanguage.FRENCH))
        assertEquals(protected, MultilingualMathSpeech.text(protected, language = NarrationLanguage.AMERICAN_ENGLISH))
        val textOnly = "6000 + 300 + 8 = 6308; no hay decenas."
        assertEquals(textOnly, SpanishMathSpeech.text(textOnly, "text"))
    }
}
