package com.learngraphicsprogramming.app.narration

import java.io.File
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class KokoroModelIntegrityTest {
    private val artifact = KokoroModelManifest.Artifact(
        "fixture.bin", "fixture.bin", 3L,
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
    )

    @Test fun acceptsOnlyExactBytesAndDigest() = withFixture("abc") { file ->
        assertTrue(KokoroModelManifest.verify(file, artifact))
    }

    @Test fun rejectsCorruptionWithSameLength() = withFixture("abd") { file ->
        assertFalse(KokoroModelManifest.verify(file, artifact))
    }

    @Test fun rejectsIncompleteFile() = withFixture("ab") { file ->
        assertFalse(KokoroModelManifest.verify(file, artifact))
    }

    @Test fun rejectsMissingFile() {
        val file = File.createTempFile("kokoro-integrity", ".bin")
        file.delete()
        assertFalse(KokoroModelManifest.verify(file, artifact))
    }

    private fun withFixture(content: String, test: (File) -> Unit) {
        val file = File.createTempFile("kokoro-integrity", ".bin")
        try {
            file.writeText(content, Charsets.UTF_8)
            test(file)
        } finally {
            file.delete()
        }
    }
}
