package com.learngraphicsprogramming.app.narration

import com.learngraphicsprogramming.app.math.MathExpression
import com.learngraphicsprogramming.app.math.MathLatexParser
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class SpanishMathSpeechTest {
    private fun speech(latex: String): String {
        val parsed = MathLatexParser.parse(latex) as MathLatexParser.Result.Success
        return SpanishMathSpeech.speak(parsed.expression)
    }

    @Test fun fractionsUseTheirStructuredMeaningAndKeepEquivalentNumerators() {
        assertEquals("un medio", speech("\\frac{1}{2}"))
        assertEquals("tres cuartos", speech("\\frac{3}{4}"))
        assertEquals("trece cuartos", speech("\\frac{13}{4}"))
        assertEquals("veintiún quintos", speech("\\frac{21}{5}"))
        assertEquals("menos un medio", speech("\\frac{-1}{2}"))
        assertEquals("equis dividido entre dos", speech("x/2"))
        assertEquals("equis sobre dos", speech("\\frac{x}{2}"))
    }

    @Test fun requestedExamplesAreNaturalSpanish() {
        assertEquals("un medio", SpanishMathSpeech.text("1/2"))
        assertEquals("tres cuartos", SpanishMathSpeech.text("3/4"))
        assertEquals("equis al cuadrado", SpanishMathSpeech.text("x²"))
        assertEquals("equis al cubo", SpanishMathSpeech.text("x³"))
        assertEquals("raíz cuadrada de nueve", SpanishMathSpeech.text("√9"))
        assertEquals("dos por tres", SpanishMathSpeech.text("2 × 3"))
        assertEquals("a es igual a be", SpanishMathSpeech.text("a = b"))
    }

    @Test fun formulasInProseKeepProseAndMathBoundaries() {
        assertEquals("En tres quintos tomamos tres partes.",
            SpanishMathSpeech.text("En \\(\\frac{3}{5}\\) tomamos tres partes."))
        assertEquals("Calcula equis al cuadrado y raíz cuadrada de nueve.",
            SpanishMathSpeech.text("Calcula x² y √9."))
        assertEquals("tres cuartos de una unidad", SpanishMathSpeech.text("3/4 de una unidad", "block"))
        assertEquals("nueve coma cinco menos dos es igual a hueco", SpanishMathSpeech.text("9,5 − 2 = ?", "block"))
        assertEquals("equis por tres", SpanishMathSpeech.text("x \\times 3", "block"))
        assertEquals("expresión matemática no interpretable", SpanishMathSpeech.text("x \\unsupported{3}", "block"))
    }

    @Test fun slashesInPathsDatesAndCodeAreNotFractions() {
        val prose = "Abre C:/1/2/main.cpp, /1/2/documento, src/main.cpp o https://ejemplo.es/1/2 el 08/10/2026. Usa `1/2` en C++."
        assertEquals(prose, SpanishMathSpeech.text(prose))
        assertEquals("Texto 3/4 sin conversión", SpanishMathSpeech.text("Texto 3/4 sin conversión", "text"))
        assertEquals("Una fracción: tres cuartos.", SpanishMathSpeech.text("Una fracción: 3/4."))
    }

    @Test fun groupingAndCompoundFractionsStayUnambiguous() {
        val speech = speech("\\frac{x+1}{2}+(x+2)^3")
        assertTrue(speech.contains("numerador, equis más uno; y denominador, dos, fin de fracción"))
        assertTrue(speech.contains("abre paréntesis, equis más dos, cierra paréntesis al cubo"))
        assertEquals("raíz cúbica de ocho", speech("\\sqrt[3]{8}"))
        assertEquals("dos por equis más uno", speech("2x+1"))
        assertEquals("base, equis más uno; fin de base al cuadrado", speech("{x+1}^2"))
        assertEquals("abre paréntesis, uno, dos, cierra paréntesis", SpanishMathSpeech.text("(1,2)"))
    }

    @Test fun structuredDigitLeavesRemainOneNumber() {
        val number = MathExpression.Sequence(listOf(MathExpression.Number("1"), MathExpression.Number("3")))
        assertEquals("trece", SpanishMathSpeech.speak(number))
        assertEquals("cero coma cero cinco", speech("0.05"))
    }

    @Test fun calculusUsesExistingLargeOperatorSiblingStructure() {
        assertEquals("integral desde cero hasta uno, de equis", speech("\\int_{0}^{1}x"))
        assertTrue(speech("\\lim_{x\\to 0}{x+1}").contains("cuando equis tiende a cero"))
        assertFalse(SpanishMathSpeech.text("Valor \\(\\unsupported{x}\\)").contains("unsupported"))
    }
}
