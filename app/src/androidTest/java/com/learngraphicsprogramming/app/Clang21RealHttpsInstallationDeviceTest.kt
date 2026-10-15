package com.learngraphicsprogramming.app

import android.app.DownloadManager
import android.content.Context
import android.content.pm.PackageManager
import android.graphics.Bitmap
import android.os.ParcelFileDescriptor
import android.os.SystemClock
import android.view.View
import android.view.accessibility.AccessibilityNodeInfo
import androidx.test.core.app.ActivityScenario
import androidx.test.espresso.Espresso.onView
import androidx.test.espresso.action.ViewActions.click
import androidx.test.espresso.matcher.RootMatchers.isDialog
import androidx.test.espresso.matcher.ViewMatchers.withId
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.runtime.Clang21FeatureDelivery
import com.learngraphicsprogramming.app.runtime.Clang21FeatureDelivery.Phase
import com.learngraphicsprogramming.app.runtime.Clang21InstallationFiles
import com.learngraphicsprogramming.app.runtime.Clang21InstallationRecipe
import com.learngraphicsprogramming.app.runtime.CppCmakeEngine
import com.learngraphicsprogramming.app.runtime.CppCmakePhase
import com.learngraphicsprogramming.app.runtime.CppCmakeProject
import com.learngraphicsprogramming.app.runtime.CppCompiler
import com.learngraphicsprogramming.app.runtime.CppCompilerVariant
import com.learngraphicsprogramming.app.runtime.CppExecutionEngine
import java.io.File
import java.security.MessageDigest
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Before
import org.junit.FixMethodOrder
import org.junit.Test
import org.junit.runner.RunWith
import org.junit.runners.MethodSorters

/**
 * Standalone HTTPS acceptance. Only the dedicated QA APK may run this suite. Android's real
 * source permission and PackageInstaller confirmation remain mandatory and are clicked through
 * their native UI, never granted through shell app-ops or replaced by adb split installation.
 * The three methods may be invoked separately to verify state after process recreation.
 */
@RunWith(AndroidJUnit4::class)
@FixMethodOrder(MethodSorters.NAME_ASCENDING)
class Clang21RealHttpsInstallationDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val evidence get() = File(context.filesDir, "real-https-clang21").apply { mkdirs() }
    private val preferences get() = context.getSharedPreferences("clang21_https_delivery", Context.MODE_PRIVATE)
    private val downloads get() = context.getSystemService(Context.DOWNLOAD_SERVICE) as DownloadManager
    private val recipe by lazy {
        context.assets.open("runtime/clang21_installation/recipe.json").bufferedReader()
            .use { Clang21InstallationRecipe.parse(it.readText()) }
    }
    private val progress = JSONArray()
    private var lastState = ""

    @Before fun requireDedicatedStandaloneQaPackageOnLdPlayer() {
        assertEquals("Never install a split into the user's application or existing QA package",
            "com.learngraphicsprogramming.app.realdownloadsqa", context.packageName)
        assertEquals("https", BuildConfig.LGP_CLANG21_DELIVERY_PROVIDER)
        val emulatorProperties = listOf("ro.boot.qemu", "ro.kernel.qemu").associateWith { property ->
            val result = instrumentation.uiAutomation.executeShellCommand("getprop $property")
            ParcelFileDescriptor.AutoCloseInputStream(result).bufferedReader().use { it.readText().trim() }
        }
        assertTrue("Only the explicitly selected LDPlayer emulator may run this suite: $emulatorProperties",
            emulatorProperties.values.any { it == "1" })
        assertTrue(BuildConfig.LGP_CLANG23_ENABLED && BuildConfig.LGP_WASMTIME_ENABLED)
        assertEquals(context.packageName, recipe.packageName)
        assertEquals(12, recipe.libraries.size)
        assertEquals(2, recipe.archives.size)
        assertTrue(recipe.archives.all { it.url.startsWith("https://github.com/FifiLeih/FoldCode/releases/download/v1.0.0/") })
    }

    @Test fun a_requestPermissionThroughAndroidCancelRetryAndRecoverRealHttpsTransfers() {
        assertFalse("This installation test needs an APK without the optional split", CppCompiler.isOptionalClang21Available(context))
        assertTrue("Only this test's two compiler transfers may be absent initially", compilerTransfers().length() == 0)
        val kokoroBefore = kokoroStamps()
        var delivery = Clang21FeatureDelivery(context)
        try {
            assertEquals("The standalone provider must be packaged and usable: ${delivery.currentState()}",
                Phase.NOT_INSTALLED, delivery.currentState().phase)
            ActivityScenario.launch(CppPrototypeActivity::class.java).use { scenario ->
                scenario.onActivity { activity ->
                    assertEquals(View.VISIBLE, activity.findViewById<View>(R.id.clang21FeatureSection).visibility)
                    assertTrue(activity.findViewById<View>(R.id.clang21DownloadButton).performClick())
                }
                onView(withId(android.R.id.button1)).inRoot(isDialog()).perform(click())
                var permissionNeeded = false
                await(delivery, "The request must reach Android permission or a genuine transfer", 120_000) { state ->
                    if (state.phase == Phase.NEEDS_CONFIRMATION && state.sessionId == -1) {
                        permissionNeeded = true
                        grantOnlyThroughOfficialSettings()
                    }
                    compilerTransfers().length() > 0
                }
                screenshot("first-real-transfer.png")
                val beforeCancel = compilerTransfers()
                delivery.cancel { error -> assertNull("Cancellation must remain available", error) }
                await(delivery, "The real compiler transfer must cancel", 30_000) { it.phase == Phase.CANCELED }
                assertFalse(preferences.getBoolean("active", true))
                val canceled = JSONObject().put("permissionUiRequired", permissionNeeded)
                    .put("beforeCancellation", beforeCancel).put("afterCancellation", compilerTransfers())
                save("cancellation.json", canceled)

                delivery.close()
                delivery = Clang21FeatureDelivery(context)
                delivery.requestInstall { }
                await(delivery, "Retry must reach a real resumed/new system transfer", 120_000) {
                    compilerTransfers().length() > 0 || it.phase in setOf(Phase.DOWNLOADED, Phase.INSTALLING, Phase.NEEDS_CONFIRMATION)
                }
                val persistedIds = preferences.all.filterKeys { it.startsWith("download:") }
                delivery.close()
                delivery = Clang21FeatureDelivery(context)
                persistedIds.forEach { (key, id) -> assertEquals("Recreating the facade must reuse its DownloadManager ID", id, preferences.all[key]) }
                await(delivery, "Official downloads must verify and reach the real Android installer", 25 * 60_000L) {
                    it.phase == Phase.NEEDS_CONFIRMATION && it.sessionId != null && it.sessionId!! >= 0
                }
                assertTrue("Android must expose the committed installation session",
                    context.packageManager.packageInstaller.getSessionInfo(delivery.currentState().sessionId!!) != null)
                verifyDownloadedArchives()
                screenshot("android-installation-confirmation.png")
                save("download-complete-awaiting-user.json", JSONObject().put("processId", android.os.Process.myPid())
                    .put("officialSourcePermission", context.packageManager.canRequestPackageInstalls())
                    .put("installerSession", delivery.currentState().sessionId)
                    .put("manualCopies", false).put("adbSplitInstallation", false)
                    .put("transfers", compilerTransfers()).put("progress", progress))
            }
        } finally {
            delivery.close()
            assertEquals("Compiler delivery must preserve all installed neural resources", kokoroBefore, kokoroStamps())
        }
    }

    @Test fun b_confirmTheRealAndroidInstallerWithoutReplacingTheBaseApk() {
        val delivery = Clang21FeatureDelivery(context)
        try {
            val initial = delivery.currentState()
            assertEquals("The real installation must still await the user's consent", Phase.NEEDS_CONFIRMATION, initial.phase)
            assertTrue(initial.sessionId != null && initial.sessionId!! >= 0)
            assertNotNull(context.packageManager.packageInstaller.getSessionInfo(initial.sessionId!!))
            ActivityScenario.launch(CppPrototypeActivity::class.java).use {
                await(delivery, "The Android installation confirmation must become accessible", 30_000) {
                    val button = nodes().firstOrNull(::isOfficialInstallButton)
                    if (button == null) false else {
                        screenshot("before-user-install-confirmation.png")
                        assertTrue("The official Android Install/Update button must accept the user's action",
                            button.performAction(AccessibilityNodeInfo.ACTION_CLICK))
                        save("user-confirmation.json", JSONObject().put("processId", android.os.Process.myPid())
                            .put("sessionId", initial.sessionId).put("androidButtonClicked", true)
                            .put("buttonPackage", button.packageName?.toString()).put("buttonText", button.text?.toString()))
                        true
                    }
                }
                // Android may legitimately kill this process while extracting a native split.
                // Method c is intentionally independent and verifies the durable OS result.
                await(delivery, "Android must install the signed native split", 180_000) { it.phase == Phase.INSTALLED }
                screenshot("platform-installation-complete.png")
            }
        } finally { delivery.close() }
    }

    @Test fun c_verifyTwelvePlatformInstalledLibrariesAndRunBothCompilers() {
        val delivery = Clang21FeatureDelivery(context)
        val kokoroBefore = kokoroStamps()
        try {
            assertEquals(Phase.INSTALLED, delivery.currentState().phase)
            assertTrue("Only a real executable OS-installed split makes Clang 21 available", CppCompiler.isOptionalClang21Available(context))
            val packageInfo = context.packageManager.getPackageInfo(context.packageName, PackageManager.GET_SIGNING_CERTIFICATES)
            assertTrue(packageInfo.splitNames.orEmpty().contains("clang21"))
            assertEquals(recipe.versionCode, packageInfo.longVersionCode)
            val signatures = packageInfo.signingInfo!!.apkContentsSigners
            assertEquals(1, signatures.size)
            val signer = MessageDigest.getInstance("SHA-256").digest(signatures.single().toByteArray()).joinToString("") { "%02x".format(it) }
            assertEquals(recipe.certificateSha256, signer)
            val nativeDir = File(context.packageManager.getApplicationInfo(context.packageName, 0).nativeLibraryDir).canonicalFile
            assertFalse("Executable libraries must never come from private staging", nativeDir.path.startsWith(context.filesDir.canonicalPath))
            val libraries = JSONArray()
            recipe.libraries.forEach { library ->
                val file = File(nativeDir, library.name)
                assertTrue("Android must install an executable native library: ${library.name}", file.canExecute())
                assertTrue("Installed ${library.name} must match the exact official bytes",
                    Clang21InstallationFiles.valid(file, library.bytes, library.sha256))
                libraries.put(JSONObject().put("name", library.name).put("bytes", file.length()).put("sha256", library.sha256).put("path", file.absolutePath))
            }
            verifyDownloadedArchives()
            val results = JSONArray()
            verifyProgram("clang21-hello", CppCompilerVariant.OPTIONAL_CLANG_21,
                "#include <iostream>\nint main() { std::cout << \"Clang 21 HTTPS OK\\n\"; }", "Clang 21 HTTPS OK\n", results)
            verifyProgram("clang21-vector", CppCompilerVariant.OPTIONAL_CLANG_21,
                "#include <iostream>\n#include <vector>\n#include <algorithm>\nint main() { std::vector<int> v{3,1,2}; std::sort(v.begin(),v.end()); for (int x:v) std::cout << x; std::cout << '\\n'; }", "123\n", results)
            verifyProgram("clang23-default", CppCompilerVariant.DEFAULT,
                "#include <iostream>\nint main() { std::cout << \"Clang 23 preserved\\n\"; }", "Clang 23 preserved\n", results)
            verifyCmakeAfterPlatformInstallation()
            val ids = preferences.all.filterKeys { it.startsWith("download:") }
            val stamps = archiveStamps()
            delivery.requestInstall { }
            SystemClock.sleep(500)
            assertEquals(Phase.INSTALLED, delivery.currentState().phase)
            assertEquals("Reusing an installed compiler must not enqueue new transfers", ids, preferences.all.filterKeys { it.startsWith("download:") })
            assertEquals(stamps, archiveStamps())
            save("verified-native-installation.json", JSONObject().put("processId", android.os.Process.myPid())
                .put("nativeDirectory", nativeDir.path).put("certificateSha256", signer).put("versionCode", packageInfo.longVersionCode)
                .put("splitNames", JSONArray(packageInfo.splitNames.orEmpty().toList()))
                .put("libraries", libraries).put("compilations", results).put("installedCompilerReused", true))
        } finally {
            delivery.close()
            assertEquals("Compilation and installation must preserve neural resources", kokoroBefore, kokoroStamps())
        }
    }

    private fun grantOnlyThroughOfficialSettings() {
        if (context.packageManager.canRequestPackageInstalls()) return
        val deadline = SystemClock.uptimeMillis() + 30_000
        while (!context.packageManager.canRequestPackageInstalls() && SystemClock.uptimeMillis() < deadline) {
            val toggle = nodes().firstOrNull { node ->
                node.packageName?.toString() == "com.android.settings" && node.isEnabled && node.isCheckable && !node.isChecked &&
                    (node.className?.toString()?.contains("Switch") == true || node.viewIdResourceName?.endsWith("switch_widget") == true)
            }
            if (toggle != null) {
                screenshot("before-source-permission.png")
                assertTrue("The Android settings switch must accept the permission change", clickable(toggle).performAction(AccessibilityNodeInfo.ACTION_CLICK))
            }
            SystemClock.sleep(250)
        }
        assertTrue("Only the official Android source-permission UI may enable installation", context.packageManager.canRequestPackageInstalls())
        screenshot("source-permission-enabled.png")
        assertTrue(instrumentation.uiAutomation.performGlobalAction(android.accessibilityservice.AccessibilityService.GLOBAL_ACTION_BACK))
        instrumentation.waitForIdleSync()
    }

    private fun isOfficialInstallButton(node: AccessibilityNodeInfo): Boolean {
        val pkg = node.packageName?.toString().orEmpty()
        if (!(pkg.contains("packageinstaller") || pkg.contains("permissioncontroller"))) return false
        val label = node.text?.toString()?.trim()?.lowercase().orEmpty()
        return node.isEnabled && node.isClickable && label in setOf("install", "update", "instalar", "actualizar", "installer", "mettre à jour")
    }
    private fun clickable(node: AccessibilityNodeInfo): AccessibilityNodeInfo {
        var current = node
        repeat(4) { if (current.isClickable) return current; current.parent?.let { current = it } }
        return current
    }
    private fun nodes(): List<AccessibilityNodeInfo> {
        val root = instrumentation.uiAutomation.rootInActiveWindow ?: return emptyList()
        val result = mutableListOf<AccessibilityNodeInfo>()
        fun visit(node: AccessibilityNodeInfo) { result += node; repeat(node.childCount) { node.getChild(it)?.let(::visit) } }
        visit(root)
        return result
    }
    private fun compilerTransfers(): JSONArray = downloads.query(DownloadManager.Query()).use { cursor ->
        val result = JSONArray()
        if (cursor == null) return@use result
        while (cursor.moveToNext()) {
            val url = cursor.getString(cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_URI))
            if (recipe.archives.none { it.url == url }) continue
            result.put(JSONObject().put("id", cursor.getLong(cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_ID)))
                .put("url", url).put("status", cursor.getInt(cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_STATUS)))
                .put("bytes", cursor.getLong(cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_BYTES_DOWNLOADED_SO_FAR)))
                .put("reason", cursor.getInt(cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_REASON))))
        }
        result
    }
    private fun await(delivery: Clang21FeatureDelivery, message: String, timeout: Long, predicate: (Clang21FeatureDelivery.State) -> Boolean) {
        val start = SystemClock.uptimeMillis()
        while (true) {
            val state = delivery.currentState()
            if (state.toString() != lastState) {
                progress.put(JSONObject().put("elapsedMs", SystemClock.uptimeMillis() - start).put("phase", state.phase.name)
                    .put("downloadedBytes", state.downloadedBytes).put("totalBytes", state.totalBytes)
                    .put("sessionId", state.sessionId ?: JSONObject.NULL).put("message", state.message ?: JSONObject.NULL)
                    .put("transfers", compilerTransfers()))
                save("installer-progress.json", JSONObject().put("states", progress))
                lastState = state.toString()
            }
            assertNotEquals("$message: ${state.message}", Phase.FAILED, state.phase)
            if (predicate(state)) return
            if (SystemClock.uptimeMillis() - start >= timeout) {
                screenshot("failure.png")
                save("failure-ui.json", JSONObject().put("nodes", JSONArray(nodes().map {
                    JSONObject().put("package", it.packageName?.toString()).put("text", it.text?.toString())
                        .put("resource", it.viewIdResourceName).put("class", it.className?.toString()).put("enabled", it.isEnabled)
                })))
                fail("$message: $state")
            }
            SystemClock.sleep(500)
        }
    }
    private fun archiveRoot() = File(context.getExternalFilesDir(null), "clang21-https-downloads")
    private fun verifyDownloadedArchives() {
        recipe.archives.forEach { archive ->
            assertTrue("The real official HTTPS archive must retain its exact SHA-256: ${archive.name}",
                Clang21InstallationFiles.valid(File(archiveRoot(), archive.name), archive.bytes, archive.sha256))
        }
    }
    private fun archiveStamps() = recipe.archives.associate { it.name to stamp(File(archiveRoot(), it.name)) }
    private fun kokoroStamps(): Map<String, Pair<Long, Long>> {
        val root = File(context.getExternalFilesDir(null), "kokoro")
        return root.walkTopDown().filter { it.isFile }.associate { it.relativeTo(root).path to stamp(it) }
    }
    private fun stamp(file: File) = file.length() to file.lastModified()
    private fun verifyProgram(name: String, variant: CppCompilerVariant, source: String, expected: String, results: JSONArray) {
        val result = CppExecutionEngine(context).compileAndRun(source, compilerVariant = variant)
        try {
            assertFalse("$name compiler timeout", result.compilation.timedOut)
            assertEquals("$name: ${result.compilation.stderr}", 0, result.compilation.exitCode)
            assertNotNull(result.compilation.wasmFile)
            assertNotNull(result.execution)
            assertEquals("$name: ${result.execution?.stderr}", 0, result.execution?.exitCode)
            assertEquals(expected, result.execution?.stdout)
            results.put(JSONObject().put("name", name).put("compiler", variant.name)
                .put("stdout", result.execution?.stdout).put("compileStderr", result.compilation.stderr)
                .put("wasmBytes", result.compilation.wasmFile!!.length()))
        } finally {
            val workspace = result.compilation.workspace.canonicalFile
            assertTrue("Only this compilation's cache workspace may be removed", workspace.toPath().startsWith(context.cacheDir.canonicalFile.toPath()))
            workspace.deleteRecursively()
        }
    }
    private fun verifyCmakeAfterPlatformInstallation() {
        val engine = CppCmakeEngine(context, "https-split-qa-${java.util.UUID.randomUUID()}")
        val project = CppCmakeProject(linkedMapOf(
            "CMakeLists.txt" to "cmake_minimum_required(VERSION 3.20)\nproject(HTTPS_SPLIT_QA LANGUAGES CXX)\nadd_executable(consulta main.cpp)\nset_target_properties(consulta PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)\n",
            "main.cpp" to "#include <iostream>\n#include <vector>\nint main() { std::vector<int> v{1,2,3}; int total=0; for (int x:v) total+=x; std::cout << \"CMake HTTPS \" << total << '\\n'; }"
        ), "consulta", compilerVariant = CppCompilerVariant.OPTIONAL_CLANG_21)
        try {
            val configured = engine.configure(project)
            assertEquals("Post-install CMake configuration: ${configured.stderr}\n${configured.stdout}", 0, configured.exitCode)
            assertTrue("Real CMake must complete configuration and generation",
                configured.records.map { it.phase }.containsAll(listOf(CppCmakePhase.CONFIGURATION, CppCmakePhase.GENERATION)))
            val built = engine.build(project)
            assertEquals("Post-install CMake build: ${built.stderr}\n${built.stdout}", 0, built.exitCode)
            assertTrue("CMake must own the build invocation", built.records.any { "--build" in it.command && "--target" in it.command })
            val executed = engine.run(project)
            assertEquals("Post-install CMake execution: ${executed.execution?.stderr}", 0, executed.exitCode)
            assertEquals("CMake HTTPS 6\n", executed.execution?.stdout)
            save("cmake-after-installation.json", JSONObject().put("compiler", "Clang 21")
                .put("engineCreatedAfterInstallation", true).put("sameInstanceBeforeInstallationTested", false)
                .put("configurationExitCode", configured.exitCode).put("buildExitCode", built.exitCode)
                .put("executionExitCode", executed.exitCode).put("stdout", executed.execution?.stdout)
                .put("buildCommand", JSONArray(built.records.first().command)).put("tools", JSONObject(configured.toolsInfo)))
        } finally {
            assertTrue("Only this CMake test's cache workspace may be cleaned",
                engine.workspace.canonicalFile.toPath().startsWith(context.cacheDir.canonicalFile.toPath()))
            engine.workspace.deleteRecursively()
        }
    }
    private fun screenshot(name: String) {
        instrumentation.uiAutomation.takeScreenshot()?.let { bitmap ->
            File(evidence, name).outputStream().use { assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) }
            bitmap.recycle()
        }
    }
    private fun save(name: String, json: JSONObject) { File(evidence, name).writeText(json.toString(2), Charsets.UTF_8) }
}
