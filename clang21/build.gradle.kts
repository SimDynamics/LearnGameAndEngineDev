import java.io.File
import java.net.URI
import java.security.MessageDigest
import java.util.zip.ZipFile
import org.gradle.api.DefaultTask
import org.gradle.api.file.DirectoryProperty
import org.gradle.api.provider.Property
import org.gradle.api.tasks.Input
import org.gradle.api.tasks.Internal
import org.gradle.api.tasks.OutputDirectory
import org.gradle.api.tasks.TaskAction

plugins {
    alias(libs.plugins.android.dynamic.feature)
}

/**
 * Produce only Clang 21's ARM64 native libraries for the Play on-demand split.
 *
 * Reuses the same FoldCode release URLs and SHA-256 digests as the validated
 * `:app` Clang 21 fallback. Downloads happen on the developer's build host,
 * never inside the Android application. This task is configuration-cache safe.
 */
abstract class PrepareClang21FeatureLibrariesTask : DefaultTask() {
    @get:Input abstract val baseSha256: Property<String>
    @get:Input abstract val extensionSha256: Property<String>
    @get:Internal abstract val archiveCacheDirectory: DirectoryProperty
    @get:OutputDirectory abstract val nativeLibraryDirectory: DirectoryProperty

    @TaskAction
    fun prepareLibraries() {
        val cache = archiveCacheDirectory.get().asFile.apply { mkdirs() }
        val native = nativeLibraryDirectory.get().asFile
        val releaseUrl = "https://github.com/FifiLeih/FoldCode/releases/download/v1.0.0/"

        fun sha256(file: File): String {
            val digest = MessageDigest.getInstance("SHA-256")
            file.inputStream().buffered().use { source ->
                val buffer = ByteArray(1024 * 1024)
                while (true) {
                    val count = source.read(buffer)
                    if (count < 0) break
                    digest.update(buffer, 0, count)
                }
            }
            return digest.digest().joinToString("") { "%02x".format(it) }
        }

        fun acquire(name: String, expectedSha256: String): File {
            val archive = File(cache, name)
            if (!archive.isFile || sha256(archive) != expectedSha256) {
                archive.delete()
                logger.lifecycle("Clang 21 opcional: descargando $name...")
                val connection = URI(releaseUrl + name).toURL().openConnection().apply {
                    connectTimeout = 30_000
                    readTimeout = 120_000
                }
                try {
                    connection.getInputStream().buffered().use { source ->
                        archive.outputStream().buffered().use { target ->
                            source.copyTo(target)
                        }
                    }
                } catch (error: Exception) {
                    archive.delete()
                    throw error
                }
            }
            check(sha256(archive) == expectedSha256) {
                "SHA-256 inválido para $name; se detiene el empaquetado."
            }
            logger.lifecycle("Clang 21 opcional: SHA-256 verificado para $name.")
            return archive
        }

        // Verify the archives before modifying the task's output.
        val base = acquire("foldcode-base-native-arm64-v1.zip", baseSha256.get())
        val extension = acquire("foldcode-cpp-1.0.0-3.fcex", extensionSha256.get())

        check(!native.exists() || native.deleteRecursively()) {
            "No se pudo limpiar el directorio generado de Clang 21."
        }
        native.mkdirs()

        fun verifyNativeLibrary(file: File) {
            check(file.isFile && file.length() >= 20L) {
                "Biblioteca ARM64 faltante o vacía: ${file.name}"
            }
            val header = ByteArray(20)
            file.inputStream().use { source ->
                check(source.read(header) == header.size) {
                    "ELF incompleto: ${file.name}"
                }
            }
            check(
                header[0] == 0x7f.toByte() &&
                header[1] == 'E'.code.toByte() &&
                header[2] == 'L'.code.toByte() &&
                header[3] == 'F'.code.toByte() &&
                header[4] == 2.toByte() && // ELF64
                header[5] == 1.toByte() && // little endian
                header[16] == 3.toByte() && // shared object, ET_DYN
                header[17] == 0.toByte() &&
                header[18] == 0xb7.toByte() && // AArch64
                header[19] == 0.toByte()
            ) { "La biblioteca no es un ELF64 AArch64 compartido: ${file.name}" }
        }

        fun extract(archive: ZipFile, prefix: String, names: List<String>) {
            names.forEach { name ->
                val entry = archive.getEntry("$prefix$name")
                    ?: error("Falta $prefix$name en ${archive.name}")
                check(!entry.isDirectory && entry.size > 0L) {
                    "Biblioteca vacía o directorio: $prefix$name"
                }
                val output = File(native, name)
                archive.getInputStream(entry).buffered().use { source ->
                    output.outputStream().buffered().use { destination ->
                        source.copyTo(destination)
                    }
                }
                verifyNativeLibrary(output)
                logger.lifecycle("Clang 21 opcional: preparado $name")
            }
        }

        ZipFile(base).use { archive ->
            extract(archive, "native/", listOf(
                "libc++_shared.so",
                "libfoldclang.so"
            ))
        }

        val nestedArchive = File.createTempFile("clang21-native-", ".zip", cache)
        try {
            ZipFile(extension).use { archive ->
                val entry = archive.getEntry("payload/cpp-runtime-arm64.zip")
                    ?: error("FoldCode no contiene payload/cpp-runtime-arm64.zip")
                archive.getInputStream(entry).buffered().use { source ->
                    nestedArchive.outputStream().buffered().use { destination ->
                        source.copyTo(destination)
                    }
                }
            }
            ZipFile(nestedArchive).use { archive ->
                extract(archive, "native/", listOf(
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
                ))
            }
        } finally {
            nestedArchive.delete()
        }
        val files = native.listFiles()?.filter { it.isFile }.orEmpty()
        check(files.size == 12) {
            "Se esperaban 12 bibliotecas Clang 21, se encontraron ${files.size}."
        }
        logger.lifecycle("Clang 21 opcional: 12 bibliotecas ARM64 listas para el split.")
    }
}

android {
    namespace = "com.learngraphicsprogramming.app.clang21"
    compileSdk {
        version = release(37)
    }
    defaultConfig {
        minSdk = 24
        ndk {
            abiFilters += "arm64-v8a"
        }
    }
    sourceSets {
        getByName("main").jniLibs.directories.add(
            "build/generated/clang21Feature/jniLibs"
        )
    }
    packaging {
        jniLibs.useLegacyPackaging = true
    }
}

val prepareClang21FeatureLibraries by tasks.registering(
    PrepareClang21FeatureLibrariesTask::class
) {
    group = "build setup"
    description = "Prepara las 12 bibliotecas FoldCode Clang 21 para el módulo bajo demanda."
    baseSha256.set("5215289fdab11c43b8d762c788357ee3eb98d1b91e0c46ae53c80528f04970f1")
    extensionSha256.set("82677ce2d912127896a75a113d522455c773f905849e0e56d49c9b71c2930407")
    archiveCacheDirectory.set(rootProject.layout.projectDirectory.dir(".gradle/cpp-runtime-cache"))
    nativeLibraryDirectory.set(layout.buildDirectory.dir("generated/clang21Feature/jniLibs/arm64-v8a"))
}

tasks.configureEach {
    if (name.startsWith("merge") && name.endsWith("JniLibFolders")) {
        dependsOn(prepareClang21FeatureLibraries)
    }
}

dependencies {
    // Dynamic features depend on the base; the base never depends on feature code.
    implementation(project(":app"))
}
