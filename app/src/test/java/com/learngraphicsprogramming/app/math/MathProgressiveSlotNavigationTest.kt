package com.learngraphicsprogramming.app.math

import org.junit.Assert.*
import org.junit.Test
import org.json.JSONArray
import org.json.JSONObject
import kotlin.random.Random

class MathProgressiveSlotNavigationTest {
    private fun parse(s: String) = (MathLatexParser.parse(s) as MathLatexParser.Result.Success).expression
    private fun model(s: String) = MathExpressionModel(parse(s))
    private fun path(vararg slots: String) = slots.map { MathCursorStep(0, it) }
    private data class State(val cursor: MathCursor, val selection: MathSelection?, val presentation: MathCursorPresentation)
    private fun state(m: MathExpressionModel) = State(m.cursor, m.selectionRange, m.cursorPresentation)
    private fun valid(m: MathExpressionModel) {
        val seq = m.sequenceAt(m.cursor.path)
        assertNotNull(seq)
        assertTrue(m.cursor.offset in 0..seq!!.children.size)
        (m.editingScope as? MathContentScope.Atom)?.let { assertTrue(it.start in 0..it.end && it.end <= seq.children.size) }
        m.selectionRange?.let { assertTrue(it.start in 0 until it.end && it.end <= m.sequenceAt(it.path)!!.children.size) }
        (m.cursorPresentation as? MathCursorPresentation.ChildBoundary)?.let { assertTrue(it.after) }
    }
    private fun step(m: MathExpressionModel, right: Boolean, expected: Boolean = true) {
        val tree = m.expression
        val draft = m.saveDraft()
        repeat(3) { assertEquals(expected, if (right) m.canNavigateNext else m.canNavigatePrevious); assertEquals(draft, m.saveDraft()) }
        assertEquals(expected, if (right) m.stepRight() else m.stepLeft())
        assertSame(tree, m.expression)
        valid(m)
    }
    private fun frontier(m: MathExpressionModel, p: List<MathCursorStep>) {
        m.cursor = MathCursor(p, m.sequenceAt(p)!!.children.size)
        step(m, true)
        assertEquals(MathCursorPresentation.ChildBoundary(p), m.cursorPresentation)
        assertEquals(p.dropLast(1), m.cursor.path)
        assertEquals(p.last().nodeIndex + 1, m.cursor.offset)
    }
    private fun walkLeft(m: MathExpressionModel): List<State> {
        m.cursor = MathCursor(offset=m.expression.children.size)
        val states = mutableListOf(state(m))
        var count = 0
        while (m.canNavigatePrevious && count++ < 2000) { step(m, false); states += state(m) }
        assertFalse("Traversal must terminate at HOME", m.canNavigatePrevious)
        assertEquals(MathCursor(),m.cursor)
        assertEquals(MathCursorPresentation.Neutral,m.cursorPresentation)
        step(m, false, false)
        return states
    }
    private fun unchangedSiblings(before: MathExpressionModel, after: MathExpressionModel, slotPath: List<MathCursorStep>) {
        slotPath.forEachIndexed { depth, edge ->
            val parentPath = slotPath.take(depth)
            val oldParent = before.sequenceAt(parentPath)!!
            val newParent = after.sequenceAt(parentPath)!!
            assertEquals(oldParent.children.take(edge.nodeIndex), newParent.children.take(edge.nodeIndex))
            val suffix = oldParent.children.drop(edge.nodeIndex + 1)
            assertEquals(suffix, newParent.children.takeLast(suffix.size))
            val oldOwner = oldParent.children[edge.nodeIndex]
            val replacement = newParent.children.drop(edge.nodeIndex).dropLast(suffix.size)
            val oldChildren = oldOwner.navigationChildren().associateBy { it.id }
            if (depth < slotPath.lastIndex) {
                assertEquals(1, replacement.size)
                assertEquals(oldOwner.javaClass, replacement.single().javaClass)
                val newChildren = replacement.single().navigationChildren().associateBy { it.id }
                oldChildren.filterKeys { it != edge.slot }.forEach { (id, child) -> assertEquals(child.content, newChildren[id]?.content) }
            } else {
                val selected = oldChildren.getValue(edge.slot)
                if (selected.requirement == MathSlotRequirement.REQUIRED) {
                    assertEquals(1, replacement.size)
                    assertEquals(oldOwner.javaClass, replacement.single().javaClass)
                    val newChildren = replacement.single().navigationChildren().associateBy { it.id }
                    assertEquals(MathExpression.Sequence(), newChildren.getValue(edge.slot).content)
                    oldChildren.filterKeys { it != edge.slot }.forEach { (id, child) -> assertEquals(child.content, newChildren[id]?.content) }
                } else if (replacement.size == 1 && replacement.single().javaClass == oldOwner.javaClass) {
                    val newChildren = replacement.single().navigationChildren().associateBy { it.id }
                    assertFalse(newChildren.containsKey(edge.slot))
                    oldChildren.filterKeys { it != edge.slot }.forEach { (id, child) -> assertEquals(child.content, newChildren[id]?.content) }
                } else {
                    // Removing the final optional script unwraps its unchanged required base.
                    assertTrue(oldOwner is MathExpression.Scripts)
                    assertEquals((oldOwner as MathExpression.Scripts).base.children, replacement)
                }
            }
        }
    }

    @Test fun leftFollowsTheExactParentThenExponentDiagramWithoutFinish() {
        val m=model("x^2")
        assertEquals(MathCursorPresentation.ParentBoundary(emptyList(),0),m.cursorPresentation)
        val interior=MathCursorPresentation.Interior(MathContentScope.Slot)
        val expected=listOf(
            State(MathCursor(path("base"),1),null,interior),
            State(MathCursor(path("base"),1),MathSelection(path("base"),0,1),interior),
            State(MathCursor(path("base"),0),null,interior),
            State(MathCursor(offset=1,level=MathCursorLevel.CHILD_BOUNDARY,scope=null),null,MathCursorPresentation.ChildBoundary(path("upper"))),
            State(MathCursor(path("upper"),1),null,interior),
            State(MathCursor(path("upper"),1),MathSelection(path("upper"),0,1),interior),
            State(MathCursor(path("upper"),0),null,interior),
            State(MathCursor(),null,MathCursorPresentation.Neutral))
        expected.forEach { step(m,false);assertEquals(it,state(m)) }
        step(m,false,false);step(m,true)
        assertEquals(MathCursorPresentation.ParentBoundary(emptyList(),0),m.cursorPresentation)
        step(m,true,false)
    }
    @Test fun rightFromHomeVisitsOnlyTheNearestParentThenTheNextParent() {
        val integral=MathExpression.LargeOperator(MathLargeOperator.INTEGRAL,MathExpression.Sequence(),parse("0"),parse("1"))
        val m=MathExpressionModel(MathExpression.Sequence(listOf(integral,parse("x^2").children.single())))
        m.cursor=MathCursor()
        step(m,true);assertEquals(MathCursorPresentation.ParentBoundary(emptyList(),0),m.cursorPresentation)
        assertEquals(MathCursor(offset=1),m.cursor)
        step(m,true);assertEquals(MathCursorPresentation.ParentBoundary(emptyList(),1),m.cursorPresentation)
        assertEquals(MathCursor(offset=2),m.cursor)
        val last=m.saveDraft();repeat(5){step(m,true,false);assertEquals(last,m.saveDraft())}
    }
    @Test fun rightFromAnyInteriorPhaseExitsItsParentOrChildWithoutAReverseCycle() {
        listOf("base","upper","lower").forEach { slot -> repeat(3) { phase ->
            val m=model("x_{12}^{23}");val p=path(slot);val end=m.sequenceAt(p)!!.children.size
            m.cursor=MathCursor(p,if(phase==0)0 else end)
            if(phase==2)step(m,false)
            val tree=m.expression;step(m,true);assertSame(tree,m.expression);assertNull(m.selectionRange)
            assertEquals(if(slot=="base")MathCursorPresentation.ParentBoundary(emptyList(),0)
                else MathCursorPresentation.ChildBoundary(p),m.cursorPresentation)
            if(slot!="base"){step(m,true);assertEquals(MathCursorPresentation.ParentBoundary(emptyList(),0),m.cursorPresentation)}
        } }
        val middle=model("x_{12}");middle.placeTextCursor(MathCursor(path("lower"),0),1)
        step(middle,true);assertEquals(MathCursorPresentation.ChildBoundary(path("lower")),middle.cursorPresentation)
    }
    @Test fun emptyRootHasOnlyHomeEvenAfterSelectAllTouchAndNativeEcho() {
        val m=MathExpressionModel();val home=m.saveDraft()
        m.selectAll();assertNull(m.selectionRange);assertEquals(home,m.saveDraft())
        assertTrue(m.placeTextCursor(MathCursor(),0));assertEquals(home,m.saveDraft())
        assertTrue(m.setTextCursorInCurrentSlot(0));assertEquals(home,m.saveDraft())
        step(m,false,false);step(m,true,false)
    }
    @Test fun rightClearsAGenuineNeutralSelectionAtItsCurrentParentBoundary() {
        val m=model("x^2+y");m.selectAll();val expression=m.expression
        assertNotNull(m.selectionRange);step(m,true);assertSame(expression,m.expression)
        assertNull(m.selectionRange);assertEquals(MathCursor(offset=3),m.cursor)
        assertEquals(MathCursorPresentation.ParentBoundary(emptyList(),2),m.cursorPresentation)
        step(m,true,false)
        m.cursor=MathCursor(offset=1);m.select(emptyList(),0,1);step(m,true)
        assertEquals(MathCursor(offset=1),m.cursor);assertNull(m.selectionRange)
    }
    @Test fun descriptorsDeclareParentContentIndependentlyFromLegacySlotOrder() {
        val m=model("x_1^2")
        assertEquals(listOf("base","upper","lower"),m.expression.children.single().navigationChildren().map { it.id })
        assertEquals(listOf(MathNavigationRole.PARENT_CONTENT,MathNavigationRole.CHILD,MathNavigationRole.CHILD),
            m.expression.children.single().navigationChildren().map { it.role })
        val states=walkLeft(m)
        assertEquals(path("base"),states[1].cursor.path)
        assertEquals(listOf("upper","lower"),states.mapNotNull {
            (it.presentation as? MathCursorPresentation.ChildBoundary)?.slotPath?.last()?.slot })
    }
    @Test fun everyDescriptorTypeOpensItsDeclaredParentThenChildrenAndTerminatesAtHome() {
        val v=parse("12")
        val nodes=listOf<MathExpression>(MathExpression.Fraction(v,v),MathExpression.Root(v,v),MathExpression.Group(v),
            MathExpression.Function("log",v,v),MathExpression.Limit(v,v),MathExpression.Derivative(v,v,v),
            MathExpression.Matrix(List(2){List(2){v}}),MathExpression.Cases(listOf(MathExpression.Case(v,v))))+
            MathLargeOperator.entries.map{MathExpression.LargeOperator(it,v,v,v)}
        nodes.forEach { node ->
            val children=node.navigationChildren();val parent=children.firstOrNull{it.role==MathNavigationRole.PARENT_CONTENT}
            val states=walkLeft(MathExpressionModel(MathExpression.Sequence(listOf(node))))
            if(parent!=null)assertEquals(path(parent.id),states[1].cursor.path)
            else assertEquals(MathCursorPresentation.ChildBoundary(path(children.first().id)),states[1].presentation)
            assertEquals(children.filter{it.role==MathNavigationRole.CHILD}.map{it.id},states.mapNotNull{
                (it.presentation as? MathCursorPresentation.ChildBoundary)?.slotPath?.last()?.slot})
        }
    }
    @Test fun structuralSlotsOpenTheirLocalParentsBeforeContinuingOuterChildren() {
        val states=walkLeft(model("\\frac{x+\\sqrt[3]{12}}{34}"))
        assertTrue(states.any{it.cursor.path==path("numerator")&&it.cursor.level==MathCursorLevel.NEUTRAL})
        assertTrue(states.any{(it.presentation as? MathCursorPresentation.ChildBoundary)?.slotPath==
            listOf(MathCursorStep(0,"numerator"),MathCursorStep(2,"index"))})
        val denominator=states.indexOfFirst{(it.presentation as? MathCursorPresentation.ChildBoundary)?.slotPath==path("denominator")}
        val index=states.indexOfFirst{(it.presentation as? MathCursorPresentation.ChildBoundary)?.slotPath==
            listOf(MathCursorStep(0,"numerator"),MathCursorStep(2,"index"))}
        assertTrue(index in 0 until denominator)
    }
    @Test fun integralAndPowerRemainIndependentParentsWhileLeftOpensEachBodyFirst() {
        val integral=MathExpression.LargeOperator(MathLargeOperator.INTEGRAL,MathExpression.Sequence(),parse("0"),parse("1"))
        val m=MathExpressionModel(MathExpression.Sequence(listOf(integral,parse("x^2").children.single())))
        val states=walkLeft(m)
        assertEquals(listOf(MathCursorStep(1,"base")),states[1].cursor.path)
        val body=states.indexOfFirst{it.cursor.path==listOf(MathCursorStep(0,"body"))}
        val lower=states.indexOfFirst{(it.presentation as? MathCursorPresentation.ChildBoundary)?.slotPath==listOf(MathCursorStep(0,"lower"))}
        assertTrue(body>0&&body<lower)
        m.cursor=MathCursor(offset=2);m.deleteBackward();assertEquals(MathExpression.Sequence(listOf(integral)),m.expression)
        m.deleteBackward();assertEquals(MathExpression.Sequence(),m.expression)
    }
    @Test fun emptyChildrenHaveOneInteriorStateAndNoEmptySelectionOrLeftBoundary() {
        val m=model("x^{}");frontier(m,path("upper"));step(m,false)
        assertEquals(MathCursor(path("upper"),0),m.cursor);assertNull(m.selectionRange)
        step(m,false);assertEquals(MathCursor(),m.cursor)
        m.cursor=MathCursor(path("upper"),0);step(m,true)
        assertEquals(MathCursorPresentation.ChildBoundary(path("upper")),m.cursorPresentation)
        walkLeft(model("\\frac{}{34}"))
        walkLeft(MathExpressionModel(MathExpression.Sequence(listOf(MathExpression.Matrix(List(2){List(2){MathExpression.Sequence()}})))))
    }
    @Test fun backspaceDistinguishesInteriorChildBoundaryAndWholeParentBoundary() {
        val m=model("x^2");m.cursor=MathCursor(path("upper"),0)
        val unchanged=m.saveDraft();assertTrue(m.deleteBackwardAtCurrentLevel());assertEquals(unchanged,m.saveDraft())
        frontier(m,path("upper"));m.deleteBackward();assertEquals(parse("x"),m.expression)
        val parent=model("x^2");parent.deleteBackward();assertEquals(MathExpression.Sequence(),parent.expression)
        assertEquals(MathCursor(),parent.cursor)
    }
    @Test fun rightChildBoundaryRemovesOnlyTheChosenOptionalChild() {
        listOf("lower" to "x^2","upper" to "x_1").forEach { (slot,keep) ->
            val m=model("x_1^2");frontier(m,path(slot));assertTrue(m.deleteBackwardAtCurrentLevel())
            assertEquals(parse(keep),m.expression);assertEquals(MathCursorLevel.NEUTRAL,m.navigationLevel)
        }
    }
    @Test fun legacyLeftBoundariesAndSymmetricOriginsCannotResurrectNavigationStates() {
        val m=model("x^2");frontier(m,path("upper"))
        val draft=JSONObject(m.saveDraft());draft.getJSONObject("navigationBoundary").put("after",false)
        draft.getJSONObject("cursor").put("offset",0)
        draft.put("navigationOrigins",JSONArray().put(JSONObject().put("edge",draft.getJSONObject("cursor"))))
        val restored=MathExpressionModel();assertTrue(restored.restoreDraft(draft.toString()))
        assertEquals(m.expression,restored.expression);assertEquals(MathCursor(),restored.cursor)
        assertFalse(restored.saveDraft().contains("navigationOrigins"))
        step(restored,false,false);step(restored,true)
        assertEquals(MathCursorPresentation.ParentBoundary(emptyList(),0),restored.cursorPresentation)
    }
    @Test fun legacyParentContentChildBoundariesNormalizeToWholeParentBoundaries() {
        val m=model("x^2");frontier(m,path("upper"))
        val draft=JSONObject(m.saveDraft());draft.getJSONObject("navigationBoundary").getJSONArray("path").getJSONObject(0).put("slot","base")
        val restored=MathExpressionModel();assertTrue(restored.restoreDraft(draft.toString()))
        assertEquals(MathCursor(offset=1),restored.cursor)
        assertEquals(MathCursorPresentation.ParentBoundary(emptyList(),0),restored.cursorPresentation)
        step(restored,false);assertEquals(MathCursor(path("base"),1),restored.cursor)
    }
    @Test fun orphanedChildBoundaryCursorsRestoreAsWholeParentBoundaries() {
        val m=model("x^2");frontier(m,path("upper"))
        val draft=JSONObject(m.saveDraft()).apply{remove("navigationBoundary")}
        val restored=MathExpressionModel();assertTrue(restored.restoreDraft(draft.toString()))
        assertEquals(MathCursor(offset=1),restored.cursor);assertFalse(restored.isAtNavigationBoundary)
        assertEquals(MathCursorPresentation.ParentBoundary(emptyList(),0),restored.cursorPresentation)
    }
    @Test fun requiredFrontierClearsSlotAndRetainsOwnerAndPlaceholder() {
        val m=model("a+\\frac{12}{34}+b"); val p=listOf(MathCursorStep(2,"numerator"))
        frontier(m,p); m.deleteBackward(); assertEquals(parse("a+\\frac{}{34}+b"),m.expression)
        assertEquals(MathCursorPresentation.ChildBoundary(p,true),m.cursorPresentation)
        val empty=m.saveDraft(); repeat(5) { m.deleteBackward() }; assertEquals(empty,m.saveDraft())
    }
    @Test fun descriptorRemovalPreservesParentSiblingsGenerically() {
        val v=parse("12")
        val nodes=listOf<MathExpression>(MathExpression.Scripts(v,v,v),MathExpression.Root(v,v),MathExpression.Fraction(v,v),
            MathExpression.Function("log",v,v),MathExpression.Derivative(v,v,v),MathExpression.Matrix(List(2){List(2){v}})) +
            MathLargeOperator.entries.map { MathExpression.LargeOperator(it,v,v,v) }
        nodes.forEach { node -> node.navigationChildren().filter{it.role==MathNavigationRole.CHILD}.forEach { child ->
            val m=MathExpressionModel(MathExpression.Sequence(listOf(MathExpression.Variable("a"),node,MathExpression.Variable("b"))))
            frontier(m,listOf(MathCursorStep(1,child.id))); m.deleteBackward()
            assertEquals(MathExpression.Sequence(listOf(MathExpression.Variable("a"))+child.remove()+MathExpression.Variable("b")),m.expression); valid(m)
        } }
    }
    @Test fun insideEmptySlotBackspaceNeverRemovesItsOwner() {
        val m=model("x_1^2"); m.cursor=MathCursor(path("lower"),1); m.deleteBackward()
        assertEquals(parse("x_{}^2"),m.expression)
        val empty=m.saveDraft(); repeat(10){m.deleteBackward()}; assertEquals(empty,m.saveDraft())
        frontier(m,path("lower"));m.deleteBackward();assertEquals(parse("x^2"),m.expression)
    }
    @Test fun contentAndSelectionBackspaceRemainWithinSlot() {
        val m=model("x_{12}^{23}");m.cursor=MathCursor(path("upper"),1);m.deleteBackward();assertEquals(parse("x_{12}^{2}"),m.expression)
        m.cursor=MathCursor(path("lower"),1);step(m,false);m.deleteBackward();assertEquals(parse("x_{}^{2}"),m.expression)
    }
    @Test fun navigationSelectionSupportsTypingAndFractionWrapping() {
        val m=model("x_{12}");m.cursor=MathCursor(path("lower"),1);step(m,false);assertTrue(m.insertText("3"));assertEquals(parse("x_3"),m.expression)
        m.cursor=MathCursor(path("lower"),1);step(m,false);val selected=m.takeSelection()
        assertTrue(m.insert(MathExpression.Fraction(selected,MathExpression.Sequence()),"denominator"));assertEquals(path("lower","denominator"),m.cursor.path)
    }
    @Test fun manualInteriorLeftMovesToStartWhileRightImmediatelyEscapes() {
        listOf(true,false).forEach { right -> val m=model("x_{12}");m.placeTextCursor(MathCursor(path("lower"),0),1)
            step(m,right);assertNull(m.selectionRange)
            if(right)assertEquals(MathCursorPresentation.ChildBoundary(path("lower")),m.cursorPresentation)
            else {assertEquals(MathCursor(path("lower"),0),m.cursor);step(m,false);assertEquals(MathCursor(),m.cursor)}
        }
    }
    @Test fun rootNumericSplitKeepsBothHalvesAndRightExitsTheOwnedSpan() {
        val m=model("1234+x^2");m.placeTextCursor(MathCursor(),2);assertEquals(MathContentScope.Atom(0,2),m.editingScope)
        val tree=m.expression;step(m,true);assertEquals(MathCursorPresentation.ParentBoundary(emptyList(),1),m.cursorPresentation)
        assertSame(tree,m.expression);assertEquals("1234+{x}^{2}",MathLatexSerializer.serialize(m.expression))
        step(m,false);assertEquals(MathContentScope.Atom(1,2),m.editingScope)
        assertEquals(2,m.cursor.offset);assertNull(m.selectionRange)
    }
    @Test fun rootTypingImmediatelyScopesOnlyTheLastIndependentAtom() {
        val m=model("12+x^2");assertTrue(m.insertText("abc"));assertEquals(MathContentScope.Atom(5,6),m.editingScope)
        step(m,false);assertEquals(MathSelection(emptyList(),5,6),m.selectionRange)
        m.deleteBackward();assertEquals(parse("12+x^2ab"),m.expression)
    }
    @Test fun partialSelectionLeftCollapsesToStartAndRightEscapesTheChild() {
        val m=model("\\frac{x+y}{3}");m.select(path("numerator"),1,2);step(m,false)
        assertEquals(1,m.cursor.offset);assertNull(m.selectionRange)
        m.select(path("numerator"),1,2);step(m,true)
        assertEquals(MathCursorPresentation.ChildBoundary(path("numerator")),m.cursorPresentation);assertNull(m.selectionRange)
    }
    @Test fun newlyCreatedScriptsEnterTheirOwnChildEvenWhenBaseIsEmpty() {
        listOf(true,false).forEach { upper -> val m=MathExpressionModel();assertTrue(m.addScript(upper))
            assertEquals(path(if(upper)"upper" else "lower"),m.cursor.path);assertEquals(MathCursorLevel.INTERIOR,m.navigationLevel)
        }
    }
    @Test fun scriptInsideExponentDoesNotReuseAncestorButDirectBaseCanAddSibling() {
        val m=model("x^2");m.cursor=MathCursor(path("upper"),1);assertTrue(m.addScript(false));assertEquals(path("upper","lower"),m.cursor.path)
        val base=model("x^2");base.cursor=MathCursor(path("base"),1);assertTrue(base.addScript(false));assertEquals(path("lower"),base.cursor.path)
    }
    @Test fun directAndNestedInsertionEnterTheFirstDeclaredChild() {
        val m=model("x^2");m.cursor=MathCursor(path("upper"),1);assertTrue(m.insert(MathExpression.Root(MathExpression.Sequence(),MathExpression.Sequence())))
        assertEquals(listOf(MathCursorStep(0,"upper"),MathCursorStep(1,"index")),m.cursor.path)
        assertTrue(m.insert(MathExpression.Fraction(MathExpression.Sequence(),MathExpression.Sequence())))
        assertEquals(listOf(MathCursorStep(0,"upper"),MathCursorStep(1,"index"),MathCursorStep(0,"numerator")),m.cursor.path)
    }
    @Test fun unsupportedPasteIsAtomicAndLatexBatchKeepsItsImportBoundary() {
        val m=model("x^2");val before=m.saveDraft();assertFalse(m.insertText("\\unsupported{y}"));assertEquals(before,m.saveDraft())
        assertTrue(m.insertLatex("\\frac{1}{2}") is MathLatexParser.Result.Success);assertEquals(MathCursor(offset=2),m.cursor)
    }
    @Test fun draftsPreserveTypedCursorSelectionAndChildIntentWhileImeEchoIsPure() {
        val m=model("1234+x^2");m.placeTextCursor(MathCursor(),2)
        val echo=m.saveDraft();assertTrue(m.setTextCursorInCurrentSlot(2));assertEquals(echo,m.saveDraft())
        val restored=MathExpressionModel();assertTrue(restored.restoreDraft(m.saveDraft()));assertEquals(state(m),state(restored))
        step(m,true);assertTrue(restored.restoreDraft(m.saveDraft()));assertEquals(state(m),state(restored))
        assertFalse(restored.saveDraft().contains("navigationOrigins"))
        val boundary=model("x_1^2");frontier(boundary,path("lower"));assertTrue(restored.restoreDraft(boundary.saveDraft()))
        assertEquals(state(boundary),state(restored));restored.deleteBackward();assertEquals(parse("x^2"),restored.expression)
    }
    @Test fun touchAndMutationInvalidateOriginsAndEmptySlotsHaveNoSelection() {
        val m=model("x_{12}");m.placeTextCursor(MathCursor(path("lower"),0),1);step(m,true)
        m.placeTextCursor(MathCursor(path("lower"),0),0);assertFalse(m.saveDraft().contains("navigationOrigins"))
        assertTrue(m.insertText("3"));assertFalse(m.saveDraft().contains("navigationOrigins"));valid(m)
        walkLeft(MathExpressionModel(MathExpression.Sequence(listOf(MathExpression.Matrix(List(2){List(2){MathExpression.Sequence()}})))))
    }
    @Test fun atomicNeutralGroupsCrossOneStepWithoutInterpretingMathematics() {
        val m=model("+12=x");m.cursor=MathCursor();step(m,true);assertEquals(MathCursor(offset=1),m.cursor)
        m.deleteBackward();assertEquals(parse("12=x"),m.expression)
    }
    @Test fun neutralNumericTypingCreatesANewSiblingAndPowerUsesOnlyThatSibling() {
        val m=model("12");assertTrue(m.insertText("3"))
        assertEquals(listOf(MathExpression.Number("12"),MathExpression.Number("3")),m.expression.children)
        assertEquals(MathContentScope.Atom(1,2),m.editingScope)
        assertTrue(m.addScript(true))
        assertEquals(MathExpression.Number("12"),m.expression.children.first())
        assertEquals(parse("3"),(m.expression.children.last() as MathExpression.Scripts).base)
    }
    @Test fun partialRootAtomEditsKeepTheSuffixAndAnEmptyAtomCanReceiveANewWord() {
        val m=model("12");m.placeTextCursor(MathCursor(),0)
        assertTrue(m.replaceTextRangeInCurrentSlot(0,1,"3"))
        assertEquals("32",MathLatexSerializer.serialize(m.expression))
        assertEquals(MathContentScope.Atom(0,2),m.editingScope)
        assertTrue(m.replaceTextRangeInCurrentSlot(0,2,""))
        assertNull(m.editingScope);assertEquals(MathCursor(),m.cursor)
        assertTrue(m.insertText("abc"));assertEquals(MathContentScope.Atom(2,3),m.editingScope);assertEquals(3,m.cursor.offset)
    }
    @Test fun typingAtAnEmptyAtomDoesNotMergeIntoItsPreviousSibling() {
        val m=model("12+3");m.placeTextCursor(MathCursor(offset=2),0)
        assertTrue(m.replaceTextRangeInCurrentSlot(3,4,""))
        assertTrue(m.insertText("4"))
        assertEquals(parse("12+4"),m.expression)
    }
    @Test fun scriptsAtTheBeginningOrEmptyInteriorAtomKeepThePreviousSibling() {
        val empty=model("12");assertTrue(empty.insertText("3"))
        assertTrue(empty.replaceTextRangeInCurrentSlot(2,3,""))
        assertEquals(MathContentScope.Atom(1,1),empty.editingScope)
        assertTrue(empty.addScript(true))
        assertEquals(MathExpression.Number("12"),empty.expression.children.first())
        val emptyScript=empty.expression.children[1] as MathExpression.Scripts
        assertEquals(MathExpression.Sequence(),emptyScript.base)
        assertEquals(MathExpression.Sequence(),emptyScript.upper)
        assertEquals(listOf(MathCursorStep(1,"upper")),empty.cursor.path)

        val beginning=model("x^2+y");assertTrue(beginning.placeTextCursor(MathCursor(offset=2),0))
        val original=beginning.expression.children
        assertTrue(beginning.addScript(false))
        assertEquals(original.take(2),beginning.expression.children.take(2))
        assertEquals(original.last(),beginning.expression.children.last())
        val script=beginning.expression.children[2] as MathExpression.Scripts
        assertEquals(MathExpression.Sequence(),script.base)
        assertEquals(MathExpression.Sequence(),script.lower)
        assertEquals(listOf(MathCursorStep(2,"lower")),beginning.cursor.path)
    }
    @Test fun whitespaceTargetsStayNeutralWhileEmptySlotsRemainInterior() {
        val m=model("x^2");assertTrue(m.placeTextCursor(MathCursor(offset=1)))
        assertEquals(MathCursorLevel.NEUTRAL,m.navigationLevel);m.deleteBackward();assertEquals(MathExpression.Sequence(),m.expression)
        val empty=model("x^{}");assertTrue(empty.placeTextCursor(MathCursor(path("upper"),0)))
        assertEquals(MathCursorLevel.INTERIOR,empty.navigationLevel)
    }
    @Test fun repeatedDirectionalNavigationKeepsNoObsoleteInverseHistory() {
        val m=model("12");val tree=m.expression
        repeat(50){m.cursor=MathCursor();step(m,true);step(m,true,false);step(m,false);step(m,true)}
        assertFalse(m.saveDraft().contains("navigationOrigins"))
        assertSame(tree,m.expression);assertEquals(MathCursor(offset=1),m.cursor)
    }
    @Test fun generatedTreesPreserveAstTerminateAtHomeAndKeepSiblingsOnChildDeletion() {
        val random=Random(0x51A7)
        fun tree(depth:Int):MathExpression.Sequence {
            fun child()=tree(depth-1)
            if(depth==0)return if(random.nextBoolean())MathExpression.Sequence() else parse((random.nextInt(9)+1).toString())
            val node:MathExpression=when(random.nextInt(5)){
                0->MathExpression.Scripts(child(),if(random.nextBoolean())child() else null,child())
                1->MathExpression.Fraction(child(),child())
                2->MathExpression.Root(child(),if(random.nextBoolean())child() else null)
                3->MathExpression.Matrix(List(2){List(2){child()}})
                else->MathExpression.Group(child())
            }
            return MathExpression.Sequence(listOf(node,MathExpression.Operator("+"),MathExpression.Number("12")))
        }
        repeat(24){
            val expression=tree(2);val candidateWalk=MathExpressionModel(expression)
            val states=walkLeft(candidateWalk);assertSame(expression,candidateWalk.expression)
            expression.children.indices.forEach{index->step(candidateWalk,true);assertEquals(MathCursor(offset=index+1),candidateWalk.cursor)}
            step(candidateWalk,true,false)
            states.mapNotNull{it.presentation as? MathCursorPresentation.ChildBoundary}.distinct().forEach{anchor->
                assertTrue(anchor.after)
                val candidate=MathExpressionModel(expression);frontier(candidate,anchor.slotPath)
                assertTrue(candidate.deleteBackwardAtCurrentLevel());valid(candidate)
                unchangedSiblings(MathExpressionModel(expression), candidate, anchor.slotPath)
                val restored=MathExpressionModel();assertTrue(restored.restoreDraft(candidate.saveDraft()));assertEquals(candidate.expression,restored.expression);valid(restored)
            }
        }
    }
}
