import java.io.File
import java.net.URI
import java.security.MessageDigest
import java.util.zip.ZipFile
import org.gradle.api.tasks.bundling.Zip
import org.gradle.api.DefaultTask
import org.gradle.api.file.ArchiveOperations
import org.gradle.api.file.DirectoryProperty
import org.gradle.api.file.FileSystemOperations
import org.gradle.api.provider.Property
import org.gradle.api.tasks.Input
import org.gradle.api.tasks.Internal
import org.gradle.api.tasks.OutputDirectory
import org.gradle.api.tasks.TaskAction
import javax.inject.Inject

plugins {
    alias(libs.plugins.android.application)
}

// Configuration-cache-safe typed task; no script/project capture in the task action.
abstract class PrepareWasiSdk34ReferenceTask : DefaultTask() {
    @get:Input abstract val archiveUrl: Property<String>
    @get:Input abstract val expectedSha256: Property<String>
    @get:Internal abstract val archiveCacheDirectory: DirectoryProperty
    @get:OutputDirectory abstract val extractedSdkDirectory: DirectoryProperty

    @get:Inject abstract val archives: ArchiveOperations
    @get:Inject abstract val files: FileSystemOperations

    @TaskAction
    fun extractVerifiedSysroot() {
        val name = "wasi-sdk-34.0-x86_64-linux.tar.gz"
        val url = archiveUrl.get()
        val expected = expectedSha256.get()

        val cache = archiveCacheDirectory.get().asFile
        cache.mkdirs()
        val archive = File(cache, name)

        fun hash(file: File): String {
            val md = MessageDigest.getInstance("SHA-256")
            file.inputStream().buffered().use { stream ->
                val buffer = ByteArray(1024 * 1024)
                while (true) {
                    val count = stream.read(buffer)
                    if (count < 0) break
                    md.update(buffer, 0, count)
                }
            }
            return md.digest().joinToString("") { "%02x".format(it) }
        }
        if (!archive.isFile || hash(archive) != expected) {
            archive.delete()
            logger.lifecycle("Conectando a la descarga oficial de WASI SDK 34...")
            val connection = URI(url).toURL().openConnection().apply {
                connectTimeout = 30_000
                readTimeout = 120_000
            }
            val contentLength = connection.contentLengthLong
            val mib = 1024.0 * 1024.0
            var downloadedBytes = 0L
            var lastProgressNanos = System.nanoTime()
            logger.lifecycle("Descargando WASI SDK 34; avance cada 4 segundos...")

            try {
                connection.getInputStream().buffered().use { src ->
                    archive.outputStream().buffered().use { destination ->
                        val buffer = ByteArray(1024 * 1024)
                        while (true) {
                            val count = src.read(buffer)
                            if (count < 0) break
                            destination.write(buffer, 0, count)
                            downloadedBytes += count

                            val now = System.nanoTime()
                            if (now - lastProgressNanos >= 4_000_000_000L) {
                                val downloadedMiB = (downloadedBytes / mib).toInt()
                                if (contentLength > 0L) {
                                    val percent = downloadedBytes * 100 / contentLength
                                    logger.lifecycle(
                                        "WASI SDK 34: ${downloadedMiB} MiB / " +
                                            "${(contentLength / mib).toInt()} MiB (${percent}%)"
                                    )
                                } else {
                                    logger.lifecycle("WASI SDK 34: ${downloadedMiB} MiB descargados")
                                }
                                lastProgressNanos = now
                            }
                        }
                    }
                }
            } catch (error: Exception) {
                archive.delete()
                throw error
            }
            logger.lifecycle(
                "Descarga completa: ${(downloadedBytes / mib).toInt()} MiB. Verificando SHA-256..."
            )
        } else {
            logger.lifecycle("WASI SDK 34 encontrado en caché; SHA-256 correcto.")
        }
        check(hash(archive) == expected) { "El SHA256 de WASI SDK 34 no coincide." }
        logger.lifecycle("SHA-256 correcto. Extrayendo bibliotecas WASI SDK 34...")

        val root = extractedSdkDirectory.get().asFile
        files.delete { delete(root) }
        root.mkdirs()

        // The Android app needs the WASI target sysroot, not Linux-host
        // executables. Gradle's archive reader avoids Windows tar.exe stalls
        // and allows us to extract only the required files.
        val sdkFolderName = "wasi-sdk-34.0-x86_64-linux"
        val sysrootPath = "$sdkFolderName/share/wasi-sysroot"
        var extractedFiles = 0
        var extractedBytes = 0L
        logger.lifecycle("Extrayendo solamente $sysrootPath mediante Gradle...")
        files.copy {
            from(archives.tarTree(archives.gzip(archive))) {
                include("$sysrootPath/**")
                includeEmptyDirs = false
                eachFile {
                    extractedFiles++
                    extractedBytes += size
                    if (extractedFiles == 1 || extractedFiles % 500 == 0) {
                        logger.lifecycle(
                            "WASI SDK 34: $extractedFiles archivos del sysroot " +
                                "(${(extractedBytes / 1048576.0).toInt()} MiB de datos)"
                        )
                    }
                }
            }
            into(root)
        }
        logger.lifecycle("Extracción finalizada: $extractedFiles archivos del sysroot.")
        check(extractedFiles > 0) {
            "WASI SDK 34: no se encontraron archivos en $sysrootPath."
        }

        val sysroot = File(root, sysrootPath)
        check(sysroot.isDirectory) {
            "WASI SDK 34 oficial: no se encontró share/wasi-sysroot."
        }

        // Do not discover archives by filename across the entire SDK:
        // wasm32-wasip3/noeh and llvm-lto are different ABI configurations.
        // All three non-LTO libraries must come from wasm32-wasip1/eh.
        val exceptionLibraries = File(sysroot, "lib/wasm32-wasip1/eh")
        check(exceptionLibraries.isDirectory) {
            "WASI SDK 34: falta lib/wasm32-wasip1/eh."
        }
        listOf("libc++.a", "libc++abi.a", "libunwind.a").forEach { name ->
            val library = File(exceptionLibraries, name)
            check(library.isFile && library.length() > 0L) {
                "WASI SDK 34: falta la biblioteca EH de wasm32-wasip1: ${library.absolutePath}"
            }
            logger.lifecycle("Biblioteca EH verificada: ${library.absolutePath}")
        }

        logger.lifecycle("WASI SDK 34 verificado. Sysroot: ${sysroot.absolutePath}")
        logger.lifecycle("Sysroot EH listo para experimentos Clang 23 + Wasmtime; APK sin cambios.")
    }
}

// Validated default: Clang 23 + Wasmtime 49 + C++23 WebAssembly EH.
// A single -Plgp.clang23=false selects the previous Clang 21 + WAMR + no-EH
// toolchain. Individual toggles remain available for regression experiments.
// This is a build-time choice; the optional Play Feature module comes later.
fun lgpBooleanProperty(name: String, defaultValue: Boolean): Boolean {
    val requested = providers.gradleProperty(name).orNull ?: return defaultValue
    check(requested.equals("true", ignoreCase = true) ||
        requested.equals("false", ignoreCase = true)) {
        "$name must be true or false; received '$requested'."
    }
    return requested.equals("true", ignoreCase = true)
}

val lgpEnableClang23 = lgpBooleanProperty("lgp.clang23", true)
val lgpEnableWasmtime = lgpBooleanProperty("lgp.wasmtime", lgpEnableClang23)
val lgpEnableCppExceptions = lgpBooleanProperty(
    "lgp.cppExceptions", lgpEnableClang23 && lgpEnableWasmtime
)

// Experimental Play-delivered feature stub. Never included by normal builds.
val lgpEnablePlayClang21Feature = lgpBooleanProperty("lgp.playClang21Feature", false)
check(!lgpEnablePlayClang21Feature || (lgpEnableClang23 && lgpEnableWasmtime)) {
    "The optional Clang 21 Play module requires a Clang 23 + Wasmtime base APK."
}

check(!lgpEnableCppExceptions || (lgpEnableClang23 && lgpEnableWasmtime)) {
    "C++ exceptions require Clang 23 and Wasmtime. " +
        "Use -Plgp.clang23=false for the Clang 21/WAMR fallback."
}

// Crash-stage files and verbose JNI logs are opt-in, never enabled by default.
val lgpWasmtimeDiagnostics =
    lgpEnableWasmtime &&
        providers.gradleProperty("lgp.wasmtimeDiagnostics").orNull.equals("true", ignoreCase = true)

android {
    namespace = "com.learngraphicsprogramming.app"
    if (lgpEnablePlayClang21Feature) {
        dynamicFeatures += setOf(":clang21")
    }
    compileSdk {
        version = release(37)
    }

    defaultConfig {
        applicationId = "com.learngraphicsprogramming.app"
        minSdk = 24
        targetSdk = 37
        versionCode = 1
        versionName = "1.0"

        ndk {
            abiFilters += "arm64-v8a"
        }

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        externalNativeBuild {
            cmake {
                cppFlags += "-std=c++17"
                arguments += "-DLGP_ENABLE_WASMTIME=${if (lgpEnableWasmtime) "ON" else "OFF"}"
                arguments += "-DLGP_WASMTIME_ROOT=${layout.buildDirectory.get().asFile.absolutePath.replace("\\", "/")}/generated/wasmtime/sdk"
            }
        }
        buildConfigField("boolean", "LGP_WASMTIME_ENABLED", lgpEnableWasmtime.toString())
        buildConfigField("boolean", "LGP_CLANG23_ENABLED", lgpEnableClang23.toString())
        buildConfigField("boolean", "LGP_CPP_EXCEPTIONS_ENABLED", lgpEnableCppExceptions.toString())
        buildConfigField("boolean", "LGP_PLAY_CLANG21_FEATURE_ENABLED", lgpEnablePlayClang21Feature.toString())
        buildConfigField("boolean", "LGP_WASMTIME_DIAGNOSTICS", lgpWasmtimeDiagnostics.toString())
    }

    buildTypes {
        release {
            optimization {
                enable = false
            }
        }
    }
    compileOptions {
        isCoreLibraryDesugaringEnabled = true
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }
    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }
    buildFeatures {
        viewBinding = true
        buildConfig = true
    }
    androidResources {
        generateLocaleConfig = true
    }
    testOptions {
        unitTests.all {
            it.systemProperty("translation.resources", file("src/main/res").absolutePath)
        }
    }
    sourceSets {
        getByName("main").jniLibs.directories.add("build/generated/cppRuntime/jniLibs")
        if (lgpEnableCppExceptions) {
            // This optional overlay is absent from all normal builds.
            getByName("main").assets.directories.add("build/generated/wasiEhRuntime/assets")
        }
    }
    packaging {
        jniLibs.useLegacyPackaging = true
        resources {
            // Retain notices from every mathematical dependency in the packaged app.
            merges += setOf("META-INF/LICENSE*", "META-INF/NOTICE*", "META-INF/DEPENDENCIES")
            // Symja external and JGraphT contain byte-identical copies of these schemas.
            pickFirsts += setOf("graphml.xsd", "xlink.xsd", "viz.xsd", "gexf.xsd")
        }
    }
}

val prepareCppRuntime by tasks.registering {
    group = "build setup"
    description = "Prepara las bibliotecas ARM64 de Clang/LLVM verificadas para el prototipo offline."
    // The task action captures only task-local paths/providers, not the Gradle script.

    val outputDirectory = layout.buildDirectory.dir("generated/cppRuntime/jniLibs/arm64-v8a")
    val experimentalDirectory =
        rootProject.layout.projectDirectory.dir(".gradle/clang23-android-arm64").asFile
    // Read Gradle project properties during configuration, never inside doLast.
    val cacheDirectoryPath =
        rootProject.layout.projectDirectory.dir(".gradle/cpp-runtime-cache").asFile.absolutePath
    val enableExperimentalCompiler = lgpEnableClang23
    val enableWasmtimeRuntime = lgpEnableWasmtime
    val experimentalClang = File(experimentalDirectory, "liblgp_clang23.so")
    val experimentalLld = File(experimentalDirectory, "liblgp_lld23.so")

    // Without these inputs Gradle can wrongly reuse the old APK's Clang
    // when switching -Plgp.clang23=true on/off.
    inputs.property("lgpEnableClang23", lgpEnableClang23)
    inputs.property("lgpEnableWasmtime", lgpEnableWasmtime)
    if (lgpEnableClang23) {
        inputs.files(experimentalClang, experimentalLld)
    }
    outputs.dir(outputDirectory)

    doLast {
        val cacheDirectory = File(cacheDirectoryPath)
        val nativeDirectory = outputDirectory.get().asFile
        cacheDirectory.mkdirs()
        nativeDirectory.mkdirs()

        // A WAMR-only build must not inherit Wasmtime from a prior APK build.
        // The Wasmtime preparation task restores the library when enabled.
        if (!enableWasmtimeRuntime) {
            val staleWasmtime = File(nativeDirectory, "libwasmtime.so")
            check(!staleWasmtime.exists() || staleWasmtime.delete()) {
                "No se pudo retirar Wasmtime del APK WAMR: ${staleWasmtime.absolutePath}"
            }
        }

        fun sha256(file: File): String {
            val digest = MessageDigest.getInstance("SHA-256")
            file.inputStream().buffered().use { input ->
                val buffer = ByteArray(1024 * 1024)
                while (true) {
                    val count = input.read(buffer)
                    if (count < 0) break
                    digest.update(buffer, 0, count)
                }
            }
            return digest.digest().joinToString("") { "%02x".format(it) }
        }

        fun acquire(name: String, url: String, expectedSha256: String): File {
            val destination = File(cacheDirectory, name)
            if (!destination.isFile || sha256(destination) != expectedSha256.lowercase()) {
                destination.delete()
                logger.lifecycle("Descargando dependencia verificada: $name")
                URI(url).toURL().openStream().buffered().use { input ->
                    destination.outputStream().buffered().use(input::copyTo)
                }
            }
            val actual = sha256(destination)
            check(actual == expectedSha256.lowercase()) {
                "SHA-256 inválido para $name. Esperado $expectedSha256, recibido $actual"
            }
            return destination
        }

        fun extractSelected(zipFile: File, prefix: String, names: List<String>) {
            ZipFile(zipFile).use { archive ->
                names.forEach { name ->
                    val entry = archive.getEntry("$prefix$name")
                        ?: error("Falta $prefix$name en ${zipFile.name}")
                    archive.getInputStream(entry).use { input ->
                        File(nativeDirectory, name).outputStream().buffered().use(input::copyTo)
                    }
                }
            }
        }

        // FoldCode libraries are required only by the Clang 21 fallback.
        // Keep this list explicit to remove stale artifacts on variant switches.
        val foldcodeLibraries = listOf(
            "libc++_shared.so",
            "libfoldclang.so",
            "libLLVM.so",
            "libclang-cpp.so",
            "libffi.so",
            "libfoldlldcore.so",
            "libiconv.so",
            "libicudataxxx.so",
            "libicuucxxx.so",
            "libxml2xxx.so",
            "libzstdxx.so",
            "libzzz.so"
        )
        val clang23Output = File(nativeDirectory, "liblgp_clang23.so")
        val lld23Output = File(nativeDirectory, "liblgp_lld23.so")
        if (enableExperimentalCompiler) {
            fun checkAndroidArm64Pie(file: File) {
                check(file.isFile) {
                    "Falta el compilador ARM64 experimental: ${file.absolutePath}"
                }
                val header = ByteArray(20)
                check(file.inputStream().use { it.read(header) } == header.size &&
                    header[0] == 0x7f.toByte() &&
                    header[1] == 'E'.code.toByte() &&
                    header[2] == 'L'.code.toByte() &&
                    header[3] == 'F'.code.toByte() &&
                    header[4] == 2.toByte() && // ELF64
                    header[5] == 1.toByte() && // little endian
                    header[16] == 3.toByte() && // ET_DYN (PIE)
                    header[17] == 0.toByte() &&
                    header[18] == 0xb7.toByte() && // EM_AARCH64
                    header[19] == 0.toByte()
                ) { "No es un ejecutable ELF64 Android ARM64 PIE: ${file.name}" }
            }
            checkAndroidArm64Pie(experimentalClang)
            checkAndroidArm64Pie(experimentalLld)
            experimentalClang.copyTo(clang23Output, overwrite = true)
            experimentalLld.copyTo(lld23Output, overwrite = true)
            logger.lifecycle("Clang 23 predeterminado preparado para Android ARM64.")
            // LLVM 23 and LLD 23 are independent of FoldCode's LLVM 21.
            // Remove stale Clang 21 libraries after switching build variants.
            foldcodeLibraries.forEach { name ->
                val oldLibrary = File(nativeDirectory, name)
                check(!oldLibrary.exists() || oldLibrary.delete()) {
                    "No se pudo retirar la biblioteca de Clang 21: ${oldLibrary.absolutePath}"
                }
            }
        } else {
            // Keep Clang 21 as a fully functional opt-in fallback.
            clang23Output.delete()
            lld23Output.delete()

            val base = acquire(
                "foldcode-base-native-arm64-v1.zip",
                "https://github.com/FifiLeih/FoldCode/releases/download/v1.0.0/foldcode-base-native-arm64-v1.zip",
                "5215289fdab11c43b8d762c788357ee3eb98d1b91e0c46ae53c80528f04970f1"
            )
            val cppExtension = acquire(
                "foldcode-cpp-1.0.0-3.fcex",
                "https://github.com/FifiLeih/FoldCode/releases/download/v1.0.0/foldcode-cpp-1.0.0-3.fcex",
                "82677ce2d912127896a75a113d522455c773f905849e0e56d49c9b71c2930407"
            )

            extractSelected(base, "native/", listOf("libc++_shared.so", "libfoldclang.so"))

            val nestedRuntime = File(cacheDirectory, "cpp-runtime-arm64.zip")
            ZipFile(cppExtension).use { archive ->
                val payload = archive.getEntry("payload/cpp-runtime-arm64.zip")
                    ?: error("La extensión C++ no contiene payload/cpp-runtime-arm64.zip")
                archive.getInputStream(payload).use { input ->
                    nestedRuntime.outputStream().buffered().use(input::copyTo)
                }
            }
            extractSelected(
                nestedRuntime,
                "native/",
                listOf(
                    "libLLVM.so",
                    "libclang-cpp.so",
                    "libffi.so",
                    "libfoldlldcore.so",
                    "libiconv.so",
                    "libicudataxxx.so",
                    "libicuucxxx.so",
                    "libxml2xxx.so",
                    "libzstdxx.so",
                    "libzzz.so"
                )
            )
        }


    }
}

// Default Clang 23 builds package the verified WASIp1 EH sysroot.
// Clang 21 fallback builds omit this overlay and retain the no-EH runtime.
// The SDK download/extraction task is incremental and SHA-256 verified.
val prepareWasiEhRuntime by tasks.registering(Zip::class) {
    group = "build setup"
    description = "Package the verified WASI SDK 34 wasm32-wasip1 EH sysroot for Clang 23."
    dependsOn(prepareWasiSdk34Reference)

    archiveFileName.set("wasi-eh-sysroot.zip")
    destinationDirectory.set(layout.buildDirectory.dir("generated/wasiEhRuntime/assets/runtime"))
    isReproducibleFileOrder = true
    isPreserveFileTimestamps = false

    val officialSysroot = layout.buildDirectory.dir(
        "generated/wasiSdk34/wasi-sdk-34.0-x86_64-linux/share/wasi-sysroot"
    )
    var includedEntries = 0
    from(officialSysroot) {
        include("include/**", "lib/wasm32-wasip1/**", "VERSION")
        exclude(
            "include/wasm32-wasip2/**",
            "include/wasm32-wasip3/**",
            "include/wasm32-wasip1-threads/**",
            "lib/wasm32-wasip1/noeh/**",
            "**/llvm-lto/**"
        )
        eachFile {
            includedEntries++
            if (includedEntries == 1 || includedEntries % 500 == 0) {
                logger.lifecycle("Empaquetando WASI EH: $includedEntries archivos...")
            }
        }
    }
    doLast {
        logger.lifecycle(
            "WASI EH preparado: $includedEntries archivos; " +
                "${(archiveFile.get().asFile.length() / 1048576.0).toInt()} MiB comprimidos."
        )
    }
}

// Official WASI SDK 34 sysroot, extracted from a verified release archive.
// Default Clang 23 builds use the EH overlay; Clang 21 builds do not.
val prepareWasiSdk34Reference by tasks.registering(PrepareWasiSdk34ReferenceTask::class) {
    group = "build setup"
    description = "Verify and extract the official exception-capable WASI SDK 34 sysroot."
    archiveUrl.set(
        "https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-34/" +
            "wasi-sdk-34.0-x86_64-linux.tar.gz"
    )
    expectedSha256.set("b761e3a0721dbae9c09a0059e5fdb2bf917d1b4a8a7b430fb3b5aafb0984b2c4")
    archiveCacheDirectory.set(rootProject.layout.projectDirectory.dir(".gradle/cpp-runtime-cache"))
    extractedSdkDirectory.set(layout.buildDirectory.dir("generated/wasiSdk34"))
}

val prepareWasmtimeRuntime by tasks.registering {
    group = "build setup"
    description = "Download and verify the official Android ARM64 Wasmtime C API."
    // Read Gradle project state while configuring, not from a cached task action.
    val enableWasmtime = lgpEnableWasmtime
    onlyIf { enableWasmtime }

    val cacheDirectoryPath =
        rootProject.layout.projectDirectory.dir(".gradle/cpp-runtime-cache").asFile.absolutePath
    val extractionDirectory = layout.buildDirectory.dir("generated/wasmtime/unpacked")
    val packagedLibraryFile = layout.buildDirectory.file(
        "generated/cppRuntime/jniLibs/arm64-v8a/libwasmtime.so"
    )

    val sdkDir = layout.buildDirectory.dir("generated/wasmtime/sdk")
    outputs.dir(sdkDir)
    // This library is also an output: after a WAMR fallback build removes it,
    // Gradle must rerun this task when Wasmtime is selected again.
    outputs.file(packagedLibraryFile)

    doLast {
        val archiveName = "wasmtime-v49.0.2-aarch64-android-c-api.tar.xz"
        val archiveUrl =
            "https://github.com/bytecodealliance/wasmtime/releases/download/v49.0.2/$archiveName"
        val expectedSha =
            "7d0fcac9a3533df910edfd0651990ba17161827485120d026f25859cf77df045"

        val cacheDir = File(cacheDirectoryPath)
        cacheDir.mkdirs()
        val archive = File(cacheDir, archiveName)

        fun sha256(file: File): String {
            val md = MessageDigest.getInstance("SHA-256")
            file.inputStream().buffered().use { stream ->
                val buffer = ByteArray(1024 * 1024)
                while (true) {
                    val count = stream.read(buffer)
                    if (count < 0) break
                    md.update(buffer, 0, count)
                }
            }
            return md.digest().joinToString("") { "%02x".format(it) }
        }
        if (!archive.isFile || sha256(archive) != expectedSha) {
            archive.delete()
            logger.lifecycle("Descargando Wasmtime ARM64 oficial...")
            URI(archiveUrl).toURL().openStream().buffered().use { src ->
                archive.outputStream().buffered().use(src::copyTo)
            }
        }
        check(sha256(archive) == expectedSha) {
            "El SHA256 de Wasmtime no coincide con la distribución oficial."
        }

        val extraction = extractionDirectory.get().asFile
        extraction.deleteRecursively()
        extraction.mkdirs()
        val extract = ProcessBuilder(
            "tar", "-xf", archive.absolutePath, "-C", extraction.absolutePath
        ).inheritIO().start()
        check(extract.waitFor() == 0) { "Falló la extracción tar.xz de Wasmtime." }

        val library = extraction.walkTopDown().firstOrNull { it.isFile && it.name == "libwasmtime.so" }
            ?: error("El archivo oficial no contiene libwasmtime.so")
        val header = extraction.walkTopDown().firstOrNull { it.isFile && it.name == "wasmtime.h" }
            ?: error("El archivo oficial no contiene wasmtime.h")
        check(File(header.parentFile, "wasi.h").isFile) {
            "No se encontró wasi.h junto a wasmtime.h."
        }

        val target = sdkDir.get().asFile
        target.deleteRecursively()
        File(target, "include").mkdirs()
        header.parentFile.copyRecursively(File(target, "include"), overwrite = true)
        val targetLibrary = File(target, "lib/libwasmtime.so")
        targetLibrary.parentFile.mkdirs()
        library.copyTo(targetLibrary, overwrite = true)

        // Packaged alongside the already existing FoldCode/LLVM libraries.
        val packagedLibrary = packagedLibraryFile.get().asFile
        packagedLibrary.parentFile.mkdirs()
        library.copyTo(packagedLibrary, overwrite = true)
        logger.lifecycle("Wasmtime 49.0.2 ARM64 verificado y preparado.")
    }
}

tasks.configureEach {
    if (lgpEnableCppExceptions && name.startsWith("merge") && name.endsWith("Assets")) {
        dependsOn(prepareWasiEhRuntime)
    }
    if (name.startsWith("merge") && name.endsWith("JniLibFolders")) {
        dependsOn(prepareCppRuntime)
        if (lgpEnableWasmtime) dependsOn(prepareWasmtimeRuntime)
    }
    if (lgpEnableWasmtime && name.startsWith("configureCMake")) {
        dependsOn(prepareWasmtimeRuntime)
    }
}

dependencies {
    // Inference runtime only. Kokoro weights are optional, never packaged in the APK.
    implementation("com.microsoft.onnxruntime:onnxruntime-android:1.30.0")
    implementation("org.matheclipse:matheclipse-core:3.2.0")
    coreLibraryDesugaring("com.android.tools:desugar_jdk_libs:2.1.5")
    implementation(libs.androidx.activity.ktx)
    implementation(libs.androidx.appcompat)
    implementation(libs.androidx.constraintlayout)
    implementation(libs.androidx.core.ktx)
    implementation(libs.material)

    /*
     * Google Play Billing.
     *
     * Se utilizará para Premium mediante
     * una compra única permanente.
     */
    implementation("com.android.billingclient:billing:9.1.0")

    // Native on-demand Clang 21 delivery through Google Play, never GitHub in-app.
    implementation("com.google.android.play:feature-delivery:2.1.0")

    testImplementation(libs.junit)
    // Android's org.json is a stub in local JVM tests; use its standalone implementation.
    testImplementation("org.json:json:20240303")
    androidTestImplementation(libs.androidx.espresso.core)
    androidTestImplementation(libs.androidx.junit)
    implementation("androidx.recyclerview:recyclerview:1.3.2")
}
