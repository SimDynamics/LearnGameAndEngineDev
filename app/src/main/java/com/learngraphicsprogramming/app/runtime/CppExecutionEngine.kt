package com.learngraphicsprogramming.app.runtime

import android.content.Context
import android.util.Log
import com.learngraphicsprogramming.app.BuildConfig
import com.learngraphicsprogramming.app.narration.NarrationWorkPriority
import java.io.File

internal class CppExecutionEngine(context: Context) {
    private val compiler = CppCompiler(context.applicationContext)
    // Optional app-private crash checkpoints; disabled during normal execution.
    // Never store source code, standard input or user file paths here.
    private val diagnosticsFile =
        File(context.applicationContext.filesDir, "cpp-runtime-phase.txt")

    private fun checkpoint(message: String, reset: Boolean = false) {
        if (!BuildConfig.DEBUG || !BuildConfig.LGP_WASMTIME_DIAGNOSTICS) return
        runCatching {
            if (reset) diagnosticsFile.writeText(message + "\n")
            else diagnosticsFile.appendText(message + "\n")
        }
        Log.i("LGP-CppEngine", message)
    }

    fun compileAndRun(
        source: String,
        standardInput: String = "",
        compilerVariant: CppCompilerVariant = CppCompilerVariant.DEFAULT
    ): CppEngineResult {
        // Stop optional background narration before BOTH compilation and WASM
        // execution, releasing the priority only when execution finishes.
        val foreground = NarrationWorkPriority.beginInteractive()
        try {
        // Recreated for each test invocation; safe if the process ends suddenly.
        checkpoint("K00 starting Clang compilation", reset = true)
        val compilation = compiler.compile(source, compilerVariant)
        checkpoint(
            "K01 Clang finished: exit=${compilation.exitCode}, wasm=${compilation.wasmFile != null}"
        )
        val wasmFile = compilation.wasmFile ?: return CppEngineResult(compilation, null)

        val backend = if (BuildConfig.LGP_WASMTIME_ENABLED) "Wasmtime" else "WAMR"
        checkpoint("K02 preparing $backend runtime; reading WASM bytes")
        val wasmBytes = wasmFile.readBytes()
        checkpoint("K03 invoking $backend JNI")
        val execution = WasiRuntime.run(
            wasmBytes,
            compilation.workspace,
            standardInput,
            if (BuildConfig.DEBUG && BuildConfig.LGP_WASMTIME_DIAGNOSTICS)
                diagnosticsFile.absolutePath
            else null
        )
        checkpoint("K04 $backend returned: exit=${execution.exitCode}")
        return CppEngineResult(compilation, execution)
        } finally {
            foreground.close()
        }
    }
}

internal data class CppEngineResult(
    val compilation: CppCompilationResult,
    val execution: WasiExecutionResult?
)
