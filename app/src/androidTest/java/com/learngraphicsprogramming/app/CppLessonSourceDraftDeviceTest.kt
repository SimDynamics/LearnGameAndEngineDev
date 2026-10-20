package com.learngraphicsprogramming.app

import android.content.Context
import android.graphics.Typeface
import android.os.Build
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
import java.util.UUID

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
            .put("nativeColdRawLength", preferences.getString("$key/source", null)!!.length)
            .put("recoveredLength", restored.length).put("exactOriginalSource", restored == initial)
            .put("recordedIndentationRetained", CppIndentationUnits.snapshot(marked) == CppIndentationUnits.snapshot(loaded))
            .put("fixture", "Recorded FastXmlSerializer closing-tag indentation, loaded by Android SharedPreferences")
            .put("manualSpacesRetained", true).put("unprovedDataRetained", true))
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
