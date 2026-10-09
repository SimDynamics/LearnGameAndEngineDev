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
        stderrPath: String,
        stdinPath: String
    ): Int

    fun run(wasmBytes: ByteArray, workingDirectory: File, standardInput: String = ""): WasiExecutionResult {
        workingDirectory.mkdirs()
        val stdoutFile = File(workingDirectory, "stdout.txt")
        val stderrFile = File(workingDirectory, "stderr.txt")
        // WASI receives a real stdin descriptor instead of an unavailable (-1) descriptor.
        // A prepared input stream supports std::cin without changing existing callers.
        val stdinFile = File(workingDirectory, "stdin.txt").apply {
            writeText(standardInput, Charsets.UTF_8)
        }
        // The Gradle feature flag routes to the separately linked Wasmtime
        // implementation without removing our validated WAMR fallback.
        val exitCode = if (com.learngraphicsprogramming.app.BuildConfig.LGP_WASMTIME_ENABLED) {
            WasmtimeRuntime.execute(
                wasmBytes,
                workingDirectory.absolutePath,
                stdoutFile.absolutePath,
                stderrFile.absolutePath,
                stdinFile.absolutePath
            )
        } else {
            execute(
                wasmBytes,
                workingDirectory.absolutePath,
                stdoutFile.absolutePath,
                stderrFile.absolutePath,
                stdinFile.absolutePath
            )
        }
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
