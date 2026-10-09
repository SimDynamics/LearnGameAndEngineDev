package com.learngraphicsprogramming.app.runtime

/**
 * Wasmtime native C API backend, linked only in builds using
 * -Plgp.wasmtime=true. Existing WAMR remains the regression baseline.
 *
 * The caller prepares working_directory/stdin/stdout/stderr, just as with WAMR.
 * Guest programs receive only the preopened job directory and bounded fuel.
 */
internal object WasmtimeRuntime {
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
}
