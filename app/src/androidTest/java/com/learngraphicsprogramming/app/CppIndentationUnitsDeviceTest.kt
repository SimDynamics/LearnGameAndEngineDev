package com.learngraphicsprogramming.app

import android.content.Context
import android.graphics.Typeface
import android.text.NoCopySpan
import android.text.Spanned
import android.text.style.MetricAffectingSpan
import android.text.style.ReplacementSpan
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

/** Native TABs and explicitly loaded indentation retain their distinct editing origins. */
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

    @Test fun loadingUntrustedAndCopiedCodeNeverInfersFourSpaceDeletionUnits() = onMain {
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
            assertEquals("main();", input.text.toString())
            nativeHistory(input, android.R.id.undo)
            assertEquals(main, input.text.toString())
            assertEquals(listOf(0), starts(requireNotNull(input.text)))
            // Reopen directly from the saved draft. Do not ask the original project
            // for a snapshot first: that could conceal a missing post-undo save.
            val immediatelyRestored = Fixture("").input
            CppLessonProjectEditor(context, block, key, immediatelyRestored) { }
            assertEquals(main, immediatelyRestored.text.toString())
            assertEquals(listOf(0), starts(requireNotNull(immediatelyRestored.text)))
            immediatelyRestored.setSelection(4)
            backspace(immediatelyRestored)
            assertEquals("main();", immediatelyRestored.text.toString())
            input.setSelection(4)
            backspace(input)
            assertEquals("main();", input.text.toString())
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
            assertEquals("helper();", restoredInput.text.toString())
            val saved = JSONObject(requireNotNull(preferences.getString("$key/project", null)))
            assertTrue("The existing draft retains optional predefined-space provenance per file", saved.has("indentationV2"))

            val legacy = JSONObject().put("files", JSONArray()
                .put(JSONObject().put("path", "main.cpp").put("code", main))
                .put(JSONObject().put("path", "helper.cpp").put("code", "\thelper();")))
                .put("sources", JSONArray().put("main.cpp").put("helper.cpp"))
                .put("selected", "main.cpp")
                .put("indentation", JSONObject().put("main.cpp", CppIndentationUnits.snapshot(
                    CppIndentationUnits.loadedText(main, seedPredefined = true))))
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
            assertEquals("main();", legacyInput.text.toString())
            assertEquals(original, block.toString())
        } finally {
            assertTrue(preferences.edit().remove("$key/project").commit())
        }
    }

    @Test fun explicitlyPredefinedUnitsAreNonMetricAndDeleteOneLevelWithoutConvertingTheirCharacters() = onMain {
        val code = "        return 0;\n\t    helper();"
        val prepared = CppIndentationUnits.loadedText(code, seedPredefined = true)
        assertEquals(code, prepared.toString())
        val marks = (prepared as Spanned).getSpans(0, prepared.length, Any::class.java)
        assertEquals(3, marks.size)
        assertTrue(marks.none { it is NoCopySpan || it is MetricAffectingSpan || it is ReplacementSpan })
        val fixture = Fixture("")
        val input = fixture.input
        input.setText(prepared)
        CppSyntaxHighlighter.applyTo(requireNotNull(input.text))
        fixture.settle()
        assertEquals(listOf(0, 4, code.indexOf('\t') + 1), starts(requireNotNull(input.text)))
        input.setSelection(8)
        backspace(input)
        assertEquals("    return 0;\n\t    helper();", input.text.toString())
        assertEquals(4, input.selectionStart)
        backspace(input)
        assertEquals("return 0;\n\t    helper();", input.text.toString())
        assertEquals(0, input.selectionStart)
        input.setSelection(requireNotNull(input.text).indexOf('\t') + 5)
        backspace(input)
        assertEquals("return 0;\n\thelper();", input.text.toString())
        assertEquals(emptyList<Int>(), starts(requireNotNull(input.text)))
    }

    @Test fun predefinedUnitPlusThreeManualSpacesRemainIndependentUntilExplicitTabCompletesThem() = onMain {
        val input = Fixture("").input
        input.setText(CppIndentationUnits.loadedText("    value;", seedPredefined = true))
        input.setSelection(4)
        requireNotNull(input.text).insert(4, "   ")
        input.setSelection(7)
        assertEquals(listOf(0), starts(requireNotNull(input.text)))
        for (remaining in 2 downTo 0) {
            backspace(input)
            assertEquals(" ".repeat(4 + remaining) + "value;", input.text.toString())
            assertEquals(4 + remaining, input.selectionStart)
        }
        backspace(input)
        assertEquals("value;", input.text.toString())

        input.setText(CppIndentationUnits.loadedText("    value;", seedPredefined = true))
        input.setSelection(4)
        requireNotNull(input.text).insert(4, "   ")
        input.setSelection(7)
        CppCodeInputRules.insertTab(input)
        assertEquals("    \tvalue;", input.text.toString())
        backspace(input)
        assertEquals("    value;", input.text.toString())
        backspace(input)
        assertEquals("value;", input.text.toString())
    }

    @Test fun restoredMetadataPreservesOnlyVerifiedOffsetsAndNeverInfersLegacyOrInvalidOrigins() = onMain {
        val code = "    value;\nplain    tail;"
        val valid = CppIndentationUnits.snapshot(CppIndentationUnits.loadedText(code, seedPredefined = true))
        val input = Fixture("").input
        input.setText(CppIndentationUnits.loadedText(code, valid))
        assertEquals(listOf(0), starts(requireNotNull(input.text)))
        input.setSelection(4)
        backspace(input)
        assertEquals("value;\nplain    tail;", input.text.toString())
        val invalid = listOf("not JSON",
            JSONObject(valid).put("version", 99).toString(),
            JSONObject(valid).put("version", 1.5).toString(),
            JSONObject(valid).put("sha256", "0".repeat(64)).toString(),
            JSONObject(valid).put("starts", JSONArray().put(0).put(0)).toString(),
            JSONObject(valid).put("starts", JSONArray().put(-1)).toString(),
            JSONObject(valid).put("starts", JSONArray().put(code.length)).toString(),
            JSONObject(valid).put("starts", JSONArray().put(code.indexOf("    tail"))).toString(),
            JSONObject(valid).put("starts", JSONArray().put("0")).toString())
        for (saved in invalid) {
            val restored = CppIndentationUnits.loadedText(code, saved, seedPredefined = true)
            assertEquals(code, restored.toString())
            assertEquals("Invalid metadata cannot authorize a fresh inference", emptyList<Int>(), starts(restored))
        }
        assertEquals(emptyList<Int>(), starts(CppIndentationUnits.loadedText(" $code", valid)))
        val manual = CppIndentationUnits.snapshot(code)
        assertEquals(emptyList<Int>(), starts(CppIndentationUnits.loadedText(code, manual, seedPredefined = true)))
        assertEquals(emptyList<Int>(), starts(CppIndentationUnits.loadedText(code)))
    }

    @Test fun interiorEditsSelectionReplacementAndForwardDeleteInvalidateOnlyAffectedOrigins() = onMain {
        val input = Fixture("").input
        val code = "        first;\n    second;"
        val second = code.indexOf("    second")
        input.setText(CppIndentationUnits.loadedText(code, seedPredefined = true))
        input.setSelection(2)
        requireNotNull(input.text).insert(2, " ")
        assertEquals(listOf(5, second + 1), starts(requireNotNull(input.text)))
        input.setSelection(5, 9)
        val connection = requireNotNull(input.onCreateInputConnection(EditorInfo()))
        try { assertTrue(connection.commitText("", 1)) }
        finally { connection.closeConnection() }
        assertEquals("     first;\n    second;", input.text.toString())
        assertEquals(listOf(second - 3), starts(requireNotNull(input.text)))

        input.setText(CppIndentationUnits.loadedText("    value;", seedPredefined = true))
        input.setSelection(0)
        val forward = requireNotNull(input.onCreateInputConnection(EditorInfo()))
        try { assertTrue(forward.deleteSurroundingText(0, 1)) }
        finally { forward.closeConnection() }
        assertEquals("   value;", input.text.toString())
        assertEquals(emptyList<Int>(), starts(requireNotNull(input.text)))
    }

    @Test fun enterCopiesOnlyExistingPredefinedOriginsAndKeepsRealBlockTabsAsSeparateUnits() = onMain {
        for (predefined in listOf(true, false)) {
            val input = Fixture("").input
            input.setText(CppIndentationUnits.loadedText("    value;", seedPredefined = predefined))
            input.setSelection(input.length())
            requireNotNull(input.text).insert(input.length(), "\n")
            assertEquals("    value;\n    ", input.text.toString())
            input.setSelection(input.length())
            backspace(input)
            assertEquals(if (predefined) "    value;\n" else "    value;\n   ", input.text.toString())

            input.setText(CppIndentationUnits.loadedText("    {}", seedPredefined = predefined))
            input.setSelection(5)
            requireNotNull(input.text).insert(5, "\n")
            assertEquals("    {\n    \t\n    }", input.text.toString())
            val childStart = requireNotNull(input.text).indexOf('\n') + 1
            val closingStart = requireNotNull(input.text).lastIndexOf('\n') + 1
            assertEquals(if (predefined) listOf(0, childStart, closingStart) else emptyList<Int>(), starts(requireNotNull(input.text)))
            input.setSelection(childStart + 5)
            backspace(input)
            assertEquals("    {\n    \n    }", input.text.toString())
            backspace(input)
            assertEquals(if (predefined) "    {\n\n    }" else "    {\n   \n    }", input.text.toString())
        }
    }

    @Test fun predefinedIndentationNeverGroupsSignificantSpacesInsideRawOrContinuedLiterals() = onMain {
        val code = "void sample() {\n" +
            "    auto raw = R\"tag(\n" +
            "    raw content\n" +
            "    )tag\";\n" +
            "    auto continued = \"first\\\n" +
            "    second\";\n" +
            "    action();\n}"
        val rawPrefix = code.indexOf("    raw content")
        val quotedPrefix = code.indexOf("    second")
        val expected = listOf(code.indexOf("    auto raw"), code.indexOf("    auto continued"),
            code.indexOf("    action"))
        val prepared = CppIndentationUnits.loadedText(code, seedPredefined = true)
        assertEquals(code, prepared.toString())
        assertEquals(expected, starts(prepared))
        val input = Fixture("").input
        for (prefix in listOf(rawPrefix, quotedPrefix)) {
            input.setText(prepared)
            input.setSelection(prefix + 4)
            backspace(input)
            assertEquals(code.removeRange(prefix + 3, prefix + 4), input.text.toString())
            assertEquals(prefix + 3, input.selectionStart)
        }
        input.setText(prepared)
        val external = expected.last()
        input.setSelection(external + 4)
        backspace(input)
        assertEquals(code.removeRange(external, external + 4), input.text.toString())
        val invalidLiteral = JSONObject(CppIndentationUnits.snapshot(prepared))
            .put("starts", JSONArray().put(rawPrefix)).toString()
        assertEquals(emptyList<Int>(), starts(CppIndentationUnits.loadedText(code, invalidLiteral)))
    }

    @Test fun nativeUndoAndRedoRestoreAuthoredUnitOriginsAndAtomicDeletionRepeatedly() = onMain {
        val code = "#include <iostream>\n\nint main() {\n    std::cout << \"Zona inicial\";\n    return 0;\n}"
        for (token in listOf("std::cout", "return")) {
            val input = Fixture("").input
            input.setText(CppIndentationUnits.loadedText(code, seedPredefined = true))
            val caret = code.indexOf(token)
            val unit = caret - 4
            val withoutUnit = code.removeRange(unit, caret)
            val authoredStarts = starts(requireNotNull(input.text))
            input.setSelection(caret)
            backspace(input)
            assertEquals(withoutUnit, input.text.toString())
            assertEquals(unit, input.selectionStart)

            repeat(3) {
                nativeHistory(input, android.R.id.undo)
                assertEquals("One native undo restores all four authored characters", code, input.text.toString())
                assertEquals(caret, input.selectionStart)
                assertEquals(caret, input.selectionEnd)
                assertEquals("Undo restores provenance as well as text", authoredStarts, starts(requireNotNull(input.text)))
                nativeHistory(input, android.R.id.redo)
                assertEquals(withoutUnit, input.text.toString())
                assertEquals(unit, input.selectionStart)
                nativeHistory(input, android.R.id.undo)
                assertEquals(code, input.text.toString())
                backspace(input)
                assertEquals("Backspace remains a whole unit after every undo", withoutUnit, input.text.toString())
                assertEquals(unit, input.selectionStart)
                assertEquals(unit, input.selectionEnd)
            }
        }
    }

    @Test fun nativeUndoNeverPromotesFourManualSpacesToAnAuthoredUnit() = onMain {
        val input = Fixture("value;").input
        input.setSelection(0)
        val connection = requireNotNull(input.onCreateInputConnection(EditorInfo()))
        try { assertTrue(connection.commitText("    ", 1)) }
        finally { connection.closeConnection() }
        assertEquals("    value;", input.text.toString())
        assertEquals(emptyList<Int>(), starts(requireNotNull(input.text)))
        backspace(input)
        assertEquals("   value;", input.text.toString())
        repeat(3) {
            nativeHistory(input, android.R.id.undo)
            assertEquals("    value;", input.text.toString())
            assertEquals(4, input.selectionStart)
            assertEquals(emptyList<Int>(), starts(requireNotNull(input.text)))
            nativeHistory(input, android.R.id.redo)
            assertEquals("   value;", input.text.toString())
            nativeHistory(input, android.R.id.undo)
            backspace(input)
            assertEquals("Undo must not infer an origin from four consecutive spaces", "   value;", input.text.toString())
            assertEquals(3, input.selectionStart)
        }
    }

    @Test fun nativeUndoOfASelectionReplacementRestoresOnlyItsOriginalPredefinedUnits() = onMain {
        val code = "    first();\n    second();"
        for ((start, end) in listOf(0 to 4, 4 to 0)) {
            val input = Fixture("").input
            input.setText(CppIndentationUnits.loadedText(code, seedPredefined = true))
            input.setSelection(start, end)
            val connection = requireNotNull(input.onCreateInputConnection(EditorInfo()))
            try { assertTrue(connection.commitText("x", 1)) }
            finally { connection.closeConnection() }
            assertEquals("xfirst();\n    second();", input.text.toString())
            assertEquals(1, input.selectionStart)
            assertEquals(1, input.selectionEnd)
            nativeHistory(input, android.R.id.undo)
            assertEquals(code, input.text.toString())
            assertEquals(listOf(0, code.indexOf("    second")), starts(requireNotNull(input.text)))
            nativeHistory(input, android.R.id.redo)
            assertEquals("xfirst();\n    second();", input.text.toString())
            assertEquals(listOf("xfirst();\n".length), starts(requireNotNull(input.text)))
            nativeHistory(input, android.R.id.undo)
            input.setSelection(4)
            backspace(input)
            assertEquals("first();\n    second();", input.text.toString())
            assertEquals(0, input.selectionStart)
        }
    }

    @Test fun nativeUndoRestoresExplicitTabAndItsManualPrefixWithoutChangingEitherOrigin() = onMain {
        for (spaces in 1..3) {
            val input = Fixture(" ".repeat(spaces) + "value;").input
            input.setSelection(spaces)
            CppCodeInputRules.insertTab(input)
            assertEquals("\tvalue;", input.text.toString())
            assertEquals(1, input.selectionStart)
            backspace(input)
            assertEquals("value;", input.text.toString())
            repeat(2) {
                nativeHistory(input, android.R.id.undo)
                assertEquals("\tvalue;", input.text.toString())
                assertEquals(1, input.selectionStart)
                nativeHistory(input, android.R.id.redo)
                assertEquals("value;", input.text.toString())
                nativeHistory(input, android.R.id.undo)
                backspace(input)
                assertEquals("A restored real TAB still deletes in one native operation", "value;", input.text.toString())
            }
            nativeHistory(input, android.R.id.undo)
            nativeHistory(input, android.R.id.undo)
            assertEquals("Undoing TAB completion restores its original manual prefix", " ".repeat(spaces) + "value;", input.text.toString())
            assertEquals(spaces, input.selectionStart)
            assertEquals(emptyList<Int>(), starts(requireNotNull(input.text)))
            backspace(input)
            assertEquals(" ".repeat(spaces - 1) + "value;", input.text.toString())
        }
    }

    @Test fun nativeHistoryDistinguishesIdenticalSourceWithAuthoredAndManualIndentationOrigins() = onMain {
        val code = "    value;"
        for ((start, end) in listOf(0 to 4, 4 to 0)) {
            val input = Fixture("").input
            input.setText(CppIndentationUnits.loadedText(code, seedPredefined = true))
            input.setSelection(start, end)
            val connection = requireNotNull(input.onCreateInputConnection(EditorInfo()))
            try { assertTrue(connection.commitText("    ", 1)) }
            finally { connection.closeConnection() }
            assertEquals("The replacement intentionally preserves every source character", code, input.text.toString())
            assertEquals("Replacement characters have manual origin", emptyList<Int>(), starts(requireNotNull(input.text)))
            assertEquals(4, input.selectionStart)

            nativeHistory(input, android.R.id.undo)
            assertEquals(code, input.text.toString())
            assertEquals("Undo must select the original origin, not any matching text hash", listOf(0), starts(requireNotNull(input.text)))
            nativeHistory(input, android.R.id.redo)
            assertEquals(code, input.text.toString())
            assertEquals("Redo restores manually entered spaces even though source bytes match", emptyList<Int>(), starts(requireNotNull(input.text)))
            input.setSelection(4)
            backspace(input)
            assertEquals("   value;", input.text.toString())
            nativeHistory(input, android.R.id.undo)
            assertEquals(code, input.text.toString())
            assertEquals(emptyList<Int>(), starts(requireNotNull(input.text)))
            nativeHistory(input, android.R.id.undo)
            assertEquals(code, input.text.toString())
            assertEquals(listOf(0), starts(requireNotNull(input.text)))
            input.setSelection(4)
            backspace(input)
            assertEquals("value;", input.text.toString())
        }
    }

    @Test fun nativeBatchUndoRestoresAuthoredOriginsAfterRepeatedIdenticalManualRetyping() = onMain {
        val code = "    value;"
        val input = Fixture("").input
        input.setText(CppIndentationUnits.loadedText(code, seedPredefined = true))
        val connection = requireNotNull(input.onCreateInputConnection(EditorInfo()))
        try {
            assertTrue(connection.beginBatchEdit())
            repeat(2) {
                input.setSelection(0, 4)
                assertTrue(connection.commitText("", 1))
                assertEquals("value;", input.text.toString())
                assertTrue(connection.commitText("    ", 1))
                assertEquals(code, input.text.toString())
                assertEquals(emptyList<Int>(), starts(requireNotNull(input.text)))
            }
        } finally {
            connection.endBatchEdit()
            connection.closeConnection()
        }
        nativeHistory(input, android.R.id.undo)
        assertEquals("One Android batch is undone as one native edit", code, input.text.toString())
        assertEquals("Repeated equal snapshots must not hide the batch's original provenance", listOf(0), starts(requireNotNull(input.text)))
        nativeHistory(input, android.R.id.redo)
        assertEquals(code, input.text.toString())
        assertEquals(emptyList<Int>(), starts(requireNotNull(input.text)))
        nativeHistory(input, android.R.id.undo)
        input.setSelection(4)
        backspace(input)
        assertEquals("value;", input.text.toString())
    }

    @Test fun anEditAfterNativeUndoDiscardsRedoProvenanceWithoutChangingManualSpaceRules() = onMain {
        val code = "    value;"
        val input = Fixture("").input
        input.setText(CppIndentationUnits.loadedText(code, seedPredefined = true))
        input.setSelection(4)
        backspace(input)
        assertEquals("value;", input.text.toString())
        nativeHistory(input, android.R.id.undo)
        assertEquals(code, input.text.toString())
        assertEquals(listOf(0), starts(requireNotNull(input.text)))
        input.setSelection(0, 4)
        val connection = requireNotNull(input.onCreateInputConnection(EditorInfo()))
        try { assertTrue(connection.commitText("   ", 1)) }
        finally { connection.closeConnection() }
        assertEquals("   value;", input.text.toString())
        assertEquals(emptyList<Int>(), starts(requireNotNull(input.text)))
        // TextView consumes the menu command even when its redo stack is empty.
        // Observe the actual source/origin state rather than its boolean result.
        input.onTextContextMenuItem(android.R.id.redo)
        assertEquals("The discarded deletion branch must not run", "   value;", input.text.toString())
        assertEquals(emptyList<Int>(), starts(requireNotNull(input.text)))
        assertEquals(3, input.selectionStart)
        backspace(input)
        assertEquals("  value;", input.text.toString())
        assertEquals(2, input.selectionStart)
        nativeHistory(input, android.R.id.undo)
        nativeHistory(input, android.R.id.undo)
        assertEquals(code, input.text.toString())
        assertEquals("Undo of the new branch restores the original authored unit", listOf(0), starts(requireNotNull(input.text)))
        input.setSelection(4)
        backspace(input)
        assertEquals("value;", input.text.toString())
    }

    @Test fun nativeHistoryReplaysAutoPairsAndEnterIndentationWithoutApplyingTheRulesTwice() = onMain {
        for ((opening, pair) in listOf("(" to "()", "[" to "[]", "{" to "{}", "\"" to "\"\"", "'" to "''")) {
            val input = Fixture("").input
            input.setSelection(0)
            val connection = requireNotNull(input.onCreateInputConnection(EditorInfo()))
            try { assertTrue(connection.commitText(opening, 1)) }
            finally { connection.closeConnection() }
            assertEquals(pair, input.text.toString())
            repeat(3) {
                nativeHistory(input, android.R.id.undo)
                assertEquals("Undo removes the original automatic pair as one native operation", "", input.text.toString())
                nativeHistory(input, android.R.id.redo)
                assertEquals("Redo must replay the recorded pair without appending another closing character", pair, input.text.toString())
                assertEquals(emptyList<Int>(), starts(requireNotNull(input.text)))
            }
        }

        val input = Fixture("").input
        val original = "    {}"
        val expanded = "    {\n    \t\n    }"
        input.setText(CppIndentationUnits.loadedText(original, seedPredefined = true))
        input.setSelection(5)
        val connection = requireNotNull(input.onCreateInputConnection(EditorInfo()))
        try { assertTrue(connection.commitText("\n", 1)) }
        finally { connection.closeConnection() }
        assertEquals(expanded, input.text.toString())
        val expandedStarts = listOf(0, expanded.indexOf('\n') + 1, expanded.lastIndexOf('\n') + 1)
        assertEquals(expandedStarts, starts(requireNotNull(input.text)))
        repeat(3) {
            nativeHistory(input, android.R.id.undo)
            assertEquals(original, input.text.toString())
            assertEquals(listOf(0), starts(requireNotNull(input.text)))
            nativeHistory(input, android.R.id.redo)
            assertEquals("Redo must not generate additional lines or indentation", expanded, input.text.toString())
            assertEquals(expandedStarts, starts(requireNotNull(input.text)))
        }
    }

    @Test fun nativeNoOpClosingDelimiterSkipPreservesOriginsAndDiscardsTheOldRedoBranch() = onMain {
        val code = "    ()"
        for (skipAfterUndo in listOf(false, true)) {
            val input = Fixture("").input
            input.setText(CppIndentationUnits.loadedText(code, seedPredefined = true))
            if (skipAfterUndo) {
                // Build an identical-text redo state whose indentation is manual.
                input.setSelection(0, 4)
                val replacement = requireNotNull(input.onCreateInputConnection(EditorInfo()))
                try { assertTrue(replacement.commitText("    ", 1)) }
                finally { replacement.closeConnection() }
                assertEquals(code, input.text.toString())
                assertEquals(emptyList<Int>(), starts(requireNotNull(input.text)))
                nativeHistory(input, android.R.id.undo)
                assertEquals(code, input.text.toString())
                assertEquals(listOf(0), starts(requireNotNull(input.text)))
            }
            input.setSelection(5)
            val connection = requireNotNull(input.onCreateInputConnection(EditorInfo()))
            try { assertTrue(connection.commitText(")", 1)) }
            finally { connection.closeConnection() }
            assertEquals("Skipping an existing delimiter changes neither source nor origins", code, input.text.toString())
            assertEquals(6, input.selectionStart)
            assertEquals(listOf(0), starts(requireNotNull(input.text)))
            if (skipAfterUndo) {
                input.onTextContextMenuItem(android.R.id.redo)
                assertEquals("The new skip must discard the previous manual-origin redo branch", code, input.text.toString())
                assertEquals(listOf(0), starts(requireNotNull(input.text)))
                assertEquals(6, input.selectionStart)
            } else {
                repeat(2) {
                    nativeHistory(input, android.R.id.undo)
                    assertEquals(code, input.text.toString())
                    assertEquals(listOf(0), starts(requireNotNull(input.text)))
                    nativeHistory(input, android.R.id.redo)
                    assertEquals(code, input.text.toString())
                    assertEquals(listOf(0), starts(requireNotNull(input.text)))
                }
            }
            input.setSelection(4)
            backspace(input)
            assertEquals("A no-op native transaction cannot erase the predefined TAB identity", "()", input.text.toString())
            assertEquals(0, input.selectionStart)
        }
    }

    private fun starts(text: CharSequence): List<Int> {
        val values = JSONObject(CppIndentationUnits.snapshot(text)).getJSONArray("starts")
        return (0 until values.length()).map { values.getInt(it) }
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

    private fun nativeHistory(input: FreeScrollEditText, action: Int) {
        assertTrue("The native TextView history handles undo/redo without a test-owned history",
            input.onTextContextMenuItem(action))
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
