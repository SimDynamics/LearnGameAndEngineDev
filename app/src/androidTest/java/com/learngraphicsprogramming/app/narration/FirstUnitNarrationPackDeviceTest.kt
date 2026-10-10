package com.learngraphicsprogramming.app.narration

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import android.content.ContextWrapper
import android.os.Build
import java.io.File
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.Assume.assumeTrue
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class FirstUnitNarrationPackDeviceTest {
    @Test fun manifestsEveryAuthoredSentenceFromTheFirstThreeLessons() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        FirstUnitNarrationPack.writeManifest(context)
        val manifest = JSONObject(
            File(context.filesDir, FirstUnitNarrationPack.MANIFEST_FILE).readText()
        )
        assertEquals("cpp_fundamentals", manifest.getString("course"))
        assertEquals("intro_programming", manifest.getString("unit"))
        val entries = manifest.getJSONArray("entries")
        assertTrue("The three first lessons must contain spoken content", entries.length() > 0)
        val names = mutableSetOf<String>()
        val hashes = mutableSetOf<String>()
        for (i in 0 until entries.length()) {
            val entry = entries.getJSONObject(i)
            names += entry.getString("lesson")
            val hash = entry.getString("hash")
            assertTrue(hash.matches(Regex("[0-9a-f]{64}")))
            assertTrue("Duplicated sentence must only be packaged once", hashes.add(hash))
        }
        assertEquals(
            setOf(
                "cpp_fundamentals__intro_programming__what_is_programming",
                "cpp_fundamentals__intro_programming__first_contact_cpp",
                "cpp_fundamentals__intro_programming__program_structure"
            ),
            names
        )
    }

    /**
     * Optional release-pack check. Uses a private sandbox rather than removing
     * any real user recordings. A packaged Opus file must satisfy the same
     * lookup the background generator uses to skip synthesis.
     */
    @Test fun packagedOpusResolvesFromFreshCacheWithoutGeneratingWav() {
        assumeTrue("Opus playback requires Android 10+", Build.VERSION.SDK_INT >= 29)
        val app = InstrumentationRegistry.getInstrumentation().targetContext
        val bundled = app.assets.list("narration/prerendered").orEmpty()
            .filter { it.endsWith(".ogg") }.toSet()
        assumeTrue("This development APK has no optional packaged Opus audio", bundled.isNotEmpty())
        val item = NarrationCurriculum.sentences(app).firstOrNull { sentence ->
            "${KokoroNarrationCache.key(sentence.language.tag, sentence.voice, sentence.text)}.ogg" in bundled
        }
        assumeTrue("Current narration voice/language does not match this optional pack", item != null)
        val sentence = requireNotNull(item)

        val sandbox = File(app.cacheDir, "kokoro-pack-test-${System.nanoTime()}")
        assertTrue("Could not create isolated test directory", sandbox.mkdirs())
        val isolatedContext = object : ContextWrapper(app) {
            override fun getFilesDir(): File = sandbox
        }
        try {
            val audio = KokoroNarrationCache.lookup(
                isolatedContext, sentence.language.tag, sentence.voice, sentence.text
            )
            assertNotNull("Packaged Opus must be found without inference", audio)
            val file = requireNotNull(audio)
            assertEquals("ogg", file.extension)
            assertTrue(file.isFile)
            assertEquals(file, KokoroNarrationCache.lookup(
                isolatedContext, sentence.language.tag, sentence.voice, sentence.text
            ))
            val hash = KokoroNarrationCache.key(sentence.language.tag, sentence.voice, sentence.text)
            assertFalse("Packaged Opus must not trigger WAV synthesis",
                File(file.parentFile, "$hash.wav").exists())
        } finally {
            // Only our temporary isolated directory; never the user's actual cache.
            sandbox.deleteRecursively()
        }
    }

}
