package com.learngraphicsprogramming.app

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

/**
 * Verifies the APK really contains the selected compiler/backend/sysroot.
 *
 * Run with no build flags to check the Clang 23 + Wasmtime + EH default.
 * Run with -Plgp.clang23=false to check the Clang 21 + WAMR fallback.
 *
 * This checks physical APK contents, not merely BuildConfig constants.
 */
@RunWith(AndroidJUnit4::class)
class CppToolchainSelectionInstrumentedTest {
    @Test
    fun packagedRuntimeMatchesSelectedConfiguration() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val nativeDirectory = File(context.applicationInfo.nativeLibraryDir)

        if (BuildConfig.LGP_CLANG23_ENABLED) {
            assertTrue("Clang 23 missing from APK",
                File(nativeDirectory, "liblgp_clang23.so").isFile)
            assertTrue("LLD 23 missing from APK",
                File(nativeDirectory, "liblgp_lld23.so").isFile)
            assertFalse("Stale Clang 21 remains in Clang 23 APK",
                File(nativeDirectory, "libfoldclang.so").exists())
            assertFalse("Stale FoldCode LLD remains in Clang 23 APK",
                File(nativeDirectory, "libfoldlldcore.so").exists())
        } else {
            assertTrue("Clang 21 missing from fallback APK",
                File(nativeDirectory, "libfoldclang.so").isFile)
            assertTrue("FoldCode LLD missing from fallback APK",
                File(nativeDirectory, "libfoldlldcore.so").isFile)
            assertFalse("Stale Clang 23 remains in fallback APK",
                File(nativeDirectory, "liblgp_clang23.so").exists())
            assertFalse("Stale LLD 23 remains in fallback APK",
                File(nativeDirectory, "liblgp_lld23.so").exists())
        }

        assertEquals(
            "Wasmtime native library does not match selected WASM backend",
            BuildConfig.LGP_WASMTIME_ENABLED,
            File(nativeDirectory, "libwasmtime.so").isFile
        )
        assertEquals(
            "WASI EH overlay does not match selected exception mode",
            BuildConfig.LGP_CPP_EXCEPTIONS_ENABLED,
            context.assets.list("runtime")?.contains("wasi-eh-sysroot.zip") == true
        )
    }
}
