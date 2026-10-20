package com.learngraphicsprogramming.app.narration

import org.junit.Assert.*
import org.junit.Test
import org.json.JSONArray
import org.json.JSONObject

/** Count real mapping work; no wall-clock thresholds or simulated device performance. */
class NarrationPuzzleMappingCacheTest {
    @Test fun firstAutoSegmentRetainsEachOriginalNumericAndOperatorOwner() {
        val formula = "6 × 100 = 600"
        val auto = requireNotNull(NarrationSourceProjection.forLanguage(formula, "auto", NarrationLanguage.SPANISH,
            restoreFormulaIntent = false)).text
        val result = NarrationPuzzleWordMappings.forViews(8, listOf(17 to formula),
            linkedMapOf(0 to auto, 1 to formula), NarrationLanguage.SPANISH)
        assertEquals("A later raw formula cannot replace the first exact auto occurrence", setOf(0), result.keys)
        val word = result.getValue(0)
        assertEquals(setOf(17), word.targets!!.toSet())
        assertEquals(formula.indices.toSet(), word.displayed.flatMap { it.start until it.endExclusive }.toSet())
        assertTrue("Exact normalization retains independent numeric/operator glyph ownership", word.displayed.distinct().size >= 5)
    }

    @Test fun literalFastPathNeverDisplacesAnEarlierExactNormalizedTechnicalOccurrence() {
        val original = "La línea #include <iostream> usa std::cout."
        val normalized = requireNotNull(NarrationSourceProjection.forLanguage(original, "auto", NarrationLanguage.SPANISH)).text
        assertNotEquals(NarrationSpokenWords.lexemes(original).map { it.canonical },
            NarrationSpokenWords.lexemes(normalized).map { it.canonical })
        val result = NarrationPuzzleWordMappings.forViews(8, listOf(10 to original),
            linkedMapOf(0 to normalized, 1 to original), NarrationLanguage.SPANISH)
        assertEquals("The earlier normalized occurrence still wins over a later literal match", setOf(0), result.keys)
        assertEquals(setOf(10), result.values.flatMap { it.targets!! }.toSet())
        assertEquals(original.indices.toSet(), result.values.flatMap { it.displayed }
            .flatMap { it.start until it.endExclusive }.toSet())
    }

    @Test fun firstLiteralSegmentStillUsesTheCompleteOrderedUnionOfOriginalViews() {
        val views = listOf(10 to "Lee bien.", 11 to "Continúa aquí.", 12 to "No anticipes esta opción.")
        val result = NarrationPuzzleWordMappings.forViews(8, views,
            linkedMapOf(0 to "Lee bien. Continúa aquí.", 1 to "Lee bien.", 2 to "No anticipes esta opción."),
            NarrationLanguage.SPANISH)
        assertEquals(setOf(0, 2), result.keys)
        assertEquals(setOf(10, 11), result.getValue(0).targets!!.toSet())
        assertEquals(setOf(12), result.getValue(2).targets!!.toSet())
    }

    @Test fun onlyKnownFixedPassiveLabelsMaySkipAnAlreadyEmptyFeedbackRebuild() {
        fun puzzle(vararg types: String) = JSONObject().put("type", "math_puzzle").put("responses",
            JSONArray().apply { types.forEach { put(JSONObject().put("type", it)) } })
        assertTrue(NarrationPuzzlePresentationChanges.hasFixedTextOrder(puzzle("math_multi_input", "math_multi_input")))
        assertFalse("Other puzzle families keep their existing refresh behavior",
            NarrationPuzzlePresentationChanges.hasFixedTextOrder(JSONObject().put("type", "output_prediction")))
        for (type in listOf("math_ordering_drag", "math_matching_drag", "order_code", "drag_fill_code", "future_unknown_control")) {
            assertFalse("The actual current card positions must still be rebuilt: $type",
                NarrationPuzzlePresentationChanges.hasFixedTextOrder(puzzle("math_multi_input", type)))
            assertFalse(NarrationPuzzlePresentationChanges.hasFixedTextOrder(JSONObject().put("type", type)))
        }
        val interactive = puzzle("math_multi_input").put("tools", JSONArray().put(JSONObject().put("interactive", true)))
        assertFalse(NarrationPuzzlePresentationChanges.hasFixedTextOrder(interactive))
        assertFalse(NarrationPuzzlePresentationChanges.hasFixedTextOrder(null))
    }

    private class Probe {
        var computations = 0
        val cache = NarrationPuzzleWordMappings.Cache { block, views, segments, language, feedback ->
            computations++
            NarrationPuzzleWordMappings.forViews(block, views, segments, language, feedback)
        }
    }

    @Test fun identicalNativeSourcesReuseTheirCompleteVerifiedMapping() {
        val probe = Probe()
        val first = probe.cache.forViews(8, listOf(10 to "Lee bien.", 11 to "Comprueba el mensaje."),
            linkedMapOf(0 to "Lee bien.", 1 to "Comprueba el mensaje."), NarrationLanguage.SPANISH)
        repeat(15) {
            assertSame(first, probe.cache.forViews(8,
                listOf(10 to StringBuilder("Lee bien."), 11 to StringBuilder("Comprueba el mensaje.")),
                linkedMapOf(1 to "Comprueba el mensaje.", 0 to "Lee bien."), NarrationLanguage.SPANISH))
        }
        assertEquals("Unchanged field callbacks perform one actual mapping, not sixteen", 1, probe.computations)
        assertEquals(setOf(0, 1), first.keys)
    }

    @Test fun mutableDisplayedTextAndExactSpokenSegmentsInvalidateIndependently() {
        val probe = Probe()
        val shown = StringBuilder("Lee bien.")
        val segments = linkedMapOf(0 to "Lee bien.")
        assertEquals(setOf(0), probe.cache.forViews(8, listOf(10 to shown), segments, NarrationLanguage.SPANISH).keys)
        shown.replace(0, shown.length, "Lee despacio.")
        assertTrue(probe.cache.forViews(8, listOf(10 to shown), segments, NarrationLanguage.SPANISH).isEmpty())
        segments[0] = "Lee despacio."
        assertEquals(setOf(0), probe.cache.forViews(8, listOf(10 to shown), segments, NarrationLanguage.SPANISH).keys)
        assertEquals(3, probe.computations)
    }

    @Test fun reorderedRepeatedViewsAndNewTargetIdentityCannotReuseTheOldOwners() {
        val probe = Probe()
        val segments = mapOf(0 to "Iguales.", 1 to "Iguales.")
        val first = probe.cache.forViews(8, listOf(10 to "Iguales.", 11 to "Iguales."), segments, NarrationLanguage.SPANISH)
        val reordered = probe.cache.forViews(8, listOf(11 to "Iguales.", 10 to "Iguales."), segments, NarrationLanguage.SPANISH)
        assertEquals(listOf(10), first.getValue(0).targets)
        assertEquals(listOf(11), reordered.getValue(0).targets)
        val replaced = probe.cache.forViews(8, listOf(42 to "Iguales.", 10 to "Iguales."), segments, NarrationLanguage.SPANISH)
        assertEquals(listOf(42), replaced.getValue(0).targets)
        assertEquals(3, probe.computations)
    }

    @Test fun formulaIntentParticipatesInTheKeyEvenWithIdenticalOriginalCharacters() {
        val probe = Probe()
        val plain = NarrationSourceProjection.Source("x + 2", emptyList())
        val formula = NarrationSourceProjection.Source(plain.text, listOf(ReadingTextRange(0, plain.length)))
        val segments = mapOf(0 to requireNotNull(NarrationSourceProjection.forLanguage(formula, "auto", NarrationLanguage.SPANISH)).text)
        probe.cache.forViews(8, listOf(10 to plain), segments, NarrationLanguage.SPANISH)
        val result = probe.cache.forViews(8, listOf(10 to formula), segments, NarrationLanguage.SPANISH)
        assertEquals("Semantic formula ranges cannot borrow a text-only cached result", 2, probe.computations)
        assertSame(result, probe.cache.forViews(8, listOf(10 to formula.copy(formulas = formula.formulas.toList())),
            segments, NarrationLanguage.SPANISH))
        assertEquals(2, probe.computations)
    }

    @Test fun languageAndFeedbackHaveSeparateExactMappingContracts() {
        val probe = Probe()
        val views = listOf(10 to "Correct.")
        val segments = mapOf(0 to "Correct.")
        val normal = probe.cache.forViews(8, views, segments, NarrationLanguage.AMERICAN_ENGLISH)
        val feedback = probe.cache.forViews(8, views, segments, NarrationLanguage.AMERICAN_ENGLISH, feedback = true)
        assertSame(normal, probe.cache.forViews(8, views, segments, NarrationLanguage.AMERICAN_ENGLISH))
        assertSame(feedback, probe.cache.forViews(8, views, segments, NarrationLanguage.AMERICAN_ENGLISH, feedback = true))
        probe.cache.forViews(8, views, segments, NarrationLanguage.BRITISH_ENGLISH)
        assertEquals(3, probe.computations)
    }

    @Test fun eachSlotRetainsOnlyItsCurrentMappingAndPageClearReleasesIt() {
        val probe = Probe()
        val original = listOf(10 to "Lee bien.")
        val segments = mapOf(0 to "Lee bien.")
        probe.cache.forViews(8, original, segments, NarrationLanguage.SPANISH)
        probe.cache.forViews(8, listOf(10 to "Otro texto."), mapOf(0 to "Otro texto."), NarrationLanguage.SPANISH)
        probe.cache.forViews(8, original, segments, NarrationLanguage.SPANISH)
        assertEquals("This is not an unbounded history of previously presented text", 3, probe.computations)
        probe.cache.clear()
        probe.cache.forViews(8, original, segments, NarrationLanguage.SPANISH)
        assertEquals(4, probe.computations)
    }
}
