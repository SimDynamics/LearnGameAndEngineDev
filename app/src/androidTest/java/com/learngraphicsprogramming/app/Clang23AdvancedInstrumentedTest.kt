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
 * Second-stage end-to-end stress and regression suite.
 *
 * Run with -Plgp.clang23=true and first WAMR (default), then Wasmtime
 * (-Plgp.wasmtime=true). All programs are compiled on the Android device,
 * linked by LLD 23, and executed as WASI WebAssembly without network access.
 *
 * Deliberately avoids EH, threads and C++23 library APIs not yet guaranteed
 * by our separately packaged WASI sysroot. Those require dedicated testing.
 */
@RunWith(AndroidJUnit4::class)
class Clang23AdvancedInstrumentedTest {
    @Before
    fun requiresNativeClang23() {
        assertTrue("Pass -Plgp.clang23=true", BuildConfig.LGP_CLANG23_ENABLED)
    }

    private fun withProgram(
        source: String,
        stdin: String = "",
        validate: (CppEngineResult) -> Unit
    ) {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val result = CppExecutionEngine(context).compileAndRun(source.trimIndent(), stdin)
        try {
            validate(result)
        } finally {
            result.compilation.workspace.deleteRecursively()
        }
    }

    private fun assertSuccess(result: CppEngineResult, expectedStdout: String) {
        val compilation = result.compilation
        assertFalse("Compilation timed out: ${compilation.stderr}", compilation.timedOut)
        assertEquals(
            "Clang 23 / LLD 23 error: ${compilation.stderr}",
            0,
            compilation.exitCode
        )
        assertNotNull("Missing linked WebAssembly module", compilation.wasmFile)
        assertTrue("Empty WebAssembly module", compilation.wasmFile!!.length() > 8L)
        val execution = result.execution
        assertNotNull("WASM execution was skipped", execution)
        assertEquals("WASM runtime error: ${execution!!.stderr}", 0, execution.exitCode)
        assertEquals("Unexpected standard output", expectedStdout, execution.stdout)
    }

    @Test
    fun supportsCpp23MultidimensionalSubscriptOperator() {
        withProgram(
            """
            #include <iostream>
            struct Grid {
                int values[4] = {1, 2, 3, 4};
                constexpr int operator[](int row, int column) const {
                    return values[row * 2 + column];
                }
            };
            constexpr Grid grid{};
            static_assert(grid[1, 0] == 3);
            int main() {
                std::cout << grid[1, 1] << std::endl;
            }
            """
        ) { assertSuccess(it, "4\n") }
    }

    @Test
    fun sortsTwentyThousandIntegersAndComputesChecksum() {
        withProgram(
            """
            #include <algorithm>
            #include <iostream>
            #include <numeric>
            #include <vector>
            int main() {
                std::vector<int> values;
                for (int n = 20000; n >= 1; --n) {
                    values.push_back(n);
                }
                std::sort(values.begin(), values.end());
                long long sum = std::accumulate(values.begin(), values.end(), 0LL);
                std::cout << values.front() << ',' << values.back()
                          << ',' << sum << std::endl;
            }
            """
        ) { assertSuccess(it, "1,20000,200010000\n") }
    }

    @Test
    fun preserves64BitArithmeticAndRecursiveCalls() {
        withProgram(
            """
            #include <cstdint>
            #include <iostream>
            std::uint64_t factorial(int n) {
                if (n <= 1) return 1;
                return static_cast<std::uint64_t>(n) * factorial(n - 1);
            }
            int main() {
                const std::uint64_t large = 4000000000ULL + 987654321ULL;
                std::cout << large << ',' << factorial(20) << std::endl;
            }
            """
        ) { assertSuccess(it, "4987654321,2432902008176640000\n") }
    }

    @Test
    fun handlesMixedStandardLibraryContainersAndSmartPointers() {
        withProgram(
            """
            #include <iostream>
            #include <memory>
            #include <sstream>
            #include <string>
            #include <unordered_map>
            int main() {
                auto value = std::make_unique<int>(23);
                std::unordered_map<std::string, int> lookup{{"clang", *value}};
                std::ostringstream message;
                message << "LLVM " << lookup["clang"];
                std::cout << message.str() << std::endl;
            }
            """
        ) { assertSuccess(it, "LLVM 23\n") }
    }

    @Test
    fun capturesStandardOutputAndErrorSeparately() {
        withProgram(
            """
            #include <iostream>
            int main() {
                std::cout << "normal-output" << std::endl;
                std::cerr << "runtime-diagnostic" << std::endl;
            }
            """
        ) { result ->
            assertSuccess(result, "normal-output\n")
            assertEquals(
                "Runtime stderr not correctly redirected",
                "runtime-diagnostic\n",
                result.execution!!.stderr
            )
        }
    }

    @Test
    fun rejectsUndefinedLinkerSymbolsWithActionableDiagnostic() {
        withProgram(
            """
            extern "C" int missing_lgp_symbol();
            int main() {
                return missing_lgp_symbol();
            }
            """
        ) { result ->
            val compilation = result.compilation
            assertFalse("Linker timed out", compilation.timedOut)
            assertTrue(
                "An undefined function unexpectedly linked successfully",
                compilation.exitCode != 0
            )
            assertTrue(
                "LLD did not identify the missing symbol: ${compilation.stderr}",
                compilation.stderr.contains("missing_lgp_symbol")
            )
            assertNull("Failed linking should not return a WASM module", compilation.wasmFile)
            assertNull("Failed linking should not start the WASM runtime", result.execution)
        }
    }

    @Test
    fun supportsCppConceptsAndTemplatedLambdas() {
        withProgram(
            """
            #include <concepts>
            #include <iostream>
            template <std::integral T>
            T doubled(T value) {
                return value * 2;
            }
            int main() {
                auto plusOne = []<std::integral T>(T value) { return value + 1; };
                std::cout << plusOne(doubled(11)) << std::endl;
            }
            """
        ) { assertSuccess(it, "23\n") }
    }

    @Test
    fun compilesAndRunsFourIndependentJobsSequentially() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val source = """
            #include <iostream>
            int main() {
                int value = 0;
                if (!(std::cin >> value)) return 2;
                std::cout << (value * value + 1) << std::endl;
            }
        """.trimIndent()
        for (value in 1..4) {
            val result = CppExecutionEngine(context).compileAndRun(source, "$"+"{value}\n")
            try {
                assertSuccess(result, "$"+"{value * value + 1}\n")
            } finally {
                result.compilation.workspace.deleteRecursively()
            }
        }
    }
}
