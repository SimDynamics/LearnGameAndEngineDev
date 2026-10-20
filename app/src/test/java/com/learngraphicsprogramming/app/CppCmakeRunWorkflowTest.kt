package com.learngraphicsprogramming.app

import com.learngraphicsprogramming.app.runtime.CppCmakePhase
import com.learngraphicsprogramming.app.runtime.CppCmakeProject
import com.learngraphicsprogramming.app.runtime.CppCmakeRecord
import com.learngraphicsprogramming.app.runtime.CppCmakeResult
import com.learngraphicsprogramming.app.runtime.WasiExecutionResult
import org.junit.Assert.*
import org.junit.Test
import java.io.File

/** Control-flow tests; real CMake and program execution remain Android checks. */
class CppCmakeRunWorkflowTest {
    @Test fun unconfiguredProjectConfiguresBuildsAndExecutesInOrderWithItsCurrentInput() {
        val engine = Engine(configured = false)
        val result = engine.run("Rita\n4\n")
        assertEquals(listOf("configure", "build", "execute:Rita\n4\n"), engine.calls)
        assertEquals(listOf(CppCmakePhase.CONFIGURATION, CppCmakePhase.GENERATION,
            CppCmakePhase.BUILD, CppCmakePhase.EXECUTION), result.records.map { it.phase })
        assertEquals("Rita\n4\n", result.executionInput)
        assertEquals("new output:Rita\n4\n", result.execution!!.stdout)
        assertEquals(listOf("configured", "generated", "built"), result.records.take(3).map { it.stdout })
    }

    @Test fun configuredProjectStillBuildsAndExecutesOnEveryRequestedCheck() {
        val engine = Engine(configured = true)
        val first = engine.run("first")
        val second = engine.run("second")
        assertEquals(listOf("build", "execute:first", "build", "execute:second"), engine.calls)
        assertEquals("new output:first", first.execution!!.stdout)
        assertEquals("new output:second", second.execution!!.stdout)
        assertEquals(listOf(CppCmakePhase.BUILD, CppCmakePhase.EXECUTION), second.records.map { it.phase })
    }

    @Test fun configurationErrorsAndTimeoutsNeverReachBuildOrExecution() {
        for (failure in listOf(
            result(CppCmakePhase.CONFIGURATION).copy(exitCode = 1, stderr = "bad description", records = emptyList()),
            result(CppCmakePhase.GENERATION).copy(timedOut = true, stderr = "generation timeout", records = emptyList())
        )) {
            val engine = Engine(configured = false).apply { configuration = failure }
            val actual = engine.run("")
            assertEquals(listOf("configure"), engine.calls)
            assertEquals(failure.phase, actual.phase)
            assertEquals(failure.stderr, actual.records.single().stderr)
            assertEquals(failure.timedOut, actual.records.single().timedOut)
            assertNull(actual.execution)
        }
    }

    @Test fun buildFailureTimeoutAndNonExecutableTargetsDoNotRunOrInventVerdicts() {
        for (failure in listOf(
            result(CppCmakePhase.BUILD).copy(exitCode = 1, stderr = "bad source", records = emptyList()),
            result(CppCmakePhase.BUILD).copy(timedOut = true, records = emptyList()),
            result(CppCmakePhase.BUILD).copy(artifact = null)
        )) {
            val engine = Engine(configured = true).apply { compilation = failure }
            val actual = engine.run("")
            assertEquals(listOf("build"), engine.calls)
            assertEquals(CppCmakePhase.BUILD, actual.phase)
            assertNull(actual.execution)
            assertNull(CppOutputCheck.successfulOutput(actual))
        }
    }

    @Test fun executionFailureRetainsTheSuccessfulPrecedingPhaseRecordsWithoutAValidOutput() {
        val engine = Engine(configured = false).apply { executionExitCode = 2 }
        val result = engine.run("bad input")
        assertEquals(CppCmakePhase.EXECUTION, result.phase)
        assertEquals(2, result.exitCode)
        assertEquals(4, result.records.size)
        assertNull(CppOutputCheck.successfulOutput(result))
    }

    private class Engine(private val configured: Boolean) {
        val calls = mutableListOf<String>()
        private val project = CppCmakeProject(mapOf(
            "CMakeLists.txt" to "project(QA LANGUAGES CXX)\nadd_executable(app main.cpp)\n",
            "main.cpp" to "int main() {}"), "app")
        var configuration = result(CppCmakePhase.GENERATION).copy(records = listOf(
            CppCmakeRecord(CppCmakePhase.CONFIGURATION, 0, stdout = "configured"),
            CppCmakeRecord(CppCmakePhase.GENERATION, 0, stdout = "generated")))
        var compilation = result(CppCmakePhase.BUILD).copy(records = listOf(
            CppCmakeRecord(CppCmakePhase.BUILD, 0, stdout = "built")))
        var executionExitCode = 0

        fun run(input: String) = CppCmakeRunWorkflow.run(project, input,
            isConfigured = { assertSame(project, it); configured },
            configure = { assertSame(project, it); calls += "configure"; configuration },
            build = { assertSame(project, it); calls += "build"; compilation },
            execute = { current, actualInput ->
                assertSame(project, current)
                calls += "execute:$actualInput"
                result(CppCmakePhase.EXECUTION).copy(exitCode = executionExitCode,
                    executionInput = actualInput,
                    execution = WasiExecutionResult(executionExitCode, "new output:$actualInput", ""),
                    records = listOf(CppCmakeRecord(CppCmakePhase.EXECUTION, executionExitCode)))
            })
    }

    companion object {
        private fun result(phase: CppCmakePhase) = CppCmakeResult(phase, 0,
            workspace = File("qa-only-not-created"), artifact = File("qa-only-not-created/app.wasm"),
            projectFingerprint = "current-project")
    }
}
