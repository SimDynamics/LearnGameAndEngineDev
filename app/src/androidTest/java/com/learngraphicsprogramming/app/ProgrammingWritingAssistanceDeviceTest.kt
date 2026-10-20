package com.learngraphicsprogramming.app

import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.SystemClock
import android.view.KeyCharacterMap
import android.view.KeyEvent
import android.view.View
import android.view.ViewGroup
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputMethodManager
import android.widget.EditText
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.narration.KokoroPagePreparationQueue
import com.learngraphicsprogramming.app.narration.NarrationAudioPlanner
import com.learngraphicsprogramming.app.narration.NarrationSettingsRepository
import java.io.File
import org.json.JSONArray
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/** Original published fields exercise the compiler's shared writing rules, never a new editor. */
@RunWith(AndroidJUnit4::class)
class ProgrammingWritingAssistanceDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val preferences = linkedMapOf<String, Map<String, *>>()
    private val plannerFiles = linkedMapOf<String, ByteArray?>()
    private var preferenceNames = emptySet<String>()
    private var originalLocale = ""
    private var preserved = false
    private var scenario: ActivityScenario<LessonActivity>? = null

    @Before fun preserveTheIsolatedLdplayerStudentState() {
        assertTrue("Only an isolated QA application", context.packageName in setOf(
            "com.learngraphicsprogramming.app.realdownloadsqa", "com.learngraphicsprogramming.app.curricularqa"))
        assertTrue("Only LDPlayer is authorized", Build.SUPPORTED_ABIS.any { it == "x86" || it == "x86_64" })
        preferenceNames = preferenceFiles()
        preferenceNames.forEach { name ->
            preferences[name] = context.getSharedPreferences(name, Context.MODE_PRIVATE).all.mapValues { (_, value) ->
                if (value is Set<*>) value.toSet() else value
            }
        }
        listOf(KokoroPagePreparationQueue.FILE_NAME, NarrationAudioPlanner.FILE_NAME).forEach { name ->
            plannerFiles[name] = File(context.filesDir, name).takeIf(File::isFile)?.readBytes()
        }
        originalLocale = UiLanguageSettings.selectedTag(context)
        preserved = true
        instrumentation.runOnMainSync {
            UiLanguageSettings.select(context, "es")
            NarrationSettingsRepository.saveEnabled(context, false)
            PremiumRepository.setVerifiedPremium(context)
        }
    }

    @After fun restoreTheIsolatedLdplayerStudentState() {
        if (!preserved) return
        scenario?.close()
        scenario = null
        instrumentation.waitForIdleSync()
        instrumentation.runOnMainSync { UiLanguageSettings.select(context, originalLocale) }
        (preferenceFiles() + preferences.keys).forEach { name ->
            if (name !in preferenceNames) context.deleteSharedPreferences(name)
            else {
                val target = context.getSharedPreferences(name, Context.MODE_PRIVATE)
                val edit = target.edit().clear()
                preferences.getValue(name).forEach { (key, value) ->
                    when (value) {
                        is String -> edit.putString(key, value)
                        is Int -> edit.putInt(key, value)
                        is Long -> edit.putLong(key, value)
                        is Float -> edit.putFloat(key, value)
                        is Boolean -> edit.putBoolean(key, value)
                        is Set<*> -> edit.putStringSet(key, value.filterIsInstance<String>().toSet())
                        else -> error("Unsupported preference $name/$key")
                    }
                }
                assertTrue(edit.commit())
                assertEquals("All student preferences and progress are restored", preferences[name], target.all)
            }
        }
        plannerFiles.forEach { (name, bytes) ->
            val file = File(context.filesDir, name)
            if (bytes == null) file.delete() else file.writeBytes(bytes)
        }
    }

    @Test fun realSingleLineOutputAndCodeAnswersSharePairsFromKeyboardAndFloatingShortcuts() {
        val host = open(FIRST, 1)
        host.onActivity { activity ->
            val fields = codeFields(activity)
            assertEquals("The original output prediction and code-answer fields", 2, fields.size)
            fields.forEach { input ->
                assertTrue(input is FreeScrollEditText)
                assertTrue(PuzzleAnswerSubmit.isSingleLine(input))
                assertSharedPairs(activity, input)
            }
        }
    }

    @Test fun realInlineBlanksSharePairsButTabStillNavigatesTheOriginalIndependentBlanks() {
        val host = open(FORMAT, 4)
        host.onActivity { activity ->
            val fields = codeFields(activity)
            assertEquals("The authored independent operator and literal blanks", 2, fields.size)
            fields.forEach { input ->
                assertTrue(PuzzleAnswerSubmit.isSingleLine(input))
                assertSharedPairs(activity, input)
            }
            fields[0].setText("<<")
            fields[1].setText("\"Carga lista\"")
            assertTrue(fields[0].requestFocus())
            shortcut(activity, "\t")
            assertSame("The floating Tab retains the established blank navigation", fields[1], activity.currentFocus)
            assertEquals("<<", fields[0].text.toString())
            fields[0].requestFocus()
            dispatch(fields[0], KeyEvent.KEYCODE_TAB)
            assertSame("Physical Tab uses exactly the same navigation", fields[1], activity.currentFocus)
            dispatch(fields[1], KeyEvent.KEYCODE_ENTER)
            assertTrue(ProgressRepository.getExerciseState(context, FORMAT, 4, 1)?.optBoolean("correct") == true)
            assertEquals("Enter checks the puzzle, it never inserts a line", "\"Carga lista\"", fields[1].text.toString())
            assertEquals(4, currentPage(activity))
        }
    }

    @Test fun singleLineEnterChecksTheAssistedQuotedAnswerWithoutInsertingANewlineOrLeavingThePage() {
        val host = open(FIRST, 1)
        lateinit var answer: EditText
        host.onActivity { activity ->
            val fields = codeFields(activity)
            fields[0].setText("Archivo preparado")
            fields[0].requestFocus()
            assertTrue(requireNotNull(fields[0].onCreateInputConnection(EditorInfo()))
                .performEditorAction(EditorInfo.IME_ACTION_DONE))
            assertTrue(ProgressRepository.getExerciseState(context, FIRST, 1, 3)?.optBoolean("correct") == true)
            answer = fields[1]
            answer.setText("")
            assertTrue(answer.requestFocus())
            shortcut(activity, "\"")
            assertEquals("\"\"", answer.text.toString())
            assertEquals(1, answer.selectionStart)
            requireNotNull(answer.onCreateInputConnection(EditorInfo())).commitText("Muelle cerrado", 1)
            shortcut(activity, "\"")
            assertEquals("\"Muelle cerrado\"", answer.text.toString())
            assertEquals(answer.length(), answer.selectionStart)
        }
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_ENTER)
        instrumentation.waitForIdleSync()
        host.onActivity { activity ->
            assertTrue(ProgressRepository.getExerciseState(context, FIRST, 1, 4)?.optBoolean("correct") == true)
            assertEquals("A single physical Enter only checks the newly typed answer", 1, currentPage(activity))
            assertEquals("\"Muelle cerrado\"", answer.text.toString())
            assertFalse(answer.text.toString().contains('\n'))
        }
    }

    @Test fun multilineOutputRetainsLiteralNewlinesAndCompilerEnterRetainsCodeIndentation() {
        val host = open(FORMAT, 1)
        lateinit var output: EditText
        host.onActivity { activity ->
            output = codeFields(activity).first()
            assertFalse(PuzzleAnswerSubmit.isSingleLine(output))
            assertSharedPairs(activity, output)
            output.setText("  Entrada")
            assertTrue(output.requestFocus())
            output.setSelection(output.length())
        }
        awaitImeEditor(host, output)
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_ENTER)
        instrumentation.waitForIdleSync()
        host.onActivity { activity ->
            assertEquals("Literal output Enter must not invent indentation", "  Entrada\n", output.text.toString())
            assertTrue("Output editing never checks by inserting a newline",
                ProgressRepository.getExerciseState(context, FORMAT, 1, 4)?.optBoolean("correct") != true)
            assertEquals(1, currentPage(activity))
        }
        val compilerHost = open(FIRST, 4)
        lateinit var compiler: EditText
        compilerHost.onActivity { activity ->
            compiler = codeFields(activity).single { !PuzzleAnswerSubmit.isSingleLine(it) }
            compiler.performClick()
            assertTrue(compiler.requestFocus())
            compiler.setText("{}")
            compiler.setSelection(1)
        }
        awaitImeEditor(compilerHost, compiler)
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_ENTER)
        instrumentation.waitForIdleSync()
        compilerHost.onActivity {
            assertEquals("The real compiler keeps its existing code assistance", "{\n\t\n}", compiler.text.toString())
            assertEquals(3, compiler.selectionStart)
        }
    }

    @Test fun reopenedOneCharacterDraftsNeverAcquireAnUnrequestedClosingSymbol() {
        ProgressRepository.resetLesson(context, ROUTE, FIRST)
        ProgressRepository.saveExerciseState(context, FIRST, 1, 3, JSONObject().put("answer", "(").put("correct", false))
        ProgressRepository.saveExerciseState(context, FIRST, 1, 4, JSONObject().put("answer", "\"").put("correct", false))
        val host = open(FIRST, 1, reset = false)
        repeat(2) { pass ->
            if (pass > 0) host.recreate()
            host.onActivity { activity ->
                assertEquals(listOf("(", "\""), codeFields(activity).map { it.text.toString() })
            }
        }
        ProgressRepository.resetLesson(context, ROUTE, FORMAT)
        ProgressRepository.saveExerciseState(context, FORMAT, 4, 1, JSONObject()
            .put("answers", JSONArray(listOf("[", "'"))).put("correct", false))
        val inlineHost = open(FORMAT, 4, reset = false)
        repeat(2) { pass ->
            if (pass > 0) inlineHost.recreate()
            inlineHost.onActivity { activity ->
                assertEquals(listOf("[", "'"), codeFields(activity).map { it.text.toString() })
            }
        }
    }

    @Test fun realSingleLineOutputRetainsImeNextWithoutSubmittingItsPhysicalTabContinuation() {
        val host = open(FIRST, 1)
        host.onActivity { activity ->
            val fields = codeFields(activity)
            val output = fields[0]
            val code = fields[1]
            assertTrue(PuzzleAnswerSubmit.isSingleLine(output))
            var outputChecks = 0
            var codeChecks = 0
            // Observe submissions on the original fields without charging a
            // deliberately incomplete answer or changing the curriculum.
            assertTrue(PuzzleAnswerSubmit.install(output) { outputChecks++ })
            assertTrue(PuzzleAnswerSubmit.install(code) { codeChecks++ })
            output.setText("value")
            assertTrue(output.requestFocus())
            output.setSelection(output.length())
            val outputConnection = requireNotNull(output.onCreateInputConnection(EditorInfo()))
            assertTrue(outputConnection.performEditorAction(EditorInfo.IME_ACTION_NEXT))
            assertEquals("Output predictions retain their previous genuine IME Next submission", 1, outputChecks)
            assertSame(output, activity.currentFocus)

            assertTrue(output.dispatchKeyEvent(KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_TAB)))
            assertEquals("value\t", output.text.toString())
            assertTrue(outputConnection.performEditorAction(EditorInfo.IME_ACTION_NEXT))
            assertEquals("The observed physical DOWN -> NEXT -> UP path must not check", 1, outputChecks)
            assertSame("The physical Tab continuation cannot move to the next field", output, activity.currentFocus)
            assertTrue(output.dispatchKeyEvent(KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_TAB)))
            assertEquals("The IME continuation cannot insert another Tab", "value\t", output.text.toString())
            assertEquals(output.length(), output.selectionStart)

            assertTrue(outputConnection.performEditorAction(EditorInfo.IME_ACTION_NEXT))
            assertEquals("After that key release, a new deliberate action checks normally", 2, outputChecks)
            assertTrue(outputConnection.performEditorAction(EditorInfo.IME_ACTION_DONE))
            assertEquals(3, outputChecks)
            assertSame(output, activity.currentFocus)

            assertTrue(code.requestFocus())
            val codeConnection = requireNotNull(code.onCreateInputConnection(EditorInfo()))
            assertTrue(codeConnection.performEditorAction(EditorInfo.IME_ACTION_NEXT))
            assertEquals("Other code fields retain their existing unconditional Tab/Next protection", 0, codeChecks)
            assertTrue(codeConnection.performEditorAction(EditorInfo.IME_ACTION_DONE))
            assertEquals(1, codeChecks)
        }
    }

    private fun assertSharedPairs(activity: LessonActivity, input: EditText) {
        assertTrue(input.requestFocus())
        for ((opening, closing) in listOf('(' to ')', '[' to ']', '{' to '}', '\"' to '\"', '\'' to '\'')) {
            // Native physical-key dispatch, followed by the original floating shortcut callback.
            input.setText("ab")
            input.setSelection(1)
            val keys = requireNotNull(KeyCharacterMap.load(KeyCharacterMap.VIRTUAL_KEYBOARD).getEvents(charArrayOf(opening)))
            keys.forEach { input.dispatchKeyEvent(it) }
            assertEquals("The shared engine closes $opening from a physical keyboard", "a$opening${closing}b", input.text.toString())
            assertEquals(2, input.selectionStart)
            shortcut(activity, closing.toString())
            assertEquals("A typed existing closing symbol is skipped, never duplicated", "a$opening${closing}b", input.text.toString())
            assertEquals(3, input.selectionStart)
            assertSame(input, activity.currentFocus)

            input.setText("ab")
            input.setSelection(1)
            shortcut(activity, opening.toString())
            assertEquals("The real floating shortcut shares the same closing pair", "a$opening${closing}b", input.text.toString())
            assertEquals(2, input.selectionStart)
            requireNotNull(input.onCreateInputConnection(EditorInfo())).deleteSurroundingText(1, 0)
            assertEquals("Backspace removes an empty auto-pair without changing surrounding text", "ab", input.text.toString())
            assertEquals(1, input.selectionStart)
        }
        input.setText("\\")
        input.setSelection(1)
        shortcut(activity, "\"")
        assertEquals("Escaped quotes retain the compiler's existing behavior", "\\\"", input.text.toString())
        assertEquals(2, input.selectionStart)
        val original = "std::cout << \"texto\";\n"
        input.setText(original)
        assertEquals("Programmatic source/output loads remain unchanged", original, input.text.toString())
    }

    private fun open(lesson: String, page: Int, reset: Boolean = true): ActivityScenario<LessonActivity> {
        scenario?.close()
        scenario = null
        if (reset) ProgressRepository.resetLesson(context, ROUTE, lesson)
        ProgressRepository.saveCurrentPage(context, ROUTE, lesson, page)
        val host = ActivityScenario.launch<LessonActivity>(Intent(context, LessonActivity::class.java)
            .putExtra("routeId", ROUTE).putExtra("courseId", "cpp_fundamentals")
            .putExtra("unitId", "programas_y_ejecucion").putExtra("lessonId", lesson)
            .putExtra("lessonNumber", 1).putExtra("lessonTotal", 1))
        scenario = host
        instrumentation.waitForIdleSync()
        host.onActivity { activity ->
            assertEquals(page, currentPage(activity))
            val original = LessonRepository.loadLessonJson(context, ROUTE, lesson)
            assertEquals("The actual curriculum is loaded unchanged", original.toString(), field<JSONObject>(activity, "lessonJson").toString())
        }
        return host
    }

    private fun shortcut(activity: LessonActivity, symbol: String) = LessonActivity::class.java
        .getDeclaredMethod("insertProgrammingShortcut", String::class.java)
        .apply { isAccessible = true }.invoke(activity, symbol)

    private fun dispatch(input: EditText, key: Int) {
        input.dispatchKeyEvent(KeyEvent(KeyEvent.ACTION_DOWN, key))
        input.dispatchKeyEvent(KeyEvent(KeyEvent.ACTION_UP, key))
    }

    private fun awaitImeEditor(host: ActivityScenario<LessonActivity>, input: EditText) {
        host.onActivity { activity ->
            val editorInfo = EditorInfo()
            assertNotNull(input.onCreateInputConnection(editorInfo))
            assertTrue("Android exposes this actual field as multiline",
                editorInfo.inputType and android.text.InputType.TYPE_TEXT_FLAG_MULTI_LINE != 0)
            assertTrue("Android's multiline connection retains a real Enter key",
                editorInfo.imeOptions and EditorInfo.IME_FLAG_NO_ENTER_ACTION != 0)
            activity.getSystemService(InputMethodManager::class.java).showSoftInput(input, InputMethodManager.SHOW_IMPLICIT)
        }
        val deadline = SystemClock.uptimeMillis() + 10_000
        while (true) {
            instrumentation.waitForIdleSync()
            var connected = false
            host.onActivity { activity ->
                connected = activity.hasWindowFocus() && activity.currentFocus === input &&
                    activity.getSystemService(InputMethodManager::class.java).isActive(input)
            }
            if (connected) return
            assertTrue("The IME must be attached to this field before injecting a physical Enter",
                SystemClock.uptimeMillis() < deadline)
            SystemClock.sleep(20)
        }
    }

    private fun codeFields(activity: LessonActivity) = descendants(activity.findViewById(R.id.lessonContent))
        .filterIsInstance<EditText>().filter { it.tag == "cpp_code_input" }
    private fun descendants(view: View): List<View> = listOf(view) +
        if (view is ViewGroup) (0 until view.childCount).flatMap { descendants(view.getChildAt(it)) } else emptyList()
    private fun currentPage(activity: LessonActivity): Int = field(activity, "currentPage")
    @Suppress("UNCHECKED_CAST") private fun <T> field(value: Any, name: String): T = value.javaClass
        .getDeclaredField(name).apply { isAccessible = true }.get(value) as T
    private fun preferenceFiles() = File(context.applicationInfo.dataDir, "shared_prefs").listFiles().orEmpty()
        .filter { it.name.endsWith(".xml") }.map { it.name.removeSuffix(".xml") }.toSet()

    companion object {
        private const val ROUTE = "programming"
        private const val FIRST = "cpp_fundamentals__programas_y_ejecucion__primer_programa_y_salida"
        private const val FORMAT = "cpp_fundamentals__programas_y_ejecucion__texto_secuencia_y_formato"
    }
}
