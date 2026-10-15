package com.learngraphicsprogramming.app.narration.phonemes

import com.learngraphicsprogramming.app.narration.EnglishTechnicalSpeech
import com.learngraphicsprogramming.app.narration.ManualEnglishPronunciations
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class EnglishTechnicalSymbolsTest {
    private fun readings(source: String) = EnglishTechnicalSpeech.replacements(source).map {
        source.substring(it.start, it.endExclusive) to it.spoken
    }

    @Test fun requiredCppSymbolsHaveExplicitEnglishNamesWithoutAddingAuthoredWords() {
        val source = "Read #include <iostream>, { std::cout; } and std:: indicates a namespace."
        assertEquals(listOf("#" to "hash", "<" to "less than", ">" to "greater than",
            "{" to "open brace", "std" to "std", "::" to "scope resolution operator", "cout" to "cout",
            "}" to "close brace", "std" to "std", "::" to "scope resolution operator"), readings(source))
        val replacements = EnglishTechnicalSpeech.replacements(source)
        assertTrue(replacements.zipWithNext().all { (left, right) -> left.endExclusive <= right.start })
        assertEquals(2, replacements.count { it.phonemes == EnglishTechnicalSpeech.STD_PHONES })
        assertEquals(ManualEnglishPronunciations.phones.getValue("cout"), replacements.single { it.spoken == "cout" }.phonemes)
        assertEquals("Read #include <iostream>, { std::cout; } and std:: indicates a namespace.", source)
    }

    @Test fun compoundOperatorsAreReadAsWholeOperatorsInsteadOfDroppingTheirSigns() {
        val examples = linkedMapOf("<=>" to "three way comparison operator",
            "<<=" to "left shift equals", ">>=" to "right shift equals", "->*" to "arrow asterisk",
            "##" to "double hash", "&&" to "logical and", "||" to "logical or",
            "==" to "equals equals", "!=" to "not equal to", "<=" to "less than or equal to",
            ">=" to "greater than or equal to", "<<" to "left shift", ">>" to "right shift",
            "++" to "plus plus", "--" to "minus minus", "+=" to "plus equals", "-=" to "minus equals",
            "*=" to "asterisk equals", "/=" to "slash equals", "%=" to "percent equals",
            "&=" to "ampersand equals", "|=" to "vertical bar equals", "^=" to "caret equals",
            "->" to "arrow", ".*" to "dot asterisk", "::" to "scope resolution operator")
        examples.forEach { (symbol, spoken) ->
            assertEquals(symbol, listOf(symbol to spoken), readings(symbol))
        }
        assertEquals(listOf("[", "]", "=", "+", "*", "/", "\\", "%", "&", "|", "^", "~", "_", "!"),
            EnglishTechnicalSpeech.replacements("[] = + * / \\ % & | ^ ~ _ !valid").map {
                "[] = + * / \\ % & | ^ ~ _ !valid".substring(it.start, it.endExclusive)
            })
    }

    @Test fun ordinaryWordsHyphensContractionsUrlsAndExplicitPhonemeLinksKeepTheirReading() {
        for (source in listOf("The standard result isn't a well-known word; read it!",
            "One - another aside; keep FP-32 and well-known intact.", "- First list item",
            "One -- another aside.", "Stop!Now continue. Yes!!Next sentence.",
            "https://example.org/#include/std::cout?q=<value>", "www.example.org/std::cout",
            "[std::cout](/sˈi ˈWt/)", "The identifiers stdio and standard retain their spelling.")) {
            assertTrue(source, EnglishTechnicalSpeech.replacements(source).isEmpty())
        }
        for (source in listOf("mystd:: indicates", "std_total::", "STD::", "stdé::", "std\u0301::")) {
            assertTrue(source, EnglishTechnicalSpeech.replacements(source).none { it.phonemes == EnglishTechnicalSpeech.STD_PHONES })
        }
    }

    @Test fun explicitSubtractionDecrementAndNegationRemainReadableWithoutNamingProsePunctuation() {
        for (source in listOf("-3", "8-3", "The - token", "a - sign", "unary -", "'-'", "`left-right`")) {
            assertTrue(source, readings(source).contains("-" to "minus"))
        }
        for (source in listOf("--counter", "counter--", "`counter --`")) {
            assertTrue(source, readings(source).contains("--" to "minus minus"))
        }
        for (source in listOf("!valid", "(!valid)", "!!valid", "'!'", "`! valid`")) {
            assertTrue(source, readings(source).contains("!" to "logical not"))
        }
        assertEquals(2, readings("!!valid").count { it == ("!" to "logical not") })
    }

    @Test fun stdAndScopeAreSeparateTokensWithExactOriginalOwnership() {
        val source = "std:: indicates a namespace; std::cout continues."
        val tokens = tokenize(source)
        assertEquals(listOf("std", "scope", "resolution", "operator", "indicates", "a", "namespace", ";",
            "std", "scope", "resolution", "operator", "cout", "continues", "."), tokens.map { it.text })
        val standards = tokens.filter { it.override == EnglishTechnicalSpeech.STD_PHONES }
        assertEquals(2, standards.size)
        standards.forEach { assertEquals("std", source.substring(it.sourceStart, it.sourceEndExclusive)) }
        tokens.filter { it.text in setOf("scope", "resolution", "operator") }.forEach {
            assertEquals("::", source.substring(it.sourceStart, it.sourceEndExclusive))
        }
        assertEquals("sˈi ˈWt", tokens.single { it.text == "cout" }.override)
    }

    @Test fun nativePipelineRetainsSymbolPhonesAndExactSourceAtomsForBothVariants() {
        val source = "# { } < > std::cout"
        val vocabulary = JSONObject("""{"hash":"hˈæʃ","open":"ˈOpən","close":"klˈOz",
            "brace":"bɹˈAs","less":"lˈɛs","than":"ðæn","greater":"ɡɹˈAtəɹ",
            "scope":"skˈOp","resolution":"ɹˌɛzəlˈuʃən","operator":"ˈɑpəɹˌAtəɹ"}""")
        for (british in listOf(false, true)) {
            val pieces = mutableListOf<EnglishMisakiG2P.PhonePiece>()
            val tokens = tokenize(source).onEach { it.tag = "NN" }
            val phones = MisakiEnglishPipeline.speakObserved(tokens, MisakiLexicon(vocabulary, JSONObject(), british),
                { error("A named technical symbol must not need BART: $it") }, {}, { word, start, end ->
                    pieces += EnglishMisakiG2P.PhonePiece(word.sourceStart, word.sourceEndExclusive, start, end)
                })
            val untracked = tokens.map { it.copy(sourceStart = -1, sourceEndExclusive = -1) }
            assertEquals("Observing rewritten source atoms must not change synthesis phones", phones,
                MisakiEnglishPipeline.speak(untracked, MisakiLexicon(vocabulary, JSONObject(), british), null))
            assertTrue("Expanded words retain the source symbol rather than their generated word length",
                pieces.all { it.sourceStart >= 0 && it.sourceStart < it.sourceEndExclusive && it.sourceEndExclusive <= source.length })
            val trace = requireNotNull(EnglishMisakiG2P.traceResolved(source, phones, pieces))
            assertEquals(listOf("#", "{", "}", "<", ">", "std", "::", "cout"), trace.words.map { it.word })
            assertEquals("hˈæʃ", phones.substring(trace.words.first().start, trace.words.first().endExclusive))
            assertEquals("skˈOp ɹˌɛzəlˈuʃən ˈɑpəɹˌAtəɹ", phones.substring(trace.words[6].start, trace.words[6].endExclusive))
            trace.words.forEach { assertEquals(it.word, source.substring(it.sourceStart, it.sourceEndExclusive)) }
            assertTrue(trace.words.zipWithNext().all { (left, right) -> left.endExclusive <= right.start })
        }
    }

    @Test fun spacySpaceTokensNeverBecomeUnknownSpokenWordsAfterTechnicalExpansion() {
        // Use the production spaCy tokenizer algorithm, including the rule that only
        // its first ASCII space is attached to the preceding word. Native model/POS
        // predictions and exact dictionary phones are also checked on Android.
        val configuration = JSONObject().put("format", 1).put("pipeline", "en_core_web_sm").put("pipelineVersion", "3.8.0")
            .put("labels", JSONArray((0 until 50).map { "tag$it" })).put("seeds", JSONArray(listOf(8, 9, 10, 11, 12, 13)))
            .put("rows", JSONArray(listOf(5000, 1000, 2500, 2500, 50, 50)))
            .put("symbols", JSONObject()).put("lowercase", JSONObject()).put("shapeRanges", JSONArray())
            .put("whitespace", JSONArray(listOf(9, 10, 13, 32))).put("lexemeNorm", JSONObject())
            .put("baseNorms", JSONObject()).put("rules", JSONObject())
        for (name in listOf("prefix", "infix", "tokenMatch", "urlMatch")) configuration.put(name, JSONObject.NULL)
        configuration.put("suffix", JSONObject().put("source", "[.,;!?]$").put("ignoreCase", false)
            .put("multiline", false).put("dotAll", false))
        val spacy = SpacyPosFrontend(configuration)
        assertTrue("The fixture reproduces spaCy's separate SPACE token", spacy.tokenize("one  two").any { it.text.isBlank() })
        val frontend = EnglishMisakiG2P({ error("Token provenance does not load model resources") })
        try {
            val source = "The #include <iostream> line contains { std::cout } and std:: indicates scope."
            var prepared = ""
            val tokens = frontend.tokenizeUsing(source, { text ->
                prepared = text
                spacy.tokenize(text).map { SpacyTaggedToken(it.text, it.whitespace, "NN") }
            }, true)
            assertEquals("The hash include less than iostream greater than line contains open brace std scope resolution operator cout close brace and std scope resolution operator indicates scope.", prepared)
            assertTrue("No artificial SPACE token is introduced", spacy.tokenize(prepared).none { it.text.isBlank() })
            assertTrue("Only lexical/technical tokens reach Misaki", tokens.none { it.text.isBlank() })
            val multiline = "\tThe  #include\nstd::cout\tends."
            var authoredSpaceTokens = false
            val multilineTokens = frontend.tokenizeUsing(multiline, { text ->
                val actual = spacy.tokenize(text)
                authoredSpaceTokens = actual.any { it.text.isBlank() }
                actual.map { SpacyTaggedToken(it.text, it.whitespace, "NN") }
            }, true)
            assertTrue("Authored tabs/newlines/extra spaces exercise the native SPACE-token path", authoredSpaceTokens)
            assertTrue("Authored whitespace also stays out of lexical fallback", multilineTokens.none { it.text.isBlank() })
            assertTrue(multilineTokens.all { it.sourceStart >= 0 && it.sourceEndExclusive <= multiline.length })
            assertTrue("Authored spacing stays separation rather than a spoken word", multilineTokens.any { it.whitespace.length > 1 || '\n' in it.whitespace || '\t' in it.whitespace })
        } finally { frontend.close() }
    }

    private fun tokenize(source: String): List<MisakiWord> {
        val frontend = EnglishMisakiG2P({ error("Token provenance does not load model resources") })
        return try {
            val method = EnglishMisakiG2P::class.java.getDeclaredMethod("tokenize", String::class.java,
                EnglishMisakiSpacyPos::class.java, Boolean::class.javaPrimitiveType, Function0::class.java).apply { isAccessible = true }
            @Suppress("UNCHECKED_CAST")
            (method.invoke(frontend, source, null, true, {}) as List<MisakiWord>)
        } finally { frontend.close() }
    }
}
