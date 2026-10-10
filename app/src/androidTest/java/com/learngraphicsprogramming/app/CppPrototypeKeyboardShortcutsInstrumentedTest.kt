package com.learngraphicsprogramming.app

import android.view.View
import android.widget.EditText
import android.widget.HorizontalScrollView
import android.widget.LinearLayout
import android.widget.TextView
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/**
 * Standalone compiler must expose the same saved character shortcuts as the
 * LessonActivity editors without modifying or depending on their view state.
 */
@RunWith(AndroidJUnit4::class)
class CppPrototypeKeyboardShortcutsInstrumentedTest {

    @Test
    fun shortcutBarMatchesTheProgrammingLessonSettings() {
        ActivityScenario.launch(CppPrototypeActivity::class.java).use { scenario ->
            scenario.onActivity { activity ->
                val bar = activity.findViewById<HorizontalScrollView>(
                    R.id.programmingKeyboardBar
                )
                val keys = activity.findViewById<LinearLayout>(
                    R.id.programmingKeyboardBarContent
                )
                val saved = KeyboardShortcutSettingsRepository.getShortcuts(activity)

                assertEquals(
                    "Keyboard shortcut bar stays hidden while the IME is closed",
                    View.GONE, bar.visibility
                )
                assertEquals("TAB plus configured character keys", saved.size + 1, keys.childCount)
                assertEquals(
                    activity.getString(R.string.lesson_tab_label),
                    (keys.getChildAt(0) as TextView).text.toString()
                )
                saved.forEachIndexed { index, value ->
                    assertEquals(value, (keys.getChildAt(index + 1) as TextView).text.toString())
                }
            }
        }
    }

    @Test
    fun shortcutsInsertAtCursorAndReplaceSelections() {
        ActivityScenario.launch(CppPrototypeActivity::class.java).use { scenario ->
            scenario.onActivity { activity ->
                val editor = activity.findViewById<EditText>(R.id.sourceCode)
                val keys = activity.findViewById<LinearLayout>(
                    R.id.programmingKeyboardBarContent
                )

                editor.setText("ab")
                editor.requestFocus()
                editor.setSelection(1)
                assertTrue((keys.getChildAt(0) as TextView).performClick())
                assertEquals("a    b", editor.text.toString())
                assertEquals(5, editor.selectionStart)

                val saved = KeyboardShortcutSettingsRepository.getShortcuts(activity)
                if (saved.isNotEmpty()) {
                    editor.setSelection(1, 5)
                    assertTrue((keys.getChildAt(1) as TextView).performClick())
                    assertEquals("a" + saved.first() + "b", editor.text.toString())
                    assertEquals(1 + saved.first().length, editor.selectionStart)
                }
            }
        }
    }
}
