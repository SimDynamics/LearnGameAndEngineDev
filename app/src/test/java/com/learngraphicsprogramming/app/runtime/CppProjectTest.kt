package com.learngraphicsprogramming.app.runtime

import java.io.File
import java.io.IOException
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Assume.assumeNoException
import org.junit.Test

class CppProjectTest {
    @Test fun defaultsCompileOnlyCppTranslationUnitsInProjectOrder() {
        val project = CppProject(linkedMapOf(
            "diferencia.h" to "int diferencia(int, int);",
            "src/diferencia.cpp" to "int diferencia(int a, int b) { return a - b; }",
            "main.cc" to "int main() {}",
            "notes.txt" to "No es una fuente.",
            "other.CXX" to "int otra() { return 1; }"
        ))
        assertEquals(listOf("src/diferencia.cpp", "main.cc", "other.CXX"), project.sourceFiles)
    }

    @Test fun explicitSelectionCanOmitImplementationWhileRetainingItsEditableFile() {
        val project = CppProject(
            mapOf("main.cpp" to "int main() {}", "diferencia.cpp" to "int diferencia();"),
            listOf("main.cpp")
        )
        assertEquals(listOf("main.cpp"), project.sourceFiles)
        assertTrue("diferencia.cpp" in project.files)
    }

    @Test fun buildSnapshotCannotChangeWithEditorsOrMutableCollections() {
        val files = linkedMapOf("main.cpp" to "version 1", "other.cpp" to "original")
        val selected = mutableListOf("main.cpp")
        val project = CppProject(files, selected)
        files["main.cpp"] = "version 2"
        files.remove("other.cpp")
        selected += "other.cpp"
        assertEquals("version 1", project.files.getValue("main.cpp"))
        assertEquals("original", project.files.getValue("other.cpp"))
        assertEquals(listOf("main.cpp"), project.sourceFiles)
        try {
            (project.files as MutableMap<String, String>)["main.cpp"] = "unexpected"
            fail("The project files must be immutable")
        } catch (_: UnsupportedOperationException) {
            // The caller cannot mutate an execution's snapshot through its exposed collection.
        }
        try {
            (project.sourceFiles as MutableList<String>).clear()
            fail("The selected translation units must be immutable")
        } catch (_: UnsupportedOperationException) {
            // Selection is part of the same immutable snapshot.
        }
    }

    @Test fun rejectsAbsoluteTraversalAndAmbiguousPaths() {
        listOf(
            "", " ", "/main.cpp", "../main.cpp", "src/../../main.cpp", "./main.cpp",
            "src/./main.cpp", "src//main.cpp", "src/", "C:/main.cpp", "C:\\main.cpp",
            "\\\\server\\main.cpp", "src\\main.cpp", " main.cpp", "main.cpp ", "main\u0000.cpp",
            "main\n.cpp"
        ).forEach { path ->
            assertInvalid(path) { CppProject.validatePath(path) }
            assertInvalid(path) { CppProject(mapOf("main.cpp" to "", path to "")) }
        }
    }

    @Test fun rejectsAFileThatIsAlsoTheParentDirectoryOfAnotherFile() {
        assertInvalid("file/directory collision") {
            CppProject(mapOf("src" to "", "src/main.cpp" to ""))
        }
    }

    @Test fun draftValidationAllowsIncompleteFilesWithoutRelaxingBuildRequirements() {
        CppProject.validateFiles(mapOf("include/api.h" to "int f();"))
        CppProject.validateFiles(emptyMap())
        assertInvalid("draft collision") {
            CppProject.validateFiles(mapOf("src" to "", "src/main.cpp" to ""))
        }
        assertInvalid("unsafe draft") {
            CppProject.validateFiles(mapOf("../api.h" to ""))
        }
        assertInvalid("headers-only build") { CppProject(mapOf("include/api.h" to "int f();")) }
    }

    @Test fun rejectsMissingRepeatedAndHeaderTranslationUnits() {
        val files = mapOf("main.cpp" to "", "api.h" to "int f();")
        assertInvalid("missing") { CppProject(files, listOf("missing.cpp")) }
        assertInvalid("repeated") { CppProject(files, listOf("main.cpp", "main.cpp")) }
        assertInvalid("header") { CppProject(files, listOf("api.h")) }
        assertInvalid("no selection") { CppProject(files, emptyList()) }
        assertInvalid("only header") { CppProject(mapOf("api.h" to "int f();")) }
        assertInvalid("empty project") { CppProject(emptyMap()) }
    }

    @Test fun nestedFilesAndRuntimeNamedFilesStayInTheProjectDirectory() = withTemporaryDirectory { job ->
        val runtimeInput = File(job, "stdin.txt").apply { writeText("runtime input") }
        val executable = File(job, "program.wasm").apply { writeText("existing artifact") }
        val project = CppProject(linkedMapOf(
            "main.cpp" to "#include \"include/api.h\"\nint main() {}",
            "include/api.h" to "#pragma once\n",
            "stdin.txt" to "editable project data",
            "program.wasm" to "editable project artifact name",
            "bin/wasm-ld" to "a harmless project file"
        ))
        val materialized = project.materialize(File(job, "project"))
        assertEquals("#pragma once\n", materialized.getValue("include/api.h").readText())
        assertEquals("editable project data", materialized.getValue("stdin.txt").readText())
        assertEquals("runtime input", runtimeInput.readText())
        assertEquals("existing artifact", executable.readText())
        assertFalse(File(job, "include/api.h").exists())
        assertFalse(File(job, "bin/wasm-ld").exists())
        val projectRoot = File(job, "project").canonicalPath + File.separator
        assertTrue(materialized.values.all { it.canonicalPath.startsWith(projectRoot) })
    }

    @Test fun nextBuildMaterializesEditedHeadersAndSourcesWithoutReusingEarlierJob() =
        withTemporaryDirectory { root ->
            val original = CppProject(mapOf("api.h" to "int f();", "main.cpp" to "return 7;"))
            val edited = CppProject(mapOf("api.h" to "double f();", "main.cpp" to "return 11;"))
            val first = original.materialize(File(root, "job1/project"))
            val second = edited.materialize(File(root, "job2/project"))
            File(root, "job1/build").mkdirs()
            File(root, "job1/build/main.cpp.o").writeText("old object")
            assertEquals("int f();", first.getValue("api.h").readText())
            assertEquals("double f();", second.getValue("api.h").readText())
            assertEquals("return 11;", second.getValue("main.cpp").readText())
            assertFalse(File(root, "job2/build/main.cpp.o").exists())
        }

    @Test fun symlinkedParentCannotMaterializeFilesOutsideTheProject() = withTemporaryDirectory { root ->
        val projectDirectory = File(root, "job/project").apply { mkdirs() }
        val outsideDirectory = File(root, "outside").apply { mkdirs() }
        val outsideHeader = File(outsideDirectory, "api.h").apply { writeText("preserve this file") }
        try {
            Files.createSymbolicLink(File(projectDirectory, "include").toPath(), outsideDirectory.toPath())
        } catch (error: IOException) {
            // Windows accounts without symlink privileges cannot exercise this filesystem case.
            assumeNoException(error)
        } catch (error: UnsupportedOperationException) {
            assumeNoException(error)
        }
        val project = CppProject(linkedMapOf("main.cpp" to "int main() {}", "include/api.h" to "overwrite"))
        assertInvalid("symlink outside project") { project.materialize(projectDirectory) }
        assertEquals("preserve this file", outsideHeader.readText())
        assertFalse(File(projectDirectory, "main.cpp").exists())
    }

    private fun assertInvalid(label: String, action: () -> Unit) {
        try {
            action()
            fail("Expected an invalid project: $label")
        } catch (_: IllegalArgumentException) {
            // Contract validation must happen before launching any subprocess.
        }
    }

    private fun withTemporaryDirectory(action: (File) -> Unit) {
        val directory = Files.createTempDirectory("lgp-cpp-project-test-").toFile()
        try {
            action(directory)
        } finally {
            directory.deleteRecursively()
        }
    }
}
