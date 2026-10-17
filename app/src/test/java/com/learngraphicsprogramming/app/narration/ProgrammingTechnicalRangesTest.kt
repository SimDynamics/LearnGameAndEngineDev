package com.learngraphicsprogramming.app.narration

import java.io.File
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class ProgrammingTechnicalRangesTest {
    private val languages = NarrationLanguage.entries
    private val curricularTechnicalSignal = Regex(
        "#include|std::|(?<![\\p{L}\\p{M}\\p{N}_])int\\s+[A-Za-z_][A-Za-z_0-9]*" +
            "(?:(?:\\s*,\\s*[A-Za-z_][A-Za-z_0-9]*)+\\s*(?=;)|\\s*(?=[=;{(\\[]))"
    )
    private fun analysis(text: String, language: NarrationLanguage = NarrationLanguage.SPANISH) = ProgrammingTechnicalRanges.analyze(text, language)
    private fun covered(result: ProgrammingTechnicalRanges.Analysis, start: Int, end: Int) =
        (start until end).all { position -> result.fragments.any { position in it.start until it.endExclusive } }

    @Test fun courseOneLessonThreePageSixColorsOnlyTheInsertionOperatorNotEsOrY() {
        val resources = File(System.getProperty("translation.resources") ?: "src/main/res")
        val lessonFile = requireNotNull(resources.absoluteFile.parentFile).resolve(
            "assets/content/es/programming/cpp_fundamentals/programas_y_ejecucion/lesson_texto_secuencia_y_formato.json")
        val lesson = JSONObject(lessonFile.readText())
        val before = lesson.toString()
        val text = lesson.getJSONArray("pages").getJSONObject(5)
            .getJSONArray("blocks").getJSONObject(1).getString("text")
        val expression = "es << y"
        val start = text.indexOf(expression)
        assertTrue(start >= 0)
        val operator = start + "es ".length
        val analysis = analysis(text)
        assertTrue("Only the two insertion characters belong to the technical fragment",
            analysis.fragments.any { it.start == operator && it.endExclusive == operator + 2 })
        assertTrue("The preceding es must retain prose styling",
            analysis.fragments.none { it.start <= start && it.endExclusive > start })
        val conjunction = operator + 3
        assertEquals('y', text[conjunction])
        assertTrue("The following y must retain prose styling",
            analysis.fragments.none { it.start <= conjunction && it.endExclusive > conjunction })
        assertEquals(before, lesson.toString())
        // A real C++ stream expression is still highlighted as a full statement.
        val sourceCode = "std::cout << value;"
        val stream = analysis(sourceCode)
        assertTrue(stream.fragments.any { it.start == 0 && it.endExclusive >= sourceCode.length })
    }

    @Test fun headersNamespacesDeclarationsAndCompleteExpressionsOwnExactSourceRanges() {
        val code = "#include <iostream>\nint count = 2; std::cout << \"ready\"; return 0;"
        val source = "Before: $code After."
        languages.forEach { language ->
            val result = analysis(source, language)
            for (fragment in listOf("#include <iostream>", "int count = 2;", "std::cout << \"ready\";", "return 0;")) {
                val start = source.indexOf(fragment)
                assertTrue("$language must recognize $fragment", covered(result, start, start + fragment.length))
            }
            assertFalse(covered(result, 0, "Before".length))
            assertFalse(covered(result, source.indexOf("After"), source.length))
            assertTrue(result.wrappers.isEmpty())
        }
    }

    @Test fun explicitInlineCodeKeepsUtf16SourceAndSeparatesOnlyItsExistingWrappers() {
        val source = "😀 Before `std::cout << \"a;b\";` after and ```int count{3};``` end."
        languages.forEach { language ->
            val result = analysis(source, language)
            assertEquals(4, result.wrappers.size)
            assertEquals(listOf("`", "`", "```", "```"), result.wrappers.map { source.substring(it.start, it.endExclusive) })
            for (code in listOf("std::cout << \"a;b\";", "int count{3};")) {
                val start = source.indexOf(code)
                assertTrue(covered(result, start, start + code.length))
            }
            assertTrue(result.fragments.none { fragment -> result.wrappers.any { fragment.start < it.endExclusive && fragment.endExclusive > it.start } })
            assertTrue(result.fragments.all { it.start >= 0 && it.start < it.endExclusive && it.endExclusive <= source.length })
            assertEquals("😀 Before `std::cout << \"a;b\";` after and ```int count{3};``` end.", source)
        }
    }

    @Test fun ordinaryFrenchAndEnglishHomonymsAndPunctuationRemainProse() {
        for (source in listOf("La main droite reste ouverte; puis elle se ferme.",
            "La fonction sort du domaine; la main est calme.", "Une double valeur existe dans cette phrase; elle reste visible.",
            "Un exemple (ici); puis une explication.")) {
            assertEquals(source, emptyList<ProgrammingTechnicalRanges.Range>(), analysis(source, NarrationLanguage.FRENCH).fragments)
        }
        for (source in listOf("The main point is to return home; read the next section.",
            "A long explanation; another ordinary clause.", "First ; next. This isn't code!", "See (details); then continue.",
            "Calculate the comparisons; do not simply print constant ones.", "A clog blocks the pipe; clean it.")) {
            assertEquals(source, emptyList<ProgrammingTechnicalRanges.Range>(), analysis(source, NarrationLanguage.AMERICAN_ENGLISH).fragments)
        }
        val technical = "La fonction main utilise std::cout. La main droite reste ouverte."
        val selected = analysis(technical, NarrationLanguage.FRENCH)
        assertTrue(covered(selected, technical.indexOf("main"), technical.indexOf("main") + 4))
        assertFalse(covered(selected, technical.lastIndexOf("main"), technical.lastIndexOf("main") + 4))
        for (source in listOf("std::clog << \"log\";", "The identifier clog remains available.")) {
            val position = source.indexOf("clog")
            assertTrue("Qualified/explicitly named clog still receives C++ presentation",
                covered(analysis(source, NarrationLanguage.AMERICAN_ENGLISH), position, position + 4))
        }
    }

    @Test fun declarationsRequireActualSyntaxAndParentheticalProseNeverBecomesAnArgumentList() {
        for (language in languages) {
            val source = "The type int is a keyword; keep this prose. std::cout (the output stream) remains visible."
            val result = analysis(source, language)
            val proseStart = source.indexOf("is a keyword")
            val proseEnd = proseStart + "is a keyword".length
            assertTrue(result.fragments.none { it.start < proseEnd && it.endExclusive > proseStart })
            val explanation = source.indexOf("(the output stream)")
            assertTrue(result.fragments.none { it.start < explanation + "(the output stream)".length && it.endExclusive > explanation })
            for (code in listOf("const int&& value = other;", "int render() { return 0; }", "left && right", "total += entrada;",
                "valor = calcular(2);", "if (listo) { avanzar(); }", "valor = objeto.calcular(2);")) {
                assertTrue("$language must retain complete syntax: $code", covered(analysis(code, language), 0, code.length))
            }
            assertTrue("An unmarked mathematical explanation is not a C++ statement", analysis("x = y", language).fragments.isEmpty())
        }
    }

    @Test fun aNamespaceMentionNeverConsumesTheFollowingExplanatoryVerb() {
        for ((source, language) in listOf("std:: indica el espacio de nombres." to NarrationLanguage.SPANISH,
            "std:: indicates the namespace." to NarrationLanguage.AMERICAN_ENGLISH,
            "std:: indique la portée." to NarrationLanguage.FRENCH)) {
            val result = analysis(source, language)
            assertTrue(covered(result, 0, 5))
            assertTrue(result.fragments.none { it.endExclusive > 5 })
        }
        for (language in languages) {
            val source = "std::vector<int> values{1, 2}; followed by prose."
            assertTrue(covered(analysis(source, language), 0, source.indexOf(';') + 1))
        }
    }

    @Test fun mathematicalDelimitersAndUrlsNeverBecomeCppThroughTheirPunctuation() {
        val source = "Read https://example.org/std::cout?a=2 and \\(x < y\\), then \\[a+b\\]."
        languages.forEach { language ->
            val result = analysis(source, language)
            for (protected in listOf("https://example.org/std::cout?a=2", "\\(x < y\\)", "\\[a+b\\]")) {
                val start = source.indexOf(protected)
                assertTrue("$language cannot color protected $protected", result.fragments.none { it.start < start + protected.length && it.endExclusive > start })
            }
        }
    }

    @Test fun metalinguisticSemicolonsAreDistinctFromClausePunctuationInEachLanguage() {
        for ((source, language) in listOf("No añadas ; a cualquier línea; conserva la prosa." to NarrationLanguage.SPANISH,
            "Do not add ; to every line; keep the prose." to NarrationLanguage.AMERICAN_ENGLISH,
            "N'ajoute pas ; à chaque ligne ; conserve la prose." to NarrationLanguage.FRENCH)) {
            val result = analysis(source, language)
            assertTrue(source, covered(result, source.indexOf(';'), source.indexOf(';') + 1))
            assertFalse(source, covered(result, source.lastIndexOf(';'), source.lastIndexOf(';') + 1))
        }
    }

    @Test fun bothExistingProgrammingCoursesAndTranslationsNeedNoCurricularPreparation() {
        val assets = File(System.getProperty("translation.resources") ?: "src/main/res").absoluteFile.parentFile!!.resolve("assets/content")
        val seen = mutableSetOf<String>()
        for (language in languages) {
            for (course in listOf("cpp_fundamentals", "programming_practice_tools")) {
                val files = assets.resolve("${language.resourceGroup}/programming/$course").walkTopDown()
                    .filter { it.isFile && it.name.startsWith("lesson_") && it.extension == "json" }.sortedBy { it.path }.take(8).toList()
                for (file in files) {
                    val lesson = JSONObject(file.readText())
                    val before = lesson.toString()
                    val title = lesson.optString("title")
                    assertTrue(analysis(title, language).fragments.all { it.start >= 0 && it.start < it.endExclusive && it.endExclusive <= title.length })
                    val pages = lesson.getJSONArray("pages")
                    for (page in 0 until pages.length()) {
                        val blocks = pages.getJSONObject(page).getJSONArray("blocks")
                        for (index in 0 until blocks.length()) {
                            val block = blocks.getJSONObject(index)
                            if (block.optString("type") !in setOf("heading", "paragraph", "note")) continue
                            val source = block.optString("text")
                            if (!curricularTechnicalSignal.containsMatchIn(source)) continue
                            val result = analysis(source, language)
                            assertTrue("Expected existing technical prose in ${file.name}/$page/$index", result.fragments.isNotEmpty())
                            assertTrue(result.fragments.all { it.start >= 0 && it.start < it.endExclusive && it.endExclusive <= source.length })
                            seen += course
                        }
                    }
                    assertEquals("No JSON content, IDs, answers or positions change", before, lesson.toString())
                }
            }
        }
        assertEquals(setOf("cpp_fundamentals", "programming_practice_tools"), seen)
    }

    @Test fun corpusFixtureSignalRequiresADeclarationInsteadOfCrossingOrdinarySentences() {
        val note = "Acotación de los laboratorios: hasta doce líneas físicas, texto ASCII, cantidades numéricas " +
            "representables por int y final normal del origen. Se admiten líneas mal formadas para comprobar su " +
            "diagnóstico. Un lote vacío es válido; una línea vacía es una línea recibida que no cumple el formato. " +
            "La última línea también se analiza si no lleva salto final. No se investigan averías del dispositivo de entrada."
        assertFalse(curricularTechnicalSignal.containsMatchIn(note))
        assertFalse(curricularTechnicalSignal.containsMatchIn("La somme des deux compteurs ne dépasse pas vingt. " +
            "Chaque élément appartient exactement à une catégorie. Si les données du bilan peuvent être des int " +
            "quelconques, les comparer ne nécessite ni soustraction ni multiplication : la vérification de l'intervalle " +
            "peut signaler false sans opération arithmétique dangereuse."))
        for (source in listOf("int x;", "int x = 2;", "int x{2};", "int main() {", "int x, y;", "int x[4];",
            "#include <iostream>", "std::cout")) {
            assertTrue(source, curricularTechnicalSignal.containsMatchIn(source))
        }
        assertFalse(curricularTechnicalSignal.containsMatchIn("El tipo int representa cantidades; conserva la explicación."))
    }
}
