package com.learngraphicsprogramming.app.runtime

import android.content.Context
import android.os.Build
import android.system.Os
import com.learngraphicsprogramming.app.BuildConfig
import com.learngraphicsprogramming.app.narration.NarrationWorkPriority
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.security.MessageDigest
import java.util.UUID
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.CopyOnWriteArraySet
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import java.util.zip.ZipInputStream

internal enum class CppCmakePhase { CONFIGURATION, GENERATION, BUILD, EXECUTION }
internal data class CppCmakeRecord(val phase: CppCmakePhase, val exitCode: Int,
    val stdout: String = "", val stderr: String = "", val command: List<String> = emptyList(), val timedOut: Boolean = false)
internal data class CppCmakeResult(val phase: CppCmakePhase, val exitCode: Int,
    val stdout: String = "", val stderr: String = "", val timedOut: Boolean = false,
    val workspace: File, val artifact: File? = null, val execution: WasiExecutionResult? = null,
    val projectFingerprint: String, val toolsInfo: Map<String, String> = emptyMap(),
    val records: List<CppCmakeRecord> = emptyList(), val executionInput: String? = null)

/** Native CMake generates Ninja rules; only CMake --build invokes those rules and their compiler. */
internal class CppCmakeEngine(context: Context, sessionKey: String) {
    private val context = context.applicationContext
    val workspace = File(this.context.cacheDir, "cpp-cmake-jobs/${hash(sessionKey.toByteArray())}")
    private val sourceDirectory = File(workspace, "project")
    private val buildDirectory = File(workspace, "build")
    private val nativeDirectory get() = File(context.packageManager.getApplicationInfo(context.packageName, 0).nativeLibraryDir)
    private val stateFile = File(workspace, "state.json")
    private val lock = locks.getOrPut(workspace.absolutePath) { Any() }
    private val cmake get() = File(nativeDirectory, "liblgp_cmake.so")
    private val ninja get() = File(nativeDirectory, "liblgp_ninja.so")

    fun isBusy(): Boolean = (operations[workspace.absolutePath]?.get() ?: 0) > 0

    /** Observers belong to attached views, not to the worker's Activity instance. */
    fun observe(listener: (CppCmakeResult?) -> Unit): AutoCloseable {
        val set = listeners.getOrPut(workspace.absolutePath) { CopyOnWriteArraySet() }
        set.add(listener)
        listener(if (isBusy()) null else completedResults[workspace.absolutePath])
        return AutoCloseable { set.remove(listener) }
    }

    private fun notifyObservers(result: CppCmakeResult?) {
        listeners[workspace.absolutePath]?.forEach { listener -> runCatching { listener(result) } }
    }

    fun isConfigured(project: CppCmakeProject): Boolean =
        state().optString("configuration") == configurationKey(project) && File(buildDirectory, "build.ninja").isFile

    fun hasBuiltArtifact(project: CppCmakeProject, verifyContent: Boolean = true): Boolean {
        val saved = state()
        return saved.optString("built") == buildKey(project) && isConfigured(project) &&
            artifactFromState(saved)?.let { file -> !verifyContent || saved.optString("artifactSha256") == hash(file.readBytes()) } == true
    }

    fun configure(project: CppCmakeProject): CppCmakeResult = perform(project, CppCmakePhase.CONFIGURATION) {
        val saved = state()
        saved.put("configuration", "").put("built", "").remove("artifact")
        writeState(saved)
        syncFiles(project)
        val tools = prepareTools(project)
        val toolchain = File(workspace, "wasi-toolchain.cmake")
        toolchain.writeText(toolchainText(tools), Charsets.UTF_8)
        File(buildDirectory, ".cmake/api/v1/query/client-lgp").apply { mkdirs() }
            .resolve("query.json").writeText("""{"requests":[{"kind":"codemodel","version":2},{"kind":"toolchains","version":1},{"kind":"cache","version":2}]}""")
        // CMake owns cache renewal: removed options and compiler changes must be re-evaluated.
        val command = listOf(cmake.absolutePath, "--fresh", "-S", sourceDirectory.absolutePath, "-B", buildDirectory.absolutePath,
            "-G", "Ninja", "-DCMAKE_TOOLCHAIN_FILE=${toolchain.absolutePath}",
            // The target is WASI Preview 1. Defining it before project() also prevents
            // Android host detection from assuming a Termux/NDK PREFIX on the phone.
            "-DCMAKE_SYSTEM_NAME=Generic", "-DCMAKE_SYSTEM_VERSION=1",
            "-DCMAKE_MAKE_PROGRAM=${ninja.absolutePath}", "-DCMAKE_BUILD_TYPE=${project.buildType}",
            "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON") +
            project.options.toSortedMap().map { (key, value) -> "-D$key=$value" }
        val invocation = invoke(command, tools)
        val configurationIncomplete = invocation.stdout.lineSequence().any {
            it.trim() == "-- Configuring incomplete, errors occurred!"
        }
        val configured = !configurationIncomplete && invocation.stdout.lineSequence().any {
            Regex("-- Configuring done(?: \\([0-9.]+s\\))?").matches(it.trim())
        }
        val phase = if (invocation.exitCode == 0 || configured) CppCmakePhase.GENERATION else CppCmakePhase.CONFIGURATION
        val records = if (configured || invocation.exitCode == 0) listOf(
            CppCmakeRecord(CppCmakePhase.CONFIGURATION, 0, command = command),
            invocation.copy(phase = phase)) else listOf(invocation.copy(phase = phase))
        if (invocation.exitCode == 0) {
            check(File(buildDirectory, "build.ninja").isFile) { "CMake no generó las reglas Ninja." }
            fileApiTargets() // A real reply is required, not a guessed target filename.
            saved.put("configuration", configurationKey(project)).put("tools", JSONObject(tools.info))
            writeState(saved)
        }
        result(project, phase, invocation, tools.info, records)
    }

    fun build(project: CppCmakeProject): CppCmakeResult = perform(project, CppCmakePhase.BUILD) {
        val saved = state()
        saved.put("built", "").remove("artifact")
        writeState(saved)
        check(isConfigured(project)) { "Configura y genera el proyecto vigente antes de construirlo." }
        syncFiles(project)
        val tools = prepareTools(project)
        val command = listOf(cmake.absolutePath, "--build", buildDirectory.absolutePath,
            "--target", project.target, "--parallel", "2")
        val invocation = invoke(command, tools).copy(phase = CppCmakePhase.BUILD)
        var artifact: File? = null
        if (invocation.exitCode == 0) {
            val target = fileApiTargets().firstOrNull { it.optString("name") == project.target }
            if (target?.optString("type") == "EXECUTABLE") {
                artifact = target.optJSONArray("artifacts")?.let { array ->
                    (0 until array.length()).map { index -> checkedArtifact(array.getJSONObject(index).getString("path")) }
                        .firstOrNull { it.isFile && isWasm(it) }
                }
                check(artifact != null) { "El objetivo ejecutable no produjo un módulo WASI dentro del proyecto." }
                saved.put("built", buildKey(project)).put("artifact", artifact.relativeTo(workspace.canonicalFile).invariantSeparatorsPath)
                    .put("artifactSha256", hash(artifact.readBytes()))
            }
            saved.put("tools", JSONObject(tools.info))
            writeState(saved)
        }
        result(project, CppCmakePhase.BUILD, invocation, tools.info, listOf(invocation), artifact)
    }

    fun run(project: CppCmakeProject, standardInput: String = ""): CppCmakeResult = perform(project, CppCmakePhase.EXECUTION) {
        check(hasBuiltArtifact(project)) { "Construye el objetivo de la versión vigente antes de ejecutarlo." }
        val saved = state()
        val artifact = requireNotNull(artifactFromState(saved))
        val executionDirectory = File(workspace, "runs/${UUID.randomUUID()}").apply { mkdirs() }
        val execution = WasiRuntime.run(artifact.readBytes(), executionDirectory, standardInput)
        val record = CppCmakeRecord(CppCmakePhase.EXECUTION, execution.exitCode, execution.stdout, execution.stderr,
            listOf(if (BuildConfig.LGP_WASMTIME_ENABLED) "Wasmtime" else "WAMR", artifact.absolutePath))
        CppCmakeResult(CppCmakePhase.EXECUTION, execution.exitCode, workspace = workspace,
            artifact = artifact, execution = execution, projectFingerprint = project.fingerprint,
            toolsInfo = stringMap(saved.optJSONObject("tools")), records = listOf(record), executionInput = standardInput)
    }

    private fun perform(project: CppCmakeProject, phase: CppCmakePhase, action: () -> CppCmakeResult): CppCmakeResult {
        val pending = operations.getOrPut(workspace.absolutePath) { AtomicInteger() }
        pending.incrementAndGet()
        notifyObservers(null)
        var completed: CppCmakeResult? = null
        try {
            return synchronized(lock) {
                val priority = NarrationWorkPriority.beginInteractive()
                try { workspace.mkdirs(); action() }
                catch (error: Exception) {
                    val saved = state().put("built", "")
                    saved.remove("artifact")
                    if (phase == CppCmakePhase.CONFIGURATION) saved.put("configuration", "")
                    runCatching { writeState(saved) }.onFailure { stateFile.delete() }
                    CppCmakeResult(phase, 127, stderr = error.message ?: "No se pudo completar la fase del proyecto.",
                        workspace = workspace, projectFingerprint = project.fingerprint,
                        toolsInfo = stringMap(saved.optJSONObject("tools")))
                } finally { priority.close() }
            }.also { completed = it }
        } finally {
            completed?.let { completedResults[workspace.absolutePath] = it }
            pending.decrementAndGet()
            notifyObservers(completed)
        }
    }

    private data class Tools(val runtimeRoot: File, val sysroot: File, val resources: File,
        val compiler: File, val headers: File, val bin: File, val useClang21: Boolean, val exceptions: Boolean,
        val info: Map<String, String>)

    private fun prepareTools(project: CppCmakeProject): Tools {
        check(Build.VERSION.SDK_INT >= MIN_ANDROID_API) { "Este laboratorio CMake requiere Android 9 o posterior." }
        check(cmake.isFile && cmake.canExecute() && ninja.isFile && ninja.canExecute()) { "Faltan las herramientas CMake y Ninja instaladas con la aplicación." }
        if (project.compilerVariant == CppCompilerVariant.OPTIONAL_CLANG_21) check(CppCompiler.isOptionalClang21Available(context)) {
            "El compilador Clang 21 seleccionado no está instalado; conserva la selección o instala el módulo." }
        val useClang21 = !BuildConfig.LGP_CLANG23_ENABLED || project.compilerVariant == CppCompilerVariant.OPTIONAL_CLANG_21
        val compiler = File(nativeDirectory, if (useClang21) "libfoldclang.so" else "liblgp_clang23.so")
        val linker = File(nativeDirectory, if (useClang21) "liblgp_wasm_ld.so" else "liblgp_lld23.so")
        check(compiler.isFile && compiler.canExecute() && linker.isFile && linker.canExecute()) { "El compilador o enlazador seleccionado no está disponible." }
        val cmakeRoot = prepareCmakeResources()
        val runtime = CppCompiler(context).prepareRuntime()
        val sysroot = File(runtime, "sysroot")
        val exceptions = BuildConfig.LGP_CPP_EXCEPTIONS_ENABLED && !useClang21
        val headers = File(sysroot, if (exceptions) "include/wasm32-wasip1/eh/c++/v1" else "include/wasm32-wasip1/c++/v1")
        check(headers.isDirectory) { "Faltan las cabeceras C++ del entorno WASI." }
        val bin = File(workspace, "bin").apply { mkdirs() }
        symlink(File(bin, "wasm-ld"), linker)
        val archiver = File(nativeDirectory, "liblgp_llvm_ar.so")
        if (archiver.isFile) {
            symlink(File(bin, "llvm-ar"), archiver)
            symlink(File(bin, "llvm-ranlib"), archiver)
        }
        val temporary = Tools(cmakeRoot, sysroot, File(runtime, "resource"), compiler, headers, bin, useClang21, exceptions, emptyMap())
        val versions = linkedMapOf("generator" to "Ninja", "targetPlatform" to "wasm32-wasip1",
            "cmake" to invoke(listOf(cmake.absolutePath, "--version"), temporary).stdout.lineSequence().firstOrNull().orEmpty(),
            "ninja" to invoke(listOf(ninja.absolutePath, "--version"), temporary).stdout.trim(),
            "compiler" to invoke(listOf(compiler.absolutePath, "--driver-mode=g++", "--version"), temporary).stdout.lineSequence().firstOrNull().orEmpty(),
            "compilerVariant" to project.compilerVariant.name,
            "runtime" to if (BuildConfig.LGP_WASMTIME_ENABLED) "Wasmtime" else "WAMR")
        return temporary.copy(info = versions)
    }

    private fun toolchainText(tools: Tools): String {
        fun quote(text: String) = "\"" + text.replace("\\", "\\\\").replace("\"", "\\\"").replace("$", "\\$") + "\""
        fun path(file: File) = quote(file.absolutePath)
        val flags = listOf("-resource-dir=${tools.resources.absolutePath}", "-isystem", tools.headers.absolutePath,
            "-ferror-limit=1", "-fshow-overloads=best") +
            if (tools.exceptions) listOf("-fwasm-exceptions", "-mllvm", "-wasm-use-legacy-eh=false") else listOf("-fno-exceptions")
        val flagsText = flags.joinToString(" ") { token -> if (token.contains(' ')) "\"$token\"" else token }
        val links = "-static -Wl,--strip-all" + if (tools.exceptions) " -L${File(tools.sysroot, "lib/wasm32-wasip1/eh").absolutePath} -lunwind" else ""
        return buildString {
            appendLine("set(CMAKE_SYSTEM_NAME Generic)")
            appendLine("set(CMAKE_SYSTEM_VERSION 1)")
            appendLine("set(CMAKE_SYSTEM_PROCESSOR wasm32)")
            appendLine("set(CMAKE_CXX_COMPILER ${path(tools.compiler)} CACHE FILEPATH \"C++ compiler\")")
            appendLine("set(CMAKE_CXX_COMPILER_ARG1 \"--driver-mode=g++\")")
            appendLine("set(CMAKE_CXX_COMPILER_TARGET wasm32-wasip1)")
            appendLine("set(CMAKE_SYSROOT ${path(tools.sysroot)})")
            appendLine("set(CMAKE_CXX_FLAGS_INIT ${quote(flagsText)})")
            appendLine("set(CMAKE_EXE_LINKER_FLAGS_INIT ${quote(links)})")
            appendLine("set(CMAKE_EXECUTABLE_SUFFIX .wasm)")
            appendLine("set(CMAKE_EXECUTABLE_SUFFIX_CXX .wasm)")
            appendLine("set(CMAKE_FIND_ROOT_PATH ${path(tools.sysroot)})")
            appendLine("set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)")
            for (kind in listOf("LIBRARY", "INCLUDE", "PACKAGE")) appendLine("set(CMAKE_FIND_ROOT_PATH_MODE_$kind ONLY)")
            if (File(tools.bin, "llvm-ar").isFile) {
                appendLine("set(CMAKE_AR ${path(File(tools.bin, "llvm-ar"))} CACHE FILEPATH \"Archiver\")")
                appendLine("set(CMAKE_RANLIB ${path(File(tools.bin, "llvm-ranlib"))} CACHE FILEPATH \"Indexer\")")
            }
        }
    }

    private fun invoke(command: List<String>, tools: Tools): CppCmakeRecord {
        val process = ProcessBuilder(command).directory(workspace).apply {
            environment()["LGP_CMAKE_ROOT"] = tools.runtimeRoot.absolutePath
            environment()["LD_LIBRARY_PATH"] = nativeDirectory.absolutePath
            environment()["PATH"] = "${tools.bin.absolutePath}:/system/bin:/system/xbin"
            environment()["TMPDIR"] = workspace.absolutePath
            environment()["HOME"] = workspace.absolutePath
            if (tools.useClang21) environment()["LGP_LLD_CORE"] = File(nativeDirectory, "libfoldlldcore.so").absolutePath
        }.start()
        val readers = Executors.newFixedThreadPool(2)
        try {
            val stdout = readers.submit<String> { process.inputStream.bufferedReader().use { it.readText() } }
            val stderr = readers.submit<String> { process.errorStream.bufferedReader().use { it.readText() } }
            val finished = process.waitFor(120, TimeUnit.SECONDS)
            if (!finished) process.destroyForcibly()
            return CppCmakeRecord(CppCmakePhase.CONFIGURATION, if (finished) process.exitValue() else -2,
                stdout.get(5, TimeUnit.SECONDS), stderr.get(5, TimeUnit.SECONDS), command.toList(), !finished)
        } finally { readers.shutdownNow() }
    }

    private fun syncFiles(project: CppCmakeProject) {
        sourceDirectory.mkdirs()
        buildDirectory.mkdirs()
        val manifestFile = File(workspace, "source-manifest.json")
        val previous = runCatching { JSONArray(manifestFile.readText()) }.getOrDefault(JSONArray())
        for (index in 0 until previous.length()) {
            val path = previous.getString(index)
            CppProject.validatePath(path)
            if (path !in project.files) {
                val file = checkedSource(path)
                check(!file.exists() || file.delete()) { "No se pudo retirar el archivo anterior: $path" }
                // Only empty parents belonging to a retired file may be removed. This
                // permits a former directory path to become a regular project file.
                var parent = file.parentFile
                while (parent != null && parent != sourceDirectory.canonicalFile && parent.isDirectory &&
                    parent.path.startsWith(sourceDirectory.canonicalPath + File.separator) && parent.list()?.isEmpty() == true) {
                    check(parent.delete()) { "No se pudo retirar la carpeta vacía anterior: ${parent.name}" }
                    parent = parent.parentFile
                }
            }
        }
        project.files.forEach { (path, code) ->
            val file = checkedSource(path)
            file.parentFile?.mkdirs()
            checkedSource(path)
            if (!file.isFile || file.readText(Charsets.UTF_8) != code) file.writeText(code, Charsets.UTF_8)
        }
        manifestFile.writeText(JSONArray(project.files.keys.toList()).toString())
    }
    private fun checkedSource(path: String): File = File(sourceDirectory, path).canonicalFile.also {
        require(it.path.startsWith(sourceDirectory.canonicalPath + File.separator)) { "El archivo sale del proyecto: $path" }
    }
    private fun checkedArtifact(path: String): File = File(buildDirectory, path).canonicalFile.also {
        require(it.path.startsWith(workspace.canonicalPath + File.separator)) { "El artefacto sale del directorio de construcción aislado." }
    }
    private fun artifactFromState(saved: JSONObject): File? = saved.optString("artifact").takeIf { it.isNotEmpty() }?.let { path ->
        runCatching { File(workspace, path).canonicalFile.takeIf {
            it.path.startsWith(workspace.canonicalPath + File.separator) && it.isFile && isWasm(it)
        } }.getOrNull()
    }
    private fun fileApiTargets(): List<JSONObject> {
        val reply = File(buildDirectory, ".cmake/api/v1/reply")
        val index = requireNotNull(reply.listFiles()?.filter { it.name.startsWith("index-") && it.extension == "json" }?.maxByOrNull { it.lastModified() }) {
            "CMake no produjo una respuesta file-api." }
        val objects = JSONObject(index.readText()).getJSONArray("objects")
        val modelFile = (0 until objects.length()).map { objects.getJSONObject(it) }
            .first { it.getString("kind") == "codemodel" }.getString("jsonFile")
        val model = JSONObject(File(reply, modelFile).readText())
        val configurations = model.getJSONArray("configurations")
        return (0 until configurations.length()).flatMap { config ->
            val targets = configurations.getJSONObject(config).optJSONArray("targets") ?: JSONArray()
            (0 until targets.length()).map { target -> JSONObject(File(reply, targets.getJSONObject(target).getString("jsonFile")).readText()) }
        }
    }
    private fun prepareCmakeResources(): File = synchronized(resourceLock) {
        val root = File(context.filesDir, "cpp-cmake-tools/cmake-4.1.2")
        val manifest = context.assets.open("runtime/cmake-tools-manifest.json").use { it.readBytes() }
        val version = hash(manifest)
        val marker = File(root, ".ready")
        if (marker.isFile && marker.readText() == version && File(root, "Modules/CMake.cmake").isFile) return@synchronized root
        root.mkdirs()
        val prefix = root.canonicalPath + File.separator
        context.assets.open("runtime/cmake-4.1.2-resources.zip").use { source ->
            ZipInputStream(source.buffered()).use { zip ->
                while (true) {
                    val entry = zip.nextEntry ?: break
                    val destination = File(root, entry.name).canonicalFile
                    check(destination.path.startsWith(prefix)) { "Entrada no segura en los recursos CMake." }
                    if (entry.isDirectory) destination.mkdirs() else {
                        destination.parentFile?.mkdirs()
                        destination.outputStream().buffered().use { zip.copyTo(it) }
                    }
                    zip.closeEntry()
                }
            }
        }
        check(File(root, "Modules/CMake.cmake").isFile) { "Faltan los módulos de CMake." }
        marker.writeText(version)
        root
    }
    private fun result(project: CppCmakeProject, phase: CppCmakePhase, invocation: CppCmakeRecord,
        info: Map<String, String>, records: List<CppCmakeRecord>, artifact: File? = null) = CppCmakeResult(phase,
        invocation.exitCode, invocation.stdout, invocation.stderr, invocation.timedOut, workspace, artifact,
        projectFingerprint = project.fingerprint, toolsInfo = info, records = records)
    private fun state(): JSONObject = runCatching { JSONObject(stateFile.readText()) }.getOrDefault(JSONObject())
    private fun writeState(value: JSONObject) {
        workspace.mkdirs()
        val pending = File(workspace, "state.pending.json")
        pending.writeText(value.toString())
        check(pending.renameTo(stateFile)) { "No se pudo guardar el estado del proyecto." }
    }
    private fun configurationKey(project: CppCmakeProject) = project.configurationFingerprint + ":" + installationSignature()
    private fun buildKey(project: CppCmakeProject) = project.fingerprint + ":" + installationSignature()
    private fun installationSignature() = nativeDirectory.absolutePath + ":${BuildConfig.LGP_CLANG23_ENABLED}:${BuildConfig.LGP_CPP_EXCEPTIONS_ENABLED}"

    companion object {
        const val MIN_ANDROID_API = 28
        private val locks = ConcurrentHashMap<String, Any>()
        private val operations = ConcurrentHashMap<String, AtomicInteger>()
        private val listeners = ConcurrentHashMap<String, CopyOnWriteArraySet<(CppCmakeResult?) -> Unit>>()
        private val completedResults = ConcurrentHashMap<String, CppCmakeResult>()
        private val resourceLock = Any()
        private fun hash(bytes: ByteArray) = MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it) }
        private fun stringMap(json: JSONObject?): Map<String, String> = json?.keys()?.asSequence()?.associateWith { json.getString(it) }.orEmpty()
        private fun isWasm(file: File) = file.inputStream().use { stream ->
            val magic = ByteArray(4)
            stream.read(magic) == 4 && magic.contentEquals(byteArrayOf(0, 97, 115, 109))
        }
        private fun symlink(link: File, target: File) {
            // Links refer to read-only platform-installed executables, never copied binaries.
            if (runCatching { Os.readlink(link.absolutePath) }.getOrNull() == target.absolutePath) return
            if (link.exists() || runCatching { Os.readlink(link.absolutePath) }.isSuccess) check(link.delete())
            Os.symlink(target.absolutePath, link.absolutePath)
        }
    }
}
