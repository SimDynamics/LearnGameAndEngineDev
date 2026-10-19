package com.learngraphicsprogramming.app.narration

import android.app.Activity
import android.app.Application
import android.app.KeyguardManager
import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.media.MediaMetadataRetriever
import android.media.MediaPlayer
import android.os.Build
import android.os.Bundle
import android.os.SystemClock
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.Button
import android.widget.CompoundButton
import android.widget.EditText
import android.widget.TextView
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

/** Native, non-simulated Kokoro/MediaPlayer checks for deliberate incorrect submissions.
 * All fixtures live in memory under fresh QA identities; no published curriculum is edited. */
@RunWith(AndroidJUnit4::class)
class WrongFeedbackNarrationDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val session = UUID.randomUUID().toString()
    private val preferences = linkedMapOf<String, Map<String, *>>()
    private val durableFiles = linkedMapOf<String, ByteArray?>()
    private var originalPreferenceFiles = emptySet<String>()
    private var originalLocale = ""
    private var preserved = false
    private val controllers = mutableListOf<LessonNarrationController>()
    private var activeScenario: ActivityScenario<LessonActivity>? = null
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

    @Before fun requireLdplayerQaAndPreserveState() {
        assertTrue("Use an isolated QA package", context.packageName in QA_PACKAGES)
        assertEquals(context.packageName, BuildConfig.APPLICATION_ID)
        assertTrue("Run only on LDPlayer", Build.SUPPORTED_ABIS.any { it == "x86" || it == "x86_64" })
        val lock = context.getSystemService(KeyguardManager::class.java)
        assertFalse("Unlock LDPlayer before testing", lock.isDeviceLocked || lock.isKeyguardLocked)
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
        assertTrue(resumePreferences().edit().clear().commit())
        NarrationSettingsRepository.saveEnabled(context, false)
        NarrationSettingsRepository.saveEngine(context, NarrationEngine.KOKORO)
        NarrationSettingsRepository.saveRate(context, 1f)
        NarrationSettingsRepository.saveControlsVisible(context, true)
        PremiumRepository.setVerifiedPremium(context)
    }

    @After fun closeNativePlaybackBeforeRestoringEveryQaValue() {
        if (!preserved) return
        try {
            instrumentation.runOnMainSync { NarrationSettingsRepository.saveEnabled(context, false) }
            activeScenario?.close()
            activeScenario = null
            instrumentation.runOnMainSync { controllers.forEach { it.close() } }
            await("All QA reader workers close before restoring state", 30_000L) {
                controllers.none { field<Thread>(it, "workerThread").isAlive }
            }
            context.stopService(Intent(context, KokoroContinuousNarrationService::class.java))
            await("Existing background synthesis stops before scheduler restoration", 30_000L) {
                Thread.getAllStackTraces().keys.none { it.isAlive && it.name == "KokoroOfflineNarration" }
            }
        } finally {
            (context.applicationContext as Application).unregisterActivityLifecycleCallbacks(guard)
            instrumentation.runOnMainSync { UiLanguageSettings.select(context, originalLocale) }
            (preferenceFiles() + preferences.keys).forEach { name ->
                if (name !in originalPreferenceFiles) assertTrue(context.deleteSharedPreferences(name))
                else {
                    val edit = context.getSharedPreferences(name, Context.MODE_PRIVATE).edit().clear()
                    preferences.getValue(name).forEach { (key, value) -> when (value) {
                        is String -> edit.putString(key, value)
                        is Int -> edit.putInt(key, value)
                        is Long -> edit.putLong(key, value)
                        is Float -> edit.putFloat(key, value)
                        is Boolean -> edit.putBoolean(key, value)
                        is Set<*> -> edit.putStringSet(key, value.filterIsInstance<String>().toSet())
                        else -> error("Unsupported preference $name/$key")
                    } }
                    assertTrue(edit.commit())
                    assertEquals("QA preferences/progress remain unchanged", preferences.getValue(name),
                        context.getSharedPreferences(name, Context.MODE_PRIVATE).all)
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

    @Test fun spanishCppIncorrectGuidanceUsesRealKokoroAndKeepsPageBookmarkAndSuccessGate() =
        exercise(NarrationLanguage.SPANISH, mathematical = false)

    @Test fun spanishInlineFillGuidanceUsesRealKokoroAndKeepsPageBookmarkAndSuccessGate() =
        exercise(NarrationLanguage.SPANISH, mathematical = false, inline = true)

    @Test fun americanEnglishCppIncorrectGuidanceUsesRealKokoroAndKeepsPageBookmarkAndSuccessGate() =
        exercise(NarrationLanguage.AMERICAN_ENGLISH, mathematical = false)

    @Test fun frenchCppIncorrectGuidanceUsesRealKokoroAndKeepsPageBookmarkAndSuccessGate() =
        exercise(NarrationLanguage.FRENCH, mathematical = false)

    @Test fun britishEnglishMathIncorrectGuidanceUsesRealKokoroAndKeepsPageBookmarkAndSuccessGate() =
        exercise(NarrationLanguage.BRITISH_ENGLISH, mathematical = true)

    @Test fun correctChoiceWhileQuestionIsSpokenJumpsToGreenFeedbackWithoutClaimingItHeard() {
        val language = NarrationLanguage.SPANISH
        instrumentation.runOnMainSync { UiLanguageSettings.select(context, "es") }
        NarrationSettingsRepository.saveLanguage(context, language)
        NarrationSettingsRepository.saveKokoroVoice(context, language.defaultVoice)
        val fixture = fixture(language, mathematical = false)
        withFixture(fixture) { host ->
            host.onActivity { activity ->
                assertTrue(activity.findViewById<View>(R.id.buttonLessonListen).performClick())
            }
            awaitNative(host, "The original question is audibly playing before the choice", 180_000L) { activity ->
                val reader = controller(activity)
                val player = field<MediaPlayer?>(reader, "player")
                val gate = field<NarrationExerciseGate?>(reader, "playbackGate")
                reader.isPlaying && field<Boolean>(reader, "playerReady") &&
                    player != null && player.currentPosition > 300 &&
                    gate?.boundary()?.let { field<Int>(reader, "index") < it.start } == true
            }
            host.onActivity { activity ->
                val reader = controller(activity)
                val gate = requireNotNull(field<NarrationExerciseGate?>(reader, "playbackGate"))
                val target = requireNotNull(gate.boundary()).start
                assertTrue(field<Int>(reader, "index") < target)
                select(activity, fixture, correct = true)
                assertTrue(check(activity, fixture).performClick())
                assertTrue("The answer is genuinely saved correct", saved(fixture).getBoolean("correct"))
                assertEquals("The first answer skips its remaining question/options", target,
                    field<Int>(reader, "index"))
                assertTrue("Reader continues on the permitted feedback", reader.isPlaying)
                assertEquals("Correct feedback is not counted heard merely by answering",
                    0, gate.heardThrough)
                assertFalse("The next success sentence remains gated until the first is heard",
                    gate.canPlay(target + 1))
                val reading = field<LessonReadingProgressModel>(activity, "readingProgress")
                val identity = "${fixture.route}/${fixture.id}/0"
                assertEquals("The visibly skipped question becomes read",
                    ReadingBlockState.READ, reading.states(identity)[0])
                assertFalse("The explanation itself is still pending",
                    reading.presentationCompletedSegments(identity).contains(target))
            }
        }
    }

    @Test fun mathCorrectAnswerWhileOptionsPlayInterruptsImmediatelyWithGreenFeedback() {
        val language = NarrationLanguage.BRITISH_ENGLISH
        instrumentation.runOnMainSync { UiLanguageSettings.select(context, "en") }
        NarrationSettingsRepository.saveLanguage(context, language)
        NarrationSettingsRepository.saveKokoroVoice(context, language.defaultVoice)
        val fixture = fixture(language, mathematical = true)
        withFixture(fixture) { host ->
            host.onActivity { activity ->
                assertTrue(activity.findViewById<View>(R.id.buttonLessonListen).performClick())
            }
            awaitNative(host, "Native maths question starts speaking before checking", 180_000L) { activity ->
                val reader = controller(activity)
                val player = field<MediaPlayer?>(reader, "player")
                reader.isPlaying && field<Boolean>(reader, "playerReady") &&
                    player != null && player.currentPosition > 300 &&
                    field<Int>(reader, "index") <
                        requireNotNull(field<NarrationExerciseGate?>(reader, "playbackGate")).boundary()!!.start
            }
            var target = -1
            host.onActivity { activity ->
                val reader = controller(activity)
                val gate = requireNotNull(field<NarrationExerciseGate?>(reader, "playbackGate"))
                target = requireNotNull(gate.feedbackStart(0)).segment
                select(activity, fixture, correct = true)
                assertTrue(check(activity, fixture).performClick())
                assertTrue("Math answer is persisted before the playback command",
                    saved(fixture).getBoolean("correct"))
                assertNull("Math correct answer must not retain wrong-guide audio",
                    field<Any?>(reader, "incorrectFeedback"))
                assertEquals("Math success starts at its own green text immediately",
                    target, field<Int>(reader, "index"))
                assertTrue("The reader remains active", reader.isPlaying)
                assertEquals("Skipped options do not count as heard audio", 0, gate.heardThrough)
            }
            awaitNative(host, "The actual math success WAV plays instead of the remaining options", 180_000L) { activity ->
                val reader = controller(activity)
                val player = field<MediaPlayer?>(reader, "player")
                reader.isPlaying && field<Int>(reader, "index") == target &&
                    field<Boolean>(reader, "playerReady") && player != null &&
                    player.currentPosition > 250
            }
        }
    }

    @Test fun backwardPastTheWrongGuideStartReturnsToTheSameOriginalPageSlot() {
        val language = NarrationLanguage.SPANISH
        instrumentation.runOnMainSync { UiLanguageSettings.select(context, "es") }
        NarrationSettingsRepository.saveLanguage(context, language)
        NarrationSettingsRepository.saveKokoroVoice(context, language.defaultVoice)
        val fixture = fixture(language, mathematical = false)
        withFixture(fixture) { host ->
            host.onActivity { activity ->
                assertTrue(activity.findViewById<View>(R.id.buttonLessonListen).performClick())
            }
            awaitNative(host, "The original page must be playing before wrong feedback", 180_000L) { activity ->
                val reader = controller(activity)
                val player = field<MediaPlayer?>(reader, "player")
                reader.isPlaying && field<Boolean>(reader, "playerReady") &&
                    player != null && player.currentPosition > 300
            }
            host.onActivity { activity ->
                select(activity, fixture, correct = false)
                check(activity, fixture).performClick()
                assertFalse(saved(fixture).getBoolean("correct"))
            }
            awaitNative(host, "A new wrong-answer guide must actually play its first part", 180_000L) { activity ->
                val reader = controller(activity)
                val guide = field<Any?>(reader, "incorrectFeedback")
                val player = field<MediaPlayer?>(reader, "player")
                guide != null && field<Int>(guide, "index") == 0 &&
                    reader.isPlaying && field<Boolean>(reader, "playerReady") &&
                    player != null && player.currentPosition > 300
            }
            host.onActivity { activity ->
                val reader = controller(activity)
                val originalKey = requireNotNull(field<String?>(reader, "resumeKey"))
                val bookmarks = bookmarkKeys()
                val gate = requireNotNull(field<NarrationExerciseGate?>(reader, "playbackGate"))
                val before = gate.heardThrough
                reader.seekBy(-120)
                assertNull("Rewind crosses the temporary guide into the real page",
                    field<Any?>(reader, "incorrectFeedback"))
                assertTrue("The active reader continues from the earlier page audio", reader.isPlaying)
                assertEquals(originalKey, field<String?>(reader, "resumeKey"))
                assertEquals("No fourth bookmark is allocated", bookmarks, bookmarkKeys())
                assertEquals("Wrong feedback never grants success", before, gate.heardThrough)
                assertFalse(saved(fixture).getBoolean("correct"))
            }
        }
    }

    @Test fun correctAnswerToAnotherPuzzleInterruptsTheCurrentIncorrectGuide() {
        val language = NarrationLanguage.SPANISH
        instrumentation.runOnMainSync { UiLanguageSettings.select(context, "es") }
        NarrationSettingsRepository.saveLanguage(context, language)
        NarrationSettingsRepository.saveKokoroVoice(context, language.defaultVoice)
        val base = fixture(language, mathematical = false)
        val lesson = JSONObject(base.lesson.toString())
        val blocks = lesson.getJSONArray("pages").getJSONObject(0).getJSONArray("blocks")
        val first = JSONObject(blocks.getJSONObject(0).toString())
        val second = JSONObject(first.toString()).put("id", "wrong_feedback_second_qa")
            .put("question", "En otra pregunta independiente, identifica la instrucción que muestra el mensaje preparado.")
        lesson.getJSONArray("pages").getJSONObject(0).put("blocks", JSONArray().put(first).put(second)
            .put(JSONObject().put("type", "paragraph").put("text", base.tail)))
        val fixture = base.copy(lesson = lesson)
        withFixture(fixture) { host ->
            host.onActivity { assertTrue(it.findViewById<View>(R.id.buttonLessonListen).performClick()) }
            awaitNative(host, "Original page speech is active", 180_000L) { activity ->
                val reader = controller(activity)
                val player = field<MediaPlayer?>(reader, "player")
                reader.isPlaying && field<Boolean>(reader, "playerReady") &&
                    player != null && player.currentPosition > 300
            }
            host.onActivity { activity ->
                val choices = descendants(content(activity)).filterIsInstance<CompoundButton>()
                assertEquals(4, choices.size)
                choices[0].performClick()
                cppChecks(activity)[0].performClick()
                assertFalse(saved(fixture).getBoolean("correct"))
            }
            awaitNative(host, "Wrong guide has already interrupted source playback", 180_000L) { activity ->
                val reader = controller(activity)
                val player = field<MediaPlayer?>(reader, "player")
                field<Any?>(reader, "incorrectFeedback") != null && reader.isPlaying &&
                    field<Boolean>(reader, "playerReady") && player != null &&
                    player.currentPosition > 350
            }
            lateinit var oldKey: String
            var originalBookmarks = emptySet<String>()
            var expectedTarget = -1
            host.onActivity { activity ->
                val reader = controller(activity)
                oldKey = requireNotNull(field<String?>(reader, "resumeKey"))
                originalBookmarks = bookmarkKeys()
                val gate = requireNotNull(field<NarrationExerciseGate?>(reader, "playbackGate"))
                val choice = descendants(content(activity)).filterIsInstance<CompoundButton>()
                choice[3].performClick()
                cppChecks(activity)[1].performClick()
                assertTrue(requireNotNull(ProgressRepository.getExerciseState(
                    context, fixture.id, 0, 1)).getBoolean("correct"))
                assertNull("New correct answer immediately replaces earlier wrong guide",
                    field<Any?>(reader, "incorrectFeedback"))
                expectedTarget = requireNotNull(gate.feedbackStart(1)).segment
                assertEquals("The new green feedback owns the source cursor immediately",
                    expectedTarget, field<Int>(reader, "index"))
                assertTrue(reader.isPlaying)
                assertEquals("The first puzzle is still unanswered", 0, gate.heardThrough)
                assertFalse("Previous unsuccessful feedback cannot be announced as correct",
                    gate.canPlay(requireNotNull(gate.feedbackStart(0)).segment))
                assertEquals("No fourth narration bookmark", originalBookmarks, bookmarkKeys())
                assertEquals(oldKey, field<String?>(reader, "resumeKey"))
            }
            awaitNative(host, "The new green feedback is the actual playing WAV", 180_000L) { activity ->
                val reader = controller(activity)
                val player = field<MediaPlayer?>(reader, "player")
                reader.isPlaying && field<Boolean>(reader, "playerReady") &&
                    field<Int>(reader, "index") == expectedTarget &&
                    player != null && player.currentPosition > 250
            }
        }
    }

    @Test fun correctingAPausedWrongGuideKeepsPauseAndStartsGreenOnExplicitResume() {
        val language = NarrationLanguage.SPANISH
        instrumentation.runOnMainSync { UiLanguageSettings.select(context, "es") }
        NarrationSettingsRepository.saveLanguage(context, language)
        NarrationSettingsRepository.saveKokoroVoice(context, language.defaultVoice)
        val fixture = fixture(language, mathematical = false)
        withFixture(fixture) { host ->
            host.onActivity { assertTrue(it.findViewById<View>(R.id.buttonLessonListen).performClick()) }
            awaitNative(host, "Question speech is playing", 180_000L) { activity ->
                val reader = controller(activity)
                val player = field<MediaPlayer?>(reader, "player")
                reader.isPlaying && field<Boolean>(reader, "playerReady") &&
                    player != null && player.currentPosition > 300
            }
            host.onActivity { activity ->
                select(activity, fixture, correct = false)
                check(activity, fixture).performClick()
            }
            awaitNative(host, "Wrong feedback guide is playing before user pause", 180_000L) { activity ->
                val reader = controller(activity)
                val player = field<MediaPlayer?>(reader, "player")
                field<Any?>(reader, "incorrectFeedback") != null && reader.isPlaying &&
                    field<Boolean>(reader, "playerReady") &&
                    player != null && player.currentPosition > 300
            }
            var savedIndex = -1
            var savedOffset = -1L
            host.onActivity { activity ->
                val reader = controller(activity)
                reader.pauseKeepingPosition()
                savedIndex = field(reader, "index")
                savedOffset = field(reader, "offset")
                select(activity, fixture, correct = true)
                check(activity, fixture).performClick()
                assertTrue(saved(fixture).getBoolean("correct"))
                assertNull("Correcting the paused guide closes its temporary player",
                    field<Any?>(reader, "incorrectFeedback"))
                assertFalse("A user pause is still respected", reader.isPlaying)
                assertEquals(savedIndex, field<Int>(reader, "index"))
                assertEquals(savedOffset, field<Long>(reader, "offset"))
                assertTrue(activity.findViewById<View>(R.id.buttonLessonListen).performClick())
            }
            awaitNative(host, "Resuming the paused reader starts green success, not old options", 180_000L) { activity ->
                val reader = controller(activity)
                val gate = field<NarrationExerciseGate?>(reader, "playbackGate")
                val player = field<MediaPlayer?>(reader, "player")
                val start = gate?.feedbackStart(0)?.segment
                start != null && reader.isPlaying && field<Int>(reader, "index") == start &&
                    field<Boolean>(reader, "playerReady") && player != null &&
                    player.currentPosition > 250
            }
        }
    }

    private fun exercise(language: NarrationLanguage, mathematical: Boolean, inline: Boolean = false) {
        val locale = language.locale.language
        instrumentation.runOnMainSync { UiLanguageSettings.select(context, locale) }
        NarrationSettingsRepository.saveLanguage(context, language)
        NarrationSettingsRepository.saveKokoroVoice(context, language.defaultVoice)
        NarrationSettingsRepository.saveEnabled(context, false)
        val fixture = fixture(language, mathematical, inline)
        val sourceJson = LessonRepository.loadLessonJson(context, fixture.route, fixture.sourceLesson).toString()
        val sourceHash = sha(sourceJson.toByteArray())
        val resumeStore = NarrationResumeStore(context)
        val olderA = "wrong-feedback-qa-older-A-$session"
        val olderB = "wrong-feedback-qa-older-B-$session"
        resumeStore.begin(olderA, NarrationResumePoint(2, 1234, finished = true))
        resumeStore.begin(olderB, NarrationResumePoint(1, 567))
        var originalBookmark: Map<String, *> = emptyMap<String, Any?>()
        var originalMainSegment = 0
        var originalMainOffset = 0L
        var originalHeard = 0
        var originalBoundary: Any? = null
        var originalResumeKey = ""
        lateinit var guideAudio: File
        var guideSha = ""
        var guideDuration = 0L
        var guidePauseOffset = 0L
        var firstGuideToken = 0L
        var originalQuestionMask = emptyList<ReadingTextRange>()
        var tailObserved = false
        val phaseEvents = JSONArray()

        withFixture(fixture) { host ->
            host.onActivity { activity ->
                assertTrue(activity.findViewById<View>(R.id.buttonLessonListen).performClick())
            }
            awaitNative(host, "The original page must actually play Kokoro before interruption", 180_000L) { activity ->
                val reader = controller(activity)
                val player = field<MediaPlayer?>(reader, "player")
                reader.isPlaying && field<Boolean>(reader, "playerReady") && player != null && player.currentPosition > 300
            }
            lateinit var originalSegments: Any
            lateinit var originalGate: NarrationExerciseGate
            host.onActivity { activity ->
                val reader = controller(activity)
                reader.pauseKeepingPosition()
                assertNull("A paused page keeps its position and releases MediaPlayer", field<MediaPlayer?>(reader, "player"))
                originalMainSegment = field(reader, "index")
                originalMainOffset = field(reader, "offset")
                assertTrue("An actual observed playback offset is retained", originalMainOffset > 0L)
                originalResumeKey = requireNotNull(field<String?>(reader, "resumeKey"))
                originalSegments = field<Any>(reader, "segments")
                originalGate = requireNotNull(field<NarrationExerciseGate?>(reader, "playbackGate"))
                originalHeard = originalGate.heardThrough
                originalBoundary = originalGate.boundary()
                val lastIndex = field<List<Any>>(reader, "segments").lastIndex
                assertFalse("The paragraph after the puzzle is blocked before successful feedback is heard", originalGate.canPlay(lastIndex))
                assertTrue("Bookmark clamping also protects the later paragraph",
                    originalGate.clamp(NarrationGatePosition(lastIndex, 999)).segment < lastIndex)
                assertEquals("Exactly the existing three page positions are retained", setOf(olderA, olderB, originalResumeKey), bookmarkKeys())
                originalBookmark = resumePreferences().all.toMap()
            }
            // Native word alignment arrives asynchronously even for a ready WAV. Establish
            // the original paused presentation after that real metadata reaches its view;
            // otherwise the comparison would mistake a late alignment for guide progress.
            awaitNative(host, "The paused original segment must receive its actual source-audio word alignment", 120_000L) { activity ->
                originalAlignment(activity) != null
            }
            host.onActivity { activity ->
                originalQuestionMask = field<List<ReadingTextRange>>(question(activity, fixture), "unreadRanges").toList()
                select(activity, fixture, correct = false)
                check(activity, fixture).performClick()
                assertFalse(saved(fixture).getBoolean("correct"))
                assertEquals("A failed attempt cannot complete the puzzle", 0, field<Int>(activity, "completedExercises"))
            }
            // This case verifies seeking *within* one real guide recording. A
            // short leading verdict may be under a second: rewinding it by a
            // second correctly returns to the original page, which the separate
            // backwardPastTheWrongGuideStart test covers. Observe a sufficiently
            // long WAV and a measured position safely beyond that boundary.
            awaitNative(host, "A real guide WAV must audibly reach an interior seek position", 180_000L) { activity ->
                val reader = controller(activity)
                val guide = field<Any?>(reader, "incorrectFeedback")
                val player = field<MediaPlayer?>(reader, "player")
                guide != null && reader.isPlaying && field<Boolean>(reader, "playerReady") &&
                    player != null && player.duration > 2_200 &&
                    player.currentPosition in 1_301..(player.duration - 400)
            }
            host.onActivity { activity ->
                val reader = controller(activity)
                val guide = requireNotNull(field<Any?>(reader, "incorrectFeedback"))
                firstGuideToken = field(guide, "token")
                val parts = field<List<Any>>(guide, "parts")
                val expectedGuidance = listOf(feedback(activity, fixture).text.toString())
                assertEquals("The complete visible review enters the ordinary language speech front end",
                    NarrationSegments.sentences(IncorrectFeedbackNarration.prepare(expectedGuidance, language, true)),
                    parts.map { field<String>(it, "text") })
                val guideText = parts.joinToString(" ") { field<String>(it, "text") }
                assertFalse("The absent option is not read", guideText.contains(fixture.absentReview))
                assertFalse("Successful feedback is not read on a failed attempt", guideText.contains(fixture.success))
                assertFalse("The hidden response ID is not spoken", guideText.contains(RESPONSE_ID))
                assertSame("No alternate page segment history is installed", originalSegments, field<Any>(reader, "segments"))
                assertSame(originalGate, field<NarrationExerciseGate?>(reader, "playbackGate"))
                assertEquals(originalMainSegment, field<Int>(reader, "index"))
                assertEquals(originalMainOffset, field<Long>(reader, "offset"))
                assertEquals(originalHeard, originalGate.heardThrough)
                assertEquals(originalBoundary, originalGate.boundary())
                assertFalse("A wrong guide cannot unlock the following paragraph", originalGate.canPlay(field<List<Any>>(reader, "segments").lastIndex))
                guideAudio = requireNotNull(field<File?>(parts[field<Int>(guide, "index")], "file"))
                assertEquals("The real recording belongs to the selected Kokoro language and voice",
                    KokoroNarrationCache.key(language.tag, language.defaultVoice, field(parts[field<Int>(guide, "index")], "text")),
                    guideAudio.nameWithoutExtension)
                val metadata = inspectWav(guideAudio)
                guideSha = metadata.getString("sha256")
                guideDuration = metadata.getLong("durationMs")
                phaseEvents.put(JSONObject().put("stage", "real-guide-playing").put("audio", metadata))
                val result = feedback(activity, fixture)
                assertTrue("The original incorrect feedback remains visible", result.text.toString().contains(fixture.guide))
                assertTrue("The unspoken review suffix stays shaded", field<List<ReadingTextRange>>(result, "unreadRanges").isNotEmpty())
                captureView(result, "${language.tag}-incorrect-visible")
                reader.pauseKeepingPosition()
                assertNull("Pausing a guide also releases the same MediaPlayer", field<MediaPlayer?>(reader, "player"))
                val guidePart = field<Int>(guide, "index")
                guidePauseOffset = field(guide, "offset")
                assertTrue("The real paused offset supports an interior one-second rewind", guidePauseOffset > 1_300L)
                assertEquals(originalBookmark, resumePreferences().all)
                reader.seekBy(-1)
                assertSame("An interior rewind retains the active guide", guide, field<Any?>(reader, "incorrectFeedback"))
                assertEquals(guidePart, field<Int>(guide, "index"))
                assertEquals("Backward uses the measured source offset exactly", guidePauseOffset - 1_000L, field<Long>(guide, "offset"))
                reader.seekBy(1)
                assertSame("Forward still addresses the same guide, not a stale reference", guide, field<Any?>(reader, "incorrectFeedback"))
                assertEquals(guidePart, field<Int>(guide, "index"))
                assertEquals(firstGuideToken, field<Long>(guide, "token"))
                assertEquals("The paired interior seeks restore the exact paused offset", guidePauseOffset, field<Long>(guide, "offset"))
                guidePauseOffset = field(guide, "offset")
                assertTrue("A forward guide seek retains a measured offset", guidePauseOffset >= 1_000L)
                assertEquals("A guide seek cannot credit any successful feedback", originalHeard, originalGate.heardThrough)
                assertEquals(originalMainOffset, field<Long>(reader, "offset"))
                assertEquals(originalBookmark, resumePreferences().all)
                assertTrue(activity.findViewById<View>(R.id.buttonLessonListen).performClick())
            }
            awaitNative(host, "The same guide audio resumes at its saved offset", 60_000L) { activity ->
                val reader = controller(activity)
                val guide = field<Any?>(reader, "incorrectFeedback")
                val player = field<MediaPlayer?>(reader, "player")
                guide != null && reader.isPlaying && field<Boolean>(reader, "playerReady") &&
                    field<Long>(guide, "token") == firstGuideToken && player != null && player.currentPosition >= guidePauseOffset
            }
            assertEquals("Resume/seek reuse the compatible recording", guideSha, sha(guideAudio.readBytes()))
            awaitNative(host, "The guide stops naturally without entering the page success gate", 120_000L) { activity ->
                val reader = controller(activity)
                field<Any?>(reader, "incorrectFeedback")?.let { field<Boolean>(it, "finished") } == true && !reader.isPlaying
            }
            host.onActivity { activity ->
                val reader = controller(activity)
                assertNull("Natural guide completion releases the decoder", field<MediaPlayer?>(reader, "player"))
                assertFalse("A guide finishing is not a page completing", field<Boolean>(reader, "finished"))
                assertEquals(originalMainSegment, field<Int>(reader, "index"))
                assertEquals(originalMainOffset, field<Long>(reader, "offset"))
                assertEquals(originalHeard, originalGate.heardThrough)
                assertEquals(originalBoundary, originalGate.boundary())
                assertFalse("Natural guide completion still leaves the following paragraph gated",
                    originalGate.canPlay(field<List<Any>>(reader, "segments").lastIndex))
                assertEquals(originalBookmark, resumePreferences().all)
                assertEquals(setOf(olderA, olderB, originalResumeKey), bookmarkKeys())
                assertEquals("The paused page's word shading is unchanged after the guide", originalQuestionMask,
                    field<List<ReadingTextRange>>(question(activity, fixture), "unreadRanges"))
                assertFalse(saved(fixture).getBoolean("correct"))
            }
        }

        // A new Activity restores the same wrong draft; it must not repeat speech simply
        // because its red feedback is rendered again.
        withFixture(fixture) { host ->
            awaitNative(host, "The restored native view must settle without repeating its guide", 15_000L) { activity ->
                controller(activity).hasNarration && !content(activity).isLayoutRequested
            }
            host.onActivity { activity ->
                val reader = controller(activity)
                assertNull("A restored wrong draft is not a new submission", field<Any?>(reader, "incorrectFeedback"))
                assertFalse(reader.isPlaying)
                assertEquals(originalBookmark, resumePreferences().all)
                assertFalse(saved(fixture).getBoolean("correct"))
                select(activity, fixture, correct = true)
                check(activity, fixture).performClick()
                assertTrue(saved(fixture).getBoolean("correct"))
                assertTrue(feedback(activity, fixture).text.toString().contains(fixture.success))
                val gate = requireNotNull(field<NarrationExerciseGate?>(reader, "playbackGate"))
                val target = requireNotNull(gate.feedbackStart(0)).segment
                assertTrue("A correct answer reads immediately, without tapping Play", reader.isPlaying)
                assertEquals(target, field<Int>(reader, "index"))
                assertEquals("Answering itself does not mark the explanation heard",
                    originalHeard, gate.heardThrough)
                assertFalse("The second green sentence waits for actual first playback",
                    gate.canPlay(target + 1))
            }
            awaitNative(host, "Green feedback must complete before the later paragraph plays naturally", 180_000L) { activity ->
                val reader = controller(activity)
                val parts = field<List<Any>>(reader, "segments")
                val player = field<MediaPlayer?>(reader, "player")
                if (!tailObserved && reader.isPlaying && field<Boolean>(reader, "playerReady") &&
                    field<Int>(reader, "index") == parts.lastIndex && player != null && player.currentPosition > 250) {
                    val gate = requireNotNull(field<NarrationExerciseGate?>(reader, "playbackGate"))
                    val boundary = originalBoundary as NarrationFeedbackGate
                    assertTrue("Only naturally completed successful feedback permits the tail", gate.heardThrough >= boundary.endExclusive)
                    assertNull(gate.boundary())
                    assertEquals(NarrationSegments.sentences(IncorrectFeedbackNarration.prepare(listOf(fixture.tail), language, true)).last(),
                        field<String>(parts.last(), "text"))
                    val audio = requireNotNull(field<File?>(parts.last(), "file"))
                    phaseEvents.put(JSONObject().put("stage", "following-paragraph-actually-playing")
                        .put("segment", parts.lastIndex).put("heardThrough", gate.heardThrough).put("audio", inspectWav(audio)))
                    tailObserved = true
                }
                !reader.isPlaying && field<Boolean>(reader, "finished")
            }
            host.onActivity { activity ->
                assertTrue("The later paragraph was actually heard after the successful feedback", tailObserved)
                val reader = controller(activity)
                val gate = requireNotNull(field<NarrationExerciseGate?>(reader, "playbackGate"))
                assertNull(field<MediaPlayer?>(reader, "player"))
                assertTrue("Natural successful feedback completion advances the original heard frontier", gate.heardThrough > originalHeard)
                assertEquals(setOf(olderA, olderB, originalResumeKey), bookmarkKeys())
                val point = requireNotNull(resumeStore.restore(originalResumeKey, field<List<Any>>(reader, "segments").size))
                assertTrue("A completed page still occupies its one bounded slot", point.finished)
                assertTrue(saved(fixture).getBoolean("correct"))
                captureView(feedback(activity, fixture), "${language.tag}-success-natural-finish")
            }
        }
        assertEquals("No published text or curricular answer changed", sourceHash,
            sha(LessonRepository.loadLessonJson(context, fixture.route, fixture.sourceLesson).toString().toByteArray()))
        File(evidence(), "${language.tag}-${if (inline) "inline" else if (mathematical) "math" else "cpp"}-native.json").writeText(JSONObject()
            .put("package", context.packageName).put("device", Build.MODEL).put("language", language.tag)
            .put("voice", language.defaultVoice).put("route", fixture.route).put("guide", fixture.guide)
            .put("guideAudioSha256", guideSha).put("guideDurationMs", guideDuration).put("savedGuideOffsetMs", guidePauseOffset)
            .put("mainSegment", originalMainSegment).put("mainOffsetMs", originalMainOffset)
            .put("mainBookmarkKey", originalResumeKey).put("threePageKeys", JSONArray(bookmarkKeys().sorted()))
            .put("guideNeverCreditsSuccess", true).put("restoreNeverRepeatsGuide", true)
            .put("laterParagraphActuallyPlayedAfterSuccess", tailObserved)
            .put("events", phaseEvents).toString(2))
    }

    private data class Fixture(val id: String, val route: String, val sourceLesson: String,
                               val mathematical: Boolean, val language: NarrationLanguage,
                               val guide: String, val absentReview: String, val success: String, val tail: String, val lesson: JSONObject)

    private fun fixture(language: NarrationLanguage, mathematical: Boolean, inline: Boolean = false): Fixture {
        require(!inline || (language == NarrationLanguage.SPANISH && !mathematical))
        val text = when (language) {
            NarrationLanguage.SPANISH -> listOf(
                "Considera con cuidado qué parte del programa muestra el mensaje y qué recurso representa cada nombre antes de elegir una respuesta.",
                "Revisa la afirmación elegida y distingue la instrucción de salida del mensaje mostrado. Relaciona std::cout con su función antes de volver a responder.",
                "Este texto del inciso no seleccionado no debe leerse.",
                "La instrucción de salida utiliza el recurso indicado y conserva el mensaje original.")
            NarrationLanguage.FRENCH -> listOf(
                "Examine attentivement quelle partie du programme affiche le message et ce que représente chaque nom avant de choisir une réponse.",
                "Revois la proposition choisie et distingue l'instruction de sortie du message affiché. Relie std::cout à sa fonction avant de répondre de nouveau.",
                "Ce texte de la proposition non choisie ne doit pas être lu.",
                "L'instruction de sortie utilise la ressource indiquée et conserve le message original.")
            else -> if (mathematical) listOf(
                "Consider carefully which quantities are being compared and which units each quantity represents before selecting an answer to the question.",
                "Review the selected statement and compare the units of the quantities. Consider what 6 × 100 = 600 represents before choosing your answer again.",
                "This unselected option's guidance must never be spoken.",
                "The comparison preserves both the original quantities and their units.") else listOf(
                "Consider carefully which part of the program displays the message and what each name represents before selecting an answer to the question.",
                "Review the selected statement and distinguish the output instruction from the displayed message. Relate std::cout to its purpose before choosing your answer again.",
                "This unselected option's guidance must never be spoken.",
                "The output instruction uses the indicated resource and preserves the original message.")
        }
        val id = (if (mathematical) "math_course_01__numeros_y_operaciones" else if (inline) "cpp_fundamentals__objetos_y_estado"
            else "cpp_fundamentals__programas_y_ejecucion") +
            "__wrong_feedback_qa_" + UUID.randomUUID().toString().replace("-", "")
        val inlineGuide = "Revisa el tipo de los operandos antes de considerar el destino del cociente. Distingue el cálculo de su almacenamiento antes de volver a responder."
        // This exact reasoning is already in the later curricular paragraph p9 b3.
        // Only the in-memory QA copy attaches it to the existing success gate.
        val inlineSuccess = "Los huecos se completan con `double`, `longitud_metros` y `partes`, en ese orden. " +
            "Aunque el inicializador escrito sea el entero `9`, la variable `longitud_metros` tiene el tipo `double` elegido en su definición."
        val block = if (inline) {
            val original = LessonRepository.loadLessonJson(context, "programming", INLINE_SOURCE)
            assertTrue(original.getJSONArray("pages").getJSONObject(8).getJSONArray("blocks").getJSONObject(2)
                .getString("text").contains(inlineSuccess))
            JSONObject(original.getJSONArray("pages").getJSONObject(7).getJSONArray("blocks").getJSONObject(6).toString())
                .put("reviewFeedback", inlineGuide).put("explanation", inlineSuccess).also {
                    assertEquals("inline_fill_code", it.getString("type"))
                    assertEquals(listOf("double", "longitud_metros", "partes"),
                        List(it.getJSONArray("answers").length()) { index -> it.getJSONArray("answers").getString(index) })
                }
        } else if (mathematical) JSONObject().put("type", "math_puzzle").put("schemaVersion", 1).put("id", "wrong_feedback_choice")
            .put("prompt", text[0]).put("responses", JSONArray().put(JSONObject().put("id", RESPONSE_ID)
                .put("type", "math_multiple_choice_single").put("options", JSONArray()
                    .put(JSONObject().put("id", "wrong").put("label", "6 × 100 = 6").put("reviewFeedback", text[1]))
                    .put(JSONObject().put("id", "correct").put("label", "6 × 100 = 600").put("reviewFeedback", text[2])))
                .put("validator", JSONObject().put("type", "choice").put("expected", JSONArray().put("correct")))))
            .put("feedback", JSONObject().put("correct", text[3]).put("incorrect", "Unused legacy global feedback"))
        else JSONObject().put("type", "multiple_choice").put("id", "wrong_feedback_choice").put("question", text[0])
            .put("options", JSONArray().put("return 0;").put("std::cout << \"Message\";"))
            .put("correctIndex", 1).put("reviewFeedback", JSONArray().put(text[1]).put(text[2])).put("explanation", text[3])
        val tail = when (language) {
            NarrationLanguage.SPANISH -> "Este párrafo posterior sólo continúa después de escuchar la explicación de la respuesta correcta."
            NarrationLanguage.FRENCH -> "Ce paragraphe suivant continue seulement après avoir entendu l'explication de la réponse correcte."
            else -> "This following paragraph continues only after hearing the explanation of the correct answer."
        }
        val lesson = JSONObject().put("id", id).put("title", "").put("language", language.tag)
            .put("pages", JSONArray().put(JSONObject().put("blocks", JSONArray().put(block).put(JSONObject().put("type", "paragraph").put("text", tail)))))
        return Fixture(id, if (mathematical) "math_physics" else "programming",
            if (mathematical) MATH_SOURCE else if (inline) INLINE_SOURCE else CPP_SOURCE,
            mathematical, language, if (inline) inlineGuide else text[1], text[2], if (inline) inlineSuccess else text[3], tail, lesson)
    }

    private fun withFixture(fixture: Fixture, body: (ActivityScenario<LessonActivity>) -> Unit) {
        NarrationSettingsRepository.saveEnabled(context, false)
        ProgressRepository.saveCurrentPage(context, fixture.route, fixture.sourceLesson, 0)
        val intent = Intent(context, LessonActivity::class.java).putExtra("routeId", fixture.route)
            .putExtra("courseId", if (fixture.mathematical) "math_course_01" else "cpp_fundamentals")
            .putExtra("unitId", if (fixture.mathematical) "numeros_y_operaciones"
                else if (fixture.sourceLesson == INLINE_SOURCE) "objetos_y_estado" else "programas_y_ejecucion")
            .putExtra("lessonId", fixture.sourceLesson).putExtra("lessonNumber", 1).putExtra("lessonTotal", 1)
        ActivityScenario.launch<LessonActivity>(intent).use { host ->
            activeScenario = host
            var stableSince = 0L
            var instance = 0
            awaitNative(host, "The real lesson host must be stable before QA injection", 15_000L) { activity ->
                val loaded = field<JSONObject?>(activity, "lessonJson")
                val ready = field<String>(activity, "currentLessonId") == fixture.sourceLesson && loaded?.optString("id") == fixture.sourceLesson &&
                    activity.hasWindowFocus() && content(activity).width > 0 && !content(activity).isLayoutRequested
                val key = System.identityHashCode(activity) xor System.identityHashCode(loaded)
                if (!ready || key != instance) { instance = key; stableSince = if (ready) SystemClock.uptimeMillis() else 0L }
                ready && SystemClock.uptimeMillis() - stableSince > 500L
            }
            host.onActivity { activity ->
                setField(activity, "currentLessonId", fixture.id)
                setField(activity, "lessonJson", JSONObject(fixture.lesson.toString()))
                setField(activity, "currentPage", 0)
                setField(activity, "totalPages", 1)
                listOf("pageViewCache", "pageRequiredExercises", "pageCompletedExercises").forEach {
                    field<MutableMap<Int, Any>>(activity, it).clear()
                }
                NarrationSettingsRepository.saveEnabled(context, true)
                invoke(activity, "renderCurrentPage")
                controllers += controller(activity)
            }
            awaitNative(host, "The injected native lesson must finish layout", 10_000L) { activity ->
                assertEquals(fixture.id, field<String>(activity, "currentLessonId"))
                content(activity).width > 0 && !content(activity).isLayoutRequested && controller(activity).hasNarration
            }
            try { body(host) } finally {
                host.onActivity { controller(it).pauseKeepingPosition() }
                activeScenario = null
            }
        }
        instrumentation.waitForIdleSync()
    }

    private fun select(activity: LessonActivity, fixture: Fixture, correct: Boolean) {
        val block = fixture.lesson.getJSONArray("pages").getJSONObject(0).getJSONArray("blocks").getJSONObject(0)
        if (block.optString("type") == "inline_fill_code") {
            val answers = block.getJSONArray("answers")
            val values = List(answers.length()) { index -> if (!correct && index == 0) "int" else answers.getString(index) }
            val inputs = descendants(content(activity)).filterIsInstance<EditText>()
            assertEquals("The original independent holes remain editable", values.size, inputs.size)
            values.forEachIndexed { index, value -> inputs[index].setText(value) }
            assertEquals(values, inputs.map { it.text.toString() })
            // As in every technical field, a first draft is not a submitted attempt.
            // After a reviewed attempt, editing persists its same independent holes.
            ProgressRepository.getExerciseState(context, fixture.id, 0, 0)?.let { draft ->
                assertFalse(draft.getBoolean("correct"))
                assertEquals(values, List(draft.getJSONArray("answers").length()) { draft.getJSONArray("answers").getString(it) })
            }
            return
        }
        val choices = descendants(content(activity)).filterIsInstance<CompoundButton>()
        assertEquals(2, choices.size)
        val index = if (correct) 1 else 0
        // CompoundButton toggles before View.performClick; its return value only reports
        // an OnClickListener. Both real renderers persist selection via checked-change.
        choices[index].performClick()
        assertTrue("The deliberate native selection is actually checked", choices[index].isChecked)
        assertEquals("Only the chosen radio option is checked", 1, choices.count { it.isChecked })
        val draft = saved(fixture)
        if (fixture.mathematical) {
            val selection = draft.getJSONObject("responses").getJSONObject(RESPONSE_ID).getJSONArray("selected")
            assertEquals(listOf(if (correct) "correct" else "wrong"), List(selection.length()) { selection.getString(it) })
        } else assertEquals("The renderer saves the selected original index", index, draft.getInt("selectedIndex"))
    }
    private fun check(activity: LessonActivity, fixture: Fixture): Button = descendants(content(activity)).filterIsInstance<Button>().single {
        it.text.toString() in if (fixture.mathematical) listOf(activity.getString(R.string.math_ui_check), activity.getString(R.string.math_ui_correct_button))
            else listOf(activity.getString(R.string.exercise_check), activity.getString(R.string.exercise_correct)) }
    private fun cppChecks(activity: LessonActivity) = descendants(content(activity)).filterIsInstance<Button>().filter {
        it.text.toString() in listOf(activity.getString(R.string.exercise_check), activity.getString(R.string.exercise_correct)) }
    private fun feedback(activity: LessonActivity, fixture: Fixture): ReadingTextView = descendants(content(activity)).filterIsInstance<ReadingTextView>().single {
        it.currentTextColor in setOf(Color.rgb(240, 150, 150), Color.rgb(240, 166, 166), Color.rgb(180, 220, 190)) }
    private fun question(activity: LessonActivity, fixture: Fixture): ReadingTextView {
        val block = fixture.lesson.getJSONArray("pages").getJSONObject(0).getJSONArray("blocks").getJSONObject(0)
        val source = block.getString(if (fixture.mathematical) "prompt" else "question")
        return descendants(content(activity)).filterIsInstance<ReadingTextView>().single { it.text.toString() == source }
    }
    private fun saved(fixture: Fixture) = requireNotNull(ProgressRepository.getExerciseState(context, fixture.id, 0, 0))
    private fun bookmarkKeys() = (0 until 3).mapNotNull { resumePreferences().getString("key_$it", null) }.toSet()
    private fun resumePreferences() = context.getSharedPreferences("lesson_narration_resume_bounded_v1", Context.MODE_PRIVATE)
    private fun controller(activity: LessonActivity): LessonNarrationController = field(activity, "narrationController")
    private fun originalAlignment(activity: LessonActivity): NarrationWordAlignment? {
        val reader = controller(activity)
        val model = field<LessonReadingProgressModel>(activity, "readingProgress")
        val words = field<Any>(model, "words")
        val page = field<Map<String, Any>>(words, "pages")[field<String>(reader, "activePageIdentity")] ?: return null
        return field<Map<Int, NarrationWordAlignment>>(page, "alignments")[field<Int>(reader, "index")]
    }
    private fun content(activity: LessonActivity): ViewGroup = activity.findViewById(R.id.lessonContent)
    private fun descendants(view: View): List<View> = buildList { add(view); if (view is ViewGroup) repeat(view.childCount) { addAll(descendants(view.getChildAt(it))) } }
    private fun preferenceFiles() = File(context.applicationInfo.dataDir, "shared_prefs").listFiles().orEmpty().filter { it.name.endsWith(".xml") }.map { it.name.removeSuffix(".xml") }.toSet()
    @Suppress("UNCHECKED_CAST") private fun <T> field(target: Any, name: String): T {
        // ReadingTextView and ReadingRadioButton share their drawing state in readingInk;
        // it is deliberately not a duplicated field on either native view class.
        val owner = if (target is ReadingTextHost && name == "unreadRanges") target.readingInk else target
        return owner.javaClass.getDeclaredField(name).apply { isAccessible = true }.get(owner) as T
    }
    private fun setField(target: Any, name: String, value: Any) = target.javaClass.getDeclaredField(name).apply { isAccessible = true }.set(target, value)
    private fun invoke(target: Any, name: String) = target.javaClass.getDeclaredMethod(name).apply { isAccessible = true }.invoke(target)
    private fun awaitNative(host: ActivityScenario<LessonActivity>, reason: String, timeout: Long, condition: (LessonActivity) -> Boolean) = await(reason, timeout) {
        var ready = false; host.onActivity { ready = condition(it) }; ready
    }
    private fun await(reason: String, timeout: Long, condition: () -> Boolean) {
        val end = SystemClock.uptimeMillis() + timeout
        while (SystemClock.uptimeMillis() < end) {
            instrumentation.waitForIdleSync()
            if (condition()) return
            SystemClock.sleep(40L)
        }
        fail(reason)
    }
    private fun evidence() = File(requireNotNull(context.getExternalFilesDir(null)), "wrong-feedback-narration-qa/$session").apply { mkdirs() }
    private fun sha(bytes: ByteArray) = MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it) }
    private fun inspectWav(file: File): JSONObject {
        assertTrue("The guide uses a real synthesized/cached WAV", file.isFile && file.length() > 44L)
        val bytes = file.readBytes()
        assertEquals("RIFF", String(bytes, 0, 4, Charsets.US_ASCII))
        assertEquals("WAVE", String(bytes, 8, 4, Charsets.US_ASCII))
        assertTrue("The produced PCM must not be silent", bytes.drop(44).any { it.toInt() != 0 })
        val retriever = MediaMetadataRetriever()
        val duration = try {
            retriever.setDataSource(file.absolutePath)
            requireNotNull(retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION)).toLong()
        } finally { retriever.release() }
        assertTrue("A real decoder reports the guide audio duration", duration > 0L)
        return JSONObject().put("file", file.absolutePath).put("bytes", bytes.size).put("sha256", sha(bytes)).put("durationMs", duration)
    }
    private fun captureView(view: TextView, name: String) {
        assertTrue(view.width > 0 && view.height > 0)
        val bitmap = Bitmap.createBitmap(view.width, view.height, Bitmap.Config.ARGB_8888)
        try {
            val canvas = Canvas(bitmap); canvas.drawColor(Color.rgb(28, 31, 36)); view.draw(canvas)
            File(evidence(), "$name.png").outputStream().use { assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) }
            File(evidence(), "$name.json").writeText(JSONObject().put("text", view.text.toString()).put("width", view.width)
                .put("height", view.height).put("textColor", view.currentTextColor).put("kind", "native feedback TextView at its real bounds").toString(2))
        } finally { bitmap.recycle() }
    }

    companion object {
        private const val RESPONSE_ID = "qa_internal_quantity_response"
        private const val CPP_SOURCE = "cpp_fundamentals__programas_y_ejecucion__primer_programa_y_salida"
        private const val INLINE_SOURCE = "cpp_fundamentals__objetos_y_estado__enteros_y_aproximaciones"
        private const val MATH_SOURCE = "math_course_01__numeros_y_operaciones__magnitudes_y_numeracion"
        private val QA_PACKAGES = setOf("com.learngraphicsprogramming.app.curricularqa", "com.learngraphicsprogramming.app.realdownloadsqa")
        private val KNOWN_PREFERENCES = setOf("learning_progress", "navigation_state", "lesson_narration_settings", "lesson_narration_resume_bounded_v1",
            "attempt_reserve_seconds_test", "premium_state", "ui_language_settings", "appearance_settings", "app_settings")
    }
}
