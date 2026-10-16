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
import android.os.Build
import android.os.Bundle
import android.os.SystemClock
import android.view.View
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

/** Exact published note and feedback views with real Kokoro; no curriculum replacement or fabricated timing. */
@RunWith(AndroidJUnit4::class)
class NoteFeedbackReadingDeviceTest {
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
            scenario?.onActivity { closed = controller(it).also(LessonNarrationController::stop) }
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

    @Test fun publishedPageFourNoteNarratesItsVisibleHeadingAndProgressesWordByWord() {
        val source = LessonRepository.loadLessonJson(context, ROUTE, LESSON)
        val sourceIdentity = source.toString()
        val host = openPage()
        lateinit var note: ReadingTextView
        lateinit var sentences: List<String>
        lateinit var segments: List<Int>
        host.onActivity { activity ->
            note = descendants(content(activity)).filterIsInstance<ReadingTextView>().single { it.text.startsWith("Nota\n\nAl escribir") }
            val script = currentScript(activity)
            sentences = NarrationSegments.sentences(script.texts)
            val plan = requireNotNull(script.readingPlan(NarrationEngine.KOKORO))
            segments = sentences.indices.filter { plan.origins[it].blockIndex == 3 && !plan.origins[it].feedback }
            val actual = segments.joinToString(" ") { ManualEnglishSpeech.strip(sentences[it]) }.replace(Regex("\\s+"), " ").trim()
            val expected = note.text.toString().replace(Regex("\\s+"), " ").trim()
            note.requestRectangleOnScreen(Rect(0, 0, note.width, note.height), true)
            capture(note, "published-page-four-note-before")
            writeEvidence("published-page-four-note-before", JSONObject().put("visible", note.text.toString()).put("actualNarrated", actual)
                .put("pendingRanges", ranges(note)))
            assertEquals("The note's actual visible heading and original body must both be spoken", expected, actual)
            assertTrue(note.safeSupportsWordReading)
            val key = NarrationResumePolicy.key(identity(activity), "es", NarrationEngine.KOKORO.name, "ef_dora", sentences)
            NarrationResumeStore(activity).begin(key, NarrationResumePoint(segments.first()))
            assertTrue(activity.findViewById<View>(R.id.buttonLessonListen).performClick())
        }
        var mixed = false
        val samples = JSONArray()
        await("The exact note must show read and pending words during real Kokoro", 180_000) {
            var done = false
            host.onActivity { activity ->
                val reader = controller(activity)
                val index = field<Int>(reader, "index")
                if (reader.isPlaying && field<Boolean>(reader, "playerReady") && index in segments) {
                    val pending = unread(note).sumOf { it.endExclusive - it.start }
                    if (!mixed && pending in 3 until note.text.count { !it.isWhitespace() } - 6) {
                        mixed = true
                        capture(note, "published-page-four-note-mixed-native")
                    }
                    samples.put(JSONObject().put("segment", index).put("positionMs", requireNotNull(field<MediaPlayer?>(reader, "player")).currentPosition)
                        .put("pendingRanges", ranges(note)))
                }
                done = index > segments.last()
            }
            done
        }
        host.onActivity { activity ->
            val reader = controller(activity)
            reader.pauseKeepingPosition()
            assertTrue("The same note has normal and gray words while it is spoken", mixed)
            assertTrue("The complete note including Nota is read at its actual end", unread(note).isEmpty())
            assertNull(field<MediaPlayer?>(reader, "player"))
            assertEquals(sourceIdentity, field<JSONObject>(activity, "lessonJson").toString())
            writeEvidence("published-page-four-note-native", JSONObject().put("visible", note.text.toString()).put("samples", samples))
        }
        assertEquals(sourceIdentity, LessonRepository.loadLessonJson(context, ROUTE, LESSON).toString())
    }

    @Test fun publishedPageFourWrongFeedbackSpeaksEveryVisiblePhraseShadesWordsAndReplaysAfterSeeking() {
        val sourceIdentity = LessonRepository.loadLessonJson(context, ROUTE, LESSON).toString()
        val host = openPage()
        host.onActivity { activity ->
            val card = mathCard(activity, 4)
            reset(card, activity)
            val choices = descendants(card).filterIsInstance<CompoundButton>()
            assertEquals(3, choices.size)
            choices[0].performClick()
            assertTrue("The original first wrong option is selected normally", choices[0].isChecked)
            assertTrue(descendants(card).single { it.tag == MathPuzzleRenderer.CHECK_ACTION_TAG }.performClick())
        }
        lateinit var feedback: ReadingTextView
        var mainIndex = 0
        var mainOffset = 0L
        var heard = 0
        lateinit var bookmarks: Map<String, *>
        await("Submitting the real published wrong option plays actual Kokoro review", 180_000) {
            var ready = false
            host.onActivity { activity ->
                val reader = controller(activity)
                ready = field<Any?>(reader, "incorrectFeedback") != null && reader.isPlaying &&
                    field<Boolean>(reader, "playerReady") && requireNotNull(field<MediaPlayer?>(reader, "player")).currentPosition >= 250
            }
            ready
        }
        host.onActivity { activity ->
            val reader = controller(activity)
            feedback = result(mathCard(activity, 4), correct = false)
            val guide = requireNotNull(field<Any?>(reader, "incorrectFeedback"))
            val actualParts = field<List<Any>>(guide, "parts").map { field<String>(it, "text") }
            val visible = feedback.text.toString().trim().removePrefix("✗").trim()
            val expected = NarrationSegments.sentences(IncorrectFeedbackNarration.prepare(listOf(visible), NarrationLanguage.SPANISH, true))
            fun normal(values: List<String>) = values.joinToString(" ") { ManualEnglishSpeech.strip(it) }.replace(Regex("\\s+"), " ").trim()
            capture(feedback, "published-page-four-incorrect-baseline-native")
            writeEvidence("published-page-four-incorrect-baseline-native", JSONObject().put("visible", feedback.text.toString())
                .put("actualNarration", JSONArray(actualParts.map(ManualEnglishSpeech::strip)))
                .put("expectedAllVisibleNarration", JSONArray(expected.map(ManualEnglishSpeech::strip))).put("pendingRanges", ranges(feedback))
                .put("playerPositionMs", requireNotNull(field<MediaPlayer?>(reader, "player")).currentPosition))
            assertEquals("The whole visible safe review, including its heading and response label, must be spoken", normal(expected), normal(actualParts))
            assertTrue("Red narrated review must keep its remaining words gray", unread(feedback).isNotEmpty())
            assertEquals(Color.rgb(240, 166, 166), feedback.currentTextColor)
            mainIndex = field(reader, "index")
            mainOffset = field(reader, "offset")
            heard = requireNotNull(field<NarrationExerciseGate?>(reader, "playbackGate")).heardThrough
            bookmarks = resumePreferences().all.toMap()
            feedback.requestRectangleOnScreen(Rect(0, 0, feedback.width, feedback.height), true)
        }
        var mixed = false
        val samples = JSONArray()
        await("The entire wrong review progresses word by word and stops without entering the next exercise", 180_000) {
            var done = false
            host.onActivity { activity ->
                val reader = controller(activity)
                val pending = unread(feedback).sumOf { it.endExclusive - it.start }
                if (reader.isPlaying && pending in 3 until feedback.text.count { !it.isWhitespace() } - 6) {
                    if (!mixed) capture(feedback, "published-page-four-incorrect-mixed-native")
                    mixed = true
                }
                samples.put(JSONObject().put("playing", reader.isPlaying).put("pendingRanges", ranges(feedback)))
                done = !reader.isPlaying && field<MediaPlayer?>(reader, "player") == null
            }
            done
        }
        host.onActivity { activity ->
            val reader = controller(activity)
            assertTrue("Read and pending words coexist in the red review itself", mixed)
            assertTrue("Natural review completion marks all of its own text read", unread(feedback).isEmpty())
            assertEquals(mainIndex, field<Int>(reader, "index"))
            assertEquals(mainOffset, field<Long>(reader, "offset"))
            assertEquals(bookmarks, resumePreferences().all)
            assertFalse("An incorrect review does not finish the page", field<Boolean>(reader, "finished"))
            assertTrue(activity.findViewById<View>(R.id.buttonLessonBackThree).performClick())
            val pending = unread(feedback).sumOf { it.endExclusive - it.start }
            assertTrue("Rewinding the stopped wrong review restores its pending words", pending > 0)
            assertTrue(activity.findViewById<View>(R.id.buttonLessonForwardFive).performClick())
            assertTrue("Advancing changes only the review presentation", unread(feedback).sumOf { it.endExclusive - it.start } < pending)
            assertTrue(activity.findViewById<View>(R.id.buttonLessonBackThree).performClick())
            assertEquals(heard, requireNotNull(field<NarrationExerciseGate?>(reader, "playbackGate")).heardThrough)
            assertNull("Seeking a stopped review cannot retain a paused player", field<MediaPlayer?>(reader, "player"))
            assertTrue(activity.findViewById<View>(R.id.buttonLessonListen).performClick())
        }
        await("The same incorrect review can replay after its natural end and explicit seeks", 30_000) {
            var ready = false
            host.onActivity { activity -> ready = field<Any?>(controller(activity), "incorrectFeedback") != null &&
                controller(activity).isPlaying && field<Boolean>(controller(activity), "playerReady") }
            ready
        }
        host.onActivity { activity ->
            val reader = controller(activity)
            reader.pauseKeepingPosition()
            assertNull(field<MediaPlayer?>(reader, "player"))
            assertEquals(mainIndex, field<Int>(reader, "index"))
            assertEquals(mainOffset, field<Long>(reader, "offset"))
            assertEquals(heard, requireNotNull(field<NarrationExerciseGate?>(reader, "playbackGate")).heardThrough)
            assertFalse(ProgressRepository.getExerciseState(context, LESSON, PAGE, 4)?.optBoolean("correct", false) ?: false)
            assertEquals(sourceIdentity, field<JSONObject>(activity, "lessonJson").toString())
            writeEvidence("published-page-four-incorrect-replay", JSONObject().put("samples", samples)
                .put("wrongReviewCanReplayAfterItsEnd", true).put("mainSegmentRetained", mainIndex).put("mainOffsetRetained", mainOffset)
                .put("successfulFeedbackHeardThroughUnchanged", heard))
        }
    }

    @Test fun publishedPageFourCorrectFeedbackRewindsItsActualAudioAndItsWordMask() {
        val host = openPage()
        host.onActivity { activity ->
            val card = mathCard(activity, 4)
            reset(card, activity)
            val choices = descendants(card).filterIsInstance<CompoundButton>()
            assertEquals(3, choices.size)
            choices[2].performClick()
            assertTrue(choices[2].isChecked)
            assertTrue(descendants(card).single { it.tag == MathPuzzleRenderer.CHECK_ACTION_TAG }.performClick())
        }
        instrumentation.waitForIdleSync()
        lateinit var feedback: ReadingTextView
        lateinit var segments: List<Int>
        host.onActivity { activity ->
            val script = currentScript(activity)
            val sentences = NarrationSegments.sentences(script.texts)
            val plan = requireNotNull(script.readingPlan(NarrationEngine.KOKORO))
            segments = sentences.indices.filter { plan.origins[it].blockIndex == 4 && plan.origins[it].feedback }
            feedback = result(mathCard(activity, 4), correct = true)
            val key = NarrationResumePolicy.key(identity(activity), "es", NarrationEngine.KOKORO.name, "ef_dora", sentences)
            NarrationResumeStore(activity).begin(key, NarrationResumePoint(segments.first()))
            assertTrue(activity.findViewById<View>(R.id.buttonLessonListen).performClick())
        }
        await("The green explanation is genuinely playing before an in-flight backwards click", 180_000) {
            var ready = false
            host.onActivity { activity ->
                val reader = controller(activity)
                ready = reader.isPlaying && field<Int>(reader, "index") == segments.last() && field<Boolean>(reader, "playerReady") &&
                    requireNotNull(field<MediaPlayer?>(reader, "player")).currentPosition >= 1_600
            }
            ready
        }
        var activeBeforeIndex = -1
        var activeBeforePosition = -1L
        var activeBeforeCredit = -1
        host.onActivity { activity ->
            val reader = controller(activity)
            activeBeforeIndex = field(reader, "index")
            activeBeforePosition = requireNotNull(field<MediaPlayer?>(reader, "player")).currentPosition.toLong()
            activeBeforeCredit = requireNotNull(field<NarrationExerciseGate?>(reader, "playbackGate")).heardThrough
        }
        // This goes through the floating bar's native touch interception as a real
        // finger DOWN/UP, rather than invoking its click callback directly.
        nativeTapFloatingButton(host, R.id.buttonLessonBackThree)
        host.onActivity { activity ->
            val reader = controller(activity)
            val after = field<Int>(reader, "index")
            val offset = field<Long>(reader, "offset")
            capture(feedback, "published-page-four-correct-active-rewind-baseline")
            writeEvidence("published-page-four-correct-active-rewind-baseline", JSONObject().put("beforeSegment", activeBeforeIndex)
                .put("beforePositionMs", activeBeforePosition).put("afterSegment", after).put("afterOffsetMs", offset)
                .put("pendingRanges", ranges(feedback)).put("heardThrough", activeBeforeCredit)
                .put("input", "native touchscreen finger DOWN/UP at the visible floating control"))
            assertTrue("Back-three also moves backwards while the green explanation is actively playing",
                after < activeBeforeIndex || after == activeBeforeIndex && offset < activeBeforePosition)
            assertTrue("The words omitted by active rewind must become pending again", unread(feedback).isNotEmpty())
            assertEquals("A backwards click cannot supply additional successful-feedback hearing", activeBeforeCredit,
                requireNotNull(field<NarrationExerciseGate?>(reader, "playbackGate")).heardThrough)
        }
        await("The real green explanation must finish naturally before rewinding it", 180_000) {
            var done = false
            host.onActivity { done = field<Int>(controller(it), "index") > segments.last() }
            done
        }
        var rewoundIndex = -1
        var rewoundOffset = -1L
        var heard = 0
        host.onActivity { activity ->
            val reader = controller(activity)
            reader.pauseKeepingPosition()
            assertTrue("Every green word has really been heard before the rewind", unread(feedback).isEmpty())
            heard = requireNotNull(field<NarrationExerciseGate?>(reader, "playbackGate")).heardThrough
            val beforeIndex = field<Int>(reader, "index")
            val beforeOffset = field<Long>(reader, "offset")
            assertTrue(activity.findViewById<View>(R.id.buttonLessonBackThree).performClick())
            rewoundIndex = field(reader, "index")
            rewoundOffset = field(reader, "offset")
            capture(feedback, "published-page-four-correct-rewind-baseline")
            writeEvidence("published-page-four-correct-rewind-baseline", JSONObject().put("beforeSegment", beforeIndex)
                .put("beforeOffsetMs", beforeOffset).put("rewoundSegment", rewoundIndex).put("rewoundOffsetMs", rewoundOffset)
                .put("feedbackSegments", JSONArray(segments)).put("pendingRanges", ranges(feedback)).put("heardThrough", heard))
            assertTrue("The existing back-three control must return into the green explanation", rewoundIndex in segments)
            assertTrue("Its actual backwards position restores gray words", unread(feedback).isNotEmpty())
            assertNull("A backwards seek keeps MediaPlayer released until replay", field<MediaPlayer?>(reader, "player"))
            assertEquals(heard, requireNotNull(field<NarrationExerciseGate?>(reader, "playbackGate")).heardThrough)
            assertTrue(activity.findViewById<View>(R.id.buttonLessonListen).performClick())
        }
        await("The real green audio resumes its rewound segment and offset", 30_000) {
            var ready = false
            host.onActivity { activity ->
                val reader = controller(activity)
                ready = reader.isPlaying && field<Int>(reader, "index") == rewoundIndex && field<Boolean>(reader, "playerReady") &&
                    requireNotNull(field<MediaPlayer?>(reader, "player")).currentPosition >= rewoundOffset
            }
            ready
        }
        host.onActivity { activity ->
            val reader = controller(activity)
            reader.pauseKeepingPosition()
            assertNull(field<MediaPlayer?>(reader, "player"))
            assertEquals(heard, requireNotNull(field<NarrationExerciseGate?>(reader, "playbackGate")).heardThrough)
        }
    }

    @Test fun publishedWrongReviewKeepsItsOwnStoppedPositionForBackForwardAndReplay() {
        val host = openPage()
        host.onActivity { activity ->
            val card = mathCard(activity, 4)
            reset(card, activity)
            val choice = descendants(card).filterIsInstance<CompoundButton>().first()
            choice.performClick()
            assertTrue(choice.isChecked)
            assertTrue(descendants(card).single { it.tag == MathPuzzleRenderer.CHECK_ACTION_TAG }.performClick())
        }
        lateinit var originalGuide: List<String>
        var mainIndex = 0
        var mainOffset = 0L
        var heard = 0
        await("A real wrong-review player starts before observing its natural stop", 180_000) {
            var ready = false
            host.onActivity { activity ->
                val reader = controller(activity)
                ready = field<Any?>(reader, "incorrectFeedback") != null && reader.isPlaying && field<Boolean>(reader, "playerReady") &&
                    requireNotNull(field<MediaPlayer?>(reader, "player")).currentPosition >= 250
            }
            ready
        }
        host.onActivity { activity ->
            val reader = controller(activity)
            originalGuide = field<List<Any>>(requireNotNull(field<Any?>(reader, "incorrectFeedback")), "parts").map { field(it, "text") }
            mainIndex = field(reader, "index")
            mainOffset = field(reader, "offset")
            heard = requireNotNull(field<NarrationExerciseGate?>(reader, "playbackGate")).heardThrough
        }
        await("The same wrong review finishes naturally and releases its decoder", 180_000) {
            var done = false
            host.onActivity { activity -> done = !controller(activity).isPlaying && field<MediaPlayer?>(controller(activity), "player") == null }
            done
        }
        host.onActivity { activity ->
            val reader = controller(activity)
            val retainedBefore = field<Any?>(reader, "incorrectFeedback") != null
            assertTrue(activity.findViewById<View>(R.id.buttonLessonBackThree).performClick())
            val guide = field<Any?>(reader, "incorrectFeedback")
            writeEvidence("published-page-four-incorrect-natural-stop-rewind-baseline", JSONObject()
                .put("originalGuide", JSONArray(originalGuide.map(ManualEnglishSpeech::strip)))
                .put("metadataRetainedBeforeBack", retainedBefore).put("metadataActiveAfterBack", guide != null)
                .put("mainSegmentBefore", mainIndex).put("mainOffsetBefore", mainOffset)
                .put("mainSegmentAfter", field<Int>(reader, "index")).put("mainOffsetAfter", field<Long>(reader, "offset")))
            assertNotNull("A backwards click after the wrong review ends must refer to that same review", guide)
            assertEquals(originalGuide, field<List<Any>>(requireNotNull(guide), "parts").map { field<String>(it, "text") })
            assertEquals(mainIndex, field<Int>(reader, "index"))
            assertEquals(mainOffset, field<Long>(reader, "offset"))
            assertTrue(activity.findViewById<View>(R.id.buttonLessonForwardThree).performClick())
            assertTrue(activity.findViewById<View>(R.id.buttonLessonBackThree).performClick())
            assertTrue(activity.findViewById<View>(R.id.buttonLessonListen).performClick())
        }
        await("The same real guide can resume after back-forward at its completed position", 30_000) {
            var ready = false
            host.onActivity { activity -> ready = controller(activity).isPlaying && field<Boolean>(controller(activity), "playerReady") &&
                field<Any?>(controller(activity), "incorrectFeedback") != null }
            ready
        }
        host.onActivity { activity ->
            val reader = controller(activity)
            reader.pauseKeepingPosition()
            assertNull(field<MediaPlayer?>(reader, "player"))
            assertEquals(heard, requireNotNull(field<NarrationExerciseGate?>(reader, "playbackGate")).heardThrough)
            assertFalse(ProgressRepository.getExerciseState(context, LESSON, PAGE, 4)?.optBoolean("correct", false) ?: false)
        }
    }

    @Test fun publishedProgrammingWrongFeedbackAlsoReadsItsHeadingAndShadesTheSameVisibleText() {
        val lesson = "cpp_fundamentals__programas_y_ejecucion__primer_programa_y_salida"
        val sourceIdentity = LessonRepository.loadLessonJson(context, "programming", lesson).toString()
        ProgressRepository.saveCurrentPage(context, "programming", lesson, 0)
        val host = ActivityScenario.launch<LessonActivity>(Intent(context, LessonActivity::class.java)
            .putExtra("routeId", "programming").putExtra("courseId", "cpp_fundamentals")
            .putExtra("unitId", "programas_y_ejecucion").putExtra("lessonId", lesson)
            .putExtra("lessonNumber", 1).putExtra("lessonTotal", 1)).also { scenario = it }
        await("The published Programming C++ example and choices open", 15_000) {
            var ready = false
            host.onActivity { activity -> ready = activity.hasWindowFocus() && field<Int>(activity, "currentPage") == 0 &&
                descendants(content(activity)).filterIsInstance<CompoundButton>().size == 3 && !content(activity).isLayoutRequested }
            ready
        }
        host.onActivity { activity ->
            NarrationSettingsRepository.saveEnabled(activity, true)
            invoke(activity, "renderCurrentPage")
        }
        await("The original Programming controls finish native layout", 10_000) {
            var ready = false
            host.onActivity { ready = !content(it).isLayoutRequested }
            ready
        }
        host.onActivity { activity ->
            val reset = descendants(content(activity)).single { it.contentDescription == activity.getString(R.string.exercise_reset) }
            assertTrue(reset.performClick())
            val choice = descendants(content(activity)).filterIsInstance<CompoundButton>().first()
            choice.performClick()
            assertTrue(choice.isChecked)
            assertTrue(descendants(content(activity)).filterIsInstance<Button>().single {
                it.text.toString() == activity.getString(R.string.exercise_check) }.performClick())
        }
        await("The C++ wrong review uses real Kokoro", 180_000) {
            var ready = false
            host.onActivity { activity ->
                val reader = controller(activity)
                ready = reader.isPlaying && field<Any?>(reader, "incorrectFeedback") != null && field<Boolean>(reader, "playerReady") &&
                    requireNotNull(field<MediaPlayer?>(reader, "player")).currentPosition >= 250
            }
            ready
        }
        lateinit var feedback: ReadingTextView
        var heard = 0
        host.onActivity { activity ->
            val reader = controller(activity)
            feedback = descendants(content(activity)).filterIsInstance<ReadingTextView>()
                .single { it.currentTextColor == Color.rgb(240, 150, 150) }
            val guide = requireNotNull(field<Any?>(reader, "incorrectFeedback"))
            val actual = field<List<Any>>(guide, "parts").map { field<String>(it, "text") }
            val visible = feedback.text.toString().trim().removePrefix("✗").trim()
            val expected = NarrationSegments.sentences(IncorrectFeedbackNarration.prepare(listOf(visible), NarrationLanguage.SPANISH, true))
            fun normal(parts: List<String>) = parts.joinToString(" ") { ManualEnglishSpeech.strip(it) }.replace(Regex("\\s+"), " ").trim()
            writeEvidence("published-programming-incorrect-visible", JSONObject().put("visible", feedback.text.toString())
                .put("actualNarration", JSONArray(actual.map(ManualEnglishSpeech::strip))).put("pendingRanges", ranges(feedback)))
            assertEquals("C++ uses the identical visible safe text, including its incorrect heading", normal(expected), normal(actual))
            assertTrue(unread(feedback).isNotEmpty())
            heard = requireNotNull(field<NarrationExerciseGate?>(reader, "playbackGate")).heardThrough
        }
        var mixed = false
        await("The existing C++ feedback shades its actual words and stops naturally", 180_000) {
            var done = false
            host.onActivity { activity ->
                val reader = controller(activity)
                val pending = unread(feedback).sumOf { it.endExclusive - it.start }
                if (reader.isPlaying && pending in 2 until feedback.text.count { !it.isWhitespace() } - 5) {
                    if (!mixed) capture(feedback, "published-programming-incorrect-mixed-native")
                    mixed = true
                }
                done = !reader.isPlaying && field<MediaPlayer?>(reader, "player") == null
            }
            done
        }
        host.onActivity { activity ->
            val reader = controller(activity)
            assertTrue(mixed)
            assertTrue(unread(feedback).isEmpty())
            assertEquals(Color.rgb(240, 150, 150), feedback.currentTextColor)
            assertEquals(heard, requireNotNull(field<NarrationExerciseGate?>(reader, "playbackGate")).heardThrough)
            assertFalse(ProgressRepository.getExerciseState(context, lesson, 0, 4)?.optBoolean("correct", false) ?: false)
            assertEquals(sourceIdentity, field<JSONObject>(activity, "lessonJson").toString())
        }
        assertEquals(sourceIdentity, LessonRepository.loadLessonJson(context, "programming", lesson).toString())
    }

    @Test fun repeatedWrongMathReviewKeepsFormulaDecorationAndRevealsEveryAtomAtNativeTimes() {
        val published = LessonRepository.loadLessonJson(context, ROUTE, LESSON)
        val publishedIdentity = published.toString()
        val fixture = JSONObject(publishedIdentity)
        val fixtureId = "math_note_review_qa_${session.replace("-", "")}"
        fixture.put("id", fixtureId)
        val review = "Revisa la igualdad 6 × 100 = 600 y la descomposición 6000 + 300 + 8 = 6308; " +
            "cada símbolo conserva el significado de la expresión y después debes comparar los precios."
        fixture.getJSONArray("pages").getJSONObject(PAGE).getJSONArray("blocks").getJSONObject(4)
            .getJSONArray("responses").getJSONObject(0).getJSONArray("options").getJSONObject(0)
            .put("reviewFeedback", review)
        val fixtureIdentity = fixture.toString()
        val host = openPage()
        host.onActivity { activity ->
            controller(activity).stop()
            invoke(activity, "clearReadingPages")
            setField(activity, "currentLessonId", fixtureId)
            setField(activity, "lessonJson", fixture)
            listOf("pageViewCache", "pageRequiredExercises", "pageCompletedExercises").forEach {
                field<MutableMap<Int, Any>>(activity, it).clear()
            }
            invoke(activity, "renderCurrentPage")
        }
        await("The isolated copy retains the exact published controls and only replaces visible QA review text", 10_000) {
            var ready = false
            host.onActivity { activity -> ready = field<String>(activity, "currentLessonId") == fixtureId &&
                !content(activity).isLayoutRequested }
            ready
        }
        host.onActivity { activity ->
            val card = mathCard(activity, 4)
            reset(card, activity)
            val choice = descendants(card).filterIsInstance<CompoundButton>().first()
            choice.performClick()
            assertTrue(choice.isChecked)
            assertTrue(descendants(card).single { it.tag == MathPuzzleRenderer.CHECK_ACTION_TAG }.performClick())
        }
        await("The first explicit wrong submission starts its real review audio", 180_000) {
            var ready = false
            host.onActivity { activity -> ready = controller(activity).isPlaying && field<Boolean>(controller(activity), "playerReady") &&
                field<Any?>(controller(activity), "incorrectFeedback") != null &&
                requireNotNull(field<MediaPlayer?>(controller(activity), "player")).currentPosition >= 250 }
            ready
        }
        lateinit var feedback: ReadingTextView
        lateinit var atoms: List<ReadingTextRange>
        var newToken = 0L
        val expressions = listOf("6 × 100 = 600", "6000 + 300 + 8 = 6308")
        val atomGroups = buildList {
            var start = 0
            expressions.forEach { expression ->
                val count = Regex("\\d+|[×+=]").findAll(expression).count()
                add(start until start + count)
                start += count
            }
        }
        host.onActivity { activity ->
            val reader = controller(activity)
            val oldToken = field<Long>(requireNotNull(field<Any?>(reader, "incorrectFeedback")), "token")
            feedback = result(mathCard(activity, 4), correct = false)
            assertTrue(feedback.text.contains(review))
            atoms = expressions.flatMap { expression ->
                val start = feedback.text.indexOf(expression)
                assertTrue(start >= 0)
                Regex("\\d+|[×+=]").findAll(expression).map {
                    ReadingTextRange(start + it.range.first, start + it.range.last + 1)
                }.toList()
            }
            assertFormulaDecoration(feedback, expressions)
            // This is a second deliberate check of the same selected answer. It must
            // replace only its ephemeral narration token, without another attempt
            // identity, altered validator or a delayed STOPPED clearing the new mask.
            assertTrue(descendants(mathCard(activity, 4)).single { it.tag == MathPuzzleRenderer.CHECK_ACTION_TAG }.performClick())
            newToken = field(requireNotNull(field<Any?>(reader, "incorrectFeedback")), "token")
            assertTrue(newToken > oldToken)
            assertTrue("The new review keeps every later mathematical atom pending", atoms.all { atom ->
                unread(feedback).any { it.start < atom.endExclusive && it.endExclusive > atom.start }
            })
            assertFormulaDecoration(feedback, expressions)
            capture(feedback, "repeated-wrong-math-review-before-native")
        }
        val firstReads = linkedMapOf<Int, Pair<Int, Int>>()
        val samples = JSONArray()
        val diagnostics = JSONArray()
        var lastDiagnostic = 0L
        val mixedExpressions = mutableSetOf<Int>()
        var mixed = false
        await("Every mathematical number and operator follows the replacement review's real Kokoro audio", 180_000) {
            var done = false
            host.onActivity { activity ->
                val reader = controller(activity)
                val guide = field<Any?>(reader, "incorrectFeedback")
                if (guide != null && field<Long>(guide, "token") == newToken && reader.isPlaying && field<Boolean>(reader, "playerReady")) {
                    val index = field<Int>(guide, "index")
                    val position = requireNotNull(field<MediaPlayer?>(reader, "player")).currentPosition
                    val pending = unread(feedback)
                    atoms.forEachIndexed { ordinal, atom ->
                        if (pending.none { it.start < atom.endExclusive && it.endExclusive > atom.start })
                            firstReads.putIfAbsent(ordinal, index to position)
                    }
                    atomGroups.forEachIndexed { ordinal, group ->
                        val read = group.count(firstReads::containsKey)
                        if (read in 1 until group.count() && mixedExpressions.add(ordinal)) {
                            assertFormulaDecoration(feedback, expressions)
                            capture(feedback, "repeated-wrong-math-review-formula-$ordinal-mixed-native")
                        }
                    }
                    if (!mixed && firstReads.size in 1 until atoms.size) {
                        mixed = true
                        assertFormulaDecoration(feedback, expressions)
                        capture(feedback, "repeated-wrong-math-review-atoms-mixed-native")
                    }
                    samples.put(JSONObject().put("guideSegment", index).put("positionMs", position)
                        .put("readAtoms", JSONArray(firstReads.keys.toList())).put("pendingRanges", ranges(feedback)))
                }
                if (SystemClock.uptimeMillis() - lastDiagnostic >= 1_000L) {
                    lastDiagnostic = SystemClock.uptimeMillis()
                    diagnostics.put(reviewDiagnostics(activity, feedback))
                }
                // A buffering handoff, an unrelated page pause or a failed request
                // must not masquerade as completion of this replacement review.
                done = guide != null && field<Long>(guide, "token") == newToken && field<Boolean>(guide, "finished") &&
                    !reader.isPlaying && field<MediaPlayer?>(reader, "player") == null
            }
            done
        }
        host.onActivity { activity ->
            capture(feedback, "repeated-wrong-math-review-final-native")
            writeEvidence("repeated-wrong-math-review-native", JSONObject().put("qaOnlyReview", review)
                .put("expressions", JSONArray(expressions)).put("token", newToken).put("nativeSamples", samples)
                .put("eachAtomFirstRead", JSONObject(firstReads.mapKeys { it.key.toString() }.mapValues { (_, point) -> JSONArray(listOf(point.first, point.second)) }))
                .put("mixedObserved", mixed).put("mixedFormulaIndices", JSONArray(mixedExpressions.toList()))
                .put("expressionAtomGroups", JSONArray(atomGroups.map { JSONArray(it.toList()) }))
                .put("diagnosticsDuringPlayback", diagnostics)
                .put("finalDiagnostics", reviewDiagnostics(activity, feedback)))
            assertTrue("The same wrong feedback contains read and pending formula atoms", mixed)
            assertEquals("Each original number and operator reveals during its own real audio", atoms.indices.toSet(), firstReads.keys)
            assertTrue("Formula atoms do not all whiten at one whole-expression boundary", firstReads.values.distinct().size >= 8)
            atomGroups.forEachIndexed { ordinal, group ->
                assertTrue("The SAME formula must contain read and pending atoms during its own native audio: ${expressions[ordinal]}",
                    ordinal in mixedExpressions)
                assertTrue("Each formula must reveal at least three distinct native source positions: ${expressions[ordinal]}",
                    group.mapNotNull(firstReads::get).distinct().size >= 3)
            }
            assertTrue(unread(feedback).isEmpty())
            assertEquals(Color.rgb(240, 166, 166), feedback.currentTextColor)
            assertFormulaDecoration(feedback, expressions)
            assertEquals(fixtureIdentity, field<JSONObject>(activity, "lessonJson").toString())
            assertFalse(ProgressRepository.getExerciseState(context, fixtureId, PAGE, 4)?.optBoolean("correct", false) ?: false)
        }
        assertEquals(publishedIdentity, LessonRepository.loadLessonJson(context, ROUTE, LESSON).toString())
    }

    private fun reviewDiagnostics(activity: LessonActivity, view: ReadingTextView): JSONObject {
        val reader = controller(activity)
        val guide = field<Any?>(reader, "incorrectFeedback")
        val presentation = field<Any?>(activity, "incorrectReadingPresentation")
        val identity = presentation?.let { field<String?>(it, "identity") }
        val parts = guide?.let { field<List<Any>>(it, "parts") }.orEmpty()
        val output = JSONObject().put("playing", reader.isPlaying).put("playerReady", field<Boolean>(reader, "playerReady"))
            .put("reviewPresent", presentation != null).put("reviewIdentity", identity).put("viewText", view.text.toString()).put("pendingRanges", ranges(view))
            .put("pendingGlyphCount", unread(view).sumOf { it.endExclusive - it.start })
            .put("semanticRanges", JSONArray(field<List<ReadingTextRange>>(view.readingInk, "semanticBackgroundRanges")
                .map { JSONArray(listOf(it.start, it.endExclusive)) }))
            .put("guideParts", JSONArray(parts.mapIndexed { index, part ->
                JSONObject().put("segment", index).put("text", field<String>(part, "text"))
                    .put("file", field<File?>(part, "file")?.absolutePath).put("durationMs", field<Int?>(part, "duration"))
            }))
        if (guide != null) output.put("token", field<Long>(guide, "token")).put("guideIndex", field<Int>(guide, "index"))
            .put("guideOffsetMs", field<Long>(guide, "offset")).put("guideFinished", field<Boolean>(guide, "finished"))
        if (presentation != null) output.put("presentationToken", field<Long?>(presentation, "token"))
            .put("presentationPlaybackKey", field<String?>(presentation, "playbackKey"))
        val words = field<Any>(field<LessonReadingProgressModel>(activity, "readingProgress"), "words")
        val pages = field<Map<String, Any>>(words, "pages")
        output.put("registeredWordPages", JSONArray(pages.keys.toList()))
        val page = identity?.let(pages::get)
        if (page != null) {
            val mappings = field<Map<Int, NarrationWordMapping>>(page, "mapping")
            val alignments = field<Map<Int, NarrationWordAlignment>>(page, "alignments")
            output.put("mapping", JSONArray(mappings.toSortedMap().map { (segment, mapping) ->
                JSONObject().put("segment", segment).put("target", mapping.blockIndex)
                    .put("perWordTargets", mapping.targets?.let(::JSONArray))
                    .put("spokenCount", mapping.spoken.size).put("displayedCount", mapping.displayed.size)
                    .put("spokenRanges", JSONArray(mapping.spoken.map { JSONArray(listOf(it.start, it.endExclusive)) }))
                    .put("displayedRanges", JSONArray(mapping.displayed.map { JSONArray(listOf(it.start, it.endExclusive)) }))
            })).put("alignments", JSONArray(alignments.toSortedMap().map { (segment, alignment) ->
                JSONObject().put("segment", segment).put("audioSha256", alignment.audioSha256)
                    .put("sampleCount", alignment.sampleCount).put("wordCount", alignment.words.size)
                    .put("words", JSONArray(alignment.words.map {
                        JSONArray(listOf(it.start, it.endExclusive, it.startSample, it.endSample))
                    }))
            }))
        }
        return output
    }

    private fun assertFormulaDecoration(view: ReadingTextView, expressions: List<String>) {
        val ranges = field<List<ReadingTextRange>>(view.readingInk, "semanticBackgroundRanges")
        expressions.forEach { expression ->
            val start = view.text.indexOf(expression)
            assertTrue(start >= 0)
            expression.indices.filter { !expression[it].isWhitespace() }.forEach { offset ->
                assertTrue("The existing formula decoration covers '${expression[offset]}' at ${start + offset}: $ranges",
                    ranges.any { start + offset in it.start until it.endExclusive })
            }
        }
    }
    private fun nativeTapFloatingButton(host: ActivityScenario<LessonActivity>, id: Int) {
        host.onActivity { activity ->
            field<LessonNarrationControls?>(activity, "narrationControls")?.restoreVisiblePosition()
        }
        instrumentation.waitForIdleSync()
        val bounds = Rect()
        host.onActivity { activity ->
            val button = activity.findViewById<View>(id)
            assertTrue("The floating control must really be visible for injected touch", button.isShown && button.getGlobalVisibleRect(bounds))
            assertTrue(bounds.width() > 0 && bounds.height() > 0)
        }
        val properties = MotionEvent.PointerProperties().apply { this.id = 0; toolType = MotionEvent.TOOL_TYPE_FINGER }
        val coords = MotionEvent.PointerCoords().apply { x = bounds.exactCenterX(); y = bounds.exactCenterY(); pressure = 1f; size = .2f }
        val downTime = SystemClock.uptimeMillis()
        fun event(action: Int): MotionEvent = MotionEvent.obtain(downTime, SystemClock.uptimeMillis(), action,
            1, arrayOf(properties), arrayOf(coords), 0, 0, 1f, 1f, 0, 0, InputDevice.SOURCE_TOUCHSCREEN, 0)
        val down = event(MotionEvent.ACTION_DOWN)
        try { instrumentation.sendPointerSync(down) } finally { down.recycle() }
        SystemClock.sleep(30L)
        coords.pressure = 0f
        val up = event(MotionEvent.ACTION_UP)
        try { instrumentation.sendPointerSync(up) } finally { up.recycle() }
        instrumentation.waitForIdleSync()
    }
    private fun mathCard(activity: LessonActivity, block: Int): ViewGroup {
        val prompt = field<JSONObject>(activity, "lessonJson").getJSONArray("pages").getJSONObject(PAGE)
            .getJSONArray("blocks").getJSONObject(block).getString("prompt")
        return descendants(content(activity)).filterIsInstance<ViewGroup>().single { card ->
            card.tag == MathPuzzleRenderer.INTERACTION_TAG && descendants(card).filterIsInstance<TextView>()
                .any { it.text.toString() == prompt } }
    }
    private fun result(card: ViewGroup, correct: Boolean): ReadingTextView = descendants(card).filterIsInstance<ReadingTextView>()
        .single { it.currentTextColor == if (correct) Color.rgb(180, 220, 190) else Color.rgb(240, 166, 166) }
    private fun reset(card: ViewGroup, activity: LessonActivity) {
        assertTrue(descendants(card).single { it.contentDescription == activity.getString(R.string.math_ui_restart) }.performClick())
    }
    private fun openPage(): ActivityScenario<LessonActivity> {
        ProgressRepository.saveCurrentPage(context, ROUTE, LESSON, PAGE)
        val host = ActivityScenario.launch<LessonActivity>(Intent(context, LessonActivity::class.java)
            .putExtra("routeId", ROUTE).putExtra("courseId", "math_course_01")
            .putExtra("unitId", "numeros_y_operaciones").putExtra("lessonId", LESSON)
            .putExtra("lessonNumber", 1).putExtra("lessonTotal", 1)).also { scenario = it }
        await("The exact published mathematics page four opens", 15_000) {
            var ready = false
            host.onActivity { activity -> ready = activity.hasWindowFocus() && field<Int>(activity, "currentPage") == PAGE &&
                descendants(content(activity)).filterIsInstance<TextView>().any { it.text.startsWith("Nota\n\nAl escribir") } &&
                !content(activity).isLayoutRequested }
            ready
        }
        host.onActivity { activity ->
            NarrationSettingsRepository.saveEnabled(activity, true)
            invoke(activity, "renderCurrentPage")
        }
        await("Native mathematics note and controls settle", 10_000) {
            var ready = false
            host.onActivity { ready = content(it).width > 0 && !content(it).isLayoutRequested }
            ready
        }
        return host
    }
    private fun currentScript(activity: LessonActivity): LessonNarrationScript = LessonActivity::class.java
        .getDeclaredMethod("narrationScript", NarrationLanguage::class.java).apply { isAccessible = true }
        .invoke(activity, NarrationLanguage.SPANISH) as LessonNarrationScript
    private fun unread(view: TextView): List<ReadingTextRange> = field((view as ReadingTextHost).readingInk, "unreadRanges")
    private fun ranges(view: TextView) = JSONArray(unread(view).map { JSONArray(listOf(it.start, it.endExclusive)) })
    private fun controller(activity: LessonActivity): LessonNarrationController = field(activity, "narrationController")
    private fun content(activity: LessonActivity): ViewGroup = activity.findViewById(R.id.lessonContent)
    private fun identity(activity: LessonActivity) = "${field<String>(activity, "currentRouteId")}/${field<String>(activity, "currentLessonId")}/${field<Int>(activity, "currentPage")}"
    private fun descendants(view: View): List<View> = listOf(view) + if (view is ViewGroup)
        (0 until view.childCount).flatMap { descendants(view.getChildAt(it)) } else emptyList()
    @Suppress("UNCHECKED_CAST") private fun <T> field(instance: Any, name: String): T = instance.javaClass
        .getDeclaredField(name).apply { isAccessible = true }.get(instance) as T
    private fun setField(instance: Any, name: String, value: Any) = instance.javaClass.getDeclaredField(name)
        .apply { isAccessible = true }.set(instance, value)
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
    private fun evidence() = File(requireNotNull(context.getExternalFilesDir(null)), "note-feedback-reading-qa/$session").apply { mkdirs() }
    private fun resumePreferences() = context.getSharedPreferences("lesson_narration_resume_bounded_v1", Context.MODE_PRIVATE)
    private fun preferenceFiles() = File(context.applicationInfo.dataDir, "shared_prefs").listFiles().orEmpty()
        .filter { it.name.endsWith(".xml") }.map { it.name.removeSuffix(".xml") }.toSet()

    companion object {
        private val QA_PACKAGES = setOf("com.learngraphicsprogramming.app.curricularqa", "com.learngraphicsprogramming.app.realdownloadsqa")
        private val KNOWN_PREFERENCES = setOf("learning_progress", "navigation_state", "lesson_narration_settings", "app_settings",
            "appearance_settings", "ui_language_settings", "premium_state", "lesson_text_size_settings", "lesson_narration_resume_bounded_v1")
        private const val ROUTE = "math_physics"
        private const val LESSON = "math_course_01__numeros_y_operaciones__magnitudes_y_numeracion"
        private const val PAGE = 3
    }
}
