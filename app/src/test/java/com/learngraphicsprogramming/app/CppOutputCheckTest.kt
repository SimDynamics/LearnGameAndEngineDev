package com.learngraphicsprogramming.app

import com.learngraphicsprogramming.app.runtime.*
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import java.io.File

/** Output checks never infer a student's implementation or edit an exercise answer. */
class CppOutputCheckTest {
    @Test fun absentContractPreservesFreeCompilerAndLegacyBlocks() {
        assertNull(CppOutputCheck.fromBlock(JSONObject().put("type", "cpp_compiler")))
    }

    @Test fun exactOutputKeepsEveryBoundaryCharacterAndAllowsAnEmptyProgramOutput() {
        val spec = spec("" to listOf("A B", ""))
        for (output in listOf(" A B", "A B ", "A  B", "A\tB", "A B\n", "A B\r\n"))
            assertFalse(output, verdict(spec, "", output).correct)
        assertTrue(verdict(spec, "", "A B").correct)
        assertTrue(verdict(spec, "", "").correct)
    }

    @Test fun onlyExplicitOutputAlternativesAdmitFinalLfAndRetainInternalSeparators() {
        for (expected in listOf("Informe\nZona Este\nFin", "Taller\nTurno B\nMaterial preparado", "Taller\nTurno C\nMaterial preparado")) {
            val spec = spec("" to listOf(expected, "$expected\n"))
            assertTrue(verdict(spec, "", expected).correct)
            assertTrue(verdict(spec, "", "$expected\n").correct)
            for (wrong in listOf("$expected\n\n", "$expected ", expected.replace("\n", " "), expected.replace("\n", "\r\n")))
                assertFalse(wrong, verdict(spec, "", wrong).correct)
        }
    }

    @Test fun technicalInputTokensUseOnlyCppAsciiWhitespaceAndNeverOtherCasesOutputs() {
        val spec = spec("Rita 4" to listOf("Rita: 7"), "Leo 2" to listOf("Leo: 5"), mode = "tokens")
        for (input in listOf("Leo 2", "Leo\n2\n", " \tLeo\u000B2\u000C\r")) {
            assertTrue(verdict(spec, input, "Leo: 5").correct)
            assertFalse(verdict(spec, input, "Rita: 7").correct)
        }
        assertNull(spec.caseFor("Leo\u00A02"))
        assertNull(spec.caseFor("Leo 2 3"))
        assertNull(spec("Leo 2" to listOf("Leo: 5")).caseFor("Leo\n2"))
    }

    @Test fun plainVerdictsAndIndependentOptionalFeedbackNeverInventReasons() {
        val value = contract("" to listOf("Caja recibida"))
        val target = value.getJSONObject("outputCheck").getJSONArray("cases").getJSONObject(0)
        target.put("reviewFeedback", JSONArray().put(JSONObject().put("outputs", JSONArray(listOf("Zona segura")))
            .put("text", "Revisa si realizaste y ejecutaste la segunda modificación.")))
        val spec = CppOutputCheck.fromBlock(value)!!
        assertEquals(CppOutputCheck.Verdict(true, null), verdict(spec, "", "Caja recibida"))
        assertEquals(CppOutputCheck.Verdict(false, null), verdict(spec, "", "Zona inicial"))
        assertEquals("Revisa si realizaste y ejecutaste la segunda modificación.", verdict(spec, "", "Zona segura").explanation)
        target.put("explanation", "Las comillas delimitan el literal y no forman parte del mensaje.")
        assertEquals(target.getString("explanation"), verdict(CppOutputCheck.fromBlock(value)!!, "", "Caja recibida").explanation)
        assertNull(verdict(CppOutputCheck.fromBlock(value)!!, "", "Otra salida").explanation)
    }

    @Test fun unknownInputHasNoVerdictRatherThanJudgingTheProgramIncorrect() {
        val session = CppOutputCheck.Session(spec("Rita 4" to listOf("Rita: 7")))
        val ticket = session.begin(snapshot(), "Mia 3")
        assertFalse(session.complete(ticket, snapshot(), "Mia 3", "Mia: 6"))
        assertFalse(session.canCheck)
        assertNull(session.check())
    }

    @Test fun editsUndoRedoAndNewRunsInvalidateEvenByteIdenticalLateResults() {
        val spec = spec("" to listOf("OK"))
        val session = CppOutputCheck.Session(spec)
        val original = snapshot()
        val obsolete = session.begin(original, "")
        session.observe(snapshot("changed"), "")
        session.observe(original, "") // Undo restores bytes, never the old execution ticket.
        assertFalse(session.complete(obsolete, original, "", "OK"))
        val first = session.begin(original, "")
        val second = session.begin(original, "")
        assertFalse(session.complete(first, original, "", "OK"))
        assertTrue(session.complete(second, original, "", "OK"))
        assertTrue(session.check()!!.correct)
        session.observe(snapshot("redo"), "")
        assertNull(session.check())
        session.observe(original, "")
        assertNull(session.check())
        assertFalse(CppOutputCheck.Session(spec).canCheck) // Reopening never revives a prior run.
    }

    @Test fun fileContentsSelectionCompilerTargetAndInputAllBelongToTheExecutionIdentity() {
        val original = snapshot()
        val modifications = listOf(
            original.copy(files = original.files + ("helper.hpp" to "changed")),
            original.copy(files = original.files + ("unused.hpp" to "new")),
            original.copy(sourceFiles = listOf("other.cpp")),
            original.copy(compiler = "clang21"),
            original.copy(configuration = "target=other;option=1"))
        for (changed in modifications) {
            val session = CppOutputCheck.Session(spec("" to listOf("OK")))
            val ticket = session.begin(original, "")
            assertTrue(session.observe(changed, ""))
            assertFalse(session.complete(ticket, changed, "", "OK"))
            assertFalse(session.canCheck)
        }
        val session = CppOutputCheck.Session(spec("" to listOf("OK")))
        val ticket = session.begin(original, "")
        assertFalse(session.complete(ticket, original, "different", "OK"))
    }

    @Test fun leavingACachedPageDropsBothTheVerdictAndAnUnfinishedRunEvenWithUnchangedCode() {
        val session = CppOutputCheck.Session(spec("" to listOf("OK")))
        val current = snapshot()
        val unfinished = session.begin(current, "")
        session.invalidate() // Detach, followed by reattaching exactly the same page Views.
        assertFalse(session.observe(current, ""))
        assertFalse(session.complete(unfinished, current, "", "OK"))
        assertNull(session.check())
        val fresh = session.begin(current, "")
        assertTrue(session.complete(fresh, current, "", "OK"))
        assertTrue(session.check()!!.correct)
        session.invalidate()
        assertNull(session.check())
        assertFalse(session.complete(fresh, current, "", "OK"))
    }

    @Test fun snapshotsDefensivelyCopyTheMutableProjectAndTranslationUnits() {
        val files = linkedMapOf("main.cpp" to "old")
        val units = mutableListOf("main.cpp")
        val captured = CppOutputCheck.snapshot(files, units, "clang23")
        files["main.cpp"] = "new"
        files["extra.cpp"] = "extra"
        units.clear()
        assertEquals(mapOf("main.cpp" to "old"), captured.files)
        assertEquals(listOf("main.cpp"), captured.sourceFiles)
    }

    @Test fun technicalFailuresNeverKeepSuccessfulStdoutOrProduceAnIncorrectVerdict() {
        val workspace = File("qa-only-not-materialized")
        val compiled = CppCompilationResult(0, "", "", File(workspace, "a.wasm"), workspace, 1)
        val executed = WasiExecutionResult(0, "OK", "")
        assertEquals("OK", CppOutputCheck.successfulOutput(CppEngineResult(compiled, executed)))
        val failures = listOf(
            CppEngineResult(compiled.copy(exitCode = 1), executed),
            CppEngineResult(compiled.copy(phase = CppBuildPhase.LINK, exitCode = 1), executed),
            CppEngineResult(compiled.copy(timedOut = true), executed),
            CppEngineResult(compiled, executed.copy(exitCode = 2)),
            CppEngineResult(compiled, null))
        for (failure in failures) {
            assertNull(CppOutputCheck.successfulOutput(failure))
            val session = CppOutputCheck.Session(spec("" to listOf("OK")))
            var ticket = session.begin(snapshot(), "")
            assertTrue(session.complete(ticket, snapshot(), "", "OK"))
            ticket = session.begin(snapshot(), "")
            assertFalse(session.complete(ticket, snapshot(), "", CppOutputCheck.successfulOutput(failure)))
            assertNull(session.check())
        }
    }

    @Test fun cmakeOnlyAcceptsTheCurrentSuccessfulExecutionPhase() {
        val value = CppCmakeResult(CppCmakePhase.EXECUTION, 0, workspace = File("qa-only"),
            execution = WasiExecutionResult(0, "OK", ""), projectFingerprint = "qa")
        assertEquals("OK", CppOutputCheck.successfulOutput(value))
        for (phase in CppCmakePhase.entries.filter { it != CppCmakePhase.EXECUTION })
            assertNull(CppOutputCheck.successfulOutput(value.copy(phase = phase)))
        assertNull(CppOutputCheck.successfulOutput(value.copy(exitCode = 1)))
        assertNull(CppOutputCheck.successfulOutput(value.copy(timedOut = true)))
        assertNull(CppOutputCheck.successfulOutput(value.copy(execution = null)))
        assertNull(CppOutputCheck.successfulOutput(value.copy(execution = value.execution!!.copy(exitCode = 1))))
    }

    @Test fun malformedOrAmbiguousContractsFailClosedWithoutWeakeningExistingValidation() {
        val invalid = listOf(
            JSONObject().put("outputCheck", "no"),
            contract("" to listOf("OK")).apply { getJSONObject("outputCheck").put("inputComparison", "trim") },
            contract("" to listOf("OK")).apply { getJSONObject("outputCheck").put("unknown", true) },
            contract("" to listOf("OK", "OK")),
            contract("" to emptyList()),
            contract("a b" to listOf("1"), "a\nb" to listOf("2"), mode = "tokens"),
            contract("" to listOf("OK")).apply { getJSONObject("outputCheck").getJSONArray("cases").getJSONObject(0)
                .put("reviewFeedback", JSONArray().put(JSONObject().put("outputs", JSONArray(listOf("OK"))).put("text", "wrong"))) })
        invalid.forEach { value ->
            try { CppOutputCheck.fromBlock(value); fail("Must reject $value") }
            catch (_: IllegalArgumentException) { }
        }
    }

    private fun snapshot(code: String = "source") = CppOutputCheck.snapshot(
        mapOf("main.cpp" to code, "helper.hpp" to "header"), listOf("main.cpp"), "clang23")
    private fun verdict(spec: CppOutputCheck.Specification, input: String, output: String): CppOutputCheck.Verdict {
        val session = CppOutputCheck.Session(spec)
        val ticket = session.begin(snapshot(), input)
        assertTrue(session.complete(ticket, snapshot(), input, output))
        return requireNotNull(session.check())
    }
    private fun spec(vararg pairs: Pair<String, List<String>>, mode: String = "exact") = CppOutputCheck.fromBlock(contract(*pairs, mode = mode))!!
    private fun contract(vararg pairs: Pair<String, List<String>>, mode: String = "exact") = JSONObject().put("type", "cpp_compiler")
        .put("outputCheck", JSONObject().put("inputComparison", mode).put("cases", JSONArray(pairs.map { (input, outputs) ->
            JSONObject().put("standardInput", input).put("outputs", JSONArray(outputs)) })))
}
