package com.learngraphicsprogramming.app

import android.app.KeyguardManager
import android.content.Context
import android.content.Intent
import android.os.SystemClock
import android.view.InputDevice
import android.view.KeyCharacterMap
import android.view.KeyEvent
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputConnection
import android.view.inputmethod.InputMethodManager
import android.widget.EditText
import android.widget.LinearLayout
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.narration.KokoroContinuousNarrationService
import com.learngraphicsprogramming.app.narration.KokoroPagePreparationQueue
import com.learngraphicsprogramming.app.narration.NarrationAudioPlanner
import com.learngraphicsprogramming.app.narration.NarrationSettingsRepository
import java.io.File
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/** P1/U1/L2/P5 is loaded normally, with its actual stdin and focus traversal intact. */
@RunWith(AndroidJUnit4::class)
class CppLessonCompilerTabStdinDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val snapshots = linkedMapOf<String, Map<String, *>>()
    private val narrationFiles = linkedMapOf<String, ByteArray?>()
    private var originalFiles = emptySet<String>()
    private var captured = false
    private var originalLanguage = ""

    @Before fun preserveTheIsolatedQaLessonDraftsAndNarrationState() {
        assertEquals("com.learngraphicsprogramming.app.realdownloadsqa", context.packageName)
        assertEquals(context.packageName, BuildConfig.APPLICATION_ID)
        val lock = context.getSystemService(KeyguardManager::class.java)
        assertFalse("Unlock the test device before window input", lock.isDeviceLocked || lock.isKeyguardLocked)
        originalFiles = preferenceFiles()
        originalFiles.forEach { name ->
            snapshots[name] = context.getSharedPreferences(name, Context.MODE_PRIVATE).all.mapValues { (_, value) ->
                if (value is Set<*>) value.toSet() else value
            }
        }
        originalLanguage = UiLanguageSettings.selectedTag(context)
        listOf(KokoroPagePreparationQueue.FILE_NAME, NarrationAudioPlanner.FILE_NAME).forEach { name ->
            narrationFiles[name] = File(context.filesDir, name).takeIf(File::isFile)?.readBytes()
        }
        captured = true
        instrumentation.runOnMainSync {
            NarrationSettingsRepository.saveEnabled(context, false)
            UiLanguageSettings.select(context, "es")
        }
        context.stopService(Intent(context, KokoroContinuousNarrationService::class.java))
        val until = SystemClock.uptimeMillis() + 30_000
        while (Thread.getAllStackTraces().keys.any { it.isAlive && it.name == "KokoroOfflineNarration" } &&
            SystemClock.uptimeMillis() < until) SystemClock.sleep(50)
        assertTrue("QA generation must release before editing isolated lesson state",
            Thread.getAllStackTraces().keys.none { it.isAlive && it.name == "KokoroOfflineNarration" })
    }

    @After fun restoreEveryQaPreferenceAndNarrationCache() {
        if (!captured) return
        instrumentation.waitForIdleSync()
        instrumentation.runOnMainSync { UiLanguageSettings.select(context, originalLanguage) }
        (preferenceFiles() + snapshots.keys).forEach { name ->
            if (name !in originalFiles) assertTrue(context.deleteSharedPreferences(name))
            else {
                val preferences = context.getSharedPreferences(name, Context.MODE_PRIVATE)
                val edit = preferences.edit().clear()
                snapshots.getValue(name).forEach { (key, value) ->
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
                assertEquals(snapshots[name], preferences.all)
            }
        }
        narrationFiles.forEach { (name, bytes) ->
            val file = File(context.filesDir, name)
            if (bytes == null) file.delete() else file.writeBytes(bytes)
        }
        synchronized(KokoroPagePreparationQueue) {
            for (name in listOf("directory", "queue")) KokoroPagePreparationQueue::class.java.getDeclaredField(name)
                .apply { isAccessible = true }.set(KokoroPagePreparationQueue, null)
        }
        synchronized(NarrationAudioPlanner) {
            @Suppress("UNCHECKED_CAST")
            val stores = NarrationAudioPlanner::class.java.getDeclaredField("stores").apply { isAccessible = true }
                .get(NarrationAudioPlanner) as MutableMap<File, Any>
            stores.remove(File(context.filesDir, NarrationAudioPlanner.FILE_NAME).canonicalFile)
        }
        assertEquals(originalFiles, preferenceFiles())
    }

    @Test fun hidingTheShortcutMenuAlsoRetiresTheCompilerCaretWithoutRewritingSource() =
        withLesson { scenario, initial ->
            focusWithIme(scenario)
            scenario.onActivity { activity ->
                val editor = source(activity)
                val bar = field(activity, "programmingKeyboardBar") as View
                assertEquals("The shortcut menu is initially visible", View.VISIBLE, bar.visibility)
                assertTrue(editor.hasFocus())
                activity.getSystemService(InputMethodManager::class.java)
                    .hideSoftInputFromWindow(editor.windowToken, 0)
            }
            await(scenario, "Hiding the native IME dismisses shortcuts and compiler caret") { activity ->
                val bar = field(activity, "programmingKeyboardBar") as View
                ViewCompat.getRootWindowInsets(activity.window.decorView)
                    ?.isVisible(WindowInsetsCompat.Type.ime()) == false &&
                    bar.visibility == View.GONE && !source(activity).hasFocus()
            }
            scenario.onActivity { activity ->
                assertEquals("Editor text is unchanged", initial, source(activity).text.toString())
                assertTrue("A tap can resume editing", source(activity).requestFocus())
            }
        }

    @Test fun pressingCompileAndRunKeepsSourceFocusAndItsShortcutMenu() =
        withLesson { scenario, initial ->
            focusWithIme(scenario)
            scenario.onActivity { activity ->
                val editor = source(activity)
                val bar = field(activity, "programmingKeyboardBar") as View
                val caret = editor.selectionStart
                assertEquals(View.VISIBLE, bar.visibility)
                assertTrue(tagged(activity, "cpp_run").performClick())
                assertTrue("Run does not steal source focus", editor.hasFocus())
                assertEquals("Run does not rewrite edited code", initial, editor.text.toString())
                assertEquals("Run retains source caret", caret, editor.selectionStart)
                assertEquals("Run leaves shortcuts available", View.VISIBLE, bar.visibility)
            }
        }

    @Test fun choosingAnotherRealPuzzleReleasesOldCompilerFocusAndKeyboard() =
        withLesson { scenario, _ ->
            focusWithIme(scenario)
            scenario.onActivity { activity ->
                val editor = source(activity)
                val published = LessonRepository.loadLessonJson(context, "programming", LESSON)
                val choice = published.getJSONArray("pages").getJSONObject(4)
                    .getJSONArray("blocks").getJSONObject(4)
                val answer = choice.getJSONArray("options").getString(choice.getInt("correctIndex"))
                val radio = descendants(activity.window.decorView)
                    .filterIsInstance<android.widget.RadioButton>()
                    .single { it.text.toString() == answer }
                assertTrue(editor.hasFocus())
                radio.performClick()
                assertTrue(radio.isChecked)
                assertFalse("Choosing another puzzle closes editor caret", editor.hasFocus())
                assertFalse("The shortcuts are no longer owned by a code editor",
                    (field(activity, "programmingKeyboardBar") as View).visibility == View.VISIBLE)
            }
        }

    @Test fun dismissingShortcutsAllowsEnterToAdvanceACompletedCompilerPage() {
        // Keep the exercise state deterministic without changing the authored lesson.
        ProgressRepository.saveExerciseState(context, "programming", LESSON, 4, 4,
            JSONObject().put("correct", false).put("selectedIndex", -1))
        withLesson { scenario, _ ->
            scenario.onActivity { activity ->
                val lesson = LessonRepository.loadLessonJson(context, "programming", LESSON)
                val question = lesson.getJSONArray("pages").getJSONObject(4)
                    .getJSONArray("blocks").getJSONObject(4)
                val answer = question.getJSONArray("options").getString(question.getInt("correctIndex"))
                val radio = descendants(activity.window.decorView)
                    .filterIsInstance<android.widget.RadioButton>()
                    .single { it.text.toString() == answer }
                radio.performClick()
                assertTrue(radio.isChecked)
                val check = descendants(activity.window.decorView).filterIsInstance<android.widget.Button>()
                    .single { it.text.toString() == activity.getString(R.string.exercise_check) && it.isEnabled }
                assertTrue(check.performClick())
                assertTrue(activity.findViewById<View>(R.id.buttonContinue).isEnabled)
            }
            focusWithIme(scenario)
            scenario.onActivity { activity ->
                val editor = source(activity)
                assertTrue(editor.hasFocus())
                activity.getSystemService(InputMethodManager::class.java)
                    .hideSoftInputFromWindow(editor.windowToken, 0)
            }
            await(scenario, "The hidden shortcuts release compiler keyboard focus") { activity ->
                (field(activity, "programmingKeyboardBar") as View).visibility == View.GONE &&
                    !source(activity).hasFocus()
            }
            instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_ENTER)
            await(scenario, "One Enter navigates instead of adding a code newline") { activity ->
                field(activity, "currentPage") == 5
            }
        }
    }

    @Test fun realLessonHidTabAndFloatingTabKeepSourceFocusAndStdinInNormalAndFullscreen() = withLesson { scenario, _ ->
        for (fullscreen in listOf(false, true)) {
            mode(scenario, fullscreen)
            focusWithIme(scenario)
            for (physical in listOf(true, false)) {
                setEditor(scenario, "   value;", 3)
                tab(scenario, physical)
                assertEditor(scenario, "\tvalue;", 1)
                instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_DEL)
                assertEditor(scenario, "value;", 0)
                for ((start, end) in listOf(1 to 5, 5 to 1)) {
                    setEditor(scenario, "prefix", start, end)
                    tab(scenario, physical)
                    assertEditor(scenario, "p\tx", 2)
                }
            }
            scenario.onActivity { activity ->
                assertEquals("The original prepared-input control remains present", STDIN, stdin(activity).text.toString())
                assertTrue(stdin(activity).isEnabled && stdin(activity).keyListener != null)
                if (!fullscreen) assertTrue("Normal mode keeps the actual stdin visible", stdin(activity).isShown)
            }
        }
        mode(scenario, false)
        scenario.onActivity { assertTrue("Returning from fullscreen restores stdin", stdin(it).isShown) }
    }

    @Test fun theActualAdvertisedNextImeActionCannotTransferSourceFocusToStdin() = withLesson { scenario, _ ->
        focusWithIme(scenario)
        setEditor(scenario, "ab", 1)
        var connection: InputConnection? = null
        try {
            scenario.onActivity { activity ->
                val info = EditorInfo()
                connection = requireNotNull(source(activity).onCreateInputConnection(info))
                val action = info.imeOptions and EditorInfo.IME_MASK_ACTION
                println("Real lesson EditorInfo: imeOptions=${info.imeOptions} action=$action inputType=${info.inputType} " +
                    "fieldId=${info.fieldId} stdinVisible=${stdin(activity).isShown} sourceFocused=${activity.currentFocus === source(activity)}")
                assertEquals("Exercise the real advertised NEXT path, never skip it", EditorInfo.IME_ACTION_NEXT, action)
                assertTrue(requireNotNull(connection).performEditorAction(EditorInfo.IME_ACTION_NEXT))
            }
            assertEditor(scenario, "ab", 1)
            scenario.onActivity { activity ->
                assertSame("The advertised source action cannot navigate to stdin", source(activity), activity.currentFocus)
                assertFalse(stdin(activity).hasFocus())
                assertEquals(STDIN, stdin(activity).text.toString())
            }
        } finally { scenario.onActivity { connection?.closeConnection() } }
    }

    @Test fun onePhysicalTabFollowedByTheSamePressNextCallbackInsertsExactlyOneTab() = withLesson { scenario, _ ->
        focusWithIme(scenario)
        setEditor(scenario, "   value;", 3)
        var connection: InputConnection? = null
        val down = SystemClock.uptimeMillis()
        var downDelivered = false
        try {
            scenario.onActivity { activity ->
                val info = EditorInfo()
                connection = requireNotNull(source(activity).onCreateInputConnection(info))
                assertEquals("This real lesson advertises the NEXT callback seen in the physical trace",
                    EditorInfo.IME_ACTION_NEXT, info.imeOptions and EditorInfo.IME_MASK_ACTION)
            }
            injectHidKey(KeyEvent.ACTION_DOWN, down)
            downDelivered = true
            assertEditor(scenario, "\tvalue;", 1)
            scenario.onActivity {
                // The physical trace emitted this event-null callback after DOWN
                // had already inserted TAB. NEXT only rejects focus navigation.
                assertTrue(requireNotNull(connection).performEditorAction(EditorInfo.IME_ACTION_NEXT))
            }
            injectHidKey(KeyEvent.ACTION_UP, down)
            downDelivered = false
            assertEditor(scenario, "\tvalue;", 1)
            scenario.onActivity { activity ->
                assertEquals("NEXT cannot insert a duplicate TAB", 1, source(activity).text.toString().count { it == '\t' })
                assertFalse(stdin(activity).hasFocus())
            }
        } finally {
            if (downDelivered) injectHidKey(KeyEvent.ACTION_UP, down)
            scenario.onActivity { connection?.closeConnection() }
        }
    }

    @Test fun authoredIndentationAndFourManuallyWrittenSpacesKeepTheirDistinctNativeDeletion() = withLesson { scenario, initial ->
        focusWithIme(scenario)
        scenario.onActivity { activity ->
            tagged(activity, "cpp_reset").performClick()
            assertEquals("Reset retains its existing authored empty-input behavior", "", stdin(activity).text.toString())
            stdin(activity).setText(STDIN)
        }
        instrumentation.waitForIdleSync()
        val start = initial.indexOf("\n    ") + 1
        assertTrue("This exact authored lab contains a four-space indentation", start > 0)
        scenario.onActivity { activity ->
            assertEquals(initial, source(activity).text.toString())
            source(activity).setSelection(start + 4)
        }
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_DEL)
        assertEditor(scenario, initial.removeRange(start, start + 4), start)

        // A plain setText carries no loaded-source origin; no inference from four spaces is allowed.
        setEditor(scenario, initial, start + 4)
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_DEL)
        assertEditor(scenario, initial.removeRange(start + 3, start + 4), start + 3)
        setEditor(scenario, "    value;", 4)
        for (remaining in 3 downTo 0) {
            instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_DEL)
            assertEditor(scenario, " ".repeat(remaining) + "value;", remaining)
        }
    }

    @Test fun realLessonCtrlZAndCtrlShiftZPreserveBothAuthoredIndentationsAndStdinFocus() = withLesson { scenario, initial ->
        focusWithIme(scenario)
        for (token in listOf("std::cout", "return")) {
            scenario.onActivity { activity ->
                tagged(activity, "cpp_reset").performClick()
                stdin(activity).setText(STDIN)
                assertTrue(source(activity).requestFocus())
            }
            val caret = initial.indexOf(token)
            val start = caret - 4
            assertTrue("The exact P1/U1/L2/P5 source keeps its authored four-space $token indentation",
                caret >= 4 && initial.substring(start, caret) == "    ")
            scenario.onActivity { source(it).setSelection(caret) }
            instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_DEL)
            val deleted = initial.removeRange(start, caret)
            assertEditor(scenario, deleted, start)
            repeat(3) {
                nativeHistoryKey(redo = false)
                assertEditor(scenario, initial, caret)
                nativeHistoryKey(redo = true)
                assertEditor(scenario, deleted, start)
                nativeHistoryKey(redo = false)
                assertEditor(scenario, initial, caret)
                instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_DEL)
                assertEditor(scenario, deleted, start)
            }
        }
    }

    @Test fun realLessonCtrlZRetainsManualSpacesAndExplicitCompletedTabAsDifferentOrigins() = withLesson { scenario, _ ->
        focusWithIme(scenario)
        setEditor(scenario, "value;", 0)
        scenario.onActivity { activity ->
            val connection = requireNotNull(source(activity).onCreateInputConnection(EditorInfo()))
            try { assertTrue(connection.commitText("    ", 1)) }
            finally { connection.closeConnection() }
        }
        assertEditor(scenario, "    value;", 4)
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_DEL)
        assertEditor(scenario, "   value;", 3)
        nativeHistoryKey(redo = false)
        assertEditor(scenario, "    value;", 4)
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_DEL)
        assertEditor(scenario, "   value;", 3)
        tab(scenario, physical = true)
        assertEditor(scenario, "\tvalue;", 1)
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_DEL)
        assertEditor(scenario, "value;", 0)
        nativeHistoryKey(redo = false)
        assertEditor(scenario, "\tvalue;", 1)
        nativeHistoryKey(redo = true)
        assertEditor(scenario, "value;", 0)
        nativeHistoryKey(redo = false)
        assertEditor(scenario, "\tvalue;", 1)
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_DEL)
        assertEditor(scenario, "value;", 0)
    }

    private fun withLesson(test: (ActivityScenario<LessonActivity>, String) -> Unit) {
        val lesson = LessonRepository.loadLessonJson(context, "programming", LESSON)
        val authored = lesson.toString()
        val blocks = lesson.getJSONArray("pages").getJSONObject(4).getJSONArray("blocks")
        assertEquals("cpp_compiler", blocks.getJSONObject(2).getString("type"))
        val initial = blocks.getJSONObject(2).getString("initialCode")
        context.getSharedPreferences("cpp_lesson_drafts", Context.MODE_PRIVATE).edit()
            .remove("$DRAFT/source").remove("$DRAFT/input").commit()
        ProgressRepository.saveCurrentPage(context, "programming", LESSON, 4)
        val intent = Intent(context, LessonActivity::class.java).putExtra("routeId", "programming")
            .putExtra("courseId", "cpp_fundamentals").putExtra("unitId", "cpp_fundamentals__programas_y_ejecucion")
            .putExtra("lessonId", LESSON).putExtra("lessonNumber", 2).putExtra("lessonTotal", 5)
        ActivityScenario.launch<LessonActivity>(intent).use { scenario ->
            await(scenario, "The exact authored P1/U1/L2/P5 lab and stdin complete native layout") { activity ->
                field(activity, "currentPage") == 4 && activity.hasWindowFocus() &&
                    descendants(activity.window.decorView).any { it.tag == "cpp_code_input" && it.width > 0 }
            }
            scenario.onActivity { activity ->
                activity.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                assertEquals(LESSON, field(activity, "currentLessonId"))
                assertEquals(initial, source(activity).text.toString())
                assertTrue(source(activity).isShown)
                assertTrue("The real stdin must not be removed from this regression", stdin(activity).isShown)
                stdin(activity).setText(STDIN)
            }
            test(scenario, initial)
        }
        assertEquals("The test must not rewrite curricular JSON", authored,
            LessonRepository.loadLessonJson(context, "programming", LESSON).toString())
    }

    private fun mode(scenario: ActivityScenario<LessonActivity>, fullscreen: Boolean) {
        scenario.onActivity { activity ->
            if (field(activity, "compilerFullscreenActive") != fullscreen) tagged(activity, "cpp_compiler_fullscreen").performClick()
        }
        await(scenario, "The actual compiler changes mode without replacing its editor") { activity ->
            field(activity, "compilerFullscreenActive") == fullscreen && source(activity).width > 0 && source(activity).height > 0
        }
    }

    private fun focusWithIme(scenario: ActivityScenario<LessonActivity>) {
        scenario.onActivity { activity ->
            val input = source(activity)
            input.showSoftInputOnFocus = true
            assertTrue(input.requestFocus())
        }
        await(scenario, "The original IME must serve the real lesson source") { activity ->
            activity.hasWindowFocus() && activity.getSystemService(InputMethodManager::class.java).isActive(source(activity))
        }
        scenario.onActivity { activity -> activity.getSystemService(InputMethodManager::class.java)
            .showSoftInput(source(activity), InputMethodManager.SHOW_IMPLICIT) }
        await(scenario, "The actual IME must be visible before physical TAB") { activity ->
            ViewCompat.getRootWindowInsets(activity.window.decorView)?.isVisible(WindowInsetsCompat.Type.ime()) == true
        }
    }

    private fun setEditor(scenario: ActivityScenario<LessonActivity>, text: String, start: Int, end: Int = start) {
        scenario.onActivity { activity ->
            source(activity).setText(text)
            source(activity).setSelection(start, end)
            assertTrue(source(activity).requestFocus())
        }
        instrumentation.waitForIdleSync()
    }

    private fun tab(scenario: ActivityScenario<LessonActivity>, physical: Boolean) {
        if (physical) {
            val down = SystemClock.uptimeMillis()
            for (action in listOf(KeyEvent.ACTION_DOWN, KeyEvent.ACTION_UP)) {
                injectHidKey(action, down)
            }
        } else {
            await(scenario, "The real floating TAB button remains visible") { activity ->
                val keys = activity.findViewById<LinearLayout>(R.id.programmingKeyboardBarContent)
                keys.childCount > 0 && keys.getChildAt(0).isShown
            }
            scenario.onActivity { activity ->
                assertTrue(activity.findViewById<LinearLayout>(R.id.programmingKeyboardBarContent).getChildAt(0).performClick())
            }
        }
        instrumentation.waitForIdleSync()
    }

    private fun injectHidKey(action: Int, down: Long) {
        val event = KeyEvent(down, SystemClock.uptimeMillis(), action, KeyEvent.KEYCODE_TAB, 0, 0,
            KeyCharacterMap.VIRTUAL_KEYBOARD, 15, KeyEvent.FLAG_FROM_SYSTEM, InputDevice.SOURCE_KEYBOARD)
        assertTrue("The window receives the HID-shaped TAB", instrumentation.uiAutomation.injectInputEvent(event, true))
    }

    private fun nativeHistoryKey(redo: Boolean) {
        val down = SystemClock.uptimeMillis()
        val modifiers = KeyEvent.META_CTRL_ON or KeyEvent.META_CTRL_LEFT_ON or
            if (redo) KeyEvent.META_SHIFT_ON or KeyEvent.META_SHIFT_LEFT_ON else 0
        for (action in listOf(KeyEvent.ACTION_DOWN, KeyEvent.ACTION_UP)) {
            val event = KeyEvent(down, SystemClock.uptimeMillis(), action, KeyEvent.KEYCODE_Z, 0, modifiers,
                KeyCharacterMap.VIRTUAL_KEYBOARD, 44, KeyEvent.FLAG_FROM_SYSTEM, InputDevice.SOURCE_KEYBOARD)
            assertTrue("The real window receives native ${if (redo) "Ctrl+Shift+Z" else "Ctrl+Z"}",
                instrumentation.uiAutomation.injectInputEvent(event, true))
        }
        instrumentation.waitForIdleSync()
    }

    private fun assertEditor(scenario: ActivityScenario<LessonActivity>, expected: String, caret: Int) {
        instrumentation.waitForIdleSync()
        scenario.onActivity { activity ->
            val input = source(activity)
            val state = "sourceFocus=${activity.currentFocus === input} stdinFocus=${stdin(activity).hasFocus()} " +
                "selection=${input.selectionStart}:${input.selectionEnd} text=${input.text.toString().replace("\t", "\\t").replace("\n", "\\n")}"
            assertSame("Physical TAB cannot focus stdin: $state", input, activity.currentFocus)
            assertEquals(state, expected, input.text.toString())
            assertEquals(state, caret, input.selectionStart)
            assertEquals(state, caret, input.selectionEnd)
            assertEquals("Draft provenance must include marks restored after native undo/redo",
                CppIndentationUnits.snapshot(requireNotNull(input.text)),
                context.getSharedPreferences("cpp_lesson_drafts", Context.MODE_PRIVATE)
                    .getString("$DRAFT/sourceIndentation_v2", null))
            assertEquals(STDIN, stdin(activity).text.toString())
            assertEquals(4, field(activity, "currentPage"))
        }
    }

    private fun source(activity: LessonActivity) = tagged(activity, "cpp_code_input") as FreeScrollEditText
    private fun stdin(activity: LessonActivity) = tagged(activity, "cpp_input") as EditText
    private fun tagged(activity: LessonActivity, tag: String) = descendants(activity.window.decorView).single { it.tag == tag }
    private fun descendants(view: View): List<View> = listOf(view) + if (view is ViewGroup)
        (0 until view.childCount).flatMap { descendants(view.getChildAt(it)) } else emptyList()
    private fun field(activity: LessonActivity, name: String): Any? = LessonActivity::class.java
        .getDeclaredField(name).apply { isAccessible = true }.get(activity)
    private fun preferenceFiles() = File(context.applicationInfo.dataDir, "shared_prefs").listFiles().orEmpty()
        .filter { it.extension == "xml" }.map { it.nameWithoutExtension }.toSet()
    private fun await(scenario: ActivityScenario<LessonActivity>, label: String, condition: (LessonActivity) -> Boolean) {
        val until = SystemClock.uptimeMillis() + 12_000
        do {
            instrumentation.waitForIdleSync()
            var ready = false
            scenario.onActivity { ready = condition(it) }
            if (ready) return
            SystemClock.sleep(50)
        } while (SystemClock.uptimeMillis() < until)
        fail(label)
    }

    companion object {
        private const val LESSON = "cpp_fundamentals__programas_y_ejecucion__primer_programa_y_salida"
        private const val DRAFT = "programming/$LESSON/4/2"
        private const val STDIN = "stdin must stay unchanged 7"
    }
}
