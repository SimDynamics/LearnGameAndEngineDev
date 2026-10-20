package com.learngraphicsprogramming.app

import android.content.Context
import android.graphics.Typeface
import android.os.Build
import android.os.Process
import android.text.InputType
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
import java.io.File
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
            val observations = JSONArray()
            for (input in listOf(original, original + "    ", "", "\n", "  Rita\t4\r\n", "ñ é λ 中文\n")) {
                val preferences = context.getSharedPreferences(newName(names), Context.MODE_PRIVATE)
                assertTrue(CppLessonInputDraft.write(preferences.edit(), key, input).commit())
                assertEquals("One input value and one format marker; no duplicate draft", 2, preferences.all.size)
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
