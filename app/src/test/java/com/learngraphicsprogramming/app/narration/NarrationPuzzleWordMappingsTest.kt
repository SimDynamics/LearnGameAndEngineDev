package com.learngraphicsprogramming.app.narration

import java.io.File
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class NarrationPuzzleWordMappingsTest {
    @Test fun aPromptAndItsOptionsKeepSeparateTargetsInsideTheSameBlock() {
        val views = listOf(40 to "¿Qué mensaje corresponde?", 41 to "Caja preparada.", 42 to "Caja abierta.")
        val segments = linkedMapOf(3 to "¿Qué mensaje corresponde?", 4 to "Opción 1.",
            5 to "Caja preparada.", 6 to "Opción 2.", 7 to "Caja abierta.")
        val mapping = mapped(views, segments)
        assertEquals(listOf(3, 5, 7), mapping.keys.toList())
        assertEquals(listOf(40, 41, 42), mapping.values.map { it.targets!!.distinct().single() })
        assertTrue(mapping.values.all { it.blockIndex == 8 })
        assertRanges(views, mapping)
    }

    @Test fun identicalOptionsConsumeSuccessiveOccurrencesInRegisteredOrder() {
        val views = listOf(15 to "Lee bien.", 16 to "Iguales.", 17 to "Iguales.")
        val segments = linkedMapOf(0 to "Lee bien.", 1 to "Opción 1.", 2 to "Iguales.",
            3 to "Opción 2.", 4 to "Iguales.")
        val mapping = mapped(views, segments)
        assertEquals(listOf(0, 2, 4), mapping.keys.toList())
        assertEquals(listOf(16), mapping.getValue(2).targets)
        assertEquals(listOf(17), mapping.getValue(4).targets)
    }

    @Test fun oneViewCanOwnSeveralWholeSentencesWithoutBorrowingAnOption() {
        val source = "Una caja está preparada. ¿Qué comprobación corresponde?"
        val views = listOf(1 to source, 2 to "Comprueba el mensaje.")
        val segments = linkedMapOf(0 to "Una caja está preparada.", 1 to "¿Qué comprobación corresponde?",
            2 to "Opción 1.", 3 to "Comprueba el mensaje.")
        val mapping = mapped(views, segments)
        assertEquals(listOf(0, 1, 3), mapping.keys.toList())
        assertTrue(mapping.getValue(0).targets!!.all { it == 1 })
        assertTrue(mapping.getValue(1).targets!!.all { it == 1 })
        assertTrue(mapping.getValue(3).targets!!.all { it == 2 })
        assertRanges(views, mapping)
    }

    @Test fun aSharedSegmentRequiresAnExactOrderedUnionOfCompleteViewTexts() {
        val views = listOf(71 to "Caja preparada.", 72 to "Comprueba el mensaje.")
        val mapping = mapped(views, mapOf(9 to "Caja preparada. Comprueba el mensaje."))
        val item = mapping.getValue(9)
        assertEquals(listOf(71, 71, 72, 72, 72), item.targets)
        assertEquals(listOf("Caja ", "preparada.", "Comprueba ", "el ", "mensaje."),
            item.displayed.indices.map { index ->
                val source = views.single { it.first == item.targets!![index] }.second
                item.displayed[index].let { source.substring(it.start, it.endExclusive) }
            })
        assertRanges(views, mapping)
    }

    @Test fun aLaterStandaloneRepeatCannotDisplaceAnEarlierExactSharedSegment() {
        val views = listOf(7 to "Lee bien.", 8 to "Continúa aquí.")
        val mapping = mapped(views, linkedMapOf(0 to "Lee bien. Continúa aquí.", 1 to "Lee bien."))
        assertEquals(setOf(0), mapping.keys)
        assertEquals(setOf(7, 8), mapping.getValue(0).targets!!.toSet())
    }

    @Test fun feedbackHeadingsAllowOnlyExistingCanonicalPunctuationDifferences() {
        val views = listOf(11 to "✓ Correcto!", 12 to "Comprueba el resultado.")
        val mapping = mapped(views, linkedMapOf(2 to "Correcto.", 3 to "Comprueba el resultado."))
        assertEquals(listOf(11), mapping.getValue(2).targets)
        assertEquals(ReadingTextRange(0, views.first().second.length), mapping.getValue(2).displayed.single())
        assertRanges(views, mapping)

        val different = mapped(views, linkedMapOf(2 to "Respuesta acertada.", 3 to "Comprueba el resultado."))
        assertEquals(setOf(3), different.keys)
        assertTrue(different.values.all { it.targets!!.all { target -> target == 12 } })
    }

    @Test fun partialSentencesAndChangedWordsHaveNoInventedCorrespondence() {
        assertTrue(mapped(listOf(1 to "Lee bien."), mapOf(0 to "Lee bien y responde.")).isEmpty())
        assertTrue(mapped(listOf(1 to "¿Qué valor?"), mapOf(0 to "¿Qué resultado?")).isEmpty())
        val result = mapped(listOf(1 to "Representación distinta", 2 to "Continúa aquí."),
            linkedMapOf(0 to "Otra representación.", 1 to "Continúa aquí."))
        assertEquals(setOf(1), result.keys)
        assertEquals(setOf(2), result.values.flatMap { it.targets!! }.toSet())
    }

    @Test fun repeatedTargetIdsCannotMergeIndependentViewsAccidentally() {
        assertTrue(mapped(listOf(5 to "Primero.", 5 to "Segundo."),
            linkedMapOf(0 to "Primero.", 1 to "Segundo.")).isEmpty())
    }

    @Test fun technicalExpansionsKeepTheirOriginalGlyphOffsetsInEveryNarrationLanguage() {
        val examples = mapOf(
            NarrationLanguage.SPANISH to "La línea #include <iostream> usa std::cout.",
            NarrationLanguage.AMERICAN_ENGLISH to "The line #include <iostream> uses std::cout.",
            NarrationLanguage.BRITISH_ENGLISH to "The line #include <iostream> uses std::cout.",
            NarrationLanguage.FRENCH to "La ligne #include <iostream> utilise std::cout.")
        for ((language, technical) in examples) {
            val speech = requireNotNull(NarrationSourceProjection.forLanguage(technical, "auto", language)).text
            val parts = NarrationSegments.sentences(listOf(speech))
            val segments = linkedMapOf(0 to "Choix.", 1 to "Option 1.")
            parts.forEachIndexed { index, part -> segments[index + 2] = part }
            val views = listOf(1 to "Choix.", 2 to technical)
            val mapping = NarrationPuzzleWordMappings.forViews(8, views, segments, language)
            assertEquals("Every actual technical segment maps in $language", setOf(0) + (parts.indices.map { it + 2 }), mapping.keys)
            val glyphs = mapping.values.flatMap { item -> item.displayed.indices.filter {
                item.targets!![it] == 2
            }.flatMap { item.displayed[it].let { range -> range.start until range.endExclusive } } }.toSet()
            assertEquals("No technical symbol loses its original owner in $language", technical.indices.toSet(), glyphs)
            mapping.forEach { (segment, item) ->
                assertEquals(NarrationSpokenWords.technicalAtoms(segments.getValue(segment), language)
                    .map { ReadingTextRange(it.start, it.endExclusive) }, item.spoken)
            }
            assertRanges(views, mapping)
        }
    }

    @Test fun thePublishedFourthPageQuestionAndOptionsNeedNoCurricularPreparation() {
        val resources = File(System.getProperty("translation.resources") ?: "src/main/res")
        val file = requireNotNull(resources.absoluteFile.parentFile).resolve(
            "assets/content/es/programming/cpp_fundamentals/programas_y_ejecucion/lesson_problemas_instrucciones_y_resultados.json")
        val lesson = JSONObject(file.readText())
        val original = lesson.toString()
        val blocks = lesson.getJSONArray("pages").getJSONObject(3).getJSONArray("blocks")
        val index = (0 until blocks.length()).single { blocks.getJSONObject(it).optString("id") == "prueba_mensaje" }
        val block = blocks.getJSONObject(index)
        val views = mutableListOf(20 to block.getString("question"))
        val options = block.getJSONArray("options")
        repeat(options.length()) { option -> views += (21 + option) to options.getString(option) }
        val script = LessonNarrationScript.page(lesson, 3, NarrationLanguage.SPANISH, forKokoro = true)
        val parts = NarrationSegments.sentences(script.texts)
        val plan = requireNotNull(script.readingPlan(NarrationEngine.KOKORO))
        val segments = parts.indices.filter { plan.origins[it] == NarrationTextOrigin(index) }.associateWith { parts[it] }
        val mapping = NarrationPuzzleWordMappings.forViews(index, views, segments, NarrationLanguage.SPANISH)
        assertEquals(views.map { it.first }.toSet(), mapping.values.flatMap { it.targets!! }.toSet())
        assertRanges(views, mapping)
        assertEquals(original, lesson.toString())
    }

    @Test fun publishedTechnicalFeedbackUsesItsActualUiHeadingAndOriginalOffsetsInEveryLanguage() {
        val resources = File(System.getProperty("translation.resources") ?: "src/main/res").absoluteFile
        val root = requireNotNull(resources.parentFile)
        for (language in NarrationLanguage.entries) {
            val lesson = JSONObject(root.resolve("assets/content/${language.resourceGroup}/programming/" +
                "cpp_fundamentals/programas_y_ejecucion/lesson_primer_programa_y_salida.json").readText())
            val original = lesson.toString()
            val blockIndex = 4
            val explanation = lesson.getJSONArray("pages").getJSONObject(0).getJSONArray("blocks")
                .getJSONObject(blockIndex).getString("explanation")
            assertTrue(explanation.contains("std::cout") && explanation.contains("#include <iostream>"))
            val values = when (language) {
                NarrationLanguage.SPANISH -> "values"
                NarrationLanguage.FRENCH -> "values-fr"
                else -> "values-en"
            }
            val resourceText = resources.resolve("$values/lesson_ui_strings.xml").readText()
            val template = requireNotNull(Regex("""<string name="lesson_correct_explanation">(.*?)</string>""",
                RegexOption.DOT_MATCHES_ALL).find(resourceText)).groupValues[1].replace("\\n", "\n")
            val ui = template.replace("%1\$s", explanation)
            val script = LessonNarrationScript.page(lesson, 0, language, forKokoro = true)
            val parts = NarrationSegments.sentences(script.texts)
            val plan = requireNotNull(script.readingPlan(NarrationEngine.KOKORO))
            val segments = parts.indices.filter {
                plan.origins[it] == NarrationTextOrigin(blockIndex, feedback = true)
            }.associateWith { parts[it] }
            val mapping = NarrationPuzzleWordMappings.forViews(blockIndex, listOf(91 to ui),
                segments, language, feedback = true)
            assertEquals("All actual feedback sentences map in $language", segments.keys, mapping.keys)
            assertEquals(ui.indices.toSet(), mapping.values.flatMap { item -> item.displayed.flatMap {
                it.start until it.endExclusive
            } }.toSet())
            for ((segment, item) in mapping) {
                assertEquals(NarrationSpokenWords.technicalAtoms(segments.getValue(segment), language)
                    .map { ReadingTextRange(it.start, it.endExclusive) }, item.spoken)
                assertTrue(item.targets!!.all { it == 91 })
                assertTrue(item.displayed.none { it.start == 0 && it.endExclusive == ui.length })
            }
            assertRanges(listOf(91 to ui), mapping)
            assertEquals(original, lesson.toString())
        }
    }

    @Test fun successfulUiHeadingProjectionIsExplicitAndDoesNotRewriteOrdinaryBodyText() {
        val body = "La palabra Correcto permite leer #include <iostream> con std::cout."
        val speech = requireNotNull(NarrationSourceProjection.forLanguage(body, "auto", NarrationLanguage.SPANISH)).text
        val segments = NarrationSegments.sentences(listOf("Correcto.", speech))
            .withIndex().associate { it.index to it.value }
        for (heading in listOf("✓ Correcto", "✓ ¡Correcto!", "✓ Correct")) for (separator in listOf("\n", "\n\n")) {
            val ui = "$heading$separator$body"
            val mapped = NarrationPuzzleWordMappings.forViews(8, listOf(12 to ui), segments,
                NarrationLanguage.SPANISH, feedback = true)
            assertEquals(segments.keys, mapped.keys)
            val repeatedWord = ui.indexOf("Correcto", heading.length)
            assertTrue("The repeated ordinary word in the body retains its own source offsets",
                mapped.values.flatMap { it.displayed }.any { it.start >= heading.length &&
                    it.start <= repeatedWord && it.endExclusive > repeatedWord })
        }
        val ui = "✓ Correcto\n\n$body"
        assertTrue(NarrationPuzzleWordMappings.forViews(8, listOf(12 to ui), segments,
            NarrationLanguage.SPANISH).isEmpty())
        for (ordinary in listOf("✓ Correcto se usa como texto.\n\n$body", "✓ Incorrecto\n\n$body")) {
            assertTrue(NarrationPuzzleWordMappings.forViews(8, listOf(12 to ordinary), segments,
                NarrationLanguage.SPANISH, feedback = true).isEmpty())
        }
    }

    @Test fun completeIncorrectFeedbackMapsItsIconToTheFirstWordAndEveryVisibleSentenceProgressively() {
        val cases = listOf(
            Triple(NarrationLanguage.SPANISH, "Incorrecto.", "Revisa qué relación debe cumplir tu respuesta."),
            Triple(NarrationLanguage.AMERICAN_ENGLISH, "Incorrect.", "Review the relation your answer must satisfy."),
            Triple(NarrationLanguage.BRITISH_ENGLISH, "Incorrect.", "Review the relation your answer must satisfy."),
            Triple(NarrationLanguage.FRENCH, "Incorrect.", "Réexaminez la relation que votre réponse doit respecter.")
        )
        for ((language, verdict, generic) in cases) {
            val details = when (language) {
                NarrationLanguage.SPANISH -> listOf(
                    "Respuesta elegida: comprueba \\(6000 + 300 + 8 = 6308\\); conserva las posiciones.",
                    "Respuesta elegida: revisa #include <iostream> y std::cout antes de responder.")
                NarrationLanguage.FRENCH -> listOf(
                    "Réponse choisie : vérifiez \\(6000 + 300 + 8 = 6308\\) ; conservez les positions.",
                    "Réponse choisie : vérifiez #include <iostream> et std::cout avant de répondre.")
                else -> listOf(
                    "Selected answer: check \\(6000 + 300 + 8 = 6308\\); preserve the positions.",
                    "Selected answer: review #include <iostream> and std::cout before answering.")
            }
            for (detail in details) {
                val raw = "✗ $verdict\n$generic\n$detail"
                val displayed = formulaSourceOrText(raw)
                val before = displayed.toString()
                val parts = NarrationSegments.sentences(IncorrectFeedbackNarration.prepare(listOf(raw), language, true))
                val segments = parts.indices.associateWith { parts[it] }
                val mapping = NarrationPuzzleWordMappings.forViews(8, listOf(81 to displayed),
                    segments, language, feedback = true)
                assertEquals("No visible verdict, guide or detail falls back to block reading in $language", segments.keys, mapping.keys)
                val words = mapping.values.flatMap { it.displayed }
                assertEquals("The decorative cross advances with the first pronounced word", 0, words.first().start)
                assertTrue(words.first().endExclusive <= displayed.indexOf('\n') + 1)
                assertTrue("The first word cannot illuminate the rest of the feedback", words.first().endExclusive < displayed.length)
                assertTrue("Generic guidance and detail keep independent word ownership", words.distinct().size > 10)
                assertEquals(displayed.indices.toSet(), words.flatMap { it.start until it.endExclusive }.toSet())
                assertTrue(mapping.values.all { it.targets!!.all { target -> target == 81 } })
                assertTrue(mapping.values.flatMap { it.displayed }.none { it.start == 0 && it.endExclusive == displayed.length })
                mapping.forEach { (segment, item) ->
                    assertEquals(NarrationSpokenWords.technicalAtoms(segments.getValue(segment), language)
                        .map { ReadingTextRange(it.start, it.endExclusive) }, item.spoken)
                }
                assertRanges(listOf(81 to displayed), mapping)
                assertEquals(before, displayed.toString())
            }
        }
    }

    @Test fun anIncorrectVerdictCannotBorrowAHiddenOrChangedFeedbackTranscript() {
        val visible = "✗ Incorrecto.\nRevisa la relación de la opción elegida."
        val actual = NarrationSegments.sentences(IncorrectFeedbackNarration.prepare(listOf(visible),
            NarrationLanguage.SPANISH, true)).withIndex().associate { it.index to it.value }
        val valid = NarrationPuzzleWordMappings.forViews(8, listOf(81 to visible), actual,
            NarrationLanguage.SPANISH, feedback = true)
        assertEquals(actual.keys, valid.keys)
        val changed = NarrationSegments.sentences(IncorrectFeedbackNarration.prepare(
            listOf("Correcto. La opción ausente es la respuesta."), NarrationLanguage.SPANISH, true))
            .withIndex().associate { it.index to it.value }
        assertTrue(NarrationPuzzleWordMappings.forViews(8, listOf(81 to visible), changed,
            NarrationLanguage.SPANISH, feedback = true).isEmpty())
    }

    @Test fun repeatedNativeWrongMathReviewMapsUnmarkedRelationsAndPartialTypesetSpansWithoutWholeSentenceFallback() {
        val review = "Revisa la igualdad 6 × 100 = 600 y la descomposición 6000 + 300 + 8 = 6308; " +
            "cada símbolo conserva el significado de la expresión y después debes comparar los precios."
        val visible = "✗ Revisa tu respuesta e inténtalo de nuevo.\nRespuesta 1: $review"
        val expressions = listOf("6 × 100 = 600", "6000 + 300 + 8 = 6308")
        // The current native auto renderer creates arithmetic spans, while '=' and
        // its right-hand side remain ordinary text. The source characters stay identical.
        val spans = listOf("6 × 100", "6000 + 300 + 8").map { arithmetic ->
            val start = visible.indexOf(arithmetic)
            ReadingTextRange(start, start + arithmetic.length)
        }
        val parts = NarrationSegments.sentences(IncorrectFeedbackNarration.prepare(listOf(visible),
            NarrationLanguage.SPANISH, true))
        val segments = parts.indices.associateWith { parts[it] }
        for (shown in listOf<CharSequence>(visible, NarrationSourceProjection.Source(visible, spans))) {
            val mapping = NarrationPuzzleWordMappings.forViews(8, listOf(81 to shown), segments,
                NarrationLanguage.SPANISH, feedback = true)
            assertEquals("The complete actual review must map both with and without native formula metadata", segments.keys, mapping.keys)
            val ownership = mapping.values.flatMap { it.displayed }.distinct()
            for (expression in expressions) {
                val start = visible.indexOf(expression)
                val atoms = Regex("\\d+|[×+=]").findAll(expression).map {
                    ReadingTextRange(start + it.range.first, start + it.range.last + 1)
                }.toList()
                assertTrue("${expression}: every number and operator must keep a spoken owner", atoms.all { atom ->
                    ownership.any { it.start < atom.endExclusive && it.endExclusive > atom.start }
                })
                assertTrue("${expression}: normalization must retain several independent formula owners",
                    ownership.count { it.start < start + expression.length && it.endExclusive > start } >= 3)
            }
            assertEquals(visible.indices.toSet(), ownership.flatMap { it.start until it.endExclusive }.toSet())
            assertRanges(listOf(81 to shown), mapping)
        }
    }

    /** Test the existing pure semantic projection; native span creation belongs to Android QA. */
    private fun formulaSourceOrText(full: String): CharSequence {
        val opening = full.indexOf("\\(")
        if (opening < 0) return full
        val closing = full.indexOf("\\)", opening + 2)
        require(closing > opening + 2)
        val shown = full.substring(0, opening) + full.substring(opening + 2, closing) + full.substring(closing + 2)
        return NarrationSourceProjection.Source(shown, listOf(ReadingTextRange(opening, closing - 2)))
    }

    private fun mapped(views: List<Pair<Int, CharSequence>>, segments: Map<Int, String>) =
        NarrationPuzzleWordMappings.forViews(8, views, segments, NarrationLanguage.SPANISH)

    private fun assertRanges(views: List<Pair<Int, CharSequence>>, mapping: Map<Int, NarrationWordMapping>) {
        for (item in mapping.values) {
            assertEquals(item.spoken.size, item.displayed.size)
            assertEquals(item.spoken.size, item.targets!!.size)
            for (word in item.displayed.indices) {
                val text = views.single { it.first == item.targets[word] }.second
                val range = item.displayed[word]
                assertTrue(range.start >= 0 && range.endExclusive <= text.length && range.endExclusive > range.start)
            }
        }
    }
}
