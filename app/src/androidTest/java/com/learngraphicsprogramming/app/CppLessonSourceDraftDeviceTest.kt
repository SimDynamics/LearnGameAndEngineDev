package com.learngraphicsprogramming.app

import android.content.Context
import android.graphics.Typeface
import android.os.Build
import android.util.Log
import android.view.ContextThemeWrapper
import android.view.inputmethod.EditorInfo
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.security.MessageDigest
import java.util.UUID
import javax.xml.parsers.DocumentBuilderFactory
import org.w3c.dom.Element

/** Private, uniquely named QA preference files exercise a real cold Android XML read.
 * The legacy XML fixture reproduces the exact recorded FastXmlSerializer output;
 * this is not a claim that LDPlayer uses the Honor/OEM serializer implementation.
 */
@RunWith(AndroidJUnit4::class)
class CppLessonSourceDraftDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val names = mutableListOf<String>()
    private val key = "isolated/first-program"

    @Test fun coldLegacyVendorXmlRecoversOnlyTheSourceProvedByItsRecordedSha() = isolated {
        val initial = initialCode()
        val marked = CppIndentationUnits.loadedText(initial, seedPredefined = true)
        val sidecar = CppIndentationUnits.snapshot(marked)
        val preferences = vendorPreferences(mapOf("$key/source" to initial, "$key/sourceIndentation_v2" to sidecar))
        assertEquals("The historical serializer adds its closing-tag indentation to a cold value",
            initial + "    ", preferences.getString("$key/source", null))
        val coldRawLength = preferences.getString("$key/source", null)!!.length
        val restored = CppLessonSourceDraft.read(preferences, key, "unused fallback")
        assertEquals("The existing SHA proves the exact original, including its final LF", initial, restored)
        val loaded = CppIndentationUnits.loadedText(restored, preferences.getString("$key/sourceIndentation_v2", null))
        assertEquals(CppIndentationUnits.snapshot(marked), CppIndentationUnits.snapshot(loaded))

        val manual = initial + "    "
        val manualPreferences = vendorPreferences(mapOf("$key/source" to manual,
            "$key/sourceIndentation_v2" to CppIndentationUnits.snapshot(manual)))
        assertEquals("Manual trailing spaces are never inferred to be serializer indentation", manual,
            CppLessonSourceDraft.read(manualPreferences, key, "unused fallback"))
        val unproven = vendorPreferences(mapOf("$key/source" to initial))
        assertEquals("Without an existing SHA, preserve every character conservatively", initial + "    ",
            CppLessonSourceDraft.read(unproven, key, "unused fallback"))
        evidence("legacy-cold-read.json", JSONObject().put("device", Build.MODEL)
            .put("nativeColdRawLength", coldRawLength)
            .put("recoveredLength", restored.length).put("exactOriginalSource", restored == initial)
            .put("recordedIndentationRetained", CppIndentationUnits.snapshot(marked) == CppIndentationUnits.snapshot(loaded))
            .put("fixture", "Recorded FastXmlSerializer closing-tag indentation, loaded by Android SharedPreferences")
            .put("manualSpacesRetained", true).put("unprovedDataRetained", true))
    }

    @Test fun shaProvenLegacyRepairIsPersistedWithoutReplacingIndentationOrChangingManualData() = isolated {
        val initial = initialCode()
        val sidecar = CppIndentationUnits.snapshot(CppIndentationUnits.loadedText(initial, seedPredefined = true))
        val manual = initial + "    "
        val manualSidecar = CppIndentationUnits.snapshot(manual)
        val unknownKey = "isolated/unproven"
        val manualKey = "isolated/manual"
        val preferences = vendorPreferences(linkedMapOf(
            "$key/source" to initial,
            "$key/sourceIndentation_v2" to sidecar,
            "$manualKey/source" to manual,
            "$manualKey/sourceIndentation_v2" to manualSidecar,
            "$unknownKey/source" to initial,
            "unrelated" to "keep this value"
        ))
        val name = names.last()
        val before = preferences.all.toMap()
        assertEquals(initial + "    ", before["$key/source"])
        assertEquals(initial, CppLessonSourceDraft.read(preferences, key, "unused fallback"))
        assertEquals(CppLessonSourceDraft.ENCODING, preferences.getString("$key/sourceEncoding", null))
        assertEquals(initial, JSONObject(preferences.getString("$key/source", null)!!).getString("code"))
        assertEquals("The original recorded tab identities must remain byte exact", sidecar,
            preferences.getString("$key/sourceIndentation_v2", null))
        for ((otherKey, value) in before.filterKeys { it != "$key/source" }) {
            assertEquals("A repair only changes the proven source and its format marker: $otherKey", value,
                preferences.all[otherKey])
        }
        assertEquals(manual, CppLessonSourceDraft.read(preferences, manualKey, "unused fallback"))
        assertEquals("An already matching SHA proves these four spaces are user data", manualSidecar,
            preferences.getString("$manualKey/sourceIndentation_v2", null))
        assertNull(preferences.getString("$manualKey/sourceEncoding", null))
        assertEquals(initial + "    ", CppLessonSourceDraft.read(preferences, unknownKey, "unused fallback"))
        assertNull(preferences.getString("$unknownKey/sourceEncoding", null))

        val repaired = preferences.all.toMap()
        val file = File(preferenceDirectory(), "$name.xml")
        val committedBytes = file.readBytes()
        assertEquals(initial, CppLessonSourceDraft.read(preferences, key, "unused fallback"))
        assertEquals("Reading a repaired draft is idempotent", repaired, preferences.all)
        assertArrayEquals("A repeated read does not rewrite the XML", committedBytes, file.readBytes())

        // This is an isolated QA fixture, not a backup of any student preference.
        // Its fresh name forces Android to parse the actual committed bytes again.
        val coldName = newName()
        File(preferenceDirectory(), "$coldName.xml").writeBytes(committedBytes)
        val cold = context.getSharedPreferences(coldName, Context.MODE_PRIVATE)
        assertEquals(repaired, cold.all)
        assertEquals(initial, CppLessonSourceDraft.read(cold, key, "unused fallback"))
        assertEquals(sidecar, cold.getString("$key/sourceIndentation_v2", null))
        assertEquals(manual, CppLessonSourceDraft.read(cold, manualKey, "unused fallback"))
        assertEquals(initial + "    ", CppLessonSourceDraft.read(cold, unknownKey, "unused fallback"))
        assertEquals("All unrelated values also survive the actual cold XML read", "keep this value",
            cold.getString("unrelated", null))
    }

    /** Normal student data is touched only by an explicit instrumentation opt-in
     * on the selected x86 LDPlayer. All comparison snapshots remain in memory.
     */
    @Test fun bulkRepairOfUnopenedSourcesRequiresDigestProofAndPreservesEveryOtherPreference() {
        val optedIn = InstrumentationRegistry.getArguments().getString("repairProvenStudentSources") == "true"
        if (context.packageName == "com.learngraphicsprogramming.app") {
            assertTrue("Normal student storage requires explicit repairProvenStudentSources=true", optedIn)
            assertTrue("Student-data repair is restricted to the selected LDPlayer, never the Honor",
                Build.MODEL == "V2307A" && Build.SUPPORTED_ABIS.any { it == "x86" || it == "x86_64" })
            val preferenceName = "cpp_lesson_drafts"
            val file = File(context.applicationInfo.dataDir, "shared_prefs/$preferenceName.xml")
            assertTrue("The existing student preferences must already exist", file.isFile)
            val otherPreferences = existingOtherPreferenceValues(preferenceName)
            val result = assertBulkRepair(context.getSharedPreferences(preferenceName, Context.MODE_PRIVATE), preferenceName)
            assertEquals("No preferences outside compiler drafts may change", otherPreferences,
                existingOtherPreferenceValues(preferenceName))
            Log.i("CppSourceRepairVerification", "Student source cleanup verified: repaired=${result.first}; " +
                "protectedUnchanged=${result.second}; otherPreferencesUnchanged=true")
            return
        }
        assertFalse("The student-data opt-in must only be used for the normal application", optedIn)
        isolated {
            val name = newName()
            val preferences = context.getSharedPreferences(name, Context.MODE_PRIVATE)
            val first = initialCode()
            assertTrue(first.endsWith('\n'))
            val second = "int main() {\n\treturn 0;\n}\n"
            val protected = "int main() { return 2; }\n"
            val manual = first + "    "
            val editor = preferences.edit()
            for ((draft, original) in mapOf("$key/first" to first, "$key/second" to second)) {
                editor.putString("$draft/source", original + "    ")
                    .putString("$draft/sourceIndentation_v2", CppIndentationUnits.snapshot(original))
            }
            editor.putString("$key/protected/source", protected)
                .putString("$key/protected/sourceIndentation_v2", CppIndentationUnits.snapshot(protected))
                .putString("$key/manual/source", manual)
                .putString("$key/manual/sourceIndentation_v2", CppIndentationUnits.snapshot(manual))
                .putString("$key/unproven/source", first + "    ")
                .putString("$key/future/source", "opaque future data")
                .putString("$key/future/sourceEncoding", "future-v2")
                .putString("$key/encoded/source", CppLessonSourceDraft.encode(second))
                .putString("$key/encoded/sourceEncoding", CppLessonSourceDraft.ENCODING)
                .putString("$key/input", "unopened stdin\n")
                .putString("$key/ambiguous/input", "Leo\n2\n    ")
                .putString("neighbor-answer", "keep this answer")
                .putInt("student-progress", 37)
                .putBoolean("completed-neighbor", true)
            assertTrue(editor.commit())
            val otherPreferences = existingOtherPreferenceValues(name)
            val result = assertBulkRepair(preferences, name)
            assertEquals(2, result.first)
            assertEquals(1, result.second)
            assertEquals("Escaping legacy stdin preserves every character, without interpreting its spaces",
                "Leo\n2\n    ", CppLessonInputDraft.read(preferences, "$key/ambiguous", "unused"))
            assertEquals(otherPreferences, existingOtherPreferenceValues(name))
        }
    }

    @Test fun encodedDraftHasOneSourceAndSurvivesActualCommitAndBothColdXmlReaders() = isolated {
        val observations = JSONArray()
        val sources = listOf(initialCode(), "", "\n\n", "\t    return 0;\r\n", "// ñ é λ 中文\n    ",
            "const char* text = R\"tag(a\n    b\n)tag\";\n")
        for (source in sources) {
            val name = newName()
            val preferences = context.getSharedPreferences(name, Context.MODE_PRIVATE)
            val text = CppIndentationUnits.loadedText(source, seedPredefined = true)
            assertTrue(CppLessonSourceDraft.write(preferences.edit(), key, text).commit())
            val encoded = preferences.getString("$key/source", null)!!
            assertEquals("The existing source key is the only stored source, with no duplicate code field", 3, preferences.all.size)
            assertEquals(source, JSONObject(encoded).getString("code"))
            assertFalse(encoded.contains('\n'))
            assertEquals(source, CppLessonSourceDraft.read(preferences, key, "unused fallback"))

            // A different name bypasses SharedPreferences' in-memory cache and parses
            // the actual committed bytes again, as reopening a new process does.
            val coldName = newName()
            File(preferenceDirectory(), "$name.xml").copyTo(File(preferenceDirectory(), "$coldName.xml"))
            val cold = context.getSharedPreferences(coldName, Context.MODE_PRIVATE)
            assertEquals(source, CppLessonSourceDraft.read(cold, key, "unused fallback"))
            val legacy = vendorPreferences(preferences.all.mapValues { it.value as String })
            assertEquals(source, CppLessonSourceDraft.read(legacy, key, "unused fallback"))
            assertEquals("The old serializer cannot inject indentation inside escaped JSON",
                encoded, legacy.getString("$key/source", null))
            assertEquals(CppIndentationUnits.snapshot(text), legacy.getString("$key/sourceIndentation_v2", null))
            observations.put(JSONObject().put("characters", source.length).put("actualCommitColdReadExact", true)
                .put("legacyFormatColdReadExact", true).put("sourceCopies", 1))
        }
        evidence("encoded-cold-roundtrip.json", JSONObject().put("device", Build.MODEL).put("observations", observations))
    }

    @Test fun coldDraftRetainsAuthoredTabUnitsAndManualSpacesThroughNativeBackspace() = isolated {
        val observations = JSONArray()
        for (predefined in listOf(true, false)) {
            val source = "    std::cout << \"Zona inicial\";\n    return 0;\n"
            val text = CppIndentationUnits.loadedText(source, seedPredefined = predefined)
            val stored = vendorPreferences(mapOf("$key/source" to CppLessonSourceDraft.encode(source),
                "$key/sourceEncoding" to CppLessonSourceDraft.ENCODING,
                "$key/sourceIndentation_v2" to CppIndentationUnits.snapshot(text)))
            instrumentation.runOnMainSync {
                val editor = FreeScrollEditText(ContextThemeWrapper(context, R.style.Theme_LearnGraphicsProgramming)).apply {
                    setSingleLine(false)
                    typeface = Typeface.MONOSPACE
                    CppCodeInputRules.install(this)
                    setText(CppIndentationUnits.loadedText(CppLessonSourceDraft.read(stored, key, "unused fallback"),
                        stored.getString("$key/sourceIndentation_v2", null)))
                    setSelection(4)
                }
                val connection = requireNotNull(editor.onCreateInputConnection(EditorInfo()))
                try { assertTrue(connection.deleteSurroundingText(1, 0)) } finally { connection.closeConnection() }
                assertEquals(if (predefined) source.drop(4) else source.drop(1), editor.text.toString())
                assertEquals(if (predefined) 0 else 3, editor.selectionStart)
                observations.put(JSONObject().put("predefined", predefined).put("expectedCharactersDeleted", if (predefined) 4 else 1)
                    .put("nativeBackspaceRetainsOrigin", true))
            }
        }
        evidence("indentation-after-cold-read.json", JSONObject().put("device", Build.MODEL).put("observations", observations))
    }

    private fun initialCode(): String = LessonRepository.loadLessonJson(context, "programming",
        "cpp_fundamentals__programas_y_ejecucion__primer_programa_y_salida")
        .getJSONArray("pages").getJSONObject(4).getJSONArray("blocks").getJSONObject(2).getString("initialCode")

    private fun vendorPreferences(values: Map<String, String>): android.content.SharedPreferences {
        val name = newName()
        File(preferenceDirectory(), "$name.xml").writeText(buildString {
            append("<?xml version='1.0' encoding='utf-8' standalone='yes' ?>\n<map>\n")
            values.forEach { (key, value) ->
                append("    <string name=\"").append(xml(key)).append("\">").append(xml(value))
                // FastXmlSerializer.text() sets mLineStart on the final LF;
                // endTag() then emits one four-space nesting level before </string>.
                if (value.endsWith('\n')) append("    ")
                append("</string>\n")
            }
            append("</map>\n")
        }, Charsets.UTF_8)
        return context.getSharedPreferences(name, Context.MODE_PRIVATE)
    }

    private fun xml(text: String): String = text.replace("&", "&amp;").replace("<", "&lt;")
        .replace(">", "&gt;").replace("\"", "&quot;")

    private fun assertBulkRepair(preferences: android.content.SharedPreferences, preferenceName: String): Pair<Int, Int> {
        val before = preferences.all.mapValues { (_, value) -> if (value is Set<*>) value.toSet() else value }
        val expected = before.toMutableMap()
        var repairedCount = 0
        var protectedCount = 0
        for ((storedKey, value) in before) {
            if (!storedKey.endsWith("/source") || value !is String) continue
            val draft = storedKey.removeSuffix("/source")
            if (before.containsKey("$draft/sourceEncoding")) continue
            val recordedHash = runCatching {
                val metadata = JSONObject(before["$draft/sourceIndentation_v2"] as? String ?: "")
                require(metadata.opt("version") == 1)
                metadata.getString("sha256").also { require(it.matches(Regex("[0-9a-f]{64}"))) }
            }.getOrNull()
            val candidate = if (value.endsWith("\n    ")) value.dropLast(4) else null
            val provedOriginal = candidate?.takeIf { recordedHash != null && sha256(value) != recordedHash &&
                sha256(it) == recordedHash }
            when {
                provedOriginal != null -> {
                    expected[storedKey] = CppLessonSourceDraft.encode(provedOriginal)
                    expected["$draft/sourceEncoding"] = CppLessonSourceDraft.ENCODING
                    repairedCount++
                }
                value.endsWith('\n') -> {
                    expected[storedKey] = CppLessonSourceDraft.encode(value)
                    expected["$draft/sourceEncoding"] = CppLessonSourceDraft.ENCODING
                    protectedCount++
                }
            }
        }
        var protectedInputCount = 0
        for ((storedKey, value) in before) {
            if (!storedKey.endsWith("/input") || value !is String) continue
            val marker = "${storedKey.removeSuffix("/input")}/inputEncoding"
            if (before.containsKey(marker)) continue
            expected[storedKey] = CppDraftTextCodec.encode(value, "input")
            expected[marker] = CppDraftTextCodec.ENCODING
            protectedInputCount++
        }
        var commits = 0
        val observedPreferences = object : android.content.SharedPreferences by preferences {
            override fun edit(): android.content.SharedPreferences.Editor {
                val editor = preferences.edit()
                return object : android.content.SharedPreferences.Editor by editor {
                    override fun commit(): Boolean {
                        commits++
                        return editor.commit()
                    }
                }
            }
        }
        assertEquals("Only independent preexisting SHA evidence authorizes removing characters", repairedCount,
            CppLessonSourceDraft.repairProvenDrafts(observedPreferences))
        val expectedCommits = if (repairedCount + protectedCount + protectedInputCount > 0) 1 else 0
        assertEquals("Inputs and sources must be committed together, with no intermediate raw-source rewrite",
            expectedCommits, commits)
        assertEquals("Only proved source repairs, exact-text protections and their format markers may change",
            expected, preferences.all)
        val file = File(context.applicationInfo.dataDir, "shared_prefs/$preferenceName.xml")
        assertEquals("The actual persisted XML must contain those exact values", expected, parsePreferenceValues(file))
        val committedBytes = file.readBytes()
        assertEquals("A second cleanup must remove no further characters", 0,
            CppLessonSourceDraft.repairProvenDrafts(observedPreferences))
        assertEquals("An idempotent cleanup must not make another commit", expectedCommits, commits)
        assertEquals(expected, preferences.all)
        assertArrayEquals("A completed bulk cleanup must not rewrite the file again", committedBytes, file.readBytes())
        return repairedCount to protectedCount
    }

    private fun sha256(value: String): String = MessageDigest.getInstance("SHA-256")
        .digest(value.toByteArray(Charsets.UTF_8)).joinToString("") { "%02x".format(it.toInt() and 0xff) }

    private fun existingOtherPreferenceValues(excludedName: String): Map<String, Map<String, Any?>> =
        File(context.applicationInfo.dataDir, "shared_prefs").listFiles().orEmpty()
            .filter { it.isFile && it.extension == "xml" && it.nameWithoutExtension != excludedName }
            .associate { it.nameWithoutExtension to parsePreferenceValues(it) }

    private fun parsePreferenceValues(file: File): Map<String, Any?> {
        val document = file.inputStream().use { DocumentBuilderFactory.newInstance().newDocumentBuilder().parse(it) }
        val result = linkedMapOf<String, Any?>()
        val children = document.documentElement.childNodes
        for (index in 0 until children.length) {
            val element = children.item(index) as? Element ?: continue
            val value: Any? = when (element.tagName) {
                "string" -> element.textContent
                "boolean" -> element.getAttribute("value").toBooleanStrict()
                "int" -> element.getAttribute("value").toInt()
                "long" -> element.getAttribute("value").toLong()
                "float" -> element.getAttribute("value").toFloat()
                "set" -> {
                    val strings = element.getElementsByTagName("string")
                    (0 until strings.length).map { strings.item(it).textContent }.toSet()
                }
                "null" -> null
                else -> error("Unexpected Android preference type ${element.tagName}")
            }
            result[element.getAttribute("name")] = value
        }
        return result
    }

    private fun newName(): String = ("cpp_source_draft_qa_" + UUID.randomUUID().toString().replace("-", ""))
        .also { names.add(it) }

    private fun preferenceDirectory(): File = File(context.applicationInfo.dataDir, "shared_prefs").apply { mkdirs() }

    private fun evidence(name: String, value: JSONObject) {
        File(context.getExternalFilesDir(null), "cpp-source-draft-qa").apply { mkdirs() }
            .resolve(name).writeText(value.toString(2))
    }

    private fun isolated(action: () -> Unit) {
        assertTrue(context.packageName in setOf("com.learngraphicsprogramming.app.curricularqa",
            "com.learngraphicsprogramming.app.realdownloadsqa"))
        try { action() } finally { names.forEach { context.deleteSharedPreferences(it) } }
    }
}
