package com.learngraphicsprogramming.app.narration

import android.content.Context
import android.content.ContextWrapper
import android.os.Build
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
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
}
