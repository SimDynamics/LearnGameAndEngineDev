package com.learngraphicsprogramming.app.narration

import java.io.File
import java.io.IOException
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Assume.assumeNoException
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class KokoroManagedFilesTest {
    @get:Rule val temporary = TemporaryFolder()
    private val current = "a".repeat(40)
    private val previous = "b".repeat(40)

    @Test fun replacementDeletesOnlyDeclaredOldFilesAndKeepsCurrentAndUnrelatedFiles() {
        val root = temporary.newFolder("kokoro")
        val currentFile = modelFile(root, current, "model.onnx", "new verified model")
        val oldFile = modelFile(root, previous, "model.onnx", "old verified model")
        val partial = modelFile(root, previous, "model.onnx.part", "incomplete transfer")
        val unrelated = modelFile(root, previous, "personal-note.txt", "keep me")

        KokoroManagedFiles.removeObsolete(root, current, setOf(current, previous), setOf("model.onnx"))

        assertFalse(oldFile.exists())
        assertFalse(partial.exists())
        assertEquals("new verified model", currentFile.readText())
        assertEquals("keep me", unrelated.readText())
    }

    @Test fun obsoleteDirectoryIsRemovedOnlyAfterItBecomesEmpty() {
        val root = temporary.newFolder("kokoro")
        modelFile(root, previous, "model.onnx", "old model")

        KokoroManagedFiles.removeObsolete(root, current, setOf(previous), setOf("model.onnx"))

        assertFalse(File(root, previous).exists())
        assertTrue(root.isDirectory)
    }

    @Test fun invalidRevisionRejectsTheWholeDeletionPlanBeforeTouchingFiles() {
        val root = temporary.newFolder("kokoro")
        val oldFile = modelFile(root, previous, "model.onnx", "preserve before validation")
        try {
            KokoroManagedFiles.removeObsolete(root, current, linkedSetOf(previous, "../outside"), setOf("model.onnx"))
            fail("Unsafe revision should be rejected")
        } catch (_: IllegalArgumentException) {
            assertTrue(oldFile.exists())
        }
    }

    @Test fun namesCannotEscapeTheirRevisionDirectory() {
        val root = temporary.newFolder("kokoro")
        val outside = temporary.newFile("outside.onnx").apply { writeText("unrelated") }
        try {
            KokoroManagedFiles.removeObsolete(root, current, setOf(previous), setOf("../../outside.onnx"))
            fail("Unsafe artifact should be rejected")
        } catch (_: IllegalArgumentException) {
            assertEquals("unrelated", outside.readText())
        }
    }

    @Test fun symbolicLinkOutsidePrivateRootIsRejectedBeforeAnyDeletion() {
        val root = temporary.newFolder("kokoro")
        val oldFile = modelFile(root, previous, "model.onnx", "preserve before validation")
        val outside = temporary.newFolder("outside")
        val outsideFile = File(outside, "model.onnx").apply { writeText("unrelated") }
        val linkedRevision = "c".repeat(40)
        val link = File(root, linkedRevision).toPath()
        try {
            Files.createSymbolicLink(link, outside.toPath())
        } catch (error: IOException) {
            assumeNoException("Host does not permit symbolic links", error)
        } catch (error: UnsupportedOperationException) {
            assumeNoException("Host does not support symbolic links", error)
        }
        try {
            KokoroManagedFiles.removeObsolete(root, current, linkedSetOf(previous, linkedRevision), setOf("model.onnx"))
            fail("Linked revision outside private root should be rejected")
        } catch (_: IOException) {
            assertTrue(oldFile.exists())
            assertEquals("unrelated", outsideFile.readText())
        } finally {
            Files.deleteIfExists(link)
        }
    }

    @Test fun obsoleteRevisionCannotAliasTheCurrentRevisionWithinThePrivateRoot() {
        val root = temporary.newFolder("kokoro")
        val currentFile = modelFile(root, current, "model.onnx", "new verified model")
        val link = File(root, previous).toPath()
        try {
            Files.createSymbolicLink(link, File(root, current).toPath())
        } catch (error: IOException) {
            assumeNoException("Host does not permit symbolic links", error)
        } catch (error: UnsupportedOperationException) {
            assumeNoException("Host does not support symbolic links", error)
        }
        try {
            KokoroManagedFiles.removeObsolete(root, current, setOf(previous), setOf("model.onnx"))
            fail("Obsolete revision must not resolve to the current revision")
        } catch (_: IOException) {
            assertEquals("new verified model", currentFile.readText())
        } finally {
            Files.deleteIfExists(link)
        }
    }

    private fun modelFile(root: File, revision: String, name: String, content: String): File {
        val directory = File(root, revision)
        directory.mkdirs()
        return File(directory, name).apply { writeText(content) }
    }
}
