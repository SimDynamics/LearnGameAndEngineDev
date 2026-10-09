import java.io.File
import java.net.URI
import java.security.MessageDigest
import java.util.zip.ZipFile

plugins {
    alias(libs.plugins.android.application)
}

// Controlled migration: -Plgp.wasmtime=true activates the verified Android
// Wasmtime C API. Without this property, existing WAMR/graphics remain intact.
val lgpEnableWasmtime =
    providers.gradleProperty("lgp.wasmtime").orNull.equals("true", ignoreCase = true)

// Experimental Android-native Clang 23.1.3 may only be selected after
// an independently built ARM64 bundle is explicitly imported and verified.
val lgpEnableClang23 =
    providers.gradleProperty("lgp.clang23").orNull.equals("true", ignoreCase = true)

// Crash-stage files and verbose JNI logs are opt-in, never enabled by default.
val lgpWasmtimeDiagnostics =
    lgpEnableWasmtime &&
        providers.gradleProperty("lgp.wasmtimeDiagnostics").orNull.equals("true", ignoreCase = true)

android {
    namespace = "com.learngraphicsprogramming.app"
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
    val experimentalClang = File(experimentalDirectory, "liblgp_clang23.so")
    val experimentalLld = File(experimentalDirectory, "liblgp_lld23.so")

    // Without these inputs Gradle can wrongly reuse the old APK's Clang
    // when switching -Plgp.clang23=true on/off.
    inputs.property("lgpEnableClang23", lgpEnableClang23)
    if (lgpEnableClang23) {
        inputs.files(experimentalClang, experimentalLld)
    }
    outputs.dir(outputDirectory)

    doLast {
        val cacheDirectory = File(cacheDirectoryPath)
        val nativeDirectory = outputDirectory.get().asFile
        cacheDirectory.mkdirs()
        nativeDirectory.mkdirs()

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
            logger.lifecycle("Clang 23 experimental incluido; verificar --version en Android.")
            // LLVM 23 and LLD 23 are independent of FoldCode's LLVM 21.
            // Remove stale Clang 21 libraries after switching build variants.
            foldcodeLibraries.forEach { name ->
                val oldLibrary = File(nativeDirectory, name)
                check(!oldLibrary.exists() || oldLibrary.delete()) {
                    "No se pudo retirar la biblioteca de Clang 21: ${oldLibrary.absolutePath}"
                }
            }
        } else {
            // Keep Clang 21 as the default, fully functional fallback.
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

// Official Wasmtime 49.0.2 aarch64-android-c-api release, hash from GitHub.
// Extracted headers are used by NDK CMake; shared library is packaged in APK.
// Reference sysroot from the official WASI SDK 34 (LLVM 23 generation).
// Deliberately NOT merged into the Android APK while its Clang host binary is 21:
// C++ exceptions require LLVM 22+ AND consistent exception-enabled libraries.
val prepareWasiSdk34Reference by tasks.registering {
    group = "build setup"
    description = "Verify and extract the official exception-capable WASI SDK 34 sysroot."
    notCompatibleWithConfigurationCache("Downloads a hashed 183 MiB tar.gz reference archive.")

    val sdkDirectory = layout.buildDirectory.dir("generated/wasiSdk34")
    outputs.dir(sdkDirectory)

    doLast {
        val name = "wasi-sdk-34.0-x86_64-linux.tar.gz"
        val url =
            "https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-34/$name"
        val expected =
            "b761e3a0721dbae9c09a0059e5fdb2bf917d1b4a8a7b430fb3b5aafb0984b2c4"

        val cache = rootProject.layout.projectDirectory.dir(".gradle/cpp-runtime-cache").asFile
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

        val root = sdkDirectory.get().asFile
        root.deleteRecursively()
        root.mkdirs()
        val process = ProcessBuilder(
            "tar", "-xf", archive.absolutePath, "-C", root.absolutePath
        ).inheritIO().start()
        check(process.waitFor() == 0) { "No se pudo extraer la distribución oficial de WASI." }

        val sysroot = root.walkTopDown().firstOrNull {
            it.isDirectory && it.name == "wasi-sysroot"
        } ?: error("WASI SDK 34 oficial: no se encontró share/wasi-sysroot.")

        val unwind = sysroot.walkTopDown().firstOrNull {
            it.isFile && it.name == "libunwind.a"
        } ?: error("WASI SDK 34: falta libunwind.a (EH).")
        val abi = sysroot.walkTopDown().firstOrNull {
            it.isFile && it.name == "libc++abi.a"
        } ?: error("WASI SDK 34: falta libc++abi.a.")

        logger.lifecycle("WASI SDK 34 verificado. Sysroot: ${sysroot.absolutePath}")
        logger.lifecycle("Bibliotecas EH: ${unwind.absolutePath}, ${abi.absolutePath}")
        logger.lifecycle("Este SDK no se activará con FoldCode Clang 21: hace falta Clang 23 ARM64.")
    }
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

    testImplementation(libs.junit)
    // Android's org.json is a stub in local JVM tests; use its standalone implementation.
    testImplementation("org.json:json:20240303")
    androidTestImplementation(libs.androidx.espresso.core)
    androidTestImplementation(libs.androidx.junit)
    implementation("androidx.recyclerview:recyclerview:1.3.2")
}
