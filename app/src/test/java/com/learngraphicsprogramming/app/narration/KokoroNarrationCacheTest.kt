package com.learngraphicsprogramming.app.narration

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class KokoroNarrationCacheTest {
    @Test fun sameModelVoiceLanguageAndSentenceAlwaysYieldSameFileName() {
        val first = KokoroNarrationCache.key(
            "es", "ef_dora", "La programación permite resolver problemas."
        )
        val second = KokoroNarrationCache.key(
            "es", "ef_dora", "La programación permite resolver problemas."
        )
        assertEquals(first, second)
        assertTrue(first.matches(Regex("[a-f0-9]{64}")))
    }

    @Test fun cacheCannotMixTextLanguageOrVoices() {
        val spanish = KokoroNarrationCache.key("es", "ef_dora", "Hola.")
        assertNotEquals(spanish, KokoroNarrationCache.key("es", "ef_dora", "Hola mundo."))
        assertNotEquals(spanish, KokoroNarrationCache.key("en-US", "ef_dora", "Hola."))
        assertNotEquals(spanish, KokoroNarrationCache.key("es", "ef_alex", "Hola."))
    }
}
