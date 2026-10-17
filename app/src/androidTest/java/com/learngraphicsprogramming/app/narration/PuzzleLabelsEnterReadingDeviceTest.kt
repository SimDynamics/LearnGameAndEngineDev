package com.learngraphicsprogramming.app.narration

import android.app.Activity
import android.app.Application
import android.app.KeyguardManager
import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Rect
import android.media.MediaPlayer
import android.text.InputType
import android.os.Build
import android.os.Bundle
import android.os.SystemClock
import android.view.View
import android.view.KeyEvent
import android.view.inputmethod.EditorInfo
import android.view.ViewGroup
import android.view.WindowManager
import android.view.MotionEvent
import android.view.InputDevice
import android.widget.TextView
import android.widget.CompoundButton
import android.widget.Button
import android.graphics.Color
import android.widget.EditText
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.BuildConfig
import com.learngraphicsprogramming.app.LessonActivity
import com.learngraphicsprogramming.app.LessonRepository
import com.learngraphicsprogramming.app.PremiumRepository
import com.learngraphicsprogramming.app.ProgressRepository
import com.learngraphicsprogramming.app.R
import com.learngraphicsprogramming.app.UiLanguageSettings
import com.learngraphicsprogramming.app.math.MathPuzzleRenderer
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

/** Real published labels and keyboard submission; no replacement answers, curriculum or synthetic word timing. */
@RunWith(AndroidJUnit4::class)
class PuzzleLabelsEnterReadingDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val preferences = linkedMapOf<String, Map<String, *>>()
    private val durableFiles = linkedMapOf<String, ByteArray?>()
    private var originalPreferenceFiles = emptySet<String>()
    private var originalLocale = ""
    private var preserved = false
    private var scenario: ActivityScenario<LessonActivity>? = null
    private val session = UUID.randomUUID().toString()
    private val guard = object : Application.ActivityLifecycleCallbacks {
        override fun onActivityCreated(activity: Activity, state: Bundle?) {
            activity.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        }
        override fun onActivityStarted(activity: Activity) = Unit
        override fun onActivityResumed(activity: Activity) = Unit
        override fun onActivityPaused(activity: Activity) = Unit
        override fun onActivityStopped(activity: Activity) = Unit
        override fun onActivitySaveInstanceState(activity: Activity, state: Bundle) = Unit
        override fun onActivityDestroyed(activity: Activity) = Unit
    }

    @Before fun isolateLdplayerAndPreserveEveryPreference() {
        assertTrue("Use an isolated QA package", context.packageName in QA_PACKAGES)
        assertEquals(context.packageName, BuildConfig.APPLICATION_ID)
        assertTrue("Only LDPlayer is authorized", Build.SUPPORTED_ABIS.any { it == "x86" || it == "x86_64" })
        val lock = context.getSystemService(KeyguardManager::class.java)
        assertFalse("Unlock LDPlayer before native checks", lock.isDeviceLocked || lock.isKeyguardLocked)
        originalPreferenceFiles = preferenceFiles()
        (originalPreferenceFiles + KNOWN_PREFERENCES).forEach { name ->
            preferences[name] = context.getSharedPreferences(name, Context.MODE_PRIVATE).all.mapValues { (_, value) ->
                if (value is Set<*>) value.toSet() else value
            }
        }
        originalLocale = UiLanguageSettings.selectedTag(context)
        listOf(KokoroPagePreparationQueue.FILE_NAME, NarrationAudioPlanner.FILE_NAME).forEach { name ->
            durableFiles[name] = File(context.filesDir, name).takeIf(File::isFile)?.readBytes()
        }
        preserved = true
        (context.applicationContext as Application).registerActivityLifecycleCallbacks(guard)
        instrumentation.runOnMainSync { UiLanguageSettings.select(context, "es") }
        assertTrue(resumePreferences().edit().clear().commit())
        NarrationSettingsRepository.saveEnabled(context, false)
        NarrationSettingsRepository.saveEngine(context, NarrationEngine.KOKORO)
        NarrationSettingsRepository.saveLanguage(context, NarrationLanguage.SPANISH)
        NarrationSettingsRepository.saveKokoroVoice(context, "ef_dora")
        NarrationSettingsRepository.saveRate(context, 1f)
        NarrationSettingsRepository.saveControlsVisible(context, true)
        PremiumRepository.setVerifiedPremium(context)
    }

    @After fun stopPlaybackBeforeRestoringStudentAndSchedulerState() {
        if (!preserved) return
        var closed: LessonNarrationController? = null
        try {
            instrumentation.runOnMainSync { NarrationSettingsRepository.saveEnabled(context, false) }
            scenario?.onActivity { activity -> closed = field<LessonNarrationController?>(activity, "narrationController")?.also { it.stop() } }
            scenario?.close()
            scenario = null
            closed?.let { reader -> await("Reader worker closes", 20_000) { !field<Thread>(reader, "workerThread").isAlive } }
            context.stopService(Intent(context, KokoroContinuousNarrationService::class.java))
            await("Background synthesis stops before restoring the scheduler", 30_000) {
                Thread.getAllStackTraces().keys.none { it.isAlive && it.name == "KokoroOfflineNarration" }
            }
        } finally {
            (context.applicationContext as Application).unregisterActivityLifecycleCallbacks(guard)
            instrumentation.runOnMainSync { UiLanguageSettings.select(context, originalLocale) }
            (preferenceFiles() + preferences.keys).forEach { name ->
                if (name !in originalPreferenceFiles) context.deleteSharedPreferences(name)
                else {
                    val target = context.getSharedPreferences(name, Context.MODE_PRIVATE)
                    val editor = target.edit().clear()
                    preferences.getValue(name).forEach { (key, value) ->
                        when (value) {
                            is String -> editor.putString(key, value)
                            is Int -> editor.putInt(key, value)
                            is Long -> editor.putLong(key, value)
                            is Float -> editor.putFloat(key, value)
                            is Boolean -> editor.putBoolean(key, value)
                            is Set<*> -> editor.putStringSet(key, value.filterIsInstance<String>().toSet())
                            else -> error("Unexpected preference $name/$key")
                        }
                    }
                    assertTrue(editor.commit())
                    assertEquals("Every progress/preference value is preserved", preferences[name], target.all)
                }
            }
            durableFiles.forEach { (name, bytes) ->
                val file = File(context.filesDir, name)
                if (bytes == null) file.delete() else file.writeBytes(bytes)
            }
            synchronized(KokoroPagePreparationQueue) {
                listOf("directory", "queue").forEach { name ->
                    KokoroPagePreparationQueue::class.java.getDeclaredField(name).apply { isAccessible = true }
                        .set(KokoroPagePreparationQueue, null)
                }
            }
            synchronized(NarrationAudioPlanner) {
                field<MutableMap<File, Any>>(NarrationAudioPlanner, "stores")
                    .remove(File(context.filesDir, NarrationAudioPlanner.FILE_NAME).canonicalFile)
            }
            assertEquals(originalPreferenceFiles, preferenceFiles())
        }
    }

    @Test fun publishedPageFiveUnitLabelIsActuallyNarratedAndWordShaded() = exercisePublishedUnitLabel(directResume = false)

    @Test fun directResumeKeepsPendingInkUntilNativeMetadataAndRewindsTheSameUnitRecording() =
        exercisePublishedUnitLabel(directResume = true)

    private fun exercisePublishedUnitLabel(directResume: Boolean) {
        val original = LessonRepository.loadLessonJson(context, ROUTE, LESSON).toString()
        val host = openMath(4, narrate = true)
        lateinit var label: ReadingTextView
        lateinit var sentences: List<String>
        lateinit var segments: List<Int>
        var labelTarget = 0
        host.onActivity { activity ->
            val card = mathCard(activity, 4, 1)
            label = descendants(card).filterIsInstance<ReadingTextView>().single { it.text.toString() == activity.getString(R.string.math_ui_unit, "cm") }
            val script = currentScript(activity)
            sentences = NarrationSegments.sentences(script.texts)
            val plan = requireNotNull(script.readingPlan(NarrationEngine.KOKORO))
            val body = sentences.indices.filter { plan.origins[it].blockIndex == 1 && !plan.origins[it].feedback }
            val spoken = body.map { ManualEnglishSpeech.strip(sentences[it]) }
            val colors = field<Map<String, LessonReadingTextColors>>(activity, "readingTextPages").getValue(identity(activity))
            labelTarget = field(requireNotNull(field<Map<TextView, Any>>(colors, "entries")[label]), "target")
            label.requestRectangleOnScreen(Rect(0, 0, label.width, label.height), true)
            capture(label, "published-page-five-unit-label-before")
            writeEvidence("published-page-five-unit-label-before", JSONObject().put("visible", label.text.toString())
                .put("actualBlockNarration", JSONArray(spoken)).put("pendingRanges", ranges(label))
                .put("labelTarget", labelTarget).put("bodyMapping", JSONArray(body.map { segment ->
                    mappingEvidence(segment, sentences[segment], wordMappings(activity)[segment])
                })))
            segments = body.filter { ManualEnglishSpeech.strip(sentences[it]).contains("Unidad", ignoreCase = true) }
            assertTrue("The real displayed unit heading must be narrated, not only its hidden abbreviation", segments.isNotEmpty())
            assertTrue("The existing visible unit label is registered for word shading", unread(label).isNotEmpty())
            assertTrue(label.safeSupportsWordReading)
            if (directResume) {
                val key = NarrationResumePolicy.key(identity(activity), "es", NarrationEngine.KOKORO.name, "ef_dora", sentences)
                NarrationResumeStore(activity).begin(key, NarrationResumePoint(segments.first()))
            }
            assertTrue(activity.findViewById<View>(R.id.buttonLessonListen).performClick())
        }
        var mixed = false
        var replay = false
        var missingMetadataObserved = false
        var conservativeWithoutMetadata = true
        val samples = JSONArray()
        var lastEvidenceAt = 0L
        fun observeUnitPlayback() = await("The actual unit label progresses with native Kokoro speech", 180_000) {
            var done = false
            host.onActivity { activity ->
                val reader = controller(activity)
                val index = field<Int>(reader, "index")
                if (reader.isPlaying && field<Boolean>(reader, "playerReady") && index in segments) {
                    val pending = unread(label)
                    val unitStart = label.text.indexOf("cm")
                    val headingPending = pending.any { it.start < 6 && it.endExclusive > 0 }
                    val unitPending = pending.any { it.start < unitStart + 2 && it.endExclusive > unitStart }
                    val alignment = field<Map<Int, NarrationWordAlignment>>(wordPage(activity), "alignments")[index]
                    if (alignment == null) {
                        missingMetadataObserved = true
                        conservativeWithoutMetadata = conservativeWithoutMetadata && label.text.indices.filter { !label.text[it].isWhitespace() }
                            .all { glyph -> pending.any { glyph in it.start until it.endExclusive } }
                    }
                    if (!headingPending && unitPending) {
                        if (!mixed) capture(label, "published-page-five-unit-label-mixed-native")
                        mixed = true
                    }
                    samples.put(JSONObject().put("replay", replay).put("segment", index).put("positionMs", requireNotNull(field<MediaPlayer?>(reader, "player")).currentPosition)
                        .put("pendingRanges", ranges(label)).put("headingPending", headingPending).put("unitPending", unitPending)
                        .put("wordMapping", mappingEvidence(index, sentences[index], wordMappings(activity)[index]))
                        .put("nativeAlignment", alignment?.let { native ->
                            JSONObject().put("audioSha256", native.audioSha256).put("sampleCount", native.sampleCount)
                                .put("wordCount", native.words.size).put("words", JSONArray(native.words.map {
                                    JSONArray(listOf(it.start, it.endExclusive, it.startSample, it.endSample))
                                }))
                        }))
                    if (SystemClock.uptimeMillis() - lastEvidenceAt >= 500) {
                        writeEvidence("published-page-five-unit-label-native", JSONObject().put("visible", label.text.toString())
                            .put("labelTarget", labelTarget).put("mixed", mixed).put("samples", samples))
                        lastEvidenceAt = SystemClock.uptimeMillis()
                    }
                }
                done = index > segments.last()
            }
            done
        }
        observeUnitPlayback()
        var reusedAudioHash: String? = null
        if (directResume) {
            lateinit var originalAudio: File
            host.onActivity { activity ->
                val reader = controller(activity)
                reader.pauseKeepingPosition()
                assertNull("Waiting for metadata cannot retain a paused MediaPlayer", field<MediaPlayer?>(reader, "player"))
                originalAudio = requireNotNull(field<File?>(field<List<Any>>(reader, "segments")[segments.first()], "file"))
                writeEvidence("published-page-five-direct-resume-first-pass", JSONObject().put("mixed", mixed)
                    .put("missingMetadataObserved", missingMetadataObserved).put("conservativeWithoutMetadata", conservativeWithoutMetadata)
                    .put("audioPath", originalAudio.absolutePath).put("samples", samples))
                assertTrue("A cold decoder never invents word timing before real metadata is available", conservativeWithoutMetadata)
            }
            reusedAudioHash = sha256(originalAudio)
            await("Native metadata must arrive on the same controller after direct resume", 30_000) {
                var ready = false
                host.onActivity { activity ->
                    val alignment = field<Map<Int, NarrationWordAlignment>>(wordPage(activity), "alignments")[segments.first()]
                    ready = alignment != null && alignment.words.size == wordMappings(activity).getValue(segments.first()).spoken.size
                    if (ready) assertEquals("Word metadata belongs to the existing immutable recording", reusedAudioHash, alignment!!.audioSha256)
                }
                ready
            }
            host.onActivity { activity ->
                val reader = controller(activity)
                reader.seekBy(-3)
                assertTrue("Rewind through the existing controls returns before the unit label", field<Int>(reader, "index") <= segments.first())
                assertTrue(activity.findViewById<View>(R.id.buttonLessonListen).performClick())
            }
            replay = true
            mixed = false
            observeUnitPlayback()
            assertEquals("Rewind reuses the WAV; no audio is replaced to obtain timings", reusedAudioHash, sha256(originalAudio))
        }
        host.onActivity { activity ->
            controller(activity).pauseKeepingPosition()
            capture(label, "published-page-five-unit-label-after-native")
            writeEvidence("published-page-five-unit-label-native", JSONObject().put("visible", label.text.toString())
                .put("labelTarget", labelTarget).put("directResume", directResume).put("mixed", mixed)
                .put("missingMetadataObserved", missingMetadataObserved).put("conservativeWithoutMetadata", conservativeWithoutMetadata)
                .put("reusedAudioSha256", reusedAudioHash).put("finalPendingRanges", ranges(label)).put("samples", samples))
            assertTrue("Unidad and cm must not whiten together only at the label's end", mixed)
            assertTrue(unread(label).isEmpty())
            assertNull(field<MediaPlayer?>(controller(activity), "player"))
            assertFalse("Reading a label cannot solve the numeric answer", saved(1)?.optBoolean("correct", false) ?: false)
            assertEquals(original, field<JSONObject>(activity, "lessonJson").toString())
        }
        assertEquals(original, LessonRepository.loadLessonJson(context, ROUTE, LESSON).toString())
    }

    @Test fun continueStaysGrayUntilEveryMathChoiceIsSolvedAndEnterAdvancesOnePage() {
        val host = openMathLesson(LESSON, "numeros_y_operaciones", 0, narrate = false,
            requireTextInput = false)
        host.onActivity { activity ->
            val continueButton = activity.findViewById<Button>(R.id.buttonContinue)
            assertFalse("Two unsolved choices block the first page", continueButton.isEnabled)
            val disabledTint = requireNotNull(continueButton.backgroundTintList)
                .getColorForState(intArrayOf(-android.R.attr.state_enabled), Color.TRANSPARENT)
            assertEquals("Disabled Continue uses the existing gray", Color.rgb(72, 74, 82), disabledTint)
            for (block in listOf(4, 5)) {
                val card = mathCard(activity, 0, block)
                reset(card, activity)
            }
        }
        host.onActivity { activity ->
            for (block in listOf(4, 5)) {
                val card = mathCard(activity, 0, block)
                val options = descendants(card).filterIsInstance<android.widget.RadioButton>()
                assertEquals(3, options.size)
                assertTrue(options[1].performClick())
                val check = descendants(card).single { it.tag == MathPuzzleRenderer.CHECK_ACTION_TAG }
                assertTrue(check.isEnabled && check.performClick())
                val expectedReady = block == 5
                assertEquals("Advance is enabled only after the last puzzle", expectedReady,
                    activity.findViewById<Button>(R.id.buttonContinue).isEnabled)
            }
            val button = activity.findViewById<Button>(R.id.buttonContinue)
            button.isFocusableInTouchMode = true
            assertTrue(button.requestFocus())
        }
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_ENTER)
        instrumentation.waitForIdleSync()
        host.onActivity { activity ->
            assertEquals("A single Enter must advance one page, not skip puzzles", 1,
                field<Int>(activity, "currentPage"))
            assertFalse("Next page has new unresolved puzzles",
                activity.findViewById<Button>(R.id.buttonContinue).isEnabled)
            invoke(activity, "goPreviousPage")
            assertEquals("Returning uses the original cached page", 0,
                field<Int>(activity, "currentPage"))
            val card = mathCard(activity, 0, 4)
            reset(card, activity)
            assertFalse("Resetting the cached page blocks Continue",
                activity.findViewById<Button>(R.id.buttonContinue).isEnabled)
            val options = descendants(card).filterIsInstance<android.widget.RadioButton>()
            assertTrue(options[1].performClick())
            assertTrue(options[1].requestFocus())
        }
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_ENTER)
        instrumentation.waitForIdleSync()
        host.onActivity { activity ->
            assertEquals(0, field<Int>(activity, "currentPage"))
            assertTrue("Enter still checks a previously cached puzzle",
                ProgressRepository.getExerciseState(context, LESSON, 0, 4)?.optBoolean("correct", false) == true)
            assertTrue("Both cached puzzles are solved again",
                activity.findViewById<Button>(R.id.buttonContinue).isEnabled)
        }
    }

    @Test fun lessonThreePageFiveChecksBothConsecutivePuzzlesWithOneEnterEachThenAdvances() {
        val lesson = "cpp_fundamentals__programas_y_ejecucion__texto_secuencia_y_formato"
        val published = LessonRepository.loadLessonJson(context, "programming", lesson)
        val page = published.getJSONArray("pages").getJSONObject(4)
        val blocks = page.getJSONArray("blocks")
        assertEquals("inline_fill_code", blocks.getJSONObject(1).getString("type"))
        assertEquals("order_code", blocks.getJSONObject(2).getString("type"))
        assertEquals(8, published.getJSONArray("pages").length())

        // Keep the test deterministic without changing any published curriculum.
        // @After restores the isolated QA application's original progress data.
        ProgressRepository.saveExerciseState(context, "programming", lesson, 4, 1, JSONObject()
            .put("correct", false).put("answers", JSONArray().put("").put("")))
        ProgressRepository.saveExerciseState(context, "programming", lesson, 4, 2, JSONObject()
            .put("correct", false).put("order", JSONArray().put(0).put(1).put(2).put(3)))
        val host = openProgramming(lesson, 4, narrate = false)
        lateinit var originalEditor: EditText
        host.onActivity { activity ->
            val inputs = descendants(content(activity)).filterIsInstance<EditText>()
                .filter { it.tag == "cpp_code_input" }
            assertEquals("Two native C++ blanks appear on this page", 2, inputs.size)
            inputs[0].setText("<<")
            inputs[1].setText("\"Carga lista\"")
            originalEditor = inputs[1]
            assertTrue(originalEditor.requestFocus())
            assertEquals(2, field<Int>(activity, "requiredExercises"))
            assertEquals(0, field<Int>(activity, "completedExercises"))
            assertFalse(activity.findViewById<Button>(R.id.buttonContinue).isEnabled)
        }
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_ENTER)
        instrumentation.waitForIdleSync()
        host.onActivity { activity ->
            assertEquals("One Enter checks the first inline-code puzzle", true,
                ProgressRepository.getExerciseState(context, lesson, 4, 1)?.optBoolean("correct", false))
            assertEquals(1, field<Int>(activity, "completedExercises"))
            assertFalse(activity.findViewById<Button>(R.id.buttonContinue).isEnabled)
            assertFalse("The completed, disabled C++ input must release native keyboard focus",
                originalEditor.hasFocus())

            // Arrange the real published draggable fragments via their supported
            // click-to-order behavior. Never replace its validator or saved answer.
            val rows = descendants(content(activity)).filterIsInstance<TextView>()
                .filter { it.tag is Int && it.isClickable }
            assertEquals(4, rows.size)
            val wanted = blocks.getJSONObject(2).getJSONArray("correctOrder")
            for (position in 0 until wanted.length()) {
                assertTrue(rows.single { it.tag == wanted.getInt(position) }.performClick())
            }
            val enabledChecks = descendants(content(activity)).filterIsInstance<Button>()
                .filter { it.text.toString() == activity.getString(R.string.exercise_check) && it.isEnabled }
            assertEquals("Only the order puzzle remains to check", 1, enabledChecks.size)
            // Reproduce the real native-focus handoff after touching the second
            // puzzle: Enter must execute it once, then release its old button focus.
            val orderCheck = enabledChecks.single()
            orderCheck.isFocusableInTouchMode = true
            assertTrue(orderCheck.requestFocus())
        }

        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_ENTER)
        instrumentation.waitForIdleSync()
        host.onActivity { activity ->
            assertTrue("One Enter checks the second order-code puzzle",
                ProgressRepository.getExerciseState(context, lesson, 4, 2)?.optBoolean("correct", false) == true)
            assertEquals(2, field<Int>(activity, "completedExercises"))
            assertTrue("Continue is enabled as soon as both puzzles are complete",
                activity.findViewById<Button>(R.id.buttonContinue).isEnabled)
            assertEquals("Checking cannot skip the current page", 4,
                field<Int>(activity, "currentPage"))
        }

        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_ENTER)
        instrumentation.waitForIdleSync()
        host.onActivity { activity ->
            assertEquals("One more Enter moves from page 5 to page 6", 5,
                field<Int>(activity, "currentPage"))
            assertEquals(8, field<Int>(activity, "totalPages"))
        }
    }

    @Test fun singleEnterChecksOnPageFiveThenAdvancesAndFinishesWithoutExtraPresses() {
        val lesson = "cpp_fundamentals__programas_y_ejecucion__primer_programa_y_salida"
        val published = LessonRepository.loadLessonJson(context, "programming", lesson)
        assertEquals(6, published.getJSONArray("pages").length())
        // QA preferences are saved and restored around each test.
        ProgressRepository.setLessonCompleted(context, lesson, false)
        val host = openProgramming(lesson, 4, narrate = false)
        host.onActivity { activity ->
            val reset = descendants(content(activity)).single {
                it.contentDescription == activity.getString(R.string.exercise_reset)
            }
            assertTrue(reset.performClick())
            val question = published.getJSONArray("pages").getJSONObject(4)
                .getJSONArray("blocks").getJSONObject(4)
            val selectedAnswer = question.getJSONArray("options")
                .getString(question.getInt("correctIndex"))
            val radio = descendants(content(activity)).filterIsInstance<android.widget.RadioButton>()
                .single { it.text.toString() == selectedAnswer }
            assertTrue(radio.performClick())
            val readyCheck = descendants(content(activity)).filterIsInstance<Button>().single {
                it.text.toString() == activity.getString(R.string.exercise_check) && it.isEnabled
            }
            assertTrue(readyCheck.isEnabled)
            assertFalse(activity.findViewById<Button>(R.id.buttonContinue).isEnabled)
            // Android may leave focus on a native control unrelated to the selected
            // answer. Enter must check the puzzle immediately, not prime the focus.
            val compilerRun = descendants(content(activity)).single { it.tag == "cpp_run" }
            compilerRun.isFocusableInTouchMode = true
            assertTrue(compilerRun.requestFocus())
        }
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_ENTER)
        instrumentation.waitForIdleSync()
        host.onActivity { activity ->
            assertEquals("Checking must not advance page five", 4, field<Int>(activity, "currentPage"))
            assertTrue("A single Enter checks the selected puzzle",
                ProgressRepository.getExerciseState(context, lesson, 4, 4)?.optBoolean("correct", false) == true)
            assertTrue("Continue becomes available after the check",
                activity.findViewById<Button>(R.id.buttonContinue).isEnabled)
            assertFalse("Checking the puzzle must not complete the lesson",
                ProgressRepository.isLessonCompleted(context, lesson))
        }

        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_ENTER)
        instrumentation.waitForIdleSync()
        host.onActivity { activity ->
            assertEquals("The next separate Enter opens page 6/6 without skipping it", 5,
                field<Int>(activity, "currentPage"))
            assertEquals(6, field<Int>(activity, "totalPages"))
            assertTrue("Finish is enabled on the puzzle-free last page",
                activity.findViewById<Button>(R.id.buttonContinue).isEnabled)
            assertFalse(ProgressRepository.isLessonCompleted(context, lesson))
        }

        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_ENTER)
        instrumentation.waitForIdleSync()
        assertTrue("A single new Enter on page six completes the lesson",
            ProgressRepository.isLessonCompleted(context, lesson))
        host.close()
        scenario = null
    }

    @Test fun enterFromPageFiveOpensPageSixOnceAndDoesNotSkipToLessonMenu() {
        val lesson = "cpp_fundamentals__programas_y_ejecucion__primer_programa_y_salida"
        val published = LessonRepository.loadLessonJson(context, "programming", lesson)
        assertEquals(6, published.getJSONArray("pages").length())
        val host = openProgramming(lesson, 4, narrate = false)
        lateinit var editor: EditText
        var before = ""
        host.onActivity { activity ->
            assertEquals(4, field<Int>(activity, "currentPage"))
            val next = activity.findViewById<Button>(R.id.buttonContinue)
            val reset = descendants(content(activity)).single {
                it.contentDescription == activity.getString(R.string.exercise_reset)
            }
            assertTrue(reset.performClick())
            assertFalse("The last unresolved choice keeps Continue gray", next.isEnabled)

            val choice = published.getJSONArray("pages").getJSONObject(4)
                .getJSONArray("blocks").getJSONObject(4)
            val correct = choice.getJSONArray("options").getString(choice.getInt("correctIndex"))
            val radio = descendants(content(activity)).filterIsInstance<android.widget.RadioButton>()
                .single { it.text.toString() == correct }
            assertTrue(radio.performClick())
            val check = descendants(content(activity)).filterIsInstance<Button>().single {
                it.text.toString() == activity.getString(R.string.exercise_check) && it.isEnabled
            }
            assertTrue(check.performClick())
            assertTrue(next.isEnabled)
            editor = descendants(content(activity)).filterIsInstance<EditText>()
                .single { it.tag == "cpp_code_input" }
            before = editor.text.toString()
            assertTrue(editor.requestFocus())
            editor.setSelection(editor.text.length)
        }
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_ENTER)
        instrumentation.waitForIdleSync()
        host.onActivity { activity ->
            assertEquals("Enter while editing C++ must keep page 5 visible", 4,
                field<Int>(activity, "currentPage"))
            assertTrue("Enter still inserts a compiler newline",
                editor.text.count { it == '\n' } > before.count { it == '\n' })
            val runButton = descendants(content(activity)).single { it.tag == "cpp_run" }
            runButton.isFocusableInTouchMode = true
            assertTrue("Moving outside the editor permits page navigation", runButton.requestFocus())
        }
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_ENTER)
        instrumentation.waitForIdleSync()
        host.onActivity { activity ->
            assertEquals("One Enter must show page 6 of 6, not finish the lesson", 5,
                field<Int>(activity, "currentPage"))
            assertEquals(6, field<Int>(activity, "totalPages"))
            assertTrue("The last page has no puzzles, so Finish is enabled",
                activity.findViewById<Button>(R.id.buttonContinue).isEnabled)
            // DOWN already advanced once. Another unmatched release is harmless,
            // but a genuinely new DOWN/UP is allowed to finish the new page.
            val t = SystemClock.uptimeMillis()
            assertTrue(activity.dispatchKeyEvent(KeyEvent(t, t + 10,
                KeyEvent.ACTION_UP, KeyEvent.KEYCODE_ENTER, 0)))
            assertEquals("Old UP cannot skip page 6", 5,
                field<Int>(activity, "currentPage"))
        }
        assertEquals("No lesson content is changed by keyboard navigation", published.toString(),
            LessonRepository.loadLessonJson(context, "programming", lesson).toString())
    }

    @Test fun pagesWithoutPuzzlesAdvanceButCompilerEditorRetainsLiteralEnter() {
        val lesson = "cpp_fundamentals__programas_y_ejecucion__texto_secuencia_y_formato"
        val host = openProgramming(lesson, 5, narrate = false)
        host.onActivity { activity ->
            val button = activity.findViewById<Button>(R.id.buttonContinue)
            assertTrue("The published sixth page has no required puzzles", button.isEnabled)
            button.isFocusableInTouchMode = true
            assertTrue(button.requestFocus())
        }
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_ENTER)
        instrumentation.waitForIdleSync()
        var editor: EditText? = null
        var before = ""
        host.onActivity { activity ->
            assertEquals("Enter goes to the next page", 6, field<Int>(activity, "currentPage"))
            assertTrue(activity.findViewById<Button>(R.id.buttonContinue).isEnabled)
            val input = descendants(content(activity)).filterIsInstance<EditText>()
                .single { it.tag == "cpp_code_input" }
            editor = input
            before = input.text.toString()
            assertTrue(input.requestFocus())
            input.setSelection(input.text.length)
        }
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_ENTER)
        instrumentation.waitForIdleSync()
        host.onActivity { activity ->
            assertEquals("Enter in the compiler editor never advances", 6, field<Int>(activity, "currentPage"))
            val actual = requireNotNull(editor).text.toString()
            assertTrue("Enter must insert an actual compiler newline",
                actual.count { it == '\n' } > before.count { it == '\n' })
            val button = activity.findViewById<Button>(R.id.buttonContinue)
            button.isFocusableInTouchMode = true
            assertTrue(button.requestFocus())
        }
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_NUMPAD_ENTER)
        instrumentation.waitForIdleSync()
        host.onActivity { activity ->
            assertEquals("Outside compiler edit mode Enter advances normally", 7,
                field<Int>(activity, "currentPage"))
        }
    }

    @Test fun enterChecksAFullscreenMathematicsChoiceWithoutAdvancingThePage() {
        val host = openMathLesson(LESSON, "numeros_y_operaciones", 0, narrate = false,
            requireTextInput = false)
        host.onActivity { activity ->
            val card = mathCard(activity, 0, 4)
            reset(card, activity)
            val options = descendants(card).filterIsInstance<android.widget.RadioButton>()
            assertTrue(options[1].performClick())
            val fullscreen = descendants(card).single {
                it.contentDescription == activity.getString(R.string.math_ui_fullscreen)
            }
            assertTrue(fullscreen.performClick())
            assertTrue("The fullscreen puzzle owns the screen",
                field<Boolean>(activity, "exerciseFullscreenActive"))
            val check = descendants(card).single { it.tag == MathPuzzleRenderer.CHECK_ACTION_TAG }
            assertTrue("Comprobar remains visible inside the fullscreen puzzle",
                check.isShown && check.isEnabled)
        }
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_ENTER)
        instrumentation.waitForIdleSync()
        host.onActivity { activity ->
            assertEquals("Enter in puzzle fullscreen must never navigate", 0,
                field<Int>(activity, "currentPage"))
            assertTrue("The first Enter checks the chosen answer inside fullscreen",
                ProgressRepository.getExerciseState(context, LESSON, 0, 4)?.optBoolean("correct", false) == true)
            assertFalse("The other unsolved puzzle keeps Continue disabled",
                activity.findViewById<Button>(R.id.buttonContinue).isEnabled)
        }
    }

    @Test fun physicalEnterChecksSelectedMathematicsChoiceWithoutAnAnswerTextField() {
        val host = openMathLesson(LESSON, "numeros_y_operaciones", 0, narrate = false, requireTextInput = false)
        host.onActivity { activity ->
            val card = mathCard(activity, 0, 4)
            reset(card, activity)
            val options = descendants(card).filterIsInstance<android.widget.RadioButton>()
            assertEquals(3, options.size)
            assertTrue(options[1].performClick())
            assertTrue(options[1].requestFocus())
            val button = descendants(card).single { it.tag == MathPuzzleRenderer.CHECK_ACTION_TAG }
            assertTrue(button.isEnabled)
        }
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_ENTER)
        instrumentation.waitForIdleSync()
        host.onActivity {
            val state = ProgressRepository.getExerciseState(context, LESSON, 0, 4)
            assertTrue("Enter must use the original Comprobar action for a math choice",
                state?.optBoolean("correct", false) == true)
        }
    }

    @Test fun physicalEnterChecksASelectedProgrammingChoiceWithoutAnAnswerTextField() {
        val lesson = "cpp_fundamentals__programas_y_ejecucion__primer_programa_y_salida"
        val host = openProgramming(lesson, 0, narrate = false)
        host.onActivity { activity ->
            val block = field<JSONObject>(activity, "lessonJson").getJSONArray("pages")
                .getJSONObject(0).getJSONArray("blocks").getJSONObject(4)
            val correctIndex = block.getInt("correctIndex")
            val expected = block.getJSONArray("options").getString(correctIndex)
            val reset = descendants(content(activity)).single {
                it.contentDescription == activity.getString(R.string.exercise_reset)
            }
            assertTrue(reset.performClick())
            val option = descendants(content(activity)).filterIsInstance<android.widget.RadioButton>()
                .single { it.text.toString() == expected }
            assertTrue(option.performClick())
            assertTrue(option.requestFocus())
        }
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_NUMPAD_ENTER)
        instrumentation.waitForIdleSync()
        host.onActivity {
            val state = ProgressRepository.getExerciseState(context, lesson, 0, 4)
            assertTrue("Numpad Enter must submit the original C++ multiple-choice check",
                state?.optBoolean("correct", false) == true)
        }
    }

    @Test fun physicalEnterSubmitsTheRealSingleLineMathematicsAnswer() {
        val host = openMath(4, narrate = false)
        prepareSingleAnswer(host)
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_ENTER)
        instrumentation.waitForIdleSync()
        host.onActivity { activity ->
            val card = mathCard(activity, 4, 1)
            writeEvidence("published-page-five-physical-enter", JSONObject().put("saved", saved(1))
                .put("fieldText", descendants(card).filterIsInstance<EditText>().single().text.toString())
                .put("visibleFeedback", JSONArray(descendants(card).filterIsInstance<TextView>().map { it.text.toString() })))
            assertTrue("KEYCODE_ENTER in a one-line answer must run the real check action", saved(1)?.optBoolean("correct", false) == true)
            assertTrue(descendants(card).filterIsInstance<ReadingTextView>().any { it.currentTextColor == Color.rgb(180, 220, 190) })
        }
    }

    @Test fun publishedInlineFractionKeepsItsVisibleSeparatorMappedAroundSpokenOnlyFieldLabels() {
        val lesson = "math_course_01__fracciones_y_equivalencia__fracciones_equivalentes"
        val original = LessonRepository.loadLessonJson(context, ROUTE, lesson).toString()
        val host = openMathLesson(lesson, "fracciones_y_equivalencia", 1, narrate = true)
        lateinit var slash: ReadingTextView
        lateinit var selectedSegments: Set<Int>
        var slashTarget = 0
        host.onActivity { activity ->
            val card = mathCard(activity, 1, 4)
            reset(card, activity)
            val inputs = descendants(card).filterIsInstance<EditText>()
            assertEquals(2, inputs.size)
            inputs[0].setText("92341")
            inputs[1].setText("87329")
            slash = descendants(card).filterIsInstance<ReadingTextView>().single { it.text.toString() == "/" }
            slash.requestRectangleOnScreen(Rect(0, 0, slash.width, slash.height), true)
            val script = currentScript(activity)
            val parts = NarrationSegments.sentences(script.texts)
            val plan = requireNotNull(script.readingPlan(NarrationEngine.KOKORO))
            val body = parts.indices.filter { plan.origins[it].blockIndex == 4 && !plan.origins[it].feedback }
            val spoken = body.map { ManualEnglishSpeech.strip(parts[it]) }
            val colors = field<Map<String, LessonReadingTextColors>>(activity, "readingTextPages").getValue(identity(activity))
            val entry = requireNotNull(field<Map<TextView, Any>>(colors, "entries")[slash])
            slashTarget = field(entry, "target")
            val mappings = wordMappings(activity)
            selectedSegments = body.filter { segment -> mappings[segment]?.targets?.contains(slashTarget) == true }.toSet()
            capture(card, "published-inline-fraction-before")
            writeEvidence("published-inline-fraction-before", JSONObject().put("visibleSeparator", slash.text.toString())
                .put("body", JSONArray(spoken)).put("mappedSegments", JSONArray(selectedSegments.toList()))
                .put("separatorTarget", slashTarget).put("pendingRanges", ranges(slash)))
            assertFalse("Student values never become the curriculum narration", spoken.joinToString(" ").contains("92341"))
            assertFalse(spoken.joinToString(" ").contains("87329"))
            assertTrue("Existing field announcements remain in speech without adding visible labels", spoken.joinToString(" ").contains("numerador"))
            assertTrue(spoken.joinToString(" ").contains("denominador positivo"))
            assertFalse("Hidden inline field labels must not become paintable text", descendants(card).filterIsInstance<ReadingTextView>()
                .any { it.text.contains("Forma reducida") })
            assertTrue("The real separator needs exact lexical ownership, rather than block-completion fallback", selectedSegments.isNotEmpty())
            selectedSegments.forEach { segment ->
                val mapping = mappings.getValue(segment)
                assertEquals("Keep every spoken field-label word for native alignment", NarrationSpokenWords.technicalAtoms(parts[segment], NarrationLanguage.SPANISH).size,
                    mapping.spoken.size)
                assertEquals(mapping.spoken.size, mapping.displayed.size)
                assertEquals(mapping.spoken.size, requireNotNull(mapping.targets).size)
            }
            assertTrue("The separator is pending before its speech", unread(slash).isNotEmpty())
            val key = NarrationResumePolicy.key(identity(activity), "es", NarrationEngine.KOKORO.name, "ef_dora", parts)
            NarrationResumeStore(activity).begin(key, NarrationResumePoint(selectedSegments.min()))
            assertTrue(activity.findViewById<View>(R.id.buttonLessonListen).performClick())
        }
        var pendingDuringNativeSpeech = false
        var separatorReadBeforeSegmentEnd = false
        var exactNativeAlignment = false
        val samples = JSONArray()
        await("The published separator follows its actual Kokoro words around the hidden field labels", 180_000) {
            var done = false
            host.onActivity { activity ->
                val reader = controller(activity)
                val segment = field<Int>(reader, "index")
                if (reader.isPlaying && field<Boolean>(reader, "playerReady") && segment in selectedSegments) {
                    val page = wordPage(activity)
                    val mapping = wordMappings(activity).getValue(segment)
                    val alignment = field<Map<Int, NarrationWordAlignment>>(page, "alignments")[segment]
                    if (alignment != null && alignment.words.size == mapping.spoken.size) exactNativeAlignment = true
                    val player = requireNotNull(field<MediaPlayer?>(reader, "player"))
                    if (unread(slash).isNotEmpty()) pendingDuringNativeSpeech = true
                    else if (player.currentPosition + 150 < player.duration) {
                        if (!separatorReadBeforeSegmentEnd) capture(slash, "published-inline-fraction-native-read")
                        separatorReadBeforeSegmentEnd = true
                    }
                    samples.put(JSONObject().put("segment", segment).put("positionMs", player.currentPosition)
                        .put("durationMs", player.duration).put("pendingRanges", ranges(slash))
                        .put("mappedWords", mapping.spoken.size).put("nativeWords", alignment?.words?.size))
                }
                done = segment > selectedSegments.max()
            }
            done
        }
        host.onActivity { activity ->
            controller(activity).pauseKeepingPosition()
            writeEvidence("published-inline-fraction-native", JSONObject().put("samples", samples)
                .put("pendingDuringNativeSpeech", pendingDuringNativeSpeech).put("separatorReadBeforeSegmentEnd", separatorReadBeforeSegmentEnd)
                .put("exactNativeAlignment", exactNativeAlignment))
            assertTrue("The static separator stays pending during the preceding field-label speech", pendingDuringNativeSpeech)
            assertTrue("The static separator is read before the following denominator announcement ends", separatorReadBeforeSegmentEnd)
            assertTrue("Use full native word alignment; do not shade on a guessed duration", exactNativeAlignment)
            assertTrue(unread(slash).isEmpty())
            assertNull(field<MediaPlayer?>(controller(activity), "player"))
            assertFalse(ProgressRepository.getExerciseState(context, lesson, 1, 4)?.optBoolean("correct", false) ?: false)
            assertEquals(original, field<JSONObject>(activity, "lessonJson").toString())
        }
        assertEquals(original, LessonRepository.loadLessonJson(context, ROUTE, lesson).toString())
    }

    @Test fun imeDoneSubmitsTheRealSingleLineMathematicsAnswer() {
        val host = openMath(4, narrate = false)
        prepareSingleAnswer(host)
        host.onActivity { activity ->
            val input = descendants(mathCard(activity, 4, 1)).filterIsInstance<EditText>().single()
            val connection = requireNotNull(input.onCreateInputConnection(EditorInfo()))
            assertTrue(connection.performEditorAction(EditorInfo.IME_ACTION_DONE))
        }
        instrumentation.waitForIdleSync()
        host.onActivity { activity ->
            writeEvidence("published-page-five-ime-done", JSONObject().put("saved", saved(1)))
            assertTrue("IME Done must submit the same visible one-line numeric answer", saved(1)?.optBoolean("correct", false) == true)
        }
    }

    @Test fun imeNextChecksTheWholeRealMultiFieldPuzzleInsteadOfChangingFocus() {
        val host = openMath(1, narrate = false)
        host.onActivity { activity ->
            val card = mathCard(activity, 1, 6)
            reset(card, activity)
            val inputs = descendants(card).filterIsInstance<EditText>()
            assertEquals(2, inputs.size)
            inputs[0].setText("5000")
            inputs[1].setText("6")
            assertTrue(inputs[0].requestFocus())
            val connection = requireNotNull(inputs[0].onCreateInputConnection(EditorInfo()))
            assertTrue(connection.performEditorAction(EditorInfo.IME_ACTION_NEXT))
        }
        instrumentation.waitForIdleSync()
        host.onActivity { activity ->
            val state = ProgressRepository.getExerciseState(context, LESSON, 1, 6)
            val card = mathCard(activity, 1, 6)
            writeEvidence("published-multifield-ime-next", JSONObject().put("saved", state)
                .put("fields", JSONArray(descendants(card).filterIsInstance<EditText>().map { it.text.toString() })))
            assertTrue("IME Next from an answer field must submit all fields through the existing validator", state?.optBoolean("correct", false) == true)
        }
    }

    @Test fun physicalEnterStillInsertsANewlineInTheRealCppCompilerWithoutCheckingAnotherPuzzle() {
        val lesson = "cpp_fundamentals__programas_y_ejecucion__primer_programa_y_salida"
        val original = LessonRepository.loadLessonJson(context, "programming", lesson).toString()
        val host = openProgramming(lesson, 4, narrate = false)
        lateinit var editor: EditText
        lateinit var before: String
        var questionBefore: String? = null
        var statusBefore = ""
        host.onActivity { activity ->
            assertTrue(descendants(content(activity)).single { it.tag == "cpp_reset" }.performClick())
            editor = descendants(content(activity)).filterIsInstance<EditText>().single { it.tag == "cpp_code_input" }
            assertTrue("This is the real multiline C++ source editor", editor.inputType and InputType.TYPE_TEXT_FLAG_MULTI_LINE != 0)
            assertTrue(editor.requestFocus())
        }
        instrumentation.waitForIdleSync()
        host.onActivity { activity ->
            before = editor.text.toString()
            editor.setSelection(editor.length())
            questionBefore = ProgressRepository.getExerciseState(context, lesson, 4, 4)?.toString()
            statusBefore = (descendants(content(activity)).single { it.tag == "cpp_status" } as TextView).text.toString()
        }
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_ENTER)
        instrumentation.waitForIdleSync()
        host.onActivity { activity ->
            writeEvidence("published-cpp-compiler-physical-enter", JSONObject().put("beforeCode", before).put("afterCode", editor.text.toString())
                .put("questionBefore", questionBefore).put("questionAfter", ProgressRepository.getExerciseState(context, lesson, 4, 4)?.toString()))
            assertEquals("Source Enter remains a real newline, without submitting or running anything", before + "\n", editor.text.toString())
            assertTrue("The real compiler editor retains focus", editor.hasFocus())
            assertEquals(questionBefore, ProgressRepository.getExerciseState(context, lesson, 4, 4)?.toString())
            assertEquals(statusBefore, (descendants(content(activity)).single { it.tag == "cpp_status" } as TextView).text.toString())
            assertEquals(original, field<JSONObject>(activity, "lessonJson").toString())
        }
    }

    @Test fun bothPublishedCoursesRegisterTitlesLabelsOptionsAndOnlyVisibleBodyTextForReading() {
        val mathSource = LessonRepository.loadLessonJson(context, ROUTE, LESSON)
        val mathIdentity = mathSource.toString()
        val mathHost = openMath(4, narrate = true)
        mathHost.onActivity { activity ->
            for (block in 1..3) reset(mathCard(activity, 4, block), activity)
        }
        instrumentation.waitForIdleSync()
        mathHost.onActivity { activity ->
            val script = currentScript(activity)
            val body = script.texts.indices.filter { !script.origins[it].feedback }.map { ManualEnglishSpeech.strip(script.texts[it]) }
            val firstPuzzleBody = script.texts.indices.filter { script.origins[it].blockIndex == 1 && !script.origins[it].feedback }
                .joinToString(" ") { ManualEnglishSpeech.strip(script.texts[it]) }
            val heading = mathSource.getJSONArray("pages").getJSONObject(4).getJSONArray("blocks").getJSONObject(0).getString("text")
            val headingView = descendants(content(activity)).filterIsInstance<ReadingTextView>().single { it.text.toString() == heading }
            assertTrue("A real mathematics content title is covered by the shared word mask", unread(headingView).isNotEmpty())
            assertTrue(body.any { it == heading })
            for (block in 1..3) {
                val card = mathCard(activity, 4, block)
                val passive = descendants(card).filterIsInstance<ReadingTextView>().filter { it.text.isNotBlank() }
                assertTrue(passive.isNotEmpty())
                passive.forEach { view ->
                    assertTrue("Every existing passive mathematics prompt, label, instruction and option is registered: ${view.text}", unread(view).isNotEmpty())
                }
            }
            assertFalse("A unit label cannot make the reader solve 125+175 or expose validator.expected", Regex("(?i)\\b(?:300|trescientos)\\b").containsMatchIn(firstPuzzleBody))
            assertFalse(body.joinToString(" ").contains("transferencia_longitud__valor"))
            assertFalse(body.joinToString(" ").contains("validator"))
            assertEquals(mathIdentity, field<JSONObject>(activity, "lessonJson").toString())
            writeEvidence("published-math-passive-reading-coverage", JSONObject().put("body", JSONArray(body)).put("firstPuzzleBody", firstPuzzleBody))
        }
        closeCurrentScenario()
        val lesson = "cpp_fundamentals__programas_y_ejecucion__primer_programa_y_salida"
        val programming = LessonRepository.loadLessonJson(context, "programming", lesson)
        val programmingIdentity = programming.toString()
        val host = openProgramming(lesson, 0, narrate = true)
        host.onActivity { activity ->
            assertTrue(descendants(content(activity)).single { it.contentDescription == activity.getString(R.string.exercise_reset) }.performClick())
        }
        instrumentation.waitForIdleSync()
        host.onActivity { activity ->
            val blocks = programming.getJSONArray("pages").getJSONObject(0).getJSONArray("blocks")
            val question = blocks.getJSONObject(4)
            val expected = listOf(programming.getString("title"), blocks.getJSONObject(0).getString("text"), question.getString("question")) +
                List(question.getJSONArray("options").length()) { question.getJSONArray("options").getString(it) }
            val allText = descendants(content(activity)).filterIsInstance<TextView>()
            expected.forEach { source ->
                val view = allText.single { it.text.toString() == source }
                assertTrue("The existing C++ title/question/option is a shared reading target: $source", view is ReadingTextHost)
                assertTrue("Every original C++ source glyph starts pending: $source", unread(view).isNotEmpty())
            }
            val script = currentScript(activity)
            val body = script.texts.indices.filter { !script.origins[it].feedback }.map { ManualEnglishSpeech.strip(script.texts[it]) }
            assertFalse("Successful feedback remains behind its existing gate", body.joinToString(" ").contains("La instrucción con"))
            assertFalse(body.joinToString(" ").contains("correctIndex"))
            assertEquals(programmingIdentity, field<JSONObject>(activity, "lessonJson").toString())
            writeEvidence("published-cpp-passive-reading-coverage", JSONObject().put("originalVisibleTargets", JSONArray(expected)).put("body", JSONArray(body)))
        }
        assertEquals(mathIdentity, LessonRepository.loadLessonJson(context, ROUTE, LESSON).toString())
        assertEquals(programmingIdentity, LessonRepository.loadLessonJson(context, "programming", lesson).toString())
    }

    private fun closeCurrentScenario() {
        var previous: LessonNarrationController? = null
        scenario?.onActivity { activity -> previous = field(activity, "narrationController") }
        scenario?.close()
        scenario = null
        previous?.let { reader -> await("The previous course releases its audio worker before opening another", 20_000) {
            !field<Thread>(reader, "workerThread").isAlive
        } }
        NarrationSettingsRepository.saveEnabled(context, false)
    }
    private fun openProgramming(lesson: String, page: Int, narrate: Boolean): ActivityScenario<LessonActivity> {
        ProgressRepository.saveCurrentPage(context, "programming", lesson, page)
        val host = ActivityScenario.launch<LessonActivity>(Intent(context, LessonActivity::class.java)
            .putExtra("routeId", "programming").putExtra("courseId", "cpp_fundamentals")
            .putExtra("unitId", "programas_y_ejecucion").putExtra("lessonId", lesson)
            .putExtra("lessonNumber", 1).putExtra("lessonTotal", 1)).also { scenario = it }
        await("The published Programming page opens with its original controls", 15_000) {
            var ready = false
            host.onActivity { activity -> ready = activity.hasWindowFocus() && field<Int>(activity, "currentPage") == page &&
                content(activity).width > 0 && !content(activity).isLayoutRequested }
            ready
        }
        if (narrate) host.onActivity { activity ->
            NarrationSettingsRepository.saveEnabled(activity, true)
            invoke(activity, "renderCurrentPage")
        }
        await("The original Programming page settles", 10_000) {
            var ready = false
            host.onActivity { ready = !content(it).isLayoutRequested }
            ready
        }
        return host
    }
    private fun prepareSingleAnswer(host: ActivityScenario<LessonActivity>) {
        host.onActivity { activity ->
            val card = mathCard(activity, 4, 1)
            reset(card, activity)
            val input = descendants(card).filterIsInstance<EditText>().single()
            input.setText("300")
            assertTrue(input.requestFocus())
            input.setSelection(input.length())
            assertEquals("The real numeric answer stays a single line", 1, input.maxLines)
        }
        instrumentation.waitForIdleSync()
    }
    private fun openMath(page: Int, narrate: Boolean): ActivityScenario<LessonActivity> =
        openMathLesson(LESSON, "numeros_y_operaciones", page, narrate)
    private fun openMathLesson(lesson: String, unit: String, page: Int, narrate: Boolean,
                               requireTextInput: Boolean = true): ActivityScenario<LessonActivity> {
        ProgressRepository.saveCurrentPage(context, ROUTE, lesson, page)
        val host = ActivityScenario.launch<LessonActivity>(Intent(context, LessonActivity::class.java)
            .putExtra("routeId", ROUTE).putExtra("courseId", "math_course_01")
            .putExtra("unitId", unit).putExtra("lessonId", lesson)
            .putExtra("lessonNumber", 1).putExtra("lessonTotal", 1)).also { scenario = it }
        await("The exact published mathematics page ${page + 1} opens", 15_000) {
            var ready = false
            host.onActivity { activity -> ready = activity.hasWindowFocus() && field<Int>(activity, "currentPage") == page &&
                (!requireTextInput || descendants(content(activity)).filterIsInstance<EditText>().isNotEmpty()) &&
                !content(activity).isLayoutRequested }
            ready
        }
        if (narrate) host.onActivity { activity ->
            NarrationSettingsRepository.saveEnabled(activity, true)
            invoke(activity, "renderCurrentPage")
        }
        await("Native answer controls settle", 10_000) {
            var ready = false
            host.onActivity { ready = content(it).width > 0 && !content(it).isLayoutRequested }
            ready
        }
        return host
    }
    private fun mathCard(activity: LessonActivity, page: Int, block: Int): ViewGroup {
        val prompt = field<JSONObject>(activity, "lessonJson").getJSONArray("pages").getJSONObject(page)
            .getJSONArray("blocks").getJSONObject(block).getString("prompt")
        return descendants(content(activity)).filterIsInstance<ViewGroup>().single { card ->
            card.tag == MathPuzzleRenderer.INTERACTION_TAG && descendants(card).filterIsInstance<TextView>().any { it.text.toString() == prompt } }
    }
    private fun reset(card: ViewGroup, activity: LessonActivity) {
        assertTrue(descendants(card).single { it.contentDescription == activity.getString(R.string.math_ui_restart) }.performClick())
    }
    private fun saved(block: Int) = ProgressRepository.getExerciseState(context, LESSON, 4, block)
    private fun currentScript(activity: LessonActivity): LessonNarrationScript = LessonActivity::class.java
        .getDeclaredMethod("narrationScript", NarrationLanguage::class.java).apply { isAccessible = true }
        .invoke(activity, NarrationLanguage.SPANISH) as LessonNarrationScript
    private fun unread(view: TextView): List<ReadingTextRange> = field((view as ReadingTextHost).readingInk, "unreadRanges")
    private fun ranges(view: TextView) = JSONArray(unread(view).map { JSONArray(listOf(it.start, it.endExclusive)) })
    private fun controller(activity: LessonActivity): LessonNarrationController = field(activity, "narrationController")
    private fun wordPage(activity: LessonActivity): Any {
        val words = field<Any>(field<LessonReadingProgressModel>(activity, "readingProgress"), "words")
        return field<Map<String, Any>>(words, "pages").getValue(identity(activity))
    }
    private fun wordMappings(activity: LessonActivity): Map<Int, NarrationWordMapping> = field(wordPage(activity), "mapping")
    private fun mappingEvidence(segment: Int, text: String, mapping: NarrationWordMapping?): JSONObject =
        JSONObject().put("segment", segment).put("text", text).put("target", mapping?.blockIndex)
            .put("spokenCount", mapping?.spoken?.size).put("targets", mapping?.targets?.let(::JSONArray))
            .put("spokenRanges", mapping?.spoken?.let { JSONArray(it.map { range -> JSONArray(listOf(range.start, range.endExclusive)) }) })
            .put("displayedRanges", mapping?.displayed?.let { JSONArray(it.map { range -> JSONArray(listOf(range.start, range.endExclusive)) }) })
    private fun content(activity: LessonActivity): ViewGroup = activity.findViewById(R.id.lessonContent)
    private fun identity(activity: LessonActivity) = "${field<String>(activity, "currentRouteId")}/${field<String>(activity, "currentLessonId")}/${field<Int>(activity, "currentPage")}"
    private fun descendants(view: View): List<View> = listOf(view) + if (view is ViewGroup)
        (0 until view.childCount).flatMap { descendants(view.getChildAt(it)) } else emptyList()
    @Suppress("UNCHECKED_CAST") private fun <T> field(instance: Any, name: String): T = instance.javaClass
        .getDeclaredField(name).apply { isAccessible = true }.get(instance) as T
    private fun invoke(instance: Any, name: String) = instance.javaClass.getDeclaredMethod(name).apply { isAccessible = true }.invoke(instance)
    private fun await(message: String, timeout: Long, ready: () -> Boolean) {
        val deadline = SystemClock.uptimeMillis() + timeout
        while (SystemClock.uptimeMillis() < deadline) {
            instrumentation.waitForIdleSync()
            if (ready()) return
            SystemClock.sleep(30)
        }
        fail(message)
    }
    private fun capture(view: View, name: String) {
        assertTrue("Real native evidence requires a laid-out view", view.width > 0 && view.height > 0)
        val bitmap = Bitmap.createBitmap(view.width, view.height, Bitmap.Config.ARGB_8888)
        try {
            val canvas = Canvas(bitmap)
            canvas.drawColor(Color.rgb(28, 31, 36))
            view.draw(canvas)
            File(evidence(), "$name.png").outputStream().use { assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) }
        } finally { bitmap.recycle() }
    }
    private fun writeEvidence(name: String, value: JSONObject) = File(evidence(), "$name.json").writeText(value.toString(2))
    private fun sha256(file: File) = MessageDigest.getInstance("SHA-256").digest(file.readBytes()).joinToString("") { "%02x".format(it) }
    private fun evidence() = File(requireNotNull(context.getExternalFilesDir(null)), "puzzle-labels-enter-qa/$session").apply { mkdirs() }
    private fun resumePreferences() = context.getSharedPreferences("lesson_narration_resume_bounded_v1", Context.MODE_PRIVATE)
    private fun preferenceFiles() = File(context.applicationInfo.dataDir, "shared_prefs").listFiles().orEmpty()
        .filter { it.name.endsWith(".xml") }.map { it.name.removeSuffix(".xml") }.toSet()

    companion object {
        private val QA_PACKAGES = setOf("com.learngraphicsprogramming.app.curricularqa", "com.learngraphicsprogramming.app.realdownloadsqa")
        private val KNOWN_PREFERENCES = setOf("learning_progress", "navigation_state", "lesson_narration_settings", "app_settings",
            "appearance_settings", "ui_language_settings", "premium_state", "lesson_text_size_settings", "lesson_narration_resume_bounded_v1")
        private const val ROUTE = "math_physics"
        private const val LESSON = "math_course_01__numeros_y_operaciones__magnitudes_y_numeracion"
    }
}
