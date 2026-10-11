package com.learngraphicsprogramming.app

import android.app.KeyguardManager
import android.content.Context
import android.content.Intent
import android.content.pm.ActivityInfo
import android.content.res.Configuration
import android.graphics.Rect
import android.os.SystemClock
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.view.inputmethod.InputMethodManager
import android.widget.CheckBox
import android.widget.EditText
import android.widget.Spinner
import android.widget.TextView
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.test.core.app.ActivityScenario
import androidx.test.espresso.Espresso.onView
import androidx.test.espresso.action.ViewActions.click
import androidx.test.espresso.action.ViewActions.closeSoftKeyboard
import androidx.test.espresso.action.ViewActions.replaceText
import androidx.test.espresso.matcher.RootMatchers.isDialog
import androidx.test.espresso.matcher.ViewMatchers.withId
import androidx.test.espresso.matcher.ViewMatchers.withTagValue
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.narration.NarrationSettingsRepository
import com.learngraphicsprogramming.app.runtime.CppCompiler
import java.io.File
import org.json.JSONArray
import org.json.JSONObject
import org.hamcrest.Matchers.equalTo
import org.junit.After
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/**
 * Acceptance checks through the real curricular renderer and its Run listener.
 * Published lesson JSON supplies the reading/diferencia/sobres/serie projects.
 * Only technical stream/legacy fixtures use in-memory content. Preferences are
 * restored after every test, including drafts, attempts and course progress.
 */
@RunWith(AndroidJUnit4::class)
class CppCurricularLabsDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val snapshots = linkedMapOf<String, Map<String, *>>()
    private var originalPreferenceFiles = emptySet<String>()

    @Before fun preserveUserState() {
        assertEquals("Usar -Plgp.curricularQa=true para aislar los datos del usuario",
            "com.learngraphicsprogramming.app.curricularqa", context.packageName)
        originalPreferenceFiles = preferenceFiles()
        (originalPreferenceFiles + KNOWN_PREFERENCES).forEach { name ->
            snapshots[name] = context.getSharedPreferences(name, Context.MODE_PRIVATE).all.mapValues { (_, value) ->
                if (value is Set<*>) value.toSet() else value
            }
        }
        val keyguard = context.getSystemService(Context.KEYGUARD_SERVICE) as KeyguardManager
        assertFalse("Unlock the phone before renderer acceptance checks", keyguard.isDeviceLocked)
        NarrationSettingsRepository.saveEnabled(context, false)
        assertTrue(context.getSharedPreferences(DRAFTS, Context.MODE_PRIVATE).edit().clear().commit())
        assertTrue(context.getSharedPreferences(ATTEMPTS, Context.MODE_PRIVATE).edit()
            .putBoolean("unlimited", false).putFloat("reserve_$ROUTE", 4f)
            .putLong("updated_at_$ROUTE", System.currentTimeMillis()).commit())
    }

    @After fun restoreUserState() {
        // JUnit también ejecuta After cuando Before rechaza un paquete no aislado.
        if (context.packageName != "com.learngraphicsprogramming.app.curricularqa") return
        (preferenceFiles() + snapshots.keys).forEach { name ->
            if (name !in originalPreferenceFiles) {
                assertTrue("Remove only preferences created by this test: $name", context.deleteSharedPreferences(name))
            } else {
                val editor = context.getSharedPreferences(name, Context.MODE_PRIVATE).edit().clear()
                snapshots.getValue(name).forEach { (key, value) ->
                    when (value) {
                        is String -> editor.putString(key, value)
                        is Int -> editor.putInt(key, value)
                        is Long -> editor.putLong(key, value)
                        is Float -> editor.putFloat(key, value)
                        is Boolean -> editor.putBoolean(key, value)
                        is Set<*> -> editor.putStringSet(key, value.filterIsInstance<String>().toSet())
                        else -> error("Unsupported preference value for $name/$key")
                    }
                }
                assertTrue("Restore user preferences: $name", editor.commit())
            }
        }
    }

    @Test fun authoredReadingLabConsumesFreshPreparedInputAndRetainsTheDraft() {
        withAsset(READING, { it.optString("initialStandardInput") == "Rita 4" }) { scenario, location ->
            runWithInput(scenario, "Rita 4", "Rita: 7")
            runWithInput(scenario, "Leo\n2\n", "Leo: 5")
            runWithInput(scenario, "  Rita\t4\n", "Rita: 7")
            runWithInput(scenario, "  Rita\t4\n", "Rita: 7")
            var source = ""
            scenario.onActivity { activity ->
                source = editor(activity).text.toString() + "\n// draft survives recreation\n"
                editor(activity).setText(source)
                input(activity).setText("Leo 2")
            }
            scenario.recreate()
            scenario.onActivity { activity ->
                assertEquals(location.page, field(activity, "currentPage"))
                assertEquals(source, editor(activity).text.toString())
                assertEquals("Leo 2", input(activity).text.toString())
            }
            assertSuccessful(run(scenario), "Leo: 5")
        }
    }

    @Test fun preparedInputHandlesEmptyMalformedUnicodeAndSeparatesAllOutputChannels() {
        withFixture(STREAM_SOURCE, "") { scenario ->
            runWithInput(scenario, "", "EMPTY")
            runWithInput(scenario, "muchos\n", "INVALID")
            runWithInput(scenario, "7\nlínea con espacios ñ\n", "7|línea con espacios ñ|EOF", "stream stderr\n")
            runWithInput(scenario, "8\ntexto\nextra\n", "8|texto|EXTRA", "stream stderr\n")
            runWithInput(scenario, "7\nlínea con espacios ñ\n", "7|línea con espacios ñ|EOF", "stream stderr\n")
            scenario.onActivity { editor(it).setText("int main() { return missing_curricular_identifier; }") }
            val invalid = run(scenario)
            assertEquals("COMPILATION", invalid.phase)
            assertTrue(invalid.compilation, invalid.compilation.contains("missing_curricular_identifier"))
            assertTrue("Source location survives rendering: ${invalid.compilation}",
                Regex("main\\.cpp:1:\\d+: error:").containsMatchIn(invalid.compilation))
            assertEquals("No program output is fabricated after failed compilation", "", invalid.stdout)
            assertEquals("Compiler diagnostics belong to the compilation pane", "", invalid.stderr)
            scenario.onActivity {
                editor(it).setText("#include <iostream>\nint main() { std::cout << \"out\"; std::cerr << \"err\"; return 3; }")
            }
            val exited = run(scenario)
            assertEquals(exited.status, exited.finishedStatus(3))
            assertEquals("", exited.compilation)
            assertEquals("out", exited.stdout)
            assertEquals("err", exited.stderr)
        }
    }

    @Test fun legacyInitialCodeStillRunsWithoutPreparedInputOrAProject() {
        withFixture("#include <iostream>\nint main() { std::cout << \"legacy single source\"; }") { scenario ->
            scenario.onActivity { activity ->
                assertEquals("", input(activity).text.toString())
                assertTrue(descendants(content(activity)).none { it.tag?.toString()?.startsWith("cpp_file_") == true })
            }
            assertSuccessful(run(scenario), "legacy single source")
        }
    }

    @Test fun authoredDiferenciaCompilesDistinctUnitsAndDetectsFreshHeaderAndImplementationEdits() {
        withProject(DIFERENCIA, "diferencia.h") { scenario, _ ->
            assertSuccessful(run(scenario), "7")
            editFile(scenario, "diferencia.cpp") { source ->
                assertTrue(source, source.contains("primero - segundo"))
                source.replace("primero - segundo", "primero + segundo")
            }
            assertSuccessful(run(scenario), "11")
            editFile(scenario, "main.cpp") { "#include \"diferencia.h\"\n$it" }
            assertSuccessful(run(scenario), "11")
            editFile(scenario, "diferencia.h") { it.replace("int diferencia(", "double diferencia(") }
            assertCompilationFailure(run(scenario), "diferencia.cpp", "error:")
            editFile(scenario, "diferencia.h") { it.replace("double diferencia(", "int diferencia(") }
            assertSuccessful(run(scenario), "11")
            enableSource(scenario, "diferencia.cpp", false)
            enableSource(scenario, "main.cpp", false)
            assertConfigurationFailure(scenario)
            enableSource(scenario, "main.cpp", true)
            assertLinkFailure(run(scenario), "diferencia")
            enableSource(scenario, "diferencia.cpp", true)
            editFile(scenario, "diferencia.cpp") { it.replace("int diferencia(", "double diferencia(") }
            assertCompilationFailure(run(scenario), "diferencia.cpp", "error:")
            editFile(scenario, "diferencia.cpp") { it.replace("double diferencia(", "int diferencia(") }
            editFile(scenario, "main.cpp") { it.replace("diferencia.h", "missing_curricular_header.h") }
            assertCompilationFailure(run(scenario), "main.cpp", "missing_curricular_header.h")
        }
    }

    @Test fun optionalClang21UsesTheSameRendererForPreparedInputAndProjectLinking() {
        assumeTrue("This check needs the executable optional Clang 21 split",
            CppCompiler.isOptionalClang21Available(context))
        withAsset(READING, { it.optString("initialStandardInput") == "Rita 4" }) { scenario, _ ->
            selectOptionalCompiler(scenario)
            runWithInput(scenario, "Rita 4", "Rita: 7")
            runWithInput(scenario, "Leo 2", "Leo: 5")
        }
        // Different draft identities: the compiler choice and input cannot cross lessons.
        withProject(DIFERENCIA, "diferencia.h") { scenario, _ ->
            selectOptionalCompiler(scenario)
            assertSuccessful(run(scenario), "7")
            enableSource(scenario, "diferencia.cpp", false)
            assertLinkFailure(run(scenario), "diferencia")
            enableSource(scenario, "diferencia.cpp", true)
            assertSuccessful(run(scenario), "7")
        }
    }

    @Test fun projectFilesSelectionInputAndEditsSurvivePageChangesAndRecreation() {
        withProject(DIFERENCIA, "diferencia.h") { scenario, location ->
            var header = ""
            var implementation = ""
            editFile(scenario, "diferencia.h") { (it + "\n// edited shared interface\n").also { text -> header = text } }
            editFile(scenario, "diferencia.cpp") {
                it.replace("primero - segundo", "primero + segundo").also { text -> implementation = text }
            }
            scenario.onActivity { input(it).setText("prepared draft\nñ") }
            scenario.onActivity { activity ->
                showPage(activity, if (location.page == 0) 1 else 0)
                showPage(activity, location.page)
                assertEquals(implementation, editor(activity).text.toString())
                assertEquals("prepared draft\nñ", input(activity).text.toString())
            }
            scenario.recreate()
            scenario.onActivity { activity ->
                assertEquals(location.page, field(activity, "currentPage"))
                assertEquals("The active file is restored", implementation, editor(activity).text.toString())
                assertEquals("prepared draft\nñ", input(activity).text.toString())
                assertEquals("Each file remains available", 3,
                    descendants(content(activity)).count { it.tag?.toString()?.startsWith("cpp_file_") == true })
            }
            assertEquals(header, readFile(scenario, "diferencia.h"))
            assertEquals(implementation, readFile(scenario, "diferencia.cpp"))
            assertSuccessful(run(scenario), "11")
        }
        withProject(INTERFACES, "sobres.h") { other, _ ->
            assertEquals("A different lesson owns a different input draft", "", currentInput(other))
            assertSuccessful(run(other), "Varios sobres: 3")
        }
    }

    @Test fun authoredSobresProjectChecksDomainCapacityAndDifferentFailureStages() {
        withProject(INTERFACES, "sobres.h") { scenario, _ ->
            assertSuccessful(run(scenario), "Varios sobres: 3")
            val main = readFile(scenario, "main.cpp")
            val implementation = readFile(scenario, "sobres.cpp")
            val header = readFile(scenario, "sobres.h")
            editFile(scenario, "main.cpp") { SOBRES_BOUNDARIES }
            assertSuccessful(run(scenario), "Sin tarjetas: 0\nUn sobre: 1\nVarios sobres: 2\nVarios sobres: 3\nVarios sobres: 10")
            editFile(scenario, "main.cpp") { main }
            editFile(scenario, "sobres.cpp") { implementation.replace("/ 12", "/ 20").replace("% 12", "% 20") }
            assertSuccessful(run(scenario), "Varios sobres: 2")
            editFile(scenario, "sobres.cpp") { implementation }
            editFile(scenario, "main.cpp") { main.replace("#include \"sobres.h\"", "") }
            assertCompilationFailure(run(scenario), "main.cpp", "descripcion_sobres")
            editFile(scenario, "main.cpp") { main }
            editFile(scenario, "sobres.h") { header.replace("int sobres_necesarios(int tarjetas);", "") }
            assertCompilationFailure(run(scenario), "main.cpp", "sobres_necesarios")
            editFile(scenario, "sobres.h") { header }
            enableSource(scenario, "sobres.cpp", false)
            assertLinkFailure(run(scenario), "sobres_necesarios")
            enableSource(scenario, "sobres.cpp", true)
            val bodyStart = implementation.indexOf("int sobres_necesarios(")
            val bodyEnd = implementation.indexOf("\n\nstd::string descripcion_sobres", bodyStart)
            assertTrue("The published implementation contains the ordinary function body", bodyStart >= 0 && bodyEnd > bodyStart)
            val body = implementation.substring(bodyStart, bodyEnd)
            assertTrue("The shared interface declares the same function", header.contains("int sobres_necesarios(int tarjetas);"))
            editFile(scenario, "sobres.h") { header.replace("int sobres_necesarios(int tarjetas);", body) }
            editFile(scenario, "sobres.cpp") { implementation.removeRange(bodyStart, bodyEnd) }
            val duplicate = run(scenario)
            assertEquals("LINK", duplicate.phase)
            assertTrue(duplicate.compilation, duplicate.compilation.contains("duplicate symbol", ignoreCase = true))
            assertTrue(duplicate.compilation, duplicate.compilation.contains("sobres_necesarios"))
            assertEquals("", duplicate.stdout)
            editFile(scenario, "sobres.h") { header }
            editFile(scenario, "sobres.cpp") { implementation }
            assertSuccessful(run(scenario), "Varios sobres: 3")
        }
    }

    @Test fun authoredSerieProjectTransfersTheInterfaceAndChecksBothDomainBoundaries() {
        withProject(INTERFACES, "serie.h") { scenario, _ ->
            assertSuccessful(run(scenario), "En curso: 2\nTerminada: 0\nTerminada: 0")
            editFile(scenario, "main.cpp") { SERIE_BOUNDARIES }
            assertSuccessful(run(scenario), "En curso: 2\nTerminada: 0\nTerminada: 0\nEn curso: 40\nTerminada: 0")
            editFile(scenario, "main.cpp") { SERIE_TRANSFER }
            assertSuccessful(run(scenario), "En curso: 8\nTerminada: 0")
        }
    }

    @Test fun virtualFileCreationRejectsEscapesAndDeletionRetainsAHeadersOnlyDraft() {
        withProject(DIFERENCIA, "diferencia.h") { scenario, location ->
            val main = readFile(scenario, "main.cpp")
            assertSuccessful(run(scenario), "7")
            scenario.onActivity { tagged(it, "cpp_add_file").performClick() }
            onView(withTagValue(equalTo("cpp_new_file_name"))).inRoot(isDialog())
                .perform(replaceText("../outside.cpp"), closeSoftKeyboard())
            onView(withId(android.R.id.button1)).inRoot(isDialog()).perform(click())
            onView(withTagValue(equalTo("cpp_new_file_name"))).inRoot(isDialog()).check { view, error ->
                if (error != null) throw error
                assertNotNull("A path outside the project remains rejected in the dialog", (view as EditText).error)
            }
            onView(withId(android.R.id.button2)).inRoot(isDialog()).perform(click())
            scenario.onActivity { activity ->
                assertEquals(3, fileCount(activity))
                assertTrue(descendants(content(activity)).none { it.tag == "cpp_file_../outside.cpp" })
            }
            createFile(scenario, "helper.h", "#ifndef CURRICULAR_HELPER_H\n#define CURRICULAR_HELPER_H\nint auxiliary(int value);\n#endif\n")
            createFile(scenario, "helper.cpp", "#include \"helper.h\"\nint auxiliary(int value) { return value * 2; }\n")
            scenario.onActivity { activity ->
                assertEquals(5, fileCount(activity))
                assertTrue("A newly created .cpp is selected for compilation",
                    (tagged(activity, "cpp_source_enabled") as CheckBox).isChecked)
            }
            editFile(scenario, "main.cpp") {
                "#include \"helper.h\"\n" + main.replace("diferencia(9, 2)", "auxiliary(diferencia(9, 2))")
            }
            assertSuccessful(run(scenario), "14")
            readFile(scenario, "helper.cpp")
            scenario.recreate()
            scenario.onActivity { activity ->
                assertEquals(location.page, field(activity, "currentPage"))
                assertEquals(5, fileCount(activity))
                assertEquals("helper.cpp", editor(activity).contentDescription.toString())
                assertTrue(editor(activity).text.toString().contains("value * 2"))
            }
            deleteFile(scenario, "helper.cpp")
            assertLinkFailure(run(scenario), "auxiliary")
            editFile(scenario, "main.cpp") { main }
            deleteFile(scenario, "helper.h")
            assertSuccessful(run(scenario), "7")
            deleteFile(scenario, "main.cpp")
            deleteFile(scenario, "diferencia.cpp")
            assertConfigurationFailure(scenario)
            scenario.recreate()
            scenario.onActivity { activity ->
                assertEquals("A headers-only project remains an editable saved draft", 1, fileCount(activity))
                assertEquals("diferencia.h", editor(activity).contentDescription.toString())
                assertTrue(editor(activity).text.toString().contains("int diferencia("))
            }
            assertConfigurationFailure(scenario)
        }
    }

    @Test fun multiFileFullscreenKeepsAnEditableViewportAndShortcutsAboveTheRealLandscapeIme() {
        withProject(DIFERENCIA, "diferencia.h") { scenario, _ ->
            var originalOrientation = ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
            var draft = ""
            var compactEditorHeight = 0
            try {
                scenario.onActivity { activity ->
                    originalOrientation = activity.requestedOrientation
                    val source = editor(activity)
                    draft = source.text.toString() + "\n// compact multi-file draft"
                    source.setText(draft)
                    hideIme(activity)
                    tagged(activity, "cpp_compiler_fullscreen").performClick()
                    activity.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
                }
                awaitUi(scenario, "The project enters landscape fullscreen before requesting the IME") { activity ->
                    activity.resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE &&
                        field(activity, "compilerFullscreenActive") == true && activity.hasWindowFocus() &&
                        !imeVisible(activity) && !editor(activity).isLayoutRequested
                }
                scenario.onActivity { activity ->
                    val source = editor(activity)
                    assertTrue("The established code input keeps keyboard focus integration", source.requestFocus())
                    source.setSelection(source.length())
                    (activity.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager)
                        .showSoftInput(source, InputMethodManager.SHOW_IMPLICIT)
                }
                awaitUi(scenario, "The real IME leaves a visible source line and active shortcuts") { activity ->
                    val source = editor(activity)
                    val toolbar = activity.findViewById<View>(R.id.programmingKeyboardBar)
                    val visibleSource = Rect()
                    imeVisible(activity) && source.hasFocus() && toolbar.isShown && toolbar.height > 0 &&
                        tagged(activity, "cpp_project_controls").visibility == View.GONE &&
                        !source.isLayoutRequested && !toolbar.isLayoutRequested &&
                        source.getGlobalVisibleRect(visibleSource) && visibleSource.height() >= source.lineHeight
                }
                scenario.onActivity { activity ->
                    val source = editor(activity)
                    val toolbar = activity.findViewById<View>(R.id.programmingKeyboardBar)
                    val sourceBounds = Rect().also { assertTrue(source.getGlobalVisibleRect(it)) }
                    val toolbarBounds = Rect().also { assertTrue(toolbar.getGlobalVisibleRect(it)) }
                    val frame = Rect().also(activity.window.decorView::getWindowVisibleDisplayFrame)
                    compactEditorHeight = source.height
                    assertSame("The source retains focus while compacting", source, activity.currentFocus)
                    assertTrue("A full source line remains visible: $sourceBounds", sourceBounds.height() >= source.lineHeight)
                    assertTrue("Source stays above the shortcut bar: source=$sourceBounds toolbar=$toolbarBounds",
                        sourceBounds.bottom <= toolbarBounds.top + 1)
                    assertTrue("Shortcut bar remains inside the real window viewport: toolbar=$toolbarBounds frame=$frame",
                        toolbarBounds.bottom <= frame.bottom + 1)
                    assertEquals(draft, source.text.toString())
                    val tab = descendants(activity.findViewById(R.id.programmingKeyboardBarContent))
                        .filterIsInstance<TextView>().single { it.text.toString() == activity.getString(R.string.lesson_tab_label) }
                    assertTrue("The existing TAB shortcut is available with the IME", tab.isShown && tab.isEnabled)
                    tab.performClick()
                    draft += KeyboardShortcutSettingsRepository.getTabText()
                    assertEquals("The shortcut edits this project's selected source", draft, source.text.toString())
                    assertTrue("Using a shortcut preserves editor focus", source.hasFocus())
                    hideIme(activity)
                }
                awaitUi(scenario, "Hiding the IME restores the project file controls and editor height") { activity ->
                    !imeVisible(activity) && tagged(activity, "cpp_project_controls").visibility == View.VISIBLE &&
                        !editor(activity).isLayoutRequested && editor(activity).height > compactEditorHeight
                }
                scenario.onActivity { activity ->
                    assertEquals(draft, editor(activity).text.toString())
                    tagged(activity, "cpp_compiler_fullscreen").performClick()
                }
                awaitUi(scenario, "Exiting fullscreen restores the authored project's normal controls") { activity ->
                    field(activity, "compilerFullscreenActive") == false &&
                        tagged(activity, "cpp_project_controls").isShown && tagged(activity, "cpp_run").isShown
                }
                scenario.onActivity { assertEquals(draft, editor(it).text.toString()) }
                assertSuccessful(run(scenario), "7")
            } finally {
                scenario.onActivity { activity ->
                    hideIme(activity)
                    activity.requestedOrientation = originalOrientation
                }
            }
        }
    }

    private data class LabLocation(val page: Int, val block: Int)
    private data class RunResult(val status: String, val phase: String, val compilation: String,
                                 val stdout: String, val stderr: String, val finishedStatuses: Map<Int, String>) {
        fun finishedStatus(exitCode: Int) = finishedStatuses.getValue(exitCode)
    }

    private fun withProject(lessonId: String, header: String,
                            test: (ActivityScenario<LessonActivity>, LabLocation) -> Unit) =
        withAsset(lessonId, { block ->
            val files = block.optJSONArray("projectFiles")
            files != null && (0 until files.length()).any { files.getJSONObject(it).optString("path") == header }
        }, test)

    private fun withAsset(lessonId: String, select: (JSONObject) -> Boolean,
                          test: (ActivityScenario<LessonActivity>, LabLocation) -> Unit) {
        val lesson = LessonRepository.loadLessonJson(context, ROUTE, lessonId)
        val locations = buildList {
            val pages = lesson.getJSONArray("pages")
            for (page in 0 until pages.length()) {
                val blocks = pages.getJSONObject(page).getJSONArray("blocks")
                for (block in 0 until blocks.length()) {
                    val candidate = blocks.getJSONObject(block)
                    if (candidate.optString("type") == "cpp_compiler" && select(candidate)) add(LabLocation(page, block))
                }
            }
        }
        assertEquals("One authored curricular lab matches $lessonId", 1, locations.size)
        val location = locations.single()
        ProgressRepository.resetLesson(context, ROUTE, lessonId)
        ProgressRepository.saveCurrentPage(context, ROUTE, lessonId, location.page)
        ActivityScenario.launch<LessonActivity>(lessonIntent(lessonId)).use { scenario ->
            scenario.onActivity { activity ->
                activity.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                assertEquals("The published asset is loaded", lessonId, (field(activity, "lessonJson") as JSONObject).getString("id"))
                assertEquals(location.page, field(activity, "currentPage"))
                tagged(activity, "cpp_code_input")
            }
            instrumentation.waitForIdleSync()
            test(scenario, location)
        }
    }

    private fun withFixture(source: String, preparedInput: String? = null,
                            test: (ActivityScenario<LessonActivity>) -> Unit) {
        assertTrue(context.getSharedPreferences(DRAFTS, Context.MODE_PRIVATE).edit().clear().commit())
        ProgressRepository.resetLesson(context, ROUTE, READING)
        val block = JSONObject().put("type", "cpp_compiler").put("initialCode", source)
        if (preparedInput != null) block.put("initialStandardInput", preparedInput)
        val lesson = JSONObject().put("id", READING).put("title", "Technical curricular runtime check")
            .put("pages", JSONArray().put(JSONObject().put("blocks", JSONArray().put(block))))
        ActivityScenario.launch<LessonActivity>(lessonIntent(READING)).use { scenario ->
            scenario.onActivity { activity ->
                activity.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                setField(activity, "lessonJson", lesson)
                setField(activity, "totalPages", 1)
                setField(activity, "currentPage", 0)
                listOf("pageViewCache", "pageRequiredExercises", "pageCompletedExercises").forEach { name ->
                    (field(activity, name) as MutableMap<*, *>).clear()
                }
                invoke(activity, "renderCurrentPage")
            }
            instrumentation.waitForIdleSync()
            test(scenario)
        }
    }

    private fun runWithInput(scenario: ActivityScenario<LessonActivity>, text: String, expected: String,
                             expectedError: String = "") {
        scenario.onActivity { input(it).setText(text) }
        assertSuccessful(run(scenario), expected, expectedError)
    }

    private fun run(scenario: ActivityScenario<LessonActivity>): RunResult {
        var completedBefore = 0
        var reserveBefore = 0f
        scenario.onActivity { activity ->
            completedBefore = field(activity, "completedExercises") as Int
            reserveBefore = context.getSharedPreferences(ATTEMPTS, Context.MODE_PRIVATE).getFloat("reserve_$ROUTE", 0f)
            val button = tagged(activity, "cpp_run")
            assertTrue("The renderer exposes an enabled Run action", button.isEnabled)
            button.performClick()
            assertFalse("The real native job disables Run while working", button.isEnabled)
        }
        val deadline = SystemClock.uptimeMillis() + 120_000L
        var finished = false
        do {
            instrumentation.waitForIdleSync()
            scenario.onActivity { finished = tagged(it, "cpp_run").isEnabled }
            if (!finished) SystemClock.sleep(50L)
        } while (!finished && SystemClock.uptimeMillis() < deadline)
        var result: RunResult? = null
        scenario.onActivity { activity ->
            result = RunResult(text(activity, "cpp_status"), tagged(activity, "cpp_status").getTag(R.id.cpp_lab_build_phase)?.toString().orEmpty(),
                text(activity, "cpp_compilation_output"), text(activity, "cpp_stdout"), text(activity, "cpp_stderr"),
                listOf(0, 3).associateWith { activity.getString(
                    if (it == 0) R.string.cpp_runtime_finished else R.string.cpp_execution_failed, it) })
            assertEquals("A lab run cannot complete an evaluated exercise", completedBefore, field(activity, "completedExercises"))
            assertTrue("A compiler run cannot charge attempts", context.getSharedPreferences(ATTEMPTS, Context.MODE_PRIVATE)
                .getFloat("reserve_$ROUTE", 0f) >= reserveBefore)
        }
        assertTrue("Native compilation/execution completes within 120 seconds: $result", finished)
        return requireNotNull(result).also { actual ->
            assertFalse("Diagnostics identify virtual source files without internal job paths: ${actual.compilation}",
                actual.compilation.contains("cpp-jobs/"))
            assertFalse("Diagnostics cannot expose the private cache directory: ${actual.compilation}",
                actual.compilation.contains(context.cacheDir.absolutePath))
        }
    }

    private fun assertSuccessful(result: RunResult, expected: String, expectedError: String = "") {
        assertEquals("The program exits successfully: $result", result.finishedStatus(0), result.status)
        assertEquals("Compilation diagnostics are separate from stdout", "", result.compilation)
        assertEquals("Only the program writes stdout", expected, result.stdout.trimEnd())
        assertEquals("Program stderr remains independent", expectedError, result.stderr)
    }

    private fun assertCompilationFailure(result: RunResult, source: String, detail: String) {
        assertEquals(result.compilation, "COMPILATION", result.phase)
        assertTrue(result.compilation, result.compilation.contains(source))
        assertTrue(result.compilation, result.compilation.contains(detail))
        assertEquals("", result.stdout)
        assertEquals("", result.stderr)
    }

    private fun assertLinkFailure(result: RunResult, symbol: String) {
        assertEquals(result.compilation, "LINK", result.phase)
        assertTrue(result.compilation, result.compilation.contains("undefined symbol", ignoreCase = true))
        assertTrue(result.compilation, result.compilation.contains(symbol))
        assertEquals("", result.stdout)
        assertEquals("", result.stderr)
    }

    private fun assertConfigurationFailure(scenario: ActivityScenario<LessonActivity>) {
        scenario.onActivity { activity ->
            val completed = field(activity, "completedExercises")
            val reserve = context.getSharedPreferences(ATTEMPTS, Context.MODE_PRIVATE).getFloat("reserve_$ROUTE", 0f)
            val button = tagged(activity, "cpp_run")
            assertTrue(button.isEnabled)
            button.performClick()
            assertTrue("Invalid project configuration cannot start a native job", button.isEnabled)
            assertEquals("CONFIGURATION", tagged(activity, "cpp_status").getTag(R.id.cpp_lab_build_phase).toString())
            assertEquals(activity.getString(R.string.cpp_project_invalid), text(activity, "cpp_status"))
            assertEquals(activity.getString(R.string.cpp_project_invalid), text(activity, "cpp_compilation_output"))
            assertEquals("A previous successful output cannot survive a rejected build", "", text(activity, "cpp_stdout"))
            assertEquals("A rejected build has no program stderr", "", text(activity, "cpp_stderr"))
            assertEquals(completed, field(activity, "completedExercises"))
            assertTrue(context.getSharedPreferences(ATTEMPTS, Context.MODE_PRIVATE)
                .getFloat("reserve_$ROUTE", 0f) >= reserve)
        }
    }

    private fun selectOptionalCompiler(scenario: ActivityScenario<LessonActivity>) {
        scenario.onActivity { activity ->
            val selector = tagged(activity, "cpp_compiler_variant") as Spinner
            assertTrue("The executable optional split is offered by the curricular UI", selector.count >= 2)
            selector.setSelection(1)
        }
        instrumentation.waitForIdleSync()
    }

    private fun editFile(scenario: ActivityScenario<LessonActivity>, path: String, transform: (String) -> String) {
        scenario.onActivity { activity ->
            tagged(activity, "cpp_file_$path").performClick()
            editor(activity).setText(transform(editor(activity).text.toString()))
        }
        instrumentation.waitForIdleSync()
    }

    private fun readFile(scenario: ActivityScenario<LessonActivity>, path: String): String {
        var result = ""
        scenario.onActivity { activity ->
            tagged(activity, "cpp_file_$path").performClick()
            result = editor(activity).text.toString()
        }
        return result
    }

    private fun createFile(scenario: ActivityScenario<LessonActivity>, path: String, code: String) {
        scenario.onActivity { tagged(it, "cpp_add_file").performClick() }
        onView(withTagValue(equalTo("cpp_new_file_name"))).inRoot(isDialog())
            .perform(replaceText(path), closeSoftKeyboard())
        onView(withId(android.R.id.button1)).inRoot(isDialog()).perform(click())
        scenario.onActivity { activity ->
            assertEquals(path, editor(activity).contentDescription.toString())
            editor(activity).setText(code)
        }
    }

    private fun deleteFile(scenario: ActivityScenario<LessonActivity>, path: String) {
        scenario.onActivity { activity ->
            tagged(activity, "cpp_file_$path").performClick()
            tagged(activity, "cpp_delete_file").performClick()
            assertTrue("The selected virtual file is removed", descendants(content(activity)).none { it.tag == "cpp_file_$path" })
        }
    }

    private fun fileCount(activity: LessonActivity) = descendants(content(activity))
        .count { it.tag?.toString()?.startsWith("cpp_file_") == true }

    private fun currentInput(scenario: ActivityScenario<LessonActivity>): String {
        var result = ""
        scenario.onActivity { result = input(it).text.toString() }
        return result
    }

    private fun enableSource(scenario: ActivityScenario<LessonActivity>, path: String, enabled: Boolean) {
        scenario.onActivity { activity ->
            tagged(activity, "cpp_file_$path").performClick()
            val checkbox = tagged(activity, "cpp_source_enabled") as CheckBox
            if (checkbox.isChecked != enabled) checkbox.performClick()
            assertEquals("Compile only the selected translation units", enabled, checkbox.isChecked)
        }
    }

    private fun showPage(activity: LessonActivity, page: Int) {
        invoke(activity, "saveCurrentPageState")
        setField(activity, "currentPage", page)
        ProgressRepository.saveCurrentPage(context, ROUTE, field(activity, "currentLessonId") as String, page)
        invoke(activity, "renderCurrentPage")
    }

    private fun lessonIntent(lessonId: String): Intent {
        val parts = lessonId.split("__")
        return Intent(context, LessonActivity::class.java).putExtra("routeId", ROUTE)
            .putExtra("courseId", parts[0]).putExtra("unitId", parts[1]).putExtra("lessonId", lessonId)
            .putExtra("lessonNumber", 1).putExtra("lessonTotal", 1)
    }

    private fun preferenceFiles(): Set<String> = File(context.applicationInfo.dataDir, "shared_prefs")
        .listFiles().orEmpty().filter { it.name.endsWith(".xml") }.map { it.name.removeSuffix(".xml") }.toSet()
    private fun content(activity: LessonActivity): ViewGroup = activity.findViewById(R.id.lessonContent)
    private fun editor(activity: LessonActivity) = tagged(activity, "cpp_code_input") as EditText
    private fun input(activity: LessonActivity) = tagged(activity, "cpp_input") as EditText
    private fun text(activity: LessonActivity, tag: String) = (tagged(activity, tag) as TextView).text.toString()
    private fun tagged(activity: LessonActivity, tag: String): View = descendants(content(activity)).single { it.tag == tag }
    private fun descendants(root: View): List<View> = buildList {
        add(root)
        if (root is ViewGroup) for (index in 0 until root.childCount) addAll(descendants(root.getChildAt(index)))
    }
    private fun imeVisible(activity: LessonActivity) = ViewCompat.getRootWindowInsets(activity.window.decorView)
        ?.isVisible(WindowInsetsCompat.Type.ime()) == true
    private fun hideIme(activity: LessonActivity) {
        (activity.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager)
            .hideSoftInputFromWindow(activity.window.decorView.windowToken, 0)
    }
    private fun awaitUi(scenario: ActivityScenario<LessonActivity>, label: String, ready: (LessonActivity) -> Boolean) {
        val deadline = SystemClock.uptimeMillis() + 15_000L
        var stableSamples = 0
        var diagnostic = ""
        do {
            instrumentation.waitForIdleSync()
            scenario.onActivity { activity ->
                val source = editor(activity)
                val toolbar = activity.findViewById<View>(R.id.programmingKeyboardBar)
                val controls = tagged(activity, "cpp_project_controls")
                diagnostic = "orientation=${activity.resources.configuration.orientation} ime=${imeVisible(activity)} " +
                    "editor=${source.width}x${source.height} focus=${source.hasFocus()} " +
                    "toolbar=${toolbar.visibility}/${toolbar.height} controls=${controls.visibility}"
                stableSamples = if (ready(activity)) stableSamples + 1 else 0
            }
            if (stableSamples >= 3) return
            SystemClock.sleep(50L)
        } while (SystemClock.uptimeMillis() < deadline)
        fail("$label; $diagnostic")
    }
    private fun field(activity: LessonActivity, name: String): Any? = LessonActivity::class.java.getDeclaredField(name)
        .apply { isAccessible = true }.get(activity)
    private fun setField(activity: LessonActivity, name: String, value: Any) = LessonActivity::class.java.getDeclaredField(name)
        .apply { isAccessible = true }.set(activity, value)
    private fun invoke(activity: LessonActivity, name: String) = LessonActivity::class.java.getDeclaredMethod(name)
        .apply { isAccessible = true }.invoke(activity)

    companion object {
        private const val ROUTE = "programming"
        private const val DRAFTS = "cpp_lesson_drafts"
        private const val ATTEMPTS = "attempt_reserve_seconds_test"
        private const val READING = "cpp_fundamentals__objetos_y_estado__leer_una_entrada_conocida"
        private const val DIFERENCIA = "cpp_fundamentals__funciones_y_contratos__declaraciones_y_firmas"
        private const val INTERFACES = "cpp_fundamentals__funciones_y_contratos__interfaces_cabeceras_y_programas_con_varios_archivos"
        private val KNOWN_PREFERENCES = setOf(DRAFTS, ATTEMPTS, "learning_progress", "navigation_state",
            "lesson_narration_settings", "app_settings", "ui_language_settings")
        private val STREAM_SOURCE = """
            #include <iostream>
            #include <limits>
            #include <string>
            int main() {
                int number = 0;
                if (!(std::cin >> number)) {
                    std::cout << (std::cin.eof() ? "EMPTY" : "INVALID");
                    return 0;
                }
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                std::string line, extra;
                if (!std::getline(std::cin, line)) return 4;
                bool eof = !std::getline(std::cin, extra);
                std::cout << number << '|' << line << '|' << (eof ? "EOF" : "EXTRA");
                std::cerr << "stream stderr\n";
                return 0;
            }
        """.trimIndent()
        private val SOBRES_BOUNDARIES = """
            #include "sobres.h"
            #include <iostream>
            #include <initializer_list>
            int main() {
                for (int tarjetas : {0, 12, 13, 25, 120}) {
                    std::cout << descripcion_sobres(tarjetas) << ": " << sobres_necesarios(tarjetas) << '\n';
                }
            }
        """.trimIndent()
        private val SERIE_BOUNDARIES = """
            #include "serie.h"
            #include <iostream>
            #include <initializer_list>
            #include <utility>
            int main() {
                for (auto caso : {std::pair{3, 5}, std::pair{0, 0}, std::pair{5, 5}, std::pair{0, 40}, std::pair{40, 40}}) {
                    std::cout << estado_serie(caso.first, caso.second) << ": "
                        << tareas_pendientes(caso.first, caso.second) << '\n';
                }
            }
        """.trimIndent()
        private val SERIE_TRANSFER = """
            #include "serie.h"
            #include <iostream>
            int main() {
                std::cout << estado_serie(0, 8) << ": " << tareas_pendientes(0, 8) << '\n';
                std::cout << estado_serie(8, 8) << ": " << tareas_pendientes(8, 8);
            }
        """.trimIndent()
    }
}
