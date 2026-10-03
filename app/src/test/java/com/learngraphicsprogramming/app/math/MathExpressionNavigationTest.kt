package com.learngraphicsprogramming.app.math

import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class MathExpressionNavigationTest {
    private fun parse(source: String): MathExpression.Sequence =
        (MathLatexParser.parse(source) as MathLatexParser.Result.Success).expression

    private fun model(source: String) = MathExpressionModel(parse(source))
    private fun latex(model: MathExpressionModel) = MathLatexSerializer.serialize(model.expression)
    private fun active(model: MathExpressionModel) = model.cursor.path.lastOrNull()?.slot
    private fun enter(model: MathExpressionModel, slot: String, offset: Int = 0) {
        model.cursor = MathCursor(listOf(MathCursorStep(0, slot)), offset)
    }

    @Test fun rightAndImeNavigationExhaustBothScriptsBeforeTheFieldBoundary() {
        val model = model("x_1^2")
        model.cursor = MathCursor()
        assertTrue(model.moveRight())
        assertEquals("base", active(model))
        assertTrue(model.nextSlot())
        assertEquals("upper", active(model))
        assertTrue(model.nextSlot())
        assertEquals("lower", active(model))
        assertTrue(model.moveRight())
        assertEquals("lower", active(model))
        assertTrue(model.moveRight())
        assertEquals(MathCursor(offset = 1), model.cursor)
        assertFalse(model.moveRight())
        assertFalse(model.nextSlot())
        assertTrue(model.previousSlot())
        assertEquals("lower", active(model))
        assertTrue(model.previousSlot())
        assertEquals("upper", active(model))
        assertTrue(model.previousSlot())
        assertEquals("base", active(model))
        assertTrue(model.previousSlot())
        assertEquals(MathCursor(), model.cursor)
        assertFalse(model.previousSlot())
    }

    @Test fun imeNextVisitsNestedStructuresBeforeTheNextOuterSlot() {
        val model = model("\\frac{\\sqrt[3]{x}}{2}")
        model.cursor = MathCursor()
        assertTrue(model.nextSlot())
        assertEquals("numerator", active(model))
        assertTrue(model.nextSlot())
        assertEquals("index", active(model))
        assertTrue(model.nextSlot())
        assertEquals("radicand", active(model))
        assertTrue(model.nextSlot())
        assertEquals(listOf(MathCursorStep(0, "numerator")), model.cursor.path)
        assertTrue(model.nextSlot())
        assertEquals("denominator", active(model))
        assertTrue(model.nextSlot())
        assertTrue(model.cursor.path.isEmpty())
        assertFalse(model.nextSlot())
    }

    @Test fun repeatedSlotChangesWriteOnlyIntoTheCurrentLogarithmSlot() {
        val model = MathExpressionModel()
        assertTrue(model.insert(MathExpression.Function("log", MathExpression.Sequence(), MathExpression.Sequence()), "base"))
        assertTrue(model.insertText("2"))
        assertTrue(model.nextSlot())
        assertTrue(model.insertText("x"))
        assertTrue(model.previousSlot())
        assertTrue(model.insertText("3"))
        assertTrue(model.nextSlot())
        assertEquals("argument", active(model))
        assertEquals(0, model.cursor.offset)
        model.cursor = model.cursor.copy(offset = model.sequenceAt(model.cursor.path)!!.children.size)
        assertTrue(model.insertText("+1"))
        assertTrue(model.previousSlot())
        model.deleteBackward()
        assertTrue(model.nextSlot())
        assertEquals("argument", active(model))
        assertEquals(0, model.cursor.offset)
        model.cursor = model.cursor.copy(offset = model.sequenceAt(model.cursor.path)!!.children.size)
        assertTrue(model.insertText("+4"))
        assertEquals("\\log_{2}{x+1+4}", latex(model))
        assertTrue(model.isComplete())
    }

    @Test fun scriptActionsFromInsideEitherScriptShareOneBase() {
        val model = MathExpressionModel()
        assertTrue(model.insertText("x"))
        assertTrue(model.addScript(upper = true))
        assertTrue(model.insertText("2"))
        assertTrue(model.addScript(upper = false))
        assertEquals("lower", active(model))
        assertTrue(model.insertText("1"))
        assertTrue(model.addScript(upper = true))
        assertEquals("upper", active(model))
        assertTrue(model.insertText("3"))
        assertTrue(model.addScript(upper = false))
        assertTrue(model.insertText("4"))
        val scripts = model.expression.children.single() as MathExpression.Scripts
        assertEquals(parse("x"), scripts.base)
        assertEquals(parse("23"), scripts.upper)
        assertEquals(parse("14"), scripts.lower)
        assertEquals("{x}_{14}^{23}", latex(model))
    }

    @Test fun typedScriptOperatorsUseTheSameStructuralMergeAsToolbarActions() {
        val model = MathExpressionModel()
        listOf("x", "^", "2", "_", "1").forEach { assertTrue(model.insertText(it)) }
        assertEquals("{x}_{1}^{2}", latex(model))
        assertEquals("lower", active(model))
        val scripts = model.expression.children.single() as MathExpression.Scripts
        assertEquals(parse("x"), scripts.base)
        assertFalse(scripts.base.children.single() is MathExpression.Scripts)
    }

    @Test fun aNewScriptStartsAtItsEmptyBaseAndKeepsEverySlotReachable() {
        val model = MathExpressionModel()
        assertTrue(model.addScript(upper = false))
        assertEquals("base", active(model))
        assertTrue(model.insertText("x"))
        assertTrue(model.nextSlot())
        assertEquals("lower", active(model))
        assertTrue(model.insertText("1"))
        assertEquals("{x}_{1}", latex(model))
    }

    @Test fun deletingEmptyOptionalScriptsPreservesTheBaseAndOtherScript() {
        val model = model("x_1^2")
        enter(model, "lower", 1)
        model.deleteBackward()
        assertEquals("{x}_{}^{2}", latex(model))
        model.deleteBackward()
        assertEquals("{x}^{2}", latex(model))
        assertEquals("upper", active(model))
        model.deleteBackward()
        assertEquals("{x}^{}", latex(model))
        model.deleteBackward()
        assertEquals("x", latex(model))
        assertEquals(MathCursor(offset = 1), model.cursor)
        model.deleteBackward()
        assertEquals("", latex(model))
    }

    @Test fun removingTheUpperScriptLeavesALowerScriptEditable() {
        val model = model("x_1^2")
        enter(model, "upper", 1)
        model.deleteBackward()
        model.deleteBackward()
        assertEquals("{x}_{1}", latex(model))
        assertEquals("base", active(model))
        assertTrue(model.nextSlot())
        assertEquals("lower", active(model))
        assertTrue(model.insertText("3"))
        assertEquals("{x}_{31}", latex(model))
        assertTrue(model.addScript(upper = true))
        assertTrue(model.insertText("4"))
        assertEquals("{x}_{31}^{4}", latex(model))
    }

    @Test fun clearingAFractionDenominatorUnwrapsItsNumeratorOnTheNextBackspace() {
        val model = model("\\frac{x+1}{2}")
        enter(model, "denominator", 1)
        model.deleteBackward()
        assertEquals("\\frac{x+1}{}", latex(model))
        model.deleteBackward()
        assertEquals("x+1", latex(model))
        assertEquals(MathCursor(offset = 3), model.cursor)
        repeat(3) { model.deleteBackward() }
        assertEquals("", latex(model))
    }

    @Test fun deletingARootIndexRetainsTheRadicandThenAllowsUnwrapping() {
        val model = model("\\sqrt[3]{x+1}")
        enter(model, "index", 1)
        model.deleteBackward()
        model.deleteBackward()
        assertEquals("\\sqrt{x+1}", latex(model))
        assertEquals("radicand", active(model))
        model.deleteBackward()
        assertEquals("x+1", latex(model))
    }

    @Test fun deletingALogarithmBaseKeepsItsArgumentAndNoInaccessibleSlot() {
        val model = model("\\log_2{x+1}")
        enter(model, "base", 1)
        model.deleteBackward()
        model.deleteBackward()
        assertEquals("\\log{x+1}", latex(model))
        assertEquals("argument", active(model))
        model.deleteBackward()
        assertEquals("x+1", latex(model))
    }

    @Test fun matricesAndCasesVisitEveryCellBeforeExiting() {
        listOf(
            "\\begin{matrix}1&2\\\\3&4\\end{matrix}" to listOf("r0c0", "r0c1", "r1c0", "r1c1"),
            "\\begin{cases}x&x>0\\\\0&x=0\\end{cases}" to listOf("expression0", "condition0", "expression1", "condition1")
        ).forEach { (source, slots) ->
            val model = model(source)
            model.cursor = MathCursor()
            slots.forEach { slot ->
                assertTrue(model.nextSlot())
                assertEquals(slot, active(model))
            }
            assertTrue(model.nextSlot())
            assertTrue(model.cursor.path.isEmpty())
            assertFalse(model.nextSlot())
            assertEquals(MathLatexSerializer.serialize(parse(source)), latex(model))
        }
    }

    @Test fun everySupportedStructureCanBeDeletedCompletelyWithBackspace() {
        val sources = listOf(
            "\\frac{x+1}{12}", "x_1^2", "\\sqrt[3]{x+1}", "\\log_2{x+1}",
            "\\left(x+1\\right)", "\\left|x+1\\right|", "\\sin{x}",
            "\\sum_{i=1}^{3}{i}", "\\int_0^1{x}", "\\prod_{i=1}^{3}{i}",
            "\\lim_{x\\to0}{x}", "\\frac{\\mathrm{d}^{2}{x}}{\\mathrm{d}{x}^{2}}",
            "\\begin{matrix}1&2\\\\3&4\\end{matrix}",
            "\\begin{cases}x&x>0\\\\0&x=0\\end{cases}"
        )
        sources.forEach { source ->
            val model = model(source)
            repeat(200) { if (model.expression.children.isNotEmpty()) model.deleteBackward() }
            assertEquals("Structure must remain removable: $source", MathExpression.Sequence(), model.expression)
            assertEquals(MathCursor(), model.cursor)
        }
    }

    @Test fun aDraftRestoresTheExactActiveCellAndDoesNotOverwriteItsSibling() {
        val original = model("\\begin{matrix}1&2\\\\3&4\\end{matrix}")
        enter(original, "r1c0", 1)
        val restored = MathExpressionModel()
        assertTrue(restored.restoreDraft(original.saveDraft()))
        assertEquals(original.cursor, restored.cursor)
        restored.deleteBackward()
        assertTrue(restored.insertText("7"))
        assertTrue(restored.nextSlot())
        assertTrue(restored.insertText("8"))
        assertEquals("\\begin{matrix}1&2\\\\7&84\\end{matrix}", latex(restored))
    }

    @Test fun anObsoleteDraftSlotFallsBackToItsNearestValidParentPosition() {
        val original = model("\\frac{x}{x^2}")
        val draft = JSONObject(original.saveDraft())
        draft.getJSONObject("cursor").put("path", JSONArray()
            .put(JSONObject().put("node", 0).put("slot", "denominator"))
            .put(JSONObject().put("node", 0).put("slot", "lower")))
        val restored = MathExpressionModel()
        assertTrue(restored.restoreDraft(draft.toString()))
        assertEquals(MathCursor(listOf(MathCursorStep(0, "denominator")), 1), restored.cursor)
        assertTrue(restored.insertText("+1"))
        assertEquals("\\frac{x}{{x}^{2}+1}", latex(restored))
    }

    @Test fun rejectedScriptInsertionIsAtomicIncludingItsSelectionAndCursor() {
        var nested = MathExpression.Sequence(listOf(MathExpression.Variable("x")))
        repeat(60) { nested = MathExpression.Sequence(listOf(MathExpression.Group(nested))) }
        val model = MathExpressionModel(nested)
        model.cursor = MathCursor(List(60) { MathCursorStep(0, "content") }, 1)
        val before = model.saveDraft()
        assertFalse(model.addScript(upper = true))
        assertEquals(before, model.saveDraft())
    }
}
