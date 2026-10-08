package com.learngraphicsprogramming.app.narration

import java.io.File
import java.io.RandomAccessFile
import java.nio.ByteBuffer
import java.nio.ByteOrder
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Test

class KokoroOnnxInputsTest {
    private val vocabulary = mapOf('$' to 0L, 'ˈ' to 156L, 'o' to 57L, 'l' to 54L, 'a' to 43L, ' ' to 16L)

    @Test fun padsPhoneticInputWithExactlyTwoZeroTokens() {
        assertArrayEquals(longArrayOf(0L, 156L, 57L, 54L, 43L, 0L), KokoroOnnxInputs.tokenize("ˈola", vocabulary))
    }

    @Test fun acceptsLegitimateShortPhonemeWithoutInventingAStressMark() {
        assertArrayEquals(longArrayOf(0L, 43L, 0L), KokoroOnnxInputs.tokenize("a", vocabulary))
    }

    @Test fun rejectsUnknownSymbolsInsteadOfSilentlyDroppingThem() {
        assertThrows(IllegalArgumentException::class.java) { KokoroOnnxInputs.tokenize("ˈola\n", vocabulary) }
    }

    @Test fun rejectsOversizedPhonemesInsteadOfTruncating() {
        assertThrows(IllegalArgumentException::class.java) { KokoroOnnxInputs.tokenize("ˈ" + "a".repeat(510), vocabulary) }
        assertEquals(512, KokoroOnnxInputs.tokenize("ˈ" + "a".repeat(509), vocabulary).size)
    }

    @Test fun limitsLongestStyleSelectionToLastExistingRow() {
        assertEquals(509, KokoroOnnxInputs.styleRow(509))
        assertEquals(509, KokoroOnnxInputs.styleRow(510))
    }

    @Test fun readsOnlySelectedStyleRowInLittleEndian() {
        val file = File.createTempFile("kokoro-style", ".bin")
        try {
            RandomAccessFile(file, "rw").use { output ->
                output.setLength(510L * 256L * 4L)
                output.seek(4L * 256L * 4L)
                val row = ByteBuffer.allocate(256 * 4).order(ByteOrder.LITTLE_ENDIAN)
                repeat(256) { row.putFloat(it.toFloat() / 256f) }
                output.write(row.array())
            }
            val style = KokoroOnnxInputs.readStyle(file, 4)
            assertEquals(0f, style[0], 0f)
            assertEquals(255f / 256f, style[255], 0f)
        } finally {
            file.delete()
        }
    }
}
