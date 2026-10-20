package com.learngraphicsprogramming.app

import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import java.security.MessageDigest

class CppLessonSourceDraftTest {
    @Test fun versionedStorageKeepsEverySourceCharacterWithoutLiteralXmlNewlines() {
        for (source in listOf("", "\n", "\n\n", "int main() {}\n", "    \n    ", "\t\tvalue;\r\n",
            "// ñ, é, λ, 中文\r\n\"quoted\" & < > \\n\n", "\u0000\u0001\u000B\u000C\n")) {
            val encoded = CppLessonSourceDraft.encode(source)
            assertFalse(encoded.contains('\n'))
            assertFalse(encoded.contains('\r'))
            assertEquals(source, CppLessonSourceDraft.decode(encoded, CppLessonSourceDraft.ENCODING, null))
        }
    }

    @Test fun unmarkedOldDraftsStayLiteralEvenWhenTheyResembleJson() {
        for (source in listOf("int main() {}\n", "\n    ", "{\"version\":1,\"code\":\"not a wrapper\"}"))
            assertEquals(source, CppLessonSourceDraft.decode(source, null, null))
    }

    @Test fun legacySerializerIndentationIsRecoveredOnlyWhenOriginalShaProvesIt() {
        val source = "#include <iostream>\n\nint main() {\n    std::cout << \"Zona inicial\";\n    return 0;\n}\n"
        assertEquals("205376ab4f9630f9d5a1f1bbda06f814f08d3b8100d4ab1c7b91b19fd12fd25e", sha(source))
        assertEquals(source, CppLessonSourceDraft.decode(source + "    ", null, metadata(source)))
        assertEquals(source, CppLessonSourceDraft.decode(source, null, metadata(source)))
        assertEquals(source.replace("\n", "\r\n"), CppLessonSourceDraft.decode(
            source.replace("\n", "\r\n") + "    ", null, metadata(source.replace("\n", "\r\n"))))
    }

    @Test fun manualSpacesRemainExactWhenTheirOwnRecordedShaMatches() {
        for (source in listOf("\n    ", "\n        ", "\r\n    ", "\t\n    ", "    value;\n    ")) {
            assertEquals(source, CppLessonSourceDraft.decode(source, null, metadata(source)))
            assertEquals(source, CppLessonSourceDraft.decode(CppLessonSourceDraft.encode(source), CppLessonSourceDraft.ENCODING,
                metadata(source.dropLast(4))))
        }
    }

    @Test fun missingStaleOrMalformedProofNeverRemovesWhitespace() {
        val original = "int main() {}\n"
        val stored = original + "    "
        for (proof in listOf(null, "", "invalid", "{}", metadata("another program\n"),
            "{\"version\":2,\"sha256\":\"${sha(original)}\"}",
            "{\"version\":1,\"sha256\":123}"))
            assertEquals(stored, CppLessonSourceDraft.decode(stored, null, proof))
        for (different in listOf(original + " ", original + "        ", original + "\t", "changed\n    "))
            assertEquals(different, CppLessonSourceDraft.decode(different, null, metadata(original)))
    }

    @Test fun unknownOrDamagedStorageVersionNeverDiscardsItsExistingValue() {
        val raw = "int main() {}\n    "
        assertEquals(raw, CppLessonSourceDraft.decode(raw, "future-version", metadata(raw.dropLast(4))))
        for (stored in listOf(raw, "{}", "{\"version\":2,\"code\":\"different\"}",
            "{\"version\":1,\"code\":3}", "{\"version\":1,\"code\":null}"))
            assertEquals(stored, CppLessonSourceDraft.decode(stored, CppLessonSourceDraft.ENCODING, null))
    }

    private fun metadata(source: String): String = JSONObject().put("version", 1).put("sha256", sha(source)).toString()
    private fun sha(source: String): String = MessageDigest.getInstance("SHA-256").digest(source.toByteArray(Charsets.UTF_8))
        .joinToString("") { "%02x".format(it.toInt() and 0xff) }
}
