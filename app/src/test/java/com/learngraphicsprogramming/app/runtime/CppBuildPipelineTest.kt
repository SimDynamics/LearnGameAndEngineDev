package com.learngraphicsprogramming.app.runtime

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class CppBuildPipelineTest {
    private val steps = listOf(
        CppBuildStep(CppBuildPhase.COMPILATION, "diferencia.cpp", listOf("compile implementation")),
        CppBuildStep(CppBuildPhase.COMPILATION, "main.cpp", listOf("compile client")),
        CppBuildStep(CppBuildPhase.LINK, null, listOf("link both objects"))
    )

    @Test fun compilesEveryUnitBeforeLinkingAndKeepsEarlierDiagnostics() {
        val executed = mutableListOf<CppBuildStep>()
        val result = CppBuildPipeline.run(steps) { step ->
            executed += step
            CppBuildOutput(0, stderr = if (step.sourcePath == "diferencia.cpp") "diferencia.cpp: warning" else "")
        }
        assertEquals(steps, executed)
        assertEquals(0, result.exitCode)
        assertEquals(CppBuildPhase.LINK, result.phase)
        assertEquals(null, result.sourcePath)
        assertEquals("diferencia.cpp: warning", result.stderr)
    }

    @Test fun incompatibleDeclarationStopsCompilationBeforeLinkAndIdentifiesUnit() {
        val executed = mutableListOf<CppBuildStep>()
        val result = CppBuildPipeline.run(steps) { step ->
            executed += step
            CppBuildOutput(1, stderr = "diferencia.cpp:3:1: error: conflicting types")
        }
        assertEquals(listOf(steps.first()), executed)
        assertEquals(CppBuildPhase.COMPILATION, result.phase)
        assertEquals("diferencia.cpp", result.sourcePath)
        assertTrue(result.stderr.contains("diferencia.cpp:3:1"))
    }

    @Test fun failureOfSecondUnitRetainsWarningsAndDoesNotLink() {
        val executed = mutableListOf<CppBuildStep>()
        val result = CppBuildPipeline.run(steps) { step ->
            executed += step
            if (step.sourcePath == "main.cpp") CppBuildOutput(1, stderr = "main.cpp: error")
            else CppBuildOutput(0, stderr = "diferencia.cpp: warning")
        }
        assertEquals(steps.take(2), executed)
        assertEquals("main.cpp", result.sourcePath)
        assertEquals("diferencia.cpp: warning\nmain.cpp: error", result.stderr)
    }

    @Test fun missingDefinitionIsALinkFailureAfterSuccessfulCompilation() {
        val executed = mutableListOf<CppBuildStep>()
        val omittedImplementation = listOf(steps[1], steps[2])
        val result = CppBuildPipeline.run(omittedImplementation) { step ->
            executed += step
            if (step.phase == CppBuildPhase.LINK) CppBuildOutput(1, stderr = "undefined symbol: diferencia")
            else CppBuildOutput(0)
        }
        assertEquals(omittedImplementation, executed)
        assertEquals(CppBuildPhase.LINK, result.phase)
        assertEquals(null, result.sourcePath)
        assertTrue(result.stderr.contains("undefined symbol"))
    }

    @Test fun duplicateDefinitionStaysALinkDiagnostic() {
        val result = CppBuildPipeline.run(steps) { step ->
            if (step.phase == CppBuildPhase.LINK) CppBuildOutput(1, stderr = "duplicate symbol: diferencia")
            else CppBuildOutput(0)
        }
        assertEquals(CppBuildPhase.LINK, result.phase)
        assertTrue(result.stderr.contains("duplicate symbol"))
    }

    @Test fun timedOutUnitCannotReachLinkEvenWithAnInconsistentZeroExitCode() {
        val executed = mutableListOf<CppBuildStep>()
        val result = CppBuildPipeline.run(steps) { step ->
            executed += step
            CppBuildOutput(0, timedOut = true)
        }
        assertEquals(listOf(steps.first()), executed)
        assertTrue(result.timedOut)
        assertEquals(CppBuildPhase.COMPILATION, result.phase)
        assertFalse(executed.any { it.phase == CppBuildPhase.LINK })
    }
}
