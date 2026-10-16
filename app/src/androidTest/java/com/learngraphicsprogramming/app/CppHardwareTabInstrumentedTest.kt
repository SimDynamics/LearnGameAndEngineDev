package com.learngraphicsprogramming.app

import android.view.KeyEvent
import android.view.View
import android.widget.EditText
import android.widget.LinearLayout
import androidx.core.view.doOnLayout
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import org.junit.Assert.assertEquals
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Android key dispatch, not a text replacement pretending to be a physical Tab event. */
@RunWith(AndroidJUnit4::class)
class CppHardwareTabInstrumentedTest {
    @Test fun physicalTabInsertsFourSpacesOnceAndRetainsEditorFocus() {
        withEditor { activity, editor ->
            editor.setText("ab")
            editor.setSelection(1)
            assertTrue(activity.dispatchKeyEvent(tab(KeyEvent.ACTION_DOWN)))
            assertTrue(activity.dispatchKeyEvent(tab(KeyEvent.ACTION_UP)))
            assertEquals("a    b", editor.text.toString())
            assertEquals(5, editor.selectionStart)
            assertEquals(5, editor.selectionEnd)
            assertSame(editor, activity.currentFocus)
        }
    }

    @Test fun physicalTabReplacesForwardAndReversedSelections() {
        withEditor { activity, editor ->
            for ((start, end) in listOf(1 to 5, 5 to 1)) {
                editor.setText("prefix")
                editor.setSelection(start, end)
                activity.dispatchKeyEvent(tab(KeyEvent.ACTION_DOWN))
                activity.dispatchKeyEvent(tab(KeyEvent.ACTION_UP))
                assertEquals("p    x", editor.text.toString())
                assertEquals(5, editor.selectionStart)
                assertEquals(5, editor.selectionEnd)
            }
        }
    }

    @Test fun repeatingPhysicalTabKeepsTheSameFourSpaceIndentationUnit() {
        withEditor { activity, editor ->
            editor.setText("code")
            editor.setSelection(0)
            repeat(3) { repeatCount ->
                activity.dispatchKeyEvent(tab(KeyEvent.ACTION_DOWN, repeatCount))
            }
            activity.dispatchKeyEvent(tab(KeyEvent.ACTION_UP))
            assertEquals("            code", editor.text.toString())
            assertEquals(12, editor.selectionStart)
            assertSame(editor, activity.currentFocus)
        }
    }

    @Test fun physicalTabAfterSkippingAClosingDelimiterIsNotDiscardedAsImeSpacing() {
        withEditor { activity, editor ->
            editor.setText("()")
            editor.setSelection(1)
            editor.text!!.insert(1, ")")
            assertEquals("()", editor.text.toString())
            assertEquals(2, editor.selectionStart)
            // Dispatch in the same UI cycle as the skipped delimiter.
            activity.dispatchKeyEvent(tab(KeyEvent.ACTION_DOWN))
            activity.dispatchKeyEvent(tab(KeyEvent.ACTION_UP))
            assertEquals("()    ", editor.text.toString())
            assertEquals(6, editor.selectionStart)
        }
    }

    @Test fun modifiedTabCombinationsDoNotInsertCodeIndentation() {
        withEditor { _, editor ->
            editor.setText("code")
            editor.setSelection(2)
            for (modifier in listOf(KeyEvent.META_SHIFT_ON, KeyEvent.META_CTRL_ON, KeyEvent.META_ALT_ON)) {
                editor.dispatchKeyEvent(KeyEvent(0L, 0L, KeyEvent.ACTION_DOWN,
                    KeyEvent.KEYCODE_TAB, 0, modifier))
                editor.dispatchKeyEvent(KeyEvent(0L, 0L, KeyEvent.ACTION_UP,
                    KeyEvent.KEYCODE_TAB, 0, modifier))
                assertEquals("code", editor.text.toString())
            }
        }
    }

    @Test fun tabOutsideACppInputRetainsAndroidFocusNavigation() {
        ActivityScenario.launch(CppPrototypeActivity::class.java).use { scenario ->
            val instrumentation = InstrumentationRegistry.getInstrumentation()
            val laidOut = CountDownLatch(1)
            lateinit var parent: LinearLayout
            lateinit var ordinary: EditText
            lateinit var next: EditText
            scenario.onActivity { activity ->
                parent = activity.findViewById(R.id.compilerContent)
                ordinary = EditText(activity).apply {
                    id = View.generateViewId()
                    setSingleLine(true)
                    setText("ordinary")
                }
                next = EditText(activity).apply {
                    id = View.generateViewId()
                    setSingleLine(true)
                }
                ordinary.nextFocusForwardId = next.id
                parent.addView(ordinary, 0)
                parent.addView(next, 1)
                next.doOnLayout { laidOut.countDown() }
            }
            try {
                assertTrue("The ordinary inputs must complete a native layout before focus traversal",
                    laidOut.await(5, TimeUnit.SECONDS))
                instrumentation.waitForIdleSync()
                scenario.onActivity { activity ->
                    assertTrue(ordinary.width > 0 && ordinary.height > 0)
                    assertTrue(next.width > 0 && next.height > 0)
                    assertTrue(ordinary.requestFocus())
                    assertSame(ordinary, activity.currentFocus)
                }
                // Deliver through the window's real input path. Activity.dispatchKeyEvent
                // alone omits ViewRoot's traversal for an otherwise unhandled Tab key.
                instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_TAB)
                instrumentation.waitForIdleSync()
                scenario.onActivity { activity ->
                    assertEquals("ordinary", ordinary.text.toString())
                    assertSame(next, activity.currentFocus)
                }
            } finally {
                scenario.onActivity {
                    parent.removeView(ordinary)
                    parent.removeView(next)
                }
            }
        }
    }

    private fun tab(action: Int, repeatCount: Int = 0) =
        KeyEvent(0L, 0L, action, KeyEvent.KEYCODE_TAB, repeatCount)

    private fun withEditor(block: (CppPrototypeActivity, EditText) -> Unit) {
        ActivityScenario.launch(CppPrototypeActivity::class.java).use { scenario ->
            scenario.onActivity { activity ->
                val editor = activity.findViewById<EditText>(R.id.sourceCode)
                assertTrue(editor.requestFocus())
                block(activity, editor)
            }
        }
    }
}
