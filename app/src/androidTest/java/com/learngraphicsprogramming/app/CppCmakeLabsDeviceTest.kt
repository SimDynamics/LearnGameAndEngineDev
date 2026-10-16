package com.learngraphicsprogramming.app

import android.app.Activity
import android.app.Application
import android.app.KeyguardManager
import android.content.Context
import android.content.Intent
import android.content.pm.ActivityInfo
import android.content.res.Configuration
import android.graphics.Bitmap
import android.graphics.Rect
import android.os.Bundle
import android.os.PowerManager
import android.os.SystemClock
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.view.inputmethod.InputMethodManager
import android.widget.EditText
import android.widget.ScrollView
import android.widget.Spinner
import android.widget.TextView
import androidx.test.core.app.ActivityScenario
import androidx.test.espresso.Espresso.onView
import androidx.test.espresso.action.ViewActions.click
import androidx.test.espresso.action.ViewActions.closeSoftKeyboard
import androidx.test.espresso.action.ViewActions.replaceText
import androidx.test.espresso.matcher.RootMatchers.isDialog
import androidx.test.espresso.matcher.ViewMatchers.withId
import androidx.test.espresso.matcher.ViewMatchers.withTagValue
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.narration.NarrationSettingsRepository
import com.learngraphicsprogramming.app.runtime.CppCmakeEngine
import com.learngraphicsprogramming.app.runtime.CppCmakeProject
import com.learngraphicsprogramming.app.runtime.CppCmakeResult
import org.hamcrest.Matchers.equalTo
import org.json.JSONArray
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.security.MessageDigest
import java.util.IdentityHashMap
import java.util.UUID
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference

/** Real Android CMake phases and the existing lesson renderer. The fixtures are only in
 * memory, under exclusive QA identities; no curriculum or normal-package state is written. */
@RunWith(AndroidJUnit4::class)
class CppCmakeLabsDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val preferences = linkedMapOf<String, Map<String, *>>()
    private val fixtureActivities = IdentityHashMap<LessonActivity, String>()
    private var originalPreferenceFiles = emptySet<String>()
    private var preserved = false
    private var screenGuardRegistered = false
    private var screenWakeLock: PowerManager.WakeLock? = null
    private val screenGuard = object : Application.ActivityLifecycleCallbacks {
        override fun onActivityCreated(activity: Activity, state: Bundle?) {
            if (activity is LessonActivity && activity.packageName in QA_PACKAGES)
                activity.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        }
        override fun onActivityStarted(activity: Activity) = Unit
        override fun onActivityResumed(activity: Activity) = Unit
        override fun onActivityPaused(activity: Activity) = Unit
        override fun onActivityStopped(activity: Activity) = Unit
        override fun onActivitySaveInstanceState(activity: Activity, state: Bundle) = Unit
        override fun onActivityDestroyed(activity: Activity) = Unit
    }

    @Before fun requireQaAndPreserveAllStateBeforeAnyWrite() {
        assertTrue("Never target the student's normal application", context.packageName in QA_PACKAGES)
        assertEquals(context.packageName, BuildConfig.APPLICATION_ID)
        val power = context.getSystemService(Context.POWER_SERVICE) as PowerManager
        assertTrue("Wake the device before native acceptance checks", power.isInteractive)
        val keyguard = context.getSystemService(Context.KEYGUARD_SERVICE) as KeyguardManager
        assertFalse("Unlock the device before native acceptance checks", keyguard.isDeviceLocked)
        assertFalse("Dismiss the visual lock screen before native acceptance checks", keyguard.isKeyguardLocked)
        originalPreferenceFiles = preferenceFiles()
        (originalPreferenceFiles + KNOWN_PREFERENCES).forEach { name ->
            preferences[name] = context.getSharedPreferences(name, Context.MODE_PRIVATE).all.mapValues { (_, value) ->
                if (value is Set<*>) value.toSet() else value
            }
        }
        preserved = true
        @Suppress("DEPRECATION")
        val lock = power.newWakeLock(PowerManager.SCREEN_BRIGHT_WAKE_LOCK or PowerManager.ON_AFTER_RELEASE, "${context.packageName}:CmakeQa")
        screenWakeLock = lock
        lock.acquire(1_200_000L)
        (context.applicationContext as Application).registerActivityLifecycleCallbacks(screenGuard)
        screenGuardRegistered = true
        NarrationSettingsRepository.saveEnabled(context, false)
        // A finite reserve makes an accidental lab penalty observable.
        assertTrue(context.getSharedPreferences(ATTEMPTS, Context.MODE_PRIVATE).edit()
            .putBoolean("unlimited", false).putFloat("reserve_$ROUTE", 4f)
            .putLong("updated_at_$ROUTE", System.currentTimeMillis()).commit())
    }

    @After fun restoreEveryQaPreferenceIncludingDraftsAndProgress() {
        if (context.packageName !in QA_PACKAGES || BuildConfig.APPLICATION_ID != context.packageName || !preserved) return
        try {
            instrumentation.waitForIdleSync()
            (preferenceFiles() + preferences.keys).forEach { name ->
                if (name !in originalPreferenceFiles) {
                    assertTrue(context.deleteSharedPreferences(name))
                } else {
                    val original = preferences.getValue(name)
                    val prefs = context.getSharedPreferences(name, Context.MODE_PRIVATE)
                    val edit = prefs.edit().clear()
                    original.forEach { (key, value) -> when (value) {
                        is String -> edit.putString(key, value)
                        is Int -> edit.putInt(key, value)
                        is Long -> edit.putLong(key, value)
                        is Float -> edit.putFloat(key, value)
                        is Boolean -> edit.putBoolean(key, value)
                        is Set<*> -> edit.putStringSet(key, value.filterIsInstance<String>().toSet())
                        else -> error("Unsupported preference type: $name/$key")
                    } }
                    assertTrue(edit.commit())
                    assertEquals("Restore every original QA value", original, prefs.all)
                }
            }
            assertEquals(originalPreferenceFiles, preferenceFiles())
        } finally {
            screenWakeLock?.takeIf { it.isHeld }?.release()
            if (screenGuardRegistered)
                (context.applicationContext as Application).unregisterActivityLifecycleCallbacks(screenGuard)
        }
    }

    @Test fun nativeCmakeBuildsOnlyTheNamedTargetAndSeparatesEveryPhase() {
        withFixture(fixture()) { scenario, fixture ->
            assertButtons(scenario, configure = true, build = false, run = false)
            val configured = action(scenario, "cpp_cmake_configure")
            assertEquals(0, configured.exitCode)
            assertEquals("GENERATION", configured.phase.name)
            assertTrue("Configuration and generation keep their distinct records", configured.records.any {
                it.phase.name == "CONFIGURATION" && it.exitCode == 0 })
            assertNull("Configuration/generation does not build our application", configured.artifact)
            assertNull("Configuration cannot execute the application", configured.execution)
            assertFalse("Only compiler/tool checks may have been built", configured.workspace.walkTopDown()
                .any { it.isFile && it.name in setOf("consulta", "consulta.wasm") })
            assertRecordsUseRealCmake(configured)
            assertButtons(scenario, configure = true, build = true, run = false)
            capture(scenario, fixture, "valid-configure-generate", "cpp_status", configured)

            val built = action(scenario, "cpp_cmake_build")
            assertBuilt(built)
            assertNull("A successful build does not execute the behavior cases", built.execution)
            assertTrue("CMake selected the requested target", built.records.any { record ->
                record.command.windowed(2).any { it == listOf("--target", "consulta") }
            })
            assertButtons(scenario, configure = true, build = true, run = true)
            capture(scenario, fixture, "valid-build-no-execution", "cpp_status", built)

            val executed = action(scenario, "cpp_run")
            assertExecuted(scenario, executed, "1:1:0")
            capture(scenario, fixture, "valid-execution", "cpp_stdout", executed)
            scenario.onActivity { activity ->
                assertEquals("No manual source checkbox decides CMake's translation units", 0,
                    descendants(content(activity)).count { it.tag == "cpp_source_enabled" && it.visibility == View.VISIBLE })
                assertEquals("The source JSON remains a technical fixture", fixture.block.toString(),
                    (field(activity, "lessonJson") as JSONObject).getJSONArray("pages").getJSONObject(0)
                        .getJSONArray("blocks").getJSONObject(0).toString())
            }
        }
    }

    @Test fun editsAndActualCmakeOptionsProduceNewArtifactsRatherThanOldObservations() {
        withFixture(fixture()) { scenario, fixture ->
            configureBuildRun(scenario, "1:1:0")
            editFile(scenario, "existencias.cpp") { source ->
                assertTrue(source.contains("disponibles <= minimo"))
                source.replace("disponibles <= minimo", "disponibles < minimo")
            }
            assertButtons(scenario, configure = true, build = true, run = false)
            assertNoOldOutput(scenario)
            val strictBuilt = action(scenario, "cpp_cmake_build")
            assertBuilt(strictBuilt)
            val strict = action(scenario, "cpp_run")
            assertExecuted(scenario, strict, "1:0:0")
            capture(scenario, fixture, "source-edit-build-success-behavior-differs", "cpp_stdout", strict)

            editFile(scenario, "existencias.cpp") { INVENTORY_SOURCE }
            scenario.onActivity { (tagged(it, "cpp_cmake_options") as EditText).setText("USAR_COMPARACION_ESTRICTA=ON") }
            instrumentation.waitForIdleSync()
            assertButtons(scenario, configure = true, build = false, run = false)
            assertNoOldOutput(scenario)
            val configured = action(scenario, "cpp_cmake_configure")
            assertEquals(0, configured.exitCode)
            assertTrue("The requested option is passed to real CMake", configured.records.any { record ->
                record.command.any { it == "-DUSAR_COMPARACION_ESTRICTA=ON" }
            })
            assertTrue("The real generated cache preserves the option", cacheText(configured).contains("USAR_COMPARACION_ESTRICTA:BOOL=ON"))
            val optionBuilt = action(scenario, "cpp_cmake_build")
            assertBuilt(optionBuilt)
            val optionRun = action(scenario, "cpp_run")
            assertExecuted(scenario, optionRun, "1:0:0")
            capture(scenario, fixture, "option-on-changes-compiled-behavior", "cpp_stdout", optionRun)

            editFile(scenario, "CMakeLists.txt") { cmake -> cmake + "\n# A changed description must configure again.\n" }
            assertButtons(scenario, configure = true, build = false, run = false)
            scenario.onActivity { (tagged(it, "cpp_cmake_options") as EditText).setText("USAR_COMPARACION_ESTRICTA=OFF") }
            val recovered = configureBuildRun(scenario, "1:1:0")
            assertNotEquals("Changing options and rebuilding cannot present the previous artifact as current",
                optionRun.projectFingerprint, recovered.projectFingerprint)
            capture(scenario, fixture, "description-option-off-reconfigured", "cpp_stdout", recovered)
        }
    }

    @Test fun failuresBelongToTheirActualCmakePhaseAndNeverExecuteThePreviousProgram() {
        withFixture(fixture()) { scenario, fixture ->
            configureBuildRun(scenario, "1:1:0")
            editFile(scenario, "CMakeLists.txt") { it + "\ncomando_cmake_inexistente()\n" }
            val syntax = action(scenario, "cpp_cmake_configure")
            assertFailedWithoutExecution(scenario, syntax, "CONFIGURATION", "comando_cmake_inexistente")
            assertButtons(scenario, configure = true, build = false, run = false)
            captureDiagnosticSegment(scenario, fixture, "configuration-invalid-description", syntax)

            editFile(scenario, "CMakeLists.txt") { CMAKE_DESCRIPTION +
                "\nmessage(STATUS \"Configuring done\")\nmessage(FATAL_ERROR \"fallo de configuración QA\")\n" }
            val authoredMarker = action(scenario, "cpp_cmake_configure")
            assertFailedWithoutExecution(scenario, authoredMarker, "CONFIGURATION", "fallo de configuración QA")
            assertTrue("The misleading authored status is present in the actual native output",
                authoredMarker.stdout.contains("Configuring done"))
            assertTrue("CMake's terminal failure, rather than arbitrary project prose, identifies configuration failure",
                (authoredMarker.stdout + authoredMarker.stderr).contains("Configuring incomplete, errors occurred!"))
            assertButtons(scenario, configure = true, build = false, run = false)
            capture(scenario, fixture, "configuration-fatal-after-authored-status", "cpp_status", authoredMarker)

            editFile(scenario, "CMakeLists.txt") { CMAKE_DESCRIPTION.replace("main.cpp existencias.cpp", "main.cpp fuente_ausente.cpp") }
            val missingSource = action(scenario, "cpp_cmake_configure")
            assertFailedWithoutExecution(scenario, missingSource, "GENERATION", "fuente_ausente.cpp")
            assertButtons(scenario, configure = true, build = false, run = false)
            capture(scenario, fixture, "generation-missing-source", "cpp_status", missingSource)

            editFile(scenario, "CMakeLists.txt") { CMAKE_DESCRIPTION }
            editFile(scenario, "existencias.cpp") { it + "\nesto no es codigo cpp;\n" }
            val configured = action(scenario, "cpp_cmake_configure")
            assertEquals("CMake checks its tools without compiling the invalid application source", 0, configured.exitCode)
            assertNull(configured.artifact)
            val cppError = action(scenario, "cpp_cmake_build")
            assertFailedWithoutExecution(scenario, cppError, "BUILD", "existencias.cpp")
            assertTrue("C++ construction diagnostics retain a source line", Regex("existencias\\.cpp:\\d+").containsMatchIn(cppError.stdout + cppError.stderr))
            assertButtons(scenario, configure = true, build = true, run = false)
            capture(scenario, fixture, "build-cpp-syntax-error", "cpp_status", cppError)

            editFile(scenario, "existencias.cpp") { INVENTORY_SOURCE }
            assertBuilt(action(scenario, "cpp_cmake_build"))
            assertExecuted(scenario, action(scenario, "cpp_run"), "1:1:0")
            scenario.onActivity { (tagged(it, "cpp_cmake_target") as EditText).setText("objetivo_inexistente") }
            instrumentation.waitForIdleSync()
            assertButtons(scenario, configure = true, build = true, run = false)
            val targetError = action(scenario, "cpp_cmake_build")
            assertFailedWithoutExecution(scenario, targetError, "BUILD", "objetivo_inexistente")
            capture(scenario, fixture, "build-target-does-not-exist", "cpp_status", targetError)
            scenario.onActivity { (tagged(it, "cpp_cmake_target") as EditText).setText("consulta") }
            assertBuilt(action(scenario, "cpp_cmake_build"))
            val recovered = action(scenario, "cpp_run")
            assertExecuted(scenario, recovered, "1:1:0")
            capture(scenario, fixture, "corrected-rebuilt-new-observation", "cpp_stdout", recovered)
        }
    }

    @Test fun filesOptionsTargetsAndIndependentDraftsSurvivePageChangesRotationAndRecreation() {
        withFixture(fixture(twoPages = true)) { scenario, fixture ->
            val changedSource = MAIN_SOURCE + "\n// First-page independent source draft.\n"
            editFile(scenario, "main.cpp") { changedSource }
            editFile(scenario, "existencias.h") { it + "\n// First-page interface draft.\n" }
            createFile(scenario, "notes/observaciones.txt", "Objetivo consulta\nComprobar la igualdad del limite.\n")
            scenario.onActivity { activity ->
                (tagged(activity, "cpp_cmake_options") as EditText).setText("USAR_COMPARACION_ESTRICTA=ON")
                selectBuildType(activity, "Release")
                (tagged(activity, "cpp_cmake_target") as EditText).setText("consulta")
                input(activity).setText("entrada independiente\n")
                tagged(activity, "cpp_file_CMakeLists.txt").performClick()
                editor(activity).setSelection(8)
            }
            assertEquals(0, action(scenario, "cpp_cmake_configure").exitCode)
            scenario.onActivity { showPage(it, fixture, 1) }
            awaitLayout(scenario, fixture, 1)
            scenario.onActivity { activity ->
                assertEquals("The second lab starts from its own options", "USAR_COMPARACION_ESTRICTA=OFF",
                    (tagged(activity, "cpp_cmake_options") as EditText).text.toString())
                assertEquals("Debug", selectedBuildType(activity))
                assertEquals(MAIN_SOURCE, readFile(activity, "main.cpp"))
                editor(activity).setText(MAIN_SOURCE + "\n// Second-page draft.\n")
                input(activity).setText("segunda entrada")
                showPage(activity, fixture, 0)
            }
            awaitLayout(scenario, fixture, 0)
            assertFirstDraft(scenario, changedSource)
            orient(scenario, fixture, Configuration.ORIENTATION_LANDSCAPE)
            assertFirstDraft(scenario, changedSource)
            scenario.recreate()
            awaitLayout(scenario, fixture, 0)
            assertFirstDraft(scenario, changedSource)
            orient(scenario, fixture, Configuration.ORIENTATION_PORTRAIT)
            assertFirstDraft(scenario, changedSource)
            val result = configureBuildRun(scenario, "1:0:0")
            capture(scenario, fixture, "restored-first-project-options-and-behavior", "cpp_stdout", result)
            scenario.onActivity { showPage(it, fixture, 1) }
            awaitLayout(scenario, fixture, 1)
            scenario.onActivity { activity ->
                assertEquals(MAIN_SOURCE + "\n// Second-page draft.\n", readFile(activity, "main.cpp"))
                assertEquals("segunda entrada", input(activity).text.toString())
                assertEquals("USAR_COMPARACION_ESTRICTA=OFF", (tagged(activity, "cpp_cmake_options") as EditText).text.toString())
                assertEquals("Debug", selectedBuildType(activity))
                assertEquals(fixture.id, field(activity, "currentLessonId"))
            }
        }
    }

    @Test fun nativeEngineRefusesAnArtifactThatDoesNotMatchTheRequestedSnapshot() {
        val session = "qa-engine-${UUID.randomUUID()}"
        val engine = CppCmakeEngine(context, session)
        val project = CppCmakeProject(FILES, "consulta", mapOf("USAR_COMPARACION_ESTRICTA" to "OFF"))
        val configure = engine.configure(project)
        assertEquals(configure.stdout + configure.stderr, 0, configure.exitCode)
        assertRecordsUseRealCmake(configure)
        assertBuilt(engine.build(project))
        val first = engine.run(project, "")
        assertEquals("1:1:0", requireNotNull(first.execution).stdout)
        assertEquals(0, requireNotNull(first.execution).exitCode)
        val changed = CppCmakeProject(FILES + ("existencias.cpp" to INVENTORY_SOURCE.replace("disponibles <= minimo", "disponibles < minimo")),
            "consulta", mapOf("USAR_COMPARACION_ESTRICTA" to "OFF"))
        val rejected = engine.run(changed, "")
        assertNotEquals("An API caller cannot bypass the renderer's stale-artifact guard", 0, rejected.exitCode)
        assertNull("The previous executable cannot run for changed files", rejected.execution)
        assertEquals("The rejected attempt must not contain the old program output", "", rejected.stdout)
        assertBuilt(engine.build(changed))
        val fresh = engine.run(changed, "")
        assertEquals("1:0:0", requireNotNull(fresh.execution).stdout)
        assertEquals(0, requireNotNull(fresh.execution).exitCode)
        saveMetadata("native-api-stale-artifact-rejected", resultMetadata(fresh)
            .put("rejectedPhase", rejected.phase.name).put("rejectedExitCode", rejected.exitCode)
            .put("originalFingerprint", first.projectFingerprint).put("changedFingerprint", fresh.projectFingerprint))

        val readsInput = """
            #include "existencias.h"
            #include <iostream>
            int main() {
                int disponibles = 0;
                int minimo = 0;
                if (!(std::cin >> disponibles >> minimo)) {
                    std::cout << "EOF";
                    std::cerr << "sin entrada\n";
                    return 9;
                }
                std::cout << disponibles << ',' << minimo << ':'
                          << debe_reponer(disponibles, minimo);
                std::cerr << "leidos:" << disponibles << '\n';
                return 7;
            }
        """.trimIndent() + "\n"
        val inputProject = CppCmakeProject(changed.files + ("main.cpp" to readsInput), "consulta", changed.options)
        assertBuilt(engine.build(inputProject))
        val executions = JSONArray()
        val cases = listOf(
            listOf("4 5\n", "4,5:1", "leidos:4\n", "7"),
            listOf("5\t5", "5,5:0", "leidos:5\n", "7"),
            listOf("", "EOF", "sin entrada\n", "9"),
            listOf("6\n5\n", "6,5:0", "leidos:6\n", "7")
        )
        for ((preparedInput, expectedOut, expectedError, expectedExit) in cases) {
            val actual = engine.run(inputProject, preparedInput)
            assertEquals("A nonzero program exit belongs to execution, rather than construction", "EXECUTION", actual.phase.name)
            assertEquals(expectedExit.toInt(), actual.exitCode)
            val execution = requireNotNull(actual.execution)
            assertEquals(expectedExit.toInt(), execution.exitCode)
            assertEquals("Each invocation consumes its current prepared input from the beginning", expectedOut, execution.stdout)
            assertEquals("Program stderr stays separate from stdout", expectedError, execution.stderr)
            assertEquals("Program output is not fabricated as a CMake diagnostic", "", actual.stdout)
            assertEquals("Program stderr is not fabricated as a construction diagnostic", "", actual.stderr)
            assertTrue(actual.records.any { record -> record.phase.name == "EXECUTION" &&
                record.exitCode == execution.exitCode && record.stdout == expectedOut && record.stderr == expectedError })
            executions.put(resultMetadata(actual).put("preparedInput", preparedInput)
                .put("programStdout", execution.stdout).put("programStderr", execution.stderr)
                .put("programExitCode", execution.exitCode))
        }
        saveMetadata("native-api-current-stdin-and-nonzero-exits", JSONObject().put("executions", executions))

        val noteContents = "Un archivo auxiliar que CMake no compila.\n"
        val nestedFiles = inputProject.files + ("src/nota.txt" to noteContents)
        val withFolder = CppCmakeProject(nestedFiles, "consulta", inputProject.options)
        val folderConfigured = engine.configure(withFolder)
        assertEquals(folderConfigured.stdout + folderConfigured.stderr, 0, folderConfigured.exitCode)
        assertEquals(noteContents, File(folderConfigured.workspace, "project/src/nota.txt").readText())
        assertBuilt(engine.build(withFolder))
        val fileContents = "La antigua carpeta src es ahora este archivo.\n"
        val withFile = CppCmakeProject((nestedFiles - "src/nota.txt") + ("src" to fileContents), "consulta", inputProject.options)
        assertEquals("The transition does not change the CMake description", withFolder.files.getValue("CMakeLists.txt"),
            withFile.files.getValue("CMakeLists.txt"))
        val fileConfigured = engine.configure(withFile)
        assertEquals("Removing an obsolete nested file releases only its now-empty parent: " +
            fileConfigured.stdout + fileConfigured.stderr, 0, fileConfigured.exitCode)
        assertEquals("GENERATION", fileConfigured.phase.name)
        val formerFolder = File(fileConfigured.workspace, "project/src")
        assertTrue("A former empty directory can become a retained project file", formerFolder.isFile)
        assertEquals(fileContents, formerFolder.readText())
        assertNull("Reconfiguration does not claim the previously built executable", fileConfigured.artifact)
        saveMetadata("native-api-folder-to-file-transition", resultMetadata(fileConfigured)
            .put("folderConfiguration", resultMetadata(folderConfigured))
            .put("oldNestedPath", "src/nota.txt").put("newFilePath", "src").put("newFileContents", fileContents))
    }

    @Test fun staticLibraryUsesRealLlvmToolsAndRemovedOptionReturnsToItsDefault() {
        val staticFiles = FILES + ("CMakeLists.txt" to STATIC_CMAKE_DESCRIPTION)
        withFixture(fixture(files = staticFiles, options = mapOf("USAR_COMPARACION_ESTRICTA" to "ON"))) { scenario, fixture ->
            val configured = action(scenario, "cpp_cmake_configure")
            assertEquals(configured.stdout + configured.stderr, 0, configured.exitCode)
            assertRecordsUseRealCmake(configured)
            val archiver = File(configured.workspace, "bin/llvm-ar")
            val indexer = File(configured.workspace, "bin/llvm-ranlib")
            val installed = File(context.applicationInfo.nativeLibraryDir, "liblgp_llvm_ar.so")
            assertTrue("LLVM's installed Android archiver is executable", installed.isFile && installed.canExecute())
            assertArrayEquals("The tools are native Android ELF rather than shell simulations", byteArrayOf(127, 69, 76, 70),
                installed.inputStream().use { input -> ByteArray(4).also { assertEquals(4, input.read(it)) } })
            assertEquals(installed.canonicalFile, archiver.canonicalFile)
            assertEquals(installed.canonicalFile, indexer.canonicalFile)
            val arVersion = runInstalledTool(archiver, listOf("--version"), configured.workspace)
            val ranlibVersion = runInstalledTool(indexer, listOf("--version"), configured.workspace)
            assertTrue(arVersion, arVersion.contains("LLVM", ignoreCase = true))
            assertTrue(ranlibVersion, ranlibVersion.contains("LLVM", ignoreCase = true))
            val cache = cacheText(configured)
            assertTrue("CMake's actual archive tool is LLVM AR", cache.lineSequence().any {
                it.startsWith("CMAKE_AR:FILEPATH=") && it.endsWith("/llvm-ar") })
            assertTrue("CMake's actual archive indexer is LLVM ranlib", cache.lineSequence().any {
                it.startsWith("CMAKE_RANLIB:FILEPATH=") && it.endsWith("/llvm-ranlib") })
            val rules = configured.workspace.walkTopDown().filter { it.isFile && it.extension == "ninja" }
                .joinToString("\n") { it.readText() }
            assertTrue("Generated static-library rules invoke the real archive tool", rules.contains(archiver.absolutePath))
            assertTrue("Generated static-library rules index the resulting archive", rules.contains(indexer.absolutePath))
            assertBuilt(action(scenario, "cpp_cmake_build"))
            val archive = requireNotNull(configured.workspace.walkTopDown().firstOrNull {
                it.isFile && it.name == "libexistencias.a" })
            assertArrayEquals("CMake generated a real static archive", "!<arch>\n".toByteArray(Charsets.US_ASCII),
                archive.inputStream().use { input -> ByteArray(8).also { assertEquals(8, input.read(it)) } })
            val members = runInstalledTool(archiver, listOf("t", archive.absolutePath), configured.workspace)
            assertTrue("The archive contains its CMake-built translation unit: $members", members.contains("existencias.cpp.o"))
            val onRun = action(scenario, "cpp_run")
            assertExecuted(scenario, onRun, "1:0:0")
            capture(scenario, fixture, "static-library-linked-option-on", "cpp_stdout", onRun)
            archive.copyTo(File(evidenceDirectory(), "static-library-option-on.a"), overwrite = true)
            saveMetadata("static-library-installed-llvm-tools", resultMetadata(onRun)
                .put("llvmArVersion", arVersion).put("llvmRanlibVersion", ranlibVersion)
                .put("installedToolSha256", sha(installed.readBytes()))
                .put("archiveSha256", sha(archive.readBytes())).put("archiveMembers", members)
                .put("preservedArchive", "static-library-option-on.a"))

            scenario.onActivity { (tagged(it, "cpp_cmake_options") as EditText).setText("") }
            instrumentation.waitForIdleSync()
            assertButtons(scenario, configure = true, build = false, run = false)
            assertNoOldOutput(scenario)
            val fresh = action(scenario, "cpp_cmake_configure")
            assertEquals(fresh.stdout + fresh.stderr, 0, fresh.exitCode)
            assertTrue("CMake itself replaces the old cache", fresh.records.any { it.command.contains("--fresh") })
            assertFalse("A removed option is absent from the new invocation", fresh.records.any { record ->
                record.command.any { it.startsWith("-DUSAR_COMPARACION_ESTRICTA=") } })
            assertTrue("The previous ON cache entry must not survive removal", cacheText(fresh)
                .contains("USAR_COMPARACION_ESTRICTA:BOOL=OFF"))
            assertBuilt(action(scenario, "cpp_cmake_build"))
            val defaultRun = action(scenario, "cpp_run")
            assertExecuted(scenario, defaultRun, "1:1:0")
            assertNotEquals(onRun.projectFingerprint, defaultRun.projectFingerprint)
            capture(scenario, fixture, "removed-option-fresh-cache-default-off", "cpp_stdout", defaultRun)
            archive.copyTo(File(evidenceDirectory(), "static-library-default-off.a"), overwrite = true)

            scenario.onActivity { (tagged(it, "cpp_cmake_target") as EditText).setText("existencias") }
            instrumentation.waitForIdleSync()
            assertButtons(scenario, configure = true, build = true, run = false)
            val library = action(scenario, "cpp_cmake_build")
            assertEquals(library.stdout + library.stderr, 0, library.exitCode)
            assertEquals("BUILD", library.phase.name)
            assertNull("A successful library target is not an executable WASI artifact", library.artifact)
            assertNull("Building a library does not run an earlier executable", library.execution)
            assertButtons(scenario, configure = true, build = true, run = false)
            assertNoOldOutput(scenario)
            scenario.onActivity { activity ->
                assertEquals("A successful library build explains that this target has no runnable program",
                    activity.getString(R.string.cpp_cmake_built_without_program), text(activity, "cpp_status"))
            }
            capture(scenario, fixture, "static-library-target-built-not-executable", "cpp_status", library)
            val engine = CppCmakeEngine(context, "$ROUTE/${fixture.id}/0/0")
            val noExecution = engine.run(CppCmakeProject(staticFiles, "existencias"), "")
            assertEquals("EXECUTION", noExecution.phase.name)
            assertNotEquals("A library cannot be treated as the prior consulta executable", 0, noExecution.exitCode)
            assertNull(noExecution.execution)
            assertEquals("", noExecution.stdout)
            assertTrue("The API explains why the library cannot be executed", noExecution.stderr.isNotBlank())
            saveMetadata("static-library-run-rejected", resultMetadata(noExecution))
        }
    }

    @Test fun recreationDuringRealConfigureAndBuildPreservesNewDraftAndReceivesCompletion() {
        val pausedDescription = CMAKE_DESCRIPTION + """

            file(WRITE "${'$'}{CMAKE_BINARY_DIR}/qa-configure-active.txt" "configure pause reached\n")
            execute_process(COMMAND "${'$'}{CMAKE_COMMAND}" -E sleep 2)
            add_custom_command(TARGET consulta POST_BUILD
                COMMAND "${'$'}{CMAKE_COMMAND}" -E touch "${'$'}{CMAKE_BINARY_DIR}/qa-build-active.txt"
                COMMAND "${'$'}{CMAKE_COMMAND}" -E sleep 2)
        """.trimIndent() + "\n"
        val pausedFiles = FILES + ("CMakeLists.txt" to pausedDescription)
        withFixture(fixture(files = pausedFiles)) { scenario, fixture ->
            val engine = CppCmakeEngine(context, "$ROUTE/${fixture.id}/0/0")
            val completed = AtomicReference<CppCmakeResult?>(null)
            val observation = engine.observe { result -> if (result != null) completed.set(result) }
            val lifecycle = JSONArray()
            try {
                val configuredDraft = MAIN_SOURCE + "\n// Edited in the new Activity during Configure.\n"
                beginPausedAction(scenario, engine, "cpp_cmake_configure", "qa-configure-active.txt", completed)
                var configureActivity: LessonActivity? = null
                scenario.onActivity { configureActivity = it }
                scenario.recreate()
                awaitLayout(scenario, fixture, 0)
                scenario.onActivity { activity ->
                    assertTrue("The previous Activity is destroyed while native configuration is active", requireNotNull(configureActivity).isDestroyed)
                    assertTrue("The real two-second configure pause is still active after recreation", engine.isBusy())
                    assertAllPhaseButtonsBusy(activity)
                    assertEquals("main.cpp", editor(activity).contentDescription.toString())
                    editor(activity).setText(configuredDraft)
                    editor(activity).setSelection(13)
                    lifecycle.put(JSONObject().put("phase", "CONFIGURATION").put("oldActivityDestroyed", true)
                        .put("sessionBusyAfterRecreation", engine.isBusy()).put("newSourceSha256", sha(configuredDraft)).put("cursor", 13))
                }
                awaitSessionControls(scenario, engine, build = true, run = false)
                assertDraftWithoutMutatingFields(scenario, configuredDraft, 13)
                assertNoOldOutput(scenario)
                capture(scenario, fixture, "recreated-draft-preserved-after-old-configure", "cpp_status", requireNotNull(completed.get()))
                scenario.recreate()
                awaitLayout(scenario, fixture, 0)
                assertDraftWithoutMutatingFields(scenario, configuredDraft, 13)
                awaitSessionControls(scenario, engine, build = true, run = false)

                val builtDraft = configuredDraft + "// Edited in the new Activity during Build.\n"
                beginPausedAction(scenario, engine, "cpp_cmake_build", "qa-build-active.txt", completed)
                var buildActivity: LessonActivity? = null
                scenario.onActivity { buildActivity = it }
                scenario.recreate()
                awaitLayout(scenario, fixture, 0)
                scenario.onActivity { activity ->
                    assertTrue("The previous Activity is destroyed while native building is active", requireNotNull(buildActivity).isDestroyed)
                    assertTrue("The real post-build pause is still active after recreation", engine.isBusy())
                    assertAllPhaseButtonsBusy(activity)
                    editor(activity).setText(builtDraft)
                    editor(activity).setSelection(18)
                    lifecycle.put(JSONObject().put("phase", "BUILD").put("oldActivityDestroyed", true)
                        .put("sessionBusyAfterRecreation", engine.isBusy()).put("newSourceSha256", sha(builtDraft)).put("cursor", 18))
                }
                awaitSessionControls(scenario, engine, build = true, run = false)
                assertDraftWithoutMutatingFields(scenario, builtDraft, 18)
                assertNoOldOutput(scenario)
                scenario.onActivity { assertNull("The old project's successful build is withheld from the edited draft",
                    tagged(it, "cpp_status").getTag(R.id.cpp_lab_cmake_result)) }
                capture(scenario, fixture, "recreated-draft-preserved-after-old-build", "cpp_status", requireNotNull(completed.get()))
                scenario.recreate()
                awaitLayout(scenario, fixture, 0)
                assertDraftWithoutMutatingFields(scenario, builtDraft, 18)
                awaitSessionControls(scenario, engine, build = true, run = false)

                beginPausedAction(scenario, engine, "cpp_cmake_configure", "qa-configure-active.txt", completed)
                scenario.recreate()
                awaitLayout(scenario, fixture, 0)
                scenario.onActivity {
                    assertTrue("Configuration is genuinely active in the recreated unedited view", engine.isBusy())
                    assertAllPhaseButtonsBusy(it)
                }
                awaitSessionControls(scenario, engine, build = true, run = false)
                assertDraftWithoutMutatingFields(scenario, builtDraft, 18)
                val configured = visibleResult(scenario)
                assertEquals("The recreated view receives native configuration completion without touching a field", "GENERATION", configured.phase.name)
                assertEquals(0, configured.exitCode)
                assertNoOldOutput(scenario)
                capture(scenario, fixture, "recreated-unchanged-configure-receives-completion", "cpp_status", configured)

                beginPausedAction(scenario, engine, "cpp_cmake_build", "qa-build-active.txt", completed)
                scenario.recreate()
                awaitLayout(scenario, fixture, 0)
                scenario.onActivity {
                    assertTrue("Build is genuinely active in the recreated unedited view", engine.isBusy())
                    assertAllPhaseButtonsBusy(it)
                }
                awaitSessionControls(scenario, engine, build = true, run = true)
                assertDraftWithoutMutatingFields(scenario, builtDraft, 18)
                val built = visibleResult(scenario)
                assertBuilt(built)
                scenario.onActivity {
                    assertEquals("Native build completion has not executed the program", "", text(it, "cpp_stdout"))
                    assertEquals("", text(it, "cpp_stderr"))
                }
                capture(scenario, fixture, "recreated-unchanged-build-enables-run", "cpp_status", built)
                val executed = action(scenario, "cpp_run")
                assertExecuted(scenario, executed, "1:1:0")
                capture(scenario, fixture, "recreated-project-executes-current-artifact", "cpp_stdout", executed)
                saveMetadata("native-inflight-recreation", resultMetadata(executed)
                    .put("editedRecreations", lifecycle).put("sourceAfterAllRecreations", builtDraft)
                    .put("cursorAfterAllRecreations", 18).put("unchangedViewsReceivedCompletion", true))
            } finally { observation.close() }
        }
    }

    @Test fun cachedPageReceivesPhaseCompletionAfterNavigatingAwayWithoutEditingFields() {
        val pausedDescription = CMAKE_DESCRIPTION + """

            file(WRITE "${'$'}{CMAKE_BINARY_DIR}/qa-configure-active.txt" "configure pause reached\n")
            execute_process(COMMAND "${'$'}{CMAKE_COMMAND}" -E sleep 2)
            add_custom_command(TARGET consulta POST_BUILD
                COMMAND "${'$'}{CMAKE_COMMAND}" -E touch "${'$'}{CMAKE_BINARY_DIR}/qa-build-active.txt"
                COMMAND "${'$'}{CMAKE_COMMAND}" -E sleep 2)
        """.trimIndent() + "\n"
        withFixture(fixture(twoPages = true, files = FILES + ("CMakeLists.txt" to pausedDescription))) { scenario, fixture ->
            val engine = CppCmakeEngine(context, "$ROUTE/${fixture.id}/0/0")
            val completed = AtomicReference<CppCmakeResult?>(null)
            val observation = engine.observe { result -> if (result != null) completed.set(result) }
            var originalConfigure: View? = null
            var originalEditor: EditText? = null
            scenario.onActivity {
                originalConfigure = tagged(it, "cpp_cmake_configure")
                originalEditor = editor(it)
            }
            try {
                beginPausedAction(scenario, engine, "cpp_cmake_configure", "qa-configure-active.txt", completed)
                scenario.onActivity { showPage(it, fixture, 1) }
                awaitLayout(scenario, fixture, 1)
                scenario.onActivity { activity ->
                    assertTrue("The native first-page phase continues after leaving its view", engine.isBusy())
                    assertFalse(requireNotNull(originalConfigure).isAttachedToWindow)
                    assertNotSame("The second page has independent controls", originalConfigure, tagged(activity, "cpp_cmake_configure"))
                }
                awaitSessionIdle(engine, completed)
                assertButtons(scenario, configure = true, build = false, run = false)
                scenario.onActivity { showPage(it, fixture, 0) }
                awaitLayout(scenario, fixture, 0)
                scenario.onActivity {
                    assertSame("Return reuses the cached controller, rather than rebuilding it", originalConfigure, tagged(it, "cpp_cmake_configure"))
                    assertSame(originalEditor, editor(it))
                }
                awaitSessionControls(scenario, engine, build = true, run = false)
                val configured = visibleResult(scenario)
                assertEquals("GENERATION", configured.phase.name)
                assertEquals(0, configured.exitCode)
                capture(scenario, fixture, "cached-page-configure-completion-after-return", "cpp_status", configured)

                beginPausedAction(scenario, engine, "cpp_cmake_build", "qa-build-active.txt", completed)
                scenario.onActivity { showPage(it, fixture, 1) }
                awaitLayout(scenario, fixture, 1)
                scenario.onActivity {
                    assertTrue("Native build is still active outside the first page", engine.isBusy())
                    assertFalse(requireNotNull(originalEditor).isAttachedToWindow)
                }
                awaitSessionIdle(engine, completed)
                assertButtons(scenario, configure = true, build = false, run = false)
                scenario.onActivity { showPage(it, fixture, 0) }
                awaitLayout(scenario, fixture, 0)
                scenario.onActivity {
                    assertSame(originalConfigure, tagged(it, "cpp_cmake_configure"))
                    assertSame(originalEditor, editor(it))
                }
                awaitSessionControls(scenario, engine, build = true, run = true)
                val built = visibleResult(scenario)
                assertBuilt(built)
                scenario.onActivity {
                    assertEquals("The cached controller's completed build has not run the program", "", text(it, "cpp_stdout"))
                    assertEquals("", text(it, "cpp_stderr"))
                    assertEquals(MAIN_SOURCE, editor(it).text.toString())
                }
                capture(scenario, fixture, "cached-page-build-completion-after-return", "cpp_status", built)
                val executed = action(scenario, "cpp_run")
                assertExecuted(scenario, executed, "1:1:0")
                capture(scenario, fixture, "cached-page-current-artifact-executes", "cpp_stdout", executed)
                saveMetadata("native-cached-controller-phase-replay", resultMetadata(executed)
                    .put("sameControlsAfterReturn", true).put("sameEditorAfterReturn", true)
                    .put("waitedOutsideDuringConfigureAndBuild", true).put("fieldsTouchedToUnblockControls", false))
            } finally { observation.close() }
        }
    }

    private data class Fixture(val id: String, val block: JSONObject, val lesson: JSONObject)
    private fun fixture(twoPages: Boolean = false, files: Map<String, String> = FILES,
                        options: Map<String, String> = mapOf("USAR_COMPARACION_ESTRICTA" to "OFF")): Fixture {
        val id = "programming_practice_tools__construccion_reproducible__cmake_native_qa_${UUID.randomUUID().toString().replace("-", "")}"
        val block = JSONObject().put("type", "cpp_compiler").put("initialCode", "")
            .put("initialStandardInput", "")
            .put("projectFiles", JSONArray().apply { files.forEach { (path, source) ->
                put(JSONObject().put("path", path).put("initialCode", source))
            } })
            .put("cmake", JSONObject().put("target", "consulta").put("buildType", "Debug")
                .put("options", JSONObject(options)))
        val pages = JSONArray().put(JSONObject().put("blocks", JSONArray().put(JSONObject(block.toString()))))
        if (twoPages) pages.put(JSONObject().put("blocks", JSONArray().put(JSONObject(block.toString()))))
        return Fixture(id, block, JSONObject().put("id", id).put("title", "Verificación técnica nativa de CMake")
            .put("pages", pages))
    }

    private fun withFixture(fixture: Fixture, check: (ActivityScenario<LessonActivity>, Fixture) -> Unit) {
        val original = fixture.block.toString()
        ProgressRepository.saveCurrentPage(context, ROUTE, SOURCE_LESSON, 0)
        val intent = Intent(context, LessonActivity::class.java).putExtra("routeId", ROUTE)
            .putExtra("courseId", "cpp_fundamentals").putExtra("unitId", "objetos_y_estado")
            .putExtra("lessonId", SOURCE_LESSON).putExtra("lessonNumber", 1).putExtra("lessonTotal", 1)
        ActivityScenario.launch<LessonActivity>(intent).use { scenario ->
            scenario.onActivity {
                assertSourceLessonLoadedBeforeFixtureInjection(it)
                inject(it, fixture, 0)
            }
            awaitLayout(scenario, fixture, 0)
            val curricularProgress = context.getSharedPreferences("learning_progress", Context.MODE_PRIVATE).all
                .filterKeys { it.startsWith("exercise_") || it.endsWith("_completed") }
            check(scenario, fixture)
            val current = context.getSharedPreferences("learning_progress", Context.MODE_PRIVATE).all
                .filterKeys { it.startsWith("exercise_") || it.endsWith("_completed") }
            curricularProgress.forEach { (key, originalValue) ->
                assertTrue("An existing evaluation/completion key must remain present: $key", current.containsKey(key))
                assertEquals("An existing evaluation/completion value must remain identical: $key", originalValue, current[key])
            }
            // The normal renderer initializes these specific completion flags to false when
            // first navigating the exclusive in-memory fixture. That is not course completion.
            val allowedNewFalseFlags = setOf("lesson_${fixture.id}_completed",
                "unit_programming_practice_tools__construccion_reproducible_completed",
                "course_programming_practice_tools_completed")
            val additions = current.keys - curricularProgress.keys
            additions.forEach { key ->
                assertTrue("Only this QA fixture's known false completion initialization is allowed: $key", key in allowedNewFalseFlags)
                assertEquals("A technical lab can never create a true completion or an exercise evaluation: $key", false, current[key])
            }
            saveMetadata("progress-preserved-${fixture.id.substringAfterLast("__")}", JSONObject()
                .put("fixtureId", fixture.id).put("existingKeysVerified", curricularProgress.size)
                .put("existingValuesSha256", sha(JSONObject(curricularProgress.toSortedMap()).toString()))
                .put("newFalseCompletionKeys", JSONArray(additions.sorted())))
        }
        assertEquals("Technical rendering never changes the authored block", original, fixture.block.toString())
    }

    private fun inject(activity: LessonActivity, fixture: Fixture, page: Int) {
        require(activity !in fixtureActivities) { "A fixture may be injected only once per Activity; never mask later view loss." }
        setField(activity, "currentLessonId", fixture.id)
        setField(activity, "lessonJson", JSONObject(fixture.lesson.toString()))
        setField(activity, "totalPages", fixture.lesson.getJSONArray("pages").length())
        setField(activity, "currentPage", page)
        listOf("pageViewCache", "pageRequiredExercises", "pageCompletedExercises").forEach { name ->
            (field(activity, name) as MutableMap<*, *>).clear()
        }
        invoke(activity, "renderCurrentPage")
        fixtureActivities[activity] = fixture.id
        assertEquals("A technical compiler fixture is not an evaluated exercise", 0, field(activity, "requiredExercises"))
    }
    private fun assertSourceLessonLoadedBeforeFixtureInjection(activity: LessonActivity) {
        assertEquals("onCreate synchronously finishes loading the source lesson before the QA injection", SOURCE_LESSON, field(activity, "currentLessonId"))
        assertEquals(SOURCE_LESSON, (field(activity, "lessonJson") as JSONObject).getString("id"))
        assertTrue("The synchronous source renderer has created its content", content(activity).childCount > 0)
    }
    private fun showPage(activity: LessonActivity, fixture: Fixture, page: Int) {
        invoke(activity, "saveCurrentPageState")
        setField(activity, "currentPage", page)
        ProgressRepository.saveCurrentPage(context, ROUTE, fixture.id, page)
        invoke(activity, "renderCurrentPage")
    }
    private fun awaitLayout(scenario: ActivityScenario<LessonActivity>, fixture: Fixture, page: Int, orientation: Int? = null) {
        val deadline = SystemClock.uptimeMillis() + 10_000L
        while (SystemClock.uptimeMillis() < deadline) {
            instrumentation.waitForIdleSync()
            var ready = false
            scenario.onActivity { activity ->
                if (activity !in fixtureActivities) {
                    assertSourceLessonLoadedBeforeFixtureInjection(activity)
                    inject(activity, fixture, page)
                }
                assertEquals("A later event must not replace the injected lesson identity", fixture.id, field(activity, "currentLessonId"))
                assertEquals("A later event must not replace the in-memory lesson", fixture.id, (field(activity, "lessonJson") as JSONObject).getString("id"))
                ready = (orientation == null || activity.resources.configuration.orientation == orientation) &&
                    field(activity, "currentPage") == page && tagged(activity, "cpp_cmake_configure").width > 0 &&
                    !content(activity).isLayoutRequested
            }
            if (ready) return
            SystemClock.sleep(50L)
        }
        fail("The real CMake lesson controls did not finish native layout")
    }
    private fun orient(scenario: ActivityScenario<LessonActivity>, fixture: Fixture, orientation: Int) {
        var page = 0
        scenario.onActivity {
            page = field(it, "currentPage") as Int
            hideIme(it)
            it.requestedOrientation = if (orientation == Configuration.ORIENTATION_PORTRAIT)
                ActivityInfo.SCREEN_ORIENTATION_PORTRAIT else ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
        }
        awaitLayout(scenario, fixture, page, orientation)
    }

    private fun action(scenario: ActivityScenario<LessonActivity>, tag: String): CppCmakeResult {
        var completed = 0
        var reserve = 0f
        scenario.onActivity { activity ->
            completed = field(activity, "completedExercises") as Int
            reserve = context.getSharedPreferences(ATTEMPTS, Context.MODE_PRIVATE).getFloat("reserve_$ROUTE", 0f)
            val button = tagged(activity, tag)
            assertTrue("The actual phase action is enabled: $tag", button.isEnabled)
            hideIme(activity)
            button.performClick()
            assertFalse("The actual native phase marks its action busy", button.isEnabled)
        }
        val deadline = SystemClock.uptimeMillis() + 180_000L
        var finished = false
        while (!finished && SystemClock.uptimeMillis() < deadline) {
            instrumentation.waitForIdleSync()
            scenario.onActivity { activity ->
                val status = tagged(activity, "cpp_status")
                finished = tagged(activity, "cpp_cmake_configure").isEnabled &&
                    status.getTag(R.id.cpp_lab_cmake_result) is CppCmakeResult &&
                    status.getTag(R.id.cpp_lab_build_phase)?.toString() != "BUILDING"
            }
            if (!finished) SystemClock.sleep(50L)
        }
        var result: CppCmakeResult? = null
        scenario.onActivity { activity ->
            result = tagged(activity, "cpp_status").getTag(R.id.cpp_lab_cmake_result) as? CppCmakeResult
            assertEquals("A CMake phase cannot complete an exercise", completed, field(activity, "completedExercises"))
            assertTrue("A CMake phase cannot consume attempts", context.getSharedPreferences(ATTEMPTS, Context.MODE_PRIVATE)
                .getFloat("reserve_$ROUTE", 0f) >= reserve)
            if (finished) {
                assertEquals(result?.phase?.name, tagged(activity, "cpp_status").getTag(R.id.cpp_lab_build_phase)?.toString())
                assertEquals(result?.exitCode, tagged(activity, "cpp_status").getTag(R.id.cpp_lab_exit_code))
            }
        }
        assertTrue("CMake phase completes within 180 seconds: $tag, $result", finished)
        return requireNotNull(result)
    }
    private fun beginPausedAction(scenario: ActivityScenario<LessonActivity>, engine: CppCmakeEngine,
                                  tag: String, markerName: String, completed: AtomicReference<CppCmakeResult?>) {
        assertFalse("Only one native operation is active in this fixture session", engine.isBusy())
        val marker = File(engine.workspace, "build/$markerName")
        if (marker.exists()) assertTrue("Remove only the fixture's previous phase marker", marker.delete())
        completed.set(null)
        scenario.onActivity { activity ->
            hideIme(activity)
            val button = tagged(activity, tag)
            assertTrue(button.isEnabled)
            button.performClick()
            assertAllPhaseButtonsBusy(activity)
        }
        val deadline = SystemClock.uptimeMillis() + 60_000L
        while (SystemClock.uptimeMillis() < deadline) {
            if (marker.isFile && engine.isBusy()) return
            val early = completed.get()
            if (early != null) fail("Native phase completed before reaching the fixture's real pause: $early")
            SystemClock.sleep(20L)
        }
        fail("The actual CMake process did not reach its bounded pause: $tag, marker=$marker, result=${completed.get()}")
    }
    private fun awaitSessionControls(scenario: ActivityScenario<LessonActivity>, engine: CppCmakeEngine, build: Boolean, run: Boolean) {
        val deadline = SystemClock.uptimeMillis() + 180_000L
        var diagnostic = ""
        while (SystemClock.uptimeMillis() < deadline) {
            instrumentation.waitForIdleSync()
            var ready = false
            var missing: JSONObject? = null
            scenario.onActivity { activity ->
                val controls = descendants(content(activity))
                val configureButton = controls.singleOrNull { it.tag == "cpp_cmake_configure" }
                val buildButton = controls.singleOrNull { it.tag == "cpp_cmake_build" }
                val runButton = controls.singleOrNull { it.tag == "cpp_run" }
                if (configureButton == null || buildButton == null || runButton == null) {
                    missing = JSONObject().put("currentLessonId", field(activity, "currentLessonId"))
                        .put("loadedLessonId", (field(activity, "lessonJson") as JSONObject).optString("id"))
                        .put("currentPage", field(activity, "currentPage")).put("totalPages", field(activity, "totalPages"))
                        .put("destroyed", activity.isDestroyed).put("finishing", activity.isFinishing)
                        .put("engineBusy", engine.isBusy()).put("contentAttached", content(activity).isAttachedToWindow)
                        .put("tags", JSONArray(controls.mapNotNull { it.tag?.toString() }))
                        .put("cachedPages", JSONArray((field(activity, "pageViewCache") as Map<*, *>).keys.toList()))
                    return@onActivity
                }
                ready = !engine.isBusy() && configureButton.isEnabled && buildButton.isEnabled == build && runButton.isEnabled == run
                diagnostic = "busy=${engine.isBusy()}, configure=${configureButton.isEnabled}, build=${buildButton.isEnabled}, " +
                    "run=${runButton.isEnabled}, status=${text(activity, "cpp_status")}, phase=${tagged(activity, "cpp_status").getTag(R.id.cpp_lab_build_phase)}"
            }
            missing?.let { evidence ->
                val name = "missing-cmake-controls-${SystemClock.uptimeMillis()}"
                saveMetadata(name, evidence)
                instrumentation.uiAutomation.takeScreenshot()?.let { bitmap ->
                    try { File(evidenceDirectory(), "$name.png").outputStream().use {
                        assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) }
                    } finally { bitmap.recycle() }
                }
                fail("CMake controls disappeared after fixture initialization; preserved native evidence $name: $evidence")
            }
            if (ready) return
            SystemClock.sleep(50L)
        }
        fail("An attached recreated view did not receive native phase completion without a field change: $diagnostic")
    }
    private fun awaitSessionIdle(engine: CppCmakeEngine, completed: AtomicReference<CppCmakeResult?>) {
        val deadline = SystemClock.uptimeMillis() + 180_000L
        while (SystemClock.uptimeMillis() < deadline) {
            if (!engine.isBusy() && completed.get() != null) {
                instrumentation.waitForIdleSync()
                return
            }
            SystemClock.sleep(50L)
        }
        fail("The native phase did not finish while its original page was detached: ${completed.get()}")
    }
    private fun assertAllPhaseButtonsBusy(activity: LessonActivity) {
        for (tag in listOf("cpp_cmake_configure", "cpp_cmake_build", "cpp_run"))
            assertFalse("Native session activity disables the actual phase control: $tag", tagged(activity, tag).isEnabled)
    }
    private fun assertDraftWithoutMutatingFields(scenario: ActivityScenario<LessonActivity>, expected: String, cursor: Int) {
        scenario.onActivity {
            assertEquals("main.cpp", editor(it).contentDescription.toString())
            assertEquals("An old Activity callback cannot overwrite the new editor draft", expected, editor(it).text.toString())
            assertEquals("The recreated editor retains the new cursor", cursor, editor(it).selectionStart)
            assertEquals("consulta", (tagged(it, "cpp_cmake_target") as EditText).text.toString())
            assertEquals("USAR_COMPARACION_ESTRICTA=OFF", (tagged(it, "cpp_cmake_options") as EditText).text.toString())
            assertEquals("Debug", selectedBuildType(it))
        }
    }
    private fun visibleResult(scenario: ActivityScenario<LessonActivity>): CppCmakeResult {
        var result: CppCmakeResult? = null
        scenario.onActivity { result = tagged(it, "cpp_status").getTag(R.id.cpp_lab_cmake_result) as? CppCmakeResult }
        return requireNotNull(result) { "The attached recreated view must receive the result of its current project" }
    }
    private fun configureBuildRun(scenario: ActivityScenario<LessonActivity>, expected: String): CppCmakeResult {
        val configure = action(scenario, "cpp_cmake_configure")
        assertEquals(configure.stdout + configure.stderr, 0, configure.exitCode)
        assertBuilt(action(scenario, "cpp_cmake_build"))
        return action(scenario, "cpp_run").also { assertExecuted(scenario, it, expected) }
    }
    private fun assertButtons(scenario: ActivityScenario<LessonActivity>, configure: Boolean, build: Boolean, run: Boolean) {
        instrumentation.waitForIdleSync()
        scenario.onActivity { activity ->
            assertEquals("Configure phase availability", configure, tagged(activity, "cpp_cmake_configure").isEnabled)
            assertEquals("Build phase availability", build, tagged(activity, "cpp_cmake_build").isEnabled)
            assertEquals("Run requires a current artifact", run, tagged(activity, "cpp_run").isEnabled)
        }
    }
    private fun assertBuilt(result: CppCmakeResult) {
        assertEquals(result.stdout + result.stderr, 0, result.exitCode)
        assertEquals("BUILD", result.phase.name)
        assertFalse(result.timedOut)
        val artifact = requireNotNull(result.artifact)
        assertTrue("The real CMake build produces an artifact", artifact.isFile && artifact.length() > 8)
        assertArrayEquals("The resulting program is real WebAssembly for the existing Android runtime",
            byteArrayOf(0, 97, 115, 109), artifact.inputStream().use { stream -> ByteArray(4).also { assertEquals(4, stream.read(it)) } })
        assertNull("The Build action must not run the application", result.execution)
    }
    private fun assertExecuted(scenario: ActivityScenario<LessonActivity>, result: CppCmakeResult, expected: String) {
        assertEquals("EXECUTION", result.phase.name)
        assertEquals(0, result.exitCode)
        assertEquals(expected, requireNotNull(result.execution).stdout)
        assertEquals(0, requireNotNull(result.execution).exitCode)
        assertEquals("", requireNotNull(result.execution).stderr)
        scenario.onActivity { activity ->
            assertEquals("Only program stdout belongs in its output pane", expected, text(activity, "cpp_stdout"))
            assertEquals("", text(activity, "cpp_stderr"))
            assertEquals(result.exitCode, tagged(activity, "cpp_status").getTag(R.id.cpp_lab_exit_code))
        }
    }
    private fun assertFailedWithoutExecution(scenario: ActivityScenario<LessonActivity>, result: CppCmakeResult, phase: String, diagnostic: String) {
        assertEquals(phase, result.phase.name)
        assertNotEquals("This phase has failed", 0, result.exitCode)
        assertTrue(result.stdout + result.stderr, (result.stdout + result.stderr).contains(diagnostic))
        assertNull("Failed phases cannot execute the earlier successful program", result.execution)
        assertNull("An old executable cannot be presented as this phase's result", result.artifact)
        assertNoOldOutput(scenario)
    }
    private fun assertNoOldOutput(scenario: ActivityScenario<LessonActivity>) {
        scenario.onActivity {
            assertEquals("A changed/failed phase cannot display a previous observation", "", text(it, "cpp_stdout"))
            assertEquals("A changed/failed phase cannot display previous program stderr", "", text(it, "cpp_stderr"))
            assertFalse(tagged(it, "cpp_run").isEnabled)
        }
    }
    private fun assertRecordsUseRealCmake(result: CppCmakeResult) {
        assertTrue("The Android result identifies CMake, the generator and compiler", result.toolsInfo.isNotEmpty())
        val identity = result.toolsInfo.entries.joinToString("\n") { "${it.key}=${it.value}" }
        assertTrue(identity, identity.contains("cmake", ignoreCase = true))
        assertTrue("A real CMake invocation configured the isolated source/build directories", result.records.any { record ->
            record.command.any { it == "-S" } && record.command.any { it == "-B" }
        })
        val cache = cacheText(result)
        assertTrue("A real CMake cache records the generator", cache.contains("CMAKE_GENERATOR:INTERNAL="))
        assertTrue("CMake selected a real compiler", cache.contains("CMAKE_CXX_COMPILER:"))
        assertTrue("The CMake project name is not the executable target name", cache.contains("CMAKE_PROJECT_NAME:STATIC=Existencias"))
        assertTrue("CMake generated build rules, rather than the app directly compiling editor .cpp files", result.workspace.walkTopDown()
            .any { it.isFile && it.name in setOf("build.ninja", "Makefile") })
    }
    private fun cacheText(result: CppCmakeResult): String = requireNotNull(result.workspace.walkTopDown()
        .firstOrNull { it.isFile && it.name == "CMakeCache.txt" }).readText()

    private fun editFile(scenario: ActivityScenario<LessonActivity>, path: String, transform: (String) -> String) {
        scenario.onActivity { activity ->
            tagged(activity, "cpp_file_$path").performClick()
            editor(activity).setText(transform(editor(activity).text.toString()))
        }
        instrumentation.waitForIdleSync()
    }
    private fun readFile(activity: LessonActivity, path: String): String {
        tagged(activity, "cpp_file_$path").performClick()
        assertEquals(path, editor(activity).contentDescription.toString())
        return editor(activity).text.toString()
    }
    private fun createFile(scenario: ActivityScenario<LessonActivity>, path: String, source: String) {
        scenario.onActivity { tagged(it, "cpp_add_file").performClick() }
        onView(withTagValue(equalTo("cpp_new_file_name"))).inRoot(isDialog()).perform(replaceText(path), closeSoftKeyboard())
        onView(withId(android.R.id.button1)).inRoot(isDialog()).perform(click())
        scenario.onActivity {
            assertEquals(path, editor(it).contentDescription.toString())
            editor(it).setText(source)
        }
    }
    private fun assertFirstDraft(scenario: ActivityScenario<LessonActivity>, changedSource: String) {
        scenario.onActivity { activity ->
            assertEquals("CMakeLists.txt", editor(activity).contentDescription.toString())
            assertEquals("The selected file cursor survives cache/recreation", 8, editor(activity).selectionStart)
            assertEquals("consulta", (tagged(activity, "cpp_cmake_target") as EditText).text.toString())
            assertEquals("USAR_COMPARACION_ESTRICTA=ON", (tagged(activity, "cpp_cmake_options") as EditText).text.toString())
            assertEquals("Release", selectedBuildType(activity))
            assertEquals("entrada independiente\n", input(activity).text.toString())
            assertEquals(changedSource, readFile(activity, "main.cpp"))
            assertEquals(FILES.getValue("existencias.h") + "\n// First-page interface draft.\n", readFile(activity, "existencias.h"))
            assertEquals("Objetivo consulta\nComprobar la igualdad del limite.\n", readFile(activity, "notes/observaciones.txt"))
            tagged(activity, "cpp_file_CMakeLists.txt").performClick()
            assertEquals(8, editor(activity).selectionStart)
        }
    }
    private fun selectBuildType(activity: LessonActivity, type: String) {
        val spinner = tagged(activity, "cpp_cmake_build_type") as Spinner
        val position = (0 until spinner.count).single { spinner.getItemAtPosition(it).toString() == type }
        spinner.setSelection(position)
    }
    private fun selectedBuildType(activity: LessonActivity) = (tagged(activity, "cpp_cmake_build_type") as Spinner).selectedItem.toString()

    private fun captureDiagnosticSegment(scenario: ActivityScenario<LessonActivity>, fixture: Fixture,
                                         name: String, result: CppCmakeResult) {
        // The status is entirely visible; the following diagnostic may be taller than the
        // viewport. Keep the native full-screen PNG and explicitly identify its visible segment.
        capture(scenario, fixture, name, "cpp_status", result)
        val metadataFile = File(evidenceDirectory(), "$name.json")
        val metadata = JSONObject(metadataFile.readText())
        scenario.onActivity { activity ->
            val diagnostic = evidenceTarget(activity, "cpp_compilation_output") as TextView
            val visible = Rect()
            assertTrue("The first native diagnostic segment is visible below the full status", diagnostic.getGlobalVisibleRect(visible))
            val rootOrigin = IntArray(2)
            diagnostic.rootView.getLocationOnScreen(rootOrigin)
            visible.offset(rootOrigin[0], rootOrigin[1])
            val location = IntArray(2)
            diagnostic.getLocationOnScreen(location)
            val full = Rect(location[0], location[1], location[0] + diagnostic.width, location[1] + diagnostic.height)
            val layout = requireNotNull(diagnostic.layout)
            val firstY = (visible.top - full.top + diagnostic.scrollY - diagnostic.totalPaddingTop).coerceAtLeast(0)
            val lastY = (visible.bottom - full.top + diagnostic.scrollY - diagnostic.totalPaddingTop).coerceAtLeast(0)
            val firstLine = layout.getLineForVertical(firstY)
            var lastFullLine = layout.getLineForVertical(lastY)
            while (lastFullLine >= firstLine && layout.getLineBottom(lastFullLine) > lastY) lastFullLine--
            assertEquals("The screenshot includes the beginning rather than an unidentified middle excerpt", 0, firstLine)
            assertTrue("At least one diagnostic line is completely visible", lastFullLine >= firstLine)
            val fullText = diagnostic.text.toString()
            val segmentEnd = layout.getLineEnd(lastFullLine)
            metadata.put("diagnosticCaptureScope", "visible_first_segment")
                .put("diagnosticFullText", fullText).put("diagnosticFullTextSha256", sha(fullText))
                .put("diagnosticFullViewBounds", JSONArray(listOf(full.left, full.top, full.right, full.bottom)))
                .put("diagnosticVisibleSegmentBounds", JSONArray(listOf(visible.left, visible.top, visible.right, visible.bottom)))
                .put("diagnosticEntireViewVisible", full == visible)
                .put("diagnosticFirstVisibleLine", firstLine).put("diagnosticLastFullyVisibleLine", lastFullLine)
                .put("diagnosticFullLineCount", layout.lineCount).put("diagnosticVisibleSegmentEndOffset", segmentEnd)
                .put("diagnosticVisibleSegmentText", fullText.substring(0, segmentEnd))
                .put("diagnosticBoundsCoordinateSpace", "displayPixels")
        }
        metadataFile.writeText(metadata.toString(2))
    }

    private fun capture(scenario: ActivityScenario<LessonActivity>, fixture: Fixture, name: String, tag: String, result: CppCmakeResult) {
        scenario.onActivity { activity ->
            hideIme(activity)
            activity.currentFocus?.clearFocus()
            val target = evidenceTarget(activity, tag)
            val bounds = Rect()
            target.getDrawingRect(bounds)
            val scroll = activity.findViewById<ScrollView>(R.id.lessonScroll)
            scroll.offsetDescendantRectToMyCoords(target, bounds)
            scroll.scrollTo(0, (bounds.top - 12 * activity.resources.displayMetrics.density).toInt().coerceAtLeast(0))
        }
        instrumentation.waitForIdleSync()
        var previousCrop: Bitmap? = null
        var current: Bitmap? = null
        var rect = Rect()
        var consecutive = 0
        var samples = 0
        val deadline = SystemClock.uptimeMillis() + 8_000L
        try {
            while (SystemClock.uptimeMillis() < deadline) {
                scenario.onActivity { activity ->
                    val target = evidenceTarget(activity, tag)
                    assertTrue("The evidence target has real Android window focus", target.hasWindowFocus())
                    assertTrue(target.getGlobalVisibleRect(rect))
                    assertEquals("The entire evidence target width is visible", target.width, rect.width())
                    assertEquals("The entire evidence target height is visible", target.height, rect.height())
                    val origin = IntArray(2)
                    target.rootView.getLocationOnScreen(origin)
                    rect.offset(origin[0], origin[1])
                }
                val screen = requireNotNull(instrumentation.uiAutomation.takeScreenshot())
                assertTrue(rect.left >= 0 && rect.top >= 0 && rect.right <= screen.width && rect.bottom <= screen.height)
                val crop = Bitmap.createBitmap(screen, rect.left, rect.top, rect.width(), rect.height())
                consecutive = if (previousCrop?.sameAs(crop) == true) consecutive + 1 else 1
                previousCrop?.recycle()
                previousCrop = crop
                current?.recycle()
                current = screen
                samples++
                if (consecutive >= 4) break
                SystemClock.sleep(250L)
            }
            assertTrue("Four real native frames are stable before recording evidence", consecutive >= 4)
            File(evidenceDirectory(), "$name.png").outputStream().use {
                assertTrue(requireNotNull(current).compress(Bitmap.CompressFormat.PNG, 100, it))
            }
            val metadata = resultMetadata(result).put("lessonId", fixture.id).put("authoredBlockSha256", sha(fixture.block.toString()))
                .put("targetTag", tag).put("visibleTargetTag", if (tag in RAW_CONSOLE_TAGS) "cpp_console_display" else tag)
                .put("boundsCoordinateSpace", "displayPixels")
                .put("nativeBounds", JSONArray(listOf(rect.left, rect.top, rect.right, rect.bottom)))
                .put("stableConsecutiveFrames", consecutive).put("sampledFrames", samples).put("samplingIntervalMillis", 250)
            scenario.onActivity { activity -> metadata.put("status", text(activity, "cpp_status"))
                .put("nativePhase", tagged(activity, "cpp_status").getTag(R.id.cpp_lab_build_phase)?.toString() ?: JSONObject.NULL)
                .put("visibleResultFingerprint", (tagged(activity, "cpp_status").getTag(R.id.cpp_lab_cmake_result) as? CppCmakeResult)?.projectFingerprint ?: JSONObject.NULL)
                .put("editorSelectedFile", editor(activity).contentDescription.toString())
                .put("editorCursor", editor(activity).selectionStart).put("editorSourceSha256", sha(editor(activity).text.toString()))
                .put("programStdout", text(activity, "cpp_stdout")).put("programStderr", text(activity, "cpp_stderr"))
                .put("options", (tagged(activity, "cpp_cmake_options") as EditText).text.toString())
                .put("target", (tagged(activity, "cpp_cmake_target") as EditText).text.toString())
                .put("buildType", selectedBuildType(activity)).put("pageIndex", field(activity, "currentPage"))
                .put("projectDraft", context.getSharedPreferences("cpp_lesson_drafts", Context.MODE_PRIVATE)
                    .getString("$ROUTE/${fixture.id}/${field(activity, "currentPage")}/0/project", null)
                    ?.let(::JSONObject) ?: JSONObject.NULL) }
            // Preserve the actual generated evidence now, before a later configure replaces it.
            val nativeFiles = JSONArray()
            val generatedDirectory = File(evidenceDirectory(), "$name-native-build").apply { mkdirs() }
            result.workspace.walkTopDown().filter { file -> file.isFile &&
                (file.name in setOf("CMakeCache.txt", "build.ninja", "rules.ninja", "Makefile", "compile_commands.json") ||
                    file.extension == "json" && file.invariantSeparatorsPath.contains("/.cmake/api/v1/reply/") ||
                    file.name.startsWith("toolchain") && file.name.endsWith(".cmake")) }.forEach { file ->
                val relative = file.relativeTo(result.workspace).invariantSeparatorsPath
                val copied = File(generatedDirectory, relative)
                requireNotNull(copied.parentFile).mkdirs()
                file.copyTo(copied, overwrite = true)
                nativeFiles.put(JSONObject().put("workspaceRelativePath", relative)
                    .put("evidenceRelativePath", "$name-native-build/$relative").put("sha256", sha(copied.readBytes())))
            }
            metadata.put("preservedGeneratedFiles", nativeFiles)
            saveMetadata(name, metadata)
        } finally { previousCrop?.recycle(); current?.recycle() }
    }

    /** Raw channel providers stay GONE; screenshots use the one real displayed console. */
    private fun evidenceTarget(activity: LessonActivity, tag: String): View {
        val channel = when (tag) {
            "cpp_compilation_output" -> CppLabPresentationPolicy.Channel.DIAGNOSTICS
            "cpp_stdout" -> CppLabPresentationPolicy.Channel.STDOUT
            "cpp_stderr" -> CppLabPresentationPolicy.Channel.STDERR
            else -> return tagged(activity, tag)
        }
        val console = descendants(content(activity)).filterIsInstance<CppConsoleView>().single()
        console.selectChannel(channel)
        assertEquals("Raw channel remains available without occupying another output pane", View.GONE, tagged(activity, tag).visibility)
        assertEquals((tagged(activity, tag) as TextView).text.toString(), console.display.text.toString())
        return console.display
    }
    private fun resultMetadata(result: CppCmakeResult) = JSONObject().put("phase", result.phase.name)
        .put("exitCode", result.exitCode).put("timedOut", result.timedOut).put("stdout", result.stdout).put("stderr", result.stderr)
        .put("projectFingerprint", result.projectFingerprint).put("tools", JSONObject(result.toolsInfo))
        .put("workspace", result.workspace.absolutePath).put("artifact", result.artifact?.absolutePath ?: JSONObject.NULL)
        .put("artifactSha256", result.artifact?.takeIf(File::isFile)?.let { sha(it.readBytes()) } ?: JSONObject.NULL)
        .put("records", JSONArray().apply { result.records.forEach { record -> put(JSONObject()
            .put("phase", record.phase.name).put("exitCode", record.exitCode).put("stdout", record.stdout)
            .put("stderr", record.stderr).put("command", JSONArray(record.command))) } })
    private fun saveMetadata(name: String, metadata: JSONObject) = File(evidenceDirectory(), "$name.json").writeText(metadata.toString(2))
    private fun runInstalledTool(tool: File, arguments: List<String>, directory: File): String {
        val process = ProcessBuilder(listOf(tool.absolutePath) + arguments).directory(directory).apply {
            environment()["LD_LIBRARY_PATH"] = context.applicationInfo.nativeLibraryDir
        }.start()
        val finished = process.waitFor(10L, TimeUnit.SECONDS)
        if (!finished) process.destroyForcibly()
        assertTrue("Installed LLVM tool completes: ${tool.name} $arguments", finished)
        val stdout = process.inputStream.bufferedReader().use { it.readText() }
        val stderr = process.errorStream.bufferedReader().use { it.readText() }
        assertEquals("Installed LLVM tool succeeds: $stderr", 0, process.exitValue())
        return stdout + stderr
    }
    private fun evidenceDirectory() = File(requireNotNull(context.getExternalFilesDir(null)), "cmake-native-qa").apply { mkdirs() }
    private fun sha(source: String) = sha(source.toByteArray(Charsets.UTF_8))
    private fun sha(bytes: ByteArray) = MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it) }
    private fun hideIme(activity: LessonActivity) = (activity.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager)
        .hideSoftInputFromWindow(activity.window.decorView.windowToken, 0)
    private fun content(activity: LessonActivity): ViewGroup = activity.findViewById(R.id.lessonContent)
    private fun descendants(view: View): List<View> = listOf(view) + if (view is ViewGroup)
        (0 until view.childCount).flatMap { descendants(view.getChildAt(it)) } else emptyList()
    private fun tagged(activity: LessonActivity, tag: String) = descendants(content(activity)).singleOrNull { it.tag == tag }
        ?: error("Missing native tag $tag: currentLesson=${field(activity, "currentLessonId")}, " +
            "loadedLesson=${(field(activity, "lessonJson") as JSONObject).optString("id")}, page=${field(activity, "currentPage")}, " +
            "tags=${descendants(content(activity)).mapNotNull { it.tag?.toString() }}")
    private fun editor(activity: LessonActivity) = tagged(activity, "cpp_code_input") as EditText
    private fun input(activity: LessonActivity) = tagged(activity, "cpp_input") as EditText
    private fun text(activity: LessonActivity, tag: String) = (tagged(activity, tag) as TextView).text.toString()
    private fun field(activity: LessonActivity, name: String): Any? = LessonActivity::class.java.getDeclaredField(name).apply { isAccessible = true }.get(activity)
    private fun setField(activity: LessonActivity, name: String, value: Any) = LessonActivity::class.java.getDeclaredField(name).apply { isAccessible = true }.set(activity, value)
    private fun invoke(activity: LessonActivity, name: String) = LessonActivity::class.java.getDeclaredMethod(name).apply { isAccessible = true }.invoke(activity)
    private fun preferenceFiles() = File(context.applicationInfo.dataDir, "shared_prefs").listFiles().orEmpty()
        .filter { it.name.endsWith(".xml") }.map { it.name.removeSuffix(".xml") }.toSet()

    companion object {
        private val QA_PACKAGES = setOf("com.learngraphicsprogramming.app.curricularqa", "com.learngraphicsprogramming.app.realdownloadsqa")
        private val RAW_CONSOLE_TAGS = setOf("cpp_compilation_output", "cpp_stdout", "cpp_stderr")
        private const val ROUTE = "programming"
        private const val ATTEMPTS = "attempt_reserve_seconds_test"
        private const val SOURCE_LESSON = "cpp_fundamentals__objetos_y_estado__leer_una_entrada_conocida"
        private val KNOWN_PREFERENCES = setOf("cpp_lesson_drafts", ATTEMPTS, "learning_progress", "navigation_state",
            "lesson_narration_settings", "app_settings", "appearance_settings", "ui_language_settings", "premium_state", "lesson_text_size_settings")
        private val CMAKE_DESCRIPTION = """
            cmake_minimum_required(VERSION 3.20)
            project(Existencias LANGUAGES CXX)
            option(USAR_COMPARACION_ESTRICTA "Comparar sin incluir la igualdad" OFF)
            add_executable(consulta main.cpp existencias.cpp)
            target_compile_features(consulta PRIVATE cxx_std_20)
            set_target_properties(consulta PROPERTIES CXX_EXTENSIONS OFF)
            if(USAR_COMPARACION_ESTRICTA)
                target_compile_definitions(consulta PRIVATE LGP_STRICT_RESTOCK=1)
            endif()
            # This target is intentionally invalid C++, but is not requested.
            add_executable(no_construir fuente_otro_objetivo.cpp)
        """.trimIndent() + "\n"
        private val MAIN_SOURCE = """
            #include "existencias.h"
            #include <iostream>
            int main() {
                std::cout << debe_reponer(4, 5) << ':'
                          << debe_reponer(5, 5) << ':'
                          << debe_reponer(6, 5);
                return 0;
            }
        """.trimIndent() + "\n"
        private val INVENTORY_SOURCE = """
            #include "existencias.h"
            bool debe_reponer(int disponibles, int minimo) {
            #ifdef LGP_STRICT_RESTOCK
                return disponibles < minimo;
            #else
                return disponibles <= minimo;
            #endif
            }
        """.trimIndent() + "\n"
        private val STATIC_CMAKE_DESCRIPTION = """
            cmake_minimum_required(VERSION 3.20)
            project(Existencias LANGUAGES CXX)
            option(USAR_COMPARACION_ESTRICTA "Comparar sin incluir la igualdad" OFF)
            add_library(existencias STATIC existencias.cpp)
            target_compile_features(existencias PUBLIC cxx_std_20)
            set_target_properties(existencias PROPERTIES CXX_EXTENSIONS OFF)
            if(USAR_COMPARACION_ESTRICTA)
                target_compile_definitions(existencias PRIVATE LGP_STRICT_RESTOCK=1)
            endif()
            add_executable(consulta main.cpp)
            target_compile_features(consulta PRIVATE cxx_std_20)
            set_target_properties(consulta PROPERTIES CXX_EXTENSIONS OFF)
            target_link_libraries(consulta PRIVATE existencias)
        """.trimIndent() + "\n"
        private val FILES = linkedMapOf(
            "CMakeLists.txt" to CMAKE_DESCRIPTION,
            "existencias.h" to "#ifndef QA_EXISTENCIAS_H\n#define QA_EXISTENCIAS_H\nbool debe_reponer(int disponibles, int minimo);\n#endif\n",
            "existencias.cpp" to INVENTORY_SOURCE,
            "main.cpp" to MAIN_SOURCE,
            "fuente_otro_objetivo.cpp" to "este objetivo no debe construirse como parte de consulta;\n"
        )
    }
}
