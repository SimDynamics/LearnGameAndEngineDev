package com.learngraphicsprogramming.app.runtime

import org.junit.Assert.*
import org.junit.Test

/** The optional CMake snapshot must not change the selected-source contract of CppProject. */
class CppCmakeProjectTest {
    private val files = linkedMapOf(
        "CMakeLists.txt" to "cmake_minimum_required(VERSION 3.20)\nproject(Existencias LANGUAGES CXX)\nadd_executable(consulta main.cpp)\n",
        "main.cpp" to "int main() { return 0; }",
        "include/existencias.h" to "bool debe_reponer(int, int);",
        "cmake/opciones.cmake" to "option(PRUEBA \"Prueba\" OFF)\n"
    )

    @Test fun snapshotIsIndependentOfSubsequentEditorAndOptionChanges() {
        val editableFiles = LinkedHashMap(files)
        val editableOptions = linkedMapOf("PRUEBA" to "OFF")
        val snapshot = CppCmakeProject(editableFiles, "consulta", editableOptions)
        val fingerprint = snapshot.fingerprint
        val configuration = snapshot.configurationFingerprint
        editableFiles["main.cpp"] = "broken next revision"
        editableFiles.remove("CMakeLists.txt")
        editableOptions["PRUEBA"] = "ON"
        assertEquals(files, snapshot.files)
        assertEquals(mapOf("PRUEBA" to "OFF"), snapshot.options)
        assertEquals(fingerprint, snapshot.fingerprint)
        assertEquals(configuration, snapshot.configurationFingerprint)
        assertImmutable { (snapshot.files as MutableMap<String, String>)["main.cpp"] = "unexpected" }
        assertImmutable { (snapshot.options as MutableMap<String, String>)["PRUEBA"] = "ON" }
    }

    @Test fun sourceAndHeaderEditsInvalidateArtifactWithoutRequiringConfigurationAgain() {
        val original = CppCmakeProject(files, "consulta")
        for (path in listOf("main.cpp", "include/existencias.h")) {
            val changed = CppCmakeProject(files + (path to files.getValue(path) + "\n// edited\n"), "consulta")
            assertNotEquals("An edited translation unit/interface cannot execute the prior artifact", original.fingerprint, changed.fingerprint)
            assertEquals("An unchanged description and file set keep generated rules usable", original.configurationFingerprint, changed.configurationFingerprint)
        }
    }

    @Test fun changedDescriptionOptionsAndToolchainRequireFreshConfiguration() {
        val original = CppCmakeProject(files, "consulta", mapOf("PRUEBA" to "OFF"))
        val changes = listOf(
            CppCmakeProject(files + ("CMakeLists.txt" to files.getValue("CMakeLists.txt") + "# revised\n"), "consulta", mapOf("PRUEBA" to "OFF")),
            CppCmakeProject(files + ("cmake/opciones.cmake" to "option(PRUEBA \"Prueba\" ON)\n"), "consulta", mapOf("PRUEBA" to "OFF")),
            CppCmakeProject(files, "consulta", mapOf("PRUEBA" to "ON")),
            CppCmakeProject(files, "consulta", emptyMap()),
            CppCmakeProject(files, "consulta", mapOf("PRUEBA" to "OFF"), "Release"),
            CppCmakeProject(files, "consulta", mapOf("PRUEBA" to "OFF"), compilerVariant = CppCompilerVariant.OPTIONAL_CLANG_21),
            CppCmakeProject(files + ("another.cpp" to "int another() { return 1; }"), "consulta", mapOf("PRUEBA" to "OFF")),
            CppCmakeProject(files - "main.cpp", "consulta", mapOf("PRUEBA" to "OFF"))
        )
        changes.forEach { changed ->
            assertNotEquals(original.fingerprint, changed.fingerprint)
            assertNotEquals(original.configurationFingerprint, changed.configurationFingerprint)
        }
    }

    @Test fun targetChangesKeepConfigurationButInvalidateThePreviousExecutable() {
        val original = CppCmakeProject(files, "consulta")
        val requestedMissingTarget = CppCmakeProject(files, "objetivo_inexistente")
        assertNotEquals(original.fingerprint, requestedMissingTarget.fingerprint)
        assertEquals("CMake's build tool, rather than snapshot validation, diagnoses a missing target", original.configurationFingerprint, requestedMissingTarget.configurationFingerprint)
    }

    @Test fun fingerprintsAreStableAcrossMapInsertionOrderAndUnambiguousForTextBoundaries() {
        val original = CppCmakeProject(files, "consulta", linkedMapOf("A" to "ON", "B" to "OFF"))
        val reordered = CppCmakeProject(files.entries.reversed().associate { it.toPair() }, "consulta", linkedMapOf("B" to "OFF", "A" to "ON"))
        assertEquals(original.fingerprint, reordered.fingerprint)
        assertEquals(original.configurationFingerprint, reordered.configurationFingerprint)
        val one = CppCmakeProject(files + ("notes.txt" to "ab\nc"), "consulta")
        val two = CppCmakeProject(files + ("notes.txt" to "a\nbc"), "consulta")
        assertNotEquals(one.fingerprint, two.fingerprint)
        assertNotEquals(one.configurationFingerprint, two.configurationFingerprint)
    }

    @Test fun incompleteDraftsRetainFilesAndDeferCmakeDiagnosticsToTheActualTool() {
        val descriptionOnly = CppCmakeProject(mapOf("CMakeLists.txt" to "project(Existencias LANGUAGES CXX)\n"), "consulta")
        val withoutDescription = CppCmakeProject(mapOf("include/existencias.h" to "bool debe_reponer(int, int);"), "consulta")
        assertEquals(1, descriptionOnly.files.size)
        assertEquals(1, withoutDescription.files.size)
        assertFalse("CMake projects need not manually select translation units", descriptionOnly.files.keys.any(CppProject::isSourceFile))
    }

    @Test fun unsafeVirtualPathsAndConflictsStillCannotEscapeTheProject() {
        for (path in listOf("../CMakeLists.txt", "/CMakeLists.txt", "C:/CMakeLists.txt", "cmake\\options.cmake", "src//main.cpp", "main\n.cpp")) {
            assertInvalid(path) { CppCmakeProject(files + (path to ""), "consulta") }
        }
        assertInvalid("file/folder conflict") { CppCmakeProject(files + ("include" to ""), "consulta") }
        // Legacy selected-source validation is deliberately unchanged.
        assertInvalid("legacy still rejects a headers-only build") { CppProject(mapOf("api.h" to "int f();")) }
        assertInvalid("legacy still rejects an empty source selection") { CppProject(mapOf("main.cpp" to "int main() {}"), emptyList()) }
    }

    @Test fun optionsCannotOverrideTheInstalledToolchainOrTurnIntoAdditionalArguments() {
        for (key in listOf("", "A\nB", "A=ON", "-DOTHER", "CMAKE_CXX_COMPILER", "CMAKE_TOOLCHAIN_FILE", "CMAKE_GENERATOR", "CMAKE_BUILD_TYPE")) {
            assertInvalid(key) { CppCmakeProject(files, "consulta", mapOf(key to "ON")) }
        }
        assertInvalid("NUL option value") { CppCmakeProject(files, "consulta", mapOf("PRUEBA" to "ON\u0000OFF")) }
        assertInvalid("blank target") { CppCmakeProject(files, " ") }
        assertInvalid("target with control character") { CppCmakeProject(files, "consulta\nother") }
    }

    @Test fun editableOptionsRetainValuesAndRejectAmbiguousOrReservedEntries() {
        assertEquals(linkedMapOf("PRUEBA" to "ON", "MENSAJE" to "a = b", "VACIA" to ""),
            CppCmakeProject.parseOptions("\n PRUEBA =ON\nMENSAJE=a = b\nVACIA=\n"))
        for (text in listOf("PRUEBA", "=ON", "PRUEBA=ON\nPRUEBA=OFF", "CMAKE_GENERATOR=Ninja", "LGP_PRIVATE=OFF")) {
            assertInvalid(text) { CppCmakeProject.parseOptions(text) }
        }
    }

    private fun assertInvalid(label: String, action: () -> Unit) {
        try { action(); fail("Expected invalid CMake snapshot: $label") }
        catch (_: IllegalArgumentException) { /* Contract error occurs before native subprocesses. */ }
    }
    private fun assertImmutable(action: () -> Unit) {
        try { action(); fail("Build snapshots must be immutable") }
        catch (_: UnsupportedOperationException) { /* A caller cannot mutate a running snapshot. */ }
    }
}
