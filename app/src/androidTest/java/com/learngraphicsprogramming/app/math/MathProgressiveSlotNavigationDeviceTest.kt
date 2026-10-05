package com.learngraphicsprogramming.app.math

import android.graphics.Color
import android.os.SystemClock
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputConnection
import android.widget.ImageButton
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

/** Exercises the editor and native toolbar against the model's real mathematical selection. */
@RunWith(AndroidJUnit4::class)
class MathProgressiveSlotNavigationDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext

    private enum class DeleteTransport { TEXT, CODE_POINTS, KEY_EVENT }

    @Test
    fun fractionToolbarAndNativeArrowsReverseAllLeftSnapshotsAfterEveryDraftRestore() =
        onMain {

            val numerator =
                listOf(
                    MathCursorStep(
                        0,
                        "numerator"
                    )
                )

            val denominator =
                listOf(
                    MathCursorStep(
                        0,
                        "denominator"
                    )
                )

            val interior =
                MathCursorPresentation.Interior(
                    MathContentScope.Slot
                )

            /*
             * Fraction:
             *
             * STRUCTURE
             * ├── PARENT numerator
             * └── PARENT denominator
             *
             * Ninguno necesita ChildBoundary.
             */
            val expected =
                listOf(
                    Triple(
                        MathCursor(),
                        null,
                        MathCursorPresentation.Neutral
                    ),

                    Triple(
                        MathCursor(
                            denominator,
                            0
                        ),
                        null,
                        interior
                    ),

                    Triple(
                        MathCursor(
                            denominator,
                            1
                        ),
                        MathSelection(
                            denominator,
                            0,
                            1
                        ),
                        interior
                    ),

                    Triple(
                        MathCursor(
                            denominator,
                            1
                        ),
                        null,
                        interior
                    ),

                    Triple(
                        MathCursor(
                            numerator,
                            0
                        ),
                        null,
                        interior
                    ),

                    Triple(
                        MathCursor(
                            numerator,
                            3
                        ),
                        MathSelection(
                            numerator,
                            0,
                            3
                        ),
                        interior
                    ),

                    Triple(
                        MathCursor(
                            numerator,
                            3
                        ),
                        null,
                        interior
                    ),

                    Triple(
                        MathCursor(
                            offset = 1
                        ),
                        null,
                        MathCursorPresentation.ParentBoundary(
                            emptyList(),
                            0
                        )
                    )
                )

            for (
            native in listOf(
                false,
                true
            )
            ) {

                val editor =
                    MathExpressionEditor(
                        context
                    )

                assertTrue(
                    editor.setLatex(
                        "\\frac{x+1}{2}"
                    )
                )

                val original =
                    editor.model.expression

                val leftStates =
                    mutableListOf(
                        navigationSnapshot(
                            editor
                        )
                    )

                while (
                    editor.model
                        .canNavigatePrevious
                ) {

                    assertTrue(
                        "LEFT has exactly seven transitions",
                        leftStates.size <
                                expected.size
                    )

                    navigationPress(
                        editor,
                        forward = false,
                        native = native
                    )

                    leftStates +=
                        navigationSnapshot(
                            editor
                        )

                    assertSame(
                        "Structural navigation cannot rebuild the fraction",
                        original,
                        editor.model.expression
                    )
                }

                assertEquals(
                    "LEFT establishes every canonical fraction checkpoint",
                    expected.asReversed(),
                    leftStates
                )

                val rightStates =
                    mutableListOf(
                        navigationSnapshot(
                            editor
                        )
                    )

                while (
                    editor.model
                        .canNavigateNext
                ) {

                    assertTrue(
                        "RIGHT cannot add a shortcut or extra endpoint",
                        rightStates.size <
                                expected.size
                    )

                    navigationPress(
                        editor,
                        forward = true,
                        native = native
                    )

                    rightStates +=
                        navigationSnapshot(
                            editor
                        )

                    assertSame(
                        original,
                        editor.model.expression
                    )
                }

                assertEquals(
                    "RIGHT must be exactly the inverse of LEFT",
                    leftStates.asReversed(),
                    rightStates
                )

                for (
                index in
                expected.indices.reversed()
                ) {

                    assertEquals(
                        expected[index],
                        navigationSnapshot(
                            editor
                        )
                    )

                    val draft =
                        editor.saveDraft()

                    for (
                    forward in
                    listOf(
                        false,
                        true
                    )
                    ) {

                        val restored =
                            MathExpressionEditor(
                                context
                            )

                        restored.restoreDraft(
                            draft
                        )

                        assertEquals(
                            "Restore includes cursor, real selection and presentation",
                            expected[index],
                            navigationSnapshot(
                                restored
                            )
                        )

                        assertEquals(
                            original,
                            restored.model.expression
                        )

                        val restoredAst =
                            restored.model.expression

                        val target =
                            index +
                                    if (forward) {
                                        1
                                    } else {
                                        -1
                                    }

                        assertEquals(
                            "Availability follows the restored checkpoint alone",
                            target in expected.indices,
                            if (forward) {
                                restored.model
                                    .canNavigateNext
                            } else {
                                restored.model
                                    .canNavigatePrevious
                            }
                        )

                        navigationPress(
                            restored,
                            forward,
                            native
                        )

                        assertEquals(
                            "A fresh editor cannot rely on a last edge in memory",
                            expected[
                                target.coerceIn(
                                    expected.indices
                                )
                            ],
                            navigationSnapshot(
                                restored
                            )
                        )

                        assertSame(
                            restoredAst,
                            restored.model.expression
                        )
                    }

                    if (index > 0) {

                        navigationPress(
                            editor,
                            forward = false,
                            native = native
                        )
                    }
                }
            }
        }

    @Test
    fun fractionRightFromHomeVisitsEmptyDenominatorBeforeNumeratorAndParent() =
        onMain {

            for (
            native in listOf(
                false,
                true
            )
            ) {

                val editor =
                    MathExpressionEditor(
                        context
                    )

                assertTrue(
                    editor.setLatex(
                        "\\frac{x+1}{}"
                    )
                )

                val original =
                    editor.model.expression

                val fraction =
                    original.children.single()
                            as MathExpression.Fraction

                val denominator =
                    fraction.denominator

                /*
                 * HOME.
                 */
                editor.model.cursor =
                    MathCursor()

                navigationPress(
                    editor,
                    forward = true,
                    native = native
                )

                /*
                 * Primer PARENT:
                 * denominator.
                 */
                assertAt(
                    editor,
                    "denominator",
                    0
                )

                assertSame(
                    denominator,
                    editor.model.sequenceAt(
                        listOf(
                            MathCursorStep(
                                0,
                                "denominator"
                            )
                        )
                    )
                )

                /*
                 * denominator está vacío.
                 *
                 * Al ser PARENT no existe
                 * ChildBoundary intermedio.
                 */
                navigationPress(
                    editor,
                    forward = true,
                    native = native
                )

                assertAt(
                    editor,
                    "numerator",
                    0
                )

                navigationPress(
                    editor,
                    forward = true,
                    native = native
                )

                assertFullSelection(
                    editor,
                    "numerator",
                    "x+1"
                )

                navigationPress(
                    editor,
                    forward = true,
                    native = native
                )

                assertAtEnd(
                    editor,
                    "numerator"
                )

                navigationPress(
                    editor,
                    forward = true,
                    native = native
                )

                /*
                 * Terminados ambos PARENT,
                 * llegamos al borde derecho
                 * de Fraction.
                 */
                assertParentBoundary(
                    editor
                )

                assertSame(
                    original,
                    editor.model.expression
                )
            }
        }

    @Test
    fun rightPreservesEverySubscriptCheckpointThenTraversesBaseBeforeParent() =
        onMain {

            val editor =
                editorAt(
                    "x_{12}",
                    "lower",
                    atEnd = false
                )

            val original =
                editor.model.expression

            assertAt(
                editor,
                "lower",
                0
            )

            editor.perform(
                MathKeyboardAction.NextSlot
            )

            assertFullSelection(
                editor,
                "lower",
                "12"
            )

            editor.perform(
                MathKeyboardAction.NextSlot
            )

            assertAtEnd(
                editor,
                "lower"
            )

            editor.perform(
                MathKeyboardAction.NextSlot
            )

            assertBoundary(
                editor,
                "lower",
                after = true
            )

            /*
             * LEFT recorre:
             *
             * Parent
             * base
             * lower
             * HOME
             *
             * Por lo tanto RIGHT debe recorrer:
             *
             * HOME
             * lower
             * base
             * Parent
             */
            editor.perform(
                MathKeyboardAction.NextSlot
            )

            assertAt(
                editor,
                "base",
                0
            )

            editor.perform(
                MathKeyboardAction.NextSlot
            )

            assertFullSelection(
                editor,
                "base",
                "x"
            )

            editor.perform(
                MathKeyboardAction.NextSlot
            )

            assertAtEnd(
                editor,
                "base"
            )

            editor.perform(
                MathKeyboardAction.NextSlot
            )

            assertParentBoundary(editor)

            assertFalse(
                editor.model.canNavigateNext
            )

            assertSame(
                original,
                editor.model.expression
            )
        }

    @Test fun leftFollowsTheDeclaredChildBeforeParentContentAndFinishesAtHome() = onMain {
        val editor = MathExpressionEditor(context)
        assertTrue(editor.setLatex("x^{23}"))
        val original = editor.model.expression
        assertParentBoundary(editor)
        editor.perform(MathKeyboardAction.PreviousSlot)
        assertBoundary(editor, "upper", after = true)
        editor.perform(MathKeyboardAction.PreviousSlot)
        assertAtEnd(editor, "upper")
        editor.perform(MathKeyboardAction.PreviousSlot)
        assertFullSelection(editor, "upper", "23")
        editor.perform(MathKeyboardAction.PreviousSlot)
        assertAt(editor, "upper", 0)
        editor.perform(MathKeyboardAction.PreviousSlot)
        assertAtEnd(editor, "base")
        editor.perform(MathKeyboardAction.PreviousSlot)
        assertFullSelection(editor, "base", "x")
        editor.perform(MathKeyboardAction.PreviousSlot)
        assertAt(editor, "base", 0)
        editor.perform(MathKeyboardAction.PreviousSlot)
        assertEquals(MathCursor(), editor.model.cursor)
        assertEquals(MathCursorPresentation.Neutral, editor.model.cursorPresentation)
        assertFalse(editor.model.canNavigatePrevious)
        assertEquals(original, editor.model.expression)
    }

    @Test fun scriptsTraverseUpperThenParentContentThenLowerOnLeft() = onMain {
        assertToolbarWalk("x_{1}^{2}", listOf("upper" to "2", "base" to "x", "lower" to "1"))
    }

    @Test fun fractionNumeratorAndDenominatorUseTheSameToolbarCycle() = onMain {
        assertToolbarWalk("\\frac{12}{34}", listOf("numerator" to "12", "denominator" to "34"))
    }

    @Test fun nthRootIndexAndRadicandUseTheSameToolbarCycle() = onMain {
        assertToolbarWalk("\\sqrt[12]{34}", listOf("index" to "12", "radicand" to "34"))
    }

    @Test fun allFourMatrixCellsUseTheSameDetailedLeftCycle() = onMain {
        // Declared order: upper-left, upper-right, lower-left, lower-right.
        assertToolbarWalk("\\begin{matrix}12&23\\\\34&45\\end{matrix}",
            listOf("r0c0" to "12", "r0c1" to "23", "r1c0" to "34", "r1c1" to "45"))
    }

    @Test fun largeOperatorVisitsOnlyItsDeclaredUpperThenLower() = onMain {
        assertToolbarWalk("\\sum_{1}^{2}", listOf("upper" to "2", "lower" to "1"))
    }

    @Test fun rapidlyCommittedNormalLettersAreImmediateParentsAndLeftSelectsOnlyTheLast() = onMain {
        for (batched in listOf(false, true)) {
            val editor = MathExpressionEditor(context)
            assertTrue(editor.setLatex("x^{2}"))
            val power = editor.model.expression.children.single()
            var mutations = 0
            var observed = editor.model.expression
            editor.onExpressionChanged = {
                if (observed != editor.model.expression) mutations++
                observed = editor.model.expression
            }
            val input = connection(editor)
            if (batched) assertTrue(input.beginBatchEdit())
            "abc".forEachIndexed { index, letter ->
                assertTrue(input.commitText(letter.toString(), 1))
                assertEquals("Every completed native insertion is already materialized in the live AST",
                    listOf(power) + "abc".take(index + 1).map { MathExpression.Variable(it.toString()) }, editor.model.expression.children)
                assertEquals(MathContentScope.Atom(index + 1, index + 2), editor.model.editingScope)
                assertEquals("A native key makes one logical AST change", index + 1, mutations)
            }
            if (batched) input.endBatchEdit()
            editor.perform(MathKeyboardAction.PreviousSlot)
            assertEquals(MathSelection(emptyList(), 3, 4), editor.model.selectionRange)
            assertEquals("c", connection(editor).getSelectedText(0).toString())
            assertEquals("Navigation itself makes no AST mutation", 3, mutations)
            editor.perform(MathKeyboardAction.PreviousSlot)
            assertEquals(MathCursor(offset = 3, level = MathCursorLevel.INTERIOR, scope = MathContentScope.Atom(3, 4)), editor.model.cursor)
            assertNull(editor.model.selectionRange)
        }
    }

    @Test
    fun emptyFractionParentsRemainReciprocalWithoutInventingSelection() =
        onMain {

            val editor =
                MathExpressionEditor(
                    context
                )

            assertTrue(
                editor.setLatex(
                    "\\frac{}{}"
                )
            )

            val original =
                editor.model.expression

            val denominator =
                listOf(
                    MathCursorStep(
                        0,
                        "denominator"
                    )
                )

            val numerator =
                listOf(
                    MathCursorStep(
                        0,
                        "numerator"
                    )
                )

            val interior =
                MathCursorPresentation.Interior(
                    MathContentScope.Slot
                )

            val expected =
                listOf(
                    Triple(
                        MathCursor(),
                        null,
                        MathCursorPresentation.Neutral
                    ),

                    Triple(
                        MathCursor(
                            denominator,
                            0
                        ),
                        null,
                        interior
                    ),

                    Triple(
                        MathCursor(
                            numerator,
                            0
                        ),
                        null,
                        interior
                    ),

                    Triple(
                        MathCursor(
                            offset = 1
                        ),
                        null,
                        MathCursorPresentation.ParentBoundary(
                            emptyList(),
                            0
                        )
                    )
                )

            /*
             * IMPORTANTE:
             * establecemos HOME antes de crear
             * la toolbar para que sus estados
             * enabled nazcan sincronizados.
             */
            editor.model.cursor =
                MathCursor()

            val toolbar =
                toolbar(
                    editor
                )

            val next =
                navigation(
                    toolbar,
                    "Slot siguiente"
                )

            val previous =
                navigation(
                    toolbar,
                    "Slot anterior"
                )

            assertEquals(
                expected[0],
                navigationSnapshot(
                    editor
                )
            )

            for (
            index in
            1 until expected.size
            ) {

                assertTrue(
                    next.performClick()
                )

                assertEquals(
                    expected[index],
                    navigationSnapshot(
                        editor
                    )
                )

                assertNull(
                    editor.model.selectionRange
                )

                assertSame(
                    original,
                    editor.model.expression
                )
            }

            assertFalse(
                editor.model.canNavigateNext
            )

            for (
            index in
            expected.lastIndex - 1 downTo 0
            ) {

                assertTrue(
                    previous.performClick()
                )

                assertEquals(
                    expected[index],
                    navigationSnapshot(
                        editor
                    )
                )

                assertNull(
                    editor.model.selectionRange
                )

                assertSame(
                    original,
                    editor.model.expression
                )
            }

            assertFalse(
                editor.model.canNavigatePrevious
            )
        }

    @Test
    fun navigationButtonsIncludePendingSelectionAndCollapseStatesAtTheLastSlot() =
        onMain {

            val editor =
                MathExpressionEditor(context)

            assertTrue(
                editor.setLatex(
                    "x_{12}"
                )
            )

            val original =
                editor.model.expression

            /*
             * Colocamos el modelo en HOME antes de
             * enlazar el toolbar.
             *
             * Así sus botones nacen con el estado
             * correcto de canNavigatePrevious/Next.
             */
            editor.model.cursor =
                MathCursor()

            val toolbar =
                toolbar(editor)

            val previous =
                navigation(
                    toolbar,
                    "Slot anterior"
                )

            val next =
                navigation(
                    toolbar,
                    "Slot siguiente"
                )

            val lower =
                listOf(
                    MathCursorStep(
                        0,
                        "lower"
                    )
                )

            val base =
                listOf(
                    MathCursorStep(
                        0,
                        "base"
                    )
                )

            val interior =
                MathCursorPresentation.Interior(
                    MathContentScope.Slot
                )

            /*
             * Recorrido canónico RIGHT:
             *
             * HOME
             * lower izquierda
             * lower seleccionado
             * lower derecha
             * borde lower
             * base izquierda
             * base seleccionado
             * base derecha
             * Parent
             *
             * LEFT debe ser exactamente
             * esta lista al revés.
             */
            val expected =
                listOf(
                    Triple(
                        MathCursor(),
                        null,
                        MathCursorPresentation.Neutral
                    ),

                    Triple(
                        MathCursor(
                            lower,
                            0
                        ),
                        null,
                        interior
                    ),

                    Triple(
                        MathCursor(
                            lower,
                            1
                        ),
                        MathSelection(
                            lower,
                            0,
                            1
                        ),
                        interior
                    ),

                    Triple(
                        MathCursor(
                            lower,
                            1
                        ),
                        null,
                        interior
                    ),

                    Triple(
                        MathCursor(
                            offset = 1,
                            level =
                                MathCursorLevel.CHILD_BOUNDARY
                        ),
                        null,
                        MathCursorPresentation.ChildBoundary(
                            lower
                        )
                    ),

                    Triple(
                        MathCursor(
                            base,
                            0
                        ),
                        null,
                        interior
                    ),

                    Triple(
                        MathCursor(
                            base,
                            1
                        ),
                        MathSelection(
                            base,
                            0,
                            1
                        ),
                        interior
                    ),

                    Triple(
                        MathCursor(
                            base,
                            1
                        ),
                        null,
                        interior
                    ),

                    Triple(
                        MathCursor(
                            offset = 1
                        ),
                        null,
                        MathCursorPresentation.ParentBoundary(
                            emptyList(),
                            0
                        )
                    )
                )


            assertEquals(
                expected.first(),
                navigationSnapshot(editor)
            )

            assertFalse(
                previous.isEnabled
            )

            assertTrue(
                next.isEnabled
            )


            /*
             * RIGHT consume exactamente
             * un checkpoint por pulsación.
             */
            for (
            index in 1 until expected.size
            ) {
                assertTrue(
                    "RIGHT debe estar disponible antes del checkpoint $index",
                    next.isEnabled
                )

                assertTrue(
                    next.performClick()
                )

                assertEquals(
                    "RIGHT checkpoint $index",
                    expected[index],
                    navigationSnapshot(editor)
                )

                assertSame(
                    "La navegación no modifica el AST",
                    original,
                    editor.model.expression
                )

                assertNavigation(
                    toolbar,
                    editor,
                    previous,
                    next
                )
            }

            assertFalse(
                next.isEnabled
            )

            assertParentBoundary(editor)


            /*
             * LEFT deshace exactamente
             * los mismos checkpoints.
             */
            for (
            index in
            expected.size - 2 downTo 0
            ) {
                assertTrue(
                    "LEFT debe estar disponible antes del checkpoint $index",
                    previous.isEnabled
                )

                assertTrue(
                    previous.performClick()
                )

                assertEquals(
                    "LEFT checkpoint $index",
                    expected[index],
                    navigationSnapshot(editor)
                )

                assertSame(
                    "La navegación inversa no modifica el AST",
                    original,
                    editor.model.expression
                )

                assertNavigation(
                    toolbar,
                    editor,
                    previous,
                    next
                )
            }

            assertFalse(
                previous.isEnabled
            )

            assertTrue(
                next.isEnabled
            )

            assertEquals(
                MathCursor(),
                editor.model.cursor
            )

            assertNull(
                editor.model.selectionRange
            )

            assertEquals(
                MathCursorPresentation.Neutral,
                editor.model.cursorPresentation
            )

            assertSame(
                original,
                editor.model.expression
            )
        }

    @Test fun progressivelySelectedSubscriptIsDeletedOnceByEveryImeBackspaceTransport() = onMain {
        DeleteTransport.entries.forEach { transport ->
            val editor = selectedLower()
            val input = connection(editor)
            assertEquals("12", input.getSelectedText(0).toString())
            backspace(input, transport)
            assertLowerAndUpper(editor, "", "2")
            val empty = editor.saveDraft().toString()
            repeat(4) { backspace(input, transport) }
            assertEquals("$transport keeps the resulting empty slot valid", empty, editor.saveDraft().toString())
            assertTrue(input.commitText("3", 1))
            assertLowerAndUpper(editor, "3", "2")
        }
        val editor = selectedLower()
        editor.perform(MathKeyboardAction.Backspace)
        assertLowerAndUpper(editor, "", "2")
    }

    @Test fun progressivelySelectedSubscriptCanBeReplacedByCommitOrCompositionWithoutDuplicates() = onMain {
        for (composing in listOf(false, true)) {
            val editor = selectedLower()
            val input = connection(editor)
            if (composing) {
                assertTrue(input.setComposingText("3", 1))
                assertLowerAndUpper(editor, "3", "2")
                assertTrue(input.setComposingText("3", 1))
                assertTrue(input.commitText("3", 1))
            } else assertTrue(input.commitText("3", 1))
            assertNull(editor.model.selectionRange)
            assertLowerAndUpper(editor, "3", "2")
            assertTrue(input.finishComposingText())
            assertLowerAndUpper(editor, "3", "2")
        }
    }

    @Test fun progressivelySelectedContentIsWrappedByTheExistingFractionAction() = onMain {
        val editor = selectedLower()
        val toolbar = toolbar(editor)
        val preview = descendants(toolbar).filterIsInstance<MathKeyboardActionPreviewView>()
            .single { it.contentDescription.toString() == "Insertar fracción" }
        assertTrue(preview.performClick())
        val scripts = editor.model.expression.children.single() as MathExpression.Scripts
        val fraction = requireNotNull(scripts.lower).children.single() as MathExpression.Fraction
        assertEquals("12", MathLatexSerializer.serialize(fraction.numerator))
        assertTrue(fraction.denominator.children.isEmpty())
        assertEquals("2", MathLatexSerializer.serialize(requireNotNull(scripts.upper)))
        assertEquals(listOf(MathCursorStep(0, "lower"), MathCursorStep(0, "denominator")), editor.model.cursor.path)
        assertNull(editor.model.selectionRange)
        assertEquals("", preview.text.toString())
    }

    @Test fun selectedSlotAcceptsSupportedLatexPasteWhileOrdinaryXPlusOneRemainsStable() = onMain {
        val editor = selectedLower()
        assertTrue(connection(editor).commitText("\\frac{x+1}{2}", 1))
        assertLowerAndUpper(editor, "\\frac{x+1}{2}", "2")
        assertFalse(editor.getLatex().contains('□'))

        val ordinary = MathExpressionEditor(context)
        assertTrue(ordinary.setLatex("x"))
        ordinary.model.placeTextCursor(MathCursor(offset = 0), 1)
        val input = connection(ordinary)
        assertTrue(input.setSelection(0, 1))
        assertNull(ordinary.model.selectionRange)
        assertTrue(input.commitText("+", 1))
        assertTrue(input.setComposingText("1", 1))
        assertTrue(input.setComposingText("1", 1))
        assertTrue(input.commitText("1", 1))
        assertEquals("x+1", ordinary.getLatex())
        assertNull(ordinary.model.selectionRange)
    }

    @Test fun imeSelectionAndComposingRegionCannotCreateTheProgressiveMathematicalSelection() = onMain {
        val editor = editorAt("x_{12}^{2}", "lower", atEnd = true)
        var input = connection(editor)
        assertTrue(input.setSelection(0, 2))
        assertNull("Android's predictive range is private IME metadata", editor.model.selectionRange)
        assertTrue(input.setComposingRegion(0, 2))
        assertNull("A composing range is not the user's full-slot selection", editor.model.selectionRange)
        val old = input
        editor.perform(MathKeyboardAction.PreviousSlot)
        assertFullSelection(editor, "lower", "12")
        input = connection(editor)
        assertFalse("A connection predating a genuine selection transition is obsolete", old.setComposingText("99", 1))
        assertTrue(input.setSelection(0, 0))
        assertTrue(input.setComposingRegion(0, 2))
        assertTrue(input.finishComposingText())
        assertFullSelection(editor, "lower", "12")
        assertTrue(input.commitText("3", 1))
        assertLowerAndUpper(editor, "3", "2")
    }

    @Test fun nativeInteriorNumericCursorMovesToTheDirectionalEdgeWithoutSelectingTheSlot() = onMain {
        for (forward in listOf(false, true)) {
            val editor = editorAt("x_{12}^{2}", "lower", atEnd = true)
            val input = connection(editor)
            assertTrue(input.setSelection(1, 1))
            assertEquals("1", input.getTextBeforeCursor(10, 0).toString())
            assertEquals("2", input.getTextAfterCursor(10, 0).toString())
            assertEquals(1, editor.model.cursor.offset)
            assertNull(editor.model.selectionRange)
            val original = editor.model.expression
            editor.perform(if (forward) MathKeyboardAction.NextSlot else MathKeyboardAction.PreviousSlot)
            if (forward) assertAtEnd(editor, "lower") else assertAt(editor, "lower", 0)
            assertEquals(original, editor.model.expression)
            assertNull("An interior cursor goes directly to an edge", editor.model.selectionRange)
            editor.perform(if (forward) MathKeyboardAction.NextSlot else MathKeyboardAction.PreviousSlot)
            assertNull("The next press exposes the adjacent structural checkpoint", editor.model.selectionRange)
            if (forward) assertBoundary(editor, "lower", after = true) else assertEquals(MathCursor(), editor.model.cursor)
        }
    }

    @Test fun manualCursorCallbackAndTapRemainExactAndNeverSelectAll() = onMain {
        val path = listOf(MathCursorStep(0, "lower"))
        val editor = MathExpressionEditor(context)
        val expression = MathExpression.Sequence(listOf(MathExpression.Scripts(
            MathExpression.Sequence(listOf(MathExpression.Variable("x"))),
            lower = MathExpression.Sequence(listOf(MathExpression.Number("12"), MathExpression.Number("34")))
        )))
        editor.model.setExpression(expression, MathCursor(path, 2))
        editor.onCursorRequested?.invoke(MathCursor(path, 1))
        assertEquals(MathCursor(path, 1), editor.model.cursor)
        assertNull(editor.model.selectionRange)
        editor.onCursorRequested?.invoke(MathCursor(path, 2))
        measure(editor)
        tapCaret(editor, MathCursor(path, 1))
        assertEquals("Touch chooses the existing AST boundary between 12 and 34", MathCursor(path, 1), editor.model.cursor)
        assertNull(editor.model.selectionRange)
        assertEquals(expression, editor.model.expression)
        editor.perform(MathKeyboardAction.NextSlot)
        assertAtEnd(editor, "lower")
        editor.perform(MathKeyboardAction.NextSlot)
        assertBoundary(editor, "lower", after = true)
        assertNull(editor.model.selectionRange)
    }

    @Test fun actualTapSplitsAnUnsplitNumberAtItsGlyphBoundaryWithoutSelectingIt() = onMain {
        for (insideSlot in listOf(false, true)) {
            val editor = MathExpressionEditor(context)
            assertTrue(editor.setLatex(if (insideSlot) "x_{1234}" else "1234"))
            val path = if (insideSlot) listOf(MathCursorStep(0, "lower")) else emptyList()
            editor.onCursorRequested?.invoke(MathCursor(path, 1))
            val original = editor.model.expression
            assertEquals(listOf(MathExpression.Number("1234")), requireNotNull(editor.model.sequenceAt(path)).children)
            measure(editor)
            val leaf = MathCursor(path, 0)
            val bounds = requireNotNull(editor.expressionLayout.textCaretBounds(leaf, 2))
            val x = editor.expressionOriginX + bounds.left
            val y = editor.expressionBaseline + (bounds.top + bounds.bottom) / 2f
            val hit = editor.expressionLayout.hitTestForEditing(x - editor.expressionOriginX, y - editor.expressionBaseline)
            assertEquals(MathExpressionTouchTarget(leaf, 2), hit)
            editor.expressionLayout.hitTest(x - editor.expressionOriginX, y - editor.expressionBaseline)
            assertSame("Reading glyph/layout targets cannot mutate the AST", original, editor.model.expression)
            assertNull(editor.model.selectionRange)
            val now = SystemClock.uptimeMillis()
            dispatch(editor, now, now, MotionEvent.ACTION_DOWN, x, y)
            assertSame("Touch-down still preserves the expression until a tap is confirmed", original, editor.model.expression)
            dispatch(editor, now, now + 30, MotionEvent.ACTION_UP, x, y)
            assertEquals(listOf(MathExpression.Number("12"), MathExpression.Number("34")),
                requireNotNull(editor.model.sequenceAt(path)).children)
            assertEquals(path, editor.model.cursor.path)
            assertEquals(1, editor.model.cursor.offset)
            assertEquals(MathCursorLevel.INTERIOR, editor.model.navigationLevel)
            assertNull("A manual glyph touch is never a full-slot mathematical selection", editor.model.selectionRange)
            if (insideSlot) {
                editor.perform(MathKeyboardAction.NextSlot)
                assertAtEnd(editor, "lower")
                assertNull(editor.model.selectionRange)
                editor.perform(MathKeyboardAction.NextSlot)
                assertBoundary(editor, "lower", after = true)
                advanceToParentBoundary(editor)
                assertParentBoundary(editor)
            }
        }
    }

    @Test fun glyphHitTestingNeverExposesOrSplitsTheMiddleOfAValidSurrogatePair() = onMain {
        val editor = MathExpressionEditor(context)
        val path = listOf(MathCursorStep(0, "lower"))
        val symbol = "A\uD83D\uDE00B"
        editor.model.setExpression(MathExpression.Sequence(listOf(MathExpression.Scripts(
            MathExpression.Sequence(listOf(MathExpression.Variable("x"))),
            lower = MathExpression.Sequence(listOf(MathExpression.Symbol(symbol)))
        ))), MathCursor(path, 1))
        editor.onCursorRequested?.invoke(MathCursor(path, 1))
        measure(editor)
        val original = editor.model.expression
        val leaf = MathCursor(path, 0)
        assertNull("UTF-16 offset 2 is inside the surrogate pair", editor.expressionLayout.textCaretBounds(leaf, 2))
        assertNotNull(editor.expressionLayout.textCaretBounds(leaf, 1))
        val bounds = requireNotNull(editor.expressionLayout.textCaretBounds(leaf, 3))
        val x = editor.expressionOriginX + bounds.left
        val y = editor.expressionBaseline + (bounds.top + bounds.bottom) / 2f
        assertEquals(MathExpressionTouchTarget(leaf, 3),
            editor.expressionLayout.hitTestForEditing(x - editor.expressionOriginX, y - editor.expressionBaseline))
        assertSame(original, editor.model.expression)
        val now = SystemClock.uptimeMillis()
        dispatch(editor, now, now, MotionEvent.ACTION_DOWN, x, y)
        dispatch(editor, now, now + 30, MotionEvent.ACTION_UP, x, y)
        assertEquals(listOf(MathExpression.Symbol("A\uD83D\uDE00"), MathExpression.Symbol("B")),
            requireNotNull(editor.model.sequenceAt(path)).children)
        assertEquals(MathCursor(path, 1), editor.model.cursor)
        assertNull(editor.model.selectionRange)
    }

    @Test fun verticalDragAndCancellationPreserveCursorSelectionAndExpression() = onMain {
        val editor = selectedLower()
        measure(editor)
        val target = MathCursor(listOf(MathCursorStep(0, "upper")), 0)
        val caret = requireNotNull(editor.expressionLayout.caretBounds(target))
        val x = editor.expressionOriginX + caret.left
        val y = editor.expressionBaseline + (caret.top + caret.bottom) / 2f
        val original = editor.saveDraft().toString()
        for (terminal in listOf(MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL)) {
            val now = SystemClock.uptimeMillis()
            dispatch(editor, now, now, MotionEvent.ACTION_DOWN, x, y)
            dispatch(editor, now, now + 20, MotionEvent.ACTION_MOVE, x, y + MathWidgetUi.dp(context, 80))
            dispatch(editor, now, now + 40, terminal, x, y + MathWidgetUi.dp(context, 120))
            assertEquals("A vertical gesture cannot turn into a slot-selection tap", original, editor.saveDraft().toString())
            assertFullSelection(editor, "lower", "12")
        }
    }

    @Test
    fun leftAndRightReenterTheSameAdjacentChildCheckpoint() =
        onMain {

            val editor =
                editorAt(
                    "x_{1}^{2}",
                    "upper",
                    atEnd = true
                )

            val original =
                editor.model.expression


            /*
             * upper END
             * RIGHT -> boundary upper
             * LEFT  -> upper END
             */
            editor.perform(
                MathKeyboardAction.NextSlot
            )

            assertOutside(
                editor,
                forward = true
            )

            editor.perform(
                MathKeyboardAction.PreviousSlot
            )

            assertAtEnd(
                editor,
                "upper"
            )


            /*
             * lower LEFT edge
             * LEFT  -> HOME
             * RIGHT -> lower LEFT edge
             */
            editor.onCursorRequested?.invoke(
                MathCursor(
                    listOf(
                        MathCursorStep(
                            0,
                            "lower"
                        )
                    ),
                    0
                )
            )

            editor.perform(
                MathKeyboardAction.PreviousSlot
            )

            assertEquals(
                MathCursor(),
                editor.model.cursor
            )

            editor.perform(
                MathKeyboardAction.NextSlot
            )

            assertAt(
                editor,
                "lower",
                0
            )

            assertEquals(
                original,
                editor.model.expression
            )
        }

    @Test fun backspaceAfterExitingAScriptRemovesThatScriptAndPreservesItsBaseAndSibling() = onMain {
        val fixtures = listOf(
            Triple("x_{1}", "lower", "x"),
            Triple("x^{2}", "upper", "x"),
            Triple("x_{1}^{2}", "lower", "x^{2}"),
            Triple("x_{1}^{2}", "upper", "x_{1}")
        )
        fixtures.forEach { (source, name, expected) ->
            (DeleteTransport.entries.map { it as DeleteTransport? } + null).forEach { transport ->
                val editor = editorAt(source, name, atEnd = true)
                editor.perform(MathKeyboardAction.NextSlot)
                assertOutside(editor, forward = true)
                if (transport == null) editor.perform(MathKeyboardAction.Backspace)
                else backspace(connection(editor), transport)
                assertEquals("Exterior $transport deletes only the exited $name", canonical(expected), editor.getLatex())
                assertEquals(MathCursor(offset = 1), editor.model.cursor)
                assertNull(editor.model.selectionRange)
            }
        }
    }

    @Test fun backspaceOutsideClearsRequiredSlotsAndRemovesOnlyOptionalChildren() = onMain {
        val fixtures = listOf(
            Triple("\\frac{1}{2}", "numerator", "\\frac{}{2}"), Triple("\\frac{1}{2}", "denominator", "\\frac{1}{}"),
            Triple("\\sqrt[3]{4}", "index", "\\sqrt{4}"),
            Triple("\\lim_{0}{1}", "condition", "\\lim_{}{1}"),
            Triple("\\begin{matrix}1&2\\\\3&4\\end{matrix}", "r0c0", "\\begin{matrix}&2\\\\3&4\\end{matrix}")
        )
        fixtures.forEach { (source, name, expected) ->
            (DeleteTransport.entries.map { it as DeleteTransport? } + null).forEach { transport ->
                val editor = editorAt(source, name, atEnd = true)
                editor.perform(MathKeyboardAction.NextSlot)
                assertOutside(editor, forward = true)
                if (transport == null) editor.perform(MathKeyboardAction.Backspace)
                else backspace(connection(editor), transport)
                assertEquals("$transport removes only the exited part outside $name", canonical(expected), editor.getLatex())
                assertEquals(1, editor.model.cursor.offset)
                assertNull(editor.model.selectionRange)
            }
        }
    }

    @Test fun rightFromParentContentReachesItsWholeOwnerForBackspaceWithoutSkippingDeclaredChildren() = onMain {
        val fixtures = listOf("x^{2}+4" to "base", "\\sqrt{3}+4" to "radicand",
            "\\sqrt[3]{4}+4" to "radicand", "\\lim_{0}{1}+4" to "body")
        fixtures.forEach { (source, name) ->
            (DeleteTransport.entries.map { it as DeleteTransport? } + null).forEach { transport ->
                val editor = editorAt(source, name, atEnd = true)
                advanceToParentBoundary(editor)
                assertParentBoundary(editor)
                if (transport == null) editor.perform(MathKeyboardAction.Backspace)
                else backspace(connection(editor), transport)
                assertEquals("$transport at BordePadre removes the complete owner and preserves its following siblings", "+4", editor.getLatex())
                assertEquals(MathCursor(), editor.model.cursor)
                assertNull(editor.model.selectionRange)
            }
        }
    }

    @Test fun backspaceAtANestedExteriorKeepsTheContainingSlotAndSiblingContent() = onMain {
        DeleteTransport.entries.forEach { transport ->
            val editor = MathExpressionEditor(context)
            assertTrue(editor.setLatex("\\frac{x_{1}}{2}"))
            val parent = listOf(MathCursorStep(0, "numerator"))
            val lower = parent + MathCursorStep(0, "lower")
            editor.onCursorRequested?.invoke(MathCursor(lower, 1))
            editor.perform(MathKeyboardAction.NextSlot)
            assertEquals(MathCursor(parent, 1, MathCursorLevel.CHILD_BOUNDARY, null), editor.model.cursor)
            assertNull(editor.model.selectionRange)
            backspace(connection(editor), transport)
            assertEquals("\\frac{x}{2}", editor.getLatex())
            assertEquals("Nested $transport retains the real numerator level", MathCursor(parent, 1, MathCursorLevel.NEUTRAL, null), editor.model.cursor)
            assertNull(editor.model.selectionRange)
        }
    }

    @Test fun exitingCompositionThenBackspacingCannotRestoreTextOrConsumeFollowingContent() = onMain {
        DeleteTransport.entries.forEach { transport ->
            val editor = editorAt("x_{1}+4", "lower", atEnd = true)
            val old = connection(editor)
            assertTrue(old.setComposingRegion(0, 1))
            editor.perform(MathKeyboardAction.NextSlot)
            assertOutside(editor, forward = true)
            assertFalse(old.setComposingText("11", 1))
            assertFalse(old.deleteSurroundingText(1, 0))
            val input = connection(editor)
            backspace(input, transport)
            assertEquals("Exterior deletion preserves the following expression", "x+4", editor.getLatex())
            assertEquals(MathCursor(offset = 1), editor.model.cursor)
            assertNull(editor.model.selectionRange)
            assertTrue(connection(editor).finishComposingText())
            assertEquals("Finishing composition never replays a deleted script", "x+4", editor.getLatex())
        }
    }

    @Test fun privateImeMarkerSelectionPreservesExteriorIntentForDeletingOnlyTheExitedScript() = onMain {
        for (name in listOf("upper", "lower")) for (transport in DeleteTransport.entries) {
            val editor = editorAt("x_{1}^{2}+4", name, atEnd = true)
            editor.perform(MathKeyboardAction.NextSlot)
            assertOutside(editor, forward = true)
            assertTrue(editor.model.isAtNavigationBoundary)
            val original = editor.model.expression
            val originalDraft = editor.saveDraft().toString()
            val previousAvailable = editor.model.canNavigatePrevious
            val nextAvailable = editor.model.canNavigateNext
            val input = connection(editor)
            assertEquals("\uFFFC", input.getTextBeforeCursor(20, 0).toString())
            assertTrue(input.setSelection(0, 1))
            assertEquals("The marker range remains private native metadata", "\uFFFC", input.getSelectedText(0).toString())
            assertNull(editor.model.selectionRange)
            assertSame(original, editor.model.expression)
            assertOutside(editor, forward = true)
            assertTrue("Updating an IME range at the same caret preserves the exterior", editor.model.isAtNavigationBoundary)
            assertEquals("The precise exterior draft is unchanged by a private IME range", originalDraft, editor.saveDraft().toString())
            assertEquals(previousAvailable, editor.model.canNavigatePrevious)
            assertEquals(nextAvailable, editor.model.canNavigateNext)
            assertFalse("Composing text cannot claim an existing structural marker", input.setComposingRegion(0, 1))
            assertTrue(input.finishComposingText())
            assertEquals(originalDraft, editor.saveDraft().toString())
            assertTrue(editor.model.isAtNavigationBoundary)
            backspace(input, transport)
            val expected = if (name == "upper") "x_{1}+4" else "x^{2}+4"
            assertEquals("$transport deletes only the exited $name despite the private marker range", canonical(expected), editor.getLatex())
            assertEquals(MathCursor(offset = 1), editor.model.cursor)
            assertNull(editor.model.selectionRange)
            assertFalse(editor.model.isAtNavigationBoundary)
        }
    }

    @Test fun movingTheNativeCaretAwayFromAnExteriorInvalidatesItsStructuralDeleteIntent() = onMain {
        for (name in listOf("upper", "lower")) for (transport in DeleteTransport.entries) {
            val editor = editorAt("x_{1}^{2}+4", name, atEnd = true)
            editor.perform(MathKeyboardAction.NextSlot)
            assertOutside(editor, forward = true)
            assertTrue(editor.model.isAtNavigationBoundary)
            val original = editor.model.expression
            val input = connection(editor)
            assertTrue(input.setSelection(0, 0))
            assertEquals(0, editor.model.cursor.offset)
            assertEquals(MathCursorLevel.NEUTRAL, editor.model.navigationLevel)
            assertNull(editor.model.selectionRange)
            assertFalse("A genuine native caret move invalidates the old exterior", editor.model.isAtNavigationBoundary)
            assertSame(original, editor.model.expression)
            backspace(input, transport)
            assertSame("Backspace before the entire expression cannot delete the previous exited $name", original, editor.model.expression)
            assertEquals(0, editor.model.cursor.offset)
            assertEquals(MathCursorLevel.NEUTRAL, editor.model.navigationLevel)
            assertNull(editor.model.selectionRange)
        }
    }

    private fun assertToolbarWalk(
        latex: String,
        slots: List<Pair<String, String>>
    ) {
        val editor =
            MathExpressionEditor(context)

        assertTrue(
            editor.setLatex(latex)
        )

        val original =
            editor.model.expression

        val toolbar =
            toolbar(editor)

        val previous =
            navigation(
                toolbar,
                "Slot anterior"
            )

        val next =
            navigation(
                toolbar,
                "Slot siguiente"
            )

        val descriptors =
            original.children
                .single()
                .navigationChildren()

        assertEquals(
            "Los descriptores conservan el orden canónico declarado",
            slots.map { it.first },
            descriptors.map { it.id }
        )

        descriptors.forEach { descriptor ->
            val path =
                listOf(
                    MathCursorStep(
                        0,
                        descriptor.id
                    )
                )

            val sequence =
                requireNotNull(
                    editor.model.sequenceAt(path)
                )

            assertEquals(
                slots.single {
                    it.first == descriptor.id
                }.second,
                MathLatexSerializer.serialize(
                    sequence
                )
            )
        }


        /*
         * Llegamos primero al checkpoint
         * más a la derecha.
         *
         * Normalmente ya estamos en
         * BordePadre.
         *
         * Si la Sequence termina en un
         * LargeOperator existe además su
         * checkpoint independiente de
         * continuación.
         */
        var terminalSteps =
            0

        while (
            editor.model.canNavigateNext
        ) {
            assertTrue(
                "El extremo derecho debe alcanzarse en pocos pasos",
                terminalSteps++ < 10
            )

            assertTrue(
                next.performClick()
            )

            assertSame(
                "Buscar el extremo derecho no modifica el AST",
                original,
                editor.model.expression
            )
        }

        val terminalState =
            navigationSnapshot(editor)


        /*
         * Recorremos LEFT completo desde
         * el extremo derecho hasta HOME.
         */
        val leftStates =
            mutableListOf(
                navigationSnapshot(editor)
            )

        var leftSteps =
            0

        while (
            editor.model.canNavigatePrevious
        ) {
            assertTrue(
                "LEFT debe terminar en un número finito de checkpoints",
                leftSteps++ < 100
            )

            assertTrue(
                previous.performClick()
            )

            leftStates +=
                navigationSnapshot(editor)

            assertSame(
                "LEFT nunca reconstruye el AST",
                original,
                editor.model.expression
            )
        }

        assertEquals(
            MathCursor(),
            editor.model.cursor
        )

        assertEquals(
            MathCursorPresentation.Neutral,
            editor.model.cursorPresentation
        )

        assertNull(
            editor.model.selectionRange
        )

        assertFalse(
            editor.model.canNavigatePrevious
        )


        /*
         * RIGHT debe recorrer exactamente
         * los mismos estados al revés.
         */
        val rightStates =
            mutableListOf(
                navigationSnapshot(editor)
            )

        var rightSteps =
            0

        while (
            editor.model.canNavigateNext
        ) {
            assertTrue(
                "RIGHT no puede inventar checkpoints",
                rightSteps++ < 100
            )

            assertTrue(
                next.performClick()
            )

            rightStates +=
                navigationSnapshot(editor)

            assertSame(
                "RIGHT nunca reconstruye el AST",
                original,
                editor.model.expression
            )
        }

        assertEquals(
            "RIGHT debe ser exactamente LEFT recorrido al revés",
            leftStates.asReversed(),
            rightStates
        )

        assertEquals(
            "RIGHT debe terminar exactamente en el mismo extremo derecho",
            terminalState,
            navigationSnapshot(editor)
        )

        assertFalse(
            editor.model.canNavigateNext
        )

        assertSame(
            original,
            editor.model.expression
        )
    }

    private fun advanceToParentBoundary(editor: MathExpressionEditor) {
        val original = editor.model.expression
        var steps = 0
        while (editor.model.cursorPresentation != MathCursorPresentation.ParentBoundary(emptyList(), 0)) {
            assertTrue("Whole-owner deletion is reachable through finite structural navigation", steps++ < 40)
            assertTrue(editor.model.canNavigateNext)
            editor.perform(MathKeyboardAction.NextSlot)
            assertSame("Opening every declared child before the owner never changes the AST", original, editor.model.expression)
        }
    }

    private fun selectedLower() = editorAt("x_{12}^{2}", "lower", atEnd = true).also {
        it.perform(MathKeyboardAction.PreviousSlot)
        assertFullSelection(it, "lower", "12")
    }

    private fun editorAt(latex: String, name: String, atEnd: Boolean): MathExpressionEditor {
        val editor = MathExpressionEditor(context)
        assertTrue(editor.setLatex(latex))
        val path = listOf(MathCursorStep(0, name))
        val sequence = requireNotNull(editor.model.sequenceAt(path))
        editor.onCursorRequested?.invoke(MathCursor(path, if (atEnd) sequence.children.size else 0))
        return editor
    }

    private fun assertFullSelection(editor: MathExpressionEditor, name: String, text: String) {
        val path = listOf(MathCursorStep(0, name))
        val sequence = requireNotNull(editor.model.sequenceAt(path))
        assertEquals(text, MathLatexSerializer.serialize(sequence))
        assertEquals(MathSelection(path, 0, sequence.children.size), editor.model.selectionRange)
    }

    private fun assertAt(editor: MathExpressionEditor, name: String, offset: Int) {
        assertEquals(MathCursor(listOf(MathCursorStep(0, name)), offset), editor.model.cursor)
        assertNull(editor.model.selectionRange)
    }

    private fun assertAtEnd(editor: MathExpressionEditor, name: String) {
        val path = listOf(MathCursorStep(0, name))
        assertAt(editor, name, requireNotNull(editor.model.sequenceAt(path)).children.size)
    }

    private fun assertOutside(
        editor: MathExpressionEditor,
        forward: Boolean
    ) {
        require(forward) {
            "An exterior fixture can only represent the RIGHT side"
        }

        assertEquals(
            "Leaving a child exposes its right boundary",
            MathCursor(
                offset = 1,
                level = MathCursorLevel.CHILD_BOUNDARY
            ),
            editor.model.cursor
        )

        assertNull(
            "The exterior cursor is never represented by an artificial IME/math selection",
            editor.model.selectionRange
        )
    }

    private fun assertBoundary(editor: MathExpressionEditor, name: String, after: Boolean) {
        require(after) { "No left child boundary exists" }
        assertOutside(editor, after)
        assertEquals(MathCursorPresentation.ChildBoundary(listOf(MathCursorStep(0, name)), after), editor.model.cursorPresentation)
    }

    private fun assertParentBoundary(
        editor: MathExpressionEditor,
        nodeIndex: Int = 0
    ) {
        assertEquals(
            MathCursor(
                offset = nodeIndex + 1
            ),
            editor.model.cursor
        )

        assertEquals(
            MathCursorPresentation.ParentBoundary(
                emptyList(),
                nodeIndex
            ),
            editor.model.cursorPresentation
        )

        assertNull(
            editor.model.selectionRange
        )
    }

    private fun assertLowerAndUpper(editor: MathExpressionEditor, lower: String, upper: String) {
        val scripts = editor.model.expression.children.single() as MathExpression.Scripts
        assertEquals("x", MathLatexSerializer.serialize(scripts.base))
        assertEquals(lower, MathLatexSerializer.serialize(requireNotNull(scripts.lower)))
        assertEquals(upper, MathLatexSerializer.serialize(requireNotNull(scripts.upper)))
        assertNull(editor.model.selectionRange)
    }

    private fun assertNavigation(toolbar: MathKeyboardView, editor: MathExpressionEditor, previous: ImageButton, next: ImageButton) {
        assertSame(previous, navigation(toolbar, "Slot anterior"))
        assertSame(next, navigation(toolbar, "Slot siguiente"))
        assertEquals(View.VISIBLE, previous.visibility)
        assertEquals(View.VISIBLE, next.visibility)
        assertEquals(editor.model.canNavigatePrevious, previous.isEnabled)
        assertEquals(editor.model.canNavigateNext, next.isEnabled)
        assertNotNull(previous.drawable)
        assertNotNull(next.drawable)
    }

    private fun toolbar(editor: MathExpressionEditor) = MathKeyboardView(context, Color.rgb(169, 233, 111)).also {
        it.bind(editor, listOf("FRACTION", "POWER", "SUBSCRIPT", "MATRIX", "+"))
    }

    private fun navigation(toolbar: MathKeyboardView, name: String) =
        descendants(toolbar).filterIsInstance<ImageButton>().single { it.contentDescription.toString() == name }

    private fun descendants(view: View): List<View> = listOf(view) +
        if (view is ViewGroup) (0 until view.childCount).flatMap { descendants(view.getChildAt(it)) } else emptyList()

    private fun tapCaret(editor: MathExpressionEditor, target: MathCursor) {
        val bounds = requireNotNull(editor.expressionLayout.caretBounds(target))
        val x = editor.expressionOriginX + bounds.left
        val y = editor.expressionBaseline + (bounds.top + bounds.bottom) / 2f
        assertEquals("The tap fixture targets the requested mathematical boundary", target,
            editor.expressionLayout.hitTest(x - editor.expressionOriginX, y - editor.expressionBaseline))
        val now = SystemClock.uptimeMillis()
        dispatch(editor, now, now, MotionEvent.ACTION_DOWN, x, y)
        dispatch(editor, now, now + 30, MotionEvent.ACTION_UP, x, y)
    }

    private fun dispatch(editor: MathExpressionEditor, downTime: Long, time: Long, action: Int, x: Float, y: Float) {
        val event = MotionEvent.obtain(downTime, time, action, x, y, 0)
        try { assertTrue(editor.dispatchTouchEvent(event)) } finally { event.recycle() }
    }

    private fun measure(editor: MathExpressionEditor) {
        editor.measure(View.MeasureSpec.makeMeasureSpec(MathWidgetUi.dp(context, 420), View.MeasureSpec.EXACTLY),
            View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED))
        editor.layout(0, 0, editor.measuredWidth, editor.measuredHeight)
    }

    private fun connection(editor: MathExpressionEditor) = requireNotNull(editor.onCreateInputConnection(EditorInfo()))

    private fun navigationSnapshot(editor: MathExpressionEditor) =
        Triple(editor.model.cursor, editor.model.selectionRange, editor.model.cursorPresentation)

    private fun navigationPress(editor: MathExpressionEditor, forward: Boolean, native: Boolean) {
        if (native) {
            val input = connection(editor)
            val key = if (forward) KeyEvent.KEYCODE_DPAD_RIGHT else KeyEvent.KEYCODE_DPAD_LEFT
            assertTrue(input.sendKeyEvent(KeyEvent(KeyEvent.ACTION_DOWN, key)))
            val afterDown = navigationSnapshot(editor)
            input.sendKeyEvent(KeyEvent(KeyEvent.ACTION_UP, key))
            assertEquals("Arrow key-up cannot consume another checkpoint", afterDown, navigationSnapshot(editor))
        } else {
            val controls = toolbar(editor)
            navigation(controls, if (forward) "Slot siguiente" else "Slot anterior").performClick()
        }
    }

    private fun canonical(latex: String) = MathLatexSerializer.serialize(
        (MathLatexParser.parse(latex) as MathLatexParser.Result.Success).expression)

    private fun backspace(input: InputConnection, transport: DeleteTransport) {
        when (transport) {
            DeleteTransport.TEXT -> assertTrue(input.deleteSurroundingText(1, 0))
            DeleteTransport.CODE_POINTS -> assertTrue(input.deleteSurroundingTextInCodePoints(1, 0))
            DeleteTransport.KEY_EVENT -> {
                assertTrue(input.sendKeyEvent(KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_DEL)))
                // A structural delete can intentionally invalidate this connection. Its old
                // key-up must not edit again; rejection is also valid after that transition.
                input.sendKeyEvent(KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_DEL))
            }
        }
    }

    private fun onMain(body: () -> Unit) = instrumentation.runOnMainSync { body() }
}
