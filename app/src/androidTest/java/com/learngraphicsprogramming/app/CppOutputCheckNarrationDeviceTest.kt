package com.learngraphicsprogramming.app

import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.media.MediaMetadataRetriever
import android.media.MediaPlayer
import android.os.SystemClock
import android.view.View
import android.widget.TextView
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.learngraphicsprogramming.app.narration.*
import org.json.JSONArray
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.security.MessageDigest

/** Four actual compiler checks, narrated by the existing transient, unscored lane. */
@RunWith(AndroidJUnit4::class)
class CppOutputCheckNarrationDeviceTest : CppOutputCheckQaSupport() {
    private val readers = mutableListOf<LessonNarrationController>()
    private val durable = linkedMapOf<String, ByteArray?>()
    private var originalLocale = ""
    private var audioPrepared = false

    @Before fun prepareExistingRealKokoroWithoutReplacingItsResources() {
        originalLocale = UiLanguageSettings.selectedTag(context)
        listOf(KokoroPagePreparationQueue.FILE_NAME, NarrationAudioPlanner.FILE_NAME).forEach { name ->
            durable[name] = File(context.filesDir, name).takeIf(File::isFile)?.readBytes()
        }
        audioPrepared = true
        UiLanguageSettings.select(context, "es")
        NarrationSettingsRepository.saveEngine(context, NarrationEngine.KOKORO)
        NarrationSettingsRepository.saveLanguage(context, NarrationLanguage.SPANISH)
        NarrationSettingsRepository.saveKokoroVoice(context, NarrationLanguage.SPANISH.defaultVoice)
        NarrationSettingsRepository.saveRate(context, 1f)
        NarrationSettingsRepository.saveControlsVisible(context, true)
    }

    @After fun closeNativeWorkersBeforeTheParentRestoresPreferences() {
        if (!audioPrepared) return
        instrumentation.runOnMainSync {
            NarrationSettingsRepository.saveEnabled(context, false)
            readers.forEach { it.close() }
        }
        awaitWorkerStop("All test readers close", 30_000L) { readers.none { field<Thread>(it, "workerThread").isAlive } }
        context.stopService(Intent(context, KokoroContinuousNarrationService::class.java))
        awaitWorkerStop("Background synthesis stops before restoring its scheduler", 30_000L) {
            Thread.getAllStackTraces().keys.none { it.isAlive && it.name == "KokoroOfflineNarration" }
        }
        durable.forEach { (name, bytes) -> File(context.filesDir, name).let { if (bytes == null) it.delete() else it.writeBytes(bytes) } }
        synchronized(KokoroPagePreparationQueue) {
            listOf("directory", "queue").forEach { name -> KokoroPagePreparationQueue::class.java.getDeclaredField(name)
                .apply { isAccessible = true }.set(KokoroPagePreparationQueue, null) }
        }
        synchronized(NarrationAudioPlanner) {
            field<MutableMap<File, Any>>(NarrationAudioPlanner, "stores").remove(File(context.filesDir, NarrationAudioPlanner.FILE_NAME).canonicalFile)
        }
        instrumentation.runOnMainSync { UiLanguageSettings.select(context, originalLocale) }
    }

    @Test fun naturallyFinishedCorrectVerdictReturnsToPlayingPageAfterAResourceReleasingPause() {
        verifyCorrectVerdictReturnsToItsSource(initiallyPaused = false, pauseVerdict = true)
    }

    @Test fun naturallyFinishedCorrectVerdictLeavesAPausedPagePausedButTheNextPlayCanResumeIt() {
        verifyCorrectVerdictReturnsToItsSource(initiallyPaused = true, pauseVerdict = false)
    }

    @Test fun alreadyFinishedNativeSourceNeverRestartsAfterCorrectVerdictOrPlay() {
        val location = CppOutputCheckDeviceTest.LOCATIONS.first()
        val lesson = source(location.path)
        val authored = lesson.getJSONArray("pages").getJSONObject(4).getJSONArray("blocks")
        val fixture = fixture(location, authored.getJSONObject(2).getJSONObject("outputCheck"))
        // A short runtime page reuses this authored heading to reach a real audio
        // completion without pretending that either neighboring puzzle was answered.
        val sourceTexts = listOf(authored.getJSONObject(0).getString("text"))
        val identity = "finished-native-source-$session"
        val resume = context.getSharedPreferences("lesson_narration_resume_bounded_v1", Context.MODE_PRIVATE)
        assertTrue(resume.edit().clear().commit())
        val store = NarrationResumeStore(context)
        val olderFinished = "finished-native-older-finished-$session"
        val olderPaused = "finished-native-older-paused-$session"
        val finishedPoint = NarrationResumePoint(1, 2345, finished = true)
        val pausedPoint = NarrationResumePoint(2, 678)
        val finishedOwner = store.begin(olderFinished, finishedPoint)
        store.save(olderFinished, finishedOwner, finishedPoint)
        assertEquals(finishedPoint, store.restore(olderFinished, finishedPoint.segment + 1))
        store.begin(olderPaused, pausedPoint)
        val events = mutableListOf<NarrationPlaybackProgress>()
        val errors = mutableListOf<String>()
        var completedIndex = -1
        var completedOffset = -1L
        var completeBookmarks: Map<String, *> = emptyMap<String, Any?>()
        var sourceRecording: File? = null
        var sourceBytes: ByteArray? = null
        lateinit var controller: LessonNarrationController
        withFixture(fixture) { host ->
            host.onActivity {
                NarrationSettingsRepository.saveEnabled(context, true)
                controller = LessonNarrationController(context, {}, { errors += it }, { events += it })
                readers += controller
                controller.toggle(sourceTexts, NarrationLanguage.SPANISH, identity)
            }
            var sourceAudiblyAdvanced = false
            await(host, "The original runtime page finishes actual Kokoro audio naturally", 180_000L) {
                val player = field<MediaPlayer?>(controller, "player")
                if (controller.isPlaying && field<Boolean>(controller, "playerReady") &&
                    player != null && player.isPlaying && player.currentPosition > 30) {
                    val part = field<List<Any>>(controller, "segments").single()
                    assertFalse(field<Boolean>(part, "usedAndroidTts"))
                    sourceAudiblyAdvanced = true
                }
                sourceAudiblyAdvanced && field<Boolean>(controller, "finished") && !controller.isPlaying
            }
            host.onActivity { activity ->
                assertTrue(errors.toString(), errors.isEmpty())
                assertNull(field<MediaPlayer?>(controller, "player"))
                completedIndex = field(controller, "index")
                completedOffset = field(controller, "offset")
                assertTrue(completedOffset > 0L)
                sourceRecording = requireNotNull(field<File?>(field<List<Any>>(controller, "segments").single(), "file"))
                sourceBytes = requireNotNull(sourceRecording).readBytes()
                completeBookmarks = resume.all.toMap()
                assertEquals(3, (0 until 3).mapNotNull { resume.getString("key_$it", null) }.size)
                val verdict = IncorrectFeedbackNarration.prepare(
                    listOf(activity.getString(R.string.lesson_correct_feedback)), NarrationLanguage.SPANISH, true)
                controller.readUnscoredFeedback(verdict, NarrationLanguage.SPANISH, identity, 2,
                    sourceTexts, emptyList(), emptySet(), continueAfterCompletion = true)
            }
            var guide: Any? = null
            var verdictAudiblyAdvanced = false
            await(host, "The correct verdict itself also finishes actual Kokoro audio", 180_000L) {
                val active = field<Any?>(controller, "incorrectFeedback") ?: return@await false
                if (guide == null) {
                    guide = active
                    assertFalse(field<Boolean>(active, "wasPlaying"))
                    assertFalse(field<Boolean>(active, "wasWaiting"))
                }
                assertSame(guide, active)
                val player = field<MediaPlayer?>(controller, "player")
                if (controller.isPlaying && field<Boolean>(controller, "playerReady") &&
                    player != null && player.isPlaying && player.currentPosition > 30) {
                    val part = field<List<Any>>(active, "parts")[field(active, "index")]
                    assertFalse(field<Boolean>(part, "usedAndroidTts"))
                    verdictAudiblyAdvanced = true
                }
                verdictAudiblyAdvanced && field<Boolean>(active, "finished") && !controller.isPlaying
            }
            host.onActivity {
                assertTrue(errors.toString(), errors.isEmpty())
                assertTrue(field<Boolean>(controller, "finished"))
                assertFalse(controller.isPlaying)
                assertNull(field<MediaPlayer?>(controller, "player"))
                assertEquals(completedIndex, field<Int>(controller, "index"))
                assertEquals(completedOffset, field<Long>(controller, "offset"))
                assertEquals(completeBookmarks, resume.all)
                val priorEvents = events.size
                controller.toggle(sourceTexts, NarrationLanguage.SPANISH, identity)
                assertNull("Play releases the completed unscored guide metadata", field<Any?>(controller, "incorrectFeedback"))
                assertTrue("Play must not restart an already completed source page", field<Boolean>(controller, "finished"))
                assertFalse(controller.isPlaying)
                assertNull(field<MediaPlayer?>(controller, "player"))
                assertEquals(completedIndex, field<Int>(controller, "index"))
                assertEquals(completedOffset, field<Long>(controller, "offset"))
                assertEquals(completeBookmarks, resume.all)
                assertEquals(finishedPoint, store.restore(olderFinished, finishedPoint.segment + 1))
                assertEquals(pausedPoint, store.restore(olderPaused, pausedPoint.segment + 1))
                assertArrayEquals("The source recording is reused unchanged", sourceBytes, requireNotNull(sourceRecording).readBytes())
                assertFalse(events.drop(priorEvents).any { it.phase == NarrationPlaybackPhase.RESTARTED })
                assertNull(ProgressRepository.getExerciseState(context, fixture.id, 4, 3))
                assertNull(ProgressRepository.getExerciseState(context, fixture.id, 4, 4))
            }
        }
    }

    private fun verifyCorrectVerdictReturnsToItsSource(initiallyPaused: Boolean, pauseVerdict: Boolean) {
        PremiumRepository.setVerifiedFree(context)
        assertFalse(AttemptRepository.getState(context, "programming").unlimited)
        val hearts = context.getSharedPreferences("attempt_reserve_seconds_test", Context.MODE_PRIVATE).all.toMap()
        val location = CppOutputCheckDeviceTest.LOCATIONS.first()
        val authored = source(location.path).getJSONArray("pages").getJSONObject(4).getJSONArray("blocks").getJSONObject(2)
        val fixture = fixture(location, authored.getJSONObject("outputCheck"))
        val correctSource = source(location.path).getJSONArray("pages").getJSONObject(5)
            .getJSONArray("blocks").getJSONObject(2).getString("code")
        val resume = context.getSharedPreferences("lesson_narration_resume_bounded_v1", Context.MODE_PRIVATE)
        assertTrue(resume.edit().clear().commit())
        val store = NarrationResumeStore(context)
        val olderFinished = "verdict-return-finished-$session"
        val olderPaused = "verdict-return-paused-$session"
        val finishedPoint = NarrationResumePoint(1, 2345, finished = true)
        val pausedPoint = NarrationResumePoint(2, 678)
        val finishedOwner = store.begin(olderFinished, finishedPoint)
        store.save(olderFinished, finishedOwner, finishedPoint)
        assertEquals(finishedPoint, store.restore(olderFinished, finishedPoint.segment + 1))
        store.begin(olderPaused, pausedPoint)
        withFixture(fixture) { host ->
            host.onActivity { activity ->
                NarrationSettingsRepository.saveEnabled(context, true)
                invoke(activity, "renderCurrentPage")
                readers += reader(activity)
                editor(activity).setText(correctSource)
                assertTrue(activity.findViewById<View>(R.id.buttonLessonListen).performClick())
            }
            await(host, "The authored paragraph really plays before the correct laboratory run", 180_000L) { activity ->
                val controller = reader(activity)
                val player = field<MediaPlayer?>(controller, "player")
                controller.isPlaying && field<Int>(controller, "index") >= 1 &&
                    field<Boolean>(controller, "playerReady") && player != null && player.isPlaying &&
                    player.duration > 4000 && player.currentPosition in 301..(player.duration - 1000)
            }
            host.onActivity { activity ->
                if (initiallyPaused) {
                    reader(activity).pauseKeepingPosition()
                    assertNull("A deliberately paused source retains its position without a decoder",
                        field<MediaPlayer?>(reader(activity), "player"))
                }
                runButton(activity).performClick()
                assertFalse(runButton(activity).isEnabled)
            }

            var guide: Any? = null
            var sourceIndex = -1
            var sourceOffset = -1L
            var gate: NarrationExerciseGate? = null
            var heard = -1
            var sourceParts: List<Any> = emptyList()
            var bookmarkKeys: List<String> = emptyList()
            var pausedGuide = !pauseVerdict
            var pausedOffset = -1L
            var pausedAudio: ByteArray? = null
            var guideFile: File? = null
            await(host, "The fresh correct verdict completes its real Kokoro audio naturally", 180_000L) { activity ->
                val controller = reader(activity)
                val currentGuide = field<Any?>(controller, "incorrectFeedback")
                if (guide == null && currentGuide != null) {
                    guide = currentGuide
                    assertTrue(field<Boolean>(currentGuide, "unscored"))
                    assertEquals("The test must distinguish an active source from a deliberate pause", !initiallyPaused,
                        field<Boolean>(currentGuide, "wasPlaying"))
                    assertFalse(field<Boolean>(currentGuide, "wasWaiting"))
                    assertEquals("Caja recibida", (tagged(activity, "cpp_stdout") as TextView).text.toString())
                    assertEquals(View.VISIBLE, result(activity).visibility)
                    assertTrue(runButton(activity).isEnabled)
                    sourceIndex = field(controller, "index")
                    sourceOffset = field(controller, "offset")
                    sourceParts = field(controller, "segments")
                    gate = field(controller, "playbackGate")
                    heard = requireNotNull(gate).heardThrough
                    bookmarkKeys = (0 until 3).mapNotNull { resume.getString("key_$it", null) }
                    assertEquals("A verdict does not acquire a fourth bookmark", 3, bookmarkKeys.size)
                }
                val active = guide ?: return@await false
                val parts = field<List<Any>>(active, "parts")
                val partIndex = field<Int>(active, "index")
                val player = field<MediaPlayer?>(controller, "player")
                if (!pausedGuide && currentGuide === active && controller.isPlaying &&
                    field<Boolean>(controller, "playerReady") && player != null && player.isPlaying &&
                    player.duration > 1200 && player.currentPosition in 301..(player.duration - 600)) {
                    val part = parts[partIndex]
                    assertFalse("The verdict really uses installed Kokoro", field<Boolean>(part, "usedAndroidTts"))
                    guideFile = requireNotNull(field<File?>(part, "file"))
                    pausedAudio = requireNotNull(guideFile).readBytes()
                    controller.pauseKeepingPosition()
                    pausedOffset = field(active, "offset")
                    assertTrue(pausedOffset > 0L)
                    assertNull("Pausing a verdict releases MediaPlayer", field<MediaPlayer?>(controller, "player"))
                    assertTrue(activity.findViewById<View>(R.id.buttonLessonListen).performClick())
                    assertArrayEquals("The exact same generated recording is reused on resume", pausedAudio,
                        requireNotNull(guideFile).readBytes())
                    pausedGuide = true
                }
                if (currentGuide === active) {
                    assertSame(sourceParts, field<List<Any>>(controller, "segments"))
                    assertEquals(sourceIndex, field<Int>(controller, "index"))
                    assertEquals(sourceOffset, field<Long>(controller, "offset"))
                    assertSame(gate, field<NarrationExerciseGate?>(controller, "playbackGate"))
                    assertEquals(heard, requireNotNull(gate).heardThrough)
                }
                field<Boolean>(active, "finished")
            }
            assertTrue("The resource-releasing pause/resume branch was actually exercised", pausedGuide)
            if (pauseVerdict) assertTrue(pausedOffset > 0L && pausedAudio != null && guideFile?.isFile == true)
            if (initiallyPaused) host.onActivity { activity ->
                val controller = reader(activity)
                assertFalse("Completing a verdict must respect a manually paused page", controller.isPlaying)
                assertNull(field<MediaPlayer?>(controller, "player"))
                assertEquals(sourceIndex, field<Int>(controller, "index"))
                assertEquals(sourceOffset, field<Long>(controller, "offset"))
                assertTrue(activity.findViewById<View>(R.id.buttonLessonListen).performClick())
            }
            await(host, "After the full correct verdict, the source page really resumes instead of being trapped", 30_000L) { activity ->
                val controller = reader(activity)
                val player = field<MediaPlayer?>(controller, "player")
                field<Any?>(controller, "incorrectFeedback") == null && controller.isPlaying &&
                    field<Boolean>(controller, "playerReady") && player != null && player.isPlaying &&
                    field<Int>(controller, "index") == sourceIndex && player.currentPosition > sourceOffset + 80L
            }
            host.onActivity { activity ->
                val controller = reader(activity)
                assertSame(sourceParts, field<List<Any>>(controller, "segments"))
                assertSame(gate, field<NarrationExerciseGate?>(controller, "playbackGate"))
                assertEquals("Unscored narration never accredits a puzzle's successful explanation", heard,
                    requireNotNull(gate).heardThrough)
                assertFalse("The neighboring unanswered puzzle keeps its curricular boundary",
                    requireNotNull(gate).canPlay(sourceParts.lastIndex))
                assertEquals(bookmarkKeys, (0 until 3).mapNotNull { resume.getString("key_$it", null) })
                assertEquals(finishedPoint, store.restore(olderFinished, finishedPoint.segment + 1))
                assertEquals(pausedPoint, store.restore(olderPaused, pausedPoint.segment + 1))
                assertTrue("A completely narrated verdict must remain visually read after source continuation",
                    unread(result(activity)).isEmpty())
                assertEquals(hearts, context.getSharedPreferences("attempt_reserve_seconds_test", Context.MODE_PRIVATE).all)
                assertEquals(0, field<Int>(activity, "completedExercises"))
                assertNull(ProgressRepository.getExerciseState(context, fixture.id, 4, 3))
                assertNull(ProgressRepository.getExerciseState(context, fixture.id, 4, 4))
                controller.pauseKeepingPosition()
                assertNull(field<MediaPlayer?>(controller, "player"))
            }
        }
    }

    @Test fun allFourVisibleVerdictsReallyPlayInFullWithoutCreditingTheNeighboringPrediction() {
        val cases = listOf(
            JSONObject().put("standardInput", "plain_correct").put("outputs", JSONArray(listOf("Caja recibida"))),
            JSONObject().put("standardInput", "plain_wrong").put("outputs", JSONArray(listOf("Caja recibida"))),
            JSONObject().put("standardInput", "explained").put("outputs", JSONArray(listOf("Caja recibida")))
                .put("explanation", CppOutputCheckDeviceTest.EXPLANATION),
            JSONObject().put("standardInput", "guided").put("outputs", JSONArray(listOf("Caja recibida")))
                .put("reviewFeedback", JSONArray().put(JSONObject().put("outputs", JSONArray(listOf("Zona segura")))
                    .put("text", CppOutputCheckDeviceTest.GUIDE))))
        val fixture = fixture(CppOutputCheckDeviceTest.LOCATIONS.first(), JSONObject().put("cases", JSONArray(cases)))
        val observations = JSONArray()
        val resume = context.getSharedPreferences("lesson_narration_resume_bounded_v1", Context.MODE_PRIVATE)
        assertTrue(resume.edit().clear().commit())
        val store = NarrationResumeStore(context)
        val olderA = "output-check-older-finished-$session"
        val olderB = "output-check-older-paused-$session"
        val olderFinishedPoint = NarrationResumePoint(1, 2345, finished = true)
        val olderFinishedOwner = store.begin(olderA, olderFinishedPoint)
        store.save(olderA, olderFinishedOwner, olderFinishedPoint)
        assertEquals(olderFinishedPoint, store.restore(olderA, olderFinishedPoint.segment + 1))
        store.begin(olderB, NarrationResumePoint(2, 678))
        withFixture(fixture) { host ->
            host.onActivity { activity ->
                NarrationSettingsRepository.saveEnabled(context, true)
                invoke(activity, "renderCurrentPage")
                readers += reader(activity)
                assertTrue(activity.findViewById<View>(R.id.buttonLessonListen).performClick())
            }
            await(host, "The existing page audio really starts before checks", 180_000L) { activity ->
                val controller = reader(activity)
                val player = field<MediaPlayer?>(controller, "player")
                controller.isPlaying && field<Boolean>(controller, "playerReady") && player != null && player.currentPosition > 300
            }
            var sourceIndex = 0
            var sourceOffset = 0L
            var heard = 0
            var originalBookmarks: Map<String, *> = emptyMap<String, Any?>()
            lateinit var originalParts: List<Any>
            host.onActivity { activity ->
                val controller = reader(activity)
                controller.pauseKeepingPosition()
                assertNull("Source pause releases the decoder", field<MediaPlayer?>(controller, "player"))
                sourceIndex = field(controller, "index")
                sourceOffset = field(controller, "offset")
                originalParts = field(controller, "segments")
                heard = field<NarrationExerciseGate?>(controller, "playbackGate")!!.heardThrough
                originalBookmarks = resume.all.toMap()
                assertEquals(3, (0 until 3).mapNotNull { resume.getString("key_$it", null) }.size)
            }
            val runs = listOf("Caja recibida", "Zona inicial", "Caja recibida", "Zona segura")
            for (index in runs.indices) {
                val played = linkedMapOf<Int, JSONObject>()
                val masks = linkedMapOf<Int, MutableSet<String>>()
                var transportChecked = index != 3
                host.onActivity { activity ->
                    editor(activity).setText(CppOutputCheckDeviceTest.program(runs[index]))
                    input(activity).setText(cases[index].getString("standardInput"))
                    assertSingleRunButton(activity, checking = true)
                    assertEquals(View.GONE, result(activity).visibility)
                    tagged(activity, "cpp_run").performClick()
                }
                await(host, "The exact checked source executes on native Clang", 120_000L) { activity -> tagged(activity, "cpp_run").isEnabled }
                var shown = ""
                host.onActivity { activity ->
                    assertEquals(runs[index], (tagged(activity, "cpp_stdout") as TextView).text.toString())
                    assertTrue(runButton(activity).isEnabled)
                    assertEquals("A single run automatically presents its fresh verdict", View.VISIBLE, result(activity).visibility)
                    shown = result(activity).text.toString()
                    val extra = when (index) { 2 -> CppOutputCheckDeviceTest.EXPLANATION; 3 -> CppOutputCheckDeviceTest.GUIDE; else -> null }
                    assertEquals(expectedVerdict(activity, index == 0 || index == 2, extra), shown)
                }
                val expectedParts = NarrationSegments.sentences(IncorrectFeedbackNarration.prepare(listOf(shown), NarrationLanguage.SPANISH, true))
                await(host, "Every visible verdict word naturally plays and stops in the unscored lane", 180_000L) { activity ->
                    val controller = reader(activity)
                    val feedback = field<Any?>(controller, "incorrectFeedback") ?: return@await false
                    val parts = field<List<Any>>(feedback, "parts")
                    assertEquals("Narrate exactly the visible verdict, never an internal expected output", expectedParts, parts.map { field<String>(it, "text") })
                    assertSame(originalParts, field<List<Any>>(controller, "segments"))
                    assertEquals(sourceIndex, field<Int>(controller, "index"))
                    assertEquals(sourceOffset, field<Long>(controller, "offset"))
                    val gate = requireNotNull(field<NarrationExerciseGate?>(controller, "playbackGate"))
                    assertEquals("Unscored output checks never credit the prediction feedback gate", heard, gate.heardThrough)
                    assertFalse(gate.canPlay(originalParts.lastIndex))
                    observe(activity, controller, parts, field(feedback, "index"), index, played, masks)
                    val player = field<MediaPlayer?>(controller, "player")
                    if (!transportChecked && controller.isPlaying && field<Boolean>(controller, "playerReady") && player != null &&
                        player.duration > 2200 && player.currentPosition in 1401..(player.duration - 500)) {
                        val segment = field<Int>(feedback, "index")
                        val file = requireNotNull(field<File?>(parts[segment], "file"))
                        val before = file.readBytes()
                        controller.pauseKeepingPosition()
                        val offset = field<Long>(feedback, "offset")
                        assertNull("Unscored verdict pauses release MediaPlayer", field<MediaPlayer?>(controller, "player"))
                        controller.seekBy(-1)
                        assertEquals(offset - 1000L, field<Long>(feedback, "offset"))
                        controller.seekBy(1)
                        assertEquals(offset, field<Long>(feedback, "offset"))
                        assertTrue(activity.findViewById<View>(R.id.buttonLessonListen).performClick())
                        assertArrayEquals("Resume reuses the existing recording", before, file.readBytes())
                        transportChecked = true
                    }
                    if (!controller.isPlaying && field<Boolean>(feedback, "finished")) {
                        assertEquals("All full visible segments were really played", parts.indices.toSet(), played.keys)
                        assertTrue("Every segment has observed native progress", played.values.all { it.getInt("advancingSamples") > 0 })
                        assertTrue("The explicit pause/resume case actually ran", transportChecked)
                        if (index >= 2) assertTrue("The long visible text progresses inside a single actual sentence", masks.values.any { it.size >= 3 })
                        assertNull(field<MediaPlayer?>(controller, "player"))
                        assertFalse("An unscored verdict is not page completion", field<Boolean>(controller, "finished"))
                        assertEquals("No additional or modified page bookmark", originalBookmarks, resume.all)
                        assertTrue("The complete displayed verdict is read", unread(result(activity)).isEmpty())
                        assertNull(ProgressRepository.getExerciseState(context, fixture.id, 4, 3))
                        capture(activity, "verdict-$index-complete")
                        true
                    } else false
                }
                var replayedAfterCompletion = false
                if (index == 3) {
                    lateinit var completedGuide: Any
                    lateinit var recording: File
                    lateinit var recordingBytes: ByteArray
                    var replayIndex = -1
                    var replayOffset = -1L
                    host.onActivity { activity ->
                        val controller = reader(activity)
                        completedGuide = requireNotNull(field<Any?>(controller, "incorrectFeedback"))
                        assertTrue(field<Boolean>(completedGuide, "finished"))
                        replayIndex = field(completedGuide, "index")
                        val end = field<Long>(completedGuide, "offset")
                        assertTrue("The narrated red guide has at least one second to replay", end > 1000L)
                        val part = field<List<Any>>(completedGuide, "parts")[replayIndex]
                        recording = requireNotNull(field<File?>(part, "file"))
                        recordingBytes = recording.readBytes()
                        controller.seekBy(-1)
                        replayOffset = end - 1000L
                        assertSame(completedGuide, field<Any?>(controller, "incorrectFeedback"))
                        assertFalse(field<Boolean>(completedGuide, "finished"))
                        assertEquals(replayIndex, field<Int>(completedGuide, "index"))
                        assertEquals(replayOffset, field<Long>(completedGuide, "offset"))
                        assertNull("Rewinding stopped feedback does not keep a player alive", field<MediaPlayer?>(controller, "player"))
                        assertTrue(activity.findViewById<View>(R.id.buttonLessonListen).performClick())
                    }
                    await(host, "The already completed incorrect guide really plays again after Back", 30_000L) { activity ->
                        val controller = reader(activity)
                        val player = field<MediaPlayer?>(controller, "player")
                        assertSame(completedGuide, field<Any?>(controller, "incorrectFeedback"))
                        controller.isPlaying && field<Boolean>(controller, "playerReady") && player != null && player.isPlaying &&
                            field<Int>(completedGuide, "index") == replayIndex && player.currentPosition > replayOffset + 40L
                    }
                    await(host, "Replayed incorrect feedback stops naturally again", 30_000L) { activity ->
                        val controller = reader(activity)
                        field<Boolean>(completedGuide, "finished") && !controller.isPlaying
                    }
                    host.onActivity { activity ->
                        val controller = reader(activity)
                        assertArrayEquals("Replaying reuses the same audio without regeneration", recordingBytes, recording.readBytes())
                        assertNull(field<MediaPlayer?>(controller, "player"))
                        assertEquals(sourceIndex, field<Int>(controller, "index"))
                        assertEquals(sourceOffset, field<Long>(controller, "offset"))
                        assertEquals(heard, requireNotNull(field<NarrationExerciseGate?>(controller, "playbackGate")).heardThrough)
                        assertEquals(originalBookmarks, resume.all)
                        assertEquals(olderFinishedPoint, store.restore(olderA, olderFinishedPoint.segment + 1))
                        assertTrue(unread(result(activity)).isEmpty())
                        assertNull(ProgressRepository.getExerciseState(context, fixture.id, 4, 3))
                    }
                    replayedAfterCompletion = true
                }
                observations.put(JSONObject().put("case", index).put("visibleResult", shown).put("expectedSpokenParts", JSONArray(expectedParts))
                    .put("audio", JSONArray(played.values.toList())).put("maskSamples", JSONObject().apply { masks.forEach { (part, ranges) -> put(part.toString(), JSONArray(ranges.toList())) } })
                    .put("pauseReleasedPlayerAndExactSeekRestored", transportChecked && index == 3)
                    .put("incorrectGuideReplayedAfterNaturalCompletion", replayedAfterCompletion))
            }
            host.onActivity { activity -> reader(activity).pauseKeepingPosition() }
        }
        savePlaybackEvidence("four-real-kokoro-verdicts", JSONObject().put("voice", NarrationLanguage.SPANISH.defaultVoice)
            .put("verdicts", observations).put("evidenceLevel", "Actual LDPlayer Kokoro WAV playback and native word masks; no human listening assessment"))
    }

    private fun observe(activity: LessonActivity, controller: LessonNarrationController, parts: List<Any>, index: Int, verdict: Int,
                        played: MutableMap<Int, JSONObject>, masks: MutableMap<Int, MutableSet<String>>) {
        val player = field<MediaPlayer?>(controller, "player") ?: return
        if (!controller.isPlaying || !field<Boolean>(controller, "playerReady")) return
        val position = player.currentPosition
        val playing = player.isPlaying
        if (!playing || position < 20) return
        val part = parts[index]
        assertFalse("Kokoro must never fall back to Android TTS", field<Boolean>(part, "usedAndroidTts"))
        val file = requireNotNull(field<File?>(part, "file"))
        if (index !in played) played[index] = inspect(file).put("text", field<String>(part, "text"))
            .put("firstPositionMs", position).put("lastPositionMs", position).put("advancingSamples", 0)
        val observation = played.getValue(index)
        if (position > observation.getInt("lastPositionMs")) observation.put("advancingSamples", observation.getInt("advancingSamples") + 1)
        observation.put("lastPositionMs", position)
        val identity = field<Any?>(activity, "incorrectReadingPresentation")?.let { field<String?>(it, "identity") }
        val model = field<LessonReadingProgressModel>(activity, "readingProgress")
        val pages = field<Map<String, Any>>(field<Any>(model, "words"), "pages")
        val alignment = identity?.let { pages[it] }?.let { field<Map<Int, NarrationWordAlignment>>(it, "alignments")[index] }
        if (alignment != null) {
            assertEquals(observation.getString("sha256"), alignment.audioSha256)
            observation.put("alignmentAudioSha256", alignment.audioSha256).put("alignmentWords", alignment.words.size)
            val current = unread(result(activity))
            if (current.isNotEmpty() && current.any { it.start > 0 && it.start < result(activity).text.length - 5 }) {
                val signature = current.joinToString(";") { "${it.start}:${it.endExclusive}" }
                val samples = masks.getOrPut(index) { linkedSetOf() }
                if (samples.size < 8 && samples.add(signature) && samples.size == 3) capture(activity, "verdict-$verdict-part-$index-progressive")
            }
        }
    }
    private fun inspect(file: File): JSONObject {
        val bytes = file.readBytes()
        assertTrue(file.length() > 44)
        assertEquals("RIFF", String(bytes, 0, 4, Charsets.US_ASCII))
        assertEquals("WAVE", String(bytes, 8, 4, Charsets.US_ASCII))
        assertTrue("The actual PCM contains signal", bytes.drop(44).any { it.toInt() != 0 })
        val retriever = MediaMetadataRetriever()
        val duration = try { retriever.setDataSource(file.absolutePath); requireNotNull(retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION)).toLong() }
        finally { retriever.release() }
        assertTrue(duration > 0L)
        return JSONObject().put("file", file.absolutePath).put("sha256", MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it) })
            .put("bytes", bytes.size).put("durationMs", duration)
    }
    private fun capture(activity: LessonActivity, name: String) {
        val view = result(activity)
        if (view.width <= 0 || view.height <= 0) return
        val image = Bitmap.createBitmap(view.width, view.height, Bitmap.Config.ARGB_8888)
        try {
            val canvas = Canvas(image); canvas.drawColor(Color.rgb(28, 31, 36)); view.draw(canvas)
            val folder = requireNotNull(context.getExternalFilesDir(null))
            File(folder, "cpp-output-check-$name.png").outputStream().use { assertTrue(image.compress(Bitmap.CompressFormat.PNG, 100, it)) }
            savePlaybackEvidence(name, JSONObject().put("text", view.text.toString()).put("width", view.width).put("height", view.height)
                .put("unreadRanges", JSONArray(unread(view).map { JSONArray(listOf(it.start, it.endExclusive)) }))
                .put("kind", "Actual Android view Canvas at original bounds sampled during real audio; not a whole-screen screenshot"))
        } finally { image.recycle() }
    }
    private fun savePlaybackEvidence(name: String, value: JSONObject) {
        File(requireNotNull(context.getExternalFilesDir(null)), "cpp-output-check-$name.json").writeText(value.toString(2))
    }
    private fun unread(view: ReadingTextView): List<ReadingTextRange> = field(view.readingInk, "unreadRanges")
    private fun reader(activity: LessonActivity): LessonNarrationController = field(activity, "narrationController")
    private fun awaitWorkerStop(reason: String, timeout: Long, predicate: () -> Boolean) {
        val end = SystemClock.uptimeMillis() + timeout
        while (SystemClock.uptimeMillis() < end) { if (predicate()) return; SystemClock.sleep(60L) }
        fail(reason)
    }
}
