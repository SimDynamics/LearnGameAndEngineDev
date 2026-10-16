package com.learngraphicsprogramming.app.runtime

import java.io.File
import java.io.IOException
import java.nio.file.Files
import java.security.MessageDigest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Assume.assumeNoException
import org.junit.Test

class CppProjectHistoryTest {
    private val source = "#include <iostream>\r\n\r\nint cajas(int piezas) {\n\treturn (piezas + 3) / 4;  \n}\n"

    @Test fun exactFilesSourcesAndIndentationSurviveReopeningWithStableShaIdentity() = temporary { root ->
        val history = CppProjectHistory(root, "course/lesson/block")
        val files = linkedMapOf("componente/src/cajas.cpp" to source, "CMakeLists.txt" to "project(Cajas)\n", "léeme.txt" to "Español, français, 😀\r\n")
        val revision = history.register(files, listOf("componente/src/cajas.cpp"), "Base verificada", mapOf("componente/src/cajas.cpp" to "{\"origin\":\"predefined\"}"))
        val stored = root.walkTopDown().single { it.name == "${revision.id}.revision" }
        assertEquals(revision.id, hash(stored.readBytes()))
        val reopened = CppProjectHistory(root, "course/lesson/block").load(revision.id)
        assertEquals(revision.id, reopened.id)
        assertEquals(revision.createdAt, reopened.createdAt)
        assertEquals("Base verificada", reopened.label)
        assertEquals(files, reopened.files)
        assertEquals(listOf("componente/src/cajas.cpp"), reopened.sourceFiles)
        assertEquals(revision.indentation, reopened.indentation)
        assertEquals(listOf(revision.id), CppProjectHistory(root, "course/lesson/block").list().map { it.id })
        assertEquals(source, reopened.files.getValue("componente/src/cajas.cpp"))
    }

    @Test fun snapshotsAreImmutableEvenWhenCallerKeepsMutableDraftAndSelection() = temporary { root ->
        val files = linkedMapOf("main.cpp" to source)
        val sources = mutableListOf("main.cpp")
        val indentation = mutableMapOf("main.cpp" to "original metadata")
        val revision = CppProjectHistory(root, "immutable").register(files, sources, "Base", indentation)
        files["main.cpp"] = "edited"
        sources.clear()
        indentation.clear()
        assertEquals(source, revision.files["main.cpp"])
        assertEquals(listOf("main.cpp"), revision.sourceFiles)
        assertEquals("original metadata", revision.indentation["main.cpp"])
        assertUnsupported { (revision.files as MutableMap<String, String>)["main.cpp"] = "mutate" }
        assertUnsupported { (revision.sourceFiles as MutableList<String>).clear() }
        assertUnsupported { (revision.indentation as MutableMap<String, String>).clear() }
    }

    @Test fun repeatedExplicitRegistrationsHaveIndependentStableIdentitiesAndNeverDeletePriorRevisions() = temporary { root ->
        val history = CppProjectHistory(root, "repeated")
        val first = history.register(mapOf("main.cpp" to "int main() {}"), listOf("main.cpp"), "Primera")
        val second = history.register(first.files, first.sourceFiles, "Segunda")
        assertNotEquals(first.id, second.id)
        assertEquals(setOf(first.id, second.id), history.list().map { it.id }.toSet())
        assertEquals(first.files, history.load(first.id).files)
        assertEquals(first.files, history.load(second.id).files)
        assertEquals(2, root.walkTopDown().count { it.name.endsWith(".revision") })
    }

    @Test fun summariesExposeOnlyImmutableIdentityLabelAndTimeWhileKeepingFullRecordsLoadable() = temporary { root ->
        val history = CppProjectHistory(root, "lightweight-chooser")
        val first = history.register(mapOf("main.cpp" to source.repeat(300)), listOf("main.cpp"), "Primera", mapOf("main.cpp" to "private indentation"))
        val second = history.register(mapOf("main.cpp" to source.repeat(500)), listOf("main.cpp"), "Segunda")
        val summaries = CppProjectHistory(root, "lightweight-chooser").summaries()
        val ordered = history.list()
        assertEquals(ordered.map { CppProjectHistory.Summary(it.id, it.label, it.createdAt) }, summaries)
        assertEquals(setOf(first.id, second.id), summaries.map { it.id }.toSet())
        assertFalse(CppProjectHistory.Summary::class.java.declaredFields.any {
            Map::class.java.isAssignableFrom(it.type) || List::class.java.isAssignableFrom(it.type) ||
                it.type == CppProjectHistory.Revision::class.java
        })
        assertUnsupported { (summaries as MutableList<CppProjectHistory.Summary>).clear() }
        assertEquals(source.repeat(300), history.load(first.id).files["main.cpp"])
        assertEquals("private indentation", history.load(first.id).indentation["main.cpp"])
        val stored = root.walkTopDown().single { it.name == "${second.id}.revision" }
        stored.appendText("broken")
        assertStorageError { history.summaries() }
        assertTrue(stored.exists())
    }

    @Test fun projectIdentityScopesHistoriesEvenForTraversalLookingLogicalIds() = temporary { root ->
        val first = CppProjectHistory(root, "../../course A")
        val second = CppProjectHistory(root, "../../course B")
        val revision = first.register(mapOf("main.cpp" to "A"), listOf("main.cpp"), "A")
        assertTrue(second.list().isEmpty())
        assertStorageError { second.load(revision.id) }
        assertInvalid { second.restoreFile("main.cpp", mapOf("main.cpp" to "B"), revision) }
        assertEquals("A", first.load(revision.id).files["main.cpp"])
        assertTrue(root.listFiles()!!.all { it.name.matches(Regex("[0-9a-f]{64}")) })
    }

    @Test fun statusDistinguishesAllFourStatesWithoutChangingOriginalDraft() = temporary { root ->
        val history = CppProjectHistory(root, "status")
        val revision = history.register(linkedMapOf("same.cpp" to "same", "changed.cpp" to "old", "removed.h" to "old"), listOf("same.cpp"), "Base")
        val draft = linkedMapOf("same.cpp" to "same", "changed.cpp" to "new", "added.h" to "new")
        assertEquals(mapOf(
            "same.cpp" to CppProjectHistory.Change.UNCHANGED,
            "changed.cpp" to CppProjectHistory.Change.MODIFIED,
            "removed.h" to CppProjectHistory.Change.REMOVED,
            "added.h" to CppProjectHistory.Change.ADDED
        ), history.statuses(draft, revision).associate { it.path to it.change })
        assertEquals("new", draft["changed.cpp"])
        assertEquals("old", history.load(revision.id).files["changed.cpp"])
    }

    @Test fun chosenSourceRestoreKeepsIndependentCmakeCommentAndOtherDraftsExact() = temporary { root ->
        val history = CppProjectHistory(root, "cmake-practice")
        val base = linkedMapOf("componente/src/cajas.cpp" to source, "CMakeLists.txt" to "add_subdirectory(componente)\n", "cases.txt" to "0:0\n1:1\n4:1\n5:2\n7:2\n8:2\n9:3\n")
        val revision = history.register(base, listOf("componente/src/cajas.cpp"), "Siete casos correctos")
        val draft = LinkedHashMap(base).apply {
            put("componente/src/cajas.cpp", source.replace("(piezas + 3) / 4", "piezas / 4"))
            put("CMakeLists.txt", getValue("CMakeLists.txt") + "# comentario de otro trabajo\n")
            put("notes.txt", "otro borrador\t\n")
        }
        val savedDraft = LinkedHashMap(draft)
        val restored = history.restoreFile("componente/src/cajas.cpp", draft, revision)
        assertEquals(source, restored["componente/src/cajas.cpp"])
        assertEquals(draft["CMakeLists.txt"], restored["CMakeLists.txt"])
        assertEquals(draft["cases.txt"], restored["cases.txt"])
        assertEquals(draft["notes.txt"], restored["notes.txt"])
        assertEquals(savedDraft, draft)
        assertEquals(base, history.load(revision.id).files)
        assertEquals(1, history.list().size)
    }

    @Test fun restoringSelectedDeletionAndAddedFileTouchesNoOtherPath() = temporary { root ->
        val history = CppProjectHistory(root, "existence")
        val revision = history.register(mapOf("main.cpp" to "main", "old.h" to "original header"), listOf("main.cpp"), "Base")
        val draft = mapOf("main.cpp" to "edited main", "new.h" to "new header")
        val restoredOld = history.restoreFile("old.h", draft, revision)
        assertEquals("original header", restoredOld["old.h"])
        assertEquals("edited main", restoredOld["main.cpp"])
        assertEquals("new header", restoredOld["new.h"])
        val restoredNew = history.restoreFile("new.h", draft, revision)
        assertFalse("new.h" in restoredNew)
        assertEquals("edited main", restoredNew["main.cpp"])
    }

    @Test fun lineDiffPreservesTabsSpacesLineEndingsAndMissingFinalNewline() = temporary { root ->
        val history = CppProjectHistory(root, "line-diff")
        val before = "same\r\n\treturn 1;  \nlast"
        val after = "same\r\n    return 1; \nlast\n"
        val revision = history.register(mapOf("main.cpp" to before), listOf("main.cpp"), "Base")
        val diff = history.diff("main.cpp", mapOf("main.cpp" to after), revision)
        assertEquals(CppProjectHistory.Change.MODIFIED, diff.change)
        assertEquals(before, reconstructBefore(diff))
        assertEquals(after, reconstructAfter(diff))
        assertEquals(CppProjectHistory.DiffLine(CppProjectHistory.LineType.CONTEXT, 1, 1, "same", "\r\n"), diff.lines.first())
        assertTrue(diff.lines.any { it.type == CppProjectHistory.LineType.REMOVED && it.text == "\treturn 1;  " })
        assertTrue(diff.lines.any { it.type == CppProjectHistory.LineType.ADDED && it.text == "    return 1; " })
        assertTrue(diff.lines.any { it.type == CppProjectHistory.LineType.REMOVED && it.text == "last" && it.lineEnding.isEmpty() })
        assertTrue(diff.lines.any { it.type == CppProjectHistory.LineType.ADDED && it.text == "last" && it.lineEnding == "\n" })
        assertEquals(listOf(1, 2, 3), diff.lines.mapNotNull { it.beforeLine })
        assertEquals(listOf(1, 2, 3), diff.lines.mapNotNull { it.afterLine })
    }

    @Test fun lineDiffAddedRemovedEmptyAndUnchangedContentsReconstructExactly() = temporary { root ->
        val history = CppProjectHistory(root, "diff-existence")
        val revision = history.register(mapOf("old.cpp" to "one\ntwo\n", "same.cpp" to "unchanged", "empty.cpp" to ""), listOf("old.cpp"), "Base")
        val draft = mapOf("new.cpp" to "three\nfour", "same.cpp" to "unchanged", "new-empty.cpp" to "")
        listOf("old.cpp", "new.cpp", "same.cpp", "empty.cpp", "new-empty.cpp", "absent.cpp").forEach { path ->
            val diff = history.diff(path, draft, revision)
            assertEquals(revision.files[path].orEmpty(), reconstructBefore(diff))
            assertEquals(draft[path].orEmpty(), reconstructAfter(diff))
        }
        assertEquals(CppProjectHistory.Change.ADDED, history.diff("new-empty.cpp", draft, revision).change)
        assertEquals(CppProjectHistory.Change.REMOVED, history.diff("empty.cpp", draft, revision).change)
    }

    @Test fun largeDiffFallsBackWithoutLosingAnyTextOrInventingContext() = temporary { root ->
        val history = CppProjectHistory(root, "large-diff")
        val before = "prefix\n" + (1..1100).joinToString("") { "old $it\t\n" } + "suffix\n"
        val after = "prefix\n" + (1..1100).joinToString("") { "new $it  \n" } + "suffix\n"
        val revision = history.register(mapOf("main.cpp" to before), listOf("main.cpp"), "Base")
        val diff = history.diff("main.cpp", mapOf("main.cpp" to after), revision)
        assertEquals(before, reconstructBefore(diff))
        assertEquals(after, reconstructAfter(diff))
        assertEquals(2, diff.lines.count { it.type == CppProjectHistory.LineType.CONTEXT })
    }

    @Test fun corruptedRevisionRaisesExplicitErrorAndIsNeverDeletedOrOverwritten() = temporary { root ->
        val history = CppProjectHistory(root, "corruption")
        val revision = history.register(mapOf("main.cpp" to source), listOf("main.cpp"), "Base")
        val stored = root.walkTopDown().single { it.name == "${revision.id}.revision" }
        val bytes = stored.readBytes().apply { this[lastIndex] = (this[lastIndex].toInt() xor 1).toByte() }
        stored.writeBytes(bytes)
        assertStorageError { history.load(revision.id) }
        assertStorageError { history.list() }
        assertTrue(stored.exists())
        assertTrue(bytes.contentEquals(stored.readBytes()))
        val fresh = history.register(mapOf("main.cpp" to "new draft"), listOf("main.cpp"), "Nueva")
        assertEquals("new draft", history.load(fresh.id).files["main.cpp"])
        assertTrue(bytes.contentEquals(stored.readBytes()))
    }

    @Test fun malformedPayloadWithMatchingHashStillFailsClosedWithoutRemovingRecords() = temporary { root ->
        val history = CppProjectHistory(root, "format-corruption")
        val valid = history.register(mapOf("main.cpp" to "ok"), listOf("main.cpp"), "Base")
        val directory = root.listFiles()!!.single()
        val malformed = byteArrayOf(127, 127, 127, 127)
        val id = hash(malformed)
        val broken = File(directory, "$id.revision").apply { writeBytes(malformed) }
        assertStorageError { history.load(id) }
        assertStorageError { history.list() }
        assertTrue(broken.exists())
        assertEquals("ok", history.load(valid.id).files["main.cpp"])
    }

    @Test fun failedStorageKeepsPublishedSnapshotsAndIndependentDraftUntouched() = temporary { root ->
        val history = CppProjectHistory(root, "storage-failure")
        val original = history.register(mapOf("main.cpp" to source), listOf("main.cpp"), "Base")
        val stored = root.walkTopDown().single { it.name == "${original.id}.revision" }
        val bytes = stored.readBytes()
        val draft = linkedMapOf("main.cpp" to "edited draft")
        // Model an unavailable project directory without relying on OS-specific ACL privileges.
        val projectDirectory = requireNotNull(stored.parentFile)
        val preservedDirectory = File(root, "preserved-history-records")
        assertTrue(projectDirectory.renameTo(preservedDirectory))
        projectDirectory.writeText("blocked directory slot")
        assertStorageError { history.register(draft, listOf("main.cpp"), "Cannot publish") }
        assertEquals("edited draft", draft["main.cpp"])
        assertEquals("blocked directory slot", projectDirectory.readText())
        assertTrue(bytes.contentEquals(File(preservedDirectory, stored.name).readBytes()))
        assertTrue(projectDirectory.delete())
        assertTrue(preservedDirectory.renameTo(projectDirectory))
        assertEquals(source, history.load(original.id).files["main.cpp"])

        val impossibleRoot = File(root, "not-a-directory").apply { writeText("keep file") }
        assertStorageError { CppProjectHistory(impossibleRoot, "other").register(draft, listOf("main.cpp"), "Fail") }
        assertEquals("edited draft", draft["main.cpp"])
        assertEquals("keep file", impossibleRoot.readText())
        assertTrue(bytes.contentEquals(stored.readBytes()))
        assertEquals(source, history.load(original.id).files["main.cpp"])
    }

    @Test fun unpublishedInterruptedWriteIsNotARevisionAndDoesNotPreventReopeningValidRecords() = temporary { root ->
        val history = CppProjectHistory(root, "interruption")
        val original = history.register(mapOf("main.cpp" to source), listOf("main.cpp"), "Base")
        val pending = File(root.listFiles()!!.single(), "revision-interrupted.pending").apply { writeText("partial bytes") }
        assertEquals(listOf(original.id), CppProjectHistory(root, "interruption").list().map { it.id })
        assertEquals("partial bytes", pending.readText())
        assertEquals(source, history.load(original.id).files["main.cpp"])
    }

    @Test fun rejectsUnsafePathsSelectionsCollisionsAndInvalidIdentityBeforeCreatingStorage() = temporary { root ->
        val history = CppProjectHistory(root, "validation")
        listOf("../main.cpp", "src/../../main.cpp", "C:/main.cpp", "/main.cpp", "src\\main.cpp", "./main.cpp").forEach { path ->
            assertInvalid { history.register(mapOf(path to ""), emptyList(), "Base") }
        }
        assertInvalid { history.register(mapOf("src" to "", "src/main.cpp" to ""), listOf("src/main.cpp"), "Base") }
        assertInvalid { history.register(mapOf("main.cpp" to ""), listOf("missing.cpp"), "Base") }
        assertInvalid { history.register(mapOf("main.cpp" to ""), listOf("main.cpp", "main.cpp"), "Base") }
        assertInvalid { history.register(mapOf("api.h" to ""), listOf("api.h"), "Base") }
        assertInvalid { history.register(mapOf("main.cpp" to ""), emptyList(), "Base", mapOf("other.cpp" to "{}")) }
        assertInvalid { history.register(mapOf("main.cpp" to ""), emptyList(), " ") }
        assertInvalid { CppProjectHistory(root, "") }
        assertInvalid { history.load("../other") }
        assertTrue(root.listFiles()!!.isEmpty())
    }

    @Test fun emptyAndHeadersOnlyDraftsCanBeRegisteredWithoutInventingBuildSources() = temporary { root ->
        val history = CppProjectHistory(root, "incomplete")
        val empty = history.register(emptyMap(), emptyList(), "Vacío")
        val header = history.register(mapOf("include/api.h" to "int f();\n"), emptyList(), "Sólo cabecera")
        assertTrue(history.load(empty.id).files.isEmpty())
        assertEquals("int f();\n", history.load(header.id).files["include/api.h"])
        assertTrue(history.load(header.id).sourceFiles.isEmpty())
    }

    @Test fun symlinkedProjectDirectoryCannotWriteOrReadAnotherProjectsRecords() = temporary { root ->
        val first = CppProjectHistory(File(root, "histories"), "first")
        val original = first.register(mapOf("main.cpp" to source), listOf("main.cpp"), "Base")
        val firstDirectory = File(root, "histories").listFiles()!!.single()
        val secondPath = File(File(root, "histories"), hash("second".toByteArray(Charsets.UTF_8)))
        createSymlinkOrSkip(secondPath, firstDirectory)
        val second = CppProjectHistory(File(root, "histories"), "second")
        assertStorageError { second.register(mapOf("main.cpp" to "overwrite"), listOf("main.cpp"), "Fail") }
        assertStorageError { second.list() }
        assertEquals(source, first.load(original.id).files["main.cpp"])
        assertEquals(1, first.list().size)
    }

    @Test fun symlinkedRevisionFileIsRejectedEvenWhenItsBytesMatchTheHash() = temporary { root ->
        val history = CppProjectHistory(File(root, "histories"), "linked-file")
        val original = history.register(mapOf("main.cpp" to source), listOf("main.cpp"), "Base")
        val snapshot = File(root, "histories").walkTopDown().single { it.name == "${original.id}.revision" }
        val outside = File(root, "outside-record").apply { writeBytes(snapshot.readBytes()) }
        assertTrue(snapshot.delete())
        createSymlinkOrSkip(snapshot, outside)
        assertStorageError { history.load(original.id) }
        assertStorageError { history.list() }
        assertEquals(original.id, hash(outside.readBytes()))
    }

    private fun createSymlinkOrSkip(link: File, destination: File) {
        try {
            Files.createSymbolicLink(link.toPath(), destination.toPath())
        } catch (error: IOException) {
            assumeNoException("Windows no concede privilegios de enlaces simbólicos a esta cuenta.", error)
        } catch (error: UnsupportedOperationException) {
            assumeNoException(error)
        }
    }

    private fun reconstructBefore(diff: CppProjectHistory.FileDiff): String = diff.lines
        .filter { it.type != CppProjectHistory.LineType.ADDED }.joinToString("") { it.text + it.lineEnding }
    private fun reconstructAfter(diff: CppProjectHistory.FileDiff): String = diff.lines
        .filter { it.type != CppProjectHistory.LineType.REMOVED }.joinToString("") { it.text + it.lineEnding }
    private fun hash(bytes: ByteArray): String = MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it) }
    private fun assertInvalid(action: () -> Unit) {
        try { action(); fail("Expected invalid snapshot input") } catch (_: IllegalArgumentException) { }
    }
    private fun assertStorageError(action: () -> Unit) {
        try { action(); fail("Expected explicit history storage failure") } catch (_: CppProjectHistory.StorageException) { }
    }
    private fun assertUnsupported(action: () -> Unit) {
        try { action(); fail("Expected immutable snapshot") } catch (_: UnsupportedOperationException) { }
    }
    private fun temporary(action: (File) -> Unit) {
        val root = Files.createTempDirectory("lgp-cpp-history-").toFile()
        try { action(root) } finally { root.deleteRecursively() }
    }
}
