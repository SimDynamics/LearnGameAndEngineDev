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

/** All nine current authored guides and complete explanations, through real native Kokoro.
 * Only the lesson identity and surrounding QA tail differ; all original block fields and string values are preserved. */
@RunWith(AndroidJUnit4::class)
class CurrentInlineFeedbackNarrationDeviceTest {
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

    @Test fun repartoActual() = exercise(0, checkTransport = true)
    @Test fun tiposActuales() = exercise(1)
    @Test fun inicializadoresActuales() = exercise(2)
    @Test fun operacionesActuales() = exercise(3)
    @Test fun nombresActuales() = exercise(4)
    @Test fun conversionActual() = exercise(5)
    @Test fun cadenasActuales() = exercise(6)
    @Test fun caracteresActuales() = exercise(7)
    @Test fun literalYOperadorActuales() = exercise(8)

    private fun exercise(location: Int, checkTransport: Boolean = false) {
        instrumentation.runOnMainSync { UiLanguageSettings.select(context, "es") }
        NarrationSettingsRepository.saveLanguage(context, NarrationLanguage.SPANISH)
        NarrationSettingsRepository.saveKokoroVoice(context, NarrationLanguage.SPANISH.defaultVoice)
        val fixture = fixture(location)
        val result = JSONObject().put("source", fixture.path).put("sourceSha256", fixture.sourceSha)
            .put("page", fixture.page).put("block", fixture.number).put("package", context.packageName)
            .put("device", Build.MODEL).put("voice", NarrationLanguage.SPANISH.defaultVoice)
            .put("guide", fixture.block.getString("reviewFeedback")).put("explanation", fixture.block.getString("explanation"))
            .put("qaOnlyChanges", "One unchanged curricular block under an isolated lesson identity; one subsequent QA paragraph")
        File(evidence(), "$location-started.json").writeText(result.toString(2))
        val store = NarrationResumeStore(context)
        val olderA = "inline-current-older-A-$session"
        val olderB = "inline-current-older-B-$session"
        store.begin(olderA, NarrationResumePoint(2, 1234, finished = true))
        store.begin(olderB, NarrationResumePoint(1, 567))
        var originalBookmark = emptyMap<String, Any?>()
        var originalHeard = 0
        var sourceSegment = -1
        var sourceOffset = -1L
        var sourceKey = ""
        var expectedBoundary: NarrationFeedbackGate? = null
        lateinit var pageSegments: List<Any>
        val guideObserved = linkedMapOf<Int, JSONObject>()
        val guideMasks = linkedMapOf<Int, MutableSet<String>>()
        val successObserved = linkedMapOf<Int, JSONObject>()
        val successMasks = linkedMapOf<Int, MutableSet<String>>()
        var transportChecked = !checkTransport

        withFixture(fixture) { host ->
            host.onActivity { activity ->
                val visible = descendants(content(activity)).filterIsInstance<TextView>().joinToString("\n") { it.text }
                assertFalse("No authored guide before a deliberate error", visible.contains(fixture.block.getString("reviewFeedback")))
                assertFalse("No solution explanation before an answer", visible.contains(fixture.block.getString("explanation")))
                assertTrue(activity.findViewById<View>(R.id.buttonLessonListen).performClick())
            }
            awaitNative(host, "The original question really plays before its interruption", 180_000L) { activity ->
                val reader = controller(activity)
                val player = field<MediaPlayer?>(reader, "player")
                reader.isPlaying && field<Boolean>(reader, "playerReady") && player != null && player.currentPosition > 300
            }
            host.onActivity { activity ->
                val reader = controller(activity)
                reader.pauseKeepingPosition()
                assertNull("Question pause releases MediaPlayer", field<MediaPlayer?>(reader, "player"))
                sourceSegment = field(reader, "index")
                sourceOffset = field(reader, "offset")
                assertTrue(sourceOffset > 0L)
                sourceKey = requireNotNull(field<String?>(reader, "resumeKey"))
                pageSegments = field(reader, "segments")
                val gate = requireNotNull(field<NarrationExerciseGate?>(reader, "playbackGate"))
                originalHeard = gate.heardThrough
                expectedBoundary = gate.boundary()
                assertEquals(setOf(olderA, olderB, sourceKey), bookmarkKeys())
                originalBookmark = resumePreferences().all.mapValues { it.value }
                writeAnswer(activity, fixture, correct = false)
                assertTrue(check(activity).performClick())
                assertFalse(saved(fixture).getBoolean("correct"))
                assertTrue(feedback(activity).text.toString().contains(fixture.block.getString("reviewFeedback")))
                assertFalse(feedback(activity).text.toString().contains(fixture.block.getString("explanation")))
            }
            awaitNative(host, "Every actual guide part plays naturally and stops without entering the answer gate", 360_000L) { activity ->
                val reader = controller(activity)
                val guide = field<Any?>(reader, "incorrectFeedback") ?: return@awaitNative false
                val parts = field<List<Any>>(guide, "parts")
                val expected = NarrationSegments.sentences(IncorrectFeedbackNarration.prepare(
                    listOf(feedback(activity).text.toString()), NarrationLanguage.SPANISH, true))
                assertEquals("The visible current guide is not shortened or replaced", expected, parts.map { field<String>(it, "text") })
                assertSame(pageSegments, field<List<Any>>(reader, "segments"))
                assertEquals(sourceSegment, field<Int>(reader, "index"))
                assertEquals(sourceOffset, field<Long>(reader, "offset"))
                val gate = requireNotNull(field<NarrationExerciseGate?>(reader, "playbackGate"))
                assertEquals("Wrong-guide audio never credits successful feedback", originalHeard, gate.heardThrough)
                assertFalse(gate.canPlay(pageSegments.lastIndex))
                observeAudio(activity, reader, parts, field(guide, "index"), "guide", guideObserved, guideMasks)
                val player = field<MediaPlayer?>(reader, "player")
                if (!transportChecked && reader.isPlaying && field<Boolean>(reader, "playerReady") &&
                    player != null && player.duration > 2400 && player.currentPosition in 1401..(player.duration - 600)) {
                    val part = field<Int>(guide, "index")
                    val audio = requireNotNull(field<File?>(parts[part], "file"))
                    val hash = sha(audio.readBytes())
                    reader.pauseKeepingPosition()
                    val offset = field<Long>(guide, "offset")
                    assertNull("A guide pause releases MediaPlayer", field<MediaPlayer?>(reader, "player"))
                    assertTrue(offset > 1300)
                    reader.seekBy(-1)
                    assertSame(guide, field<Any?>(reader, "incorrectFeedback"))
                    assertEquals(part, field<Int>(guide, "index"))
                    assertEquals(offset - 1000, field<Long>(guide, "offset"))
                    reader.seekBy(1)
                    assertEquals(offset, field<Long>(guide, "offset"))
                    assertEquals(originalBookmark, resumePreferences().all)
                    assertTrue(activity.findViewById<View>(R.id.buttonLessonListen).performClick())
                    assertEquals("Position recovery reuses the same valid recording", hash, sha(audio.readBytes()))
                    result.put("pausedGuideSegment", part).put("pausedGuideOffsetMs", offset)
                        .put("pauseReleasedMediaPlayer", true).put("pairedSeekRestoredExactOffset", true)
                    transportChecked = true
                }
                if (!reader.isPlaying && field<Boolean>(guide, "finished")) {
                    assertEquals("Every current wrong-guide WAV was actually observed playing", parts.indices.toSet(), guideObserved.keys)
                    assertAdvancingAudio(guideObserved)
                    assertTrue("Within one real guide sentence, words progress through at least three observed masks",
                        guideMasks.values.any { it.size >= 3 })
                    assertTrue("The long-guide transport precondition was actually exercised", transportChecked)
                    assertNull(field<MediaPlayer?>(reader, "player"))
                    assertFalse(field<Boolean>(reader, "finished"))
                    assertEquals(originalBookmark, resumePreferences().all)
                    assertTrue("The entire visible wrong feedback is now read", unread(feedback(activity)).isEmpty())
                    captureFeedback(activity, "$location-guide-complete")
                    true
                } else false
            }
        }

        withFixture(fixture) { host ->
            host.onActivity { activity ->
                val reader = controller(activity)
                assertFalse("Restoring a wrong draft does not repeat its audio", reader.isPlaying)
                assertNull(field<Any?>(reader, "incorrectFeedback"))
                assertFalse(saved(fixture).getBoolean("correct"))
                assertEquals(originalBookmark, resumePreferences().all)
                writeAnswer(activity, fixture, correct = true)
                assertTrue(check(activity).performClick())
                assertTrue(saved(fixture).getBoolean("correct"))
                assertTrue(feedback(activity).text.toString().contains(fixture.block.getString("explanation")))
                val gate = requireNotNull(field<NarrationExerciseGate?>(reader, "playbackGate"))
                assertEquals(requireNotNull(gate.feedbackStart(0)).segment, field<Int>(reader, "index"))
                assertEquals("Answering is not listening", originalHeard, gate.heardThrough)
                assertFalse(gate.canPlay(field<Int>(reader, "index") + 1))
                assertTrue(reader.isPlaying)
            }
            var tailObserved = false
            awaitNative(host, "The complete current explanation must play naturally before its following paragraph", 480_000L) { activity ->
                val reader = controller(activity)
                val parts = field<List<Any>>(reader, "segments")
                val gate = requireNotNull(field<NarrationExerciseGate?>(reader, "playbackGate"))
                val boundary = requireNotNull(expectedBoundary)
                val expectedScript = LessonNarrationScript.page(fixture.lesson, 0, NarrationLanguage.SPANISH, true)
                assertEquals(NarrationSegments.sentences(expectedScript.texts), parts.map { field<String>(it, "text") })
                val index = field<Int>(reader, "index")
                observeAudio(activity, reader, parts, index, "success", successObserved, successMasks)
                val player = field<MediaPlayer?>(reader, "player")
                if (reader.isPlaying && field<Boolean>(reader, "playerReady") && player != null && player.currentPosition > 20) {
                    assertFalse("Kokoro must not silently fall back to Android TTS", field<Boolean>(parts[index], "usedAndroidTts"))
                    if (index == parts.lastIndex) {
                        assertTrue("Only naturally heard full successful feedback permits the tail", gate.heardThrough >= boundary.endExclusive)
                        assertNull(gate.boundary())
                        tailObserved = true
                    }
                }
                if (!reader.isPlaying && field<Boolean>(reader, "finished")) {
                    assertTrue("The following paragraph really played", tailObserved)
                    assertEquals("Every success sentence and the subsequent paragraph was actually played",
                        (boundary.start..parts.lastIndex).toSet(), successObserved.keys)
                    assertAdvancingAudio(successObserved)
                    assertTrue("Words advance progressively inside an actual explanation sentence",
                        successMasks.filterKeys { it in boundary.start until boundary.endExclusive }.values.any { it.size >= 3 })
                    if (location == 0) {
                        val slashSegment = (boundary.start until boundary.endExclusive).single { candidate ->
                            field<String>(parts[candidate], "text").contains("9 / 2")
                        }
                        val slashAudio = successObserved.getValue(slashSegment)
                        assertTrue("The exact formerly unaligned reparto sentence now has real audio-bound timing",
                            slashAudio.getInt("observedAlignmentWordCount") > 0)
                        assertEquals(slashAudio.getString("sha256"), slashAudio.getString("alignmentAudioSha256"))
                        assertTrue("The exact 9 / 2 sentence advances through at least three real native word masks",
                            (successMasks[slashSegment]?.size ?: 0) >= 3)
                        assertTrue("The exact regression retains source word ranges and model-derived sample times",
                            slashAudio.getJSONArray("alignmentWordTimings").length() > 0)
                        result.put("specificContextualSlash", JSONObject(slashAudio.toString()))
                    }
                    assertTrue(unread(feedback(activity)).isEmpty())
                    assertNull(field<MediaPlayer?>(reader, "player"))
                    assertEquals(setOf(olderA, olderB, sourceKey), bookmarkKeys())
                    assertTrue("Finished pages retain exactly one bounded slot", requireNotNull(store.restore(sourceKey, parts.size)).finished)
                    captureFeedback(activity, "$location-success-complete")
                    true
                } else false
            }
        }
        assertEquals("Actual source bytes including answers and metadata remain unchanged", fixture.sourceSha, sha(sourceBytes(fixture.path)))
        result.put("guideAudio", JSONArray(guideObserved.values.toList())).put("successAudio", JSONArray(successObserved.values.toList()))
            .put("guideWordMasks", masksJson(guideMasks)).put("successWordMasks", masksJson(successMasks))
            .put("threePositions", JSONArray(bookmarkKeys().sorted())).put("wrongDraftRestoredSilently", true)
            .put("wrongGuideCannotGrantSuccess", true).put("allCurrentExplanationSentencesNaturallyPlayed", true)
            .put("evidenceLevel", "Real LDPlayer Kokoro WAV playback, PCM and native presentation; no human listening assessment")
        File(evidence(), "$location-passed.json").writeText(result.toString(2))
    }

    /** Sampling reads the actual player's position and native mask; it does not drive either. */
    private fun observeAudio(activity: LessonActivity, reader: LessonNarrationController, parts: List<Any>, index: Int,
                             phase: String, observed: MutableMap<Int, JSONObject>, masks: MutableMap<Int, MutableSet<String>>) {
        val player = field<MediaPlayer?>(reader, "player") ?: return
        if (!reader.isPlaying || !field<Boolean>(reader, "playerReady")) return
        // Snapshot the native decoder before any WAV reads, hashing, metadata
        // retrieval or bitmap I/O. It can naturally finish while that work runs;
        // its completion callback then waits for this main-thread sample to end.
        val sampledPosition = player.currentPosition
        val sampledPlaying = player.isPlaying
        if (!sampledPlaying || sampledPosition <= 20) return
        val part = parts[index]
        assertFalse("Every observed part is Kokoro", field<Boolean>(part, "usedAndroidTts"))
        val file = requireNotNull(field<File?>(part, "file"))
        assertEquals("Each WAV uses the existing cache identity for its exact spoken text",
            KokoroNarrationCache.key(NarrationLanguage.SPANISH.tag, NarrationLanguage.SPANISH.defaultVoice,
                field<String>(part, "text")), file.nameWithoutExtension)
        if (index !in observed) observed[index] = inspectWav(file).put("segment", index).put("spokenText", field<String>(part, "text"))
            .put("observedPositionMs", sampledPosition).put("nativePlayerPlaying", sampledPlaying)
            .put("lastObservedPositionMs", sampledPosition).put("advancingNativeSamples", 0).put("wordMaskSamples", JSONArray())
        val previousPosition = observed.getValue(index).getInt("lastObservedPositionMs")
        if (sampledPosition > previousPosition) observed.getValue(index).put("advancingNativeSamples",
            observed.getValue(index).getInt("advancingNativeSamples") + 1)
        observed.getValue(index).put("lastObservedPositionMs", sampledPosition)
        val model = field<LessonReadingProgressModel>(activity, "readingProgress")
        val words = field<Any>(model, "words")
        val identity = if (phase == "guide") field<Any?>(activity, "incorrectReadingPresentation")
            ?.let { field<String?>(it, "identity") } else field<String>(reader, "activePageIdentity")
        val page = identity?.let { field<Map<String, Any>>(words, "pages")[it] }
        val alignment = page?.let { field<Map<Int, NarrationWordAlignment>>(it, "alignments")[index] }
        if (alignment != null) {
            assertEquals("Word timings belong to the exact currently playing immutable WAV",
                observed.getValue(index).getString("sha256"), alignment.audioSha256)
            assertTrue("The native model supplies real word timings", alignment.words.isNotEmpty())
            observed.getValue(index).put("observedAlignmentWordCount", alignment.words.size)
                .put("alignmentAudioSha256", alignment.audioSha256)
            if (phase == "success" && field<String>(part, "text").contains("9 / 2") &&
                !observed.getValue(index).has("alignmentWordTimings")) {
                observed.getValue(index).put("alignmentSampleCount", alignment.sampleCount)
                    .put("alignmentWordTimings", JSONArray(alignment.words.map { timing ->
                        JSONObject().put("sourceStart", timing.start).put("sourceEndExclusive", timing.endExclusive)
                            .put("startSample", timing.startSample).put("endSample", timing.endSample)
                    }))
            }
        }
        val current = unread(feedback(activity))
        val text = feedback(activity).text
        if (alignment != null && current.isNotEmpty() && current.any { it.start > 10 && it.start < text.length - 10 }) {
            val mask = current.joinToString(";") { "${it.start}:${it.endExclusive}" }
            val observedMasks = masks.getOrPut(index) { linkedSetOf() }
            if (mask !in observedMasks && observedMasks.size < 8) {
                observedMasks += mask
                observed.getValue(index).getJSONArray("wordMaskSamples").put(JSONObject()
                    .put("observedPositionMs", sampledPosition).put("unreadRanges", mask)
                    .put("alignmentAudioSha256", alignment.audioSha256))
                if (observedMasks.size == 3) captureFeedback(activity, "$phase-$index-progressive")
            }
        }
    }

    private fun assertAdvancingAudio(observed: Map<Int, JSONObject>) {
        observed.forEach { (index, sample) ->
            assertTrue("Segment $index was sampled as actually playing", sample.getBoolean("nativePlayerPlaying"))
            assertTrue("Segment $index has multiple independently observed advancing native positions",
                sample.getInt("advancingNativeSamples") > 0)
            assertTrue(sample.getInt("lastObservedPositionMs") > sample.getInt("observedPositionMs"))
        }
    }

    private fun masksJson(masks: Map<Int, Set<String>>) = JSONObject().apply {
        masks.forEach { (segment, value) -> put(segment.toString(), JSONArray(value.toList())) }
    }
    private data class Fixture(val id: String, val path: String, val sourceLesson: String, val sourceSha: String,
                               val page: Int, val number: Int, val block: JSONObject, val lesson: JSONObject)
    private fun fixture(index: Int): Fixture {
        val (path, page, number) = LOCATIONS[index]
        val bytes = sourceBytes(path)
        val source = JSONObject(String(bytes, Charsets.UTF_8))
        val block = source.getJSONArray("pages").getJSONObject(page - 1).getJSONArray("blocks").getJSONObject(number - 1)
        assertEquals("inline_fill_code", block.getString("type"))
        assertTrue(block.getString("reviewFeedback").isNotBlank())
        assertTrue(block.getString("explanation").length > 100)
        val id = source.getString("id") + "__current_feedback_qa_" + session.replace("-", "")
        val lesson = JSONObject().put("id", id).put("title", "").put("language", "es").put("pages", JSONArray()
            .put(JSONObject().put("blocks", JSONArray().put(JSONObject(block.toString()))
                .put(JSONObject().put("type", "paragraph").put("text", "La lectura continúa después de escuchar toda la explicación.")))))
        return Fixture(id, path, source.getString("id"), sha(bytes), page, number, block, lesson)
    }
    private fun sourceBytes(path: String) = context.assets.open("content/es/programming/cpp_fundamentals/$path").use { it.readBytes() }
    private fun writeAnswer(activity: LessonActivity, fixture: Fixture, correct: Boolean) {
        val values = fixture.block.getJSONArray("answers")
        val inputs = descendants(content(activity)).filterIsInstance<EditText>()
        assertEquals(values.length(), inputs.size)
        inputs.forEachIndexed { index, input -> input.setText(if (!correct && index == 0) "incorrecto_qa" else values.getString(index)) }
    }
    private fun check(activity: LessonActivity) = descendants(content(activity)).filterIsInstance<Button>().single {
        it.text.toString() in listOf(activity.getString(R.string.exercise_check), activity.getString(R.string.exercise_correct))
    }
    private fun feedback(activity: LessonActivity) = descendants(content(activity)).filterIsInstance<ReadingTextView>().single {
        it.currentTextColor in setOf(Color.rgb(240, 150, 150), Color.rgb(240, 166, 166), Color.rgb(180, 220, 190))
    }
    private fun unread(view: ReadingTextView) = field<List<ReadingTextRange>>(view.readingInk, "unreadRanges").toList()
    private fun captureFeedback(activity: LessonActivity, name: String) {
        val view = feedback(activity)
        assertTrue(view.width > 0 && view.height > 0)
        val bitmap = Bitmap.createBitmap(view.width, view.height, Bitmap.Config.ARGB_8888)
        try {
            val canvas = Canvas(bitmap); canvas.drawColor(Color.rgb(28, 31, 36)); view.draw(canvas)
            File(evidence(), "$name.png").outputStream().use { assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) }
            File(evidence(), "$name.json").writeText(JSONObject().put("text", view.text.toString()).put("width", view.width)
                .put("height", view.height).put("unreadRanges", JSONArray(unread(view).map { JSONArray(listOf(it.start, it.endExclusive)) }))
                .put("kind", "Native feedback TextView drawn at its real layout bounds during observed playback; not a whole-screen screenshot")
                .toString(2))
        } finally { bitmap.recycle() }
    }

    private fun withFixture(fixture: Fixture, body: (ActivityScenario<LessonActivity>) -> Unit) {
        NarrationSettingsRepository.saveEnabled(context, false)
        ProgressRepository.saveCurrentPage(context, "programming", fixture.sourceLesson, 0)
        val intent = Intent(context, LessonActivity::class.java).putExtra("routeId", "programming")
            .putExtra("courseId", "cpp_fundamentals").putExtra("unitId", fixture.sourceLesson.removePrefix("cpp_fundamentals__").substringBefore("__"))
            .putExtra("lessonId", fixture.sourceLesson).putExtra("lessonNumber", 1).putExtra("lessonTotal", 1)
        ActivityScenario.launch<LessonActivity>(intent).use { host ->
            activeScenario = host
            var stableSince = 0L
            var instance = 0
            awaitNative(host, "The real lesson host must be stable before exclusive QA injection", 15_000L) { activity ->
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
            awaitNative(host, "The unchanged inline block finishes native layout", 10_000L) { activity ->
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
    private fun saved(fixture: Fixture) = requireNotNull(ProgressRepository.getExerciseState(context, fixture.id, 0, 0))
    private fun bookmarkKeys() = (0 until 3).mapNotNull { resumePreferences().getString("key_$it", null) }.toSet()
    private fun resumePreferences() = context.getSharedPreferences("lesson_narration_resume_bounded_v1", Context.MODE_PRIVATE)
    private fun controller(activity: LessonActivity): LessonNarrationController = field(activity, "narrationController")
    private fun content(activity: LessonActivity): ViewGroup = activity.findViewById(R.id.lessonContent)
    private fun descendants(view: View): List<View> = buildList { add(view); if (view is ViewGroup) repeat(view.childCount) { addAll(descendants(view.getChildAt(it))) } }
    private fun preferenceFiles() = File(context.applicationInfo.dataDir, "shared_prefs").listFiles().orEmpty().filter { it.name.endsWith(".xml") }.map { it.name.removeSuffix(".xml") }.toSet()
    @Suppress("UNCHECKED_CAST") private fun <T> field(target: Any, name: String): T = target.javaClass.getDeclaredField(name).apply { isAccessible = true }.get(target) as T
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
    private fun evidence() = File(requireNotNull(context.getExternalFilesDir(null)), "current-inline-feedback-narration-qa/$session").apply { mkdirs() }
    private fun sha(bytes: ByteArray) = MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it) }
    private fun inspectWav(file: File): JSONObject {
        assertTrue("Every observed part has an actual WAV", file.isFile && file.length() > 44L)
        val bytes = file.readBytes()
        assertEquals("RIFF", String(bytes, 0, 4, Charsets.US_ASCII))
        assertEquals("WAVE", String(bytes, 8, 4, Charsets.US_ASCII))
        assertTrue("Real PCM is nonzero", bytes.drop(44).any { it.toInt() != 0 })
        val retriever = MediaMetadataRetriever()
        val duration = try {
            retriever.setDataSource(file.absolutePath)
            requireNotNull(retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION)).toLong()
        } finally { retriever.release() }
        assertTrue(duration > 0L)
        return JSONObject().put("file", file.absolutePath).put("bytes", bytes.size).put("sha256", sha(bytes)).put("durationMs", duration)
    }

    companion object {
        private val LOCATIONS = listOf(
            Triple("objetos_y_estado/lesson_enteros_y_aproximaciones.json", 8, 7),
            Triple("objetos_y_estado/lesson_enteros_y_aproximaciones.json", 4, 4),
            Triple("objetos_y_estado/lesson_declarar_e_inicializar.json", 3, 5),
            Triple("objetos_y_estado/lesson_estado_asignacion_y_copia.json", 6, 5),
            Triple("objetos_y_estado/lesson_nombres_con_intencion.json", 4, 5),
            Triple("objetos_y_estado/lesson_conversiones_y_deduccion.json", 4, 6),
            Triple("objetos_y_estado/lesson_cadenas_y_texto.json", 4, 5),
            Triple("objetos_y_estado/lesson_caracteres_y_valores_logicos.json", 3, 4),
            Triple("programas_y_ejecucion/lesson_texto_secuencia_y_formato.json", 5, 2))
        private val QA_PACKAGES = setOf("com.learngraphicsprogramming.app.curricularqa", "com.learngraphicsprogramming.app.realdownloadsqa")
        private val KNOWN_PREFERENCES = setOf("learning_progress", "navigation_state", "lesson_narration_settings", "lesson_narration_resume_bounded_v1",
            "attempt_reserve_seconds_test", "premium_state", "ui_language_settings", "appearance_settings", "app_settings")
    }
}
