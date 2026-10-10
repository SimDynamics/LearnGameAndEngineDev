package com.learngraphicsprogramming.app

import android.util.Log
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.runtime.CppEngineResult
import com.learngraphicsprogramming.app.runtime.CppExecutionEngine
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

/**
 * Device-side catalog of C++23 standard-library compatibility.
 *
 * Unlike the existing regression suites, missing optional library features
 * are NOT test failures. Each independent feature is guarded with
 * __has_include and the relevant SD-6 feature-test macro, then compiled,
 * linked and (if available) actually executed with our selected WASI runtime.
 *
 * A declared feature that fails to compile/link/execute is reported as BROKEN,
 * not conflated with UNAVAILABLE. The positive baseline MUST pass.
 *
 * After the test, read the persistent report with:
 * adb shell run-as com.learngraphicsprogramming.app cat files/cpp23-library-compatibility.txt
 *
 * Run with -Plgp.clang23=true, first WAMR, then -Plgp.wasmtime=true.
 */
@RunWith(AndroidJUnit4::class)
class Cpp23LibraryCompatibilityInstrumentedTest {

    @Before
    fun requireClang23() {
        assertTrue("Pass -Plgp.clang23=true", BuildConfig.LGP_CLANG23_ENABLED)
    }

    private data class Probe(
        val name: String,
        val guardedInclude: String,
        val macroCondition: String,
        val body: String,
        val expectedStdout: String
    )

    private val probes = listOf(
        Probe(
            "std::expected",
            "#if __has_include(<expected>)\n#include <expected>\n#endif",
            "defined(__cpp_lib_expected) && __cpp_lib_expected >= 202202L",
            """std::expected<int, int> value(23);
std::cout << value.value() << '\n';""",
            "23\n"
        ),
        Probe(
            "std::print / std::println",
            "#if __has_include(<print>)\n#include <print>\n#endif",
            "defined(__cpp_lib_print) && __cpp_lib_print >= 202207L",
            """std::println("{}", 23);""",
            "23\n"
        ),
        Probe(
            "std::format (C++20 library baseline)",
            "#if __has_include(<format>)\n#include <format>\n#endif",
            "defined(__cpp_lib_format) && __cpp_lib_format >= 201907L",
            """std::cout << std::format("{}", 23) << '\n';""",
            "23\n"
        ),
        Probe(
            "std::mdspan",
            "#if __has_include(<mdspan>)\n#include <mdspan>\n#endif",
            "defined(__cpp_lib_mdspan) && __cpp_lib_mdspan >= 202207L",
            """int data[6] = {1, 2, 3, 4, 5, 6};
std::mdspan view(data, 2, 3);
std::cout << view[1, 2] << '\n';""",
            "6\n"
        ),
        Probe(
            "std::ranges::to",
            "#if __has_include(<ranges>)\n#include <ranges>\n#endif\n#include <vector>",
            "defined(__cpp_lib_ranges_to_container) && __cpp_lib_ranges_to_container >= 202202L",
            """auto values = std::ranges::to<std::vector<int>>(std::views::iota(1, 4));
std::cout << values[0] << values[1] << values[2] << '\n';""",
            "123\n"
        ),
        Probe(
            "std::flat_map",
            "#if __has_include(<flat_map>)\n#include <flat_map>\n#endif",
            "defined(__cpp_lib_flat_map) && __cpp_lib_flat_map >= 202207L",
            """std::flat_map<int, int> values;
values.emplace(1, 23);
std::cout << values.at(1) << '\n';""",
            "23\n"
        ),
        Probe(
            "std::ispanstream",
            "#if __has_include(<spanstream>)\n#include <spanstream>\n#endif\n#include <span>",
            "defined(__cpp_lib_spanstream) && __cpp_lib_spanstream >= 202106L",
            """char buffer[] = {'2', '3'};
std::ispanstream input(std::span<const char>(buffer, 2));
int result = 0;
input >> result;
std::cout << result << '\n';""",
            "23\n"
        ),
        Probe(
            "std::optional::transform",
            "#include <optional>",
            "defined(__cpp_lib_optional) && __cpp_lib_optional >= 202110L",
            """auto value = std::optional<int>(22).transform([](int n) { return n + 1; });
std::cout << *value << '\n';""",
            "23\n"
        ),
        Probe(
            "std::string::contains",
            "#include <string>",
            "defined(__cpp_lib_string_contains) && __cpp_lib_string_contains >= 202011L",
            """std::cout << std::string("graphics").contains("aph") << '\n';""",
            "1\n"
        ),
        Probe(
            "std::byteswap",
            "#include <bit>\n#include <cstdint>",
            "defined(__cpp_lib_byteswap) && __cpp_lib_byteswap >= 202110L",
            """std::cout << std::hex << std::byteswap(std::uint32_t{0x12345678u}) << '\n';""",
            "78563412\n"
        ),
        Probe(
            "std::to_underlying",
            "#include <utility>",
            "defined(__cpp_lib_to_underlying) && __cpp_lib_to_underlying >= 202102L",
            """enum class Value { number = 23 };
std::cout << std::to_underlying(Value::number) << '\n';""",
            "23\n"
        ),
        Probe(
            "std::invoke_r",
            "#include <functional>",
            "defined(__cpp_lib_invoke_r) && __cpp_lib_invoke_r >= 202106L",
            """std::cout << std::invoke_r<int>([] { return 23; }) << '\n';""",
            "23\n"
        )
    )

    private fun sourceFor(probe: Probe): String = """
        #include <iostream>
        #include <version>
        @INCLUDES@
        int main() {
        #if @CONDITION@
        @BODY@
        #else
            std::cout << "UNAVAILABLE\n";
        #endif
        }
    """.trimIndent()
        .replace("@INCLUDES@", probe.guardedInclude)
        .replace("@CONDITION@", probe.macroCondition)
        .replace("@BODY@", probe.body)

    private fun diagnostics(result: CppEngineResult): String {
        val raw = if (result.compilation.stderr.isNotBlank()) {
            result.compilation.stderr
        } else {
            result.execution?.stderr.orEmpty()
        }
        return raw.replace('\n', ' ').take(250)
    }

    @Test
    fun catalogsStandardLibraryFeaturesAndWritesDeviceReport() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val engine = CppExecutionEngine(context)
        val report = File(context.filesDir, "cpp23-library-compatibility.txt")
        val backend = if (BuildConfig.LGP_WASMTIME_ENABLED) "Wasmtime 49.0.2" else "WAMR 2.4.5"
        val lines = mutableListOf(
            "Clang 23.1.3 + LLD 23.1.3 / $backend",
            "WASI sysroot: wasi-sdk-34 (" +
                (if (BuildConfig.LGP_CPP_EXCEPTIONS_ENABLED) "WebAssembly EH enabled" else "no exceptions") + ")",
            "SUPPORTED = macro declared, compiled, linked, executed and verified",
            "UNAVAILABLE = feature header or feature-test macro missing",
            "BROKEN = declared but compilation, linking, runtime or output failed",
            ""
        )

        // Distinguishes missing basic infrastructure from optional C++23 support.
        val baseline = engine.compileAndRun(
            """
            #include <iostream>
            int main() { std::cout << "BASELINE_OK" << '\n'; }
            """.trimIndent()
        )
        try {
            assertFalse("Baseline Clang compilation timed out", baseline.compilation.timedOut)
            assertEquals("Baseline Clang/LLD failure: " + baseline.compilation.stderr,
                0, baseline.compilation.exitCode)
            assertNotNull("No baseline WASM module", baseline.compilation.wasmFile)
            assertNotNull("Baseline WASM was not executed", baseline.execution)
            assertEquals("Baseline WASI failure: " + baseline.execution!!.stderr,
                0, baseline.execution!!.exitCode)
            assertEquals("BASELINE_OK\n", baseline.execution!!.stdout)
        } finally {
            baseline.compilation.workspace.deleteRecursively()
        }

        for (probe in probes) {
            val result = engine.compileAndRun(sourceFor(probe))
            try {
                val execution = result.execution
                val status = when {
                    result.compilation.timedOut -> "BROKEN (compiler timeout)"
                    result.compilation.exitCode != 0 -> "BROKEN (compile/link)"
                    execution == null -> "BROKEN (no execution)"
                    execution.exitCode != 0 -> "BROKEN (runtime exit)"
                    execution.stdout == "UNAVAILABLE\n" -> "UNAVAILABLE"
                    execution.stdout == probe.expectedStdout -> "SUPPORTED"
                    else -> "BROKEN (unexpected stdout)"
                }
                val line = probe.name + ": " + status
                lines.add(line)
                if (status.startsWith("BROKEN")) {
                    lines.add("  diagnostic: " + diagnostics(result))
                }
                Log.i("LGP-Cpp23Library", line)
            } finally {
                result.compilation.workspace.deleteRecursively()
            }
            // Keep partial results if the instrumentation process is interrupted.
            report.writeText(lines.joinToString("\n") + "\n", Charsets.UTF_8)
        }
        lines.add("")
        lines.add("Catalog only: unavailable features do not fail this JUnit test.")
        lines.add("Do not interpret BUILD SUCCESSFUL as universal C++23 library support.")
        report.writeText(lines.joinToString("\n") + "\n", Charsets.UTF_8)
        println(lines.joinToString("\n"))
    }
}
