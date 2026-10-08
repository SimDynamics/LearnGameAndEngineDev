package com.learngraphicsprogramming.app.narration.phonemes

import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class EnglishMisakiBartTokensTest {
    @Test fun encodingRetainsCasingAndUsesOriginalStartEndUnknownAndCharacterIndices() {
        val table = EnglishMisakiBartResources.americanGraphemes
        assertArrayEquals(longArrayOf(1, table.indexOf('a').toLong(), table.indexOf('A').toLong(), 3, 2), MisakiBartTokens.encode("aAé", table))
        assertArrayEquals(longArrayOf(1, 3, 2), MisakiBartTokens.encode("_", table))
        assertArrayEquals(longArrayOf(1, 3, 2), MisakiBartTokens.encode("😀", table))
        assertEquals(64, MisakiBartTokens.encode("a".repeat(62), table).size)
        assertThrows(EnglishMisakiException::class.java) { MisakiBartTokens.encode("a".repeat(63), table) }
    }

    @Test fun decoderDiscardsOnlyReservedAndUnmappedIdsLikeUpstreamTokenToPhonemeMap() {
        val table = EnglishMisakiBartResources.americanPhonemes
        val ids = longArrayOf(0, 1, 2, 3, table.indexOf('k').toLong(), table.indexOf('ˈ').toLong(), table.indexOf('æ').toLong(), 62)
        assertEquals("kˈæ", MisakiBartTokens.decode(ids, table))
        assertEquals("ː", MisakiBartTokens.decode(longArrayOf(EnglishMisakiBartResources.britishPhonemes.indexOf('ː').toLong()), EnglishMisakiBartResources.britishPhonemes))
    }

    @Test fun greedyDecodingStopsAtEosAndUsesLowestIndexForArgmaxTies() {
        var calls = 0
        val generated = MisakiBartTokens.generate { prefix ->
            calls++
            if (prefix.size == 1) FloatArray(63) { -10f }.also { it[7] = 1f; it[8] = 1f }
            else FloatArray(63) { -10f }.also { it[2] = 1f }
        }
        assertArrayEquals(longArrayOf(1, 7, 2), generated)
        assertEquals(2, calls)
    }

    @Test fun originalTransformersDefaultAllowsTwentyNewTokensAndForcesFinalEos() {
        var calls = 0
        val generated = MisakiBartTokens.generate {
            calls++
            FloatArray(63).also { it[10] = 1f }
        }
        assertEquals(21, generated.size)
        assertEquals(20, calls)
        assertEquals(1L, generated.first())
        assertTrue(generated.slice(1 until 20).all { it == 10L })
        assertEquals(2L, generated.last())
        assertThrows(EnglishMisakiException::class.java) { MisakiBartTokens.generate { FloatArray(63) { Float.NaN } } }
    }

    @Test fun manifestedExportsMustIdentifyPinnedSourceAndExactLanguageTables() {
        for (british in listOf(false, true)) {
            val manifest = JSONObject().put("format", 1).put("license", "Apache-2.0")
                .put("misaki_revision", EnglishMisakiBartResources.misakiRevision).put("transformers", "4.51.2")
                .put("source_revision", if (british) EnglishMisakiBartResources.britishRevision else EnglishMisakiBartResources.americanRevision)
                .put("source_sha256", if (british) EnglishMisakiBartResources.britishWeightSha256 else EnglishMisakiBartResources.americanWeightSha256)
                .put("grapheme_chars", if (british) EnglishMisakiBartResources.britishGraphemes else EnglishMisakiBartResources.americanGraphemes)
                .put("phoneme_chars", if (british) EnglishMisakiBartResources.britishPhonemes else EnglishMisakiBartResources.americanPhonemes)
                .put("maximum_generated_tokens", 21).put("maximum_input_tokens", 64).put("vocab_size", 63).put("opset", 17)
            EnglishMisakiBartResources.validateManifest(manifest, british)
            manifest.put("source_sha256", "UNKNOWN")
            assertThrows(IllegalArgumentException::class.java) { EnglishMisakiBartResources.validateManifest(manifest, british) }
        }
    }
}
