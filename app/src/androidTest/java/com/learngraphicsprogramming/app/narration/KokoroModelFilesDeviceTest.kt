package com.learngraphicsprogramming.app.narration

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import java.io.IOException
import java.nio.file.Files
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

/** Runs the link boundary cases on Android where creating symlinks is supported. */
@RunWith(AndroidJUnit4::class)
class KokoroModelFilesDeviceTest {
    @Test fun linkedRevisionCannotDeleteFilesOutsideTheModelDirectory() = checkBoundary(linkToCurrent = false)

    @Test fun linkedOldRevisionCannotDeleteTheCurrentModel() = checkBoundary(linkToCurrent = true)

    private fun checkBoundary(linkToCurrent: Boolean) {
        val cache = InstrumentationRegistry.getInstrumentation().targetContext.cacheDir.canonicalFile
        val base = File(cache, "kokoro-boundary-test-${System.nanoTime()}")
        require(base.canonicalFile.parentFile == cache)
        assertTrue(base.mkdir())
        val root = File(base, "kokoro").apply { mkdir() }
        val current = "a".repeat(40)
        val previous = "b".repeat(40)
        val currentDirectory = File(root, current).apply { mkdir() }
        val previousDirectory = File(root, previous).apply { mkdir() }
        val outside = File(base, "outside").apply { mkdir() }
        val currentFile = File(currentDirectory, "model.onnx").apply { writeText("current fixture") }
        val previousFile = File(previousDirectory, "model.onnx").apply { writeText("previous fixture") }
        val outsideFile = File(outside, "model.onnx").apply { writeText("unrelated fixture") }
        val linkedRevision = "c".repeat(40)
        val link = File(root, linkedRevision).toPath()
        try {
            Files.createSymbolicLink(link, (if (linkToCurrent) currentDirectory else outside).toPath())
            try {
                KokoroManagedFiles.removeObsolete(root, current, linkedSetOf(previous, linkedRevision), setOf("model.onnx"))
                fail("A revision alias must reject the complete deletion plan")
            } catch (_: IOException) {
                assertEquals("current fixture", currentFile.readText())
                assertEquals("previous fixture", previousFile.readText())
                assertEquals("unrelated fixture", outsideFile.readText())
            }
        } finally {
            // Remove the link first and clean only the explicit synthetic files/directories.
            Files.deleteIfExists(link)
            listOf(currentFile, previousFile, outsideFile).forEach { it.delete() }
            listOf(currentDirectory, previousDirectory, outside, root, base).forEach { it.delete() }
        }
    }
}
