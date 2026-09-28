package com.learngraphicsprogramming.app.runtime

import android.content.Context

internal class CppExecutionEngine(context: Context) {
    private val compiler = CppCompiler(context.applicationContext)

    fun compileAndRun(source: String): CppEngineResult {
        val compilation = compiler.compile(source)
        val wasmFile = compilation.wasmFile ?: return CppEngineResult(compilation, null)
        val execution = WasiRuntime.run(wasmFile.readBytes(), compilation.workspace)
        return CppEngineResult(compilation, execution)
    }
}

internal data class CppEngineResult(
    val compilation: CppCompilationResult,
    val execution: WasiExecutionResult?
)
