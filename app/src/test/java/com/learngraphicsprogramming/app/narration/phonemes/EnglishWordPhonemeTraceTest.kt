package com.learngraphicsprogramming.app.narration.phonemes

import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

/** Native-model timing is checked on Android; these fixtures verify exact frontend provenance. */
class EnglishWordPhonemeTraceTest {
    private fun lexicon(british: Boolean = false) = MisakiLexicon(JSONObject("""
        {"They":"ðA","record":{"DEFAULT":"ɹˈɛkəɹd","VERB":"ɹəkˈɔɹd"},
         "I":"ˈI","can't":"kˈænt","read":"ɹˈid","it":"ɪt","Use":"jˈuz",
         "well-known":"wˌɛlnˈOn","tools":"tˈulz","the":"ði","apple":"ˈæpəl",
         "and":"ænd","book":"bˈʊk","to":"tu","page":"pˈAʤ","hello":"həlˈO"}
    """), JSONObject(), british)

    private fun tokens(text: String, vararg words: Pair<String, String>): List<MisakiWord> {
        var cursor = 0
        return words.map { (word, tag) ->
            val start = text.indexOf(word, cursor)
            check(start >= cursor)
            cursor = start + word.length
            val whitespace = text.substring(cursor).takeWhile { it.isWhitespace() }
            MisakiWord(word, whitespace, tag, sourceStart = start, sourceEndExclusive = cursor)
        }
    }

    private fun trace(text: String, tokens: List<MisakiWord>, british: Boolean = false,
        fallback: ((String) -> String)? = null): EnglishMisakiG2P.EnglishPhoneTrace {
        val pieces = mutableListOf<EnglishMisakiG2P.PhonePiece>()
        val plain = MisakiEnglishPipeline.speak(tokens.map { it.copy() }, lexicon(british), fallback)
        val observed = MisakiEnglishPipeline.speakObserved(tokens.map { it.copy() }, lexicon(british), fallback, {},
            { token, start, end -> pieces += EnglishMisakiG2P.PhonePiece(token.sourceStart, token.sourceEndExclusive, start, end) })
        assertEquals("Observing must retain the exact contextual production phones", plain, observed)
        return assertNotNullTrace(EnglishMisakiG2P.traceResolved(text, observed, pieces))
    }

    private fun assertNotNullTrace(trace: EnglishMisakiG2P.EnglishPhoneTrace?): EnglishMisakiG2P.EnglishPhoneTrace {
        assertNotNull(trace)
        return checkNotNull(trace)
    }

    @Test fun homographsObserveTheSameWholeSentencePosAndPhoneRanges() {
        val text = "They record a record."
        val trace = trace(text, tokens(text, "They" to "PRP", "record" to "VBP", "a" to "DT", "record" to "NN", "." to "."))
        assertEquals(listOf("they", "record", "a", "record"), trace.words.map { it.word })
        assertEquals("ɹəkˈɔɹd", trace.phonemes.substring(trace.words[1].start, trace.words[1].endExclusive))
        assertEquals("ɹˈɛkəɹd", trace.phonemes.substring(trace.words[3].start, trace.words[3].endExclusive))
        trace.words.forEach { assertEquals(it.word, text.substring(it.sourceStart, it.sourceEndExclusive).lowercase()) }
    }

    @Test fun spacyContractionPiecesRemainOneActualIndivisiblePronunciation() {
        val text = "I can't read it."
        val trace = trace(text, tokens(text, "I" to "PRP", "ca" to "MD", "n't" to "RB", "read" to "VB", "it" to "PRP", "." to "."))
        assertEquals(listOf("i", "can't", "read", "it"), trace.words.map { it.word })
        val contraction = trace.words[1]
        assertEquals("can't", text.substring(contraction.sourceStart, contraction.sourceEndExclusive))
        assertEquals("kˈænt", trace.phonemes.substring(contraction.start, contraction.endExclusive))
    }

    @Test fun aDictionaryCompoundNeverInventsInternalSubwordTimes() {
        val text = "Use well-known tools."
        val trace = trace(text, tokens(text, "Use" to "VB", "well" to "RB", "-" to "HYPH", "known" to "VBN", "tools" to "NNS", "." to "."))
        assertEquals(listOf("use", "well-known", "tools"), trace.words.map { it.word })
        val compound = trace.words[1]
        assertEquals("wˌɛlnˈOn", trace.phonemes.substring(compound.start, compound.endExclusive))
    }

    @Test fun contextualFunctionWordsAndPunctuationRetainExactBoundariesForBothVariants() {
        val text = "the apple, the book; to page."
        for (british in listOf(false, true)) {
            val trace = trace(text, tokens(text, "the" to "DT", "apple" to "NN", "," to ",", "the" to "DT", "book" to "NN", ";" to ":", "to" to "TO", "page" to "NN", "." to "."), british)
            assertEquals(listOf("the", "apple", "the", "book", "to", "page"), trace.words.map { it.word })
            assertEquals("ði", trace.phonemes.substring(trace.words[0].start, trace.words[0].endExclusive))
            assertEquals("ðə", trace.phonemes.substring(trace.words[2].start, trace.words[2].endExclusive))
            assertTrue(trace.words.zipWithNext().all { (left, right) -> left.endExclusive <= right.start })
        }
    }

    @Test fun bartFallbackIsObservedAfterResolutionWithoutSplittingOrChangingItsPhones() {
        val text = "hello outofdictionary."
        val trace = trace(text, tokens(text, "hello" to "UH", "outofdictionary" to "NN", "." to "."), fallback = {
            assertEquals("outofdictionary", it)
            "Wtˌɑfdˈɪkʃənˌɛɹi"
        })
        assertEquals("Wtˌɑfdˈɪkʃənˌɛɹi", trace.phonemes.substring(trace.words[1].start, trace.words[1].endExclusive))
    }

    @Test fun missingOrOverlappingOwnershipIsRejectedInsteadOfGuessing() {
        assertNull(EnglishMisakiG2P.traceResolved("hello book", "həlˈO", listOf(EnglishMisakiG2P.PhonePiece(0, 5, 0, 5))))
        assertNull(EnglishMisakiG2P.traceResolved("hello book", "həlˈObˈʊk", listOf(
            EnglishMisakiG2P.PhonePiece(0, 5, 0, 7), EnglishMisakiG2P.PhonePiece(6, 10, 5, 9))))
    }

    @Test fun qualifiedOutputOverridesRetainEachOriginalCodeWordWithoutRenderingNewText() {
        val text = "Use std::cout; read it."
        val frontend = EnglishMisakiG2P({ error("Token provenance does not open model resources") })
        try {
            val method = EnglishMisakiG2P::class.java.getDeclaredMethod("tokenize", String::class.java,
                EnglishMisakiSpacyPos::class.java, Boolean::class.javaPrimitiveType, Function0::class.java).apply { isAccessible = true }
            @Suppress("UNCHECKED_CAST")
            val tokens = method.invoke(frontend, text, null, true, {}) as List<MisakiWord>
            assertEquals(listOf("Use", "std", "cout", ";", "read", "it", "."), tokens.map { it.text })
            tokens.zip(listOf("VB", "NNP", "NNP", ":", "VB", "PRP", ".")).forEach { (token, tag) -> token.tag = tag }
            val trace = trace(text, tokens)
            assertEquals(listOf("use", "std", "cout", "read", "it"), trace.words.map { it.word })
            val std = trace.words[1]
            val cout = trace.words[2]
            assertEquals("std", text.substring(std.sourceStart, std.sourceEndExclusive))
            assertEquals("cout", text.substring(cout.sourceStart, cout.sourceEndExclusive))
            assertEquals("ˈɛs tˈi dˈi", trace.phonemes.substring(std.start, std.endExclusive))
            assertEquals("sˈi ˈWt", trace.phonemes.substring(cout.start, cout.endExclusive))
            assertFalse(trace.phonemes.contains("standard"))
        } finally { frontend.close() }
    }
}
