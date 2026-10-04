package com.learngraphicsprogramming.app.math

import android.accessibilityservice.AccessibilityServiceInfo
import android.content.Context
import android.content.Intent
import android.content.pm.ActivityInfo
import android.content.res.Configuration
import android.graphics.Rect
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
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputMethodManager
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.LessonActivity
import com.learngraphicsprogramming.app.LessonTextSizeSettingsRepository
import com.learngraphicsprogramming.app.ProgressRepository
import com.learngraphicsprogramming.app.R
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

/** Screen taps on the installed Android keyboard exercise its actual composition protocol. */
@RunWith(AndroidJUnit4::class)
class MathRealImeBackspaceDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val automation get() = instrumentation.uiAutomation
    private val route = "math_physics"
    private val lesson = "math_course_01__arithmetic_fundamentals__math_puzzle_sandbox"
    private val snapshots = linkedMapOf<String, Map<String, *>>()
    private var originalServiceInfo: AccessibilityServiceInfo? = null

    @Before fun prepare() {
        for (command in listOf("input keyevent KEYCODE_WAKEUP", "wm dismiss-keyguard")) {
            ParcelFileDescriptor.AutoCloseInputStream(automation.executeShellCommand(command)).use { it.readBytes() }
        }
        listOf("learning_progress", "navigation_state", "lesson_text_size_settings").forEach {
            snapshots[it] = context.getSharedPreferences(it, Context.MODE_PRIVATE).all.toMap()
        }
        ProgressRepository.resetLesson(context, route, lesson)
        LessonTextSizeSettingsRepository.saveUsesGlobalScale(context, "math_course_01", true)
        LessonTextSizeSettingsRepository.saveGlobalScale(context, 100)
        originalServiceInfo = automation.serviceInfo
        val parcel = Parcel.obtain()
        try {
            requireNotNull(originalServiceInfo).writeToParcel(parcel, 0)
            parcel.setDataPosition(0)
            val updated = AccessibilityServiceInfo.CREATOR.createFromParcel(parcel)
            updated.flags = updated.flags or AccessibilityServiceInfo.FLAG_RETRIEVE_INTERACTIVE_WINDOWS
            automation.serviceInfo = updated
        } finally {
            parcel.recycle()
        }
    }

    @After fun restoreUserState() {
        originalServiceInfo?.let { automation.serviceInfo = it }
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

    @Test fun swiftKeyFirstRightFromEmptyIntegralUpperRetainsTheChildBoundary() {
        assertTrue("Use the installed SwiftKey IME for this regression",
            Settings.Secure.getString(context.contentResolver, Settings.Secure.DEFAULT_INPUT_METHOD).contains("swiftkey"))
        ActivityScenario.launch<LessonActivity>(intent()).use { scenario ->
            val editor = openEditor(scenario, "")
            lateinit var original: MathExpression.Sequence
            val upper = listOf(MathCursorStep(0, "upper"))
            scenario.onActivity {
                editor.perform(MathKeyboardAction.Integral)
                original = editor.model.expression
                assertEquals(MathCursor(upper, 0), editor.model.cursor)
                assertEquals(listOf("upper", "lower"), original.children.single().navigationChildren().map { it.id })
            }
            tapSlotNavigation(forward = true)
            await(scenario, "One physical toolbar RIGHT must remain on the upper child border",
                failureDetails = { "cursor=${editor.model.cursor}, presentation=${editor.model.cursorPresentation}" }) {
                editor.model.cursorPresentation == MathCursorPresentation.ChildBoundary(upper)
            }
            scenario.onActivity {
                assertSame("IME acknowledgements cannot consume a second navigation state", original, editor.model.expression)
                assertNull(editor.model.selectionRange)
            }
            tapSlotNavigation(forward = true)
            val lower = listOf(MathCursorStep(0, "lower"))
            await(scenario, "The next physical RIGHT reaches the real lower child") {
                editor.model.cursor == MathCursor(lower, 0) &&
                    editor.model.cursorPresentation == MathCursorPresentation.Interior(MathContentScope.Slot)
            }
            tapSlotNavigation(forward = true)
            await(scenario, "RIGHT exposes the lower child's own border") {
                editor.model.cursorPresentation == MathCursorPresentation.ChildBoundary(lower)
            }
            tapSlotNavigation(forward = true)
            await(scenario, "Only after both declared children does RIGHT reach the whole operator border") {
                editor.model.cursorPresentation == MathCursorPresentation.ParentBoundary(emptyList(), 0)
            }
            scenario.onActivity { assertTrue(editor.model.canNavigateNext) }
            tapSlotNavigation(forward = true)
            await(scenario, "RIGHT reaches the independent Sequence continuation while SwiftKey stays open") {
                editor.model.cursor == MathCursor(offset = 1) &&
                    editor.model.cursorPresentation == MathCursorPresentation.Neutral && editor.hasFocus()
            }
            scenario.onActivity {
                assertSame(original, editor.model.expression)
                assertFalse(editor.model.canNavigateNext)
                assertNull(editor.model.sequenceAt(listOf(MathCursorStep(0, "body"))))
            }
            tapImeKey("x")
            awaitLatex(scenario, editor, "\\int_{}^{}x")
            scenario.onActivity {
                assertEquals(listOf(original.children.single(), MathExpression.Variable("x")), editor.model.expression.children)
            }
        }
    }

    @Test fun swiftKeyRightFromFractionNumeratorReachesTheEmptyDenominatorBeforeParent() {
        assertTrue("Use the installed SwiftKey IME for this regression",
            Settings.Secure.getString(context.contentResolver, Settings.Secure.DEFAULT_INPUT_METHOD).contains("swiftkey"))
        ActivityScenario.launch<LessonActivity>(intent()).use { scenario ->
            val editor = openEditor(scenario, "\\frac{x+1}{}", fieldIndex = 0)
            val numerator = listOf(MathCursorStep(0, "numerator"))
            val denominator = listOf(MathCursorStep(0, "denominator"))
            lateinit var original: MathExpression.Sequence
            lateinit var realDenominator: MathExpression.Sequence
            scenario.onActivity {
                original = editor.model.expression
                realDenominator = (original.children.single() as MathExpression.Fraction).denominator
                editor.onCursorRequested?.invoke(MathCursor(numerator,
                    requireNotNull(editor.model.sequenceAt(numerator)).children.size))
            }
            tapSlotNavigation(forward = true)
            await(scenario, "Physical RIGHT leaves numerator at its child border") {
                editor.model.cursorPresentation == MathCursorPresentation.ChildBoundary(numerator)
            }
            tapSlotNavigation(forward = true)
            await(scenario, "Physical RIGHT reaches the empty denominator before the whole fraction") {
                editor.model.cursor == MathCursor(denominator, 0) &&
                    editor.model.cursorPresentation == MathCursorPresentation.Interior(MathContentScope.Slot) && editor.hasFocus()
            }
            scenario.onActivity {
                assertSame(original, editor.model.expression)
                assertSame(realDenominator, editor.model.sequenceAt(denominator))
                assertEquals(listOf("numerator", "denominator"), original.children.single().navigationChildren().map { it.id })
                assertTrue(realDenominator.children.isEmpty())
            }
            tapSlotNavigation(forward = false)
            await(scenario, "LEFT coherently returns across the just-traversed child edge") {
                editor.model.cursorPresentation == MathCursorPresentation.ChildBoundary(numerator)
            }
            tapSlotNavigation(forward = true)
            await(scenario, "RIGHT reenters the same real denominator") {
                editor.model.cursor == MathCursor(denominator, 0)
            }
            tapSlotNavigation(forward = true)
            await(scenario, "RIGHT exposes the denominator border") {
                editor.model.cursorPresentation == MathCursorPresentation.ChildBoundary(denominator)
            }
            tapSlotNavigation(forward = true)
            await(scenario, "ParentBoundary is reached only after both children") {
                editor.model.cursorPresentation == MathCursorPresentation.ParentBoundary(emptyList(), 0)
            }
            scenario.onActivity { assertSame(original, editor.model.expression) }
            tapSlotNavigation(forward = false)
            await(scenario, "LEFT returns to the denominator border") {
                editor.model.cursorPresentation == MathCursorPresentation.ChildBoundary(denominator)
            }
            tapSlotNavigation(forward = false)
            await(scenario, "LEFT enters the existing empty denominator") {
                editor.model.cursor == MathCursor(denominator, 0)
            }
            tapImeKey("2")
            awaitLatex(scenario, editor, "\\frac{x+1}{2}")
            scenario.onActivity {
                assertEquals("x+1", MathLatexSerializer.serialize((editor.model.expression.children.single() as MathExpression.Fraction).numerator))
                assertEquals(listOf(MathExpression.Number("2")), requireNotNull(editor.model.sequenceAt(denominator)).children)
            }
        }
    }

    @Test fun swiftKeyBackspaceOnTouchedSubscriptOnlyDeletesOneDigit() {
        assertTrue("Use the installed SwiftKey IME for this regression",
            Settings.Secure.getString(context.contentResolver, Settings.Secure.DEFAULT_INPUT_METHOD).contains("swiftkey"))
        ActivityScenario.launch<LessonActivity>(intent()).use { scenario ->
            scenario.onActivity { it.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_PORTRAIT }
            await(scenario, "Portrait window settles") {
                it.resources.configuration.orientation == Configuration.ORIENTATION_PORTRAIT && it.hasWindowFocus()
            }
            lateinit var editor: MathExpressionEditor
            var astChanges = 0
            scenario.onActivity { activity ->
                val content = activity.findViewById<ViewGroup>(R.id.lessonContent)
                val fields = tree(content).filterIsInstance<MathExpressionEditor>()
                assertEquals("Exercise page 17 contains the existing eight structured fields", 8, fields.size)
                editor = fields[1]
                assertTrue(editor.setLatex("x_{12}^{2}"))
                var observed = editor.model.expression
                val saveDraft = editor.onExpressionChanged
                editor.onExpressionChanged = {
                    if (observed != editor.model.expression) astChanges++
                    observed = editor.model.expression
                    saveDraft?.invoke()
                }
                assertTrue(editor.requestFocus())
                (activity.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager)
                    .showSoftInput(editor, InputMethodManager.SHOW_IMPLICIT)
            }
            await(scenario, "SwiftKey opens") { activity ->
                activity.hasWindowFocus() && editor.hasFocus() && ViewCompat.getRootWindowInsets(activity.window.decorView)
                    ?.isVisible(WindowInsetsCompat.Type.ime()) == true
            }
            tapSlot(scenario, editor, "lower", 1)
            tapImeDelete()
            awaitLatex(scenario, editor, "x_{1}^{2}")
            scenario.onActivity { assertEquals("One SwiftKey press makes one logical AST change", 1, astChanges) }
            tapImeDelete()
            awaitLatex(scenario, editor, "x_{}^{2}")
            scenario.onActivity { assertEquals(2, astChanges) }
            repeat(3) { tapImeDelete() }
            scenario.onActivity {
                assertEquals("Empty subscript remains valid and no digits reappear", canonical("x_{}^{2}"), editor.getLatex())
                assertEquals("lower", editor.model.cursor.path.last().slot)
                assertEquals("Empty Backspace produces no AST mutation", 2, astChanges)
            }
            tapImeKey("1")
            awaitLatex(scenario, editor, "x_{1}^{2}")
            tapSlot(scenario, editor, "lower", 1)
            tapImeDelete()
            awaitLatex(scenario, editor, "x_{}^{2}")
            scenario.onActivity { assertEquals("Typing and deleting the touched 1 each mutate once", 4, astChanges) }
        }
    }

    @Test fun swiftKeyEditsTheGenuineSelectionCreatedByProgressiveSlotNavigation() {
        ActivityScenario.launch<LessonActivity>(intent()).use { scenario ->
            val editor = openEditor(scenario, "x_{12}^{2}")
            tapSlot(scenario, editor, "lower", 1)
            scenario.onActivity { assertNull("Entering a slot never selects its contents", editor.model.selectionRange) }
            tapSlotNavigation(forward = false)
            await(scenario, "Left from END creates the explicit mathematical selection") {
                editor.model.selectionRange?.let { selection ->
                    selection.path == listOf(MathCursorStep(0, "lower")) && selection.start == 0 && selection.end == 1
                } == true
            }
            scenario.onActivity { assertEquals(canonical("x_{12}^{2}"), editor.getLatex()) }
            tapImeDelete()
            awaitLatex(scenario, editor, "x_{}^{2}")
            repeat(3) { tapImeDelete() }
            scenario.onActivity {
                assertEquals(canonical("x_{}^{2}"), editor.getLatex())
                assertNull(editor.model.selectionRange)
                assertTrue(editor.setLatex("x_{12}^{2}"))
            }
            tapSlot(scenario, editor, "lower", 1)
            tapSlotNavigation(forward = false)
            await(scenario, "The full mathematical selection is available for replacement") { editor.model.selectionRange != null }
            tapImeKey("3")
            awaitLatex(scenario, editor, "x_{3}^{2}")
            scenario.onActivity { assertNull(editor.model.selectionRange) }
            tapImeDelete()
            awaitLatex(scenario, editor, "x_{}^{2}")
        }
    }

    @Test fun swiftKeyBackspaceAtAnExteriorBoundaryRemovesTheAccidentalStructure() {
        ActivityScenario.launch<LessonActivity>(intent()).use { scenario ->
            val editor = openEditor(scenario, "x_{1}^{2}")
            val cases = listOf(
                Triple("x_{1}^{2}", "lower", "x^{2}"),
                Triple("x_{1}", "lower", "x"),
                Triple("x^{2}", "upper", "x"),
                Triple("\\frac{1}{2}", "denominator", "\\frac{1}{}"),
                Triple("\\sqrt[3]{x}", "index", "\\sqrt{x}"),
                Triple("\\sqrt{x}", "radicand", "")
            )
            cases.forEach { (source, slot, expected) ->
                scenario.onActivity { assertTrue(editor.setLatex(source)) }
                tapSlot(scenario, editor, slot, 1)
                tapSlotNavigation(forward = true)
                await(scenario, "Next puts the cursor outside $slot") {
                    editor.model.cursor.path.isEmpty() && editor.model.cursor.offset == 1 && editor.model.selectionRange == null
                        && editor.model.cursorPresentation == if (slot == "radicand")
                            MathCursorPresentation.ParentBoundary(emptyList(), 0)
                        else MathCursorPresentation.ChildBoundary(listOf(MathCursorStep(0, slot)), true)
                }
                scenario.onActivity { assertEquals("The exterior step changes only navigation", canonical(source), editor.getLatex()) }
                tapImeDelete()
                awaitLatex(scenario, editor, expected)
            }
        }
    }

    @Test fun swiftKeyBackspaceAtNeutralDeletesWholeScriptAndNumberGroups() {
        ActivityScenario.launch<LessonActivity>(intent()).use { scenario ->
            val editor = openEditor(scenario, "x_{1}^{2}")
            scenario.onActivity {
                assertEquals(MathCursorLevel.NEUTRAL, editor.model.navigationLevel)
                assertEquals(MathCursor(offset = 1), editor.model.cursor)
            }
            tapSlotNavigation(forward = false)
            await(scenario, "Actual left navigation opens the first declared upper child border") {
                editor.model.cursorPresentation == MathCursorPresentation.ChildBoundary(listOf(MathCursorStep(0, "upper"))) &&
                    editor.model.selectionRange == null
            }
            val base = listOf(MathCursorStep(0, "base"))
            val lower = listOf(MathCursorStep(0, "lower"))
            val interior = MathCursorPresentation.Interior(MathContentScope.Slot)
            val forwardStates = listOf(
                Triple(MathCursor(base, 0), null, interior),
                Triple(MathCursor(base, 1), MathSelection(base, 0, 1), interior),
                Triple(MathCursor(base, 1), null, interior),
                Triple(MathCursor(lower, 0), null, interior),
                Triple(MathCursor(lower, 1), MathSelection(lower, 0, 1), interior),
                Triple(MathCursor(lower, 1), null, interior),
                Triple(MathCursor(offset = 1, level = MathCursorLevel.CHILD_BOUNDARY), null,
                    MathCursorPresentation.ChildBoundary(lower)),
                Triple(MathCursor(offset = 1), null, MathCursorPresentation.ParentBoundary(emptyList(), 0))
            )
            for (state in forwardStates) {
                tapSlotNavigation(forward = true)
                await(scenario, "RIGHT visits the declared script checkpoint before neutral deletion: $state") {
                    Triple(editor.model.cursor, editor.model.selectionRange, editor.model.cursorPresentation) == state
                }
                scenario.onActivity { assertEquals(canonical("x_{1}^{2}"), editor.getLatex()) }
            }
            instrumentation.waitForIdleSync()
            tapImeDelete()
            awaitLatex(scenario, editor, "")
            scenario.onActivity {
                assertTrue(editor.setLatex("12"))
                assertEquals(MathCursorLevel.NEUTRAL, editor.model.navigationLevel)
                assertNull(editor.model.selectionRange)
            }
            instrumentation.waitForIdleSync()
            await(scenario, "The neutral Number(12) is served to the open SwiftKey keyboard") {
                editor.model.navigationLevel == MathCursorLevel.NEUTRAL && editor.model.cursor.offset == 1 &&
                    (it.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager).isActive(editor)
            }
            tapImeDelete()
            awaitLatex(scenario, editor, "")
            scenario.onActivity { assertTrue("The number group is gone; Backspace did not enter it", editor.model.expression.children.isEmpty()) }
        }
    }

    @Test fun swiftKeyNormalLettersAreImmediateParentsAndRapidLeftSelectsOnlyC() {
        ActivityScenario.launch<LessonActivity>(intent()).use { scenario ->
            val editor = openEditor(scenario, "x^{2}")
            lateinit var power: MathExpression
            var astChanges = 0
            val observedPrefixes = mutableListOf<List<MathExpression>>()
            val observedScopes = mutableListOf<MathContentScope?>()
            val mutationTimes = mutableListOf<Long>()
            var pendingDelivery: CountDownLatch? = null
            scenario.onActivity {
                power = editor.model.expression.children.single()
                var observed = editor.model.expression
                val saveDraft = editor.onExpressionChanged
                editor.onExpressionChanged = {
                    if (observed != editor.model.expression) {
                        astChanges++
                        observedPrefixes += editor.model.expression.children.toList()
                        observedScopes += editor.model.editingScope
                        mutationTimes += SystemClock.uptimeMillis()
                        pendingDelivery?.countDown()
                    }
                    observed = editor.model.expression
                    saveDraft?.invoke()
                }
            }
            "abc".forEachIndexed { index, letter ->
                val delivered = CountDownLatch(1)
                scenario.onActivity { pendingDelivery = delivered }
                val pressedAt = SystemClock.uptimeMillis()
                tapImeKey(letter.toString())
                val arrived = delivered.await(4, TimeUnit.SECONDS)
                val physicalButtons = if (arrived) emptyList() else imeNodes()
                    .filter { it.text == letter.toString() || it.description == letter.toString() }
                    .map { "${it.text}/${it.description} ${it.bounds}" }
                var diagnostic = ""
                scenario.onActivity {
                    diagnostic = "key=$letter pressedAt=$pressedAt now=${SystemClock.uptimeMillis()}, " +
                        "LaTeX='${editor.getLatex()}', AST=${editor.model.expression}, cursor=${editor.model.cursor}, " +
                        "scope=${editor.model.editingScope}, mutations=$astChanges at=$mutationTimes, " +
                        "focus=${editor.hasFocus()}, actualKeyButtons=$physicalButtons"
                }
                assertTrue("A physical SwiftKey '$letter' press must deliver an AST mutation; $diagnostic", arrived)
                scenario.onActivity {
                    assertEquals("The live AST already contains each independent parent after its physical key",
                        listOf(power) + "abc".take(index + 1).map { MathExpression.Variable(it.toString()) }, editor.model.expression.children)
                    assertEquals("The insertion callback itself observes the fully materialized AST",
                        listOf(power) + "abc".take(index + 1).map { MathExpression.Variable(it.toString()) }, observedPrefixes.last())
                    assertEquals("Each SwiftKey key produces one logical mutation", index + 1, astChanges)
                    assertEquals(MathContentScope.Atom(index + 1, index + 2), editor.model.editingScope)
                    assertEquals(MathContentScope.Atom(index + 1, index + 2), observedScopes.last())
                    pendingDelivery = null
                }
            }
            tapSlotNavigation(forward = false)
            await(scenario, "Left immediately after normal typing selects only c") {
                editor.model.selectionRange == MathSelection(emptyList(), 3, 4)
            }
            scenario.onActivity {
                assertEquals("c", requireNotNull(editor.onCreateInputConnection(EditorInfo())).getSelectedText(0).toString())
                assertEquals(3, astChanges)
                assertTrue(editor.setLatex("x^{2}"))
                astChanges = 0
                observedPrefixes.clear()
                observedScopes.clear()
                mutationTimes.clear()
            }
            await(scenario, "The reset parent is ready for consecutive SwiftKey presses") {
                editor.model.cursorPresentation == MathCursorPresentation.ParentBoundary(emptyList(), 0) &&
                    (it.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager).isActive(editor)
            }
            val keys = stableImeNodes()
            val keyBounds = "abc".map { letter ->
                requireNotNull(keys.filter { it.text == letter.toString() || it.description == letter.toString() }
                    .sortedByDescending { it.clickable }.firstOrNull()) { "SwiftKey exposes the normal '$letter' key" }.bounds
            }
            val left = requireNotNull(accessibleNodes(imeOnly = false)
                .firstOrNull { it.description == "Slot anterior" && it.clickable }).bounds
            // Resolve the live keys once, then inject consecutive physical presses and LEFT
            // without intermediate waits, retries, or model/editor calls.
            (keyBounds + left).forEach { bounds -> tap(bounds.exactCenterX(), bounds.exactCenterY(), waitForIdle = false) }
            var rapidDiagnostic = ""
            await(scenario, "Rapid a,b,c followed by LEFT observes the already updated AST and selects only c",
                failureDetails = { rapidDiagnostic }) {
                rapidDiagnostic = "AST=${editor.model.expression}, cursor=${editor.model.cursor}, " +
                    "scope=${editor.model.editingScope}, selection=${editor.model.selectionRange}, " +
                    "prefixes=$observedPrefixes, scopes=$observedScopes, mutations=$astChanges at=$mutationTimes, " +
                    "physicalKeys=$keyBounds, left=$left"
                editor.model.selectionRange == MathSelection(emptyList(), 3, 4)
            }
            scenario.onActivity {
                assertEquals(listOf(power) + "abc".map { MathExpression.Variable(it.toString()) }, editor.model.expression.children)
                assertEquals("Each rapid key materializes one parent in one logical mutation", 3, astChanges)
                assertEquals((1..3).map { count -> listOf(power) + "abc".take(count).map { MathExpression.Variable(it.toString()) } }, observedPrefixes)
                assertEquals(MathContentScope.Atom(3, 4), editor.model.editingScope)
            }
        }
    }

    private fun openEditor(scenario: ActivityScenario<LessonActivity>, source: String, fieldIndex: Int = 1): MathExpressionEditor {
        assertTrue("This regression requires the installed SwiftKey keyboard",
            Settings.Secure.getString(context.contentResolver, Settings.Secure.DEFAULT_INPUT_METHOD).contains("swiftkey"))
        scenario.onActivity { it.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_PORTRAIT }
        await(scenario, "Portrait window settles") {
            it.resources.configuration.orientation == Configuration.ORIENTATION_PORTRAIT && it.hasWindowFocus()
        }
        lateinit var editor: MathExpressionEditor
        scenario.onActivity { activity ->
            val fields = tree(activity.findViewById<ViewGroup>(R.id.lessonContent)).filterIsInstance<MathExpressionEditor>()
            assertEquals(8, fields.size)
            editor = fields[fieldIndex]
            assertTrue(editor.setLatex(source))
            assertTrue(editor.requestFocus())
            (activity.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager)
                .showSoftInput(editor, InputMethodManager.SHOW_IMPLICIT)
        }
        await(scenario, "SwiftKey opens") { activity ->
            activity.hasWindowFocus() && editor.hasFocus() &&
                (activity.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager).isActive(editor) &&
                ViewCompat.getRootWindowInsets(activity.window.decorView)?.isVisible(WindowInsetsCompat.Type.ime()) == true
        }
        return editor
    }

    private fun tapSlotNavigation(forward: Boolean) {
        val description = if (forward) "Slot siguiente" else "Slot anterior"
        repeat(20) {
            val match = accessibleNodes(imeOnly = false).firstOrNull { it.description == description && it.clickable }
            if (match != null) {
                tap(match.bounds.exactCenterX(), match.bounds.exactCenterY())
                return
            }
            SystemClock.sleep(100)
        }
        fail("The actual toolbar exposes no $description button")
    }

    private fun tapImeDelete() {
        var nodes = emptyList<ImeNode>()
        repeat(20) {
            nodes = imeNodes()
            val match = nodes.filter {
                Regex("(?i)(backspace|delete|borrar|retroceso|eliminar)").containsMatchIn(it.text + " " + it.description)
            }.sortedByDescending { it.clickable }.firstOrNull()
            if (match != null) {
                tap(match.bounds.exactCenterX(), match.bounds.exactCenterY())
                return
            }
            SystemClock.sleep(100)
        }
        fail("SwiftKey exposes no accessible Backspace: " + nodes.map { it.text.ifEmpty { it.description } }
            .filter { it.isNotBlank() }.distinct().joinToString())
    }
    private fun tapSlot(scenario: ActivityScenario<LessonActivity>, editor: MathExpressionEditor, slot: String, offset: Int) {
        val target = MathCursor(listOf(MathCursorStep(0, slot)), offset)
        // Page 17 scrolls manually. Align the actual field after IME/layout changes, in either direction.
        repeat(4) {
            scenario.onActivity { activity ->
                val caret = requireNotNull(editor.expressionLayout.caretBounds(target))
                val location = IntArray(2).also(editor::getLocationOnScreen)
                val scroll = generateSequence(editor.parent) { it.parent }
                    .filterIsInstance<android.widget.ScrollView>().first()
                val viewport = Rect().also { assertTrue(scroll.getGlobalVisibleRect(it)) }
                val frame = Rect().also(activity.window.decorView::getWindowVisibleDisplayFrame)
                assertTrue(viewport.intersect(frame))
                viewport.bottom = maxOf(viewport.top + 1, viewport.bottom - MathWidgetUi.dp(activity, 120))
                val caretY = location[1] + editor.expressionBaseline + caret.centerY()
                scroll.scrollBy(0, kotlin.math.round(caretY - viewport.exactCenterY()).toInt())
            }
            instrumentation.waitForIdleSync()
            SystemClock.sleep(80)
        }
        instrumentation.waitForIdleSync()
        var x = 0f
        var y = 0f
        scenario.onActivity {
            val caret = requireNotNull(editor.expressionLayout.caretBounds(target))
            val location = IntArray(2).also(editor::getLocationOnScreen)
            x = location[0] + editor.expressionOriginX + caret.centerX()
            y = location[1] + editor.expressionBaseline + caret.centerY()
            val visible = Rect()
            assertTrue("Selected structural slot is visible above the IME; caret=($x,$y)", editor.getGlobalVisibleRect(visible))
            assertTrue("A screen tap must land on the visible editor $visible; caret=($x,$y)", visible.contains(x.toInt(), y.toInt()))
        }
        tap(x, y)
        await(scenario, "A touch selects $slot at offset $offset") { editor.hasFocus() && editor.model.cursor == target }
    }

    private data class ImeNode(val text: String, val description: String, val bounds: Rect, val clickable: Boolean)

    private fun imeNodes(): List<ImeNode> = accessibleNodes(imeOnly = true)

    private fun accessibleNodes(imeOnly: Boolean): List<ImeNode> {
        val result = mutableListOf<ImeNode>()
        fun collect(node: AccessibilityNodeInfo) {
            if (!node.refresh()) return
            if (node.isVisibleToUser) {
                val bounds = Rect().also(node::getBoundsInScreen)
                if (!bounds.isEmpty) result += ImeNode(node.text?.toString().orEmpty(),
                    node.contentDescription?.toString().orEmpty(), bounds, node.isClickable)
            }
            for (index in 0 until node.childCount) {
                val child = node.getChild(index) ?: continue
                try { collect(child) } finally { child.recycle() }
            }
        }
        automation.windows.filter { (it.type == AccessibilityWindowInfo.TYPE_INPUT_METHOD) == imeOnly }.forEach { window ->
            val root = window.root ?: return@forEach
            try { collect(root) } finally { root.recycle() }
        }
        return result
    }

    /** Keys are resolved from the live IME tree; no hardware/input-text substitute is used. */
    private fun tapImeKey(key: String) {
        var nodes = emptyList<ImeNode>()
        var switched = false
        repeat(20) {
            nodes = stableImeNodes()
            val match = nodes.filter { it.text == key || it.description == key }
                .sortedByDescending { it.clickable }.firstOrNull()
            if (match != null) {
                tap(match.bounds.exactCenterX(), match.bounds.exactCenterY())
                return
            }
            if (!switched && it >= 3) {
                val modeLabels = if (key.single().isDigit())
                    setOf("123", "?123", "? 123", "Símbolos", "Números y símbolos", "Symbols", "Numbers and symbols")
                else setOf("ABC", "abc", "Letras", "Letters", "Alfabeto")
                val mode = nodes.firstOrNull { it.text in modeLabels || it.description in modeLabels }
                if (mode != null) {
                    tap(mode.bounds.exactCenterX(), mode.bounds.exactCenterY())
                    switched = true
                }
            }
            SystemClock.sleep(100)
        }
        val installedIme = Settings.Secure.getString(context.contentResolver, Settings.Secure.DEFAULT_INPUT_METHOD)
        fail("The installed IME ($installedIme) does not expose a visible accessible '$key' key. " +
            "This real-IME test requires accessible keyboard keys; labels: " +
            nodes.map { it.text.ifEmpty { it.description } }.filter { it.isNotBlank() }.distinct().joinToString())
    }

    /** Synchronizes only the live keyboard UI, before any physical character press. */
    private fun stableImeNodes(): List<ImeNode> {
        var previous = emptyList<ImeNode>()
        var previousWindows = emptyList<Rect>()
        repeat(30) {
            val nodes = imeNodes()
            val windows = automation.windows.filter { it.type == AccessibilityWindowInfo.TYPE_INPUT_METHOD }
                .map { window -> Rect().also(window::getBoundsInScreen) }
            if (nodes.isNotEmpty() && nodes == previous && windows == previousWindows) return nodes
            previous = nodes
            previousWindows = windows
            SystemClock.sleep(80)
        }
        throw AssertionError("The live IME key labels/bounds did not settle before injection: windows=$previousWindows, nodes=$previous")
    }

    private fun tap(x: Float, y: Float, waitForIdle: Boolean = true) {
        val downTime = SystemClock.uptimeMillis()
        val down = MotionEvent.obtain(downTime, downTime, MotionEvent.ACTION_DOWN, x, y, 0)
        down.source = InputDevice.SOURCE_TOUCHSCREEN
        try { assertTrue(automation.injectInputEvent(down, true)) } finally { down.recycle() }
        SystemClock.sleep(45)
        val up = MotionEvent.obtain(downTime, SystemClock.uptimeMillis(), MotionEvent.ACTION_UP, x, y, 0)
        up.source = InputDevice.SOURCE_TOUCHSCREEN
        try { assertTrue(automation.injectInputEvent(up, true)) } finally { up.recycle() }
        if (waitForIdle) instrumentation.waitForIdleSync()
    }

    private fun canonical(source: String) = MathLatexSerializer.serialize((MathLatexParser.parse(source) as MathLatexParser.Result.Success).expression)

    private fun awaitLatex(scenario: ActivityScenario<LessonActivity>, editor: MathExpressionEditor, expected: String) {
        var diagnostic = "Editor state has not yet been sampled"
        await(scenario, "The actual IME produces $expected exactly once", failureDetails = { diagnostic }) {
            diagnostic = "Actual LaTeX='${editor.getLatex()}', cursor=${editor.model.cursor}, " +
                "presentation=${editor.model.cursorPresentation}, selection=${editor.model.selectionRange}, " +
                "focus=${editor.hasFocus()}, IME active=" +
                (it.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager).isActive(editor)
            editor.getLatex() == canonical(expected) && editor.hasFocus()
        }
        scenario.onActivity { assertEquals(canonical(expected), editor.getLatex()) }
    }

    private fun await(scenario: ActivityScenario<LessonActivity>, message: String,
        failureDetails: (() -> String)? = null, condition: (LessonActivity) -> Boolean) {
        var ready = false
        repeat(40) {
            if (!ready) {
                SystemClock.sleep(100)
                scenario.onActivity { ready = condition(it) }
            }
        }
        assertTrue(message + (failureDetails?.let { " — " + it() } ?: ""), ready)
    }

    private fun tree(view: View): List<View> = listOf(view) + if (view is ViewGroup)
        (0 until view.childCount).flatMap { tree(view.getChildAt(it)) } else emptyList()

    private fun intent() = Intent(context, LessonActivity::class.java)
        .putExtra("routeId", route).putExtra("courseId", "math_course_01")
        .putExtra("unitId", "arithmetic_fundamentals").putExtra("lessonId", lesson).putExtra("lessonNumber", 4)
}
