package com.learngraphicsprogramming.app.narration

import java.io.File
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class SpanishTechnicalSpeechTest {
    private fun read(source: String, mode: String = "auto"): String {
        val result = SpanishTechnicalSpeech.prepare(source, mode)
        assertTrue("A contextual reading was expected for $source", result.changed)
        return result.text
    }

    @Test fun inlineFormattingIsSilentWithoutJoiningNeighboringWordsOrChangingFencedSpeech() {
        assertEquals("doble menor que", read("`<<`"))
        assertEquals("El operador doble menor que inserta datos.", read("El operador `<<` inserta datos."))
        assertEquals("Usa doble menor que aquí.", read("Usa`<<`aquí."))
        assertEquals("doble menor que doble mayor que", read("`<<``>>`"))
        assertEquals("¿ doble menor que?", read("¿`<<`?"))
        assertEquals("código, doble menor que, fin de código", read("```<<```"))
        assertEquals("Sin cierre `", SpanishTechnicalSpeech.prepare("Sin cierre `").text)
    }

    @Test fun ordinaryProseNumbersUnitsAndLanguageNamesKeepTheirExistingReadingAndCacheIdentity() {
        for (source in listOf("Las fracciones representan la misma cantidad.", "C++", "Ejemplo de código C más más.",
            "12 casas", "0,05 litros", "-3 °C", "90°", "cm²", "La temperatura (en °C) indica un estado.",
            "Conserva el contenido (sin cambios).", "Logaritmo (base diez).")) {
            val result = SpanishTechnicalSpeech.prepare(source)
            assertFalse(source, result.changed)
            assertEquals(source, source, result.text)
        }
    }

    @Test fun completeMathematicalExpressionsRemainWithTheAstReader() {
        for (source in listOf("3/4", "x=1", "a = b", "|x|", "{x+y}", "[x,y]", "a_b", "f(x)",
            "a * b", "x²", "(1,2)", "x < 1", "x <= 1", "-4 - (-3)")) {
            val result = SpanishTechnicalSpeech.prepare(source)
            assertFalse(source, result.changed)
            assertEquals(source, source, result.text)
        }
    }

    @Test fun forcedMathAndExplicitMathDelimitersKeepTheirContent() {
        for (mode in listOf("inline", "block")) {
            for (source in listOf("|x|", "[1,2]", "f(x)", "x_{n}", "\\frac{1}{2}", "a&&b", "std::vector<int>")) {
                assertEquals(SpanishTechnicalSpeech.Preparation(source, false), SpanishTechnicalSpeech.prepare(source, mode))
            }
        }
        val source = "Calcula \\(\\frac{1}{2}\\), $" + "x_{n}$ y \\[a_b\\]."
        assertEquals(SpanishTechnicalSpeech.Preparation(source, false), SpanishTechnicalSpeech.prepare(source))
        assertEquals("equis", read("`x`", "block"))
    }

    @Test fun mathematicalFunctionNamesRemainWithTheExistingMathReader() {
        for (name in listOf("sin", "cos", "tan", "arcsin", "arccos", "arctan", "sinh", "cosh", "tanh", "ln", "log", "exp")) {
            val source = "Calcula $name(x) y conserva el resultado."
            assertEquals(source, SpanishTechnicalSpeech.Preparation(source, false), SpanishTechnicalSpeech.prepare(source))
        }
        assertEquals("sinh abre paréntesis equis cierra paréntesis", read("`sinh(x)`"))
    }

    @Test fun includeDirectivesAndHeadersAreNotComparisons() {
        assertEquals("almohadilla include abre ángulo iostream cierra ángulo", read("#include <iostream>"))
        assertEquals("La línea almohadilla include abre ángulo iostream cierra ángulo pone a disposición las declaraciones.",
            read("La línea #include <iostream> pone a disposición las declaraciones."))
        assertEquals("almohadilla include abre comilla doble sobres punto ache cierra comilla doble", read("#include \"sobres.h\""))
        assertEquals("De abre ángulo stdexcept cierra ángulo.", read("De <stdexcept>."))
        assertEquals("directiva almohadilla ifndef CPP guion bajo FUNDAMENTALS guion bajo SOBRES guion bajo ache",
            read("#ifndef CPP_FUNDAMENTALS_SOBRES_H"))
    }

    @Test fun qualifiedNamesCastsAndNestedTemplatesPreserveEachDelimiter() {
        assertEquals("ese te de operador de resolución de ámbito vector abre ángulo int cierra ángulo", read("std::vector<int>"))
        val nested = read("std::vector<std::vector<int>>")
        assertEquals(2, Regex("abre ángulo").findAll(nested).count())
        assertEquals(2, Regex("cierra ángulo").findAll(nested).count())
        val cast = read("static_cast<int>(valor)")
        assertEquals("static guion bajo cast abre ángulo int cierra ángulo abre paréntesis valor cierra paréntesis", cast)
        assertEquals("$cast", read("`static_cast<int>(valor)`"))
        val comparison = read("`std::max(a < b, c)`")
        assertTrue(comparison, comparison.contains("a menor que be"))
        assertFalse(comparison, comparison.contains("abre ángulo"))
    }

    @Test fun bracesAndSquareBracketsRetainTheirOwnNames() {
        assertEquals("int filas abre llave cinco cierra llave punto y coma", read("int filas{5};"))
        assertEquals("Repite con abre llave cierra llave, abre llave cuatro cierra llave y abre llave cero cierra llave.",
            read("Repite con {}, {4} y {0}."))
        assertEquals("No ejecutes abre corchete cierra corchete fuera del rango.", read("No ejecutes [] fuera del rango."))
        assertEquals("operator abre corchete cierra corchete", read("operator[]"))
        assertEquals("datos abre corchete uno cierra corchete", read("`datos[1]`"))
    }

    @Test fun logicalOperatorsReferencesAndLiteralSeparatorsHaveDifferentReadings() {
        assertEquals("El operador y lógico significa «y».", read("El operador && significa «y»."))
        assertEquals("El operador o lógico significa «o».", read("El operador || significa «o»."))
        assertEquals("int ampersand valor", read("int& valor"))
        assertEquals("const auto ampersand texto", read("const auto& texto"))
        assertEquals("La salida barra vertical termina la fila.", read("La salida | termina la fila."))
        assertEquals("abre comilla doble barra vertical barra vertical cierra comilla doble", read("`\"||\"`"))
        assertEquals("int doble ampersand valor", read("`int&& valor`"))
    }

    @Test fun compoundOperatorsAreReadBeforeTheirIndividualCharacters() {
        assertEquals("cajas multiplica y asigna tres", read("cajas *= 3"))
        assertEquals("equis comparación de igualdad uno", read("x==1"))
        assertEquals("a distinto de uno", read("a!=1"))
        assertEquals("equis o lógico uno", read("x||1"))
        assertEquals("cout doble menor que equis punto y coma", read("`cout << x;`"))
        val condition = read("También se puede escribir (temperatura >= 15) && (temperatura <= 25).")
        assertTrue(condition.contains("mayor o igual que"))
        assertTrue(condition.contains("menor o igual que"))
        assertFalse(condition.contains("mayor que es igual"))
        assertEquals("equis asigna uno punto y coma", read("`x = 1;`"))
    }

    @Test fun asteriskKeepsItsSyntacticAndArithmeticRoles() {
        assertEquals("El símbolo asterisco expresa multiplicación.", read("El símbolo * expresa multiplicación."))
        assertEquals("En este caso equivale a cajas = cajas por 3.", read("En este caso equivale a cajas = cajas * 3."))
        assertEquals("int asterisco valor", read("`int* valor`"))
        assertEquals("a por be", read("`a*b`"))
    }

    @Test fun technicalNamesKeepNamespaceSpellingAndEveryCompoundOperatorIntact() {
        assertEquals("operador de resolución de ámbito", read("::"))
        assertEquals("a operador de resolución de ámbito be", read("a::b"))
        assertEquals("El operador operador de resolución de ámbito resuelve un nombre.",
            read("El operador :: resuelve un nombre."))
        val names = linkedMapOf(
            "<<=" to "asignación con desplazamiento a la izquierda",
            ">>=" to "asignación con desplazamiento a la derecha",
            "&=" to "asignación con y bit a bit", "|=" to "asignación con o bit a bit",
            "^=" to "asignación con o exclusivo bit a bit",
            "<=>" to "comparación de tres vías",
            "->*" to "acceso a miembro mediante puntero a miembro",
            ".*" to "acceso a miembro mediante puntero a miembro"
        )
        for ((operator, name) in names) {
            assertEquals(operator, "equis $name uno punto y coma", read("`x $operator 1;`"))
            if (operator in listOf("<<=", ">>=")) assertEquals("equis $name uno", read("x${operator}1"))
        }
        assertEquals("negación lógica listo", read("`!listo`"))
        assertEquals("negación lógica listo", read("!listo"))
        assertEquals("Evalúa negación lógica listo y continúa.", read("Evalúa !listo y continúa."))
        for (source in listOf("¡Bien! Continúa.", "¡No!Sigue.", "https://example.org/std::cout?x+=2", "www.example.org/a::b")) {
            assertEquals(SpanishTechnicalSpeech.Preparation(source, false), SpanishTechnicalSpeech.prepare(source))
        }
        assertEquals("abre comilla doble admiración cierra comilla doble", read("`\"!\"`"))
        assertEquals("abre comilla doble más asigna cierra comilla doble", read("`\"+=\"`"))
        assertEquals("abre comilla doble dos puntos dos puntos cierra comilla doble", read("`\"::\"`"))
    }

    @Test fun identifiersKeepTheirUnderscoresAndDigitsWithoutSpellingProse() {
        assertEquals("total guion bajo count dos", read("total_count2"))
        assertEquals("nombre guion bajo guion bajo valor", read("nombre__valor"))
        assertEquals("guion bajo valor tres", read("_valor3"))
        assertEquals("for abre paréntesis int ampersand valor dos puntos datos cierra paréntesis",
            read("for (int& valor: datos)"))
        assertEquals("efe abre paréntesis args cierra paréntesis", read("`f(args)`"))
    }

    @Test fun literalEscapesQuotesAndNumericRepresentationsRemainRecoverable() {
        assertEquals("barra inversa ene", read("\\n"))
        assertEquals("abre comilla simple barra inversa te cierra comilla simple", read("`'\\t'`"))
        assertEquals("abre comilla doble dos barra inversa te asterisco espacio nueve barra inversa ene cierra comilla doble",
            read("`\"2\\t* 9\\n\"`"))
        assertEquals("cero equis uno a", read("0x1a"))
        assertEquals("uno punto dos cinco e más cero uno", read("1.25e+01"))
        assertEquals("cero tres dos", read("`032`"))
        assertEquals("tres punto cero efe", read("`3.0f`"))
        assertEquals("uno apóstrofo cero cero cero", read("`1'000`"))
    }

    @Test fun referencesOrdinalsAndRangesKeepTheirMeaningWithoutBecomingDecimals() {
        assertEquals("sección siete punto cuatro punto diez", read("§7.4.10"))
        assertEquals("sección siete punto seis a siete punto seis punto uno", read("§7.6–7.6.1"))
        assertEquals("tercera edición", read("3.ª edición"))
        assertEquals("primero", read("1.º"))
        assertEquals("doscientos cinco a doscientos seis.", read("205–206."))
        assertEquals("Dominio cero a ciento veinte.", read("Dominio 0..120."))
        assertEquals("ID guion Recuento", read("ID–Recuento"))
    }

    @Test fun largeIntegerLiteralsAreReadCompletelyWithoutLeakingDigitsToStrictG2p() {
        val names = listOf("cero", "uno", "dos", "tres", "cuatro", "cinco", "seis", "siete", "ocho", "nueve")
        for (literal in listOf("1234567890123", "9223372036854775807", "9223372036854775808")) {
            val expected = literal.map { names[it.digitToInt()] }.joinToString(" ")
            val speech = read("`$literal`")
            assertEquals("$expected", speech)
            assertFalse(speech, speech.any(Char::isDigit))
        }
    }

    @Test fun publishedLessonFourPageTwoMissingSemicolonIsTechnicalSpeech() {
        val resources = File(System.getProperty("translation.resources") ?: "src/main/res")
        val sourceFile = resources.absoluteFile.parentFile!!.resolve(
            "assets/content/es/programming/cpp_fundamentals/programas_y_ejecucion/lesson_fuente_compilacion_y_diagnostico.json")
        val lesson = JSONObject(sourceFile.readText())
        val unchanged = lesson.toString()
        val question = lesson.getJSONArray("pages").getJSONObject(1)
            .getJSONArray("blocks").getJSONObject(3).getString("question")
        assertTrue(question.contains("no tiene su ;"))
        val spoken = SpanishTechnicalSpeech.prepare(question)
        assertTrue(spoken.changed)
        assertTrue(spoken.text.contains("no tiene su punto y coma"))
        assertEquals(unchanged, lesson.toString())
        // A clause delimiter must remain ordinary punctuation.
        assertFalse(SpanishTechnicalSpeech.prepare("Termina la instrucción; continúa la lectura.").changed)
    }

    @Test fun anArticleCanIdentifyAMentionedSemicolonWithoutAnAdjacentActionVerb() {
        assertEquals("¿Conviene quitar sólo el punto y coma, o cambiar el texto?",
            read("¿Conviene quitar sólo el ;, o cambiar el texto?"))
        assertEquals("El error consiste en olvidar el punto y coma en C++.",
            read("El error consiste en olvidar el ; en C++."))
        val ordinary = "El compilador informa del error; revisa la línea."
        assertEquals(SpanishTechnicalSpeech.Preparation(ordinary, false),
            SpanishTechnicalSpeech.prepare(ordinary))
    }

    @Test fun correctedClausePunctuationAndTechnicalMentionKeepTheirOwnSourceOwners() {
        val source = "Primero ; después sigue. No añadas ; a cualquier línea."
        val pieces = mutableListOf<Triple<Int, Int, String>>()
        val prepared = SpanishTechnicalSpeech.prepareForReading(source) { start, end, spoken ->
            pieces += Triple(start, end, spoken)
        }
        assertEquals(SpanishTechnicalSpeech.prepareForKokoro(source), prepared)
        val signs = pieces.filter { it.first < it.second && source.substring(it.first, it.second) == ";" }
        assertEquals(2, signs.size)
        assertEquals(";", signs[0].third.trim())
        assertEquals("punto y coma", signs[1].third.trim())
        val segments = NarrationSegments.sentences(listOf(prepared.text)).withIndex().associate { it.index to it.value }
        val mapping = NarrationWordMappings.forBlock(0, source, segments)
        assertEquals(segments.keys, mapping.keys)
        assertEquals(source.indices.toSet(), mapping.values.flatMap { it.displayed.flatMap { range -> range.start until range.endExclusive } }.toSet())
    }

    @Test fun technicalSemicolonMentionsAreSpokenWithoutNamingOrdinaryClausePunctuation() {
        assertEquals("No añadas punto y coma a cualquier línea.", read("No añadas ; a cualquier línea."))
        assertEquals("Cada instrucción termina con punto y coma.", read("Cada instrucción termina con ;."))
        for (source in listOf("El código termina; después continúa.", "Primero ; después sigue.", "main; estudiaremos los tipos.")) {
            assertEquals(SpanishTechnicalSpeech.Preparation(source, false), SpanishTechnicalSpeech.prepare(source))
        }
        val resources = File(System.getProperty("translation.resources") ?: "src/main/res")
        val file = requireNotNull(resources.absoluteFile.parentFile).resolve(
            "assets/content/es/programming/cpp_fundamentals/programas_y_ejecucion/lesson_primer_programa_y_salida.json")
        val lesson = JSONObject(file.readText())
        val before = lesson.toString()
        val blocks = lesson.getJSONArray("pages").getJSONObject(2).getJSONArray("blocks")
        val source = (0 until blocks.length()).map { blocks.getJSONObject(it).optString("text") }
            .single { it.contains("Cada instrucción de salida") && it.contains("termina con ;.") }
        val prepared = SpanishTechnicalSpeech.prepareForKokoro(source).text
        val spoken = ManualEnglishSpeech.strip(prepared)
        assertEquals("One symbolic terminator and its authored name remain spoken", 2,
            Regex("punto y coma").findAll(spoken).count())
        assertTrue("Ordinary clause punctuation remains punctuation", spoken.contains("instrucción; un cambio"))
        assertTrue(spoken.contains("main; no se necesita"))
        assertEquals(4, spoken.count { it == ';' })
        val parts = NarrationSegments.sentences(listOf(prepared)).withIndex().associate { it.index to it.value }
        val mapping = NarrationWordMappings.forBlock(0, source, parts)
        assertEquals(parts.keys, mapping.keys)
        assertEquals(source.indices.toSet(), mapping.values.flatMap { it.displayed.flatMap { range -> range.start until range.endExclusive } }.toSet())
        val legacy = "Cada instrucción termina con ;. No añadas ; a cualquier línea. Las llaves delimitan instrucciones; tampoco son una salida."
        val legacyPrepared = SpanishTechnicalSpeech.prepareForKokoro(legacy).text
        val legacySpoken = ManualEnglishSpeech.strip(legacyPrepared)
        assertEquals("Keep the original two technical mentions as an explicit regression", 2,
            Regex("punto y coma").findAll(legacySpoken).count())
        assertTrue(legacySpoken.contains("instrucciones; tampoco"))
        val legacyParts = NarrationSegments.sentences(listOf(legacyPrepared)).withIndex().associate { it.index to it.value }
        val legacyMapping = NarrationWordMappings.forBlock(0, legacy, legacyParts)
        assertEquals(legacyParts.keys, legacyMapping.keys)
        assertEquals(legacy.indices.toSet(), legacyMapping.values.flatMap { it.displayed.flatMap { range -> range.start until range.endExclusive } }.toSet())
        assertEquals(before, lesson.toString())
    }

    @Test fun commandOptionsAndExplicitRegularExpressionsAreNotCppUpdateOrMemberOperators() {
        for (source in listOf("cmake --build salida", "La opción --version consulta la versión.", "`clang++ --version`")) {
            val spoken = read(source)
            assertTrue(source, spoken.contains("guion guion"))
            assertFalse(source, spoken.contains("decremento"))
            if (source.contains("clang++")) {
                assertTrue(source, spoken.contains("más más"))
                assertFalse(source, spoken.contains("incremento"))
            }
        }
        val regex = read("La expresión regular `.*` conserva sus signos.")
        assertTrue(regex, regex.contains("punto asterisco"))
        assertFalse(regex, regex.contains("puntero a miembro"))
        assertTrue(read("`--contador;`"), read("`--contador;`").contains("decremento"))
        assertTrue(read("`objeto.*miembro`"), read("`objeto.*miembro`").contains("puntero a miembro"))
    }

    @Test fun unsupportedNotationIsPreservedForStrictFrontendRejectionAndPreparationIsIdempotent() {
        val unknown = "hola ☃"
        assertEquals(SpanishTechnicalSpeech.Preparation(unknown, false), SpanishTechnicalSpeech.prepare(unknown))
        val prepared = read("`std::vector<int> datos{1,2};` y §7.4.10.")
        assertEquals(SpanishTechnicalSpeech.Preparation(prepared, false), SpanishTechnicalSpeech.prepare(prepared))
        assertTrue(read("`hola ☃`"), read("`hola ☃`").contains('☃'))
    }

    @Test fun realIncludeParagraphsAreReadableWithoutChangingTheAuthoredLessons() {
        val resources = File(System.getProperty("translation.resources") ?: "src/main/res")
        val assets = requireNotNull(resources.absoluteFile.parentFile).resolve("assets/content/es/programming/cpp_fundamentals")
        for ((relative, expected) in listOf(
            "programas_y_ejecucion/lesson_primer_programa_y_salida.json" to "abre ángulo iostream cierra ángulo",
            "funciones_y_contratos/lesson_declaraciones_y_firmas.json" to "abre comilla doble diferencia punto ache cierra comilla doble"
        )) {
            val lesson = JSONObject(assets.resolve(relative).readText())
            val unchanged = lesson.toString()
            val pages = lesson.getJSONArray("pages")
            val page = (0 until pages.length()).first { pn ->
                val blocks = pages.getJSONObject(pn).getJSONArray("blocks")
                (0 until blocks.length()).any { bn ->
                    val block = blocks.getJSONObject(bn)
                    block.optString("type") == "paragraph" && block.optString("text").contains("#include")
                }
            }
            val speech = LessonNarrationText.page(lesson, page, NarrationLanguage.SPANISH).joinToString(" ")
            assertTrue(relative, speech.contains("almohadilla include"))
            assertTrue(relative, speech.contains(expected))
            assertFalse(relative, speech.contains("es menor que iostream es mayor que"))
            assertEquals("The authored lesson must remain unchanged", unchanged, lesson.toString())
        }
    }
}
