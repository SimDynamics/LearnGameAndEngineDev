package com.learngraphicsprogramming.app

import java.math.BigInteger
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.fail
import org.junit.Test

class CppDecimalLiteralTest {
    @Test fun curricularEquivalentThreeSpellingsHaveTheSameExactValue() {
        val expected = parse("3")
        for (answer in listOf("3", "3.0", "3.00", "3e0", "3E+0", "30e-1", "0.3e1",
            "3.", ".3e+1", "300.00e-2", "0.0003e4", "3e-0", "3.e0")) {
            assertEquals(answer, expected, parse(answer))
            assertEquals("Only external whitespace is trimmed: $answer", expected,
                parse(" \t\r\n$answer\u000b\u000c "))
            assertEquals(answer, expected.hashCode(), parse(answer).hashCode())
        }
    }

    @Test fun differentQuantitiesAndDecimalNeighboursDoNotBecomeThree() {
        val expected = parse("3")
        for (answer in listOf("0", "2", "4", "3.1", "3.9", "3e1", "3e-1",
            "3.0000000000000001", "2.9999999999999999", "3.00000000000000000001",
            "2.99999999999999999999", "30000000000000001e-16")) {
            val actual = parse(answer)
            assertNotNull("Valid syntax must remain a different quantity: $answer", actual)
            assertNotEquals(answer, expected, actual)
        }
    }

    @Test fun digitSeparatorsPreserveValueInAllThreeDigitSequences() {
        for (answer in listOf("3.0'0", "3'0e-1", "0.0'3e0'2", "0'03.0",
            "3E+0'0", "3.0e-0'0", "0'3e0", "3'0'0.0'0e-0'2")) {
            assertEquals(answer, parse("3"), parse(answer))
        }
        assertEquals(parse("1000000"), parse("1'000'000"))
    }

    @Test fun separatorsMustBeBetweenDigitsWithinTheirOwnDigitSequence() {
        for (answer in listOf("'3", "3'", "3''0", "3'.0", "3.'0", ".'3", "3'e0",
            "3e'0", "3e+'0", "3e-'0", "3e0'", "3e0''0", "3.0'e0", "3' 0")) {
            assertNull(answer, parse(answer))
        }
    }

    @Test fun leadingZerosOnlyBelongToFloatingSyntaxInTheDecimalProfile() {
        for (answer in listOf("03", "00", "000", "0'3", "0'0", "08", "09")) {
            assertNull("Octal/incomplete octal syntax is excluded: $answer", parse(answer))
        }
        for (answer in listOf("03.0", "0003.", "003e0", "000.3e1", "0'03.00", "00'3e0")) {
            assertEquals(answer, parse("3"), parse(answer))
        }
        assertNotNull(parse("0"))
    }

    @Test fun wholeInputRejectsIncompleteSyntaxAndInternalWhitespace() {
        for (answer in listOf("", " \t\r\n", ".", "e0", ".e0", "3e", "3e+", "3e-",
            "3..0", "3 .0", "3. 0", "3 e0", "3e 0", "3e+ 0", "3\n.0",
            "3\t.0", "3,0", "3\u00a0.0", "３", "٣", "3\u0000")) {
            assertNull(answer, parse(answer))
        }
    }

    @Test fun namesCommentsQuotedTextAndExpressionsAreNotSingleLiterals() {
        for (answer in listOf("tres", "minutos", "NAN", "INFINITY", "nan", "inf", "true",
            "1+2", "(3)", "+3", "-3", "3 + 0", "3*1", "3/1", "3;", "3 0",
            "\"3\"", "'3'", "3//texto", "/*texto*/3", "3/*texto*/", "3\n//texto",
            "3\\\n.0")) {
            assertNull(answer, parse(answer))
        }
    }

    @Test fun nonDecimalBasesAndEverySuffixAreOutsideTheProfile() {
        for (answer in listOf("0x3", "0X03", "0b11", "0B11", "0x3p0", "0x1.8p1",
            "3u", "3U", "3l", "3L", "3ll", "3LL", "3ul", "3LLU", "3z", "3uz",
            "3.0f", "3.0F", "3.0l", "3.0L", "3e0f", "3.0f16", "3.0f32", "3.0f64",
            "3.0f128", "3.0bf16", "3_minutes", "3.0_minutes")) {
            assertNull(answer, parse(answer))
        }
    }

    @Test fun unsuffixedIntegerMustHaveIntTypeOnTheAndroidTarget() {
        assertEquals(parse("2147483647.0"), parse("2147483647"))
        assertEquals(parse("2147483647.0"), parse("2'147'483'647"))
        for (answer in listOf("2147483648", "2'147'483'648", "30000000000",
            "9223372036854775807", "18446744073709551615")) {
            assertNull("Unsuffixed long/long long are outside the int/double profile: $answer",
                parse(answer))
        }
        assertNotNull(parse("2147483648.0"))
        assertEquals(parse("3"), parse("30000000000e-10"))
    }

    @Test fun canonicalizationStripsZerosAndBalancesTheDecimalExponentExactly() {
        val value = parse("00012.3400e+3")!!
        assertEquals("1234", value.significand)
        assertEquals(BigInteger.ONE, value.exponent)
        assertEquals(value, parse("123400e-1"))
        assertEquals(parse("100"), parse("1e2"))
        assertEquals(parse("0.00000001"), parse("1e-8"))
        assertNotEquals(parse("1e-8"), parse("1e-9"))
    }

    @Test fun exponentsLargerThanIntAndLongDoNotOverflowOrRequirePowerExpansion() {
        for (exponent in listOf(BigInteger("2147483648"), BigInteger("-2147483649"),
            BigInteger("9223372036854775808"), BigInteger("-9223372036854775809"),
            BigInteger("9".repeat(1000)))) {
            val expected = parse("3e$exponent")!!
            assertEquals(exponent, expected.exponent)
            assertEquals(expected, parse("30e${exponent - BigInteger.ONE}"))
            assertNotEquals(expected, parse("3e${exponent + BigInteger.ONE}"))
            assertNotEquals(parse("3"), expected)
        }
    }

    @Test fun zeroHasOneCanonicalValueRegardlessOfExponentAndSeparators() {
        for (answer in listOf("0", "0.0", "000.000", ".0", "0e0", "0e99999999999999999999",
            "0e-99999999999999999999", "0'0.0'0e+1'000", "0e${"9".repeat(1000)}")) {
            assertEquals(answer, parse("0"), parse(answer))
        }
        assertEquals(BigInteger.ZERO, parse("0e2147483648")!!.exponent)
    }

    @Test fun parserLimitIsInclusiveAndAppliesAfterExternalTrimming() {
        val answer = "3." + "0".repeat(CppDecimalLiteral.MAX_LENGTH - 2)
        assertEquals(CppDecimalLiteral.MAX_LENGTH, answer.length)
        assertEquals(parse("3"), parse(answer))
        assertEquals(parse("3"), parse(" \t$answer\r\n "))
    }

    @Test fun exceedingParserLimitRaisesATechnicalFailureInsteadOfReturningNull() {
        for (answer in listOf("3." + "0".repeat(CppDecimalLiteral.MAX_LENGTH - 1),
            "x".repeat(CppDecimalLiteral.MAX_LENGTH + 1))) {
            try {
                parse(answer)
                fail("A resource limit must not become a correct or incorrect answer")
            } catch (_: CppDecimalLiteral.ResourceLimitException) {
                // The renderer can distinguish this technical state from invalid student syntax.
            }
        }
    }

    private fun parse(answer: String): CppDecimalLiteral.Value? = CppDecimalLiteral.parse(answer)
}
