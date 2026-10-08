package com.learngraphicsprogramming.app.narration.phonemes

import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

/** Structural fixtures exercise the upstream pipeline; the exported neural goldens run separately. */
class MisakiEnglishPipelineTest {
    private fun lexicon() = MisakiLexicon(JSONObject("""{"doesn't":"dˈʌzᵊnt","work":"wˈɜɹk",
        "hello":"həlˈO","world":"wˈɜɹld","can":"kæn","the":"ði","to":"tu","A":"ˈA","B":"bˈi"}"""), JSONObject(), false)

    @Test fun adjacentSpacyContractionTokensResolveAsTheOriginalWholeWord() {
        val tokens = listOf(MisakiWord("does", "", "VBZ"), MisakiWord("n't", " ", "RB"), MisakiWord("work", "", "VB"))
        assertEquals("dˈʌzᵊnt wˈɜɹk", MisakiEnglishPipeline.speak(tokens, lexicon(), null))
    }

    @Test fun PennTreebankPunctuationControlsDelimitersAndPreservesPauseBoundaries() {
        val tokens = listOf(MisakiWord("(", "", "-LRB-"), MisakiWord("hello", " ", "UH"),
            MisakiWord("world", "", "NN"), MisakiWord(")", "", "-RRB-"), MisakiWord("!", "", "."))
        assertEquals("(həlˈO wˈɜɹld)!", MisakiEnglishPipeline.speak(tokens, lexicon(), null))
    }

    @Test fun fallbackReceivesOriginalCasedUnknownWordAndReturnsOnlyItsPhonemes() {
        val requested = mutableListOf<String>()
        val tokens = listOf(MisakiWord("hello", " ", "UH"), MisakiWord("Graphi", "", "NNP"))
        val result = MisakiEnglishPipeline.speak(tokens, lexicon()) { word -> requested += word; "ɡɹˈæfi" }
        assertEquals(listOf("Graphi"), requested)
        assertEquals("həlˈO ɡɹˈæfi", result)
        assertFalse(result.contains("Graphi"))
    }

    @Test fun failedOptionalFallbackProducesAControlledErrorRatherThanPlaceholderSpeech() {
        assertThrows(EnglishMisakiException::class.java) {
            MisakiEnglishPipeline.speak(listOf(MisakiWord("outofdictionary", tag = "NN")), lexicon(), null)
        }
    }
}
