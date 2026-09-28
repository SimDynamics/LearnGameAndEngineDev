package com.learngraphicsprogramming.app.runtime

import android.content.Context
import android.system.Os
import java.io.File
import java.util.UUID
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.zip.ZipInputStream

internal class CppCompiler(private val context: Context) {

    fun compile(source: String): CppCompilationResult {
        val startedAt = System.nanoTime()
        val runtimeDirectory = prepareRuntime()
        val workspace = File(context.cacheDir, "cpp-jobs/${UUID.randomUUID()}").apply { mkdirs() }
        val sourceFile = File(workspace, "main.cpp").apply { writeText(source) }
        val wasmFile = File(workspace, "program.wasm")
        val nativeDirectory = File(context.applicationInfo.nativeLibraryDir)
        val clang = File(nativeDirectory, "libfoldclang.so")
        val lldCore = File(nativeDirectory, "libfoldlldcore.so")
        val linkerDirectory = File(runtimeDirectory, "bin").apply { mkdirs() }
        val linker = File(linkerDirectory, "wasm-ld")

        check(clang.isFile) { "No se encontró el ejecutable local de Clang." }
        check(lldCore.isFile) { "No se encontró el enlazador local de LLVM." }

        if (linker.exists()) linker.delete()
        Os.symlink(File(nativeDirectory, "liblgp_wasm_ld.so").absolutePath, linker.absolutePath)

        val sysroot = File(runtimeDirectory, "sysroot")
        val resourceDirectory = File(runtimeDirectory, "resource")
        val command = listOf(
            clang.absolutePath,
            "--driver-mode=g++",
            "--target=wasm32-wasip1",
            "--sysroot=${sysroot.absolutePath}",
            "-resource-dir=${resourceDirectory.absolutePath}",
            "-isystem",
            File(sysroot, "include/wasm32-wasip1/c++/v1").absolutePath,
            "-std=c++20",
            "-static",
            "-O0",
            "-g0",
            sourceFile.absolutePath,
            "-Wl,--strip-all",
            "-o",
            wasmFile.absolutePath
        )

        val process = ProcessBuilder(command)
            .directory(workspace)
            .apply {
                environment()["LD_LIBRARY_PATH"] = nativeDirectory.absolutePath
                environment()["LGP_LLD_CORE"] = lldCore.absolutePath
                environment()["PATH"] = "${linkerDirectory.absolutePath}:/system/bin:/system/xbin"
                environment()["TMPDIR"] = workspace.absolutePath
                environment()["HOME"] = workspace.absolutePath
            }
            .start()

        val readers = Executors.newFixedThreadPool(2)
        val stdout = readers.submit<String> { process.inputStream.bufferedReader().use { it.readText() } }
        val stderr = readers.submit<String> { process.errorStream.bufferedReader().use { it.readText() } }
        val finished = process.waitFor(COMPILER_TIMEOUT_SECONDS, TimeUnit.SECONDS)
        if (!finished) process.destroyForcibly()
        val compilerStdout = stdout.get(5, TimeUnit.SECONDS)
        val compilerStderr = stderr.get(5, TimeUnit.SECONDS)
        readers.shutdownNow()
        val exitCode = if (finished) process.exitValue() else TIMEOUT_EXIT_CODE

        return CppCompilationResult(
            exitCode = exitCode,
            stdout = compilerStdout,
            stderr = if (finished) compilerStderr else "$compilerStderr\nLa compilación superó el límite de tiempo.",
            wasmFile = wasmFile.takeIf { exitCode == 0 && it.isFile },
            workspace = workspace,
            durationMillis = TimeUnit.NANOSECONDS.toMillis(System.nanoTime() - startedAt)
        )
    }

    private fun prepareRuntime(): File {
        val runtimeDirectory = File(context.filesDir, "cpp-runtime/wasi-sdk-34")
        val marker = File(runtimeDirectory, ".ready")
        if (marker.takeIf(File::isFile)?.readText() == RUNTIME_VERSION) return runtimeDirectory

        runtimeDirectory.deleteRecursively()
        runtimeDirectory.mkdirs()
        context.assets.open("runtime/wasi-runtime.zip").use { input ->
            ZipInputStream(input.buffered()).use { archive ->
                while (true) {
                    val entry = archive.nextEntry ?: break
                    val output = File(runtimeDirectory, entry.name).canonicalFile
                    check(output.path.startsWith(runtimeDirectory.canonicalPath + File.separator)) {
                        "Entrada no segura en el sysroot: ${entry.name}"
                    }
                    if (entry.isDirectory) {
                        output.mkdirs()
                    } else {
                        output.parentFile?.mkdirs()
                        output.outputStream().buffered().use(archive::copyTo)
                    }
                    archive.closeEntry()
                }
            }
        }
        marker.writeText(RUNTIME_VERSION)
        return runtimeDirectory
    }

    private companion object {
        const val RUNTIME_VERSION = "wasi-sdk-34-noeh-0f338c64"
        const val COMPILER_TIMEOUT_SECONDS = 60L
        const val TIMEOUT_EXIT_CODE = -2
    }
}

internal data class CppCompilationResult(
    val exitCode: Int,
    val stdout: String,
    val stderr: String,
    val wasmFile: File?,
    val workspace: File,
    val durationMillis: Long
)
