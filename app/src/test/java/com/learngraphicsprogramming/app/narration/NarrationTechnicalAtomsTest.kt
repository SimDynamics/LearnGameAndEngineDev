package com.learngraphicsprogramming.app.narration

import org.junit.Assert.*
import org.junit.Test

/** Source ownership only; Android tests exercise the real waveform and model frames. */
class NarrationTechnicalAtomsTest {
    private val technicalLanguages = listOf(NarrationLanguage.AMERICAN_ENGLISH,
        NarrationLanguage.BRITISH_ENGLISH, NarrationLanguage.FRENCH)

    @Test fun explicitNotationHasItsOwnAtomWithoutChangingOrdinarySpanishLexemes() {
        val text = "Use #include <iostream>; std::cout { value > 0; }"
        val expected = listOf("Use", "#", "include", "<", "iostream", ">", "std", "::", "cout", "{", "value", ">", "0", "}")
        for (language in technicalLanguages) {
            val atoms = NarrationSpokenWords.technicalAtoms(text, language)
            assertEquals(language.tag, expected, atoms.map { text.substring(it.start, it.endExclusive) })
            assertTrue(atoms.zipWithNext().all { (a, b) -> a.endExclusive <= b.start })
        }
        assertEquals(NarrationSpokenWords.lexemes(text),
            NarrationSpokenWords.technicalAtoms(text, NarrationLanguage.SPANISH))
    }

    @Test fun annotationsUrlsAndSignedNumbersDoNotAcquireOverlappingSourceAtoms() {
        val include = ManualEnglishTerms.entries.single { it.spelling == "include" }
        val encoded = ManualEnglishSpeech.PREPARED + "#" + ManualEnglishSpeech.encode(include, code = false) + " std::cout -12."
        for (language in technicalLanguages) {
            val atoms = NarrationSpokenWords.technicalAtoms(encoded, language)
            assertEquals(listOf("#", "include", "std", "::", "cout", "-12"), atoms.map { it.canonical })
            assertTrue(atoms.zipWithNext().all { (a, b) -> a.endExclusive <= b.start })
            for (url in listOf("https://example.test/a#b?x=1", "www.example.test/<foo>", "[read](https://example.test/#x)")) {
                assertEquals(url, NarrationSpokenWords.lexemes(url), NarrationSpokenWords.technicalAtoms(url, language))
            }
        }
    }

    @Test fun symbolRangesKeepEveryOriginalGlyphAndNeverMergeTheFollowingProse() {
        for (language in technicalLanguages) {
            val source = "Use #include <iostream>; std::cout and std:: indicates { value > 0; }."
            val mapping = NarrationWordMappings.forBlock(2, source, mapOf(5 to source), language).getValue(5)
            assertEquals(source.indices.toList(), mapping.displayed.flatMap { it.start until it.endExclusive })
            val atoms = mapping.spoken.map { source.substring(it.start, it.endExclusive) }
            assertEquals(2, atoms.count { it == "::" })
            val scope = atoms.lastIndexOf("::")
            assertEquals(":: ", source.substring(mapping.displayed[scope].start, mapping.displayed[scope].endExclusive))
            assertEquals("indicates ", source.substring(mapping.displayed[scope + 1].start, mapping.displayed[scope + 1].endExclusive))
        }
    }

    @Test fun annotatedFrenchTechnicalWordsRetainIndependentHashAndScopeOwnership() {
        val source = "La ligne #include <iostream> utilise std::cout; std:: indique une portée."
        val prepared = FrenchTechnicalSpeech.prepareForKokoro(source)
        val mapping = NarrationWordMappings.forBlock(1, source, mapOf(3 to prepared), NarrationLanguage.FRENCH).getValue(3)
        val display = mapping.displayed.map { source.substring(it.start, it.endExclusive) }
        assertEquals(1, display.count { it == "#" })
        assertEquals(2, display.count { it.startsWith("::") })
        assertEquals(source.indices.toList(), mapping.displayed.flatMap { it.start until it.endExclusive })
        assertEquals(1, display.count { it == "indique " })
    }

    @Test fun symbolTimingUsesOnlyItsObservedModelPhoneBoundaries() {
        val source = "Use std::cout #."
        for (language in technicalLanguages) {
            val atoms = NarrationSpokenWords.technicalAtoms(source, language)
            var cursor = 0
            val sources = atoms.map { atom ->
                val width = if (source.substring(atom.start, atom.endExclusive) == "::") 3 else 1
                NarrationSourcePhones(atom.start, atom.endExclusive, cursor, cursor + width).also { cursor += width }
            }
            val phones = "a".repeat(cursor)
            val frames = LongArray(cursor + 2) { (it % 4 + 1).toLong() }
            val samples = frames.sum() * 600L
            val alignment = requireNotNull(NarrationWordTimingBuilder.buildSourceChunked(source, "0".repeat(64),
                samples, phones, sources, listOf(phones), listOf(frames), language))
            assertTrue(NarrationWordTimingBuilder.valid(source, samples, alignment.words, language))
            for ((word, observed) in alignment.words.zip(sources)) {
                assertEquals(frames.take(observed.phoneStart + 1).sum() * 600L, word.startSample)
                assertEquals(frames.take(observed.phoneEndExclusive + 1).sum() * 600L, word.endSample)
            }
            val scope = atoms.indexOfFirst { source.substring(it.start, it.endExclusive) == "::" }
            assertNull(NarrationWordTimingBuilder.buildSourceChunked(source, "0".repeat(64), samples,
                phones, sources.filterIndexed { index, _ -> index != scope }, listOf(phones), listOf(frames), language))
            assertFalse(NarrationWordTimingBuilder.valid(source, samples,
                alignment.words.filterIndexed { index, _ -> index != scope }, language))
        }
    }
}
