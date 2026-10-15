package com.learngraphicsprogramming.app.runtime

import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.ByteArrayOutputStream
import java.io.File
import java.security.MessageDigest
import java.util.concurrent.CancellationException
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

class Clang21InstallationRecipeTest {
    @get:Rule val temporary = TemporaryFolder()
    private fun digest(bytes: ByteArray) = MessageDigest.getInstance("SHA-256").digest(bytes)
        .joinToString("") { "%02x".format(it) }
    private fun zip(entries: Map<String, ByteArray>): ByteArray = ByteArrayOutputStream().also { bytes ->
        ZipOutputStream(bytes).use { output ->
            entries.forEach { (name, value) ->
                output.putNextEntry(ZipEntry(name)); output.write(value); output.closeEntry()
            }
        }
    }.toByteArray()

    private inner class Fixture {
        val names = Clang21InstallationRecipe.libraryNames.toList()
        val payloads = names.mapIndexed { index, name -> name to ByteArray(32 + index) { (index + 1).toByte() } }.toMap()
        val envelope = "manifest--signature".toByteArray()
        val expectedApk = envelope.copyOfRange(0, 10) + names.flatMap { payloads.getValue(it).toList() }.toByteArray() +
            envelope.copyOfRange(10, envelope.size)
        val base = zip(names.take(2).associate { "native/$it" to payloads.getValue(it) })
        val nested = zip(names.drop(2).associate { "native/$it" to payloads.getValue(it) })
        val extensionBytes = zip(mapOf("payload/cpp-runtime-arm64.zip" to nested))
        val root = temporary.newFolder()
        val archives = mapOf("base.zip" to File(root, "base.zip").apply { writeBytes(base) },
            "extension.fcex" to File(root, "extension.fcex").apply { writeBytes(extensionBytes) })
        val envelopeFile = File(root, "envelope.bin").apply { writeBytes(envelope) }
        val libraries = File(root, "libraries")
        val apk = File(root, "clang21.apk")
        val json = JSONObject().put("format", 1).put("packageName", "com.example.learning")
            .put("versionCode", 7).put("certificateSha256", "a".repeat(64)).put("splitName", "clang21")
            .put("apk", JSONObject().put("bytes", expectedApk.size).put("sha256", digest(expectedApk)))
            .put("envelope", JSONObject().put("file", "runtime/clang21_installation/envelope.bin")
                .put("bytes", envelope.size).put("sha256", digest(envelope)))
            .put("archives", JSONArray().put(JSONObject().put("name", "base.zip")
                .put("url", "https://github.com/FifiLeih/FoldCode/releases/download/v1.0.0/foldcode-base-native-arm64-v1.zip")
                .put("bytes", base.size).put("sha256", digest(base)))
                .put(JSONObject().put("name", "extension.fcex")
                    .put("url", "https://github.com/FifiLeih/FoldCode/releases/download/v1.0.0/foldcode-cpp-1.0.0-3.fcex")
                    .put("bytes", extensionBytes.size).put("sha256", digest(extensionBytes))))
            .put("libraries", JSONArray().apply {
                names.forEachIndexed { index, name ->
                    put(JSONObject().put("name", name).put("bytes", payloads.getValue(name).size)
                        .put("sha256", digest(payloads.getValue(name)))
                        .put("archive", if (index < 2) "base.zip" else "extension.fcex")
                        .put("entry", "native/$name")
                        .put("nestedEntry", if (index < 2) JSONObject.NULL else "payload/cpp-runtime-arm64.zip"))
                }
            }).put("segments", JSONArray().apply {
                put(JSONObject().put("kind", "envelope").put("offset", 0).put("bytes", 10))
                names.forEach { name -> put(JSONObject().put("kind", "library").put("name", name)
                    .put("bytes", payloads.getValue(name).size)) }
                put(JSONObject().put("kind", "envelope").put("offset", 10).put("bytes", envelope.size - 10))
            })
        fun recipe() = Clang21InstallationRecipe.parse(json.toString())
    }

    private fun rejects(action: () -> Unit) {
        try { action(); fail("Invalid resource must be rejected") } catch (_: IllegalArgumentException) { }
    }

    @Test fun validatesCompleteManifestAndOfficialHttpsSources() {
        val fixture = Fixture()
        val recipe = fixture.recipe()
        assertEquals(12, recipe.libraries.size)
        assertEquals(2, recipe.archives.size)
        assertEquals(fixture.expectedApk.size.toLong(), recipe.apkBytes)
        for (url in listOf("http://github.com/FifiLeih/FoldCode/releases/download/v1.0.0/a.zip",
            "https://evil.example/FifiLeih/FoldCode/releases/download/v1.0.0/a.zip",
            "https://github.com/Someone/Else/releases/download/v1.0.0/a.zip")) {
            fixture.json.getJSONArray("archives").getJSONObject(0).put("url", url)
            rejects { fixture.recipe() }
        }
    }

    @Test fun refusesMissingLibrariesAndUnsafePaths() {
        Fixture().let { fixture ->
            fixture.json.getJSONArray("libraries").remove(0)
            rejects { fixture.recipe() }
        }
        Fixture().let { fixture ->
            fixture.json.getJSONArray("libraries").getJSONObject(0).put("entry", "../libfoldclang.so")
            rejects { fixture.recipe() }
        }
        Fixture().let { fixture ->
            fixture.json.getJSONArray("libraries").getJSONObject(3).put("nestedEntry", "../../runtime.zip")
            rejects { fixture.recipe() }
        }
    }

    @Test fun refusesOverlapsDuplicatePayloadsAndMismatchedSizes() {
        Fixture().let { fixture ->
            fixture.json.getJSONArray("segments").getJSONObject(13).put("offset", 9)
            rejects { fixture.recipe() }
        }
        Fixture().let { fixture ->
            fixture.json.getJSONArray("segments").getJSONObject(2).put("name", fixture.names.first())
            rejects { fixture.recipe() }
        }
        Fixture().let { fixture ->
            fixture.json.getJSONObject("apk").put("bytes", 1)
            rejects { fixture.recipe() }
        }
    }

    @Test fun extractsOriginalBytesAndReconstructsExactSignedEnvelope() {
        val fixture = Fixture()
        val recipe = fixture.recipe()
        Clang21InstallationFiles.extractLibraries(recipe, fixture.archives, fixture.libraries)
        fixture.payloads.forEach { (name, bytes) -> assertArrayEquals(bytes, File(fixture.libraries, name).readBytes()) }
        Clang21InstallationFiles.reconstruct(recipe, fixture.envelopeFile, fixture.libraries, fixture.apk)
        assertArrayEquals(fixture.expectedApk, fixture.apk.readBytes())
        assertEquals(recipe.apkSha256, Clang21InstallationFiles.sha256(fixture.apk))
    }

    @Test fun reusesVerifiedApkWithoutRewritingIt() {
        val fixture = Fixture()
        fixture.apk.writeBytes(fixture.expectedApk)
        val timestamp = fixture.apk.lastModified()
        // The verified result needs neither extraction nor a new envelope read.
        fixture.envelopeFile.delete()
        Clang21InstallationFiles.reconstruct(fixture.recipe(), fixture.envelopeFile, fixture.libraries, fixture.apk)
        assertEquals(timestamp, fixture.apk.lastModified())
        assertArrayEquals(fixture.expectedApk, fixture.apk.readBytes())
    }

    @Test fun rejectsCorruptArchiveBeforeAnyLibraryIsInstalled() {
        val fixture = Fixture()
        fixture.archives.getValue("extension.fcex").appendBytes(byteArrayOf(7))
        try {
            Clang21InstallationFiles.extractLibraries(fixture.recipe(), fixture.archives, fixture.libraries)
            fail("Corrupt archive must fail integrity verification")
        } catch (_: IllegalStateException) { }
        assertTrue(fixture.libraries.listFiles().orEmpty().isEmpty())
    }

    @Test fun rejectsIncorrectLibraryHashAndNeverProducesAnApk() {
        val fixture = Fixture()
        fixture.json.getJSONArray("libraries").getJSONObject(0).put("sha256", "0".repeat(64))
        try {
            Clang21InstallationFiles.extractLibraries(fixture.recipe(), fixture.archives, fixture.libraries)
            fail("Corrupt library must fail integrity verification")
        } catch (_: IllegalStateException) { }
        assertFalse(File(fixture.libraries, fixture.names.first()).exists())
        assertFalse(fixture.apk.exists())
    }

    @Test fun rejectsTamperedEnvelopeAndDoesNotLeavePartialApk() {
        val fixture = Fixture()
        Clang21InstallationFiles.extractLibraries(fixture.recipe(), fixture.archives, fixture.libraries)
        fixture.envelopeFile.appendBytes(byteArrayOf(3))
        try {
            Clang21InstallationFiles.reconstruct(fixture.recipe(), fixture.envelopeFile, fixture.libraries, fixture.apk)
            fail("Tampered envelope must be rejected")
        } catch (_: IllegalStateException) { }
        assertFalse(fixture.apk.exists())
        assertFalse(File(fixture.root, "clang21.apk.part").exists())
    }

    @Test fun cancellationNeverExposesAnUnverifiedApk() {
        val fixture = Fixture()
        Clang21InstallationFiles.extractLibraries(fixture.recipe(), fixture.archives, fixture.libraries)
        try {
            Clang21InstallationFiles.reconstruct(fixture.recipe(), fixture.envelopeFile, fixture.libraries, fixture.apk) { true }
            fail("Cancellation must stop reconstruction")
        } catch (_: CancellationException) { }
        assertFalse(fixture.apk.exists())
        assertFalse(File(fixture.root, "clang21.apk.part").exists())
    }

    @Test fun finalApkHashPreventsAcceptingAnIncorrectRecipe() {
        val fixture = Fixture()
        fixture.json.getJSONObject("apk").put("sha256", "0".repeat(64))
        val recipe = fixture.recipe()
        Clang21InstallationFiles.extractLibraries(recipe, fixture.archives, fixture.libraries)
        try {
            Clang21InstallationFiles.reconstruct(recipe, fixture.envelopeFile, fixture.libraries, fixture.apk)
            fail("A complete but incorrectly signed-envelope recipe must be rejected")
        } catch (_: IllegalStateException) { }
        assertFalse(fixture.apk.exists())
        assertFalse(File(fixture.root, "clang21.apk.part").exists())
    }
}
