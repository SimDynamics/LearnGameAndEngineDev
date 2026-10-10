package com.learngraphicsprogramming.app

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.runtime.WasiRuntime
import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/**
 * Runs the Clang 21 + LLD 21 output already generated on the device.
 *
 * This test deliberately does not compile, download, or select a toolchain.
 * It verifies only that the unchanged Wasmtime backend can execute the
 * C++23/iostream WASI module created independently via adb.
 */
@RunWith(AndroidJUnit4::class)
class Clang21WasmRuntimeInstrumentedTest {

    @Test
    fun executesPreviouslyCompiledClang21WasmWithWasmtime() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        assertTrue(
            "Esta variante debe utilizar Wasmtime.",
            BuildConfig.LGP_WASMTIME_ENABLED
        )

        val wasm = File(context.cacheDir, "clang21-verify/iostream.wasm")
        assertTrue(
            "Falta el archivo compilado con Clang 21: ${wasm.absolutePath}",
            wasm.isFile && wasm.length() > 0L
        )

        val workspace = File(context.cacheDir, "clang21-verify/wasmtime-run")
        val execution = WasiRuntime.run(wasm.readBytes(), workspace)

        assertEquals(execution.stderr, 0, execution.exitCode)
        assertEquals("Hola desde Clang 21\n", execution.stdout)
    }
}
