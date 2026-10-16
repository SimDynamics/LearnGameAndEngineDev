package com.learngraphicsprogramming.app.narration.phonemes

import com.learngraphicsprogramming.app.narration.EnglishTechnicalSpeech
import com.learngraphicsprogramming.app.narration.LessonNarrationText
import com.learngraphicsprogramming.app.narration.ManualEnglishPronunciations
import com.learngraphicsprogramming.app.narration.NarrationLanguage
import com.learngraphicsprogramming.app.narration.NarrationSegments
import com.learngraphicsprogramming.app.narration.NarrationWordMappings
import java.io.File
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
            assertTrue(source, readings(source).contains("--" to "decrement operator"))
        }
        for (source in listOf("!valid", "(!valid)", "!!valid", "'!'", "`! valid`")) {
            assertTrue(source, readings(source).contains("!" to "logical not"))
        }
        assertEquals(2, readings("!!valid").count { it == ("!" to "logical not") })
    }

    @Test fun compoundAssignmentOperationsUseTheirCppNamesOnlyInOperationContext() {
        val names = linkedMapOf("<<=" to "left shift assignment operator", ">>=" to "right shift assignment operator",
            "+=" to "addition assignment operator", "-=" to "subtraction assignment operator",
            "*=" to "multiplication assignment operator", "/=" to "division assignment operator",
            "%=" to "remainder assignment operator", "&=" to "bitwise and assignment operator",
            "|=" to "bitwise or assignment operator", "^=" to "bitwise exclusive or assignment operator",
            "==" to "equality operator", "->*" to "pointer to member operator", ".*" to "pointer to member operator")
        names.forEach { (symbol, name) ->
            assertTrue(symbol, readings("value $symbol 2;").contains(symbol to name))
            assertTrue(symbol, readings("`$symbol`").contains(symbol to name))
            assertTrue(symbol, readings("The $symbol operator changes the expression.").contains(symbol to name))
            assertFalse("Quoted data does not apply $symbol", readings("\"value $symbol 2;\"").contains(symbol to name))
            assertFalse("An isolated glyph lacks operation context: $symbol", readings(symbol).contains(symbol to name))
            val replacement = EnglishTechnicalSpeech.replacements("value $symbol 2;").single { it.spoken == name }
            assertEquals(symbol, "value $symbol 2;".substring(replacement.start, replacement.endExclusive))
        }
    }

    @Test fun incrementAndDecrementNeverRenameCppLanguageVersionsOrLiteralData() {
        for (source in listOf("++count", "count++", "`++`", "The ++ operator", "A++;")) {
            assertTrue(source, readings(source).contains("++" to "increment operator"))
        }
        for (source in listOf("--count", "count--", "`--`", "The -- operator")) {
            assertTrue(source, readings(source).contains("--" to "decrement operator"))
        }
        for (source in listOf("C++", "C++20", "C++23", "The C++ language.", "`C++20`", "C++/CLI", "A++ rating",
            "\"count++\"", "'++'", "\"--count\"", "'--'", "R\"tag(count++ value += 2)tag\"",
            "\"an escaped \\\" quote and count++\"")) {
            assertTrue(source, readings(source).none { it.second == "increment operator" || it.second == "decrement operator" || it.second == "addition assignment operator" })
        }
        for (source in listOf("\\(value += 2\\)", "\\[value += 2\\]", "\$\$value += 2\$\$", "\$value += 2\$")) {
            assertTrue("Explicit math remains under the math reader: $source", readings(source).none { it.second == "addition assignment operator" })
        }
        assertEquals(listOf("std" to "std", "::" to "scope resolution operator", "cout" to "cout"), readings("std::cout"))
        assertTrue("The accepted scope-only namespace stays identical", readings("std:: indicates scope.").contains("::" to "scope resolution operator"))
    }

    @Test fun compilerNamesAndCommandLineFlagsDoNotBecomeIncrementOrDecrementOperations() {
        for (command in listOf("cmake", "clang", "clang++", "ctest", "ninja", "git", "gcc", "g++", "gradle")) {
            for (flag in listOf("build", "version", "help")) {
                for (source in listOf("$command --$flag", "`$command --$flag`")) {
                    assertTrue(source, readings(source).contains("--" to "minus minus"))
                    assertTrue(source, readings(source).none { it.second == "increment operator" || it.second == "decrement operator" })
                }
            }
        }
        for (source in listOf("The clang++ compiler.", "Use g++ for the program.", "clang++", "g++")) {
            assertTrue(source, readings(source).contains("++" to "plus plus"))
        }
        for (source in listOf("The flag --counter", "The option `--counter`", "`--counter` argument", "--counter option")) {
            assertTrue(source, readings(source).contains("--" to "minus minus"))
        }
        assertTrue(readings("cmake --build ./build --target app").filter { it.first == "--" }.all { it.second == "minus minus" })
        assertTrue(readings("--counter").contains("--" to "decrement operator"))
        assertTrue(readings("`--counter`").contains("--" to "decrement operator"))
    }

    @Test fun regularExpressionsAndKnownReferenceTypesKeepTheirOwnSymbolMeaning() {
        for (source in listOf("The regular expression .* matches text.", "The regex `.*` matches text.",
            "`.*` is a regular expression.", "The regexp ->* is shown literally.")) {
            assertTrue(source, readings(source).none { it.second == "pointer to member operator" })
        }
        assertTrue(readings("object.*member").contains(".*" to "pointer to member operator"))
        assertTrue(readings("pointer->*member").contains("->*" to "pointer to member operator"))
        for (type in listOf("int", "double", "float", "bool", "char", "auto", "long", "short", "void", "wchar_t", "char8_t", "char16_t", "char32_t")) {
            for (gap in listOf("", " ", "  ")) {
                val source = "$type$gap&& value"
                val reference = EnglishTechnicalSpeech.replacements(source).single { source.substring(it.start, it.endExclusive) == "&&" }
                assertEquals(source, "double ampersand", reference.spoken)
                assertEquals("&&", source.substring(reference.start, reference.endExclusive))
            }
        }
        for (source in listOf("'&&'", "\"&&\"")) assertTrue(readings(source).contains("&&" to "double ampersand"))
        for (source in listOf("a&&b", "a && b", "valid && ready")) assertTrue(readings(source).contains("&&" to "logical and"))
    }

    @Test fun aTechnicalSemicolonIsNamedWhileOrdinaryClausePunctuationRemainsProsody() {
        for (source in listOf("Do not add ; to every line.", "Every statement ends with ;.",
            "Do not add;", "Every statement ends with;.", "The; symbol is a terminator.",
            "The ; symbol is a terminator.", "Remove ; before this brace.", "Type ; after the expression.",
            "The symbol ; terminates the statement.", "';'", "\";\"", "`return 0;`", ";")) {
            assertTrue(source, readings(source).contains(";" to "semicolon"))
        }
        for (source in listOf("First read the question; then answer.", "first ; next", "First ; next.",
            "The braces delimit a group of statements; they are not an output message either.",
            "Finish first; tokens follow.", "Finish first; symbols follow.",
            "The result is complete; the statement follows.", "https://example.org/a;b")) {
            assertTrue(source, readings(source).none { it.second == "semicolon" })
        }
    }

    @Test fun theActualEnglishFirstProgramPageKeepsItsTwoSymbolMentionsAndProsePauseDistinct() {
        val resources = File(System.getProperty("translation.resources") ?: "src/main/res").absoluteFile.parentFile!!
        val lesson = JSONObject(resources.resolve("assets/content/en/programming/cpp_fundamentals/programas_y_ejecucion/lesson_primer_programa_y_salida.json").readText())
        val before = lesson.toString()
        val blocks = lesson.getJSONArray("pages").getJSONObject(2).getJSONArray("blocks")
        val source = (0 until blocks.length()).map { blocks.getJSONObject(it).optString("text") }
            .single { "Do not add ; to every line" in it }
        val mentions = EnglishTechnicalSpeech.replacements(source).filter { it.spoken == "semicolon" }
        assertEquals(3, source.count { it == ';' })
        assertEquals(2, mentions.size)
        assertEquals(listOf(source.indexOf(';'), source.indexOf(';', source.indexOf(';') + 1)), mentions.map { it.start })
        mentions.forEach { assertEquals(";", source.substring(it.start, it.endExclusive)) }
        assertFalse("The final clauses still use punctuation prosody", mentions.any { it.start == source.lastIndexOf(';') })
        for (language in listOf(NarrationLanguage.AMERICAN_ENGLISH, NarrationLanguage.BRITISH_ENGLISH)) {
            val page = LessonNarrationText.page(lesson, 2, language, forKokoro = true)
            val paragraph = page.single { it.startsWith("Every output statement in our examples") }
            assertTrue("Exercise the production cleanup that removes the preceding space", "Do not add;" in paragraph)
            val sentences = NarrationSegments.sentences(listOf(paragraph))
            assertTrue("The preceding cue must survive a segment ending at the sign", sentences.any { it == "Do not add;" })
            val mapping = NarrationWordMappings.forBlock(0, source, sentences.mapIndexed { index, text -> index to text }.toMap(), language)
            assertEquals("Both language variants map every production sentence", sentences.indices.toSet(), mapping.keys)
            val mappedSigns = mutableListOf<IntRange>()
            sentences.forEachIndexed { sentenceIndex, text ->
                EnglishTechnicalSpeech.replacements(text).filter { it.spoken == "semicolon" }.forEach { sign ->
                    assertEquals(";", text.substring(sign.start, sign.endExclusive))
                    val sentenceMapping = mapping.getValue(sentenceIndex)
                    val word = sentenceMapping.spoken.indexOfFirst { it.start == sign.start && it.endExclusive == sign.endExclusive }
                    assertTrue("The spoken symbol has its own original-source timing atom", word >= 0)
                    val visible = sentenceMapping.displayed[word]
                    mappedSigns += visible.start until visible.endExclusive
                }
            }
            assertEquals("Exactly the two sign mentions are named after real normalization/segmentation", 2, mappedSigns.size)
            assertTrue(source.indexOf(';') in mappedSigns[0])
            assertTrue(source.indexOf(';', source.indexOf(';') + 1) in mappedSigns[1])
            assertTrue("The ordinary clause separator remains a pause", mappedSigns.none { source.lastIndexOf(';') in it })
        }
        assertEquals("The curriculum remains unchanged", before, lesson.toString())
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
