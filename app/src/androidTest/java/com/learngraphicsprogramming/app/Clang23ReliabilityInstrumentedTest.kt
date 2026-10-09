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
 * Third-stage Clang 23 + LLD 23 + WASI reliability tests on a real Android device.
 *
 * Run once with WAMR (default) and once with -Plgp.wasmtime=true.
 * Tests avoid unsupported C++ exceptions and threads.
 *
 * These are bounded resource tests, not intentionally destructive OOM tests.
 * They exercise real standard streams, a 4 MiB heap allocation, larger source
 * files, failure recovery, and repeated in-process invocations.
 */
@RunWith(AndroidJUnit4::class)
class Clang23ReliabilityInstrumentedTest {

    @Before
    fun requiresClang23() {
        assertTrue("Pass -Plgp.clang23=true", BuildConfig.LGP_CLANG23_ENABLED)
    }

    private val engine: CppExecutionEngine
        get() = CppExecutionEngine(InstrumentationRegistry.getInstrumentation().targetContext)

    private fun withResult(
        source: String,
        standardInput: String = "",
        block: (CppEngineResult) -> Unit
    ) {
        val result = engine.compileAndRun(source.trimIndent(), standardInput)
        try {
            block(result)
        } finally {
            result.compilation.workspace.deleteRecursively()
        }
    }

    private fun assertSuccess(result: CppEngineResult, expectedOutput: String) {
        val compilation = result.compilation
        assertFalse("Compiler timed out: ${compilation.stderr}", compilation.timedOut)
        assertEquals("Clang/LLD failed: ${compilation.stderr}", 0, compilation.exitCode)
        assertNotNull("No WebAssembly module generated", compilation.wasmFile)
        assertTrue("The generated module is empty", compilation.wasmFile!!.length() > 8L)
        val execution = result.execution
        assertNotNull("WebAssembly was not executed", execution)
        assertEquals("WASI execution failed: ${execution!!.stderr}", 0, execution.exitCode)
        assertEquals("Wrong standard output", expectedOutput, execution.stdout)
    }

    @Test
    fun readsSixtyFourKilobytesOfStandardInput() {
        val standardInput = "x".repeat(65535) + "Z\n"
        withResult(
            """
            #include <iostream>
            #include <string>
            int main() {
                std::string line;
                if (!std::getline(std::cin, line)) return 3;
                std::cout << line.size() << ':' << line.back() << '\n';
            }
            """,
            standardInput
        ) { assertSuccess(it, "65536:Z\n") }
    }

    @Test
    fun capturesThirtyTwoKilobytesOfStandardOutputWithoutTruncation() {
        withResult(
            """
            #include <iostream>
            #include <string>
            int main() {
                std::cout << std::string(32768, '#') << '\n';
            }
            """
        ) { assertSuccess(it, "#".repeat(32768) + "\n") }
    }

    @Test
    fun allocatesAndProcessesFourMegabytesOfDynamicMemory() {
        withResult(
            """
            #include <iostream>
            #include <numeric>
            #include <vector>
            int main() {
                std::vector<int> values(1048576, 3);
                const long long result =
                    std::accumulate(values.begin(), values.end(), 0LL);
                std::cout << values.size() << ':' << result << '\n';
            }
            """
        ) { assertSuccess(it, "1048576:3145728\n") }
    }

    @Test
    fun compilesAndExecutesMathLibraryFunctions() {
        withResult(
            """
            #include <cmath>
            #include <iomanip>
            #include <iostream>
            int main() {
                const double result = std::sqrt(144.0) + std::hypot(3.0, 4.0);
                std::cout << std::fixed << std::setprecision(2)
                          << result << '\n';
            }
            """
        ) { assertSuccess(it, "17.00\n") }
    }

    @Test
    fun preservesUtf8AcrossInputAndOutput() {
        val sentence = "Español: pingüino, jalapeño, français, 日本語"
        withResult(
            """
            #include <iostream>
            #include <string>
            int main() {
                std::string line;
                if (!std::getline(std::cin, line)) return 3;
                std::cout << line << '\n';
            }
            """,
            "${sentence}\n"
        ) { assertSuccess(it, "${sentence}\n") }
    }

    @Test
    fun handlesSourceFileWithOneThousandDeclarations() {
        val declarations = (0..1023).joinToString("\n") { index ->
            "constexpr int value_${index} = ${index};"
        }
        val source = """
            #include <iostream>
            ${declarations}
            int main() {
                std::cout << value_1023 << '\n';
            }
        """
        withResult(source) { assertSuccess(it, "1023\n") }
    }

    @Test
    fun recoversAfterInvalidCodeWithoutRestartingApplication() {
        withResult(
            """
            int main() {
                int broken = ;
                return broken;
            }
            """
        ) { result ->
            assertFalse("Invalid input should produce a compiler diagnostic", result.compilation.timedOut)
            assertTrue("Malformed C++ unexpectedly compiled", result.compilation.exitCode != 0)
            assertTrue("Missing compiler diagnostic", result.compilation.stderr.contains("error:"))
            assertNull("Malformed C++ must not run", result.execution)
        }
        withResult(
            """
            #include <iostream>
            int main() {
                std::cout << "recovered" << '\n';
            }
            """
        ) { assertSuccess(it, "recovered\n") }
    }

    @Test
    fun runsTenSequentialJobsWithDistinctStandardInput() {
        val source = """
            #include <iostream>
            int main() {
                int n = 0;
                if (!(std::cin >> n)) return 5;
                std::cout << (n * n + 1) << '\n';
            }
        """
        for (n in 1..10) {
            withResult(source, "${n}\n") { result ->
                assertSuccess(result, "${n * n + 1}\n")
            }
        }
    }
}
