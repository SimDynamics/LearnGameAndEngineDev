package com.learngraphicsprogramming.app

import android.content.Context
import android.text.Editable
import android.text.NoCopySpan
import android.text.Spanned
import android.text.style.MetricAffectingSpan
import android.text.style.ReplacementSpan
import android.view.ContextThemeWrapper
import android.view.View
import android.view.ViewGroup
import android.widget.EditText
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.util.UUID

/** Real Android span/edit/draft behavior, without launching an Activity or any compiler. */
@RunWith(AndroidJUnit4::class)
class CppIndentationUnitsDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = ContextThemeWrapper(instrumentation.targetContext, R.style.Theme_LearnGraphicsProgramming)

    @Test fun manuallyWrittenSpacesStayIndividualAcrossSnapshotAndNativeSetText() = onMain {
        val code = "    value;"
        val plain = editable(CppIndentationUnits.loadedText(code))
        assertEquals(emptyList<Int>(), starts(plain))
        assertNull(backspace(plain, 4))
        assertEquals("   value;", plain.toString())

        val saved = CppIndentationUnits.snapshot(CppIndentationUnits.loadedText(code))
        val editor = EditText(context)
        editor.setText(CppIndentationUnits.loadedText(code, saved, seedPredefined = true))
        assertEquals("An explicit empty saved provenance must override seeding", emptyList<Int>(), starts(editor.text))
        assertNull(backspace(editor.text, 4))
        assertEquals("   value;", editor.text.toString())
    }

    @Test fun predefinedIndentationCopiesNonMetricUnitsAndRemovesOneLevelAtATime() = onMain {
        val code = "        return 0;\n\t    helper();"
        val prepared = CppIndentationUnits.loadedText(code, seedPredefined = true)
        assertEquals(code, prepared.toString())
        val spans = (prepared as Spanned).getSpans(0, prepared.length, Any::class.java)
        assertEquals(3, spans.size)
        assertTrue(spans.none { it is NoCopySpan || it is MetricAffectingSpan || it is ReplacementSpan })
        val editor = EditText(context)
        editor.setText(prepared)
        val text = editor.text
        assertEquals(listOf(0, 4, code.indexOf('\t') + 1), starts(text))
        CppSyntaxHighlighter.applyTo(text)
        assertEquals("Syntax coloring preserves editing provenance", listOf(0, 4, code.indexOf('\t') + 1), starts(text))
        assertEquals(4, backspace(text, 8))
        assertEquals("    return 0;\n\t    helper();", text.toString())
        assertEquals(0, backspace(text, 4))
        assertEquals("return 0;\n\t    helper();", text.toString())
        val second = text.indexOf('\t') + 1
        assertEquals(listOf(second), starts(text))
        assertEquals(second, backspace(text, second + 4))
        assertEquals("return 0;\n\thelper();", text.toString())
    }

    @Test fun aTabUnitAndThreeManualSpacesDeleteManualSpacesBeforeTheUnit() = onMain {
        val text = editable(CppIndentationUnits.loadedText("    ", seedPredefined = true))
        assertNull(CppIndentationUnits.beforeEdit(text, 4, 0, 3, 4, 4))
        text.insert(4, "   ")
        assertEquals(listOf(0), starts(text))
        for (caret in 7 downTo 5) {
            assertNull(backspace(text, caret))
            assertEquals(caret - 1, text.length)
            assertEquals(listOf(0), starts(text))
        }
        assertEquals(0, backspace(text, 4))
        assertEquals("", text.toString())

        val nativeTab = editable("\t   ")
        for (caret in 4 downTo 1) assertNull(backspace(nativeTab, caret))
        assertEquals("", nativeTab.toString())
    }

    @Test fun enterInheritsOnlyOriginalPredefinedUnitsAndKeepsNewBlockTabsIndependent() = onMain {
        val source = "    value;"
        for (predefined in listOf(true, false)) {
            val input = rulesInput(source, predefined)
            input.setSelection(input.length())
            input.text.insert(input.length(), "\n")
            assertEquals("$source\n    ", input.text.toString())
            assertEquals(if (predefined) listOf(0, source.length + 1) else emptyList<Int>(), starts(input.text))
            nativeBackspace(input)
            assertEquals(if (predefined) "$source\n" else "$source\n   ", input.text.toString())
        }

        for (predefined in listOf(true, false)) {
            val input = rulesInput("    {}", predefined)
            input.setSelection(5)
            input.text.insert(5, "\n")
            assertEquals("    {\n    \t\n    }", input.text.toString())
            val childStart = input.text.indexOf('\n') + 1
            val closingStart = input.text.lastIndexOf('\n') + 1
            assertEquals(if (predefined) listOf(0, childStart, closingStart) else emptyList<Int>(), starts(input.text))
            input.setSelection(childStart + 5)
            nativeBackspace(input)
            assertEquals("    {\n    \n    }", input.text.toString())
            nativeBackspace(input)
            assertEquals(if (predefined) "    {\n\n    }" else "    {\n   \n    }", input.text.toString())
            assertEquals(if (predefined) listOf(0, input.text.lastIndexOf('\n') + 1) else emptyList<Int>(), starts(input.text))
        }
    }

    @Test fun interiorEditsAndSelectionsInvalidateTouchedUnitsWithoutClaimingTheirNeighbors() = onMain {
        val text = editable(CppIndentationUnits.loadedText("        first;\n    second;", seedPredefined = true))
        val originalSecondLine = text.indexOf("    second")
        assertNull(CppIndentationUnits.beforeEdit(text, 2, 0, 1, 2, 2))
        text.insert(2, " ")
        assertEquals(listOf(5, originalSecondLine + 1), starts(text))
        assertNull("A selection never becomes a four-space backspace", CppIndentationUnits.beforeEdit(text, 5, 4, 0, 5, 9))
        text.delete(5, 9)
        assertEquals(listOf(originalSecondLine - 3), starts(text))
        assertEquals("     first;\n    second;", text.toString())

        val boundary = editable(CppIndentationUnits.loadedText("    first;", seedPredefined = true))
        assertNull(CppIndentationUnits.beforeEdit(boundary, 0, 0, 1, 0, 0))
        boundary.insert(0, " ")
        assertEquals(listOf(1), starts(boundary))
        val forward = CppIndentationUnits.beforeEdit(boundary, 1, 1, 0, 1, 1)
        assertNull("Forward delete remains an ordinary one-character edit", forward)
        boundary.delete(1, 2)
        assertEquals(emptyList<Int>(), starts(boundary))
    }

    @Test fun staleMalformedOrNonIndentationMetadataNeverCreatesUnitsOrChangesCode() = onMain {
        val code = "    value;\nplain    tail;"
        val valid = CppIndentationUnits.snapshot(CppIndentationUnits.loadedText(code, seedPredefined = true))
        assertEquals(listOf(0), starts(CppIndentationUnits.loadedText(code, valid)))
        val invalid = listOf(
            "not JSON",
            JSONObject(valid).put("version", 99).toString(),
            JSONObject(valid).put("sha256", "0".repeat(64)).toString(),
            JSONObject(valid).put("starts", JSONArray().put(0).put(0)).toString(),
            JSONObject(valid).put("starts", JSONArray().put(-1)).toString(),
            JSONObject(valid).put("starts", JSONArray().put(code.length)).toString(),
            JSONObject(valid).put("starts", JSONArray().put(code.indexOf("    tail"))).toString(),
            JSONObject(valid).put("starts", JSONArray().put("0")).toString()
        )
        invalid.forEach { saved ->
            val restored = CppIndentationUnits.loadedText(code, saved, seedPredefined = true)
            assertEquals(code, restored.toString())
            assertEquals("Invalid metadata cannot trigger fresh inference", emptyList<Int>(), starts(restored))
        }
        val changed = " $code"
        assertEquals(emptyList<Int>(), starts(CppIndentationUnits.loadedText(changed, valid)))
    }

    @Test fun projectDraftsKeepProvenancePerFileAcrossSwitchAndRecreationButLegacyDraftsDoNotInferIt() = onMain {
        assertTrue("Use an isolated QA package for draft integration", context.packageName in setOf(
            "com.learngraphicsprogramming.app.curricularqa", "com.learngraphicsprogramming.app.realdownloadsqa"))
        val key = "indentation_units_qa_${UUID.randomUUID()}"
        val preferences = context.getSharedPreferences("cpp_lesson_drafts", Context.MODE_PRIVATE)
        val source = "    main();"
        val helper = "    helper();"
        val block = JSONObject().put("projectFiles", JSONArray()
            .put(JSONObject().put("path", "main.cpp").put("initialCode", source))
            .put(JSONObject().put("path", "helper.cpp").put("initialCode", helper)))
        val before = block.toString()
        try {
            val editor = EditText(context)
            val project = CppLessonProjectEditor(context, block, key, editor) { }
            assertEquals(listOf(0), starts(editor.text))
            CppIndentationUnits.beforeEdit(editor.text, 0, 0, 1, 0, 0)
            editor.text.insert(0, " ")
            tree(project.controls).single { it.tag == "cpp_file_helper.cpp" }.performClick()
            assertEquals(helper, editor.text.toString())
            assertEquals(listOf(0), starts(editor.text))
            CppIndentationUnits.beforeEdit(editor.text, 4, 0, 3, 4, 4)
            editor.text.insert(4, "   ")
            tree(project.controls).single { it.tag == "cpp_file_main.cpp" }.performClick()
            assertEquals(" $source", editor.text.toString())
            assertEquals(listOf(1), starts(editor.text))

            val restoredEditor = EditText(context)
            val restored = CppLessonProjectEditor(context, block, key, restoredEditor) { }
            assertEquals(" $source", restoredEditor.text.toString())
            assertEquals(listOf(1), starts(restoredEditor.text))
            tree(restored.controls).single { it.tag == "cpp_file_helper.cpp" }.performClick()
            assertEquals("       helper();", restoredEditor.text.toString())
            assertEquals(listOf(0), starts(restoredEditor.text))
            for (caret in 7 downTo 5) assertNull(backspace(restoredEditor.text, caret))
            assertEquals(0, backspace(restoredEditor.text, 4))
            assertEquals("helper();", restoredEditor.text.toString())

            val draft = JSONObject(requireNotNull(preferences.getString("$key/project", null)))
            assertTrue(draft.has("indentation"))
            draft.remove("indentation")
            assertTrue(preferences.edit().putString("$key/project", draft.toString()).commit())
            val legacyEditor = EditText(context)
            val legacy = CppLessonProjectEditor(context, block, key, legacyEditor) { }
            tree(legacy.controls).single { it.tag == "cpp_file_main.cpp" }.performClick()
            assertEquals(" $source", legacyEditor.text.toString())
            assertEquals(emptyList<Int>(), starts(legacyEditor.text))
            legacy.reset()
            assertEquals(source, legacyEditor.text.toString())
            assertEquals(listOf(0), starts(legacyEditor.text))
            assertEquals(before, block.toString())
        } finally {
            assertTrue(preferences.edit().remove("$key/project").commit())
        }
    }

    private fun editable(text: CharSequence): Editable = Editable.Factory.getInstance().newEditable(text)
    private fun rulesInput(code: String, predefined: Boolean) = EditText(context).apply {
        setSingleLine(false)
        CppCodeInputRules.install(this)
        setText(CppIndentationUnits.loadedText(code, seedPredefined = predefined))
    }

    private fun nativeBackspace(input: EditText) {
        val caret = input.selectionStart
        input.text.delete(caret - 1, caret)
    }

    private fun starts(text: CharSequence): List<Int> {
        val values = JSONObject(CppIndentationUnits.snapshot(text)).getJSONArray("starts")
        return (0 until values.length()).map { values.getInt(it) }
    }

    /** Applies exactly the established editor's single native deletion plus its requested tail. */
    private fun backspace(text: Editable, caret: Int): Int? {
        val start = caret - 1
        val unit = CppIndentationUnits.beforeEdit(text, start, 1, 0, caret, caret)
        text.delete(start, caret)
        if (unit != null) text.delete(unit, start)
        return unit
    }

    private fun tree(view: View): List<View> = listOf(view) + if (view is ViewGroup)
        (0 until view.childCount).flatMap { tree(view.getChildAt(it)) } else emptyList()

    private fun onMain(action: () -> Unit) = instrumentation.runOnMainSync(action)
}
