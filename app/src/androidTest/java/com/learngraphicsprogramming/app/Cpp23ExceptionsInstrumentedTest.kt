package com.learngraphicsprogramming.app

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.runtime.CppExecutionEngine
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/**
 * Experimental C++ exception handling, never used by the default compiler.
 *
 * Requires the official WASI SDK 34 wasm32-wasip1/eh headers/libs, Clang 23,
 * and the Wasmtime runtime. Compile/link/run results are checked separately.
 *
 * Run only with all three opt-in properties:
 * .\gradlew :app:connectedDebugAndroidTest "-Plgp.clang23=true" "-Plgp.wasmtime=true" "-Plgp.cppExceptions=true" "-Pandroid.testInstrumentationRunnerArguments.class=com.learngraphicsprogramming.app.Cpp23ExceptionsInstrumentedTest"
 */
@RunWith(AndroidJUnit4::class)
class Cpp23ExceptionsInstrumentedTest {

    @Before
    fun requireExperimentalToolchain() {
        assertTrue("Expected Clang 23", BuildConfig.LGP_CLANG23_ENABLED)
        assertTrue("Expected Wasmtime", BuildConfig.LGP_WASMTIME_ENABLED)
        assertTrue("Use -Plgp.cppExceptions=true", BuildConfig.LGP_CPP_EXCEPTIONS_ENABLED)
    }

    private fun verifyProgram(source: String, expectedOutput: String) {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val result = CppExecutionEngine(context).compileAndRun(source.trimIndent())
        try {
            val compilation = result.compilation
            assertFalse("Clang 23 exceeded compilation timeout: ${compilation.stderr}", compilation.timedOut)
            assertEquals(
                "Clang 23 / LLD 23 compile/link failed: ${compilation.stderr}\n" +
                    "stdout: ${compilation.stdout}",
                0,
                compilation.exitCode
            )
            assertNotNull("No WASI WebAssembly module generated", compilation.wasmFile)
            val execution = result.execution
            assertNotNull("Wasmtime execution did not run", execution)
            assertEquals("Wasmtime execution failed: ${execution!!.stderr}", 0, execution.exitCode)
            assertEquals("Exception output mismatch", expectedOutput, execution.stdout)
        } finally {
            result.compilation.workspace.deleteRecursively()
        }
    }

    @Test
    fun throwsAndCatchesInteger() {
        verifyProgram(
            """
            #include <iostream>
            int main() {
                try {
                    throw 42;
                } catch (int number) {
                    std::cout << "Excepcion capturada: " << number << '\n';
                }
            }
            """,
            "Excepcion capturada: 42\n"
        )
    }

    @Test
    fun catchesStdRuntimeError() {
        verifyProgram(
            """
            #include <iostream>
            #include <stdexcept>
            int main() {
                try {
                    throw std::runtime_error("mensaje de prueba");
                } catch (const std::exception& error) {
                    std::cout << error.what() << '\n';
                }
            }
            """,
            "mensaje de prueba\n"
        )
    }
}
