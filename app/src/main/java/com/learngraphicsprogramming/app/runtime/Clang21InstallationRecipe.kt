package com.learngraphicsprogramming.app.runtime

import org.json.JSONObject
import java.io.File
import java.io.InputStream
import java.io.OutputStream
import java.io.RandomAccessFile
import java.net.URI
import java.security.MessageDigest
import java.util.concurrent.CancellationException
import java.util.zip.ZipFile

/** A signed APK recipe contains metadata and signatures, never a signing key. */
internal data class Clang21InstallationRecipe(
    val packageName: String,
    val versionCode: Long,
    val certificateSha256: String,
    val splitName: String,
    val apkBytes: Long,
    val apkSha256: String,
    val envelopeFile: String,
    val envelopeBytes: Long,
    val envelopeSha256: String,
    val archives: List<Archive>,
    val libraries: List<Library>,
    val segments: List<Segment>
) {
    data class Archive(val name: String, val url: String, val bytes: Long, val sha256: String)
    data class Library(
        val name: String, val bytes: Long, val sha256: String, val archive: String,
        val entry: String, val nestedEntry: String?
    )
    data class Segment(val kind: String, val offset: Long, val bytes: Long, val name: String?)

    companion object {
        val libraryNames = setOf(
            "libc++_shared.so", "libfoldclang.so", "libLLVM.so", "libclang-cpp.so",
            "libffi.so", "libfoldlldcore.so", "libiconv.so", "libicudataxxx.so",
            "libicuucxxx.so", "libxml2xxx.so", "libzstdxx.so", "libzzz.so"
        )
        private val hashPattern = Regex("[a-f0-9]{64}")
        private val filePattern = Regex("[A-Za-z0-9_+.-]+")
        private fun hash(value: String): String = value.also {
            require(hashPattern.matches(it)) { "Invalid SHA-256 in compiler installation recipe" }
        }
        private fun positive(value: Long): Long = value.also {
            require(it in 1..1_073_741_824L) { "Invalid compiler resource size" }
        }

        fun parse(text: String): Clang21InstallationRecipe {
            val json = JSONObject(text)
            require(json.getInt("format") == 1) { "Unsupported compiler installation recipe" }
            val apk = json.getJSONObject("apk")
            val envelope = json.getJSONObject("envelope")
            val archiveJson = json.getJSONArray("archives")
            require(archiveJson.length() == 2) { "The compiler requires two official archives" }
            val archives = (0 until archiveJson.length()).map { index ->
                val source = archiveJson.getJSONObject(index)
                val name = source.getString("name")
                require(filePattern.matches(name) && name != "." && name != "..")
                val url = source.getString("url")
                val uri = URI(url)
                require(uri.scheme == "https" && uri.host == "github.com" && uri.userInfo == null &&
                    uri.port == -1 && uri.query == null && uri.fragment == null &&
                    uri.path.startsWith("/FifiLeih/FoldCode/releases/download/v1.0.0/")) {
                    "The compiler archive is not an official HTTPS FoldCode release"
                }
                Archive(name, url, positive(source.getLong("bytes")), hash(source.getString("sha256")))
            }
            require(archives.map { it.name }.toSet().size == archives.size)
            require(archives.map { it.url }.toSet().size == archives.size)
            val libraryJson = json.getJSONArray("libraries")
            val libraries = (0 until libraryJson.length()).map { index ->
                val source = libraryJson.getJSONObject(index)
                val name = source.getString("name")
                require(name in libraryNames) { "Unexpected compiler library" }
                val archive = source.getString("archive")
                require(archives.any { it.name == archive }) { "Unknown compiler archive" }
                val entry = source.getString("entry")
                require(entry == "native/$name") { "Unsafe compiler archive path" }
                val nested = if (source.isNull("nestedEntry")) null else source.getString("nestedEntry")
                require(nested == null || nested == "payload/cpp-runtime-arm64.zip") {
                    "Unsafe nested compiler archive path"
                }
                Library(name, positive(source.getLong("bytes")), hash(source.getString("sha256")), archive, entry, nested)
            }
            require(libraries.size == libraryNames.size && libraries.map { it.name }.toSet() == libraryNames) {
                "The compiler installation must contain all twelve libraries"
            }
            val envelopeSize = positive(envelope.getLong("bytes"))
            require(envelopeSize <= 4 * 1024 * 1024) { "Compiler envelope unexpectedly contains payload" }
            val envelopePath = envelope.getString("file")
            require(envelopePath == "runtime/clang21_installation/envelope.bin")
            val segmentJson = json.getJSONArray("segments")
            var expectedEnvelopeOffset = 0L
            var rebuiltSize = 0L
            val seenLibraries = mutableSetOf<String>()
            val segments = (0 until segmentJson.length()).map { index ->
                val source = segmentJson.getJSONObject(index)
                val size = positive(source.getLong("bytes"))
                rebuiltSize = Math.addExact(rebuiltSize, size)
                when (val kind = source.getString("kind")) {
                    "envelope" -> {
                        val offset = source.getLong("offset")
                        require(offset == expectedEnvelopeOffset) { "Compiler envelope overlaps or has gaps" }
                        expectedEnvelopeOffset = Math.addExact(expectedEnvelopeOffset, size)
                        require(expectedEnvelopeOffset <= envelopeSize)
                        Segment(kind, offset, size, null)
                    }
                    "library" -> {
                        val name = source.getString("name")
                        require(seenLibraries.add(name) && libraries.any { it.name == name && it.bytes == size })
                        Segment(kind, 0, size, name)
                    }
                    else -> error("Unknown compiler APK segment")
                }
            }
            val apkSize = positive(apk.getLong("bytes"))
            require(rebuiltSize == apkSize && expectedEnvelopeOffset == envelopeSize && seenLibraries == libraryNames) {
                "The compiler APK recipe is incomplete"
            }
            val packageName = json.getString("packageName")
            require(Regex("[A-Za-z][A-Za-z0-9_]*(\\.[A-Za-z][A-Za-z0-9_]*)+").matches(packageName))
            val version = json.getLong("versionCode")
            require(version > 0)
            require(json.getString("splitName") == "clang21")
            return Clang21InstallationRecipe(packageName, version, hash(json.getString("certificateSha256")),
                "clang21", apkSize, hash(apk.getString("sha256")), envelopePath, envelopeSize,
                hash(envelope.getString("sha256")), archives, libraries, segments)
        }
    }
}

/** Every operation here reads data only; executable installation belongs to Android. */
internal object Clang21InstallationFiles {
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

    fun valid(file: File, bytes: Long, hash: String): Boolean =
        file.isFile && file.length() == bytes && sha256(file) == hash

    private fun checkCancellation(cancelled: () -> Boolean) {
        if (cancelled()) throw CancellationException("Compiler installation canceled")
    }

    private fun copyExactly(input: InputStream, output: OutputStream, size: Long, cancelled: () -> Boolean) {
        val buffer = ByteArray(1024 * 1024)
        var remaining = size
        while (remaining > 0) {
            checkCancellation(cancelled)
            val count = input.read(buffer, 0, minOf(remaining, buffer.size.toLong()).toInt())
            check(count > 0) { "Truncated compiler resource" }
            output.write(buffer, 0, count)
            remaining -= count
        }
    }

    fun extractLibraries(
        recipe: Clang21InstallationRecipe, archives: Map<String, File>, directory: File,
        cancelled: () -> Boolean = { false }
    ) {
        directory.mkdirs()
        recipe.archives.forEach { archive ->
            check(valid(archives.getValue(archive.name), archive.bytes, archive.sha256)) {
                "Compiler archive SHA-256 mismatch: ${archive.name}"
            }
        }
        recipe.libraries.groupBy { it.archive to it.nestedEntry }.forEach { (key, libraries) ->
            checkCancellation(cancelled)
            val source = archives.getValue(key.first)
            val nested = key.second?.let { path ->
                File(directory, "runtime-verified.zip").also { output ->
                    ZipFile(source).use { zip ->
                        val entry = zip.getEntry(path) ?: error("Missing compiler runtime archive")
                        require(!entry.isDirectory && entry.size in 1..1_073_741_824L)
                        zip.getInputStream(entry).use { input ->
                            output.outputStream().buffered().use { target ->
                                copyExactly(input, target, entry.size, cancelled)
                                check(input.read() == -1) { "Oversized compiler runtime archive" }
                            }
                        }
                    }
                }
            }
            try {
                ZipFile(nested ?: source).use { zip ->
                    libraries.forEach { library ->
                        checkCancellation(cancelled)
                        val destination = File(directory, library.name)
                        if (!valid(destination, library.bytes, library.sha256)) {
                            val entry = zip.getEntry(library.entry) ?: error("Missing compiler library: ${library.name}")
                            require(!entry.isDirectory && entry.size == library.bytes)
                            val temporary = File(directory, "${library.name}.part")
                            try {
                                zip.getInputStream(entry).use { input ->
                                    temporary.outputStream().buffered().use { output ->
                                        copyExactly(input, output, library.bytes, cancelled)
                                        check(input.read() == -1) { "Oversized compiler library" }
                                    }
                                }
                                check(valid(temporary, library.bytes, library.sha256)) { "Compiler library SHA-256 mismatch: ${library.name}" }
                                check(!destination.exists() || destination.delete())
                                check(temporary.renameTo(destination)) { "Cannot save verified compiler library" }
                            } finally { temporary.delete() }
                        }
                    }
                }
            } finally { nested?.delete() }
        }
    }

    fun reconstruct(
        recipe: Clang21InstallationRecipe, envelope: File, libraries: File, output: File,
        cancelled: () -> Boolean = { false }
    ) {
        if (valid(output, recipe.apkBytes, recipe.apkSha256)) return
        check(valid(envelope, recipe.envelopeBytes, recipe.envelopeSha256)) { "Compiler envelope SHA-256 mismatch" }
        recipe.libraries.forEach { library ->
            checkCancellation(cancelled)
            check(valid(File(libraries, library.name), library.bytes, library.sha256)) {
                "Compiler library SHA-256 mismatch: ${library.name}"
            }
        }
        output.parentFile?.mkdirs()
        val temporary = File(output.parentFile, "${output.name}.part")
        try {
            temporary.outputStream().buffered().use { target ->
                RandomAccessFile(envelope, "r").use { source ->
                    recipe.segments.forEach { segment ->
                        checkCancellation(cancelled)
                        if (segment.kind == "envelope") {
                            source.seek(segment.offset)
                            var left = segment.bytes
                            val buffer = ByteArray(64 * 1024)
                            while (left > 0) {
                                checkCancellation(cancelled)
                                val count = source.read(buffer, 0, minOf(left, buffer.size.toLong()).toInt())
                                check(count > 0) { "Truncated compiler envelope" }
                                target.write(buffer, 0, count)
                                left -= count
                            }
                        } else {
                            File(libraries, segment.name!!).inputStream().buffered().use { input ->
                                copyExactly(input, target, segment.bytes, cancelled)
                            }
                        }
                    }
                }
            }
            check(valid(temporary, recipe.apkBytes, recipe.apkSha256)) { "Reconstructed compiler APK SHA-256 mismatch" }
            checkCancellation(cancelled)
            check(!output.exists() || output.delete())
            check(temporary.renameTo(output)) { "Cannot save verified compiler APK" }
        } finally { temporary.delete() }
    }
}
