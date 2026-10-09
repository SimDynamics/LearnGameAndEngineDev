package com.learngraphicsprogramming.app

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.util.concurrent.TimeUnit

/**
 * Run separately, only after importing experimental LLVM/Clang 23.1.3.
 *
 * Command:
 * .\gradlew.bat :app:connectedDebugAndroidTest "-Plgp.wasmtime=true"
 *   "-Plgp.clang23=true"
 *   "-Pandroid.testInstrumentationRunnerArguments.class=com.learngraphicsprogramming.app.Clang23InstrumentedTest"
 */
@RunWith(AndroidJUnit4::class)
class Clang23InstrumentedTest {
    @Test
    fun reportsExactClang2313FromAndroidNativeExecutable() {
        assertTrue("This test requires -Plgp.clang23=true", BuildConfig.LGP_CLANG23_ENABLED)
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val executable = File(context.applicationInfo.nativeLibraryDir, "liblgp_clang23.so")
        assertTrue("Clang 23 ARM64 binary missing from APK", executable.isFile)

        val process = ProcessBuilder(executable.absolutePath, "--version")
            .redirectErrorStream(true)
            .start()
        try {
            val finished = process.waitFor(15, TimeUnit.SECONDS)
            assertTrue("Native Clang 23 --version timed out", finished)
            val output = process.inputStream.bufferedReader().use { it.readText() }
            assertEquals(output, 0, process.exitValue())
            assertTrue(
                "Expected LLVM/Clang 23.1.3 on Android, got: $output",
                Regex("""(?i)clang version 23\.1\.3\b""").containsMatchIn(output)
            )
        } finally {
            if (process.isAlive) process.destroyForcibly()
        }
    }
}
