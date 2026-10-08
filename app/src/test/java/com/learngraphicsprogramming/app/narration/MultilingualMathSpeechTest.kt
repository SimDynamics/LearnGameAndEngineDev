package com.learngraphicsprogramming.app.narration

import com.learngraphicsprogramming.app.math.MathDelimiter
import com.learngraphicsprogramming.app.math.MathExpression
import com.learngraphicsprogramming.app.math.MathLargeOperator
import com.learngraphicsprogramming.app.math.MathLatexParser
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class MultilingualMathSpeechTest {
    private val english = NarrationLanguage.AMERICAN_ENGLISH
    private val british = NarrationLanguage.BRITISH_ENGLISH
    private val french = NarrationLanguage.FRENCH
    private fun seq(vararg values: MathExpression) = MathExpression.Sequence(values.toList())
    private fun number(value: String) = seq(MathExpression.Number(value))
    private fun speech(latex: String, language: NarrationLanguage): String = MultilingualMathSpeech.speak(
        (MathLatexParser.parse(latex) as MathLatexParser.Result.Success).expression, language
    )

    @Test fun theSameStructuredMathTreeHasIndependentLanguageReadings() {
        val tree = seq(MathExpression.Fraction(number("3"), number("4")), MathExpression.Operator("+"),
            MathExpression.Scripts(seq(MathExpression.Variable("x")), upper = number("2")), MathExpression.Relation("="),
            MathExpression.Root(number("9")))
        val snapshot = tree.toString()
        assertEquals("three quarters plus ex squared equals square root of nine", MultilingualMathSpeech.speak(tree, english))
        assertEquals("trois quarts plus ix au carré est égal à racine carrée de neuf", MultilingualMathSpeech.speak(tree, french))
        assertEquals("tres cuartos más equis al cuadrado es igual a raíz cuadrada de nueve", MultilingualMathSpeech.speak(tree, NarrationLanguage.SPANISH))
        assertEquals(snapshot, tree.toString())
    }

    @Test fun groupingAndFractionBoundariesPreserveTheirMathematicalMeaning() {
        assertEquals("one half", speech("\\frac{1}{2}", english))
        assertEquals("two halves", speech("\\frac{2}{2}", english))
        assertEquals("un demi", speech("\\frac{1}{2}", french))
        assertEquals("deux tiers", speech("\\frac{2}{3}", french))
        assertEquals("ex over two", speech("\\frac{x}{2}", english))
        assertEquals("ix sur deux", speech("\\frac{x}{2}", french))
        assertEquals("fraction with numerator, ex plus one; and denominator, two, end of fraction", speech("\\frac{x+1}{2}", english))
        assertEquals("fraction de numérateur, ix plus un; et de dénominateur, deux, fin de la fraction", speech("\\frac{x+1}{2}", french))
        assertEquals("base, ex plus one; end of base squared", speech("{x+1}^2", english))
        assertEquals("base, ix plus un; fin de la base au carré", speech("{x+1}^2", french))
        assertEquals("open parenthesis, ex plus two, close parenthesis cubed", speech("(x+2)^3", english))
        assertEquals("cube root of eight", speech("\\sqrt[3]{8}", english))
        assertEquals("racine cubique de huit", speech("\\sqrt[3]{8}", french))
    }

    @Test fun proseMathKeepsPathsDatesAndCodeAndUsesSelectedLanguage() {
        assertEquals("Take three fifths of the area.", MultilingualMathSpeech.text("Take \\(\\frac{3}{5}\\) of the area.", language = english))
        assertEquals("Calcule ix au carré et racine carrée de neuf.", MultilingualMathSpeech.text("Calcule x² et √9.", language = french))
        val prose = "C:/1/2/main.cpp /1/2/document src/main.cpp https://example.com/1/2 08/10/2026 `1/2` C++"
        assertEquals(prose, MultilingualMathSpeech.text(prose, language = english))
        assertEquals(prose, MultilingualMathSpeech.text(prose, language = french))
        assertEquals("Keep 3/4", MultilingualMathSpeech.text("Keep 3/4", "text", english))
        assertEquals("Value unreadable mathematical expression", MultilingualMathSpeech.text("Value \\(\\unsupported{x}\\)", language = english))
        assertEquals("Valeur expression mathématique illisible", MultilingualMathSpeech.text("Valeur \\(\\unsupported{x}\\)", language = french))
    }

    @Test fun numberNamesCoverFrenchAndBothEnglishVariantsWithoutChangingDigitLeaves() {
        val american = MultilingualMathSpeech.speak(number("101"), english)
        assertEquals("one hundred one", american)
        assertEquals("one hundred and one", MultilingualMathSpeech.speak(number("101"), british))
        assertEquals("zee", MultilingualMathSpeech.speak(MathExpression.Variable("z"), english))
        assertEquals("zed", MultilingualMathSpeech.speak(MathExpression.Variable("z"), british))
        val expected = mapOf("21" to "vingt et un", "71" to "soixante et onze", "80" to "quatre-vingts",
            "81" to "quatre-vingt-un", "90" to "quatre-vingt-dix", "200" to "deux cents", "201" to "deux cent un",
            "80000" to "quatre-vingt mille", "200000" to "deux cent mille", "200000000" to "deux cents millions")
        expected.forEach { (digits, words) -> assertEquals(digits, words, MultilingualMathSpeech.speak(number(digits), french)) }
        assertEquals("zero point zero five", speech("0.05", english))
        assertEquals("zéro virgule zéro cinq", speech("0.05", french))
        val digitLeaves = seq(MathExpression.Number("1"), MathExpression.Number("3"))
        assertEquals("thirteen", MultilingualMathSpeech.speak(digitLeaves, english))
        assertEquals("treize", MultilingualMathSpeech.speak(digitLeaves, french))
    }

    @Test fun calculusConsumesTheExistingOperatorAndItsFollowingSibling() {
        val variable = seq(MathExpression.Variable("x"))
        val integral = seq(MathExpression.LargeOperator(MathLargeOperator.INTEGRAL, number("0"), number("1")), variable.children.single())
        assertEquals("integral from zero to one, of ex", MultilingualMathSpeech.speak(integral, english))
        assertEquals("intégrale de zéro à un, de ix", MultilingualMathSpeech.speak(integral, french))
        val derivative = MathExpression.Derivative(variable, variable, number("2"))
        assertEquals("derivative of order two of ex, with respect to ex", MultilingualMathSpeech.speak(derivative, english))
        assertEquals("dérivée d'ordre deux de ix, par rapport à ix", MultilingualMathSpeech.speak(derivative, french))
        assertTrue(speech("\\lim_{x\\to 0}{x+1}", english).contains("as ex tends to zero"))
        assertTrue(speech("\\lim_{x\\to 0}{x+1}", french).contains("lorsque ix tend vers zéro"))
        val logarithm = MathExpression.Function("log", number("8"), number("2"))
        assertEquals("logarithm in base two of eight", MultilingualMathSpeech.speak(logarithm, english))
        assertEquals("logarithme en base deux de huit", MultilingualMathSpeech.speak(logarithm, french))
    }

    @Test fun matricesCasesIndicesAndEmptySlotsHaveAudibleStructure() {
        val matrix = MathExpression.Matrix(listOf(listOf(number("1"), number("2")), listOf(number("3"), number("4"))))
        assertEquals("matrix. Row one: one; two. Row two: three; four", MultilingualMathSpeech.speak(matrix, english))
        assertEquals("matrice. Ligne un: un; deux. Ligne deux: trois; quatre", MultilingualMathSpeech.speak(matrix, french))
        val cases = MathExpression.Cases(listOf(MathExpression.Case(number("1"), seq(MathExpression.Variable("x"), MathExpression.Relation(">"), MathExpression.Number("0")))))
        assertEquals("piecewise expression. one, if ex is greater than zero", MultilingualMathSpeech.speak(cases, english))
        assertEquals("expression par morceaux. un, si ix est supérieur à zéro", MultilingualMathSpeech.speak(cases, french))
        val script = MathExpression.Scripts(seq(MathExpression.Variable("a")), upper = number("4"), lower = number("1"))
        assertEquals("ay subscript one to the power of four", MultilingualMathSpeech.speak(script, english))
        assertEquals("a indice un à la puissance quatre", MultilingualMathSpeech.speak(script, french))
        assertEquals("blank", MultilingualMathSpeech.speak(seq(), english))
        assertEquals("case vide", MultilingualMathSpeech.speak(seq(), french))
        assertEquals("absolute value of ex, end of absolute value", MultilingualMathSpeech.speak(MathExpression.Group(seq(MathExpression.Variable("x")), MathDelimiter.ABSOLUTE), english))
        assertFalse(MultilingualMathSpeech.speak(cases, french).contains("equis"))
    }
}
