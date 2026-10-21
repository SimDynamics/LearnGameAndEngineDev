package com.learngraphicsprogramming.app

import android.content.Context
import android.graphics.Typeface
import android.os.Build
import android.os.Process
import android.text.InputType
import android.util.Log
import android.util.Xml
import android.view.ContextThemeWrapper
import android.view.inputmethod.EditorInfo
import android.widget.EditText
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.xmlpull.v1.XmlPullParser
import java.io.File
import java.io.StringReader
import java.util.UUID

/** Only uniquely named QA preferences are used. The two explicitly named process
 * phases must run in separate instrumentation invocations; the second asserts a
 * different PID before reading Android's persisted file and restoring native text.
 */
@RunWith(AndroidJUnit4::class)
class CppPreparedInputPersistenceDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val key = "programming/cpp_fundamentals__objetos_y_estado__leer_una_entrada_conocida/6/2"
    private val prefix = "cpp_input_draft_qa_"

    @Test fun actualPreferenceWriterChecksLegacyAmbiguityAndKeepsEncodedInputDistinct() {
        requireQa()
        val names = mutableListOf<String>()
        try {
            val original = "Leo\n2\n"
            val manual = original + "    "
            fun committedRawXml(input: String): String {
                val name = newName(names)
                val preferences = context.getSharedPreferences(name, Context.MODE_PRIVATE)
                assertTrue(preferences.edit().putString("$key/input", input).commit())
                assertEquals("The live value is exact before Android serializes and reloads it", input,
                    preferences.getString("$key/input", null))
                return File(context.applicationInfo.dataDir, "shared_prefs/$name.xml").readText()
            }
            val originalXml = committedRawXml(original)
            val manualXml = committedRawXml(manual)
            val originalOnDisk = inputFromPreferenceXml(originalXml)
            val manualOnDisk = inputFromPreferenceXml(manualXml)
            assertEquals("Deliberately typed spaces must remain data", manual, manualOnDisk)
            when (originalOnDisk) {
                manual -> {
                    assertEquals("The historical defect makes different original inputs byte-identical on disk",
                        originalXml, manualXml)
                    assertEquals(10, originalOnDisk.length)
                    Log.i("CppInputPersistence", "Actual preference writer reproduces LF closing-tag indentation; " +
                        "the six-character input and deliberate ten-character input have identical XML")
                }
                original -> {
                    assertNotEquals("An unaffected Android writer keeps the two inputs distinct", originalXml, manualXml)
                    Log.i("CppInputPersistence", "This Android preference writer keeps the raw six-character input exact; " +
                        "the historical closing-tag defect is not reproduced on this device")
                }
                else -> fail("Android changed legacy stdin in an unexpected way: ${JSONObject.quote(originalOnDisk)}")
            }

            // Inspect actual committed bytes rather than the preferences memory cache.
            // No synthetic spaces, copied XML fixtures or hidden Android APIs are used.
            val encodedName = newName(names)
            val encodedPreferences = context.getSharedPreferences(encodedName, Context.MODE_PRIVATE)
            val encodedFile = File(context.applicationInfo.dataDir, "shared_prefs/$encodedName.xml")
            val encodedValues = mutableSetOf<String>()
            for (input in listOf(original, manual, "", "\n", "\n\n", "\t\n    ",
                "  Rita\t4\r\n", "ñ é λ 中文 & < > \" \\\n")) {
                assertTrue(CppLessonInputDraft.write(encodedPreferences.edit(), key, input).commit())
                val storedOnDisk = inputFromPreferenceXml(encodedFile.readText())
                assertFalse("The prepared-input envelope cannot expose a final LF to the XML writer",
                    storedOnDisk.contains('\n'))
                assertEquals(input, CppLessonInputDraft.decode(storedOnDisk, CppDraftTextCodec.ENCODING))
                assertTrue("Every distinct original input must retain a distinct persisted envelope",
                    encodedValues.add(storedOnDisk))
            }
        } finally { names.forEach { context.deleteSharedPreferences(it) } }
    }

    @Test fun coldXmlReproducesLegacyIndentationAndProtectsNewInputWithoutGuessingManualSpaces() {
        requireQa()
        val names = mutableListOf<String>()
        try {
            val original = "Leo\n2\n"
            val raw = vendorPreferences(mapOf("$key/input" to original), names)
            assertEquals("Legacy vendor XML reproduces the recorded ten-character value", original + "    ",
                raw.getString("$key/input", null))
            assertEquals("No old SHA exists for stdin: never discard potentially manual spaces", original + "    ",
                CppLessonInputDraft.read(raw, key, "unused"))
            assertFalse("Protecting unproved legacy data must not invent an original checksum",
                raw.contains("$key/inputSha256"))
            val observations = JSONArray()
            for (input in listOf(original, original + "    ", "", "\n", "  Rita\t4\r\n", "ñ é λ 中文\n")) {
                val preferences = context.getSharedPreferences(newName(names), Context.MODE_PRIVATE)
                assertTrue(CppLessonInputDraft.write(preferences.edit(), key, input).commit())
                assertEquals("One input value, its format marker and digest; no duplicate draft", 3, preferences.all.size)
                val encoded = requireNotNull(preferences.getString("$key/input", null))
                assertFalse(encoded.contains('\n'))
                assertEquals(input, JSONObject(encoded).getString("input"))
                val coldVendor = vendorPreferences(preferences.all.mapValues { it.value as String }, names)
                assertEquals(encoded, coldVendor.getString("$key/input", null))
                assertEquals(input, CppLessonInputDraft.read(coldVendor, key, "unused"))
                observations.put(JSONObject().put("characters", input.length).put("exactColdVendorXmlRead", true))
            }
            evidence("cold-vendor-input.json", JSONObject().put("device", Build.MODEL)
                .put("legacyRawCharacters", 10).put("legacyWithoutProofPreserved", true)
                .put("fixture", "Recorded XML closing-tag indentation parsed by Android SharedPreferences")
                .put("cases", observations))
        } finally { names.forEach { context.deleteSharedPreferences(it) } }
    }

    @Test fun aProvedLegacyRepairIsPersistedWithoutChangingSourceOrNeighboringDrafts() {
        requireQa()
        val names = mutableListOf<String>()
        try {
            val name = newName(names)
            val preferences = context.getSharedPreferences(name, Context.MODE_PRIVATE)
            val original = "Leo\n2\n"
            val source = CppLessonSourceDraft.encode("int main() { return 0; }\n")
            val neighbor = "$key/neighbor"
            val editor = preferences.edit()
                .putString("$key/input", original + "    ")
                .putString("$key/inputSha256", CppLessonInputDraft.checksum(original))
                .putString("$key/source", source)
                .putString("$key/sourceEncoding", CppDraftTextCodec.ENCODING)
                .putString("$key/sourceIndentation_v2", "unchanged-source-metadata")
                .putInt("student-progress", 37)
            assertTrue(CppLessonInputDraft.write(editor, neighbor, "\t\r\n    ").commit())
            val untouched = preferences.all.filterKeys { !it.startsWith("$key/input") }

            assertEquals(original, CppLessonInputDraft.read(preferences, key, "unused"))
            assertEquals("Repair must be stored, not just shown in the editor", CppDraftTextCodec.encode(original, "input"),
                preferences.getString("$key/input", null))
            assertEquals(CppDraftTextCodec.ENCODING, preferences.getString("$key/inputEncoding", null))
            assertEquals(CppLessonInputDraft.checksum(original), preferences.getString("$key/inputSha256", null))
            assertEquals(untouched, preferences.all.filterKeys { !it.startsWith("$key/input") })
            val persisted = File(context.applicationInfo.dataDir, "shared_prefs/$name.xml").readText()
            assertEquals(original, CppLessonInputDraft.decode(inputFromPreferenceXml(persisted), CppDraftTextCodec.ENCODING))
            assertEquals(source, inputFromPreferenceXml(persisted, "$key/source"))
            assertEquals("\t\r\n    ", CppLessonInputDraft.decode(inputFromPreferenceXml(persisted, "$neighbor/input"),
                CppDraftTextCodec.ENCODING))
            val afterRepair = preferences.all.toMap()
            assertEquals(original, CppLessonInputDraft.read(preferences, key, "unused"))
            assertEquals("A completed repair is idempotent", afterRepair, preferences.all)
        } finally { names.forEach { context.deleteSharedPreferences(it) } }
    }

    @Test fun aLegacyDigestOfManualSpacesProtectsAllTenCharacters() {
        requireQa()
        val names = mutableListOf<String>()
        try {
            val name = newName(names)
            val preferences = context.getSharedPreferences(name, Context.MODE_PRIVATE)
            val manual = "Leo\n2\n    "
            val checksum = CppLessonInputDraft.checksum(manual)
            assertTrue(preferences.edit().putString("$key/input", manual)
                .putString("$key/inputSha256", checksum).commit())
            assertEquals(manual, CppLessonInputDraft.read(preferences, key, "unused"))
            assertEquals(3, preferences.all.size)
            assertEquals(checksum, preferences.getString("$key/inputSha256", null))
            val encoded = inputFromPreferenceXml(File(context.applicationInfo.dataDir, "shared_prefs/$name.xml").readText())
            assertEquals(manual, CppLessonInputDraft.decode(encoded, CppDraftTextCodec.ENCODING))
        } finally { names.forEach { context.deleteSharedPreferences(it) } }
    }

    @Test fun bothLegacyFormatsPreserveUnprovedWhitespaceExactlyWithoutInventingADigest() {
        requireQa()
        val names = mutableListOf<String>()
        try {
            for (encoding in listOf(null, CppDraftTextCodec.ENCODING)) {
                for (input in listOf("Leo\n2\n    ", " \t ", "  Patio norte  \nSala azul", "\t\r\n    ")) {
                    val name = newName(names)
                    val preferences = context.getSharedPreferences(name, Context.MODE_PRIVATE)
                    val editor = preferences.edit().putString("$key/input",
                        if (encoding == null) input else CppDraftTextCodec.encode(input, "input"))
                    if (encoding != null) editor.putString("$key/inputEncoding", encoding)
                    assertTrue(editor.commit())
                    assertEquals("A legacy whitespace pattern alone authorizes no deletion", input,
                        CppLessonInputDraft.read(preferences, key, "unused"))
                    assertEquals(setOf("$key/input", "$key/inputEncoding"), preferences.all.keys)
                    assertFalse("The old original remains unproved", preferences.contains("$key/inputSha256"))
                    val protected = preferences.all.toMap()
                    assertEquals(input, CppLessonInputDraft.read(preferences, key, "unused"))
                    assertEquals("Repeated reads do not alter any characters or metadata", protected, preferences.all)
                    assertTrue(preferences.edit().putInt("unrelated-progress", 37).commit())
                    val encoded = inputFromPreferenceXml(File(context.applicationInfo.dataDir, "shared_prefs/$name.xml").readText())
                    assertEquals(input, CppLessonInputDraft.decode(encoded, CppDraftTextCodec.ENCODING))
                }
            }
        } finally { names.forEach { context.deleteSharedPreferences(it) } }
    }

    @Test fun newEditsRecordTheirExactWhitespaceAndChecksumWithoutChangingNeighboringData() {
        requireQa()
        val names = mutableListOf<String>()
        try {
            val name = newName(names)
            val preferences = context.getSharedPreferences(name, Context.MODE_PRIVATE)
            val source = CppLessonSourceDraft.encode("int main() { return 0; }\n")
            val sidecar = "unchanged-source-metadata"
            assertTrue(preferences.edit().putString("$key/input", "Leo\n2\n    ")
                .putString("$key/source", source).putString("$key/sourceEncoding", CppDraftTextCodec.ENCODING)
                .putString("$key/sourceIndentation_v2", sidecar).putInt("student-progress", 37).commit())
            assertEquals("Leo\n2\n    ", CppLessonInputDraft.read(preferences, key, "unused"))
            val untouched = preferences.all.filterKeys { !it.startsWith("$key/input") }
            for (newInput in listOf("Leo\n3\n    ", " \t ", "  Patio norte  \nSala azul", "\t\r\n    ", "\n\t", "\r\n", "")) {
                assertTrue(CppLessonInputDraft.write(preferences.edit(), key, newInput).commit())
                assertEquals(newInput, CppLessonInputDraft.read(preferences, key, "unused"))
                assertEquals(CppLessonInputDraft.checksum(newInput), preferences.getString("$key/inputSha256", null))
                assertEquals("Newly typed whitespace is stored exactly, not inferred from its shape", 3,
                    preferences.all.keys.count { it.startsWith("$key/input") })
                assertEquals(untouched, preferences.all.filterKeys { !it.startsWith("$key/input") })
                val persisted = File(context.applicationInfo.dataDir, "shared_prefs/$name.xml").readText()
                assertEquals(newInput, CppLessonInputDraft.decode(inputFromPreferenceXml(persisted), CppDraftTextCodec.ENCODING))
                assertEquals(source, inputFromPreferenceXml(persisted, "$key/source"))
                assertEquals(sidecar, inputFromPreferenceXml(persisted, "$key/sourceIndentation_v2"))
            }
        } finally { names.forEach { context.deleteSharedPreferences(it) } }
    }

    @Test fun protectingAllLegacyInputsPreventsUnrelatedPreferenceWritesFromDamagingUnopenedDrafts() {
        requireQa()
        val names = mutableListOf<String>()
        try {
            val name = newName(names)
            val preferences = context.getSharedPreferences(name, Context.MODE_PRIVATE)
            val original = "Leo\n2\n"
            val ambiguous = "\tRita\r\n4\n    "
            val unopened = "$key/unopened"
            val future = "$key/future"
            val alreadyEncoded = "$key/encoded"
            val futureValue = JSONObject().put("version", 2).put("input", "opaque\nfuture\tdata").toString()
            val source = CppLessonSourceDraft.encode("int main() { return 0; }\n")
            val editor = preferences.edit()
                .putString("$key/input", original)
                .putString("$key/inputSha256", CppLessonInputDraft.checksum(original))
                .putString("$unopened/input", ambiguous)
                .putString("$future/input", futureValue)
                .putString("$future/inputEncoding", "future-v2")
                .putString("$key/source", source)
                .putString("$key/sourceEncoding", CppDraftTextCodec.ENCODING)
                .putString("answer/neighbor", "unchanged answer")
            assertTrue(CppLessonInputDraft.write(editor, alreadyEncoded, "\r\n\t    ").commit())
            val before = preferences.all.toMap()
            assertEquals(original, preferences.getString("$key/input", null))
            assertTrue(CppLessonInputDraft.protectLegacyInputs(preferences))
            assertEquals("Only the two absent format markers may be added", before.size + 2, preferences.all.size)
            assertEquals(CppDraftTextCodec.ENCODING, preferences.getString("$key/inputEncoding", null))
            assertEquals(CppDraftTextCodec.ENCODING, preferences.getString("$unopened/inputEncoding", null))
            assertEquals(before["$key/inputSha256"], preferences.getString("$key/inputSha256", null))
            assertFalse("Protection must not invent evidence for an ambiguous old draft",
                preferences.contains("$unopened/inputSha256"))
            for ((storedKey, value) in before) {
                if (storedKey != "$key/input" && storedKey != "$unopened/input")
                    assertEquals("Unrelated data and known formats remain untouched at $storedKey", value,
                        preferences.all[storedKey])
            }
            assertEquals("Escaping old data must preserve its full original whitespace", ambiguous,
                CppLessonInputDraft.read(preferences, unopened, "unused"))
            assertEquals(original, CppLessonInputDraft.read(preferences, key, "unused"))
            val protectedValues = preferences.all.toMap()
            assertTrue(CppLessonInputDraft.protectLegacyInputs(preferences))
            assertEquals("A second protection pass does not alter any value", protectedValues, preferences.all)

            // The actual Android writer rewrites the complete map for this unrelated edit.
            assertTrue(preferences.edit().putString("unrelated-setting", "changed").commit())
            val persisted = File(context.applicationInfo.dataDir, "shared_prefs/$name.xml").readText()
            assertEquals(original, CppLessonInputDraft.decode(inputFromPreferenceXml(persisted), CppDraftTextCodec.ENCODING))
            assertEquals(ambiguous, CppLessonInputDraft.decode(inputFromPreferenceXml(persisted, "$unopened/input"),
                CppDraftTextCodec.ENCODING))
            assertEquals(futureValue, inputFromPreferenceXml(persisted, "$future/input"))
            assertEquals(source, inputFromPreferenceXml(persisted, "$key/source"))
            assertEquals("unchanged answer", inputFromPreferenceXml(persisted, "answer/neighbor"))
            assertEquals("\r\n\t    ", CppLessonInputDraft.decode(inputFromPreferenceXml(persisted, "$alreadyEncoded/input"),
                CppDraftTextCodec.ENCODING))
        } finally { names.forEach { context.deleteSharedPreferences(it) } }
    }

    @Test fun prepareNativeInputForFreshProcess() {
        requireQa()
        clearOwnPendingStage()
        val names = mutableListOf<String>()
        val cases = JSONArray()
        try {
            for (input in listOf("Rita 4", "Leo\n2\n", "Leo\n2\n    ", "", "\n\n", "  Rita\t4\r\n", "ñ é λ 中文\n")) {
                val name = newName(names)
                val preferences = context.getSharedPreferences(name, Context.MODE_PRIVATE)
                var nativeText = ""
                instrumentation.runOnMainSync {
                    val field = newInput()
                    val connection = requireNotNull(field.onCreateInputConnection(EditorInfo()))
                    try { assertTrue(connection.commitText(input, 1)) } finally { connection.closeConnection() }
                    nativeText = field.text.toString()
                    assertEquals("Native multiline input preserves the typed characters", input, nativeText)
                    assertTrue(CppLessonInputDraft.write(preferences.edit(), key, nativeText).commit())
                }
                cases.put(JSONObject().put("preferences", name).put("input", input)
                    .put("nativeBefore", nativeText).put("characters", nativeText.length)
                    .put("stored", preferences.getString("$key/input", null)))
            }
            evidence("cold-process-stage.json", JSONObject().put("pidBefore", Process.myPid())
                .put("device", Build.MODEL).put("cases", cases))
        } catch (error: Throwable) {
            names.forEach { context.deleteSharedPreferences(it) }
            throw error
        }
    }

    @Test fun verifyNativeInputInFreshProcess() {
        requireQa()
        val stageFile = File(evidenceDirectory(), "cold-process-stage.json")
        assertTrue("Run prepareNativeInputForFreshProcess in a separate invocation first", stageFile.isFile)
        val stage = JSONObject(stageFile.readText())
        val cases = stage.getJSONArray("cases")
        val names = (0 until cases.length()).map { cases.getJSONObject(it).getString("preferences") }
        assertTrue(names.all { it.startsWith(prefix) })
        try {
            assertNotEquals("This check must start a new Android process, not reuse the preferences cache",
                stage.getInt("pidBefore"), Process.myPid())
            for (index in 0 until cases.length()) {
                val item = cases.getJSONObject(index)
                val expected = item.getString("input")
                val preferences = context.getSharedPreferences(item.getString("preferences"), Context.MODE_PRIVATE)
                assertEquals(item.getString("stored"), preferences.getString("$key/input", null))
                assertEquals(expected, CppLessonInputDraft.read(preferences, key, "missing"))
                instrumentation.runOnMainSync {
                    val field = newInput()
                    field.setText(CppLessonInputDraft.read(preferences, key, "missing"))
                    assertEquals(item.getString("nativeBefore"), field.text.toString())
                    assertEquals(item.getInt("characters"), field.length())
                    item.put("nativeAfter", field.text.toString()).put("nativeAfterCharacters", field.length())
                }
            }
            evidence("cold-process-input-result.json", stage.put("pidAfter", Process.myPid())
                .put("newProcessVerified", true).put("everyNativeInputExact", true))
        } finally {
            names.forEach { context.deleteSharedPreferences(it) }
            stageFile.delete()
        }
    }

    private fun newInput(): EditText = EditText(ContextThemeWrapper(context, R.style.Theme_LearnGraphicsProgramming)).apply {
        inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_MULTI_LINE or InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
        imeOptions = EditorInfo.IME_FLAG_NO_EXTRACT_UI
        typeface = Typeface.MONOSPACE
        minLines = 1
    }

    private fun vendorPreferences(values: Map<String, String>, names: MutableList<String>): android.content.SharedPreferences {
        val name = newName(names)
        File(context.applicationInfo.dataDir, "shared_prefs").apply { mkdirs() }.resolve("$name.xml").writeText(buildString {
            append("<?xml version='1.0' encoding='utf-8' standalone='yes' ?>\n<map>\n")
            values.forEach { (key, value) ->
                append("    <string name=\"").append(xml(key)).append("\">").append(xml(value))
                if (value.endsWith('\n')) append("    ")
                append("</string>\n")
            }
            append("</map>\n")
        }, Charsets.UTF_8)
        return context.getSharedPreferences(name, Context.MODE_PRIVATE)
    }

    private fun xml(text: String): String = text.replace("&", "&amp;").replace("<", "&lt;")
        .replace(">", "&gt;").replace("\"", "&quot;")

    private fun inputFromPreferenceXml(xml: String, inputKey: String = "$key/input"): String {
        val parser = Xml.newPullParser()
        try {
            parser.setInput(StringReader(xml))
            while (parser.eventType != XmlPullParser.END_DOCUMENT) {
                if (parser.eventType == XmlPullParser.START_TAG && parser.name == "string" &&
                    parser.getAttributeValue(null, "name") == inputKey) return parser.nextText()
                parser.next()
            }
            error("The committed preferences XML has no prepared-input value")
        } finally { parser.setInput(null as java.io.Reader?) }
    }

    private fun newName(names: MutableList<String>): String = (prefix + UUID.randomUUID().toString().replace("-", ""))
        .also { names.add(it) }

    private fun evidenceDirectory(): File = File(context.getExternalFilesDir(null), "cpp-input-draft-qa").apply { mkdirs() }
    private fun evidence(name: String, value: JSONObject) = File(evidenceDirectory(), name).writeText(value.toString(2))

    private fun clearOwnPendingStage() {
        val file = File(evidenceDirectory(), "cold-process-stage.json")
        if (!file.isFile) return
        val cases = JSONObject(file.readText()).getJSONArray("cases")
        for (index in 0 until cases.length()) {
            val name = cases.getJSONObject(index).getString("preferences")
            require(name.startsWith(prefix))
            context.deleteSharedPreferences(name)
        }
        file.delete()
    }

    private fun requireQa() = assertTrue(context.packageName in setOf(
        "com.learngraphicsprogramming.app.curricularqa", "com.learngraphicsprogramming.app.realdownloadsqa"))
}
