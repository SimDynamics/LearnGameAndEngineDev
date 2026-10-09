package com.learngraphicsprogramming.app.runtime

import com.learngraphicsprogramming.app.BuildConfig
import com.learngraphicsprogramming.app.R
import com.learngraphicsprogramming.app.UiLanguageSettings
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
        // Experimental LLVM 23 uses its own native Clang + LLD pair.
        // Normal builds continue to use the validated FoldCode LLVM 21 pair.
        val clang = File(
            nativeDirectory,
            if (BuildConfig.LGP_CLANG23_ENABLED) "liblgp_clang23.so"
            else "libfoldclang.so"
        )
        val lldCore = File(nativeDirectory, "libfoldlldcore.so")
        val linkerExecutable = File(
            nativeDirectory,
            if (BuildConfig.LGP_CLANG23_ENABLED) "liblgp_lld23.so"
            else "liblgp_wasm_ld.so"
        )
        val linkerDirectory = File(runtimeDirectory, "bin").apply { mkdirs() }
        val linker = File(linkerDirectory, "wasm-ld")

        check(clang.isFile) { UiLanguageSettings.localizedContext(context).getString(R.string.math_ui_clang_missing) }
        check(linkerExecutable.isFile) { UiLanguageSettings.localizedContext(context).getString(R.string.math_ui_llvm_missing) }
        // FoldCode's LLD core belongs to the Clang 21 linker wrapper only.
        if (!BuildConfig.LGP_CLANG23_ENABLED) {
            check(lldCore.isFile) { UiLanguageSettings.localizedContext(context).getString(R.string.math_ui_llvm_missing) }
        }

        linker.delete()
        Os.symlink(linkerExecutable.absolutePath, linker.absolutePath)

        val sysroot = File(runtimeDirectory, "sysroot")
        val resourceDirectory = File(runtimeDirectory, "resource")
        val useCppExceptions = BuildConfig.LGP_CPP_EXCEPTIONS_ENABLED

        // Official WASI SDK 34 has distinct EH and no-EH C++ headers.
        val cppHeaders = File(
            sysroot,
            if (useCppExceptions) "include/wasm32-wasip1/eh/c++/v1"
            else "include/wasm32-wasip1/c++/v1"
        )
        check(cppHeaders.isDirectory) {
            "Faltan las cabeceras C++ del entorno WASI: ${cppHeaders.absolutePath}"
        }

        val exceptionFlags =
            if (useCppExceptions) listOf(
                "-fwasm-exceptions",
                "-mllvm", "-wasm-use-legacy-eh=false"
            ) else listOf("-fno-exceptions")

        val exceptionLinkFlags =
            if (useCppExceptions) listOf(
                "-L${File(sysroot, "lib/wasm32-wasip1/eh").absolutePath}",
                "-lunwind"
            ) else emptyList()

        val command = listOf(
            clang.absolutePath,
            "--driver-mode=g++",
            "--target=wasm32-wasip1",
            "--sysroot=${sysroot.absolutePath}",
            "-resource-dir=${resourceDirectory.absolutePath}",
            "-isystem",
            cppHeaders.absolutePath,
            "-std=c++23"
        ) + exceptionFlags + listOf(
            // Keep learning-oriented diagnostics compact in both modes.
            "-ferror-limit=1",
            "-fshow-overloads=best",
            "-static",
            "-O0",
            "-g0",
            sourceFile.absolutePath,
            "-Wl,--strip-all"
        ) + exceptionLinkFlags + listOf(
            "-o", wasmFile.absolutePath
        )

        val process = ProcessBuilder(command)
            .directory(workspace)
            .apply {
                environment()["LD_LIBRARY_PATH"] = nativeDirectory.absolutePath
                if (!BuildConfig.LGP_CLANG23_ENABLED) {
                    environment()["LGP_LLD_CORE"] = lldCore.absolutePath
                }
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
        val compilerStdout =
            cleanCompilerOutput(
                stdout.get(
                    5,
                    TimeUnit.SECONDS
                ),
                workspace,
                sourceFile,
                runtimeDirectory
            )

        val compilerStderr =
            cleanCompilerOutput(
                stderr.get(
                    5,
                    TimeUnit.SECONDS
                ),
                workspace,
                sourceFile,
                runtimeDirectory
            )

        readers.shutdownNow()

        val exitCode =
            if (finished) {
                process.exitValue()
            } else {
                TIMEOUT_EXIT_CODE
            }

        return CppCompilationResult(
            exitCode = exitCode,
            stdout = compilerStdout,
            stderr = compilerStderr,
            wasmFile = wasmFile.takeIf { exitCode == 0 && it.isFile },
            workspace = workspace,
            durationMillis = TimeUnit.NANOSECONDS.toMillis(System.nanoTime() - startedAt),
            timedOut = !finished
        )
    }

    private fun cleanCompilerOutput(
        text: String,
        workspace: File,
        sourceFile: File,
        runtimeDirectory: File
    ): String {

        if (
            text.isEmpty()
        ) {
            return ""
        }

        var cleaned =
            text

        /*
         * Archivo escrito por el usuario.
         */
        cleaned =
            cleaned.replace(
                sourceFile.absolutePath,
                "main.cpp"
            )

        /*
         * Archivos temporales de compilación.
         */
        cleaned =
            cleaned.replace(
                workspace.absolutePath +
                        File.separator,
                ""
            )

        /*
         * Cabeceras de la biblioteca estándar C++.
         */
        val cppIncludeDirectory =
            File(
                runtimeDirectory,
                "sysroot/include/wasm32-wasip1/c++/v1"
            )

        val cppHeaderRegex =
            Regex(
                Regex.escape(
                    cppIncludeDirectory.absolutePath +
                            File.separator
                ) +
                        """([^:\r\n]+)"""
            )

        cleaned =
            cleaned.replace(
                cppHeaderRegex
            ) { match ->

                "<${match.groupValues[1]}>"
            }

        /*
         * Cabeceras generales del sysroot WASI.
         */
        val sysrootIncludeDirectory =
            File(
                runtimeDirectory,
                "sysroot/include"
            )

        val sysrootHeaderRegex =
            Regex(
                Regex.escape(
                    sysrootIncludeDirectory.absolutePath +
                            File.separator
                ) +
                        """([^:\r\n]+)"""
            )

        cleaned =
            cleaned.replace(
                sysrootHeaderRegex
            ) { match ->

                "<${match.groupValues[1]}>"
            }

        /*
         * Clang puede generar decenas de notas
         * internas al buscar sobrecargas.
         *
         * Conservamos solamente notas que sí
         * ayudan directamente al estudiante.
         */
        val diagnosticRegex =
            Regex(
                """^.+?:\d+:\d+:\s+(error|warning|note):\s+.*$"""
            )

        val summaryRegex =
            Regex(
                """^\d+\s+(error|errors|warning|warnings)\s+generated\.$"""
            )

        val usefulNoteRegex =
            Regex(
                """note:.*(declared here|previous definition is here|expanded from macro)""",
                RegexOption.IGNORE_CASE
            )

        val outputLines =
            mutableListOf<String>()

        var skippingNoteContext =
            false

        for (
        line in cleaned.lines()
        ) {

            val diagnosticMatch =
                diagnosticRegex.matches(
                    line
                )

            /*
             * Comienza un nuevo diagnóstico.
             */
            if (
                diagnosticMatch
            ) {

                val isNote =
                    line.contains(
                        ": note:"
                    )

                if (
                    isNote &&
                    !usefulNoteRegex.containsMatchIn(
                        line
                    )
                ) {

                    /*
                     * Nota interna de Clang/libc++.
                     *
                     * También omitiremos las líneas
                     * de código y carets pertenecientes
                     * a esta nota.
                     */
                    skippingNoteContext =
                        true

                    continue
                }

                skippingNoteContext =
                    false

                outputLines.add(
                    line
                )

                continue
            }

            /*
             * No debemos perder:
             *
             * 1 error generated.
             */
            if (
                summaryRegex.matches(
                    line.trim()
                )
            ) {

                skippingNoteContext =
                    false

                outputLines.add(
                    line
                )

                continue
            }

            /*
             * Líneas pertenecientes a una nota
             * interna que hemos decidido ocultar.
             */
            if (
                skippingNoteContext
            ) {

                continue
            }

            outputLines.add(
                line
            )
        }

        return outputLines
            .joinToString(
                "\n"
            )
            .replace(
                Regex(
                    """\n{3,}"""
                ),
                "\n\n"
            )
            .trimEnd()
    }

    private fun prepareRuntime(): File {
        val runtimeDirectory = File(context.filesDir, "cpp-runtime/wasi-sdk-34")
        val marker = File(runtimeDirectory, ".ready")
        val expectedVersion =
            if (BuildConfig.LGP_CPP_EXCEPTIONS_ENABLED) "$RUNTIME_VERSION-eh-experiment-1"
            else RUNTIME_VERSION
        if (marker.takeIf(File::isFile)?.readText() == expectedVersion) return runtimeDirectory

        runtimeDirectory.deleteRecursively()
        runtimeDirectory.mkdirs()

        // Both assets are bundled at build time. No executable is downloaded.
        // The EH overlay replaces target headers/libs only in experimental APKs.
        fun unpackAsset(assetName: String, destination: File) {
            val canonicalRoot = destination.canonicalPath + File.separator
            context.assets.open(assetName).use { input ->
                ZipInputStream(input.buffered()).use { archive ->
                    while (true) {
                        val entry = archive.nextEntry ?: break
                        val output = File(destination, entry.name).canonicalFile
                        check(output.path.startsWith(canonicalRoot)) {
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
        }

        unpackAsset("runtime/wasi-runtime.zip", runtimeDirectory)
        if (BuildConfig.LGP_CPP_EXCEPTIONS_ENABLED) {
            unpackAsset("runtime/wasi-eh-sysroot.zip", File(runtimeDirectory, "sysroot"))
            val ehLibraries = File(runtimeDirectory, "sysroot/lib/wasm32-wasip1/eh")
            listOf("libc++.a", "libc++abi.a", "libunwind.a").forEach { name ->
                check(File(ehLibraries, name).isFile) {
                    "Biblioteca de excepciones WASI ausente: $name"
                }
            }
        }
        marker.writeText(expectedVersion)
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
    val durationMillis: Long,
    val timedOut: Boolean = false
)
