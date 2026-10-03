package com.learngraphicsprogramming.app.math

import org.junit.Assert.*
import org.junit.Test

class MathLatexTextTest {
    private fun content(source: String): MathExpression.Sequence {
        val result = MathLatexParser.parseContent(source)
        assertTrue("$source: $result", result is MathLatexParser.Result.Success)
        return (result as MathLatexParser.Result.Success).expression
    }

    @Test fun completeContentPreservesProseWordsAndTheirSpaces() {
        val tree = content("\\frac{3}{4} de una unidad")
        assertTrue(tree.children.first() is MathExpression.Fraction)
        assertEquals(listOf(" ", "de", " ", "una", " ", "unidad"),
            tree.children.drop(1).map { (it as MathExpression.Symbol).value })
        assertEquals("  pintura de una unidad  ", content("  pintura de una unidad  ")
            .children.joinToString("") { (it as MathExpression.Symbol).value })
        val grouped = content("Unidad (de una unidad)").children.last() as MathExpression.Group
        assertEquals("de una unidad", grouped.content.children.joinToString("") { (it as MathExpression.Symbol).value })
        val punctuation = content("\\frac{3}{4} de una unidad. ¿Correcto?")
        assertTrue(punctuation.children.first() is MathExpression.Fraction)
        assertEquals(" de una unidad. ¿Correcto?", punctuation.children.drop(1)
            .joinToString("") { (it as MathExpression.Symbol).value })
    }

    @Test fun explicitLatexTextPreservesInternalWhitespaceAndCanonicalRoundTrip() {
        val source = "\\frac{3}{4}\\text{ de una unidad}"
        val parsed = MathLatexParser.parse(source) as MathLatexParser.Result.Success
        assertEquals(MathExpression.Symbol(" de una unidad"), parsed.expression.children.last())
        assertEquals(parsed.expression,
            (MathLatexParser.parse(MathLatexSerializer.serialize(parsed.expression)) as MathLatexParser.Result.Success).expression)
        assertEquals(parsed.expression, content(source))
        val escapedSpace = content("\\frac{3}{4}\\ de una unidad")
        assertEquals(MathExpression.Symbol(" "), escapedSpace.children[1])
        val quad = content("x\\quad y").children.filterIsInstance<MathExpression.Symbol>().map { it.value }
        assertTrue(quad.contains("\u2003"))
    }

    @Test fun proseSpacingDoesNotChangeAnswerParserOrStructuralSlots() {
        val compact = (MathLatexParser.parse("\\frac{x+1}{2}+x_{1}^{2}") as MathLatexParser.Result.Success).expression
        val spaced = (MathLatexParser.parse("  \\frac{ x + 1 }{ 2 } + x _{ 1 } ^{ 2 }  ") as MathLatexParser.Result.Success).expression
        assertEquals(compact, spaced)
        val content = content("\\frac{ x + 1 }{ 2 } unidades")
        assertEquals(compact.children.first(), content.children.first())
        assertEquals(MathExpression.Symbol(" "), content.children[1])
    }

    @Test fun simultaneousScriptsHaveOneBaseInEitherSourceOrder() {
        val upperFirst = (MathLatexParser.parse("x^{22}_{1}") as MathLatexParser.Result.Success).expression
        val lowerFirst = (MathLatexParser.parse("x_{1}^{22}") as MathLatexParser.Result.Success).expression
        assertEquals(upperFirst, lowerFirst)
        val scripts = upperFirst.children.single() as MathExpression.Scripts
        assertEquals(listOf(MathExpression.Variable("x")), scripts.base.children)
        assertEquals("{x}_{1}^{22}", MathLatexSerializer.serialize(upperFirst))
    }
}
