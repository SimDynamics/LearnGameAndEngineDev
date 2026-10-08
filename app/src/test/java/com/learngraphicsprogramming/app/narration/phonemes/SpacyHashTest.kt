package com.learngraphicsprogramming.app.narration.phonemes

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Test

class SpacyHashTest {
    /** Goldens produced by spaCy 3.8.7 strings.hash_string, not a second Kotlin hash. */
    @Test fun utf8StringHashMatchesTheOriginalNativeStringStore() {
        val cases = mapOf(
            "hello" to 5_983_625_672_228_268_878UL,
            "world" to 1_703_489_418_272_052_182UL,
            "Equivalent" to 3_249_796_829_112_261_788UL,
            "résumé" to 7_545_801_987_643_780_203UL,
            "\uD801\uDC00" to 492_027_492_781_394_499UL,
            "a".repeat(100) to 12_500_226_556_396_514_998UL
        )
        cases.forEach { (text, expected) -> assertEquals(expected.toLong(), SpacyHash.stringHash(text)) }
    }

    /** Thinc 8.3.11 NumpyOps.hash goldens cover zero, overflow and all four uint32 halves. */
    @Test fun uint64HashBucketsMatchTheActualThincCpuSpecialization() {
        fun unsigned(values: IntArray): LongArray = values.map { it.toLong() and 0xffff_ffffL }.toLongArray()
        assertArrayEquals(longArrayOf(0, 0, 0, 0), unsigned(SpacyHash.hashEmbed(0L, 8)))
        assertArrayEquals(longArrayOf(1_161_550_108, 2_247_783_286, 1_742_325_162, 1_224_191_281), unsigned(SpacyHash.hashEmbed(1L, 8)))
        assertArrayEquals(longArrayOf(1_280_338_815, 3_260_680_329, 3_463_791_303, 2_886_450_919), unsigned(SpacyHash.hashEmbed(-1L, 9)))
        assertArrayEquals(longArrayOf(767_635_603, 3_862_494_181, 1_552_527_052, 3_703_363_205), unsigned(SpacyHash.hashEmbed(0x123456789abcdef0L, 13)))
    }
}
