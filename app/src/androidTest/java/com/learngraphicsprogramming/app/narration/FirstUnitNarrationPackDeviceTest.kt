package com.learngraphicsprogramming.app.narration

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
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
}
