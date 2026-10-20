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
}
