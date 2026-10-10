package com.learngraphicsprogramming.app

import android.os.SystemClock
import android.system.Os
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.narration.NarrationWorkPriority
import com.learngraphicsprogramming.app.runtime.CppCompilationResult
import com.learngraphicsprogramming.app.runtime.CppCompiler
import com.learngraphicsprogramming.app.runtime.CppEngineResult
import com.learngraphicsprogramming.app.runtime.CppExecutionEngine
import com.learngraphicsprogramming.app.runtime.WasiRuntime
import com.learngraphicsprogramming.app.runtime.WasiExecutionResult
import java.io.File
import java.util.UUID
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/** Native Clang/LLD/WASI/Wasmtime checks for gaps beyond the existing six EH regressions. */
@RunWith(AndroidJUnit4::class)
class CppRuntimeAcceptanceInstrumentedTest {
    private val context = InstrumentationRegistry.getInstrumentation().targetContext

    @Before fun requireTheExistingDefaultToolchain() {
        assertTrue("The main route must use Clang 23", BuildConfig.LGP_CLANG23_ENABLED)
        assertTrue("The main route must use Wasmtime", BuildConfig.LGP_WASMTIME_ENABLED)
        assertTrue("C++ exception ABI must remain enabled", BuildConfig.LGP_CPP_EXCEPTIONS_ENABLED)
    }

    @Test fun valueCatchesCopyWhileReferenceCatchesPreserveDerivedExceptionsAndStreams() {
        withProgram("""
            #include <iostream>
            struct Error {
                int value;
                explicit Error(int value) : value(value) {}
                virtual const char* kind() const { return "base"; }
                virtual ~Error() = default;
            };
            struct Derived : Error {
                using Error::Error;
                const char* kind() const override { return "derived"; }
            };
            int main() {
                try { throw Derived(7); }
                catch (Error copy) {
                    copy.value = 8;
                    std::cout << "copy:" << copy.kind() << ':' << copy.value << '\n';
                    try { throw; }
                    catch (const Derived& original) {
                        std::cout << "original:" << original.kind() << ':' << original.value << '\n';
                    }
                }
                try { throw Derived(9); }
                catch (Error& reference) {
                    reference.value = 10;
                    std::cout << "reference:" << reference.kind() << ':' << reference.value << '\n';
                    try { throw; }
                    catch (const Derived& original) {
                        std::cout << "original:" << original.kind() << ':' << original.value << '\n';
                    }
                }
                std::cerr << "EH stderr remains independent\n";
                return 23;
            }
        """) { result ->
            val execution = requireExecution(result)
            assertEquals("A caught exception must preserve the program's explicit exit status", 23, execution.exitCode)
            assertEquals("copy:base:8\noriginal:derived:7\nreference:derived:10\noriginal:derived:10\n", execution.stdout)
            assertEquals("EH stderr remains independent\n", execution.stderr)
        }
    }

    @Test fun invalidSourceRetainsUsefulDiagnosticsAndTheNextStdinJobRecovers() {
        withProgram("""
            int main() {
                return missing_phase_b_identifier;
            }
        """) { result ->
            assertFalse("An ordinary diagnostic must not be mistaken for compiler timeout", result.compilation.timedOut)
            assertTrue("Invalid source must fail compilation", result.compilation.exitCode != 0)
            assertTrue("Keep the source location and actual Clang error: ${result.compilation.stderr}",
                Regex("main\\.cpp:2:\\d+: error:").containsMatchIn(result.compilation.stderr))
            assertTrue(result.compilation.stderr.contains("missing_phase_b_identifier"))
            assertNull("The failed source must not produce an executable WASM", result.compilation.wasmFile)
            assertNull("Compiler errors must never reach the execution engine", result.execution)
            assertFalse("Temporary app paths must not replace the readable source name",
                result.compilation.stderr.contains(result.compilation.workspace.absolutePath))
        }
        verifyNextStdinJob()
    }

    @Test fun sandboxRejectsAbsoluteParentAndSymlinkEscapesAndOtherJobFiles() {
        val fixture = createFixture()
        val marker = File(fixture, "outside.txt").apply { writeText("outside marker must survive", Charsets.UTF_8) }
        val source = """
            #include <cstdio>
            #include <iostream>
            bool denied(const char* path, const char* mode) {
                FILE* file = std::fopen(path, mode);
                if (file) { std::fclose(file); return false; }
                return true;
            }
            int main() {
                bool isolated = denied("@ABSOLUTE@", "r") && denied("@ABSOLUTE@", "w")
                    && denied("../../@FIXTURE@/outside.txt", "r")
                    && denied("../../@FIXTURE@/outside.txt", "w")
                    && denied("escape-link", "r") && denied("escape-link", "w");
                FILE* own = std::fopen("inside.txt", "w");
                if (!own) return 6;
                std::fputs("owned workspace output", own);
                std::fclose(own);
                std::cout << (isolated ? "ISOLATED" : "ESCAPED") << '\n';
                return isolated ? 0 : 7;
            }
        """.trimIndent().replace("@ABSOLUTE@", marker.absolutePath).replace("@FIXTURE@", fixture.name)
        val priority = NarrationWorkPriority.beginInteractive()
        var compilation: CppCompilationResult? = null
        try {
            compilation = CppCompiler(context).compile(source)
            assertCompiled(compilation)
            val workspace = compilation.workspace
            Os.symlink(marker.absolutePath, File(workspace, "escape-link").absolutePath)
            val execution = WasiRuntime.run(requireNotNull(compilation.wasmFile).readBytes(), workspace)
            assertEquals(execution.stderr, 0, execution.exitCode)
            assertEquals("ISOLATED\n", execution.stdout)
            assertEquals("outside marker must survive", marker.readText(Charsets.UTF_8))
            assertEquals("owned workspace output", File(workspace, "inside.txt").readText())
            withProgram("""
                #include <cstdio>
                #include <iostream>
                int main() {
                    FILE* inherited = std::fopen("inside.txt", "r");
                    FILE* sibling = std::fopen("../@OTHER@/inside.txt", "r");
                    bool isolated = inherited == nullptr && sibling == nullptr;
                    if (inherited) std::fclose(inherited);
                    if (sibling) std::fclose(sibling);
                    std::cout << (isolated ? "FRESH_JOB" : "LEAKED_JOB") << '\n';
                    return isolated ? 0 : 8;
                }
            """.replace("@OTHER@", workspace.name)) { second ->
                assertTrue("Every compilation owns a different workspace", workspace.canonicalFile != second.compilation.workspace.canonicalFile)
                val next = requireExecution(second)
                assertEquals(next.stderr, 0, next.exitCode)
                assertEquals("FRESH_JOB\n", next.stdout)
            }
        } finally {
            try { compilation?.let { removeOwnedWorkspace(it.workspace) } }
            finally {
                try { removeOwnedFixture(fixture) } finally { priority.close() }
            }
        }
    }

    @Test fun finiteInstructionFuelTerminatesEndlessComputeAndTheNextNativeJobRecovers() {
        withProgram("""
            int main() {
                volatile unsigned counter = 0;
                for (;;) { counter = counter + 1; }
            }
        """) { result ->
            val execution = requireExecution(result)
            assertTrue("Endless guest computation must end in a controlled runtime failure", execution.exitCode != 0)
            assertTrue("The finite Wasmtime instruction budget must be the actual terminating cause: ${execution.stderr}",
                execution.stderr.contains("fuel", ignoreCase = true))
            assertEquals("No output from an endless calculation is fabricated", "", execution.stdout)
        }
        verifyNextStdinJob()
    }

    @Test fun malformedModulesAndGuestTrapsStayControlledAndTheNextStdinJobRecovers() {
        val fixture = createFixture()
        val priority = NarrationWorkPriority.beginInteractive()
        try {
            val invalid = WasiRuntime.run(byteArrayOf(0, 1, 2, 3), fixture)
            assertTrue("A malformed module returns a controlled error", invalid.exitCode != 0)
            assertTrue("The actual native parse error must be retained", invalid.stderr.isNotBlank())
            assertEquals("Malformed modules never produce guest output", "", invalid.stdout)
            withProgram("""
                int main() { __builtin_trap(); }
            """) { result ->
                val trapped = requireExecution(result)
                assertTrue("Guest traps must not crash the Android process", trapped.exitCode != 0)
                assertTrue("Retain the actual Wasmtime trap diagnostic: ${trapped.stderr}",
                    trapped.stderr.contains("trap", ignoreCase = true) || trapped.stderr.contains("unreachable", ignoreCase = true))
            }
            verifyNextStdinJob()
        } finally {
            try { removeOwnedFixture(fixture) } finally { priority.close() }
        }
    }

    private fun verifyNextStdinJob() = withProgram("""
        #include <iostream>
        #include <limits>
        #include <string>
        int main() {
            int value = 0;
            if (!(std::cin >> value)) return 4;
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            std::string line, extra;
            if (!std::getline(std::cin, line)) return 5;
            bool eof = !std::getline(std::cin, extra);
            std::cout << value << '|' << line << '|' << (eof ? "EOF" : "EXTRA") << '\n';
            return 0;
        }
    """, "7\nlínea con espacios ñ\n") { result ->
        val execution = requireExecution(result)
        assertEquals("A later native job must recover its own streams and runtime store: ${execution.stderr}", 0, execution.exitCode)
        assertEquals("7|línea con espacios ñ|EOF\n", execution.stdout)
        assertEquals("No previous diagnostic leaks into the recovered job", "", execution.stderr)
    }

    private fun withProgram(source: String, input: String = "", verify: (CppEngineResult) -> Unit) {
        val started = SystemClock.elapsedRealtime()
        val result = CppExecutionEngine(context).compileAndRun(source.trimIndent(), input)
        try {
            verify(result)
            // An observed-device acceptance bound, not a universal wall-clock guarantee.
            assertTrue("This native compile/run completed within 90 seconds on the test device; elapsed=${SystemClock.elapsedRealtime() - started}ms",
                SystemClock.elapsedRealtime() - started < 90_000)
        } finally { removeOwnedWorkspace(result.compilation.workspace) }
    }

    private fun assertCompiled(compilation: CppCompilationResult) {
        assertFalse("Clang 23 compilation timed out: ${compilation.stderr}", compilation.timedOut)
        assertEquals("Clang 23/LLD/EH compilation or link failed: ${compilation.stderr}", 0, compilation.exitCode)
        assertNotNull("Successful Clang/LLD compilation generates real WASM", compilation.wasmFile)
        assertTrue(requireNotNull(compilation.wasmFile).length() > 8)
    }

    private fun requireExecution(result: CppEngineResult): WasiExecutionResult {
        assertCompiled(result.compilation)
        assertNotNull("A successfully linked WASI program actually executes", result.execution)
        return requireNotNull(result.execution)
    }

    private fun createFixture(): File = File(context.cacheDir, "cpp-phase-b-test-${UUID.randomUUID()}").also {
        assertTrue(it.mkdirs())
    }

    private fun removeOwnedFixture(fixture: File) {
        val canonical = fixture.canonicalFile
        require(canonical.parentFile == context.cacheDir.canonicalFile && canonical.name.startsWith("cpp-phase-b-test-"))
        assertTrue("Remove only the private files this test owns", canonical.deleteRecursively())
    }

    private fun removeOwnedWorkspace(workspace: File) {
        val canonical = workspace.canonicalFile
        require(canonical.parentFile == File(context.cacheDir, "cpp-jobs").canonicalFile)
        require(runCatching { UUID.fromString(canonical.name) }.isSuccess)
        // FileTreeWalk does not follow the fixture's symbolic link to outside data.
        assertTrue("Remove only this compiler invocation's private workspace", canonical.deleteRecursively())
    }
}
