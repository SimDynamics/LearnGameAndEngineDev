package com.learngraphicsprogramming.app

import android.content.Context
import android.os.SystemClock
import android.text.Editable
import android.text.TextWatcher
import android.view.InputDevice
import android.view.KeyCharacterMap
import android.view.KeyEvent
import android.view.View
import android.view.WindowManager
import android.view.inputmethod.BaseInputConnection
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputConnection
import android.view.inputmethod.InputMethodManager
import android.widget.EditText
import android.widget.LinearLayout
import androidx.core.view.doOnLayout
import androidx.core.view.doOnPreDraw
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import org.junit.After
import org.junit.Before
import org.junit.Assert.assertEquals
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Handler regressions plus actual window/IME delivery into the isolated native compiler editor. */
@RunWith(AndroidJUnit4::class)
class CppHardwareTabInstrumentedTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val preferences = linkedMapOf<String, Map<String, *>>()
    private var preferenceNames = emptySet<String>()
    private var captured = false

    @Before fun preserveQaPreferencesAndCppDrafts() {
        assertEquals("Use the isolated QA application", "com.learngraphicsprogramming.app.realdownloadsqa", context.packageName)
        preferenceNames = preferenceFiles()
        preferenceNames.forEach { name ->
            preferences[name] = context.getSharedPreferences(name, Context.MODE_PRIVATE).all.mapValues { (_, value) ->
                if (value is Set<*>) value.toSet() else value
            }
        }
        captured = true
    }

    @After fun restoreQaPreferencesAndCppDrafts() {
        if (!captured) return
        instrumentation.waitForIdleSync()
        (preferenceFiles() + preferences.keys).forEach { name ->
            if (name !in preferenceNames) {
                assertTrue(context.deleteSharedPreferences(name))
            } else {
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
                assertEquals(preferences[name], target.all)
            }
        }
        assertEquals(preferenceNames, preferenceFiles())
    }

    @Test fun windowDeliveredTabWithVisibleImeKeepsTheRealCompilerFocused() {
        withWindowEditor { scenario ->
            // A real window input event must traverse pre-IME, IME and post-IME stages.
            // Never call Activity/View.dispatchKeyEvent in this acceptance check.
            instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_TAB)
            instrumentation.waitForIdleSync()
            scenario.onActivity { activity ->
                val editor = activity.findViewById<EditText>(R.id.sourceCode)
                assertEquals("a" + KeyboardShortcutSettingsRepository.getTabText() + "b", editor.text.toString())
                assertSame(editor, activity.currentFocus)
                assertEquals(1 + KeyboardShortcutSettingsRepository.getTabText().length, editor.selectionStart)
                assertEquals(editor.selectionStart, editor.selectionEnd)
                assertEquals("One physical Tab must insert one real TAB character", 1,
                    editor.text!!.count { it == '\t' })
            }
        }
    }

    @Test fun tabHandlerInsertsOneRealTabOnceAndRetainsEditorFocus() {
        withEditor { activity, editor ->
            editor.setText("ab")
            editor.setSelection(1)
            assertTrue(activity.dispatchKeyEvent(tab(KeyEvent.ACTION_DOWN)))
            assertTrue(activity.dispatchKeyEvent(tab(KeyEvent.ACTION_UP)))
            assertEquals("a\tb", editor.text.toString())
            assertEquals(2, editor.selectionStart)
            assertEquals(2, editor.selectionEnd)
            assertSame(editor, activity.currentFocus)
        }
    }

    @Test fun tabHandlerReplacesForwardAndReversedSelections() {
        withEditor { activity, editor ->
            for ((start, end) in listOf(1 to 5, 5 to 1)) {
                editor.setText("prefix")
                editor.setSelection(start, end)
                activity.dispatchKeyEvent(tab(KeyEvent.ACTION_DOWN))
                activity.dispatchKeyEvent(tab(KeyEvent.ACTION_UP))
                assertEquals("p\tx", editor.text.toString())
                assertEquals(2, editor.selectionStart)
                assertEquals(2, editor.selectionEnd)
            }
        }
    }

    @Test fun repeatingTabHandlerKeepsOneCharacterPerIndentationUnit() {
        withEditor { activity, editor ->
            editor.setText("code")
            editor.setSelection(0)
            repeat(3) { repeatCount ->
                activity.dispatchKeyEvent(tab(KeyEvent.ACTION_DOWN, repeatCount))
            }
            activity.dispatchKeyEvent(tab(KeyEvent.ACTION_UP))
            assertEquals("\t\t\tcode", editor.text.toString())
            assertEquals(3, editor.selectionStart)
            assertSame(editor, activity.currentFocus)
        }
    }

    @Test fun tabHandlerAfterSkippingAClosingDelimiterIsNotDiscardedAsImeSpacing() {
        withEditor { activity, editor ->
            editor.setText("()")
            editor.setSelection(1)
            editor.text!!.insert(1, ")")
            assertEquals("()", editor.text.toString())
            assertEquals(2, editor.selectionStart)
            // Dispatch in the same UI cycle as the skipped delimiter.
            activity.dispatchKeyEvent(tab(KeyEvent.ACTION_DOWN))
            activity.dispatchKeyEvent(tab(KeyEvent.ACTION_UP))
            assertEquals("()\t", editor.text.toString())
            assertEquals(3, editor.selectionStart)
        }
    }

    @Test fun modifiedTabCombinationsDoNotInsertCodeIndentation() {
        withEditor { _, editor ->
            editor.setText("code")
            editor.setSelection(2)
            for (modifier in listOf(KeyEvent.META_SHIFT_ON, KeyEvent.META_CTRL_ON, KeyEvent.META_ALT_ON)) {
                editor.dispatchKeyEvent(KeyEvent(0L, 0L, KeyEvent.ACTION_DOWN,
                    KeyEvent.KEYCODE_TAB, 0, modifier))
                editor.dispatchKeyEvent(KeyEvent(0L, 0L, KeyEvent.ACTION_UP,
                    KeyEvent.KEYCODE_TAB, 0, modifier))
                assertEquals("code", editor.text.toString())
            }
        }
    }

    @Test fun imeConnectionKeyEventsInsertExactlyOneTabAndKeepTheCompilerFocused() {
        withWindowEditor { scenario ->
            var connection: InputConnection? = null
            try {
                scenario.onActivity { activity ->
                    val editor = activity.findViewById<EditText>(R.id.sourceCode)
                    val native = requireNotNull(editor.onCreateInputConnection(EditorInfo()))
                    connection = native
                    val time = SystemClock.uptimeMillis()
                    for (action in listOf(KeyEvent.ACTION_DOWN, KeyEvent.ACTION_UP)) {
                        native.sendKeyEvent(KeyEvent(time, time, action, KeyEvent.KEYCODE_TAB, 0,
                            0, KeyCharacterMap.VIRTUAL_KEYBOARD, 0, KeyEvent.FLAG_SOFT_KEYBOARD,
                            InputDevice.SOURCE_KEYBOARD))
                    }
                }
                instrumentation.waitForIdleSync()
                assertWindowEditor(scenario, "a\tb", 2)
            } finally {
                scenario.onActivity { connection?.closeConnection() }
            }
        }
    }

    @Test fun windowDeliveredTabReplacesSelectionsInEitherDirectionWithoutLeavingTheEditor() {
        withWindowEditor { scenario ->
            for ((start, end) in listOf(1 to 5, 5 to 1)) {
                scenario.onActivity { activity ->
                    activity.findViewById<EditText>(R.id.sourceCode).apply {
                        setText("prefix")
                        setSelection(start, end)
                    }
                }
                instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_TAB)
                instrumentation.waitForIdleSync()
                assertWindowEditor(scenario, "p\tx", 2)
            }
        }
    }

    @Test fun imeCommittedTabCanBeRemovedByOneWindowDeliveredBackspace() {
        withWindowEditor { scenario ->
            var connection: InputConnection? = null
            try {
                scenario.onActivity { activity ->
                    val editor = activity.findViewById<EditText>(R.id.sourceCode)
                    val native = requireNotNull(editor.onCreateInputConnection(EditorInfo()))
                    connection = native
                    assertTrue(native.commitText("\t", 1))
                }
                instrumentation.waitForIdleSync()
                assertWindowEditor(scenario, "a\tb", 2)
                instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_DEL)
                instrumentation.waitForIdleSync()
                assertWindowEditor(scenario, "ab", 1)
            } finally {
                scenario.onActivity { connection?.closeConnection() }
            }
        }
    }

    @Test fun windowDeliveredBackspaceRemovesOneTabAtATimeAndPreservesOrdinarySpaces() {
        withWindowEditor { scenario ->
            instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_TAB)
            instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_TAB)
            instrumentation.waitForIdleSync()
            assertWindowEditor(scenario, "a\t\tb", 3)
            instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_DEL)
            instrumentation.waitForIdleSync()
            assertWindowEditor(scenario, "a\tb", 2)
            instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_DEL)
            instrumentation.waitForIdleSync()
            assertWindowEditor(scenario, "ab", 1)

            val frame = CountDownLatch(1)
            val edits = mutableListOf<String>()
            lateinit var observed: EditText
            lateinit var observer: TextWatcher
            scenario.onActivity { activity ->
                observed = activity.findViewById(R.id.sourceCode)
                observer = object : TextWatcher {
                    override fun beforeTextChanged(text: CharSequence?, start: Int, count: Int, after: Int) = Unit
                    override fun onTextChanged(text: CharSequence?, start: Int, before: Int, count: Int) = Unit
                    override fun afterTextChanged(text: Editable?) {
                        if (edits.size < 32) edits += editorState(activity, observed)
                    }
                }
                observed.addTextChangedListener(observer)
                observed.setText("a    b")
                observed.setSelection(5)
                observed.doOnPreDraw { frame.countDown() }
            }
            try {
                assertTrue("The programmatic fixture and its selection must complete a native frame",
                    frame.await(5, TimeUnit.SECONDS))
                instrumentation.waitForIdleSync()
                instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_DEL)
                instrumentation.waitForIdleSync()
                // App-idle is not an acknowledgement from the separate IME process.
                // Wait for the exact observable edit; never resend Backspace to make it pass.
                val until = SystemClock.uptimeMillis() + 2_500L
                do {
                    var exact = false
                    scenario.onActivity { activity ->
                        exact = observed.text.toString() == "a   b" && observed.selectionStart == 4 &&
                            observed.selectionEnd == 4 && activity.currentFocus === observed
                    }
                    if (exact) break
                    SystemClock.sleep(20)
                } while (SystemClock.uptimeMillis() < until)
                scenario.onActivity { activity ->
                    val diagnostic = editorState(activity, observed) + " edits=" + edits.joinToString(" -> ")
                    assertEquals(diagnostic, "a   b", observed.text.toString())
                    assertEquals(diagnostic, 4, observed.selectionStart)
                    assertEquals(diagnostic, 4, observed.selectionEnd)
                    assertSame(diagnostic, observed, activity.currentFocus)
                }
            } finally {
                scenario.onActivity { observed.removeTextChangedListener(observer) }
            }
        }
    }

    @Test fun loadedIndentationUnitsAreRemovedByNativeImeAndHardwareBackwardDeletion() {
        withWindowEditor { scenario ->
            var connection: InputConnection? = null
            try {
                scenario.onActivity { activity ->
                    val editor = activity.findViewById<EditText>(R.id.sourceCode)
                    editor.setText(CppIndentationUnits.loadedText("        x", seedPredefined = true))
                    editor.setSelection(8)
                    val native = requireNotNull(editor.onCreateInputConnection(EditorInfo()))
                    connection = native
                    assertTrue(native.deleteSurroundingText(1, 0))
                }
                instrumentation.waitForIdleSync()
                assertWindowEditor(scenario, "    x", 4)
                instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_DEL)
                instrumentation.waitForIdleSync()
                assertWindowEditor(scenario, "x", 0)
            } finally {
                scenario.onActivity { connection?.closeConnection() }
            }
        }
    }

    @Test fun recreationKeepsLoadedIndentationDistinctFromFourManuallyCommittedSpaces() {
        withWindowEditor { scenario ->
            var connection: InputConnection? = null
            try {
                scenario.onActivity { activity ->
                    val editor = activity.findViewById<EditText>(R.id.sourceCode)
                    editor.setText(CppIndentationUnits.loadedText("    x", seedPredefined = true))
                    editor.setSelection(4)
                    val native = requireNotNull(editor.onCreateInputConnection(EditorInfo()))
                    connection = native
                    assertTrue(native.commitText("    ", 1))
                }
                instrumentation.waitForIdleSync()
                assertWindowEditor(scenario, "        x", 8)
                scenario.onActivity { connection?.closeConnection(); connection = null }

                // The Activity saves and restores its own Bundle sidecar. The test
                // never creates or reapplies provenance for the manually entered spaces.
                scenario.recreate()
                focusEditorWithOriginalIme(scenario)
                assertWindowEditor(scenario, "        x", 8)
                for (manualSpaces in 3 downTo 0) {
                    instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_DEL)
                    instrumentation.waitForIdleSync()
                    assertWindowEditor(scenario, " ".repeat(4 + manualSpaces) + "x", 4 + manualSpaces)
                }
                instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_DEL)
                instrumentation.waitForIdleSync()
                assertWindowEditor(scenario, "x", 0)
            } finally {
                scenario.onActivity { connection?.closeConnection() }
            }
        }
    }

    @Test fun theVisibleFloatingTabButtonUsesTheSameRealTabInTheActualCompilerEditor() {
        withWindowEditor { scenario ->
            awaitActivity(scenario, "The actual floating shortcut bar must be visible") { activity ->
                val keys = activity.findViewById<LinearLayout>(R.id.programmingKeyboardBarContent)
                keys.childCount > 0 && keys.getChildAt(0).isShown
            }
            scenario.onActivity { activity ->
                val keys = activity.findViewById<LinearLayout>(R.id.programmingKeyboardBarContent)
                assertTrue(keys.getChildAt(0).performClick())
            }
            instrumentation.waitForIdleSync()
            assertWindowEditor(scenario, "a\tb", 2)
        }
    }

    @Test fun tabOutsideACppInputRetainsAndroidFocusNavigation() {
        ActivityScenario.launch(CppPrototypeActivity::class.java).use { scenario ->
            val instrumentation = InstrumentationRegistry.getInstrumentation()
            val laidOut = CountDownLatch(1)
            lateinit var parent: LinearLayout
            lateinit var ordinary: EditText
            lateinit var next: EditText
            scenario.onActivity { activity ->
                parent = activity.findViewById(R.id.compilerContent)
                ordinary = EditText(activity).apply {
                    id = View.generateViewId()
                    setSingleLine(true)
                    setText("ordinary")
                }
                next = EditText(activity).apply {
                    id = View.generateViewId()
                    setSingleLine(true)
                }
                ordinary.nextFocusForwardId = next.id
                parent.addView(ordinary, 0)
                parent.addView(next, 1)
                next.doOnLayout { laidOut.countDown() }
            }
            try {
                assertTrue("The ordinary inputs must complete a native layout before focus traversal",
                    laidOut.await(5, TimeUnit.SECONDS))
                instrumentation.waitForIdleSync()
                scenario.onActivity { activity ->
                    assertTrue(ordinary.width > 0 && ordinary.height > 0)
                    assertTrue(next.width > 0 && next.height > 0)
                    assertTrue(ordinary.requestFocus())
                    assertSame(ordinary, activity.currentFocus)
                }
                // Deliver through the window's real input path. Activity.dispatchKeyEvent
                // alone omits ViewRoot's traversal for an otherwise unhandled Tab key.
                instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_TAB)
                instrumentation.waitForIdleSync()
                scenario.onActivity { activity ->
                    assertEquals("ordinary", ordinary.text.toString())
                    assertSame(next, activity.currentFocus)
                }
            } finally {
                scenario.onActivity {
                    parent.removeView(ordinary)
                    parent.removeView(next)
                }
            }
        }
    }

    private fun tab(action: Int, repeatCount: Int = 0) =
        KeyEvent(0L, 0L, action, KeyEvent.KEYCODE_TAB, repeatCount)

    private fun withEditor(block: (CppPrototypeActivity, EditText) -> Unit) {
        ActivityScenario.launch(CppPrototypeActivity::class.java).use { scenario ->
            scenario.onActivity { activity ->
                val editor = activity.findViewById<EditText>(R.id.sourceCode)
                assertTrue(editor.requestFocus())
                block(activity, editor)
            }
        }
    }

    private fun withWindowEditor(block: (ActivityScenario<CppPrototypeActivity>) -> Unit) {
        ActivityScenario.launch(CppPrototypeActivity::class.java).use { scenario ->
            scenario.onActivity { activity ->
                activity.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                val editor = activity.findViewById<EditText>(R.id.sourceCode)
                editor.setText("ab")
                editor.setSelection(1)
            }
            focusEditorWithOriginalIme(scenario)
            block(scenario)
        }
    }

    private fun focusEditorWithOriginalIme(scenario: ActivityScenario<CppPrototypeActivity>) {
        // Only this QA Activity stays awake. Recreated windows need the same flag;
        // no device setting or screen-lock policy is changed.
        scenario.onActivity { activity ->
            activity.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        }
        awaitActivity(scenario, "The compiler must have window focus and a laid-out source editor") { activity ->
            val editor = activity.findViewById<EditText>(R.id.sourceCode)
            activity.hasWindowFocus() && editor.width > 0 && editor.height > 0 && editor.layout != null
        }
        scenario.onActivity { activity ->
            val editor = activity.findViewById<EditText>(R.id.sourceCode)
            editor.showSoftInputOnFocus = true
            assertTrue(editor.requestFocus())
        }
        awaitActivity(scenario, "The original IME must serve the compiler editor") { activity ->
            val editor = activity.findViewById<EditText>(R.id.sourceCode)
            editor.hasFocus() && activity.getSystemService(InputMethodManager::class.java).isActive(editor)
        }
        scenario.onActivity { activity ->
            activity.getSystemService(InputMethodManager::class.java).showSoftInput(
                activity.findViewById(R.id.sourceCode), InputMethodManager.SHOW_IMPLICIT)
        }
        awaitActivity(scenario, "The existing IME must actually be visible before delivering Tab") { activity ->
            ViewCompat.getRootWindowInsets(activity.window.decorView)
                ?.isVisible(WindowInsetsCompat.Type.ime()) == true
        }
    }

    private fun assertWindowEditor(scenario: ActivityScenario<CppPrototypeActivity>, text: String, cursor: Int) {
        scenario.onActivity { activity ->
            val editor = activity.findViewById<EditText>(R.id.sourceCode)
            assertEquals(text, editor.text.toString())
            assertEquals(cursor, editor.selectionStart)
            assertEquals(cursor, editor.selectionEnd)
            assertSame(editor, activity.currentFocus)
        }
    }

    private fun editorState(activity: CppPrototypeActivity, editor: EditText): String {
        val text = editor.text
        return "t=${SystemClock.uptimeMillis()} text=${text.toString().replace("\t", "\\t").replace("\n", "\\n")}" +
            " selection=${editor.selectionStart}:${editor.selectionEnd}" +
            " composing=${BaseInputConnection.getComposingSpanStart(text)}:${BaseInputConnection.getComposingSpanEnd(text)}" +
            " focused=${activity.currentFocus === editor} window=${activity.hasWindowFocus()}" +
            " activeIme=${activity.getSystemService(InputMethodManager::class.java).isActive(editor)}" +
            " visibleIme=${ViewCompat.getRootWindowInsets(activity.window.decorView)?.isVisible(WindowInsetsCompat.Type.ime())}" +
            " layoutRequested=${editor.isLayoutRequested}"
    }

    private fun awaitActivity(scenario: ActivityScenario<CppPrototypeActivity>, message: String,
                              condition: (CppPrototypeActivity) -> Boolean) {
        val until = SystemClock.uptimeMillis() + 10_000L
        do {
            var ready = false
            scenario.onActivity { ready = condition(it) }
            if (ready) return
            SystemClock.sleep(40)
        } while (SystemClock.uptimeMillis() < until)
        throw AssertionError(message)
    }

    private fun preferenceFiles() = File(context.applicationInfo.dataDir, "shared_prefs")
        .listFiles().orEmpty().filter { it.extension == "xml" }.map { it.nameWithoutExtension }.toSet()
}
