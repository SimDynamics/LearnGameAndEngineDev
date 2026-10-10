package com.learngraphicsprogramming.app

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.runtime.WasiRuntime
import com.learngraphicsprogramming.app.runtime.CppExecutionEngine
import com.learngraphicsprogramming.app.runtime.CppCompiler
import com.learngraphicsprogramming.app.runtime.CppCompilerVariant
import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith

/**
 * Verifies the optional Clang 21 compiler through Wasmtime, then independently
 * compiles another WebAssembly program for direct Wasmtime execution.
 */
@RunWith(AndroidJUnit4::class)
class Clang21WasmRuntimeInstrumentedTest {

    @Test
    fun compilesAndExecutesThroughSelectedOptionalClang21() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        assumeTrue(
            "The optional module is not installed as executable native libraries.",
            CppCompiler.isOptionalClang21Available(context)
        )
        val source = """
            #include <iostream>
            int main() {
                std::cout << "Clang 21 desde el selector" << '\n';
            }
        """.trimIndent()
        val result = CppExecutionEngine(context).compileAndRun(
            source,
            compilerVariant = CppCompilerVariant.OPTIONAL_CLANG_21
        )
        assertEquals(result.compilation.stderr, 0, result.compilation.exitCode)
        assertTrue(result.compilation.wasmFile?.isFile == true)
        assertEquals(result.execution?.stderr, 0, result.execution?.exitCode)
        assertEquals("Clang 21 desde el selector\n", result.execution?.stdout)
    }

    @Test
    fun executesClang21CompiledWasmDirectlyWithWasmtime() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        assertTrue(
            "Esta variante debe utilizar Wasmtime.",
            BuildConfig.LGP_WASMTIME_ENABLED
        )
        assumeTrue(
            "The optional module is not installed as executable native libraries.",
            CppCompiler.isOptionalClang21Available(context)
        )

        // Compile an independent test program instead of relying on an adb-pushed
        // artifact that disappears whenever the app is reinstalled or reset.
        val source = """
            #include <iostream>
            int main() {
                std::cout << "Hola desde Clang 21" << '\n';
            }
        """.trimIndent()
        val compilation = CppCompiler(context).compile(
            source,
            compilerVariant = CppCompilerVariant.OPTIONAL_CLANG_21
        )
        assertEquals(compilation.stderr, 0, compilation.exitCode)
        val wasm = compilation.wasmFile
        assertTrue(
            "Clang 21 no generó un módulo WebAssembly válido.",
            wasm?.isFile == true && wasm.length() > 0L
        )

        val workspace = File(context.cacheDir, "clang21-verify/wasmtime-run")
        val execution = WasiRuntime.run(requireNotNull(wasm).readBytes(), workspace)

        assertEquals(execution.stderr, 0, execution.exitCode)
        assertEquals("Hola desde Clang 21\n", execution.stdout)
    }
}
