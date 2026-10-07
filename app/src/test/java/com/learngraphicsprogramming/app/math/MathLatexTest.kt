package com.learngraphicsprogramming.app.math

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class MathLatexTest {
    @Test fun scriptsAfterPostfixOperatorsOwnTheirCompleteOperandAndRoundTrip() {
        for ((source, canonicalBase) in listOf(
            "5!^2" to "{5!}^{2}",
            "50%^2" to "{50\\%}^{2}",
            "5!%^2" to "{5!\\%}^{2}",
            "2+5!^2" to "2+{5!}^{2}",
            "2(3+1)!^2" to "2{\\left(3+1\\right)!}^{2}"
        )) {
            assertEquals(parse(canonicalBase), roundTrip(source))
        }
        val incomplete = roundTrip("2+!^2")
        assertEquals(listOf(MathExpression.Number("2"), MathExpression.Operator("+")), incomplete.children.take(2))
        assertEquals(parse("!").children, (incomplete.children.last() as MathExpression.Scripts).base.children)
    }

    @Test fun keypadScriptsWrapPostfixChainsWithoutAbsorbingBinaryOperatorsOrSelections() {
        val model = MathExpressionModel()
        for (part in listOf("2", "+", "1", "2", "%", "!", "^", "2")) assertTrue(model.insertText(part))
        assertEquals(parse("2+{12\\%!}^{2}"), model.expression)
        assertEquals("upper", model.cursor.path.last().slot)
        val restored = MathExpressionModel()
        assertTrue(restored.restoreDraft(model.saveDraft()))
        assertEquals(model.expression, restored.expression)
        assertEquals(model.cursor, restored.cursor)

        val selected = MathExpressionModel(parse("2+5!"))
        selected.selectAll()
        assertTrue(selected.addScript(upper = true))
        assertTrue(selected.insertText("2"))
        assertEquals(parse("{2+5!}^{2}"), selected.expression)

        val incomplete = MathExpressionModel()
        for (part in listOf("2", "+", "!", "^", "2")) assertTrue(incomplete.insertText(part))
        assertEquals(parse("2+!^2"), incomplete.expression)
    }

    @Test fun postfixPowerInsideAFractionRetainsItsSlotAndWholeSplitNumber() {
        val model = MathExpressionModel()
        assertTrue(model.insert(MathExpression.Fraction(MathExpression.Sequence(), parse("2")), "numerator"))
        for (part in listOf("1", "2", "%", "^", "2")) assertTrue(model.insertText(part))
        assertEquals(parse("\\frac{{12\\%}^{2}}{2}"), model.expression)
        assertEquals(listOf("numerator", "upper"), model.cursor.path.map { it.slot })
        val restored = MathExpressionModel()
        assertTrue(restored.restoreDraft(model.saveDraft()))
        assertEquals(model.expression, restored.expression)
        assertEquals(model.cursor, restored.cursor)
    }

    @Test fun calculatorOperatorsPreserveSemanticNodesAndDrafts() {
        for (source in listOf("50%+5!", "5nCr2+5nPr2", "\\arcsin{0.5}+\\arccos{0.5}+\\arctan{1}")) {
            roundTrip(source)
        }
        assertEquals(parse("50%"), parse("50\\%"))
        assertEquals(parse("5nCr2"), parse("5\\operatorname{nCr}2"))
        val model = MathExpressionModel()
        for (part in listOf("5", "nCr", "2", "+", "5", "!", "+", "50", "%")) {
            assertTrue(model.insertText(part))
        }
        assertEquals(parse("5nCr2+5!+50%"), model.expression)
        val draft = model.saveDraft()
        val restored = MathExpressionModel()
        assertTrue(restored.restoreDraft(draft))
        assertEquals(model.expression, restored.expression)
        assertEquals(model.cursor, restored.cursor)
        // Older drafts used generic symbols. Their representation remains importable.
        val legacy = MathExpression.Sequence(listOf(MathExpression.Number("50"), MathExpression.Symbol("%")))
        assertEquals(legacy, parse(MathLatexSerializer.serialize(legacy)))
    }

    private fun parse(source: String): MathExpression.Sequence {
        val result = MathLatexParser.parse(source)
        assertTrue("$source: $result", result is MathLatexParser.Result.Success)
        return (result as MathLatexParser.Result.Success).expression
    }

    private fun roundTrip(source: String): MathExpression.Sequence {
        val tree = parse(source)
        val latex = MathLatexSerializer.serialize(tree)
        assertEquals("Canonical serialization must preserve the mathematical tree: $latex", tree, parse(latex))
        assertEquals(latex, MathLatexSerializer.serialize(parse(latex)))
        return tree
    }

    @Test fun fractionHasIndependentNumeratorAndDenominator() {
        val fraction = roundTrip("\\frac{x+1}{2}").children.single() as MathExpression.Fraction
        assertEquals(3, fraction.numerator.children.size)
        assertEquals(MathExpression.Number("2"), fraction.denominator.children.single())
        assertEquals("\\frac{x+1}{2}", MathLatexSerializer.serialize(fraction))
    }

    @Test fun exponentsAndSubscriptsAreStructuralAndCanonical() {
        val scripts = roundTrip("x_1^2").children.single() as MathExpression.Scripts
        assertEquals(MathExpression.Number("1"), scripts.lower!!.children.single())
        assertEquals(MathExpression.Number("2"), scripts.upper!!.children.single())
        assertEquals("{x}_{1}^{2}", MathLatexSerializer.serialize(scripts))
        roundTrip("{x+y}^{\\frac{1}{2}}")
    }

    @Test fun squareAndNthRootsPreserveNestedContent() {
        assertEquals(null, (roundTrip("\\sqrt{x+4}").children.single() as MathExpression.Root).index)
        val cube = roundTrip("\\sqrt[3]{\\frac{x+1}{\\sqrt{2}}}").children.single() as MathExpression.Root
        assertEquals(MathExpression.Number("3"), cube.index!!.children.single())
        assertTrue(cube.radicand.children.single() is MathExpression.Fraction)
    }

    @Test fun groupingAbsoluteValuesFunctionsAndLogBasesRoundTrip() {
        roundTrip("\\left(x+1\\right)")
        roundTrip("\\left|x\\right|")
        roundTrip("\\left\\{x+1\\right\\}")
        roundTrip("\\sin(x)+\\cos(y)+\\log_2(x)+\\ln{x}")
        roundTrip("\\operatorname{f}{x+1}")
    }

    @Test fun constantsOperatorsAndRelationsNormalizeWithoutTextComparison() {
        val tree = roundTrip("2\\pi+e\\leq x\\times y\\neq0")
        assertTrue(tree.children.any { it == MathExpression.Symbol("π") })
        assertTrue(tree.children.any { it == MathExpression.Relation("≤") })
        assertTrue(tree.children.any { it == MathExpression.Operator("×") })
        assertEquals(parse("4\\pi"), parse("4pi"))
        assertEquals(parse("x+1"), parse("\\(x+1\\)"))
        roundTrip("A\\cup B\\cap C\\in D+50%")
    }

    @Test fun largeOperatorsHaveEditableBoundsAndIndependentFollowingSiblingsWhileLimitsRetainTheirBody() {
        val integralTree = roundTrip("\\int_0^1 x^2\\,dx")
        val integral = integralTree.children.first() as MathExpression.LargeOperator
        assertEquals(MathLargeOperator.INTEGRAL, integral.kind)
        assertEquals(MathExpression.Number("0"), integral.lower!!.children.single())
        assertEquals(MathExpression.Number("1"), integral.upper!!.children.single())
        assertEquals(listOf("upper", "lower"), integral.navigationChildren().map { it.id })
        assertEquals(parse("x^2dx").children, integralTree.children.drop(1))
        val sum = roundTrip("\\sum_{i=1}^{n}i")
        assertEquals(MathLargeOperator.SUM, (sum.children.first() as MathExpression.LargeOperator).kind)
        assertEquals(parse("i").children, sum.children.drop(1))
        val product = roundTrip("\\prod_{i=1}^{n}{i+1}")
        assertEquals(MathLargeOperator.PRODUCT, (product.children.first() as MathExpression.LargeOperator).kind)
        assertEquals(parse("i+1").children, product.children.drop(1))
        val limit = roundTrip("\\lim_{x\\to0}{\\frac{\\sin{x}}{x}}").children.single() as MathExpression.Limit
        assertTrue(limit.condition.children.contains(MathExpression.Relation("→")))
        assertTrue(limit.body.children.single() is MathExpression.Fraction)
    }

    @Test fun supportedUnicodeSymbolsHaveTheSameCanonicalTreeAsLatexCommands() {
        val unicode = roundTrip("θ+α+β+γ+λ+μ+σ+Δ")
        val latex = roundTrip("\\theta+\\alpha+\\beta+\\gamma+\\lambda+\\mu+\\sigma+\\Delta")
        assertEquals(latex, unicode)
        assertEquals(MathLatexSerializer.serialize(latex), MathExpressionSemantics.canonical("θ+α+β+γ+λ+μ+σ+Δ"))
    }

    @Test fun derivativesHaveBodyVariableAndOptionalOrder() {
        val first = roundTrip("\\frac{\\mathrm{d}x}{\\mathrm{d}x}").children.single() as MathExpression.Derivative
        assertEquals(null, first.order)
        val second = roundTrip("\\frac{\\mathrm{d}^{2}x}{\\mathrm{d}{x}^{2}}").children.single() as MathExpression.Derivative
        assertEquals(MathExpression.Number("2"), second.order!!.children.single())
    }

    @Test fun matricesAndCasesRetainTheirTwoDimensionalShape() {
        val matrix = roundTrip("\\begin{matrix}1&\\frac{1}{2}\\\\x&\\sqrt{y}\\end{matrix}").children.single() as MathExpression.Matrix
        assertEquals(2, matrix.rows.size)
        assertEquals(2, matrix.rows[0].size)
        val cases = roundTrip("\\begin{cases}x&x>0\\\\-x&x\\le0\\end{cases}").children.single() as MathExpression.Cases
        assertEquals(2, cases.rows.size)
        roundTrip("\\begin{pmatrix}1&2\\\\3&4\\end{pmatrix}")
    }

    @Test fun malformedOrUnsupportedLatexReturnsControlledErrors() {
        listOf("\\frac{x}", "\\sqrt[3{x}", "x^{2", "x^^2", "\\unknown{x}", "\\left(x\\right]", "\\begin{matrix}a&b\\\\c\\end{matrix}", "\\begin{matrix}x\\end{cases}", "\\begin{vmatrix}1&2\\\\3&4\\end{vmatrix}", "\\input{secrets}", "\\", "}").forEach {
            val error = MathLatexParser.parse(it)
            assertTrue("$it should fail: $error", error is MathLatexParser.Result.Failure)
            error as MathLatexParser.Result.Failure
            assertTrue(error.start in 0..it.length)
            assertTrue(error.end in error.start..it.length)
        }
        assertTrue(MathLatexParser.parse("{".repeat(100) + "x" + "}".repeat(100)) is MathLatexParser.Result.Failure)
        assertTrue(MathLatexParser.parse("x".repeat(40_000)) is MathLatexParser.Result.Failure)
    }

    @Test
    fun symjaLatexOutputsParseAndRoundTrip()
    {
        val outputs = listOf(
            "\\frac{x}{\\sqrt{5}}",
            "x",
            "3 \\cdot {x}^{2}",
            "\\frac{{x}^{3}}{3}",
            "\\{\\{x\\to -2\\},\\{x\\to 2\\}\\}"
        )

        outputs.forEach { source ->
            println()
            println("SOURCE:")
            println(source)

            val tree = parse(source)

            println("AST:")
            println(tree)

            val serialized =
                MathLatexSerializer.serialize(tree)

            println("SERIALIZED:")
            println(serialized)

            val reparsed =
                parse(serialized)

            println("ROUND TRIP TREE EQUAL:")
            println(tree == reparsed)

            assertEquals(
                tree,
                reparsed
            )
        }
    }

    @Test fun fractionEditingNavigatesStructuralSlotsAndRetainsCursor() {
        val model = MathExpressionModel()
        model.insert(MathExpression.Fraction(MathExpression.Sequence(), MathExpression.Sequence()), "numerator")
        assertEquals("numerator", model.cursor.path.last().slot)
        model.insertText("3")
        model.nextSlot()
        assertEquals("denominator", model.cursor.path.last().slot)
        model.insertText("4")
        model.nextSlot()
        assertTrue(model.cursor.path.isEmpty())
        assertEquals("\\frac{3}{4}", MathLatexSerializer.serialize(model.expression))
        model.previousSlot()
        assertEquals("denominator", model.cursor.path.last().slot)
        model.deleteBackward()
        assertEquals("\\frac{3}{}", MathLatexSerializer.serialize(model.expression))
        assertFalse(model.isComplete())
    }

    @Test fun deletingInsideAnEmptyStructureRetainsItAndNeutralDeletionRemovesItSafely() {
        val model = MathExpressionModel()
        model.insert(MathExpression.Fraction(MathExpression.Sequence(), MathExpression.Sequence()), "numerator")
        model.deleteBackward()
        assertTrue(model.expression.children.single() is MathExpression.Fraction)
        model.cursor = MathCursor(offset = 1)
        model.deleteBackward()
        assertEquals(MathExpression.Sequence(), model.expression)
        assertEquals(MathCursor(), model.cursor)
        model.insert(MathExpression.Root(MathExpression.Sequence()), "radicand")
        model.deleteBackward()
        assertTrue(model.expression.children.single() is MathExpression.Root)
        model.cursor = MathCursor(offset = 1)
        model.deleteBackward()
        assertTrue(model.expression.children.isEmpty())
    }

    @Test fun selectionCanBeWrappedAndDeletedStructurally() {
        val model = MathExpressionModel(parse("x+1"))
        model.selectAll()
        val selected = model.takeSelection()
        model.insert(MathExpression.Fraction(selected, MathExpression.Sequence()), "denominator")
        model.insertText("2")
        assertEquals("\\frac{x+1}{2}", MathLatexSerializer.serialize(model.expression))
        model.selectAll()
        model.deleteBackward()
        assertTrue(model.expression.children.isEmpty())
    }

    @Test fun incompleteDraftRoundTripPreservesAstCursorAndSelection() {
        val original = MathExpressionModel()
        original.insert(MathExpression.Fraction(parse("x+1"), MathExpression.Sequence()), "denominator")
        original.insert(MathExpression.Root(MathExpression.Sequence()), "radicand")
        val restored = MathExpressionModel()
        assertTrue(restored.restoreDraft(original.saveDraft()))
        assertEquals(original.expression, restored.expression)
        assertEquals(original.cursor, restored.cursor)
        assertFalse(restored.isComplete())
        restored.insertText("4")
        assertTrue(restored.isComplete())
        original.selectAll()
        assertTrue(restored.restoreDraft(original.saveDraft()))
        assertEquals(original.selectionRange, restored.selectionRange)
    }

    @Test fun invalidPasteAndInvalidDraftLeaveExistingExpressionUntouched() {
        val model = MathExpressionModel(parse("x+1"))
        val before = model.saveDraft()
        assertTrue(model.insertLatex("\\input{x}") is MathLatexParser.Result.Failure)
        assertFalse(model.insertText("\\invalid"))
        assertFalse(model.restoreDraft("{\"version\":1,\"expression\":{\"type\":\"bogus\"}}"))
        assertEquals(before, model.saveDraft())
    }

    @Test fun androidTextAndScriptsEditAstRatherThanLatexStrings() {
        val model = MathExpressionModel()
        model.insertText("x")
        model.insertText("^")
        model.insertText("2")
        assertEquals("upper", model.cursor.path.last().slot)
        assertEquals("{x}^{2}", MathLatexSerializer.serialize(model.expression))
        model.moveRight()
        model.insertText("+3")
        assertEquals("{x}^{2}+3", MathLatexSerializer.serialize(model.expression))
        model.clear()
        assertTrue(model.expression.children.isEmpty())
    }

    @Test fun successiveAndroidNumberCommitsKeepTheEntireNumberAsPowerBase() {
        val model = MathExpressionModel()
        model.insertText("1")
        model.insertText("2")
        assertEquals(listOf(MathExpression.Number("12")), model.expression.children)
        model.insertText("^")
        model.insertText("2")
        assertEquals("{12}^{2}", MathLatexSerializer.serialize(model.expression))
        model.deleteBackward()
        assertFalse(model.isComplete())
    }

    @Test fun oversizePlainPasteIsRejectedAtomically() {
        val model = MathExpressionModel(parse("x+1"))
        model.selectAll()
        val original = model.saveDraft()
        assertFalse(model.insertText("a".repeat(5_000)))
        assertEquals(original, model.saveDraft())
        assertFalse(model.insertText("(".repeat(100)))
        assertEquals(original, model.saveDraft())
    }

    @Test fun latexImportIntoAnAlmostFullTreeReturnsFailureWithoutLosingState() {
        val model = MathExpressionModel(parse("x".repeat(4_090)))
        val original = model.saveDraft()
        assertTrue(model.insertLatex("a".repeat(10)) is MathLatexParser.Result.Failure)
        assertEquals(original, model.saveDraft())
        model.selectAll()
        assertTrue(model.insertLatex("\\frac{x}{2}") is MathLatexParser.Result.Success)
        assertEquals("\\frac{x}{2}", MathLatexSerializer.serialize(model.expression))
    }
}
