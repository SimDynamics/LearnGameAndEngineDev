package com.learngraphicsprogramming.app.math

import org.junit.Assert.*
import org.junit.Test

class MathTextSemanticsTest {
    private fun fragments(source: String, mode: String = "auto") =
        MathTextSemantics.ranges(source, mode).map { source.substring(it.start, it.endExclusive) }

    @Test fun existingMathDetectionIdentifiesExpressionsAndIndividualSymbolsInsideProse() {
        val source = "Calcula 3/4, x² y √9; compara x = 2 y observa π."
        assertEquals(listOf("3/4", "x²", "√9", "x = 2", "π"), fragments(source))
        assertEquals(listOf("2 × 3"), fragments("Producto 2 × 3"))
        assertEquals(listOf("≤"), fragments("La relación ≤ conserva el orden."))
        assertEquals(listOf("a+b"), fragments("a+b"))
        assertEquals(listOf("x²π"), fragments("x²π"))
    }

    @Test fun explicitFormulasAndExistingModesKeepTheirOriginalSourceRanges() {
        val source = "Usa \\(\\frac{3}{5}\\) y \$x^2\$ antes de \\[a=b\\]."
        assertEquals(listOf("\\(\\frac{3}{5}\\)", "\$x^2\$", "\\[a=b\\]"), fragments(source))
        assertEquals(listOf("x \\times 3"), fragments("x \\times 3", "block"))
        assertEquals(listOf("x + 3"), fragments("  x + 3  ", "inline"))
        assertTrue(MathTextSemantics.ranges(source, "text").isEmpty())
        assertTrue(MathTextSemantics.ranges("x \\unsupported{3}", "block").isEmpty())
        assertTrue(MathTextSemantics.ranges("   ").isEmpty())
    }

    @Test fun proseCodePathsAndDatesKeepTheirExistingPlainPresentation() {
        for (source in listOf("Una explicación ordinaria de cantidades y relaciones.",
            "Use std::cout and #include <iostream>.", "C++ et std::cout.",
            "Código `x = 2; 3/4` dentro de una frase.",
            "Ver https://example.org/3/4 y C:\\temp\\3/4 y src/3/4.",
            "La fecha es 12/03/2026.")) {
            assertEquals(source, emptyList<String>(), fragments(source))
        }
    }

    @Test fun completeNumericEquationsKeepTheRelationAndBothSidesInsideOneDecorationRange() {
        val feedback = "El 6 está en las centenas y aporta 6 × 100 = 600."
        val decomposition = "5006 = 5000 + 0 + 0 + 6. Los dos ceros mantienen centenas y decenas."
        assertEquals(listOf("6", "6 × 100 = 600"), fragments(feedback))
        assertEquals(listOf("5006 = 5000 + 0 + 0 + 6"), fragments(decomposition))
        assertEquals(listOf("600 = 6 × 100"), fragments("La comprobación es 600 = 6 × 100."))
        for (language in listOf("Le 6 représente ", "The 6 contributes ")) {
            assertEquals(listOf("6", "6 × 100 = 600"), fragments("${language}6 × 100 = 600."))
        }
        val range = MathTextSemantics.ranges(feedback).last()
        assertEquals(feedback.lastIndexOf("6 ×"), range.start)
        assertEquals(feedback.lastIndexOf('.'), range.endExclusive)
    }

    @Test fun numericRelationsAndArithmeticChainsRetainTheirFullDecorationWithoutSentencePunctuation() {
        for (operator in listOf("=", "<", ">", "≤", "≥", "≠", "≈")) {
            val expression = "−2,5 + 3 $operator 0.5 × 1"
            assertEquals(operator, listOf(expression), fragments("Compara $expression."))
        }
        for (operator in listOf("+", "-", "−", "*", "×", "·", "/", "÷")) {
            val expression = "6 $operator 2 = 3"
            assertEquals(operator, listOf(expression), fragments("La expresión $expression termina aquí."))
        }
        assertEquals(listOf("1 < 2 ≤ 3", "−3 + 4 = +1"),
            fragments("Observa 1 < 2 ≤ 3; después −3 + 4 = +1."))
    }

    @Test fun extendingDecorationDoesNotTreatProtectedNumericSourcesAsMathematics() {
        for (source in listOf(
            "Texto sin cantidades ni expresiones.",
            "Código `6 × 100 = 600` y ```5006 = 5000 + 0 + 0 + 6```.",
            "Ver https://example.org/6+3=9 y C:\\temp\\6+3=9 y src/6+3=9.",
            "Rutas /tmp/6+3=9 y fechas 12/03/2026.",
            "Una fórmula inválida \\(\\unsupported{6+3=9}\\) no se decora parcialmente.",
            "Otra fórmula inválida \$\\unsupported{6+3=9}\$ tampoco se decora parcialmente.",
            "También \$\$\\unsupported{6+3=9}\$\$ queda sin decoración parcial."
        )) assertEquals(source, emptyList<String>(), fragments(source))
        assertTrue(MathTextSemantics.ranges("6 × 100 = 600", "text").isEmpty())
        assertTrue(MathTextSemantics.ranges("\\unsupported{6+3=9}", "block").isEmpty())
    }


    @Test fun isolatedNumbersAndCountedUnitsReceiveSemanticDecorationWithoutStylingProse() {
        assertEquals(listOf("2", "300"), fragments("Compara 2 con 300."))
        assertEquals(listOf("12", "4"), fragments("12 y 4"))
        assertEquals(listOf("1", "2", "3"), fragments("1, 2 y 3"))
        assertEquals(listOf("3 cm", "300 cm", "5 piezas"),
            fragments("Mide 3 cm, registra 300 cm y cuenta 5 piezas."))
        assertEquals(listOf("6 centenas", "100 unidades"),
            fragments("Hay 6 centenas y 100 unidades."))
        assertEquals(listOf("3 cm²", "2 kg", "7 unidades", "4 paquetes"),
            fragments("Necesitamos 3 cm², 2 kg, 7 unidades y 4 paquetes."))
        assertEquals(listOf("125 + 175 = 300 cm"),
            fragments("125 + 175 = 300 cm."))
        assertEquals(listOf("125 cm", "175 cm", "300 cm"),
            fragments("Dos listones miden 125 cm y 175 cm. El total es 300 cm."))
    }

    @Test fun unitLabelsAndEnglishFrenchQuantitiesShareTheSameDecorationRules() {
        assertEquals(listOf("cm"), fragments("Unidad: cm"))
        assertEquals(listOf("piezas"), fragments("Unidad: piezas"))
        assertEquals(listOf("pieces"), fragments("Unit: pieces"))
        assertEquals(listOf("pièces"), fragments("Unité : pièces"))
        assertEquals(listOf("3 pieces", "2 mètres"),
            fragments("Count 3 pieces et mesure 2 mètres."))
        assertEquals(emptyList<String>(), fragments("La unidad se indica en el rótulo."))
    }

    @Test fun codeDatesUrlsAndNonUnitProseAreNotAbsorbedByStandaloneNumbers() {
        val examples = listOf(
            "C++23 sigue siendo una referencia técnica.",
            "La versión 1.2.3 no es una cantidad.",
            "La fecha es 12/03/2026.",
            "Código `300 cm` y ```5 piezas```.",
            "https://example.org/3/4 y C:\\temp\\3/4 y src/3/4."
        )
        examples.forEach { source -> assertEquals(source, emptyList<String>(), fragments(source)) }
        assertEquals(listOf("7"), fragments("Hay 7 ejemplos sin unidad."))
        assertEquals(listOf("2", "3"), fragments("Tenemos 2 y luego 3."))
    }

    @Test fun offsetsUseOriginalUtf16WithoutChangingAnyTextOrSplittingSurrogatePairs() {
        val source = "🧮 E\u0301tudie 3/4 puis x²."
        val before = source.toCharArray()
        val ranges = MathTextSemantics.ranges(source)
        assertEquals(listOf("3/4", "x²"), fragments(source))
        assertEquals(source.indexOf("3/4"), ranges.first().start)
        ranges.forEach { range ->
            assertTrue(range.start >= 0 && range.start < range.endExclusive && range.endExclusive <= source.length)
            assertFalse(source[range.start].isLowSurrogate())
            assertFalse(source[range.endExclusive - 1].isHighSurrogate())
        }
        assertArrayEquals(before, source.toCharArray())
    }

    @Test fun balancedInlineFormattingHasSeparateOriginalDelimiterRangesWithoutRewritingMath() {
        val source = "🧮 Compara `x²` y ``3/4``; después ```a=b```."
        val ranges = MathTextSemantics.wrappers(source)
        assertEquals(listOf("`", "`", "``", "``", "```", "```"), ranges.map { source.substring(it.start, it.endExclusive) })
        assertEquals(source.indexOf('`'), ranges.first().start)
        assertTrue(ranges.zipWithNext().all { (first, second) -> first.endExclusive <= second.start })
        assertTrue(ranges.none { source.substring(it.start, it.endExclusive).any(Char::isLetterOrDigit) })
        assertEquals(source, source.toCharArray().concatToString())
    }

    @Test fun escapedEmptyUnmatchedAndMultilineInlineMarksStayLiteral() {
        for (source in listOf("", "`x", "x`", "``", "`   `", "`x\ny`", "\\`x\\`", "````x````")) {
            assertTrue(source, MathTextSemantics.wrappers(source).isEmpty())
        }
        val source = "\\`x\\` puis `y`"
        assertEquals(listOf("`", "`"), MathTextSemantics.wrappers(source).map { source.substring(it.start, it.endExclusive) })
        assertEquals(source.lastIndexOf("`y`"), MathTextSemantics.wrappers(source).first().start)
    }
}
