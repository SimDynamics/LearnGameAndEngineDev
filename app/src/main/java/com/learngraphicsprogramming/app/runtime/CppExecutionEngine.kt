package com.learngraphicsprogramming.app.runtime

import android.content.Context
import android.util.Log
import com.learngraphicsprogramming.app.BuildConfig

internal class CppExecutionEngine(context: Context) {
    private val compiler = CppCompiler(context.applicationContext)

    fun compileAndRun(source: String, standardInput: String = ""): CppEngineResult {
        // Diagnostic checkpoints intentionally exclude user code and stdin.
        if (BuildConfig.DEBUG) Log.i("LGP-CppEngine", "00 starting Clang compilation")
        val compilation = compiler.compile(source)
        if (BuildConfig.DEBUG) {
            Log.i(
                "LGP-CppEngine",
                "01 Clang finished: exit=${compilation.exitCode}, wasm=${compilation.wasmFile != null}"
            )
        }
        val wasmFile = compilation.wasmFile ?: return CppEngineResult(compilation, null)

        if (BuildConfig.DEBUG) {
            val backend = if (BuildConfig.LGP_WASMTIME_ENABLED) "Wasmtime" else "WAMR"
            Log.i("LGP-CppEngine", "02 calling $backend with WASM module")
        }
        val execution = WasiRuntime.run(wasmFile.readBytes(), compilation.workspace, standardInput)
        if (BuildConfig.DEBUG) {
            Log.i("LGP-CppEngine", "03 runtime finished: exit=${execution.exitCode}")
        }
        return CppEngineResult(compilation, execution)
    }
}

internal data class CppEngineResult(
    val compilation: CppCompilationResult,
    val execution: WasiExecutionResult?
)
