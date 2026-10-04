package com.learngraphicsprogramming.app

import android.accessibilityservice.AccessibilityServiceInfo
import android.content.Context
import android.content.Intent
import android.content.pm.ActivityInfo
import android.content.res.Configuration
import android.graphics.Rect
import android.graphics.RectF
import android.os.Bundle
import android.os.Build
import android.os.Parcel
import android.os.ParcelFileDescriptor
import android.os.SystemClock
import android.provider.Settings
import android.view.InputDevice
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.view.accessibility.AccessibilityNodeInfo
import android.view.accessibility.AccessibilityWindowInfo
import android.view.inputmethod.InputMethodManager
import android.widget.EditText
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import kotlin.math.abs

/** Uses the actual compiler, inline exercise and fake console in the programming lesson. */
@RunWith(AndroidJUnit4::class)
class ProgrammingLandscapeDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val route = "programming"
    private val course = "cpp_fundamentals"
    private val lesson = "cpp_fundamentals__intro_programming__what_is_programming"
    private val snapshots = linkedMapOf<String, Map<String, *>>()
    private var originalServiceInfo: AccessibilityServiceInfo? = null
    private var trackedInput: EditText? = null
    private var trackedNormalToolbarHeight: Int? = null
    private var trackedFocusLosses: List<String> = emptyList()

    @Before fun prepare() {
        trackedInput = null
        trackedNormalToolbarHeight = null
        trackedFocusLosses = emptyList()
        listOf("learning_progress", "navigation_state", "lesson_text_size_settings", "app_settings").forEach {
            snapshots[it] = context.getSharedPreferences(it, Context.MODE_PRIVATE).all.toMap()
        }
        ProgressRepository.resetLesson(context, route, lesson)
        LessonTextSizeSettingsRepository.saveUsesGlobalScale(context, course, true)
        LessonTextSizeSettingsRepository.saveGlobalScale(context, 100)
        for (command in listOf("input keyevent KEYCODE_WAKEUP", "wm dismiss-keyguard")) {
            ParcelFileDescriptor.AutoCloseInputStream(instrumentation.uiAutomation.executeShellCommand(command)).use { it.readBytes() }
        }
        originalServiceInfo = instrumentation.uiAutomation.serviceInfo
        val parcel = Parcel.obtain()
        try {
            requireNotNull(originalServiceInfo).writeToParcel(parcel, 0)
            parcel.setDataPosition(0)
            instrumentation.uiAutomation.serviceInfo = AccessibilityServiceInfo.CREATOR.createFromParcel(parcel).apply {
                flags = flags or AccessibilityServiceInfo.FLAG_RETRIEVE_INTERACTIVE_WINDOWS
            }
        } finally { parcel.recycle() }
    }

    @After fun restoreUserState() {
        originalServiceInfo?.let { instrumentation.uiAutomation.serviceInfo = it }
        snapshots.forEach { (name, original) ->
            val editor = context.getSharedPreferences(name, Context.MODE_PRIVATE).edit().clear()
            original.forEach { (key, value) ->
                when (value) {
                    is String -> editor.putString(key, value)
                    is Int -> editor.putInt(key, value)
                    is Long -> editor.putLong(key, value)
                    is Float -> editor.putFloat(key, value)
                    is Boolean -> editor.putBoolean(key, value)
                    is Set<*> -> editor.putStringSet(key, value.filterIsInstance<String>().toSet())
                }
            }
            assertTrue(editor.commit())
        }
    }

    @Test fun normalLandscapeCompilerExerciseAndFakeConsoleUseSafeWidthAndRestorePortrait() {
        for (page in listOf(1, 3, 7)) {
            ProgressRepository.saveCurrentPage(context, route, lesson, page)
            ActivityScenario.launch<LessonActivity>(intent()).use { scenario ->
                orient(scenario, false)
                lateinit var card: ViewGroup
                lateinit var body: View
                var portraitWidth = 0
                var portraitPadding = 0 to 0
                scenario.onActivity { activity ->
                    val content = activity.findViewById<ViewGroup>(R.id.lessonContent)
                    val fullscreen = tree(content).single { it.contentDescription == "Pantalla completa" }
                    card = generateSequence<View>(fullscreen) { it.parent as? View }
                        .first { it.parent === content } as ViewGroup
                    body = card.getChildAt(1)
                    trackedInput = tree(card).filterIsInstance<EditText>().firstOrNull { it.tag == "cpp_code_input" }
                    portraitPadding = content.paddingLeft to content.paddingRight
                    hideIme(activity, activity.currentFocus ?: content)
                }
                awaitUi(scenario, "Portrait programming surface settles on page ${page + 1}") { activity ->
                    !imeVisible(activity) && card.width > 0 && !card.isLayoutRequested
                }
                scenario.onActivity { portraitWidth = card.width }
                orient(scenario, true)
                var diagnostic = "Landscape width not yet sampled"
                awaitUi(scenario, "The normal code card and its body fill safe width on page ${page + 1}", { diagnostic }) { activity ->
                    val scroll = activity.findViewById<View>(R.id.lessonScroll)
                    diagnostic = "card=${bounds(card)}, body=${bounds(body)}, scroll=${bounds(scroll)}, frame=${available(activity)}"
                    !imeVisible(activity) && !card.isLayoutRequested && card.width == scroll.width && body.width == card.width
                }
                scenario.onActivity { activity ->
                    val scroll = activity.findViewById<View>(R.id.lessonScroll)
                    val cardBounds = bounds(card)
                    val safeBounds = bounds(scroll)
                    assertEquals("Code begins at the useful left edge", safeBounds.left, cardBounds.left)
                    assertEquals("Code reaches the useful right edge", safeBounds.right, cardBounds.right)
                    assertTrue("The surface grows beyond its portrait width", card.width > portraitWidth)
                    assertEquals("Normal mode has no reserved keyboard half", View.GONE,
                        activity.findViewById<View>(R.id.programmingKeyboardBar).visibility)
                    assertTrue("Normal lesson uses its available height", scroll.height > available(activity).height() / 2)
                }
                orient(scenario, false)
                awaitUi(scenario, "Portrait width restores on page ${page + 1}") {
                    !card.isLayoutRequested && card.width == portraitWidth
                }
                scenario.onActivity { activity ->
                    val content = activity.findViewById<ViewGroup>(R.id.lessonContent)
                    assertEquals(portraitPadding, content.paddingLeft to content.paddingRight)
                    assertSame("Rotation keeps the original surface", content, card.parent)
                }
            }
        }
    }

    @Test fun compilerFullscreenSplitsRealSwiftKeyViewportAndPreservesCodeSelection() {
        requireSwiftKey()
        ProgressRepository.saveCurrentPage(context, route, lesson, 7)
        ActivityScenario.launch<LessonActivity>(intent()).use { scenario ->
            orient(scenario, false)
            lateinit var input: EditText
            lateinit var fullscreen: View
            lateinit var card: ViewGroup
            lateinit var body: View
            lateinit var toolbar: View
            var fullscreenHeightWithoutIme = 0
            val source = "int main() {\n    int value = 12;\n    return value;\n}\n"
            scenario.onActivity { activity ->
                val content = activity.findViewById<ViewGroup>(R.id.lessonContent)
                input = tree(content).filterIsInstance<EditText>().single { it.tag == "cpp_code_input" }
                trackedInput = input
                fullscreen = tree(content).single { it.contentDescription == "Pantalla completa" }
                card = generateSequence<View>(fullscreen) { it.parent as? View }.first { it.parent === content } as ViewGroup
                body = card.getChildAt(1)
                toolbar = activity.findViewById(R.id.programmingKeyboardBar)
                input.setText(source)
                hideIme(activity, input)
                fullscreen.performClick()
            }
            orient(scenario, true)
            awaitUi(scenario, "Landscape fullscreen without IME uses the compiler's full height") { activity ->
                !imeVisible(activity) && fullscreen.contentDescription == "Salir de pantalla completa" &&
                    !card.isLayoutRequested && card.height > 0 && input.height > available(activity).height() / 2
            }
            scenario.onActivity { activity ->
                val viewport = available(activity)
                fullscreenHeightWithoutIme = viewport.height()
                assertEquals(View.GONE, toolbar.visibility)
                assertEquals("Fullscreen compiler uses safe width", viewport.width(), card.width)
                assertTrue("No half-height split without IME", input.height > viewport.height() / 2)
                assertEquals(source, input.text.toString())
            }
            orient(scenario, false)
            val focusLosses = mutableListOf<String>()
            trackedFocusLosses = focusLosses
            scenario.onActivity { activity ->
                activity.window.decorView.viewTreeObserver.addOnGlobalFocusChangeListener { oldFocus, newFocus ->
                    if (oldFocus === input && newFocus !== input) focusLosses += ancestorDiagnostic(input, newFocus)
                }
                assertTrue(input.requestFocus())
                input.setSelection(3, 7)
                showIme(activity, input)
            }
            awaitUi(scenario, "Portrait compiler shows the real SwiftKey and normal shortcuts") { activity ->
                imeVisible(activity) && toolbar.isShown && toolbar.height > 0
            }
            val normalToolbarHeight = toolbar.height
            trackedNormalToolbarHeight = normalToolbarHeight
            orient(scenario, true)
            var diagnostic = "Compact compiler not yet sampled"
            awaitUi(scenario, "Compiler shortcuts occupy at most half the real landscape IME viewport", { diagnostic }) { activity ->
                val viewport = available(activity)
                val imeTop = actualImeTop()
                diagnostic = "viewport=$viewport, input=${bounds(input)}, body=${bounds(body)}, toolbar=${bounds(toolbar)}, losses=$focusLosses"
                imeVisible(activity) && toolbar.isShown && toolbar.height in 1..(viewport.height() / 2 + 1) &&
                    imeTop != null && viewport.height() < fullscreenHeightWithoutIme &&
                    abs(bounds(toolbar).bottom - imeTop) <= 3 && !body.isLayoutRequested &&
                    caretVisible(input, viewport, bounds(toolbar).top)
            }
            scenario.onActivity { activity ->
                assertTrue("The original compiler keeps focus", input.hasFocus())
                assertSame(input, activity.currentFocus)
                assertTrue("Resizing never clears focus: $focusLosses", focusLosses.isEmpty())
                assertEquals(source, input.text.toString())
                assertEquals(3, input.selectionStart)
                assertEquals(7, input.selectionEnd)
                val viewport = available(activity)
                emitMetric("compiler landscape IME: H=${viewport.height()}, toolbar=${toolbar.height}, " +
                    "editor=${input.height}, body=${body.height}, viewport=$viewport, ${imeDiagnostic(activity)}")
                assertEquals(viewport.width(), card.width)
                assertTrue(toolbar.height <= viewport.height() / 2 + 1)
                assertTrue("The physical SwiftKey window reduces the usable fullscreen height", viewport.height() < fullscreenHeightWithoutIme)
                assertEquals("The toolbar ends above the physical SwiftKey window", requireNotNull(actualImeTop()).toDouble(),
                    bounds(toolbar).bottom.toDouble(), 3.0)
                assertTrue("The compiler caret is above the physical IME: ${programmingDiagnostic(activity)}",
                    caretVisible(input, viewport, requireNotNull(actualImeTop())))
                assertEquals("Code starts at the first safe available pixel", viewport.top, bounds(body).top)
                assertTrue("Caret remains in the upper editor region: ${programmingDiagnostic(activity)}",
                    caretVisible(input, viewport, bounds(toolbar).top))
            }
            pressSwiftKeyTwo()
            val edited = source.replaceRange(3, 7, "2")
            awaitUi(scenario, "A real SwiftKey press replaces the compiler's preserved selection") { input.text.toString() == edited }
            scenario.onActivity { activity -> hideIme(activity, input) }
            awaitUi(scenario, "Closing SwiftKey restores fullscreen height") { activity ->
                !imeVisible(activity) && toolbar.visibility == View.GONE && input.height > available(activity).height() / 2
            }
            scenario.onActivity { activity ->
                assertTrue(input.hasFocus())
                assertEquals(edited, input.text.toString())
                assertEquals(4, input.selectionStart)
                assertEquals(4, input.selectionEnd)
                showIme(activity, input)
            }
            awaitUi(scenario, "Reopened fullscreen shortcuts compact again") { activity ->
                imeVisible(activity) && toolbar.isShown && toolbar.height <= available(activity).height() / 2 + 1 &&
                    caretVisible(input, available(activity), bounds(toolbar).top)
            }
            scenario.onActivity { fullscreen.performClick() }
            awaitUi(scenario, "Leaving fullscreen restores the normal shortcut height") { activity ->
                imeVisible(activity) && fullscreen.contentDescription == "Pantalla completa" && toolbar.isShown &&
                    toolbar.height == normalToolbarHeight
            }
            scenario.onActivity { activity ->
                assertSame(input, activity.currentFocus)
                assertEquals(edited, input.text.toString())
                assertEquals(4, input.selectionEnd)
            }
            orient(scenario, false)
            awaitUi(scenario, "Portrait restores normal programming shortcuts") { activity ->
                imeVisible(activity) && toolbar.isShown && toolbar.height == normalToolbarHeight &&
                    activity.findViewById<View>(R.id.lessonHeader).visibility == View.VISIBLE &&
                    activity.findViewById<View>(R.id.lessonProgress).isShown
            }
            scenario.onActivity { activity ->
                assertTrue(input.hasFocus())
                assertSame(input, activity.currentFocus)
                assertEquals(edited, input.text.toString())
                assertEquals(4, input.selectionStart)
                assertEquals(4, input.selectionEnd)
                assertTrue("All compiler transitions preserve focus: $focusLosses", focusLosses.isEmpty())
                assertEquals(View.VISIBLE, activity.findViewById<View>(R.id.lessonHeader).visibility)
                assertTrue(activity.findViewById<View>(R.id.lessonProgress).isShown)
            }
        }
    }

    @Test fun inlineExerciseFullscreenRetainsSelectedInputAndRestoresNormalToolbar() {
        requireSwiftKey()
        ProgressRepository.saveCurrentPage(context, route, lesson, 3)
        ActivityScenario.launch<LessonActivity>(intent()).use { scenario ->
            orient(scenario, false)
            lateinit var input: EditText
            lateinit var fullscreen: View
            lateinit var body: View
            lateinit var toolbar: View
            lateinit var originalInputParent: ViewGroup
            var fullscreenHeightWithoutIme = 0
            scenario.onActivity { activity ->
                val content = activity.findViewById<ViewGroup>(R.id.lessonContent)
                input = tree(content).filterIsInstance<EditText>().single { it.tag == "cpp_code_input" }
                trackedInput = input
                originalInputParent = input.parent as ViewGroup
                fullscreen = tree(content).single { it.contentDescription == "Pantalla completa" }
                val card = generateSequence<View>(fullscreen) { it.parent as? View }.first { it.parent === content } as ViewGroup
                body = card.getChildAt(1)
                toolbar = activity.findViewById(R.id.programmingKeyboardBar)
                input.setText("abcdef")
                hideIme(activity, input)
                fullscreen.performClick()
            }
            orient(scenario, true)
            awaitUi(scenario, "Exercise fullscreen without IME uses the safe width") { activity ->
                !imeVisible(activity) && body.width == available(activity).width() && !body.isLayoutRequested
            }
            scenario.onActivity { activity ->
                assertEquals(View.GONE, toolbar.visibility)
                fullscreenHeightWithoutIme = available(activity).height()
                val scroll = generateSequence(body.parent) { it.parent }.filterIsInstance<android.widget.ScrollView>().first()
                assertTrue("No half-height split without IME in exercises", scroll.height > available(activity).height() / 2)
            }
            orient(scenario, false)
            val focusLosses = mutableListOf<String>()
            trackedFocusLosses = focusLosses
            scenario.onActivity { activity ->
                activity.window.decorView.viewTreeObserver.addOnGlobalFocusChangeListener { oldFocus, newFocus ->
                    if (oldFocus === input && newFocus !== input) focusLosses += ancestorDiagnostic(input, newFocus)
                }
                assertTrue(input.requestFocus())
                input.setSelection(2, 4)
                showIme(activity, input)
            }
            awaitUi(scenario, "Portrait exercise shows normal shortcuts above SwiftKey") { activity ->
                imeVisible(activity) && toolbar.isShown && toolbar.height > 0
            }
            val normalToolbarHeight = toolbar.height
            trackedNormalToolbarHeight = normalToolbarHeight
            orient(scenario, true)
            var diagnostic = "Compact exercise not yet sampled"
            awaitUi(scenario, "Exercise shortcuts compact and the selected caret remains visible", { diagnostic }) { activity ->
                val viewport = available(activity)
                val imeTop = actualImeTop()
                diagnostic = "viewport=$viewport, input=${bounds(input)}, body=${bounds(body)}, toolbar=${bounds(toolbar)}, losses=$focusLosses"
                imeVisible(activity) && toolbar.isShown && toolbar.height in 1..(viewport.height() / 2 + 1) &&
                    imeTop != null && viewport.height() < fullscreenHeightWithoutIme && abs(bounds(toolbar).bottom - imeTop) <= 3 &&
                    caretVisible(input, viewport, bounds(toolbar).top)
            }
            scenario.onActivity { activity ->
                assertTrue(input.hasFocus())
                assertSame(input, activity.currentFocus)
                assertSame(originalInputParent, input.parent)
                assertTrue("Exercise rotation never clears focus: $focusLosses", focusLosses.isEmpty())
                assertEquals("abcdef", input.text.toString())
                assertEquals(2, input.selectionStart)
                assertEquals(4, input.selectionEnd)
                assertEquals(available(activity).width(), body.width)
                assertTrue(toolbar.height <= available(activity).height() / 2 + 1)
                assertTrue("The physical SwiftKey window reduces exercise fullscreen height", available(activity).height() < fullscreenHeightWithoutIme)
                assertEquals("Exercise shortcuts end above the physical SwiftKey window", requireNotNull(actualImeTop()).toDouble(),
                    bounds(toolbar).bottom.toDouble(), 3.0)
                assertTrue("The exercise caret is above the physical IME: ${programmingDiagnostic(activity)}",
                    caretVisible(input, available(activity), requireNotNull(actualImeTop())))
                emitMetric("exercise landscape IME: H=${available(activity).height()}, " +
                    "toolbar=${toolbar.height}, input=${input.height}, body=${body.height}, viewport=${available(activity)}, ${imeDiagnostic(activity)}")
            }
            pressSwiftKeyTwo()
            awaitUi(scenario, "A real SwiftKey press edits the same exercise selection") { input.text.toString() == "ab2ef" }
            scenario.onActivity { activity -> hideIme(activity, input) }
            awaitUi(scenario, "Closing SwiftKey restores the exercise fullscreen viewport") { activity ->
                !imeVisible(activity) && toolbar.visibility == View.GONE
            }
            scenario.onActivity { activity -> showIme(activity, input) }
            awaitUi(scenario, "Reopened exercise shortcuts compact again") { activity ->
                imeVisible(activity) && toolbar.isShown && toolbar.height <= available(activity).height() / 2 + 1 &&
                    caretVisible(input, available(activity), bounds(toolbar).top)
            }
            orient(scenario, false)
            awaitUi(scenario, "Portrait fullscreen restores normal exercise shortcut height") { activity ->
                imeVisible(activity) && toolbar.isShown && toolbar.height == normalToolbarHeight
            }
            scenario.onActivity { activity ->
                assertTrue(input.hasFocus())
                assertSame(input, activity.currentFocus)
                assertEquals("ab2ef", input.text.toString())
                assertEquals(3, input.selectionStart)
                assertEquals(3, input.selectionEnd)
                fullscreen.performClick()
            }
            awaitUi(scenario, "Leaving exercise fullscreen restores its hierarchy while the same input and SwiftKey remain active") { activity ->
                fullscreen.contentDescription == "Pantalla completa" && input.parent === originalInputParent &&
                    toolbar.height == normalToolbarHeight && toolbar.isShown && imeVisible(activity) &&
                    input.hasFocus() && activity.currentFocus === input &&
                    caretVisible(input, available(activity), bounds(toolbar).top)
            }
            scenario.onActivity { activity ->
                assertTrue("The original exercise input retains focus when leaving fullscreen", input.hasFocus())
                assertSame(input, activity.currentFocus)
                assertTrue("SwiftKey remains open after leaving exercise fullscreen", imeVisible(activity))
                assertEquals("ab2ef", input.text.toString())
                assertEquals(3, input.selectionStart)
                assertEquals(3, input.selectionEnd)
                assertTrue("The restored normal exercise keeps its caret accessible: ${programmingDiagnostic(activity)}",
                    caretVisible(input, available(activity), bounds(toolbar).top))
            }
        }
    }

    @Test fun normalLandscapeWithSwiftKeyKeepsFullWidthNormalToolbarAndSelectedCaret() {
        requireSwiftKey()
        for (page in listOf(3, 7)) {
            ProgressRepository.saveCurrentPage(context, route, lesson, page)
            ActivityScenario.launch<LessonActivity>(intent()).use { scenario ->
                orient(scenario, false)
                lateinit var input: EditText
                lateinit var card: ViewGroup
                lateinit var body: View
                lateinit var toolbar: View
                lateinit var fullscreen: View
                val source = if (page == 7) "int main() {\n    return 12;\n}\n" else "abcdef"
                val focusLosses = mutableListOf<String>()
                trackedFocusLosses = focusLosses
                scenario.onActivity { activity ->
                    val content = activity.findViewById<ViewGroup>(R.id.lessonContent)
                    input = tree(content).filterIsInstance<EditText>().single { it.tag == "cpp_code_input" }
                    trackedInput = input
                    fullscreen = tree(content).single { it.contentDescription == "Pantalla completa" }
                    card = generateSequence<View>(fullscreen) { it.parent as? View }.first { it.parent === content } as ViewGroup
                    body = card.getChildAt(1)
                    toolbar = activity.findViewById(R.id.programmingKeyboardBar)
                    input.setText(source)
                    activity.window.decorView.viewTreeObserver.addOnGlobalFocusChangeListener { oldFocus, newFocus ->
                        if (oldFocus === input && newFocus !== input) focusLosses += ancestorDiagnostic(input, newFocus)
                    }
                    assertTrue(input.requestFocus())
                    input.setSelection(2, 4)
                    showIme(activity, input)
                }
                awaitUi(scenario, "The normal portrait programming toolbar appears on page ${page + 1}") { activity ->
                    imeVisible(activity) && toolbar.isShown && toolbar.height > 0
                }
                val normalToolbarHeight = toolbar.height
                trackedNormalToolbarHeight = normalToolbarHeight
                orient(scenario, true)
                var diagnostic = "Normal landscape IME geometry not yet sampled"
                awaitUi(scenario, "Normal landscape keeps its toolbar size and reveals the selected caret on page ${page + 1}",
                    { diagnostic }) { activity ->
                    val viewport = available(activity)
                    val scroll = activity.findViewById<View>(R.id.lessonScroll)
                    val textLayout = input.layout
                    val line = textLayout?.getLineForOffset(input.selectionEnd.coerceIn(0, input.text.length)) ?: 0
                    val lineHeight = textLayout?.let { it.getLineBottom(line) - it.getLineTop(line) }
                    diagnostic = "H=${viewport.height()}, viewport=$viewport, toolbar=${bounds(toolbar)}, " +
                        "card=${bounds(card)}, body=${bounds(body)}, input=${bounds(input)}, textSize=${input.textSize}, " +
                        "lineHeight=$lineHeight, padding=${input.totalPaddingTop}/${input.totalPaddingBottom}, " +
                        "inputScroll=${input.scrollX}/${input.scrollY}, selection=${input.selectionStart}/${input.selectionEnd}, losses=$focusLosses"
                    imeVisible(activity) && toolbar.isShown && toolbar.height == normalToolbarHeight &&
                        card.width == scroll.width && body.width == card.width &&
                        caretVisible(input, viewport, bounds(toolbar).top)
                }
                scenario.onActivity { activity ->
                    assertEquals("Normal mode remains outside fullscreen", "Pantalla completa", fullscreen.contentDescription)
                    assertEquals("Normal mode does not apply the fullscreen toolbar cap", normalToolbarHeight, toolbar.height)
                    assertEquals(bounds(activity.findViewById(R.id.lessonScroll)).left, bounds(card).left)
                    assertEquals(bounds(activity.findViewById(R.id.lessonScroll)).right, bounds(card).right)
                    assertTrue(input.hasFocus())
                    assertSame(input, activity.currentFocus)
                    assertTrue("Normal landscape never clears focus: $focusLosses", focusLosses.isEmpty())
                    assertEquals(source, input.text.toString())
                    assertEquals(2, input.selectionStart)
                    assertEquals(4, input.selectionEnd)
                    assertTrue("The selected normal landscape caret remains visible after settling: ${programmingDiagnostic(activity)}",
                        caretVisible(input, available(activity), bounds(toolbar).top))
                    emitMetric("normal page=${page + 1}: $diagnostic, ${imeDiagnostic(activity)}")
                }
                pressSwiftKeyTwo()
                val edited = source.replaceRange(2, 4, "2")
                awaitUi(scenario, "SwiftKey edits the same normal landscape programming control") {
                    input.text.toString() == edited && input.hasFocus()
                }
                scenario.onActivity { activity -> hideIme(activity, input) }
                awaitUi(scenario, "Closing IME releases all toolbar space in normal mode") { activity ->
                    !imeVisible(activity) && toolbar.visibility == View.GONE
                }
                orient(scenario, false)
                awaitUi(scenario, "Normal portrait restores the lesson header and progress on page ${page + 1}") { activity ->
                    activity.findViewById<View>(R.id.lessonHeader).visibility == View.VISIBLE &&
                        activity.findViewById<View>(R.id.lessonProgress).isShown
                }
                scenario.onActivity { activity ->
                    assertEquals(edited, input.text.toString())
                    assertEquals(3, input.selectionStart)
                    assertEquals(3, input.selectionEnd)
                    assertEquals("Pantalla completa", fullscreen.contentDescription)
                    assertEquals(View.VISIBLE, activity.findViewById<View>(R.id.lessonHeader).visibility)
                    assertTrue(activity.findViewById<View>(R.id.lessonProgress).isShown)
                }
            }
        }
    }

    private fun orient(scenario: ActivityScenario<LessonActivity>, landscape: Boolean) {
        scenario.onActivity { it.requestedOrientation = if (landscape) ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE else ActivityInfo.SCREEN_ORIENTATION_PORTRAIT }
        val expected = if (landscape) Configuration.ORIENTATION_LANDSCAPE else Configuration.ORIENTATION_PORTRAIT
        awaitUi(scenario, "Requested programming orientation settles") {
            it.resources.configuration.orientation == expected && it.hasWindowFocus() && !it.window.decorView.isLayoutRequested
        }
    }

    private fun available(activity: LessonActivity): Rect {
        val frame = Rect().also(activity.window.decorView::getWindowVisibleDisplayFrame)
        val decor = activity.window.decorView
        val rootInsets = ViewCompat.getRootWindowInsets(decor)
        val metrics = if (Build.VERSION.SDK_INT >= 30) activity.windowManager.currentWindowMetrics else null
        val metricsInsets = metrics?.let { WindowInsetsCompat.toWindowInsetsCompat(it.windowInsets, decor) }
        val types = WindowInsetsCompat.Type.systemBars() or WindowInsetsCompat.Type.displayCutout() or
            if (imeVisible(activity)) WindowInsetsCompat.Type.ime() else 0
        val rootSafe = rootInsets?.getInsets(types)
        val metricSafe = metricsInsets?.getInsets(types)
        val safe = metrics?.bounds?.let(::Rect) ?: bounds(decor)
        safe.left += maxOf(rootSafe?.left ?: 0, metricSafe?.left ?: 0)
        safe.top += maxOf(rootSafe?.top ?: 0, metricSafe?.top ?: 0)
        safe.right -= maxOf(rootSafe?.right ?: 0, metricSafe?.right ?: 0)
        safe.bottom -= maxOf(rootSafe?.bottom ?: 0, metricSafe?.bottom ?: 0)
        safe.intersect(bounds(activity.findViewById(android.R.id.content)))
        safe.intersect(frame)
        if (imeVisible(activity)) actualImeTop()?.let { safe.bottom = minOf(safe.bottom, it) }
        return safe
    }

    private fun imeAccessibilityBounds(): List<Rect> = instrumentation.uiAutomation.windows
        .filter { it.type == AccessibilityWindowInfo.TYPE_INPUT_METHOD }
        .map { Rect().also(it::getBoundsInScreen) }.filterNot { it.isEmpty }

    private fun actualImeTop(): Int? = imeAccessibilityBounds().minOfOrNull { it.top }

    private fun imeDiagnostic(activity: LessonActivity): String {
        val rootInsets = ViewCompat.getRootWindowInsets(activity.window.decorView)
        val metricInsets = if (Build.VERSION.SDK_INT >= 30) WindowInsetsCompat.toWindowInsetsCompat(
            activity.windowManager.currentWindowMetrics.windowInsets, activity.window.decorView) else null
        return "rootImeInsets=${rootInsets?.getInsets(WindowInsetsCompat.Type.ime())}, " +
            "rootImeVisible=${rootInsets?.isVisible(WindowInsetsCompat.Type.ime())}, " +
            "metricImeInsets=${metricInsets?.getInsets(WindowInsetsCompat.Type.ime())}, " +
            "imeAccessibilityBounds=${imeAccessibilityBounds()}"
    }

    private fun caretVisible(input: EditText, viewport: Rect, bottom: Int): Boolean {
        val caret = caretBounds(input) ?: return false
        val inputBounds = bounds(input)
        return caret.left >= maxOf(viewport.left, inputBounds.left) && caret.right <= minOf(viewport.right, inputBounds.right) &&
            caret.top >= maxOf(viewport.top, inputBounds.top) && caret.bottom <= minOf(bottom, inputBounds.bottom, viewport.bottom)
    }

    private fun caretBounds(input: EditText): RectF? {
        val layout = input.layout ?: return null
        if (input.selectionEnd !in 0..input.text.length) return null
        val line = layout.getLineForOffset(input.selectionEnd)
        val inputBounds = bounds(input)
        return RectF(
            inputBounds.left + input.totalPaddingLeft + layout.getPrimaryHorizontal(input.selectionEnd) - input.scrollX,
            inputBounds.top + input.totalPaddingTop + layout.getLineTop(line).toFloat() - input.scrollY,
            inputBounds.left + input.totalPaddingLeft + layout.getPrimaryHorizontal(input.selectionEnd) - input.scrollX + 1,
            inputBounds.top + input.totalPaddingTop + layout.getLineBottom(line).toFloat() - input.scrollY
        )
    }

    private fun bounds(view: View): Rect = IntArray(2).let { location ->
        view.getLocationOnScreen(location)
        Rect(location[0], location[1], location[0] + view.width, location[1] + view.height)
    }

    private fun ancestorDiagnostic(input: EditText, newFocus: View?): String = "newFocus=$newFocus, " +
        generateSequence<View>(input) { it.parent as? View }.joinToString(" > ") {
            "${it.javaClass.simpleName}(height=${it.height},measured=${it.measuredHeight})"
        }

    private fun imeVisible(activity: LessonActivity) =
        ViewCompat.getRootWindowInsets(activity.window.decorView)?.isVisible(WindowInsetsCompat.Type.ime()) == true

    private fun showIme(activity: LessonActivity, input: EditText) {
        (activity.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager).showSoftInput(input, InputMethodManager.SHOW_IMPLICIT)
    }

    private fun hideIme(activity: LessonActivity, input: View) {
        (activity.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager).hideSoftInputFromWindow(input.windowToken, 0)
    }

    private fun requireSwiftKey() = assertTrue("This integration test uses the installed SwiftKey keyboard",
        Settings.Secure.getString(context.contentResolver, Settings.Secure.DEFAULT_INPUT_METHOD).orEmpty().contains("swiftkey"))

    private fun emitMetric(message: String) {
        instrumentation.sendStatus(2, Bundle().apply { putString("stream", "ProgrammingLandscape: $message\n") })
    }

    private fun awaitUi(scenario: ActivityScenario<LessonActivity>, message: String, diagnostic: () -> String = { "" }, ready: (LessonActivity) -> Boolean) {
        val deadline = SystemClock.uptimeMillis() + 6_000
        var settled: Boolean
        var lifecycleDiagnostic = "Programming lifecycle not yet sampled"
        var previousGeometry: String? = null
        var stableSamples = 0
        do {
            instrumentation.waitForIdleSync()
            settled = false
            scenario.onActivity {
                val matches = ready(it)
                lifecycleDiagnostic = programmingDiagnostic(it)
                val toolbar = it.findViewById<View>(R.id.programmingKeyboardBar)
                val input = trackedInput
                val pendingLayout = it.window.decorView.isLayoutRequested || (toolbar.isShown && toolbar.isLayoutRequested) ||
                    input?.let { field -> generateSequence<View>(field) { ancestor -> ancestor.parent as? View }
                        .any { ancestor -> ancestor.isLayoutRequested } } == true
                val imeTop = actualImeTop()
                val imeEdgeSettled = !imeVisible(it) || !toolbar.isShown ||
                    (imeTop != null && abs(bounds(toolbar).bottom - imeTop) <= 3)
                val geometryReady = matches && !pendingLayout && imeEdgeSettled
                stableSamples = if (geometryReady && previousGeometry == lifecycleDiagnostic) stableSamples + 1 else 0
                previousGeometry = if (geometryReady) lifecycleDiagnostic else null
                settled = stableSamples >= 2
            }
            if (!settled) SystemClock.sleep(50)
        } while (!settled && SystemClock.uptimeMillis() < deadline)
        assertTrue("$message ${diagnostic()} | $lifecycleDiagnostic", settled)
    }

    private fun programmingDiagnostic(activity: LessonActivity): String {
        val toolbar = activity.findViewById<View>(R.id.programmingKeyboardBar)
        val header = activity.findViewById<View>(R.id.lessonHeader)
        val fullscreenHeader = tree(activity.window.decorView).firstOrNull { it.tag == "programming_fullscreen_header" }
        val frame = Rect().also(activity.window.decorView::getWindowVisibleDisplayFrame)
        val input = trackedInput
        val inputDetails = if (input == null) "input=null" else
            "input=${bounds(input)}, attached=${input.isAttachedToWindow}, focus=${input.hasFocus()}, " +
                "selection=${input.selectionStart}/${input.selectionEnd}, inputScroll=${input.scrollX}/${input.scrollY}, " +
                "textSize=${input.textSize}, padding=${input.totalPaddingTop}/${input.totalPaddingBottom}, " +
                "caretRect=${caretBounds(input)}, layoutHeight=${input.layout?.height}, source=${input.text.toString().replace("\n", "\\n")}, " +
                "layoutRequested=${input.isLayoutRequested}"
        return "orientation=${activity.resources.configuration.orientation}, IMEvisible=${imeVisible(activity)}, " +
            "frame=$frame, available=${available(activity)}, toolbar=${bounds(toolbar)}, toolbarHeight=${toolbar.height}, " +
            "normalToolbarHeight=$trackedNormalToolbarHeight, toolbarVisible=${toolbar.visibility}, toolbarShown=${toolbar.isShown}, " +
            "$inputDetails, currentFocus=${activity.currentFocus}, windowFocus=${activity.hasWindowFocus()}, " +
            "lessonHeader=${bounds(header)}/${header.visibility}, " +
            "fullscreenHeader=${fullscreenHeader?.let { "${bounds(it)}/${it.visibility}" }}, focusLosses=$trackedFocusLosses, " +
            imeDiagnostic(activity)
    }

    private data class ImeKey(val label: String, val description: String, val bounds: Rect, val clickable: Boolean)

    private fun imeKeys(): List<ImeKey> {
        val keys = mutableListOf<ImeKey>()
        fun collect(node: AccessibilityNodeInfo) {
            if (!node.refresh()) return
            if (node.isVisibleToUser) {
                val rect = Rect().also(node::getBoundsInScreen)
                if (!rect.isEmpty) keys += ImeKey(node.text?.toString().orEmpty(), node.contentDescription?.toString().orEmpty(), rect, node.isClickable)
            }
            for (index in 0 until node.childCount) node.getChild(index)?.let { child ->
                try { collect(child) } finally { child.recycle() }
            }
        }
        instrumentation.uiAutomation.windows.filter { it.type == AccessibilityWindowInfo.TYPE_INPUT_METHOD }.forEach { window ->
            window.root?.let { root -> try { collect(root) } finally { root.recycle() } }
        }
        return keys
    }

    private fun stableImeKeys(): List<ImeKey> {
        val deadline = SystemClock.uptimeMillis() + 3_000
        var previous = emptyList<ImeKey>()
        do {
            val keys = imeKeys()
            if (keys.isNotEmpty() && keys == previous) return keys
            previous = keys
            SystemClock.sleep(80)
        } while (SystemClock.uptimeMillis() < deadline)
        throw AssertionError("SwiftKey key bounds did not settle: $previous")
    }

    private fun pressSwiftKeyTwo() {
        var keys = stableImeKeys()
        var key = keys.filter { it.label == "2" || it.description == "2" }.maxByOrNull { it.clickable }
        if (key == null) {
            val modeLabels = setOf("123", "?123", "? 123", "Símbolos", "Números y símbolos", "Symbols", "Numbers and symbols")
            val mode = requireNotNull(keys.firstOrNull { it.label in modeLabels || it.description in modeLabels }) { "SwiftKey exposes no numeric mode: $keys" }
            tap(mode.bounds)
            keys = stableImeKeys()
            key = keys.filter { it.label == "2" || it.description == "2" }.maxByOrNull { it.clickable }
        }
        tap(requireNotNull(key) { "SwiftKey exposes no visible 2 key: $keys" }.bounds)
    }

    private fun tap(rect: Rect) {
        val downTime = SystemClock.uptimeMillis()
        val down = MotionEvent.obtain(downTime, downTime, MotionEvent.ACTION_DOWN, rect.exactCenterX(), rect.exactCenterY(), 0).apply { source = InputDevice.SOURCE_TOUCHSCREEN }
        try { assertTrue(instrumentation.uiAutomation.injectInputEvent(down, true)) } finally { down.recycle() }
        SystemClock.sleep(45)
        val up = MotionEvent.obtain(downTime, SystemClock.uptimeMillis(), MotionEvent.ACTION_UP, rect.exactCenterX(), rect.exactCenterY(), 0).apply { source = InputDevice.SOURCE_TOUCHSCREEN }
        try { assertTrue(instrumentation.uiAutomation.injectInputEvent(up, true)) } finally { up.recycle() }
        instrumentation.waitForIdleSync()
    }

    private fun tree(view: View): List<View> = listOf(view) + if (view is ViewGroup)
        (0 until view.childCount).flatMap { tree(view.getChildAt(it)) } else emptyList()

    private fun intent() = Intent(context, LessonActivity::class.java).putExtra("routeId", route).putExtra("courseId", course)
        .putExtra("unitId", "cpp_fundamentals__intro_programming").putExtra("lessonId", lesson).putExtra("lessonNumber", 1)
}
