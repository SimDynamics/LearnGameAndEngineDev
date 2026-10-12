package com.learngraphicsprogramming.app

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Test

class CppCodeAnswerNormalizerTest {
    @Test fun publishedCurricularLiteralsKeepOutsideWhitespaceToleranceAndRejectChangedMessages() {
        val answers = listOf(
            "\"Muelle cerrado\"",
            "\"Estacion abierta\\nAcceso por el norte\"",
            "\"Carga lista\""
        )
        for (answer in answers) {
            assertEquals(answer, normalize(answer))
            assertEquals(answer, normalize(" \t\n$answer\r\n "))
            for (replacement in listOf("  ", "\t", "\n", "\r\n")) {
                val changed = answer.replaceFirst(" ", replacement)
                assertNotEquals("Literal characters must be significant: $changed",
                    normalize(answer), normalize(changed))
            }
        }
    }

    @Test fun escapedNewlineDoesNotBecomeARealNewlineOrWhitespace() {
        val report = "\"Estacion abierta\\nAcceso por el norte\""
        for (replacement in listOf("\n", "\r\n", " ", "\t")) {
            assertNotEquals(normalize(report), normalize(report.replace("\\n", replacement)))
        }
    }

    @Test fun ordinaryLiteralsPreserveEveryWhitespaceCharacterIncludingEdges() {
        val literal = "\"  \tMuelle  cerrado\n\r\u000b\u000c \""
        assertEquals(literal, normalize("\t$literal\t"))
        assertNotEquals(normalize(literal), normalize("\"Muelle cerrado\""))
    }

    @Test fun escapedQuotesAndBackslashesDoNotEndTheLiteralEarly() {
        val literal = "\"Una \\\"zona  segura\\\" y \\\\fin\""
        val source = " \t$literal \t+\n \" otra  zona \" \n"
        assertEquals("$literal + \" otra  zona \"", normalize(source))
        assertNotEquals(normalize(source), normalize(source.replace("zona  segura", "zona segura")))
    }

    @Test fun anEvenNumberOfBackslashesAllowsTheClosingQuoteAndOutsideTolerance() {
        val literal = "\"ruta\\\\\""
        assertEquals("$literal + valor", normalize("$literal \t+\n valor"))
    }

    @Test fun characterLiteralsKeepSpacesTabsEscapesAndEscapedApostrophes() {
        for (literal in listOf("' '", "'\t'", "'\\t'", "'\\''", "'\\\\'")) {
            assertEquals(literal, normalize(" \n$literal \t"))
        }
        assertNotEquals(normalize("' '"), normalize("'\t'"))
        assertNotEquals(normalize("'\\n'"), normalize("'\n'"))
    }

    @Test fun encodingPrefixesAndAdjacentLiteralsArePreserved() {
        for (prefix in listOf("", "u8", "u", "U", "L")) {
            val literal = "$prefix\"  zona\tsegura  \""
            assertEquals("$literal + valor", normalize(" \n$literal \t+\n valor "))
            val character = "$prefix' '"
            assertEquals(character, normalize(" \t$character\n"))
        }
        assertEquals("\"uno  \" \" dos\"", normalize("\"uno  \" \n\t \" dos\""))
    }

    @Test fun rawStringBodiesRetainQuotesSlashesTabsAndMultilineWhitespace() {
        for (prefix in listOf("R", "u8R", "uR", "UR", "LR")) {
            val literal = "$prefix\"marca(  \"zona  segura\"\\\t\n\r\n )marca\""
            assertEquals("$literal + valor", normalize("\n$literal\t + \nvalor \t"))
            assertNotEquals(normalize(literal), normalize(literal.replace("zona  segura", "zona segura")))
        }
        val emptyDelimiter = "R\"(\"  texto\n\")\""
        assertEquals(emptyDelimiter, normalize(" \t$emptyDelimiter\n"))
    }

    @Test fun rawStringsOnlyCloseAtTheMatchingDelimiter() {
        val literal = "R\"fin(uno )otro\"  dos )\"\t tres)fin\""
        assertEquals("$literal + fin", normalize(" $literal \n + \t fin "))
        val maximumDelimiter = "R\"abcdefghijklmnop(  \n )abcdefghijklmnop\""
        assertEquals(maximumDelimiter, normalize(maximumDelimiter))
    }

    @Test fun numericDigitSeparatorsDoNotProtectFollowingOutsideWhitespace() {
        for (number in listOf("1'000", "1'000'000", "0xFF'AA", "0b1111'0000",
            "1.2'34e+1'0", ".1'2", "0x1'f.fp-2")) {
            assertEquals("$number + valor", normalize(" \t$number \n + \t valor \n"))
        }
        assertEquals("1'000 + ' '", normalize("1'000 \t+\n ' '"))
    }

    @Test fun quotesInCommentsDoNotChangeThePreviousWhitespaceTolerance() {
        val blockComment = " /* una \"  cita  ' abierta */ \n valor \t + 1 "
        val lineComment = " // una \"  cita  ' abierta \n \"zona  segura\" \t + valor "
        assertEquals(oldNormalize(blockComment), normalize(blockComment))
        assertEquals("// una \" cita ' abierta \"zona  segura\" + valor", normalize(lineComment))
    }

    @Test fun operatorAndTypeAnswersRetainTheOldNormalizationExactly() {
        for (answer in listOf("", " \t\n", "&&", " and ", "\t||\n", "or", "==", "!=",
            "not_eq", "push_back", "emplace_back", "9", "valor", " long \t long \n int ",
            "std::vector<int> \t valores", "a\u000bb\u000cc", "\u00a0 a\u00a0\tb \u00a0")) {
            assertEquals("Prior answer tolerance must be preserved: $answer",
                oldNormalize(answer), normalize(answer))
        }
    }

    @Test fun incompleteLiteralsArePreservedRatherThanRepaired() {
        val ordinary = "\"Muelle  cerrado \t"
        val raw = "R\"tag(Muelle  cerrado \t\n"
        assertEquals(ordinary, normalize(ordinary))
        assertEquals(raw, normalize(raw))
        assertNotEquals(normalize("\"Muelle cerrado\""), normalize(ordinary))
    }

    @Test fun normalizationIsIdempotentForMixedCppFragments() {
        val source = " \n std::cout \t << u8R\"tag( a  b\n\"c\")tag\" \n << '\\n' \t + 1'000; "
        assertEquals(normalize(source), normalize(normalize(source)))
    }

    @Test fun inlineHoleInsidePublishedTokenStringKeepsEveryInsertedCharacter() {
        val template = "Token t{nombre, [], \"[]\"};\n"
        val correct = listOf("0", "Mesa9")
        assertEquals(correct, CppCodeAnswerNormalizer.normalizeInline(template, listOf(" \t0\n", "Mesa9")))
        for (answer in listOf(" Mesa9", "Mesa9 ", "\tMesa9", "Mesa9\t", "Mesa9\n", "Mesa 9")) {
            assertNotEquals(correct,
                CppCodeAnswerNormalizer.normalizeInline(template, listOf("0", answer)))
        }
    }

    @Test fun inlineHolesOutsideQuotesStillNormalizeFullLiteralAnswers() {
        val template = "std::cout [] [];"
        val correct = listOf("<<", "\"Carga lista\"")
        assertEquals(correct, CppCodeAnswerNormalizer.normalizeInline(template,
            listOf("\t<<\n", " \t\"Carga lista\"\n")))
        assertNotEquals(correct, CppCodeAnswerNormalizer.normalizeInline(template,
            listOf("<<", "\"Carga  lista\"")))
    }

    @Test fun fillHolesInExistingCharacterAndFormatLiteralsRejectBoundaryWhitespace() {
        for ((code, answer) in listOf(
            "std::setfill('___')" to "0",
            "std::format(\"{:04___}\", 8)" to "d",
            "std::format(\"[{:7.___f}]\", 2.0)" to "2",
            "std::format(\"{1}:{0}:{___}\", \"R\", 5)" to "0"
        )) {
            assertEquals(answer, CppCodeAnswerNormalizer.normalizeFill(code, answer))
            for (changed in listOf(" $answer", "$answer ", "\t$answer", "$answer\t", "$answer\n")) {
                assertNotEquals(answer, CppCodeAnswerNormalizer.normalizeFill(code, changed))
            }
        }
    }

    @Test fun fillHoleOutsideLiteralKeepsTokenAndWholeLiteralWhitespaceTolerance() {
        val code = "std::cout << ___;"
        assertEquals("long long", CppCodeAnswerNormalizer.normalizeFill(code, "\tlong \n long "))
        assertEquals("\"Muelle cerrado\"",
            CppCodeAnswerNormalizer.normalizeFill(code, " \"Muelle cerrado\"\n"))
        assertNotEquals("\"Muelle cerrado\"",
            CppCodeAnswerNormalizer.normalizeFill(code, "\"Muelle  cerrado\""))
        assertEquals(";", CppCodeAnswerNormalizer.normalizeFill("std::cout << \"Caja 8\"___", " \t;\n"))
    }

    @Test fun templateContextsUnderstandRawStringsEscapesCommentsAndDigitSeparators() {
        val template = "1'000 + [] + R\"tag(\"[]\")tag\" + '\\'' + \"\\\"[]\"; /* \" */ []"
        val answers = listOf(" \tvalor\n", " a  b\t\n", " c  d\n", " \tand ")
        assertEquals(listOf("valor", " a  b\t\n", " c  d\n", "and"),
            CppCodeAnswerNormalizer.normalizeInline(template, answers))
        assertEquals(" \t", CppCodeAnswerNormalizer.normalizeFill("R\"(___)\"", " \t"))
        assertEquals("long long", CppCodeAnswerNormalizer.normalizeFill("/* \"___ */ ___", "long\tlong"))
    }

    @Test fun repeatedFillHolesUseLiteralContextIfAnyOccurrenceIsQuoted() {
        assertEquals(" 0 ", CppCodeAnswerNormalizer.normalizeFill("___ + \"___\"", " 0 "))
        assertEquals("0", CppCodeAnswerNormalizer.normalizeFill("___ + ___", " 0 "))
    }

    private fun normalize(text: String) = CppCodeAnswerNormalizer.normalize(text)

    private fun oldNormalize(text: String) = text.trim().replace(Regex("\\s+"), " ")
}
