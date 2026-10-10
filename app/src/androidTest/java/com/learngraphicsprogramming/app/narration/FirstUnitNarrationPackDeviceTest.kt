package com.learngraphicsprogramming.app.narration

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import android.content.Context
import android.content.ContextWrapper
import android.content.res.Configuration
import android.media.MediaMetadataRetriever
import android.os.Build
import com.learngraphicsprogramming.app.R
import java.io.File
import java.util.Locale
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
     * Verify the entire Spanish/Dora release pack independently of the user's
     * current UI/voice preferences. Only a private test sandbox is populated.
     */
    @Test fun packagedOpusResolvesFromFreshCacheWithoutGeneratingWav() {
        assumeTrue("Opus playback requires Android 10+", Build.VERSION.SDK_INT >= 29)
        val app = InstrumentationRegistry.getInstrumentation().targetContext
        val bundled = app.assets.list("narration/prerendered").orEmpty()
            .filter { it.endsWith(".ogg") }.toSet()
        assertTrue("The current APK must include the verified first-unit Opus pack", bundled.isNotEmpty())
        val spanish = app.createConfigurationContext(Configuration(app.resources.configuration).apply {
            setLocale(Locale.forLanguageTag("es"))
        })
        val required = linkedMapOf<String, String>()
        for (name in listOf("what_is_programming", "first_contact_cpp", "program_structure")) {
            val path = "content/es/programming/cpp_fundamentals/intro_programming/lesson_$name.json"
            val lesson = JSONObject(app.assets.open(path).bufferedReader().use { it.readText() })
            for (page in 0 until lesson.getJSONArray("pages").length()) {
                val texts = LessonNarrationText.page(lesson, page, NarrationLanguage.SPANISH) { type ->
                    when (type) {
                        "order_code" -> spanish.getString(R.string.exercise_order_instruction)
                        "drag_fill_code" -> spanish.getString(R.string.exercise_fill_instruction)
                        else -> ""
                    }
                }
                NarrationSegments.sentences(texts).forEach { text ->
                    required[KokoroNarrationCache.key("es", "ef_dora", text)] = text
                }
            }
        }
        assertTrue("Missing packaged first-unit sentences: ${required.keys.filter { "$it.ogg" !in bundled }}",
            required.keys.all { "$it.ogg" in bundled })

        val sandbox = File(app.cacheDir, "kokoro-pack-test-${System.nanoTime()}")
        assertTrue("Could not create isolated test directory", sandbox.mkdirs())
        val isolatedContext = object : ContextWrapper(app) {
            override fun getFilesDir(): File = sandbox
            override fun getApplicationContext(): Context = this
        }
        try {
            for ((hash, text) in required) {
                val audio = KokoroNarrationCache.lookup(isolatedContext, "es", "ef_dora", text)
                assertNotNull("Packaged Opus must be found without inference: $hash", audio)
                val file = requireNotNull(audio)
                assertEquals("ogg", file.extension)
                assertTrue(file.isFile)
                assertEquals(file, KokoroNarrationCache.lookup(isolatedContext, "es", "ef_dora", text))
                assertFalse("Packaged Opus must not trigger WAV synthesis",
                    File(file.parentFile, "$hash.wav").exists())
                val decoder = MediaMetadataRetriever()
                try {
                    decoder.setDataSource(file.absolutePath)
                    val duration = decoder.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION)
                    assertTrue("Android must decode nonempty packaged Opus: $hash",
                        (duration?.toLongOrNull() ?: 0) > 0)
                } finally { decoder.release() }
            }
        } finally {
            // Only our temporary isolated directory; never the user's actual cache.
            sandbox.deleteRecursively()
        }
    }

    @Test fun pagesSixAndSevenIncludeTheInstructionsActuallyDisplayedByTheUi() {
        val app = InstrumentationRegistry.getInstrumentation().targetContext
        val path = "content/es/programming/cpp_fundamentals/intro_programming/lesson_what_is_programming.json"
        val lesson = JSONObject(app.assets.open(path).bufferedReader().use { it.readText() })
        for ((page, type) in listOf(5 to "order_code", 6 to "drag_fill_code")) {
            val expected = NarrationUiInstructions.forBlock(app, type)
            val texts = LessonNarrationText.page(lesson, page, NarrationLanguage.SPANISH) {
                NarrationUiInstructions.forBlock(app, it)
            }
            assertTrue("Small visible instructions must be narrated on page ${page + 1}",
                NarrationSegments.sentences(texts).contains(expected))
            assertFalse("Hidden explanations and source code must remain private",
                texts.any { "#include" in it || "Primero incluimos la biblioteca" in it })
        }
    }

}
