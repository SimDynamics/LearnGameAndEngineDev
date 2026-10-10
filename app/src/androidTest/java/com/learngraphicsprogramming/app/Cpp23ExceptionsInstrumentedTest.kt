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
 * End-to-end regression for the default C++23 exception handling mode.
 *
 * Requires WASI SDK 34 wasm32-wasip1/eh, Clang 23, and Wasmtime.
 * Compile/link/run results are checked separately.
 *
 * Run with the new defaults (no extra Gradle toolchain flags):
 * .\gradlew :app:connectedDebugAndroidTest "-Pandroid.testInstrumentationRunnerArguments.class=com.learngraphicsprogramming.app.Cpp23ExceptionsInstrumentedTest"
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

    @Test
    fun propagatesExceptionAcrossThreeFunctions() {
        verifyProgram(
            """
            #include <iostream>

            void deepest() { throw 73; }
            void middle() { deepest(); }
            void outer() { middle(); }

            int main() {
                try {
                    outer();
                } catch (int number) {
                    std::cout << "propagada: " << number << '\n';
                }
            }
            """,
            "propagada: 73\n"
        )
    }

    @Test
    fun destroysLocalObjectsInReverseOrderDuringStackUnwinding() {
        verifyProgram(
            """
            #include <iostream>

            struct Guard {
                const char* label;
                ~Guard() { std::cout << "destructor: " << label << '\n'; }
            };

            void raise() {
                Guard first{"interno-1"};
                Guard second{"interno-2"};
                throw 5;
            }

            int main() {
                try {
                    Guard outside{"externo"};
                    raise();
                } catch (int code) {
                    std::cout << "capturada: " << code << '\n';
                }
            }
            """,
            "destructor: interno-2\ndestructor: interno-1\ndestructor: externo\ncapturada: 5\n"
        )
    }

    @Test
    fun bareRethrowPreservesOriginalException() {
        verifyProgram(
            """
            #include <iostream>
            #include <stdexcept>

            int main() {
                try {
                    try {
                        throw std::runtime_error("mensaje original");
                    } catch (const std::runtime_error& error) {
                        std::cout << "primera: " << error.what() << '\n';
                        throw;
                    }
                } catch (const std::exception& error) {
                    std::cout << "segunda: " << error.what() << '\n';
                }
            }
            """,
            "primera: mensaje original\nsegunda: mensaje original\n"
        )
    }

    @Test
    fun recoversOriginalCauseFromNestedException() {
        verifyProgram(
            """
            #include <exception>
            #include <iostream>
            #include <stdexcept>

            void lowLevel() {
                throw std::runtime_error("causa original");
            }

            void highLevel() {
                try {
                    lowLevel();
                } catch (...) {
                    std::throw_with_nested(std::logic_error("contexto adicional"));
                }
            }

            int main() {
                try {
                    highLevel();
                } catch (const std::exception& error) {
                    std::cout << "externa: " << error.what() << '\n';
                    try {
                        std::rethrow_if_nested(error);
                    } catch (const std::exception& cause) {
                        std::cout << "interna: " << cause.what() << '\n';
                    }
                }
            }
            """,
            "externa: contexto adicional\ninterna: causa original\n"
        )
    }

}
