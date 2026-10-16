package com.learngraphicsprogramming.app

import android.content.Context
import android.graphics.Typeface
import android.view.ContextThemeWrapper
import android.view.View
import android.view.ViewGroup
import android.view.inputmethod.EditorInfo
import android.widget.FrameLayout
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import java.util.UUID
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

/** Native source characters are the indentation units; no predefined-space provenance is inferred. */
@RunWith(AndroidJUnit4::class)
class CppIndentationUnitsDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = ContextThemeWrapper(instrumentation.targetContext, R.style.Theme_LearnGraphicsProgramming)

    @Test fun tabCompletesOneTwoOrThreeManualSpacesAsOneRealCharacterAndOneNativeDeletion() = onMain {
        for (spaces in 1..3) {
            val fixture = Fixture(" ".repeat(spaces) + "value;")
            val input = fixture.input
            input.setSelection(spaces)
            CppCodeInputRules.insertTab(input)
            fixture.settle()
            assertEquals("\tvalue;", input.text.toString())
            assertEquals(1, input.selectionStart)
            assertEquals(1, input.selectionEnd)
            assertEquals(4 * input.paint.measureText(" "), input.layout.getPrimaryHorizontal(1), 1.1f)
            backspace(input)
            assertEquals("value;", input.text.toString())
            assertEquals(0, input.selectionStart)
        }
    }

    @Test fun directTabAndThreeTabsPlusOneManualSpaceReachTheNextFourColumnStop() = onMain {
        val fixture = Fixture("value;")
        val input = fixture.input
        input.setSelection(0)
        CppCodeInputRules.insertTab(input)
        assertEquals("\tvalue;", input.text.toString())
        backspace(input)
        assertEquals("value;", input.text.toString())

        input.setText("\t\t\t value;")
        input.setSelection(4)
        CppCodeInputRules.insertTab(input)
        fixture.settle()
        assertEquals("\t\t\t\tvalue;", input.text.toString())
        assertEquals(4, input.selectionStart)
        assertEquals(16 * input.paint.measureText(" "), input.layout.getPrimaryHorizontal(4), 1.1f)
        backspace(input)
        assertEquals("\t\t\tvalue;", input.text.toString())
        assertEquals(3, input.selectionStart)
    }

    @Test fun imeCommittedTabUsesTheSamePartialCellCompletionAndSelectionRules() = onMain {
        for (spaces in 1..4) {
            val input = Fixture(" ".repeat(spaces) + "value;").input
            input.setSelection(spaces)
            val connection = requireNotNull(input.onCreateInputConnection(EditorInfo()))
            try { assertTrue(connection.commitText("\t", 1)) }
            finally { connection.closeConnection() }
            assertEquals(if (spaces == 4) "    \tvalue;" else "\tvalue;", input.text.toString())
            assertEquals(if (spaces == 4) 5 else 1, input.selectionStart)
            backspace(input)
            assertEquals(if (spaces == 4) "    value;" else "value;", input.text.toString())
        }
        for ((start, end) in listOf(1 to 5, 5 to 1)) {
            val input = Fixture("prefix").input
            input.setSelection(start, end)
            val connection = requireNotNull(input.onCreateInputConnection(EditorInfo()))
            try { assertTrue(connection.commitText("\t", 1)) }
            finally { connection.closeConnection() }
            assertEquals("p\tx", input.text.toString())
            assertEquals(2, input.selectionStart)
            assertEquals(2, input.selectionEnd)
        }
    }

    @Test fun tabDoesNotRewriteCompletedFourSpaceCellsAndOnlyCompletesTheCurrentPartialCell() = onMain {
        for (spaces in 4..7) {
            val input = Fixture(" ".repeat(spaces) + "value;").input
            input.setSelection(spaces)
            CppCodeInputRules.insertTab(input)
            assertEquals("    \tvalue;", input.text.toString())
            assertEquals(5, input.selectionStart)
            backspace(input)
            assertEquals("    value;", input.text.toString())
            assertEquals(4, input.selectionStart)
            for (remaining in 3 downTo 0) {
                backspace(input)
                assertEquals(" ".repeat(remaining) + "value;", input.text.toString())
                assertEquals(remaining, input.selectionStart)
            }
        }
    }

    @Test fun manualSpacesAfterATabDeleteIndividuallyUntilAnExplicitTabCompletesTheirCell() = onMain {
        val input = Fixture("\t   value;").input
        input.setSelection(4)
        for (remaining in 2 downTo 0) {
            backspace(input)
            assertEquals("\t" + " ".repeat(remaining) + "value;", input.text.toString())
            assertEquals(1 + remaining, input.selectionStart)
        }
        backspace(input)
        assertEquals("value;", input.text.toString())

        input.setText("\t   value;")
        input.setSelection(4)
        CppCodeInputRules.insertTab(input)
        assertEquals("\t\tvalue;", input.text.toString())
        backspace(input)
        assertEquals("\tvalue;", input.text.toString())
    }

    @Test fun loadingPredefinedAndCopiedCodeNeverConvertsItsSpacesIntoDeletionUnits() = onMain {
        val source = "        return 0;\n\t    helper();"
        val fixture = Fixture(source)
        val input = fixture.input
        CppSyntaxHighlighter.applyTo(requireNotNull(input.text))
        fixture.settle()
        assertEquals(source, input.text.toString())
        input.setSelection(8)
        backspace(input)
        assertEquals("       return 0;\n\t    helper();", input.text.toString())
        input.setText(input.text.toString())
        input.setSelection(7)
        backspace(input)
        assertEquals("      return 0;\n\t    helper();", input.text.toString())
    }

    @Test fun selectionReplacementAndPairedDelimiterRulesRemainIndependentOfTabCompletion() = onMain {
        val input = Fixture("prefix").input
        for ((start, end) in listOf(1 to 5, 5 to 1)) {
            input.setText("prefix")
            input.setSelection(start, end)
            CppCodeInputRules.insertTab(input)
            assertEquals("p\tx", input.text.toString())
            assertEquals(2, input.selectionStart)
            assertEquals(2, input.selectionEnd)
        }
        input.setText("")
        input.setSelection(0)
        requireNotNull(input.text).insert(0, "(")
        assertEquals("()", input.text.toString())
        assertEquals(1, input.selectionStart)
        requireNotNull(input.text).insert(1, ")")
        assertEquals("()", input.text.toString())
        assertEquals(2, input.selectionStart)
        CppCodeInputRules.insertTab(input)
        assertEquals("()\t", input.text.toString())
        backspace(input)
        assertEquals("()", input.text.toString())
    }

    @Test fun enterPreservesExistingSpacesAndAddsOnlyRealTabsForANewBlockLevel() = onMain {
        val spaces = Fixture("    value;").input
        spaces.setSelection(spaces.length())
        requireNotNull(spaces.text).insert(spaces.length(), "\n")
        assertEquals("    value;\n    ", spaces.text.toString())
        spaces.setSelection(spaces.length())
        backspace(spaces)
        assertEquals("    value;\n   ", spaces.text.toString())

        val tabs = Fixture("\tvalue;").input
        tabs.setSelection(tabs.length())
        requireNotNull(tabs.text).insert(tabs.length(), "\n")
        assertEquals("\tvalue;\n\t", tabs.text.toString())
        tabs.setSelection(tabs.length())
        backspace(tabs)
        assertEquals("\tvalue;\n", tabs.text.toString())

        val block = Fixture("    {}").input
        block.setSelection(5)
        requireNotNull(block.text).insert(5, "\n")
        assertEquals("    {\n    \t\n    }", block.text.toString())
        block.setSelection(requireNotNull(block.text).indexOf('\n') + 6)
        backspace(block)
        assertEquals("    {\n    \n    }", block.text.toString())
        backspace(block)
        assertEquals("    {\n   \n    }", block.text.toString())
    }

    @Test fun projectDraftsRestoreRealTabsPerFileAndLegacySpacesRemainOrdinaryCharacters() = onMain {
        val key = "real_tab_units_qa_${UUID.randomUUID()}"
        val preferences = context.getSharedPreferences("cpp_lesson_drafts", Context.MODE_PRIVATE)
        val main = "    main();"
        val helper = "    helper();"
        val block = JSONObject().put("projectFiles", JSONArray()
            .put(JSONObject().put("path", "main.cpp").put("initialCode", main))
            .put(JSONObject().put("path", "helper.cpp").put("initialCode", helper)))
        val original = block.toString()
        try {
            val input = Fixture("").input
            val project = CppLessonProjectEditor(context, block, key, input) { }
            assertEquals(main, input.text.toString())
            input.setSelection(4)
            backspace(input)
            assertEquals("   main();", input.text.toString())
            CppCodeInputRules.insertTab(input)
            assertEquals("\tmain();", input.text.toString())
            file(project, "helper.cpp").performClick()
            assertEquals(helper, input.text.toString())
            input.setSelection(4)
            requireNotNull(input.text).insert(4, "   ")
            input.setSelection(7)
            CppCodeInputRules.insertTab(input)
            assertEquals("    \thelper();", input.text.toString())
            file(project, "main.cpp").performClick()
            assertEquals("\tmain();", input.text.toString())

            val restoredInput = Fixture("").input
            val restored = CppLessonProjectEditor(context, block, key, restoredInput) { }
            assertEquals("\tmain();", restoredInput.text.toString())
            restoredInput.setSelection(1)
            backspace(restoredInput)
            assertEquals("main();", restoredInput.text.toString())
            file(restored, "helper.cpp").performClick()
            assertEquals("    \thelper();", restoredInput.text.toString())
            restoredInput.setSelection(5)
            backspace(restoredInput)
            assertEquals(helper, restoredInput.text.toString())
            backspace(restoredInput)
            assertEquals("   helper();", restoredInput.text.toString())
            val saved = JSONObject(requireNotNull(preferences.getString("$key/project", null)))
            assertFalse("Persistent units are source TABs, not an indentation sidecar", saved.has("indentation"))

            val legacy = JSONObject().put("files", JSONArray()
                .put(JSONObject().put("path", "main.cpp").put("code", main))
                .put(JSONObject().put("path", "helper.cpp").put("code", "\thelper();")))
                .put("sources", JSONArray().put("main.cpp").put("helper.cpp"))
                .put("selected", "main.cpp")
                .put("indentation", JSONObject().put("main.cpp", "obsolete or malformed metadata"))
            assertTrue(preferences.edit().putString("$key/project", legacy.toString()).commit())
            val legacyInput = Fixture("").input
            val legacyProject = CppLessonProjectEditor(context, block, key, legacyInput) { }
            assertEquals(main, legacyInput.text.toString())
            legacyInput.setSelection(4)
            backspace(legacyInput)
            assertEquals("   main();", legacyInput.text.toString())
            file(legacyProject, "helper.cpp").performClick()
            assertEquals("\thelper();", legacyInput.text.toString())
            legacyProject.reset()
            assertEquals(main, legacyInput.text.toString())
            legacyInput.setSelection(4)
            backspace(legacyInput)
            assertEquals("   main();", legacyInput.text.toString())
            assertEquals(original, block.toString())
        } finally {
            assertTrue(preferences.edit().remove("$key/project").commit())
        }
    }

    private inner class Fixture(source: String) {
        val input = FreeScrollEditText(context).apply {
            setSingleLine(false)
            typeface = Typeface.MONOSPACE
            textSize = 18f
            CppCodeInputRules.install(this)
            setText(source)
        }
        private val host = FrameLayout(context).apply {
            addView(input, FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT))
        }

        init { settle() }

        fun settle() {
            repeat(2) {
                host.measure(View.MeasureSpec.makeMeasureSpec(800, View.MeasureSpec.EXACTLY),
                    View.MeasureSpec.makeMeasureSpec(500, View.MeasureSpec.EXACTLY))
                host.layout(0, 0, host.measuredWidth, host.measuredHeight)
            }
        }
    }

    private fun backspace(input: FreeScrollEditText) {
        val connection = requireNotNull(input.onCreateInputConnection(EditorInfo()))
        try { assertTrue("Android performs its ordinary one-character deletion", connection.deleteSurroundingText(1, 0)) }
        finally { connection.closeConnection() }
    }

    private fun file(project: CppLessonProjectEditor, path: String) = tree(project.controls).single { it.tag == "cpp_file_$path" }
    private fun tree(view: View): List<View> = listOf(view) + if (view is ViewGroup)
        (0 until view.childCount).flatMap { tree(view.getChildAt(it)) } else emptyList()

    private fun onMain(action: () -> Unit) {
        assertTrue("Only isolated QA applications may use test drafts", context.packageName in setOf(
            "com.learngraphicsprogramming.app.curricularqa", "com.learngraphicsprogramming.app.realdownloadsqa"))
        instrumentation.runOnMainSync(action)
    }
}
