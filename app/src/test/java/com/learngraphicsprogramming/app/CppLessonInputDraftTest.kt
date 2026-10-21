package com.learngraphicsprogramming.app

import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class CppLessonInputDraftTest {
    @Test fun preparedInputRoundTripsEverySignificantCharacterWithoutRawXmlNewlines() {
        for (input in listOf("Rita 4", "Leo\n2\n", "Leo\n2\n    ", "", "\n", "\n\n",
            "  Rita\t4\r\n", "\t\n    ", "ñ é λ 中文 & < > \" \\\n", "\u0000\u0001\u000B\u000C\n")) {
            val stored = CppDraftTextCodec.encode(input, "input")
            assertFalse(stored.contains('\n'))
            assertFalse(stored.contains('\r'))
            assertEquals(input, CppLessonInputDraft.decode(stored, CppDraftTextCodec.ENCODING))
        }
    }

    @Test fun unmarkedLegacyInputIsNeverTrimmedOrInterpretedAsAnEnvelope() {
        for (input in listOf("Leo\n2\n", "Leo\n2\n    ", "\n        ", "",
            "{\"version\":1,\"input\":\"ordinary user data\"}"))
            assertEquals(input, CppLessonInputDraft.decode(input, null))
    }

    @Test fun manualTrailingSpacesAndLineBreaksAreKeptExactly() {
        val base = "Leo\n2\n"
        for (suffix in listOf("", " ", "    ", "        ", "\t", "\n", "\r\n")) {
            val input = base + suffix
            assertEquals(input, CppLessonInputDraft.decode(CppDraftTextCodec.encode(input, "input"),
                CppDraftTextCodec.ENCODING))
        }
    }

    @Test fun unknownAndDamagedFormatsPreserveTheStoredValue() {
        val raw = "Leo\n2\n    "
        assertEquals(raw, CppLessonInputDraft.decode(raw, "future"))
        for (stored in listOf(raw, "{}", "{\"version\":2,\"input\":\"another\"}",
            "{\"version\":1,\"input\":3}", "{\"version\":1,\"input\":null}"))
            assertEquals(stored, CppLessonInputDraft.decode(stored, CppDraftTextCodec.ENCODING))
    }

    @Test fun sourceAndInputRetainTheirDistinctVersionOneFieldsInTheSharedCodec() {
        val source = "int main() {}\n"
        val input = "Leo\n2\n"
        val sourceStored = CppLessonSourceDraft.encode(source)
        val inputStored = CppDraftTextCodec.encode(input, "input")
        assertEquals(source, JSONObject(sourceStored).getString("code"))
        assertEquals(input, JSONObject(inputStored).getString("input"))
        assertEquals(source, CppLessonSourceDraft.decode(sourceStored, CppLessonSourceDraft.ENCODING, null))
        assertEquals(sourceStored, CppLessonInputDraft.decode(sourceStored, CppDraftTextCodec.ENCODING))
        assertEquals(inputStored, CppLessonSourceDraft.decode(inputStored, CppDraftTextCodec.ENCODING, null))
    }

    @Test fun checksumUsesTheStandardSha256RatherThanAnInputNormalization() {
        assertEquals("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
            CppLessonInputDraft.checksum(""))
        assertEquals("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            CppLessonInputDraft.checksum("abc"))
        val variants = listOf("Leo\n2\n", "Leo\n2\n    ", "Leo\n2\r\n", "Leo\n2\n\t")
        assertEquals(variants.size, variants.map(CppLessonInputDraft::checksum).toSet().size)
    }

    @Test fun onlyAPreexistingDigestOfTheExactOriginalProvesRemovalOfFourInsertedSpaces() {
        for (original in listOf("Leo\n2\n", "\n", "  Rita\t4\r\n", "ñ é λ 中文\n")) {
            val damaged = original + "    "
            val proof = CppLessonInputDraft.checksum(original)
            assertEquals(original, CppLessonInputDraft.recoverProvedInput(damaged, null, proof))
            assertEquals(original, CppLessonInputDraft.recoverProvedInput(
                CppDraftTextCodec.encode(damaged, "input"), CppDraftTextCodec.ENCODING, proof))
            assertTrue("The mismatch remains unresolved until the proved original is actually persisted",
                CppLessonInputDraft.needsReview(damaged, proof))
        }
    }

    @Test fun aDigestOfDeliberateTrailingSpacesNeverAuthorizesRemovingThem() {
        for (manual in listOf("Leo\n2\n    ", "\n    ", "  Rita\t4\r\n    ", "ñ é λ 中文\n    ")) {
            val proofOfManualInput = CppLessonInputDraft.checksum(manual)
            assertNull(CppLessonInputDraft.recoverProvedInput(manual, null, proofOfManualInput))
            assertNull(CppLessonInputDraft.recoverProvedInput(CppDraftTextCodec.encode(manual, "input"),
                CppDraftTextCodec.ENCODING, proofOfManualInput))
            assertFalse(CppLessonInputDraft.needsReview(manual, proofOfManualInput))
        }
    }

    @Test fun absentWrongOrMalformedDigestsCannotProveThatAmbiguousSpacesWereInserted() {
        val original = "Leo\n2\n"
        val ambiguous = original + "    "
        val wrongOriginal = CppLessonInputDraft.checksum("different input\n")
        for (unproved in listOf(null, "", "not-a-digest", "0".repeat(64), wrongOriginal,
            CppLessonInputDraft.checksum(original).uppercase())) {
            assertNull(CppLessonInputDraft.recoverProvedInput(ambiguous, null, unproved))
            assertNull(CppLessonInputDraft.recoverProvedInput(CppDraftTextCodec.encode(ambiguous, "input"),
                CppDraftTextCodec.ENCODING, unproved))
            assertTrue("The old value still requires an explicit owner decision",
                CppLessonInputDraft.needsReview(ambiguous, unproved))
            assertEquals(ambiguous, CppLessonInputDraft.decode(ambiguous, null))
        }
    }

    @Test fun anEscapedEnvelopeDoesNotTurnInheritedAmbiguityIntoProofOfUserIntent() {
        val ambiguous = "Leo\n2\n    "
        val inherited = CppDraftTextCodec.encode(ambiguous, "input")
        val decoded = CppLessonInputDraft.decode(inherited, CppDraftTextCodec.ENCODING)
        assertEquals(ambiguous, decoded)
        assertTrue(CppLessonInputDraft.needsReview(decoded, null))
        assertNull(CppLessonInputDraft.recoverProvedInput(inherited, CppDraftTextCodec.ENCODING, null))
        assertFalse("An exact confirmed value may retain all four deliberate spaces",
            CppLessonInputDraft.needsReview(decoded, CppLessonInputDraft.checksum(decoded)))
    }

    @Test fun unknownAndMalformedEnvelopesCannotBeRepairedEvenWhenAProvidedHashMatchesACandidate() {
        val original = "Leo\n2\n"
        val damaged = original + "    "
        val proof = CppLessonInputDraft.checksum(original)
        for (encoding in listOf("future", "", "JSON-V1")) {
            assertNull(CppLessonInputDraft.recoverProvedInput(damaged, encoding, proof))
            assertEquals(damaged, CppLessonInputDraft.decode(damaged, encoding))
        }
        for (stored in listOf(damaged, "{}", "{\"version\":2,\"input\":\"Leo\\n2\\n    \"}",
            "{\"version\":1,\"input\":3}", "{\"version\":1,\"input\":null}",
            CppDraftTextCodec.encode(damaged, "code"))) {
            assertNull(CppLessonInputDraft.recoverProvedInput(stored, CppDraftTextCodec.ENCODING, proof))
            assertEquals(stored, CppLessonInputDraft.decode(stored, CppDraftTextCodec.ENCODING))
        }
    }

    @Test fun proofRecoveryIsNotAGeneralWhitespaceTrimmerOrAnAssumptionAboutInternalIndentation() {
        val original = "Leo\n2\n"
        val proof = CppLessonInputDraft.checksum(original)
        val manualVariants = listOf(original, original + " ", original + "   ", original + "        ",
            original + "\t", original + "\r\n", "Leo\n    2\n", "Leo\n2\n    next", "    ")
        for (manual in manualVariants) {
            assertNull(CppLessonInputDraft.recoverProvedInput(manual, null, proof))
            val encoded = CppDraftTextCodec.encode(manual, "input")
            assertNull(CppLessonInputDraft.recoverProvedInput(encoded, CppDraftTextCodec.ENCODING, proof))
            assertEquals(manual, CppLessonInputDraft.decode(encoded, CppDraftTextCodec.ENCODING))
        }
    }
}
