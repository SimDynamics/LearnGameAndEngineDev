package com.learngraphicsprogramming.app

import android.app.Activity
import android.app.Application
import android.app.KeyguardManager
import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.os.Bundle
import android.os.SystemClock
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.Button
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.TextView
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
import com.learngraphicsprogramming.app.narration.LessonNarrationScript
import com.learngraphicsprogramming.app.narration.IncorrectFeedbackNarration
import com.learngraphicsprogramming.app.narration.LessonNarrationController
import com.learngraphicsprogramming.app.narration.NarrationLanguage
import com.learngraphicsprogramming.app.narration.NarrationSettingsRepository
import com.learngraphicsprogramming.app.runtime.CppCmakeResult
import com.learngraphicsprogramming.app.runtime.CppProjectHistory
import org.hamcrest.Matchers.equalTo
import org.json.JSONArray
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.security.MessageDigest
import java.util.UUID

/** Published blocks, copied only in memory, with exclusive QA storage identities.
 * The source assets and the normal application's preferences are never written. */
@RunWith(AndroidJUnit4::class)
class CppPendingCapabilitiesDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val session = UUID.randomUUID().toString()
    private var originalLocale = ""
    private var originalFiles = emptySet<String>()
    private val preferences = linkedMapOf<String, Map<String, *>>()
    private var preserved = false
    private val disposableProjectDirectories = mutableListOf<File>()
    private var phaseSequence = 0
    private val screenGuard = object : Application.ActivityLifecycleCallbacks {
        override fun onActivityCreated(activity: Activity, state: Bundle?) {
            if (activity is LessonActivity) activity.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        }
        override fun onActivityStarted(activity: Activity) = Unit
        override fun onActivityResumed(activity: Activity) = Unit
        override fun onActivityPaused(activity: Activity) = Unit
        override fun onActivityStopped(activity: Activity) = Unit
        override fun onActivitySaveInstanceState(activity: Activity, state: Bundle) = Unit
        override fun onActivityDestroyed(activity: Activity) = Unit
    }

    @Before fun preserveQaStateAndRequireUnlockedEmulator() {
        assertTrue("Use an isolated QA application", context.packageName in QA_PACKAGES)
        assertEquals(context.packageName, BuildConfig.APPLICATION_ID)
        assertTrue("These checks must run on LDPlayer, never on Work's phone",
            android.os.Build.FINGERPRINT.contains("generic", true) ||
                android.os.Build.MODEL.contains("V2307A", true) || android.os.Build.HARDWARE.contains("ranchu", true) ||
                android.os.Build.HARDWARE.contains("goldfish", true))
        val keyguard = context.getSystemService(Context.KEYGUARD_SERVICE) as KeyguardManager
        assertFalse("Unlock LDPlayer before the native tests", keyguard.isDeviceLocked)
        assertFalse(keyguard.isKeyguardLocked)
        originalLocale = UiLanguageSettings.selectedTag(context)
        originalFiles = preferenceFiles()
        (originalFiles + KNOWN_PREFERENCES).forEach { name ->
            preferences[name] = context.getSharedPreferences(name, Context.MODE_PRIVATE).all.mapValues { (_, value) ->
                if (value is Set<*>) value.toSet() else value
            }
        }
        preserved = true
        (context.applicationContext as Application).registerActivityLifecycleCallbacks(screenGuard)
        instrumentation.runOnMainSync { UiLanguageSettings.select(context, "es") }
        NarrationSettingsRepository.saveEnabled(context, false)
        PremiumRepository.setVerifiedPremium(context)
    }

    @After fun restoreAllOriginalPreferencesAndLocale() {
        if (!preserved) return
        instrumentation.waitForIdleSync()
        instrumentation.runOnMainSync { UiLanguageSettings.select(context, originalLocale) }
        (preferenceFiles() + preferences.keys).forEach { name ->
            if (name !in originalFiles) context.deleteSharedPreferences(name)
            else {
                val edit = context.getSharedPreferences(name, Context.MODE_PRIVATE).edit().clear()
                preferences.getValue(name).forEach { (key, value) -> when (value) {
                    is String -> edit.putString(key, value)
                    is Int -> edit.putInt(key, value)
                    is Long -> edit.putLong(key, value)
                    is Float -> edit.putFloat(key, value)
                    is Boolean -> edit.putBoolean(key, value)
                    is Set<*> -> edit.putStringSet(key, value.filterIsInstance<String>().toSet())
                    else -> error("Unexpected preference $name/$key")
                } }
                assertTrue(edit.commit())
                assertEquals("Restore all previous QA progress/preferences", preferences.getValue(name),
                    context.getSharedPreferences(name, Context.MODE_PRIVATE).all)
            }
        }
        assertEquals(originalFiles, preferenceFiles())
        (context.applicationContext as Application).unregisterActivityLifecycleCallbacks(screenGuard)
        disposableProjectDirectories.forEach { directory ->
            assertTrue("Only the exclusive QA project may be cleaned", directory.name.matches(Regex("[a-f0-9]{64}")))
            assertTrue(directory.canonicalPath.startsWith(context.filesDir.canonicalPath + "/cpp_project_history/") ||
                directory.canonicalPath.startsWith(context.cacheDir.canonicalPath + "/cpp-cmake-jobs/"))
            if (directory.exists()) assertTrue(directory.deleteRecursively())
        }
    }

    @Test fun publishedDragRetainsTwoWrongPlacementsGuidanceAndCorrectRetryAcrossReopening() {
        val fixture = published(DRAG_LESSON, 9, 4)
        assertEquals("regla_recuperar_los_papeles", fixture.block.getString("id"))
        assertEquals(listOf(2, 0), integers(fixture.block.getJSONArray("correctOrder")))
        val choices = strings(fixture.block.getJSONArray("choices"))
        for ((attempt, placement) in listOf(listOf(0, 2), listOf(1, 3)).withIndex()) {
            withFixture(fixture) { scenario ->
                setDrag(scenario, fixture, placement)
                scenario.onActivity { activity ->
                    assertEquals("Editing clears guidance from the previous attempt", "", dragFeedback(activity, fixture).text.toString())
                    check(activity).performClick()
                    assertDrag(activity, fixture, placement, correct = false)
                    val feedback = dragFeedback(activity, fixture).text.toString()
                    val resource = activity.resources.getIdentifier("cpp_review_drag", "string", activity.packageName)
                    assertNotEquals("There is a localized conceptual guide for drag-fill", 0, resource)
                    assertTrue(feedback, feedback.contains(activity.getString(resource)))
                    choices.forEach { assertFalse("The generic guide cannot disclose the correct identifiers", feedback.contains(it)) }
                    assertFalse(feedback.contains(fixture.block.getString("id")))
                    assertTrue("The guide remains ordinary text, not internal IDs", feedback.any(Char::isLetter))
                }
                capture(scenario, "drag-${attempt + 1}-incorrect") { dragFeedback(it, fixture) }
            }
            withFixture(fixture) { scenario -> scenario.onActivity { activity ->
                assertDrag(activity, fixture, placement, correct = false)
                val restored = dragFeedback(activity, fixture).text.toString()
                assertTrue("The reviewed same draft restores its guide without another submission", restored.isNotBlank())
                assertTrue(saved(fixture).has(CppExerciseFeedback.REVIEWED_ANSWER_IDENTITY))
            } }
        }
        withFixture(fixture) { scenario ->
            setDrag(scenario, fixture, listOf(2, 0))
            scenario.onActivity { activity ->
                assertEquals("", dragFeedback(activity, fixture).text.toString())
                check(activity).performClick()
                assertDrag(activity, fixture, listOf(2, 0), correct = true)
                assertTrue(dragFeedback(activity, fixture).text.toString().contains(activity.getString(R.string.lesson_correct_feedback)))
                val script = LessonNarrationScript.page(fixture.lesson, fixture.page, NarrationLanguage.SPANISH)
                assertTrue("The existing narration mechanism still recognizes the exercise's success gate",
                    script.gates.any { it.blockIndex == fixture.index })
                assertFalse("Incorrect feedback is not automatically added to continuous narration",
                    script.texts.any { it.contains(activity.getString(activity.resources.getIdentifier("cpp_review_drag", "string", activity.packageName))) })
            }
            capture(scenario, "drag-correct") { dragFeedback(it, fixture) }
        }
        withFixture(fixture) { scenario -> scenario.onActivity {
            assertDrag(it, fixture, listOf(2, 0), correct = true)
        } }
        verifyOriginalAsset(fixture)
    }

    @Test fun authoredDragGuidanceFromWorkPreservesBothTokensAndOnlyExposesSuccessExplanationAfterCorrectRetry() {
        val original = published(DRAG_LESSON, 9, 4)
        val source = LessonRepository.loadLessonJson(context, ROUTE, DRAG_LESSON)
        val explanation = source.getJSONArray("pages").getJSONObject(9).getJSONArray("blocks").getJSONObject(1).getString("text")
        val guide = "Relaciona cada hueco con la magnitud que necesita: una capacidad en el cálculo de cajas y un plazo en la comparación de días. Distingue datos de entrada y reglas."
        val authored = JSONObject(original.block.toString()).put("reviewFeedback", guide).put("explanation", explanation)
        val lesson = JSONObject(original.lesson.toString())
        lesson.getJSONArray("pages").getJSONObject(original.page).getJSONArray("blocks").put(original.index, JSONObject(authored.toString()))
        val fixture = original.copy(block = authored, lesson = lesson)
        val choices = strings(fixture.block.getJSONArray("choices"))
        for ((attempt, placement) in listOf(listOf(0, 2), listOf(1, 3)).withIndex()) {
            var dimensions = emptyMap<Int, String>()
            withFixture(fixture) { scenario ->
                setDrag(scenario, fixture, placement)
                scenario.onActivity { activity ->
                    assertEquals("An edited independent draft clears the previous review", "", dragFeedback(activity, fixture).text.toString())
                    check(activity).performClick()
                    assertDrag(activity, fixture, placement, correct = false)
                    val feedback = dragFeedback(activity, fixture).text.toString()
                    assertTrue("Only the attempted conceptual guide is shown: $feedback", feedback.contains(guide))
                    assertFalse("The complete later explanation is not a failure hint", feedback.contains(explanation))
                    choices.forEach { assertFalse("No slot identifier or correct sequence is disclosed", feedback.contains(it)) }
                    assertFalse(feedback.contains("[2, 0]"))
                    assertFalse(feedback.contains(fixture.block.getString("id")))
                }
                await("All native tokens must finish their compact layout before recording geometry") { ready -> scenario.onActivity {
                    ready(dragTokens(it, fixture).all { token -> token.width > 0 && token.height > 0 && !token.isLayoutRequested })
                } }
                scenario.onActivity { dimensions = tokenDimensions(it, fixture) }
                capture(scenario, "drag-authored-${attempt + 1}-incorrect") { dragFeedback(it, fixture) }
            }
            withFixture(fixture) { scenario -> scenario.onActivity { activity ->
                assertDrag(activity, fixture, placement, correct = false)
                assertEquals("Reopening preserves the text and existing dimensions of every fragment", dimensions, tokenDimensions(activity, fixture))
                val restored = dragFeedback(activity, fixture).text.toString()
                assertTrue(restored.contains(guide))
                assertFalse(restored.contains(explanation))
                assertTrue(saved(fixture).has(CppExerciseFeedback.REVIEWED_ANSWER_IDENTITY))
            } }
        }
        withFixture(fixture) { scenario ->
            setDrag(scenario, fixture, listOf(2, 0))
            scenario.onActivity { activity ->
                assertEquals("", dragFeedback(activity, fixture).text.toString())
                check(activity).performClick()
                assertDrag(activity, fixture, listOf(2, 0), correct = true)
                assertTrue(dragFeedback(activity, fixture).text.toString().contains(explanation))
                assertFalse(dragFeedback(activity, fixture).text.toString().contains(guide))
                val script = LessonNarrationScript.page(fixture.lesson, fixture.page, NarrationLanguage.SPANISH)
                val gate = script.gates.single { it.blockIndex == fixture.index }
                val success = script.texts.subList(gate.start, gate.endExclusive).joinToString(" ")
                assertTrue("The existing successful feedback gate includes the complete authored explanation: $success",
                    success.contains("El desplazamiento deriva de la capacidad") && success.contains("la regla que lo interpreta"))
                assertFalse("Incorrect red guidance remains excluded from automatic continuous narration", script.texts.any { it.contains(guide) })
            }
            capture(scenario, "drag-authored-success-explanation") { dragFeedback(it, fixture) }
        }
        withFixture(fixture) { scenario -> scenario.onActivity { activity ->
            assertDrag(activity, fixture, listOf(2, 0), correct = true)
            assertTrue("The same successful explanation survives a new Activity", dragFeedback(activity, fixture).text.toString().contains(explanation))
        } }
        assertEquals("Original choices and answer remain exactly those published by Work", original.block.getJSONArray("choices").toString(), fixture.block.getJSONArray("choices").toString())
        assertEquals(original.block.getJSONArray("correctOrder").toString(), fixture.block.getJSONArray("correctOrder").toString())
        verifyOriginalAsset(fixture)
    }

    @Test fun publishedCmakeOrderingHasLocalizedHeadingAndPreservesIncorrectRetryAndRestoration() {
        for (language in listOf("es", "en", "fr")) {
            instrumentation.runOnMainSync { UiLanguageSettings.select(context, language) }
            NarrationSettingsRepository.saveEnabled(context, false)
            val fixture = published(ORDER_LESSON, 6, 6)
            assertEquals("dato_lista_antes_de_consumir", fixture.block.getString("id"))
            val correct = integers(fixture.block.getJSONArray("correctOrder"))
            val wrong = correct.reversed()
            withFixture(fixture) { scenario ->
                scenario.onActivity { activity ->
                    val heading = tagged(activity, "order_code_language_${fixture.index}") as TextView
                    assertEquals(activity.getString(R.string.lesson_code_heading, "CMake"), heading.text.toString())
                    assertEquals(language, activity.resources.configuration.locales[0].language)
                    orderRows(activity, fixture).forEach { assertTrue(it.text.isNotBlank()) }
                    setOrder(activity, fixture, wrong)
                    check(activity).performClick()
                    assertEquals(wrong, integers(saved(fixture).getJSONArray("order")))
                    assertFalse(saved(fixture).getBoolean("correct"))
                }
                capture(scenario, "order-$language-incorrect-heading") { tagged(it, "order_code_language_${fixture.index}") }
            }
            withFixture(fixture) { scenario -> scenario.onActivity { activity ->
                assertEquals(wrong, orderRows(activity, fixture).map { it.tag as Int })
                setOrder(activity, fixture, correct)
                check(activity).performClick()
                assertTrue(saved(fixture).getBoolean("correct"))
                assertEquals(correct, integers(saved(fixture).getJSONArray("order")))
            } }
            withFixture(fixture) { scenario -> scenario.onActivity { activity ->
                assertEquals(correct, orderRows(activity, fixture).map { it.tag as Int })
                assertFalse(check(activity).isEnabled)
                assertEquals(activity.getString(R.string.lesson_code_heading, "CMake"),
                    (tagged(activity, "order_code_language_${fixture.index}") as TextView).text.toString())
            } }
            verifyOriginalAsset(fixture)
        }
    }

    @Test fun existingCppOrderingRetainsItsCppHeadingAndOriginalFragments() {
        val fixture = findPublishedOrderCpp()
        withFixture(fixture) { scenario -> scenario.onActivity { activity ->
            assertEquals(activity.getString(R.string.lesson_code_heading, activity.getString(R.string.lesson_cpp_label)),
                (tagged(activity, "order_code_language_${fixture.index}") as TextView).text.toString())
            assertEquals(strings(fixture.block.getJSONArray("fragments")).toSet(),
                orderRows(activity, fixture).map { it.text.toString() }.toSet())
        } }
        verifyOriginalAsset(fixture)
    }

    @Test fun publishedInlineFillShowsTheSameSafeGuidanceForSpeechAndRestoresOnlyReviewedDrafts() {
        val fixture = published(INLINE_LESSON, 2, 5)
        assertEquals("inline_fill_code", fixture.block.getString("type"))
        val answers = strings(fixture.block.getJSONArray("answers"))
        assertEquals(listOf("0", "Mesa9"), answers)
        val wrong = listOf("1", "Mesa9")
        withFixture(fixture) { scenario ->
            scenario.onActivity { activity ->
                val edits = descendants(content(activity)).filterIsInstance<EditText>()
                assertEquals(answers.size, edits.size)
                wrong.forEachIndexed { index, answer -> edits[index].setText(answer) }
                check(activity).performClick()
                val result = inlineFeedback(activity)
                val guide = activity.getString(R.string.cpp_review_fill)
                assertTrue(result.text.toString().contains(guide))
                assertTrue(result.text.toString().contains(activity.getString(R.string.exercise_wrong_spacing)))
                assertEquals(wrong, strings(saved(fixture).getJSONArray("answers")))
                assertFalse(saved(fixture).getBoolean("correct"))
                assertTrue(saved(fixture).has(CppExerciseFeedback.REVIEWED_ANSWER_IDENTITY))
                assertFalse("A generic process guide never discloses the expected literal", guide.contains("Mesa9"))
                assertFalse(guide.contains(fixture.block.getString("id")))
                assertFalse("The displayed guide is not silently lost by the shared speech front end",
                    IncorrectFeedbackNarration.prepare(listOf(guide), NarrationLanguage.SPANISH, true).isEmpty())
                assertFalse("Disabled narration remains silent on a deliberate wrong attempt",
                    (field(activity, "narrationController") as? LessonNarrationController)?.isPlaying == true)
            }
            capture(scenario, "inline-incorrect-process-guidance") { inlineFeedback(it) }
        }
        withFixture(fixture) { scenario -> scenario.onActivity { activity ->
            assertEquals(wrong, descendants(content(activity)).filterIsInstance<EditText>().map { it.text.toString() })
            assertTrue(inlineFeedback(activity).text.toString().contains(activity.getString(R.string.cpp_review_fill)))
            assertFalse("Restoring a reviewed draft is never a fresh spoken submission",
                (field(activity, "narrationController") as? LessonNarrationController)?.isPlaying == true)
            descendants(content(activity)).filterIsInstance<EditText>()[0].setText("2")
            assertEquals("Editing clears the previous guide", "", inlineFeedback(activity).text.toString())
            assertFalse(saved(fixture).has(CppExerciseFeedback.REVIEWED_ANSWER_IDENTITY))
        } }
        // Historical states contain only answers/correct. Their draft is retained without
        // inventing evidence that the learner submitted it to the new guide mechanism.
        ProgressRepository.saveExerciseState(context, ROUTE, fixture.id, fixture.page, fixture.index,
            JSONObject().put("answers", JSONArray(wrong)).put("correct", false))
        withFixture(fixture) { scenario -> scenario.onActivity { activity ->
            assertEquals(wrong, descendants(content(activity)).filterIsInstance<EditText>().map { it.text.toString() })
            assertEquals("A legacy unreviewed draft stays unreviewed", "", inlineFeedback(activity).text.toString())
            val edits = descendants(content(activity)).filterIsInstance<EditText>()
            answers.forEachIndexed { index, answer -> edits[index].setText(answer) }
            check(activity).performClick()
            assertTrue(saved(fixture).getBoolean("correct"))
            assertEquals(answers, strings(saved(fixture).getJSONArray("answers")))
            assertFalse(check(activity).isEnabled)
            assertTrue(inlineFeedback(activity).text.toString().contains(activity.getString(R.string.lesson_correct_feedback)))
        } }
        verifyOriginalAsset(fixture)
    }

    @Test fun publishedCmakeProjectRecordsReopensComparesAndRecoversOnlyItsSourceBeforeFreshNativeEvidence() {
        val fixture = findPublishedHistoryProject()
        val base = files(fixture.block)
        val sourcePath = "componente/src/cajas.cpp"
        val originalSource = base.getValue(sourcePath)
        assertTrue(originalSource.contains("(piezas + 3) / 4"))
        val defectiveSource = originalSource.replace("(piezas + 3) / 4", "piezas / 4")
        val independentComment = "\n# Comentario independiente de la revision QA; no cambia objetivos.\n"
        val commentedDescription = base.getValue("CMakeLists.txt") + independentComment
        val historyRoot = File(context.filesDir, "cpp_project_history")
        val history = CppProjectHistory(historyRoot, fixture.draftKey)
        val historyDirectory = File(historyRoot, sha(fixture.draftKey))
        val workspace = File(context.cacheDir, "cpp-cmake-jobs/${sha(fixture.draftKey)}")
        assertFalse("The new exclusive project cannot reuse any old revision", historyDirectory.exists())
        assertFalse("The new exclusive project cannot reuse an old build product", workspace.exists())
        disposableProjectDirectories += historyDirectory
        disposableProjectDirectories += workspace
        assertTrue(history.list().isEmpty())

        withFixture(fixture) { scenario ->
            scenario.onActivity { tagged(it, "cpp_project_history").performClick() }
            awaitHistoryReady(history, expected = 0)
            onView(withTagValue(equalTo("cpp_history_register"))).inRoot(isDialog()).perform(click())
            onView(withTagValue(equalTo("cpp_history_label"))).inRoot(isDialog())
                .perform(replaceText("Base cajas: siete casos"), closeSoftKeyboard())
            clickDialogPositiveForField("cpp_history_label")
            awaitHistoryReady(history, expected = 1)
            val recorded = history.list().single()
            assertEquals("Base cajas: siete casos", recorded.label)
            assertEquals("A revision records the actual project, not a build product", base, recorded.files)
            closeHistoryRoot()
        }

        val revision = history.list().single()
        val recordedId = revision.id
        val recordFile = File(historyDirectory, "$recordedId.revision")
        val recordBytesHash = shaBytes(recordFile.readBytes())
        withFixture(fixture) { scenario ->
            scenario.onActivity { activity ->
                assertEquals(originalSource, readFile(activity, sourcePath))
                tagged(activity, "cpp_project_history").performClick()
            }
            awaitHistoryReady(history, expected = 1)
            onView(withTagValue(equalTo("cpp_history_revision_$recordedId"))).inRoot(isDialog()).check { view, _ ->
                assertTrue("The same revision identity and label survive reopening", (view as TextView).text.contains(revision.label))
            }
            closeHistoryRoot()
            editFile(scenario, sourcePath, defectiveSource)
            editFile(scenario, "CMakeLists.txt", commentedDescription)
            assertEquals(setOf(sourcePath, "CMakeLists.txt"), history.statuses(draftFiles(fixture), revision)
                .filter { it.change == CppProjectHistory.Change.MODIFIED }.map { it.path }.toSet())

            // A real failing native run demonstrates that the edited body, not an old green
            // artifact, is what CMake has actually built before recovery.
            setTarget(scenario, "comprobar_cajas")
            val configuredBad = phase(scenario, "cpp_cmake_configure")
            assertEquals(0, configuredBad.exitCode)
            assertEquals("GENERATION", configuredBad.phase.name)
            assertEquals(0, phase(scenario, "cpp_cmake_build").exitCode)
            val badRun = phase(scenario, "cpp_run")
            assertEquals("The changed body must reach a real checker execution: ${phaseEvidence(badRun)}", "EXECUTION", badRun.phase.name)
            assertEquals("The checker must reject the changed body: ${phaseEvidence(badRun)}", 1, badRun.exitCode)
            val badOutput = requireNotNull(badRun.execution).stdout
            assertTrue("The checker must actually report its failed comparisons: ${phaseEvidence(badRun)}", badOutput.contains("Bateria: FALLO"))
            assertEquals("The seven original cases are still executed", 7, badOutput.lineSequence().count { it.startsWith("piezas=") })

            scenario.onActivity { tagged(it, "cpp_project_history").performClick() }
            awaitHistoryReady(history, expected = 1)
            openComparison(recordedId, sourcePath)
            onView(withTagValue(equalTo("cpp_history_diff"))).inRoot(isDialog()).check { view, _ ->
                val diff = (view as TextView).text.toString()
                assertTrue(diff, diff.contains("−     return (piezas + 3) / 4;"))
                assertTrue(diff, diff.contains("+     return piezas / 4;"))
                assertFalse("Only the chosen source is compared", diff.contains(independentComment.trim()))
            }
            captureDialog("history-real-source-diff", "cpp_history_diff")
            onView(withTagValue(equalTo("cpp_history_restore"))).inRoot(isDialog()).perform(click())
            await("Restoration replaces exactly the chosen source") { ready -> ready(draftFiles(fixture)[sourcePath] == originalSource) }
            closeHistoryRoot()
            scenario.onActivity { activity ->
                assertEquals(originalSource, readFile(activity, sourcePath))
                assertEquals("Independent CMake work must survive source recovery", commentedDescription, readFile(activity, "CMakeLists.txt"))
                assertFalse("Recovery cannot credit a previous execution as the new one", tagged(activity, "cpp_run").isEnabled)
            }
            assertEquals("Restoring a source does not replace the registered revision", recordedId, history.list().single().id)
            assertEquals(recordBytesHash, shaBytes(recordFile.readBytes()))
            assertEquals(setOf("CMakeLists.txt"), history.statuses(draftFiles(fixture), revision)
                .filter { it.change == CppProjectHistory.Change.MODIFIED }.map { it.path }.toSet())

            // Process the restored source/independent description and build all three real
            // targets. A static library is built but is never presented as executable.
            val configured = phase(scenario, "cpp_cmake_configure")
            assertEquals(0, configured.exitCode)
            assertEquals(commentedDescription, File(configured.workspace, "project/CMakeLists.txt").readText())
            assertEquals(originalSource, File(configured.workspace, "project/$sourcePath").readText())
            setTarget(scenario, "envios")
            val library = phase(scenario, "cpp_cmake_build")
            assertEquals(0, library.exitCode)
            assertTrue(library.records.any { it.command.windowed(2).any { args -> args == listOf("--target", "envios") } })
            assertNull("A static library is not executable", library.artifact)
            scenario.onActivity { assertFalse(tagged(it, "cpp_run").isEnabled) }
            setTarget(scenario, "consultar_cajas")
            assertEquals(0, phase(scenario, "cpp_cmake_build").exitCode)
            val client = phase(scenario, "cpp_run")
            assertEquals(0, client.exitCode)
            val clientOutput = requireNotNull(client.execution).stdout
            assertEquals("1:2", clientOutput.trim())
            setTarget(scenario, "comprobar_cajas")
            val rebuilt = phase(scenario, "cpp_cmake_build")
            assertEquals(0, rebuilt.exitCode)
            val recovered = phase(scenario, "cpp_run")
            assertEquals(0, recovered.exitCode)
            assertNotEquals("The recovered body must have its own project evidence", badRun.projectFingerprint, recovered.projectFingerprint)
            val recoveredOutput = requireNotNull(recovered.execution).stdout
            assertTrue(recoveredOutput, recoveredOutput.contains("Bateria: OK"))
            assertEquals(7, recoveredOutput.lineSequence().count { it.startsWith("piezas=") && it.endsWith("OK") })
            assertFalse(recoveredOutput.contains("FALLO"))
            assertEquals(listOf(0, 4, 8, 20, 1, 5, 17), recoveredOutput.lineSequence().filter { it.startsWith("piezas=") }
                .map { Regex("piezas=(\\d+)").find(it)!!.groupValues[1].toInt() }.toList())
            scenario.onActivity { activity ->
                val visible = tagged(activity, "cpp_console_display") as TextView
                assertTrue("The unified visible console shows the recovered checker output", visible.text.toString().contains("Bateria: OK"))
                assertEquals(7, visible.text.toString().lineSequence().count { it.startsWith("piezas=") && it.endsWith("OK") })
            }
            capture(scenario, "history-recovered-seven-case-run") { tagged(it, "cpp_console_display") }
            File(evidenceDirectory(), "history-native-evidence.json").writeText(JSONObject()
                .put("sourceLesson", fixture.sourceLesson).put("sourcePage", fixture.page + 1).put("sourceBlock", fixture.index + 1)
                .put("qaLessonId", fixture.id).put("revision", recordedId).put("revisionSha256", recordBytesHash)
                .put("badFingerprint", badRun.projectFingerprint).put("recoveredFingerprint", recovered.projectFingerprint)
                .put("configuredCommand", JSONArray(configured.records.flatMap { it.command }))
                .put("libraryBuildCommand", JSONArray(library.records.flatMap { it.command }))
                .put("badOutput", badOutput).put("clientOutput", clientOutput).put("recoveredOutput", recoveredOutput)
                .put("independentCommentPreserved", true).put("workspaceWasFresh", true).toString(2))
        }
        withFixture(fixture) { scenario -> scenario.onActivity { activity ->
            assertEquals(originalSource, readFile(activity, sourcePath))
            assertEquals(commentedDescription, readFile(activity, "CMakeLists.txt"))
            assertEquals(recordedId, history.list().single().id)
            assertEquals(recordBytesHash, shaBytes(recordFile.readBytes()))
        } }
        verifyOriginalAsset(fixture)
    }

    private data class Fixture(val id: String, val sourceLesson: String, val page: Int, val index: Int,
                               val block: JSONObject, val lesson: JSONObject, val sourceSha: String) {
        val draftKey: String get() = "$ROUTE/$id/$page/$index"
    }

    private fun published(lessonId: String, pageNumber: Int, blockNumber: Int): Fixture {
        val original = LessonRepository.loadLessonJson(context, ROUTE, lessonId)
        val block = JSONObject(original.getJSONArray("pages").getJSONObject(pageNumber - 1)
            .getJSONArray("blocks").getJSONObject(blockNumber - 1).toString())
        val id = "programming_practice_tools__comprobaciones_repetibles__pending_native_${UUID.randomUUID().toString().replace("-", "") }"
        val pages = JSONArray().apply { repeat(original.getJSONArray("pages").length()) { page ->
            put(JSONObject().put("blocks", JSONArray().apply {
                if (page == pageNumber - 1) {
                    repeat(blockNumber - 1) { put(JSONObject().put("type", "paragraph").put("text", "Verificación aislada")) }
                    put(JSONObject(block.toString()))
                } else put(JSONObject().put("type", "paragraph").put("text", "Verificación aislada"))
            }))
        } }
        return Fixture(id, lessonId, pageNumber - 1, blockNumber - 1, block,
            JSONObject().put("id", id).put("title", original.getString("title")).put("pages", pages), sha(original.toString()))
    }

    private fun findPublishedOrderCpp(): Fixture {
        val source = LessonRepository.loadLessonJson(context, ROUTE, CPP_ORDER_LESSON)
        val pages = source.getJSONArray("pages")
        for (p in 0 until pages.length()) {
            val blocks = pages.getJSONObject(p).getJSONArray("blocks")
            for (b in 0 until blocks.length()) {
                val block = blocks.getJSONObject(b)
                if (block.optString("type") == "order_code" &&
                    block.getJSONArray("fragments").toString().contains("std::")) return published(CPP_ORDER_LESSON, p + 1, b + 1)
            }
        }
        error("The existing C++ fixture must contain an actual published ordering exercise")
    }

    private fun findPublishedHistoryProject(): Fixture {
        val source = LessonRepository.loadLessonJson(context, ROUTE, HISTORY_LESSON)
        val pages = source.getJSONArray("pages")
        for (p in 0 until pages.length()) {
            val blocks = pages.getJSONObject(p).getJSONArray("blocks")
            for (b in 0 until blocks.length()) {
                val block = blocks.getJSONObject(b)
                if (block.optString("type") == "cpp_compiler" && block.has("cmake")) {
                    val files = files(block)
                    if (files["componente/src/cajas.cpp"]?.contains("(piezas + 3) / 4") == true &&
                        files["CMakeLists.txt"]?.contains("add_library(envios STATIC componente/src/cajas.cpp)") == true)
                        return published(HISTORY_LESSON, p + 1, b + 1)
                }
            }
        }
        error("The unchanged lesson must retain the complete known three-target CMake project")
    }

    private fun files(block: JSONObject): Map<String, String> = linkedMapOf<String, String>().apply {
        val array = block.getJSONArray("projectFiles")
        repeat(array.length()) { val file = array.getJSONObject(it); put(file.getString("path"), file.getString("initialCode")) }
    }
    private fun draftFiles(fixture: Fixture): Map<String, String> = linkedMapOf<String, String>().apply {
        val text = context.getSharedPreferences("cpp_lesson_drafts", Context.MODE_PRIVATE).getString("${fixture.draftKey}/project", null)
        val array = JSONObject(requireNotNull(text)).getJSONArray("files")
        repeat(array.length()) { val file = array.getJSONObject(it); put(file.getString("path"), file.getString("code")) }
    }
    private fun readFile(activity: LessonActivity, path: String): String {
        assertTrue(tagged(activity, "cpp_file_$path").performClick())
        val editor = descendants(content(activity)).filterIsInstance<FreeScrollEditText>().single()
        assertEquals(path, editor.contentDescription.toString())
        return editor.text.toString()
    }
    private fun editFile(scenario: ActivityScenario<LessonActivity>, path: String, text: String) {
        scenario.onActivity { activity ->
            readFile(activity, path)
            descendants(content(activity)).filterIsInstance<FreeScrollEditText>().single().setText(text)
        }
        instrumentation.waitForIdleSync()
    }
    private fun setTarget(scenario: ActivityScenario<LessonActivity>, target: String) {
        scenario.onActivity { (tagged(it, "cpp_cmake_target") as EditText).setText(target) }
        instrumentation.waitForIdleSync()
    }
    private fun phase(scenario: ActivityScenario<LessonActivity>, tag: String): CppCmakeResult {
        val progressBefore = context.getSharedPreferences("learning_progress", Context.MODE_PRIVATE).all
            .filterKeys { it.startsWith("exercise_") || it.endsWith("_completed") }
        scenario.onActivity { activity ->
            assertTrue("A genuine CMake phase must be available: $tag", tagged(activity, tag).isEnabled)
            tagged(activity, tag).performClick()
            assertFalse("The actual native operation disables its action while busy", tagged(activity, tag).isEnabled)
        }
        var result: CppCmakeResult? = null
        await("The real CMake operation must finish: $tag", 180_000L) { ready -> scenario.onActivity { activity ->
            val status = tagged(activity, "cpp_status")
            val value = status.getTag(R.id.cpp_lab_cmake_result) as? CppCmakeResult
            val finished = tagged(activity, "cpp_cmake_configure").isEnabled && value != null &&
                status.getTag(R.id.cpp_lab_build_phase)?.toString() != "BUILDING"
            if (finished) result = value
            ready(finished)
        } }
        return requireNotNull(result).also {
            File(evidenceDirectory(), "native-phase-${++phaseSequence}-$tag.json").writeText(phaseEvidence(it).toString(2))
            android.util.Log.i("CppPendingQA", phaseEvidence(it).toString())
            assertFalse("Native CMake cannot be simulated by direct compilation", it.timedOut)
            assertTrue("Every phase has real process records", it.records.isNotEmpty())
            assertEquals("A technical build/run neither evaluates an exercise nor changes curricular completion", progressBefore,
                context.getSharedPreferences("learning_progress", Context.MODE_PRIVATE).all
                    .filterKeys { key -> key.startsWith("exercise_") || key.endsWith("_completed") })
        }
    }
    private fun phaseEvidence(result: CppCmakeResult): JSONObject = JSONObject()
        .put("phase", result.phase.name).put("exitCode", result.exitCode).put("stdout", result.stdout)
        .put("stderr", result.stderr).put("timedOut", result.timedOut).put("projectFingerprint", result.projectFingerprint)
        .put("runtime", if (BuildConfig.LGP_WASMTIME_ENABLED) "Wasmtime" else "WAMR")
        .put("workspace", result.workspace.absolutePath)
        .put("artifact", result.artifact?.absolutePath).put("artifactBytes", result.artifact?.length())
        .put("artifactSha256", result.artifact?.takeIf(File::isFile)?.let { shaBytes(it.readBytes()) })
        .put("execution", result.execution?.let { JSONObject().put("exitCode", it.exitCode).put("stdout", it.stdout).put("stderr", it.stderr) })
        .put("toolsInfo", JSONObject(result.toolsInfo)).put("records", JSONArray().apply { result.records.forEach {
            put(JSONObject().put("phase", it.phase.name).put("exitCode", it.exitCode).put("stdout", it.stdout)
                .put("stderr", it.stderr).put("timedOut", it.timedOut).put("command", JSONArray(it.command)))
        } })
        .put("projectFiles", JSONObject().apply {
            listOf("CMakeLists.txt", "componente/src/cajas.cpp", "pruebas/cajas_casos.cpp").forEach { path ->
                File(result.workspace, "project/$path").takeIf(File::isFile)?.let { put(path, it.readText()) }
            }
        })
    private fun awaitHistoryReady(history: CppProjectHistory, expected: Int) {
        await("The immutable revision must be visible after real asynchronous persistence") { ready ->
            ready(history.list().size == expected)
        }
        // Query the actual dialog rather than assuming the I/O callback and refresh completed.
        await("The history dialog must finish its asynchronous refresh") { ready ->
            var enabled = false
            runCatching { onView(withTagValue(equalTo("cpp_history_register"))).inRoot(isDialog()).check { view, _ ->
                enabled = view?.isEnabled == true
            } }
            ready(enabled)
        }
    }
    private fun clickDialogPositiveForField(tag: String) {
        onView(withTagValue(equalTo(tag))).inRoot(isDialog()).check { view, exception ->
            if (exception != null) throw exception
            assertTrue(requireNotNull(view).rootView.findViewById<Button>(android.R.id.button1).performClick())
        }
    }
    private fun closeHistoryRoot() {
        onView(withTagValue(equalTo("cpp_history_dialog"))).inRoot(isDialog()).check { view, exception ->
            if (exception != null) throw exception
            assertTrue(requireNotNull(view).findViewById<Button>(android.R.id.button2).performClick())
        }
        instrumentation.waitForIdleSync()
    }
    private fun openComparison(revision: String, path: String) {
        onView(withTagValue(equalTo("cpp_history_revision_$revision"))).inRoot(isDialog()).perform(click())
        await("The asynchronous actual file status list must be visible") { ready ->
            var exists = false
            runCatching { onView(withTagValue(equalTo("cpp_history_file_$path"))).inRoot(isDialog()).check { view, _ -> exists = view != null } }
            ready(exists)
        }
        onView(withTagValue(equalTo("cpp_history_file_$path"))).inRoot(isDialog()).perform(click())
        await("The real comparison text must finish loading") { ready ->
            var exists = false
            runCatching { onView(withTagValue(equalTo("cpp_history_diff"))).inRoot(isDialog()).check { view, _ -> exists = view != null } }
            ready(exists)
        }
    }
    private fun captureDialog(name: String, tag: String) {
        onView(withTagValue(equalTo(tag))).inRoot(isDialog()).check { view, exception ->
            if (exception != null) throw exception
            val text = requireNotNull(view) as TextView
            assertTrue(text.width > 0 && text.height > 0)
            val bitmap = Bitmap.createBitmap(text.width, text.height, Bitmap.Config.ARGB_8888)
            try {
                text.draw(Canvas(bitmap))
                File(evidenceDirectory(), "$name.png").outputStream().use { assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) }
                File(evidenceDirectory(), "$name.json").writeText(JSONObject().put("text", text.text.toString())
                    .put("width", text.width).put("height", text.height).put("kind", "native comparison TextView at its real Android bounds").toString(2))
            } finally { bitmap.recycle() }
        }
    }

    private fun withFixture(fixture: Fixture, body: (ActivityScenario<LessonActivity>) -> Unit) {
        ProgressRepository.saveCurrentPage(context, ROUTE, fixture.sourceLesson, 0)
        val intent = Intent(context, LessonActivity::class.java).putExtra("routeId", ROUTE)
            .putExtra("courseId", "programming_practice_tools").putExtra("unitId", "comprobaciones_repetibles")
            .putExtra("lessonId", fixture.sourceLesson).putExtra("lessonNumber", 1).putExtra("lessonTotal", 1)
        ActivityScenario.launch<LessonActivity>(intent).use { scenario ->
            var sourceInstance = 0
            var stableSince = 0L
            await("The published source lesson must load before injection") { ready ->
                scenario.onActivity { activity ->
                    val loaded = field(activity, "lessonJson") as? JSONObject
                    val sourceReady = field(activity, "currentLessonId") == fixture.sourceLesson && loaded?.optString("id") == fixture.sourceLesson &&
                        content(activity).childCount > 0 && activity.window.decorView.hasWindowFocus() &&
                        !content(activity).isLayoutRequested && content(activity).width > 0
                    val instance = System.identityHashCode(activity) xor System.identityHashCode(loaded)
                    if (!sourceReady || instance != sourceInstance) {
                        sourceInstance = instance
                        stableSince = if (sourceReady) SystemClock.uptimeMillis() else 0L
                    }
                    ready(sourceReady && SystemClock.uptimeMillis() - stableSince >= 500L)
                }
            }
            scenario.onActivity { activity ->
                assertEquals(fixture.sourceLesson, (field(activity, "lessonJson") as JSONObject).getString("id"))
                setField(activity, "currentLessonId", fixture.id)
                setField(activity, "lessonJson", JSONObject(fixture.lesson.toString()))
                setField(activity, "totalPages", fixture.lesson.getJSONArray("pages").length())
                setField(activity, "currentPage", fixture.page)
                listOf("pageViewCache", "pageRequiredExercises", "pageCompletedExercises").forEach {
                    (field(activity, it) as MutableMap<*, *>).clear()
                }
                invoke(activity, "renderCurrentPage")
                assertEquals(fixture.id, field(activity, "currentLessonId"))
            }
            await("The native fixture layout must finish") { ready -> scenario.onActivity {
                assertEquals("A later event cannot silently replace the QA fixture", fixture.id, field(it, "currentLessonId"))
                ready(content(it).width > 0 && !content(it).isLayoutRequested)
            } }
            body(scenario)
        }
        verifyOriginalAsset(fixture)
    }

    private fun setDrag(scenario: ActivityScenario<LessonActivity>, fixture: Fixture, order: List<Int>) {
        val choices = strings(fixture.block.getJSONArray("choices"))
        for (index in choices.indices) {
            var occupied = false
            scenario.onActivity { activity ->
                val token = dragTokens(activity, fixture).single { it.tag == index }
                occupied = token.parent is FrameLayout
                if (occupied) token.performClick()
            }
            if (occupied) await("The native token animation returns $index to the palette") { ready -> scenario.onActivity {
                ready(dragTokens(it, fixture).single { token -> token.tag == index }.parent !is FrameLayout &&
                    integers(saved(fixture).getJSONArray("slots")).none { value -> value == index })
            } }
        }
        order.forEachIndexed { slot, index ->
            scenario.onActivity { dragTokens(it, fixture).single { token -> token.tag == index }.performClick() }
            await("The native token animation saves slot $slot") { ready -> scenario.onActivity {
                ready(ProgressRepository.getExerciseState(context, fixture.id, fixture.page, fixture.index)
                    ?.optJSONArray("slots")?.optInt(slot, -1) == index)
            } }
        }
    }

    private fun assertDrag(activity: LessonActivity, fixture: Fixture, order: List<Int>, correct: Boolean) {
        assertEquals(order, integers(saved(fixture).getJSONArray("slots")))
        assertEquals(correct, saved(fixture).getBoolean("correct"))
        val placed = dragTokens(activity, fixture).filter { it.parent is FrameLayout }
        assertEquals(order.toSet(), placed.map { it.tag as Int }.toSet())
        assertEquals(!correct, check(activity).isEnabled)
        assertEquals(if (correct) 1 else 0, field(activity, "completedExercises"))
    }

    private fun setOrder(activity: LessonActivity, fixture: Fixture, order: List<Int>) {
        order.forEach { index -> orderRows(activity, fixture).single { it.tag == index }.performClick() }
    }
    private fun orderRows(activity: LessonActivity, fixture: Fixture) = descendants(content(activity))
        .filterIsInstance<TextView>().filter { it.tag is Int && it.tag as Int in 0 until fixture.block.getJSONArray("fragments").length() }
    private fun dragTokens(activity: LessonActivity, fixture: Fixture): List<TextView> = descendants(content(activity))
        .filterIsInstance<TextView>().filter { it.tag is Int && it.text.toString() in strings(fixture.block.getJSONArray("choices")) }
    private fun tokenDimensions(activity: LessonActivity, fixture: Fixture) = dragTokens(activity, fixture).associate {
        (it.tag as Int) to "${it.text}|${it.width}x${it.height}"
    }
    private fun dragFeedback(activity: LessonActivity, fixture: Fixture) = tagged(activity, "drag_fill_feedback_${fixture.index}") as TextView
    private fun inlineFeedback(activity: LessonActivity) = content(activity).getChildAt(content(activity).childCount - 1) as TextView
    private fun check(activity: LessonActivity) = descendants(content(activity)).filterIsInstance<Button>().single {
        it.text.toString() in listOf(activity.getString(R.string.exercise_check), activity.getString(R.string.exercise_correct)) }
    private fun saved(fixture: Fixture): JSONObject = requireNotNull(ProgressRepository.getExerciseState(context, fixture.id, fixture.page, fixture.index))
    private fun verifyOriginalAsset(fixture: Fixture) = assertEquals("Published lesson JSON, IDs, answers and coordinates stay unchanged",
        fixture.sourceSha, sha(LessonRepository.loadLessonJson(context, ROUTE, fixture.sourceLesson).toString()))
    private fun tagged(activity: LessonActivity, tag: String): View = requireNotNull(descendants(content(activity)).singleOrNull { it.tag == tag }) { "Missing native view $tag" }
    private fun content(activity: LessonActivity): ViewGroup = activity.findViewById(R.id.lessonContent)
    private fun descendants(view: View): List<View> = buildList { add(view); if (view is ViewGroup) repeat(view.childCount) { addAll(descendants(view.getChildAt(it))) } }
    private fun integers(array: JSONArray) = List(array.length()) { array.getInt(it) }
    private fun strings(array: JSONArray) = List(array.length()) { array.getString(it) }
    private fun field(activity: LessonActivity, name: String): Any? = LessonActivity::class.java.getDeclaredField(name).apply { isAccessible = true }.get(activity)
    private fun setField(activity: LessonActivity, name: String, value: Any) = LessonActivity::class.java.getDeclaredField(name).apply { isAccessible = true }.set(activity, value)
    private fun invoke(activity: LessonActivity, name: String) = LessonActivity::class.java.getDeclaredMethod(name).apply { isAccessible = true }.invoke(activity)
    private fun preferenceFiles() = File(context.applicationInfo.dataDir, "shared_prefs").listFiles().orEmpty()
        .filter { it.name.endsWith(".xml") }.map { it.name.removeSuffix(".xml") }.toSet()
    private fun sha(text: String) = MessageDigest.getInstance("SHA-256").digest(text.toByteArray()).joinToString("") { "%02x".format(it) }
    private fun shaBytes(bytes: ByteArray) = MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it) }
    private fun await(reason: String, timeout: Long = 12_000L, condition: ((Boolean) -> Unit) -> Unit) {
        val until = SystemClock.uptimeMillis() + timeout
        while (SystemClock.uptimeMillis() < until) {
            instrumentation.waitForIdleSync()
            var success = false
            condition { success = it }
            if (success) return
            SystemClock.sleep(40L)
        }
        fail(reason)
    }
    private fun evidenceDirectory() = File(requireNotNull(context.getExternalFilesDir(null)), "cpp-pending-capabilities-qa/$session").apply { mkdirs() }
    private fun capture(scenario: ActivityScenario<LessonActivity>, name: String, target: (LessonActivity) -> View) {
        await("The captured native view must have real nonzero dimensions") { ready -> scenario.onActivity {
            val view = target(it); ready(view.width > 0 && view.height > 0 && !view.isLayoutRequested)
        } }
        scenario.onActivity { activity ->
            val view = target(activity)
            val bitmap = Bitmap.createBitmap(view.width, view.height, Bitmap.Config.ARGB_8888)
            try {
                val canvas = Canvas(bitmap); canvas.drawColor(Color.rgb(28, 31, 36)); view.draw(canvas)
                File(evidenceDirectory(), "$name.png").outputStream().use { assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) }
                File(evidenceDirectory(), "$name.json").writeText(JSONObject().put("package", context.packageName)
                    .put("width", view.width).put("height", view.height).put("text", (view as? TextView)?.text?.toString())
                    .put("kind", "native view at its real Android bounds; not a window screenshot").toString(2))
            } finally { bitmap.recycle() }
        }
    }

    companion object {
        private const val ROUTE = "programming"
        private const val DRAG_LESSON = "programming_practice_tools__comprobaciones_repetibles__nombrar_reglas_y_separar_acuerdos"
        private const val ORDER_LESSON = "programming_practice_tools__comprobaciones_repetibles__datos_y_listas_de_objetivos"
        private const val CPP_ORDER_LESSON = "cpp_fundamentals__programas_y_ejecucion__primer_programa_y_salida"
        private const val INLINE_LESSON = "cpp_fundamentals__texto_interpretacion_y_flujos__nombres_del_lenguaje_y_tokens_con_texto"
        private const val HISTORY_LESSON = "programming_practice_tools__comprobaciones_repetibles__reorganizar_la_descripcion_conservando_el_comprobador"
        private val QA_PACKAGES = setOf("com.learngraphicsprogramming.app.curricularqa", "com.learngraphicsprogramming.app.realdownloadsqa")
        private val KNOWN_PREFERENCES = setOf("learning_progress", "navigation_state", "lesson_narration_settings", "attempt_reserve_seconds_test",
            "cpp_lesson_drafts", "app_settings", "appearance_settings", "ui_language_settings", "premium_state")
    }
}
