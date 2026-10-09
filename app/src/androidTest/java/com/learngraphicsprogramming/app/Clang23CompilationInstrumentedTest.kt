package com.learngraphicsprogramming.app

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.runtime.CppEngineResult
import com.learngraphicsprogramming.app.runtime.CppExecutionEngine
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/**
 * End-to-end Android ARM64 verification for the experimental Clang 23 + LLD 23 pair.
 *
 * This is deliberately separate from the existing Clang 21 regression tests.
 * Each case compiles C++ on the phone, links WebAssembly with LLD and then
 * executes the produced module through the selected WAMR/Wasmtime backend.
 *
 * Run the first smoke test before the whole suite:
 *
 * .\gradlew :app:connectedDebugAndroidTest "-Plgp.clang23=true" "-Pandroid.testInstrumentationRunnerArguments.class=com.learngraphicsprogramming.app.Clang23CompilationInstrumentedTest#compilesLinksAndExecutesHelloWorld"
 *
 * Then run the class (WAMR), followed by the class with -Plgp.wasmtime=true.
 * The packaged WASI sysroot currently uses -fno-exceptions; these tests do
 * not claim that C++ exceptions or the entire C++23 library are supported.
 */
@RunWith(AndroidJUnit4::class)
class Clang23CompilationInstrumentedTest {

    @Before
    fun requireClang23() {
        assertTrue("Use -Plgp.clang23=true for this suite", BuildConfig.LGP_CLANG23_ENABLED)
    }

    private fun verifyProgram(
        source: String,
        stdin: String = "",
        validate: (CppEngineResult) -> Unit
    ) {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val result = CppExecutionEngine(context).compileAndRun(source.trimIndent(), stdin)
        try {
            validate(result)
        } finally {
            // Keep repeated device test runs from accumulating compiled WASM files.
            result.compilation.workspace.deleteRecursively()
        }
    }

    private fun assertCompiledAndExecuted(result: CppEngineResult, expectedOutput: String) {
        val compilation = result.compilation
        assertFalse("Clang 23 timed out: ${compilation.stderr}", compilation.timedOut)
        assertEquals(
            "Clang 23 / LLD 23 failed: ${compilation.stderr}\nstdout: ${compilation.stdout}",
            0,
            compilation.exitCode
        )
        val wasmFile = compilation.wasmFile
        assertNotNull("Clang 23 did not produce a WASM file", wasmFile)
        assertTrue("The WASM module is empty", wasmFile!!.length() > 8)
        val magic = ByteArray(4)
        wasmFile.inputStream().use { input ->
            assertEquals("Cannot read the WASM magic", 4, input.read(magic))
        }
        assertTrue(
            "The linker did not produce a WebAssembly module",
            magic.contentEquals(byteArrayOf(0x00, 0x61, 0x73, 0x6d))
        )
        val execution = result.execution
        assertNotNull("WASM execution was skipped", execution)
        assertEquals("WASM execution error: ${execution!!.stderr}", 0, execution.exitCode)
        assertEquals("Unexpected standard output", expectedOutput, execution.stdout)
    }

    @Test
    fun compilesLinksAndExecutesHelloWorld() {
        verifyProgram(
            """
            #include <iostream>
            int main() {
                std::cout << "Clang 23 Android OK" << '\n';
            }
            """
        ) { assertCompiledAndExecuted(it, "Clang 23 Android OK\n") }
    }

    @Test
    fun compilesCppStandardLibraryVectorsAndAlgorithms() {
        verifyProgram(
            """
            #include <algorithm>
            #include <iostream>
            #include <vector>
            int main() {
                std::vector<int> numbers{5, 1, 4, 2, 3};
                std::sort(numbers.begin(), numbers.end());
                for (int n : numbers) std::cout << n;
                std::cout << '\n';
            }
            """
        ) { assertCompiledAndExecuted(it, "12345\n") }
    }

    @Test
    fun compilesCppStandardLibraryMapAndOptional() {
        verifyProgram(
            """
            #include <iostream>
            #include <map>
            #include <optional>
            #include <string>
            int main() {
                std::map<std::string, int> values{{"graphics", 23}};
                std::optional<int> answer = values.at("graphics");
                std::cout << answer.value() << '\n';
            }
            """
        ) { assertCompiledAndExecuted(it, "23\n") }
    }

    @Test
    fun readsIntegersFromRealWasiStandardInput() {
        verifyProgram(
            """
            #include <iostream>
            int main() {
                int a = 0, b = 0;
                if (!(std::cin >> a >> b)) return 4;
                std::cout << (a * b) << '\n';
            }
            """,
            "6 7\n"
        ) { assertCompiledAndExecuted(it, "42\n") }
    }

    @Test
    fun readsUnicodeLineWithSpacesFromRealWasiStandardInput() {
        verifyProgram(
            """
            #include <iostream>
            #include <string>
            int main() {
                std::string line;
                if (!std::getline(std::cin, line)) return 4;
                std::cout << "[" << line << "]" << '\n';
            }
            """,
            "La isla de Zambo y C++23\n"
        ) { assertCompiledAndExecuted(it, "[La isla de Zambo y C++23]\n") }
    }

    @Test
    fun compilesActualCpp23IfConstevalLanguageFeature() {
        verifyProgram(
            """
            #include <iostream>
            constexpr int select(int value) {
                if consteval {
                    return value + 1;
                } else {
                    return value + 2;
                }
            }
            static_assert(select(22) == 23);
            int main() {
                int input = 22;
                std::cout << select(input) << ' ' << select(22) << '\n';
            }
            """
        ) { assertCompiledAndExecuted(it, "24 24\n") }
    }

    @Test
    fun reportsSyntaxErrorsWithoutProducingExecutableWasm() {
        verifyProgram(
            """
            int main() {
                int number = ;
                return number;
            }
            """
        ) { result ->
            val compilation = result.compilation
            assertFalse("Compiler timed out on invalid C++ source", compilation.timedOut)
            assertTrue("Invalid source unexpectedly compiled", compilation.exitCode != 0)
            assertTrue(
                "Missing actionable Clang diagnostic: ${compilation.stderr}",
                compilation.stderr.contains("error:") && compilation.stderr.contains("main.cpp")
            )
            assertNull("Invalid C++ source produced an executable", compilation.wasmFile)
            assertNull("Invalid C++ source was unexpectedly executed", result.execution)
        }
    }

    @Test
    fun propagatesWasiProgramNonZeroExitCode() {
        verifyProgram(
            """
            int main() {
                return 7;
            }
            """
        ) { result ->
            assertEquals(
                "Clang/LLD failure: ${result.compilation.stderr}",
                0,
                result.compilation.exitCode
            )
            assertNotNull("WASM module was not generated", result.compilation.wasmFile)
            assertNotNull("WASM execution was skipped", result.execution)
            assertEquals(
                "Expected WASI exit code 7: ${result.execution!!.stderr}",
                7,
                result.execution!!.exitCode
            )
        }
    }
}
