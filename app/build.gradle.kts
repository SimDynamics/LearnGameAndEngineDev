import java.io.File
import java.net.URI
import java.security.MessageDigest
import java.util.zip.ZipFile

plugins {
    alias(libs.plugins.android.application)
}

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
            }
        }
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
        getByName("main").jniLibs.srcDir("build/generated/cppRuntime/jniLibs")
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
    notCompatibleWithConfigurationCache("Descarga y extrae archivos ZIP verificados.")

    val outputDirectory = layout.buildDirectory.dir("generated/cppRuntime/jniLibs/arm64-v8a")
    outputs.dir(outputDirectory)

    doLast {
        val cacheDirectory = rootProject.layout.projectDirectory.dir(".gradle/cpp-runtime-cache").asFile
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

tasks.configureEach {
    if (name.startsWith("merge") && name.endsWith("JniLibFolders")) {
        dependsOn(prepareCppRuntime)
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
