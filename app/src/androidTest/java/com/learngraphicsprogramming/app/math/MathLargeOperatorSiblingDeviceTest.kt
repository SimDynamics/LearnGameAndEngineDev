package com.learngraphicsprogramming.app.math

import android.graphics.Color
import android.view.KeyEvent
import android.view.View
import android.view.ViewGroup
import android.view.inputmethod.EditorInfo
import android.widget.ImageButton
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class MathLargeOperatorSiblingDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()

    @Test fun integralContinuationIsEditableAndIndependentSiblingsKeepTheToolbarMoving() =
        instrumentation.runOnMainSync {
            val editor = MathExpressionEditor(instrumentation.targetContext)
            assertTrue(editor.setLatex("\\int_0^1"))
            val original = editor.model.expression
            val integral = original.children.single() as MathExpression.LargeOperator
            val toolbar = toolbar(editor)
            val next = navigation(toolbar, "Slot siguiente")
            val previous = navigation(toolbar, "Slot anterior")
            val parent = snapshot(editor)

            assertTrue(next.isEnabled)
            assertTrue(next.performClick())
            val interior = snapshot(editor)
            val scope = MathContentScope.Atom(1, 1)
            assertEquals(MathCursor(emptyList(), 1, MathCursorLevel.INTERIOR, scope), editor.model.cursor)
            assertEquals(MathCursorPresentation.Interior(scope), editor.model.cursorPresentation)
            assertTrue("The empty independent expression must retain an enabled RIGHT", next.isEnabled)
            assertTrue(editor.model.canNavigateNext)
            assertSame(original, editor.model.expression)

            assertTrue(next.performClick())
            assertTrue(editor.model.isAtSequenceInsertion)
            assertFalse(next.isEnabled)
            assertTrue(previous.performClick())
            assertEquals(interior, snapshot(editor))
            assertTrue(previous.performClick())
            assertEquals(parent, snapshot(editor))
            assertSame("Navigation cannot add a child to the integral", original, editor.model.expression)

            assertTrue(next.performClick())
            val input = requireNotNull(editor.onCreateInputConnection(EditorInfo()))
            assertTrue(input.commitText("x", 1))
            assertEquals(listOf(integral, MathExpression.Variable("x")), editor.model.expression.children)
            assertSame(integral, editor.model.expression.children.first())
            assertEquals(listOf("upper", "lower"), integral.navigationChildren().map { it.id })
            assertNull(editor.model.sequenceAt(listOf(MathCursorStep(0, "body"))))
            toolbar.hide()

            for (source in listOf("\\int_0^1 12", "\\int_0^1 x^{2}")) {
                val siblingEditor = MathExpressionEditor(instrumentation.targetContext)
                assertTrue(siblingEditor.setLatex(source))
                val unchanged = siblingEditor.model.expression
                val controls = toolbar(siblingEditor)
                val right = navigation(controls, "Slot siguiente")
                val left = navigation(controls, "Slot anterior")
                val leftStates = mutableListOf(snapshot(siblingEditor))
                while (siblingEditor.model.canNavigatePrevious) {
                    assertTrue("Traversal must reach HOME", leftStates.size < 40)
                    assertTrue(left.isEnabled)
                    assertTrue(left.performClick())
                    assertSame(unchanged, siblingEditor.model.expression)
                    leftStates += snapshot(siblingEditor)
                }
                assertEquals(MathCursor(), siblingEditor.model.cursor)
                var enteredSibling = false
                for (expected in leftStates.reversed().drop(1)) {
                    assertTrue("RIGHT cannot stop before the last sibling's border", right.isEnabled)
                    val connection = requireNotNull(siblingEditor.onCreateInputConnection(EditorInfo()))
                    assertTrue(connection.sendKeyEvent(KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_DPAD_RIGHT)))
                    val afterDown = snapshot(siblingEditor)
                    connection.sendKeyEvent(KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_DPAD_RIGHT))
                    assertEquals("Key-up must not consume a second checkpoint", afterDown, snapshot(siblingEditor))
                    assertEquals("Native RIGHT must exactly reverse toolbar LEFT", expected, afterDown)
                    assertSame(unchanged, siblingEditor.model.expression)
                    val cursor = siblingEditor.model.cursor
                    if (cursor.level == MathCursorLevel.INTERIOR &&
                        (cursor.path.firstOrNull()?.nodeIndex == 1 ||
                            cursor.path.isEmpty() && cursor.scope == MathContentScope.Atom(1, 2))) {
                        enteredSibling = true
                        assertTrue("The independent sibling still has a right frontier", right.isEnabled)
                    }
                }
                assertTrue("RIGHT enters the actual independent sibling", enteredSibling)
                assertEquals(MathCursorPresentation.ParentBoundary(emptyList(), 1), siblingEditor.model.cursorPresentation)
                assertFalse(right.isEnabled)
                assertEquals(listOf("upper", "lower"), unchanged.children.first().navigationChildren().map { it.id })
                assertNull(siblingEditor.model.sequenceAt(listOf(MathCursorStep(0, "body"))))
                controls.hide()
            }
        }

    private fun toolbar(editor: MathExpressionEditor) =
        MathKeyboardView(instrumentation.targetContext, Color.rgb(169, 233, 111)).also {
            it.bind(editor, listOf("INTEGRAL", "+"))
        }

    private fun navigation(toolbar: MathKeyboardView, description: String) =
        descendants(toolbar).filterIsInstance<ImageButton>().single { it.contentDescription == description }

    private fun descendants(view: View): List<View> = listOf(view) +
        if (view is ViewGroup) (0 until view.childCount).flatMap { descendants(view.getChildAt(it)) } else emptyList()

    private fun snapshot(editor: MathExpressionEditor) =
        Triple(editor.model.cursor, editor.model.selectionRange, editor.model.cursorPresentation)
}
