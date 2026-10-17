package com.learngraphicsprogramming.app.narration

import android.content.Context
import android.content.ContextWrapper
import android.os.Build
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class KokoroNarrationCacheDeviceTest {
    private fun isolated(test: (Context, File) -> Unit) {
        val app = InstrumentationRegistry.getInstrumentation().targetContext
        val sandbox = File(app.cacheDir, "kokoro-cache-preservation-${System.nanoTime()}")
        assertTrue(sandbox.mkdirs())
        val context = object : ContextWrapper(app) {
            override fun getFilesDir(): File = sandbox
            override fun getApplicationContext(): Context = this
        }
        try { test(context, sandbox) } finally {
            // This directory belongs only to this test, never to the user's recordings.
            sandbox.deleteRecursively()
        }
    }

    @Test fun rejectedCachedOpusIsPreservedWhenThePackRepairsIt() = isolated { context, sandbox ->
        assumeTrue(Build.VERSION.SDK_INT >= 29)
        val text = "¿Qué es la programación?"
        val hash = KokoroNarrationCache.key("es", "ef_dora", text)
        val cache = File(sandbox, "kokoro-narration-cache").apply { mkdirs() }
        val original = "a previous interrupted transfer".toByteArray()
        File(cache, "$hash.ogg").writeBytes(original)

        val repaired = KokoroNarrationCache.lookup(context, "es", "ef_dora", text)
        assertNotNull("A rejected cache file must not prevent matching packaged audio", repaired)
        assertEquals("ogg", requireNotNull(repaired).extension)
        val preserved = cache.listFiles().orEmpty().single { ".unverified-" in it.name }
        assertArrayEquals(original, preserved.readBytes())
        KokoroNarrationCache.prune(context)
        assertArrayEquals("Pruning may not erase rejected original bytes", original, preserved.readBytes())
        assertEquals("The storage ceiling includes preserved files",
            cache.listFiles().orEmpty().sumOf { it.length() }, NarrationStorageBudget.snapshot(context).storedBytes)
    }

    @Test fun storingAValidReplacementPreservesTheRejectedWav() = isolated { context, sandbox ->
        val text = "Test-only preservation sentence."
        val hash = KokoroNarrationCache.key("es", "ef_dora", text)
        val cache = File(sandbox, "kokoro-narration-cache").apply { mkdirs() }
        val original = "old incomplete WAV".toByteArray()
        File(cache, "$hash.wav").writeBytes(original)
        val generated = File(sandbox, "generated.wav")
        NarrationWavWriter.write(generated, floatArrayOf(0f, 0.1f, -0.1f))

        val stored = KokoroNarrationCache.store(context, "es", "ef_dora", text, generated)
        assertNotNull(stored)
        assertArrayEquals(generated.readBytes(), requireNotNull(stored).readBytes())
        assertArrayEquals(original, cache.listFiles().orEmpty()
            .single { ".unverified-" in it.name }.readBytes())
    }

    @Test fun aRejectedNewWavCannotChangeAnyExistingRecording() = isolated { context, sandbox ->
        val text = "Test-only invalid replacement."
        val hash = KokoroNarrationCache.key("es", "ef_dora", text)
        val cache = File(sandbox, "kokoro-narration-cache").apply { mkdirs() }
        val target = File(cache, "$hash.wav")
        val original = "original incomplete bytes".toByteArray()
        target.writeBytes(original)
        val rejected = File(sandbox, "rejected.wav").apply { writeText("Not a WAV") }
        assertNull(KokoroNarrationCache.store(context, "es", "ef_dora", text, rejected))
        assertArrayEquals(original, target.readBytes())
        assertEquals(1, cache.listFiles().orEmpty().size)
    }

    @Test fun anExistingValidWavIsReusedWithoutOverwritingIt() = isolated { context, sandbox ->
        val text = "Test-only valid reuse."
        val first = File(sandbox, "first.wav")
        val second = File(sandbox, "second.wav")
        NarrationWavWriter.write(first, floatArrayOf(0f, 0.1f, -0.1f))
        NarrationWavWriter.write(second, floatArrayOf(0f, 0.2f, -0.2f))
        val stored = requireNotNull(KokoroNarrationCache.store(context, "es", "ef_dora", text, first))
        assertEquals(stored, KokoroNarrationCache.store(context, "es", "ef_dora", text, second))
        assertArrayEquals(first.readBytes(), stored.readBytes())
        assertEquals(stored, KokoroNarrationCache.lookup(context, "es", "ef_dora", text))
        assertEquals(1, requireNotNull(stored.parentFile).listFiles().orEmpty().size)
    }

    @Test fun explicitMaintenanceRemovesOnlyChosenVerifiedRecording() = isolated { context, sandbox ->
        val input = File(sandbox, "source.wav")
        NarrationWavWriter.write(input, floatArrayOf(0f, 0.1f, -0.1f))
        val selected = requireNotNull(KokoroNarrationCache.store(
            context, "es", "ef_dora", "Only this test audio is removable.", input))
        val shared = requireNotNull(KokoroNarrationCache.store(
            context, "es", "ef_dora", "Keep this other course audio.", input))
        val savedShared = shared.readBytes()
        val unknownHash = "a".repeat(64)
        val invalid = File(selected.parentFile, "$unknownHash.wav").apply { writeText("Invalid") }
        val staging = File(selected.parentFile, ".unfinished-123.part").apply { writeText("Partial") }
        val targetKeys = linkedSetOf(selected.nameWithoutExtension, unknownHash)

        val denied = KokoroNarrationCache.deleteExclusiveVerified(context, targetKeys) { true }
        assertTrue("Explicit deletion requires an exclusive maintenance lease", denied.interrupted)
        assertEquals(0, denied.deletedFiles)
        assertTrue(selected.isFile)

        val lease = requireNotNull(NarrationWorkPriority.beginMaintenanceIfIdle())
        try {
            val result = KokoroNarrationCache.deleteExclusiveVerified(context, targetKeys) {
                NarrationWorkPriority.isMaintenanceExclusive
            }
            assertFalse(result.interrupted)
            assertEquals(1, result.deletedFiles)
            assertEquals(input.length(), result.deletedBytes)
            assertEquals(0, result.failedFiles)
        } finally {
            lease.close()
        }
        assertFalse(selected.exists())
        assertArrayEquals(savedShared, shared.readBytes())
        assertTrue("Invalid audio must not be removed", invalid.isFile)
        assertTrue("Partial audio must not be removed", staging.isFile)
    }

    @Test fun interruptedMaintenanceDoesNotResumeAfterReaderCloses() = isolated { context, sandbox ->
        val input = File(sandbox, "source.wav")
        NarrationWavWriter.write(input, floatArrayOf(0f, 0.1f, -0.1f))
        val first = requireNotNull(KokoroNarrationCache.store(
            context, "es", "ef_dora", "First deletion fixture.", input))
        val second = requireNotNull(KokoroNarrationCache.store(
            context, "es", "ef_dora", "Second recording must remain.", input))

        var reader: AutoCloseable? = null
        val lease = requireNotNull(NarrationWorkPriority.beginMaintenanceIfIdle())
        try {
            val result = KokoroNarrationCache.deleteExclusiveVerified(context,
                linkedSetOf(first.nameWithoutExtension, second.nameWithoutExtension)) {
                if (!first.exists() && reader == null) {
                    reader = NarrationWorkPriority.beginInteractive()
                    reader?.close()
                }
                NarrationWorkPriority.isMaintenanceExclusive
            }
            assertTrue(result.interrupted)
            assertEquals(1, result.deletedFiles)
            assertFalse(first.exists())
            assertTrue(second.isFile)
            assertFalse(NarrationWorkPriority.isMaintenanceExclusive)
        } finally {
            reader?.close()
            lease.close()
        }
    }
}
