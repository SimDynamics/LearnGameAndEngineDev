package com.learngraphicsprogramming.app

import android.content.Context
import android.os.Bundle
import android.os.SystemClock
import android.text.Editable
import android.text.TextWatcher
import android.util.Log
import android.view.InputDevice
import android.view.KeyEvent
import android.view.View
import android.view.ViewTreeObserver
import android.view.WindowManager
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputMethodManager
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicLong
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/** Opt-in observation of one human keyboard press. Passing is evidence collection, not a fix verdict. */
@RunWith(AndroidJUnit4::class)
class CppPhysicalKeyboardProbeDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val preferences = linkedMapOf<String, Map<String, *>>()
    private var preferenceNames = emptySet<String>()
    private var captured = false

    @Before fun preserveOnlyOptedInQaState() {
        assumeTrue("Requires cppPhysicalTabProbe=true and one human physical Tab press",
            InstrumentationRegistry.getArguments().getString("cppPhysicalTabProbe") == "true")
        assertEquals("Never run the keyboard probe against the student's application",
            "com.learngraphicsprogramming.app.realdownloadsqa", context.packageName)
        preferenceNames = preferenceFiles()
        preferenceNames.forEach { name ->
            preferences[name] = context.getSharedPreferences(name, Context.MODE_PRIVATE).all.mapValues { (_, value) ->
                if (value is Set<*>) value.toSet() else value
            }
        }
        captured = true
    }

    @After fun restoreQaPreferences() {
        if (!captured) return
        instrumentation.waitForIdleSync()
        (preferenceFiles() + preferences.keys).forEach { name ->
            if (name !in preferenceNames) assertTrue(context.deleteSharedPreferences(name))
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
                assertEquals(preferences[name], target.all)
            }
        }
        assertEquals(preferenceNames, preferenceFiles())
    }

    @Test fun observeOnePhysicalTabWithoutInjectingAnyKeyOrChangingTheIme() {
        val lastRelevant = AtomicLong(0L)
        val kinds = ConcurrentHashMap.newKeySet<String>()
        fun observe(kind: String) {
            kinds += kind
            lastRelevant.set(SystemClock.uptimeMillis())
        }
        ActivityScenario.launch(CppPrototypeActivity::class.java).use { scenario ->
            lateinit var editor: FreeScrollEditText
            var original: ((Int, KeyEvent) -> Boolean)? = null
            lateinit var focusObserver: ViewTreeObserver.OnGlobalFocusChangeListener
            lateinit var textObserver: TextWatcher
            var installed = false
            try {
                scenario.onActivity { activity ->
                    activity.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                }
                await(scenario, "Unlock the QA device and wait for the compiler window/layout") { activity ->
                    val input = activity.findViewById<FreeScrollEditText>(R.id.sourceCode)
                    activity.hasWindowFocus() && input.width > 0 && input.height > 0 && input.layout != null
                }
                scenario.onActivity { activity ->
                    editor = activity.findViewById(R.id.sourceCode)
                    original = editor.cppHardwareKeyHandler
                    editor.cppHardwareKeyHandler = { keyCode, event ->
                        val route = Thread.currentThread().stackTrace.firstOrNull {
                            it.className == FreeScrollEditText::class.java.name &&
                                it.methodName in setOf("onKeyPreIme", "dispatchKeyEvent")
                        }?.methodName ?: "unknown"
                        val before = state(activity.currentFocus, editor)
                        val consumed = original?.invoke(keyCode, event) ?: false
                        Log.i(TAG, "KEY route=$route key=${KeyEvent.keyCodeToString(keyCode)}($keyCode)" +
                            " scan=${event.scanCode} deviceId=${event.deviceId} device=${device(event.device)} source=0x${event.source.toString(16)}" +
                            " action=${event.action} repeat=${event.repeatCount}" +
                            " meta=0x${event.metaState.toString(16)} event=[$event]" +
                            " noModifiers=${event.hasNoModifiers()} flags=0x${event.flags.toString(16)}" +
                            " consumed=$consumed before=[$before] after=[${state(activity.currentFocus, editor)}]")
                        if (keyCode == KeyEvent.KEYCODE_TAB || event.characters?.contains('\t') == true)
                            observe("KEY_TAB/$route")
                        consumed
                    }
                    editor.setOnEditorActionListener { _, action, event ->
                        Log.i(TAG, "EDITOR_ACTION id=$action event=${event?.toString() ?: "null"}" +
                            " state=[${state(activity.currentFocus, editor)}]")
                        if (action == EditorInfo.IME_ACTION_NEXT) observe("IME_ACTION_NEXT")
                        false // Observation must leave the ordinary Android action unchanged.
                    }
                    focusObserver = ViewTreeObserver.OnGlobalFocusChangeListener { old, new ->
                        Log.i(TAG, "FOCUS old=${view(old)} new=${view(new)} state=[${state(new, editor)}]")
                    }
                    activity.window.decorView.viewTreeObserver.addOnGlobalFocusChangeListener(focusObserver)
                    textObserver = object : TextWatcher {
                        override fun beforeTextChanged(text: CharSequence?, start: Int, count: Int, after: Int) = Unit
                        override fun onTextChanged(text: CharSequence?, start: Int, before: Int, count: Int) = Unit
                        override fun afterTextChanged(text: Editable?) {
                            Log.i(TAG, "TEXT state=[${state(activity.currentFocus, editor)}]")
                            if (text?.contains('\t') == true) observe("COMMITTED_TAB")
                        }
                    }
                    editor.addTextChangedListener(textObserver)
                    installed = true
                    editor.setText("   x")
                    editor.setSelection(3)
                    editor.showSoftInputOnFocus = true
                    assertTrue(editor.requestFocus())
                    Log.i(TAG, "INPUT_DEVICES " + InputDevice.getDeviceIds().joinToString { device(InputDevice.getDevice(it)) })
                }
                await(scenario, "The original installed IME must serve the focused compiler") { activity ->
                    editor.hasFocus() && activity.getSystemService(InputMethodManager::class.java).isActive(editor)
                }
                scenario.onActivity { activity ->
                    activity.getSystemService(InputMethodManager::class.java).showSoftInput(editor,
                        InputMethodManager.SHOW_IMPLICIT)
                }
                await(scenario, "The original IME must be visible; no keyboard configuration was changed") { activity ->
                    ViewCompat.getRootWindowInsets(activity.window.decorView)?.isVisible(WindowInsetsCompat.Type.ime()) == true
                }
                scenario.onActivity { activity -> Log.i(TAG, "READY press physical TAB once; ${state(activity.currentFocus, editor)}") }
                instrumentation.sendStatus(0, Bundle().apply {
                    putString("stream", "CppPhysicalTab READY: press physical TAB once; observing up to 90 seconds.\n")
                })
                val deadline = SystemClock.uptimeMillis() + 90_000L
                while (SystemClock.uptimeMillis() < deadline) {
                    val last = lastRelevant.get()
                    if (last > 0L && SystemClock.uptimeMillis() - last >= 2_000L) break
                    SystemClock.sleep(50)
                }
                scenario.onActivity { activity ->
                    Log.i(TAG, "SUMMARY observed=${lastRelevant.get() > 0L} kinds=$kinds" +
                        " final=[${state(activity.currentFocus, editor)}] This is diagnostic evidence, not a correctness verdict.")
                }
                assertTrue("No Tab key, committed TAB or IME_ACTION_NEXT arrived within 90 seconds. " +
                    "Check the external keyboard/host focus and Logcat tag $TAG; no physical-input result was verified.",
                    lastRelevant.get() > 0L)
            } finally {
                if (installed) scenario.onActivity { activity ->
                    editor.cppHardwareKeyHandler = original
                    editor.setOnEditorActionListener(null)
                    editor.removeTextChangedListener(textObserver)
                    activity.window.decorView.viewTreeObserver.takeIf { it.isAlive }
                        ?.removeOnGlobalFocusChangeListener(focusObserver)
                }
            }
        }
    }

    private fun await(scenario: ActivityScenario<CppPrototypeActivity>, message: String,
                      ready: (CppPrototypeActivity) -> Boolean) {
        val deadline = SystemClock.uptimeMillis() + 12_000L
        do {
            var result = false
            scenario.onActivity { result = ready(it) }
            if (result) return
            SystemClock.sleep(40)
        } while (SystemClock.uptimeMillis() < deadline)
        throw AssertionError(message)
    }

    private fun state(focus: View?, input: FreeScrollEditText) =
        "focus=${view(focus)} text=${input.text.toString().replace("\t", "\\t").replace("\n", "\\n")}" +
            " selection=${input.selectionStart}:${input.selectionEnd}" +
            " windowFocus=${input.hasWindowFocus()} editorFocus=${input.hasFocus()}" +
            " enabled=${input.isEnabled} keyListener=${input.keyListener != null} handler=${input.cppHardwareKeyHandler != null}"

    private fun view(view: View?) = view?.let { "${it.javaClass.simpleName}@${System.identityHashCode(it)} id=${it.id} tag=${it.tag}" } ?: "null"
    private fun device(device: InputDevice?) = device?.let {
        "id=${it.id} name=${it.name} external=${it.isExternal} virtual=${it.isVirtual}" +
            " vendor=${it.vendorId} product=${it.productId} descriptor=${it.descriptor} sources=0x${it.sources.toString(16)}"
    } ?: "null"
    private fun preferenceFiles() = File(context.applicationInfo.dataDir, "shared_prefs").listFiles().orEmpty()
        .filter { it.extension == "xml" }.map { it.nameWithoutExtension }.toSet()

    companion object { private const val TAG = "CppPhysicalTab" }
}
