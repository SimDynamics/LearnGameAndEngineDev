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
        store.begin(olderA, NarrationResumePoint(1, 2345, finished = true))
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
                    assertFalse(check(activity).isEnabled)
                    tagged(activity, "cpp_run").performClick()
                }
                await(host, "The exact checked source executes on native Clang", 120_000L) { activity -> tagged(activity, "cpp_run").isEnabled }
                var shown = ""
                host.onActivity { activity ->
                    assertEquals(runs[index], (tagged(activity, "cpp_stdout") as TextView).text.toString())
                    assertTrue(check(activity).isEnabled)
                    check(activity).performClick()
                    shown = result(activity).text.toString()
                    val verdict = activity.getString(if (index == 0 || index == 2) R.string.cpp_output_check_correct else R.string.cpp_output_check_incorrect)
                    val extra = when (index) { 2 -> CppOutputCheckDeviceTest.EXPLANATION; 3 -> CppOutputCheckDeviceTest.GUIDE; else -> null }
                    assertEquals(verdict + (extra?.let { "\n$it" } ?: ""), shown)
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
                observations.put(JSONObject().put("case", index).put("visibleResult", shown).put("expectedSpokenParts", JSONArray(expectedParts))
                    .put("audio", JSONArray(played.values.toList())).put("maskSamples", JSONObject().apply { masks.forEach { (part, ranges) -> put(part.toString(), JSONArray(ranges.toList())) } })
                    .put("pauseReleasedPlayerAndExactSeekRestored", transportChecked && index == 3))
            }
            host.onActivity { activity -> reader(activity).pauseKeepingPosition() }
        }
        saveEvidence("four-real-kokoro-verdicts", JSONObject().put("voice", NarrationLanguage.SPANISH.defaultVoice)
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
            val folder = File(requireNotNull(context.getExternalFilesDir(null)), "cpp-output-check-qa/$session").apply { mkdirs() }
            File(folder, "$name.png").outputStream().use { assertTrue(image.compress(Bitmap.CompressFormat.PNG, 100, it)) }
            saveEvidence(name, JSONObject().put("text", view.text.toString()).put("width", view.width).put("height", view.height)
                .put("unreadRanges", JSONArray(unread(view).map { JSONArray(listOf(it.start, it.endExclusive)) }))
                .put("kind", "Actual Android view Canvas at original bounds sampled during real audio; not a whole-screen screenshot"))
        } finally { image.recycle() }
    }
    private fun unread(view: ReadingTextView): List<ReadingTextRange> = field(view.readingInk, "unreadRanges")
    private fun reader(activity: LessonActivity): LessonNarrationController = field(activity, "narrationController")
    private fun awaitWorkerStop(reason: String, timeout: Long, predicate: () -> Boolean) {
        val end = SystemClock.uptimeMillis() + timeout
        while (SystemClock.uptimeMillis() < end) { if (predicate()) return; SystemClock.sleep(60L) }
        fail(reason)
    }
}
