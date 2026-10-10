package com.learngraphicsprogramming.app

import android.view.inputmethod.EditorInfo
import android.widget.EditText
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Regressions for the shared editor rules originally used by LessonActivity. */
@RunWith(AndroidJUnit4::class)
class CppCodeInputRulesInstrumentedTest {

    @Test
    fun standaloneEditorUsesAdvancedEditorAndDisablesImeExtractMode() {
        ActivityScenario.launch(CppPrototypeActivity::class.java).use { scenario ->
            scenario.onActivity { activity ->
                val field = activity.findViewById<EditText>(R.id.sourceCode)
                assertTrue(
                    "The standalone compiler should share FreeScrollEditText",
                    field is FreeScrollEditText
                )
                assertTrue(
                    "Landscape must preserve the app editor instead of the IME extract view",
                    field.imeOptions and EditorInfo.IME_FLAG_NO_EXTRACT_UI != 0
                )
                assertTrue(
                    "Landscape keyboard must not replace the code editor",
                    field.imeOptions and EditorInfo.IME_FLAG_NO_FULLSCREEN != 0
                )
            }
        }
    }

    @Test
    fun bothEditorsShareOpeningPairInsertionBehavior() {
        ActivityScenario.launch(CppPrototypeActivity::class.java).use { scenario ->
            scenario.onActivity { activity ->
                val field = activity.findViewById<EditText>(R.id.sourceCode)
                field.setText("ab")
                field.setSelection(1)
                field.text!!.insert(1, "(")
                assertEquals("a()b", field.text.toString())
                assertEquals(2, field.selectionStart)
            }
        }
    }
}
