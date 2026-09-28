package com.learngraphicsprogramming.app.runtime

import java.io.File

internal object WasiRuntime {
    init {
        System.loadLibrary("app")
    }

    external fun execute(
        wasmBytes: ByteArray,
        workingDirectory: String,
        stdoutPath: String,
        stderrPath: String
    ): Int

    fun run(wasmBytes: ByteArray, workingDirectory: File): WasiExecutionResult {
        workingDirectory.mkdirs()
        val stdoutFile = File(workingDirectory, "stdout.txt")
        val stderrFile = File(workingDirectory, "stderr.txt")
        val exitCode = execute(
            wasmBytes,
            workingDirectory.absolutePath,
            stdoutFile.absolutePath,
            stderrFile.absolutePath
        )
        return WasiExecutionResult(
            exitCode,
            stdoutFile.takeIf(File::isFile)?.readText().orEmpty(),
            stderrFile.takeIf(File::isFile)?.readText().orEmpty()
        )
    }
}

internal data class WasiExecutionResult(
    val exitCode: Int,
    val stdout: String,
    val stderr: String
)
