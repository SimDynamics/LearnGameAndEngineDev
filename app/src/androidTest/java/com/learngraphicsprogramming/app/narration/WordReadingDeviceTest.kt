package com.learngraphicsprogramming.app.narration

import android.app.Activity
import android.app.Application
import android.app.KeyguardManager
import android.content.Context
import android.content.Intent
import android.content.res.ColorStateList
import android.graphics.Bitmap
import android.graphics.Canvas
import android.media.MediaPlayer
import android.os.Build
import android.os.Bundle
import android.os.SystemClock
import android.system.Os
import android.text.Spanned
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.Button
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
import java.io.File
import java.security.MessageDigest
import java.util.UUID
import org.json.JSONArray
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/** Real Kokoro, MediaPlayer and native lesson views; the fixture exists only in memory. */
@RunWith(AndroidJUnit4::class)
class WordReadingDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val preferences = linkedMapOf<String, Map<String, *>>()
    private var preferenceNames = emptySet<String>()
    private var captured = false
    private var scenario: ActivityScenario<LessonActivity>? = null
    private val postResumed = java.util.WeakHashMap<Activity, Long>()
    private val screenGuard = object : Application.ActivityLifecycleCallbacks {
        override fun onActivityCreated(activity: Activity, state: Bundle?) {
            if (activity.packageName == QA_PACKAGE) activity.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        }
        override fun onActivityPostResumed(activity: Activity) { postResumed[activity] = SystemClock.uptimeMillis() }
        override fun onActivityResumed(activity: Activity) {
            if (Build.VERSION.SDK_INT < 29) postResumed[activity] = SystemClock.uptimeMillis()
        }
        override fun onActivityStarted(activity: Activity) = Unit
        override fun onActivityPaused(activity: Activity) = Unit
        override fun onActivityStopped(activity: Activity) = Unit
        override fun onActivitySaveInstanceState(activity: Activity, state: Bundle) = Unit
        override fun onActivityDestroyed(activity: Activity) = Unit
    }

    @Before fun isolateQaAndPreserveOriginalPreferences() {
        assertEquals("Never test against the ordinary application", QA_PACKAGE, context.packageName)
        assertEquals(QA_PACKAGE, BuildConfig.APPLICATION_ID)
        val lock = context.getSystemService(KeyguardManager::class.java)
        assertFalse("Unlock LDPlayer before native visual checks", lock.isDeviceLocked || lock.isKeyguardLocked)
        preferenceNames = preferenceFiles()
        (preferenceNames + KNOWN_PREFERENCES).forEach { name ->
            preferences[name] = context.getSharedPreferences(name, Context.MODE_PRIVATE).all.mapValues { (_, value) ->
                if (value is Set<*>) value.toSet() else value
            }
        }
        captured = true
        (context.applicationContext as Application).registerActivityLifecycleCallbacks(screenGuard)
        assertTrue(context.getSharedPreferences("lesson_narration_resume_bounded_v1", Context.MODE_PRIVATE).edit().clear().commit())
        NarrationSettingsRepository.saveEnabled(context, false)
        NarrationSettingsRepository.saveEngine(context, NarrationEngine.KOKORO)
        NarrationSettingsRepository.saveLanguage(context, NarrationLanguage.SPANISH)
        NarrationSettingsRepository.saveKokoroVoice(context, "ef_dora")
        NarrationSettingsRepository.saveRate(context, 1f)
        NarrationSettingsRepository.saveControlsVisible(context, true)
        PremiumRepository.setVerifiedPremium(context)
    }

    @After fun closeReaderBeforeRestoringEveryPreference() {
        if (!captured) return
        var closed: LessonNarrationController? = null
        try {
            scenario?.onActivity { closed = controller(it).also { reader -> reader.stop() } }
            scenario?.close()
            instrumentation.waitForIdleSync()
            closed?.let { reader -> await("The existing audio worker closes before restoring preferences", 15_000) {
                !field<Thread>(reader, "workerThread").isAlive
            } }
        } finally {
            (context.applicationContext as Application).unregisterActivityLifecycleCallbacks(screenGuard)
            (preferenceFiles() + preferences.keys).forEach { name ->
                if (name !in preferenceNames) context.deleteSharedPreferences(name)
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
                            else -> error("Unsupported preference $name/$key")
                        }
                    }
                    assertTrue(editor.commit())
                    assertEquals("Student progress and settings are restored", preferences[name], target.all)
                }
            }
            assertEquals(preferenceNames, preferenceFiles())
        }
    }

    @Test fun actualKokoroWordTimesDriveMixedColorsAndPreservePauseResumeSeekAndRestart() {
        val fixture = fixture()
        val authored = fixture.toString()
        val id = fixture.getString("id")
        ProgressRepository.saveCurrentPage(context, ROUTE, SOURCE_LESSON, 0)
        val host = ActivityScenario.launch<LessonActivity>(Intent(context, LessonActivity::class.java)
            .putExtra("routeId", ROUTE).putExtra("courseId", "math_course_01")
            .putExtra("unitId", "estructura_y_divisibilidad").putExtra("lessonId", SOURCE_LESSON)
            .putExtra("lessonNumber", 1).putExtra("lessonTotal", 1)).also { scenario = it }
        await("The ordinary lesson is focused and stable before a single fixture injection", 15_000) {
            var ready = false
            host.onActivity { activity ->
                ready = postResumed[activity]?.let { SystemClock.uptimeMillis() - it >= 700 } == true &&
                    activity.hasWindowFocus() && content(activity).width > 0 && !content(activity).isLayoutRequested
            }
            ready
        }
        host.onActivity { activity ->
            setField(activity, "currentLessonId", id)
            setField(activity, "lessonJson", JSONObject(authored))
            setField(activity, "totalPages", 2)
            setField(activity, "currentPage", 0)
            listOf("pageViewCache", "pageRequiredExercises", "pageCompletedExercises").forEach {
                field<MutableMap<Int, Any>>(activity, it).clear()
            }
            invoke(activity, "renderCurrentPage")
        }
        awaitLayout(host, id)
        host.onActivity { activity ->
            NarrationSettingsRepository.saveEnabled(activity, true)
            invoke(activity, "renderCurrentPage")
        }
        awaitLayout(host, id)
        lateinit var paragraph: ReadingTextView
        lateinit var originalFacts: List<TextFact>
        host.onActivity { activity ->
            paragraph = texts(activity).single { it.text.toString() == PROSE } as ReadingTextView
            assertTrue(paragraph.safeSupportsWordReading)
            originalFacts = facts(content(activity))
            assertEquals("Every original paragraph word starts pending", allWords().size, pending(activity).size)
            assertTrue(activity.findViewById<View>(R.id.buttonLessonListen).performClick())
        }
        await("The real immutable Kokoro WAV has model-derived word alignment", 180_000) {
            var ready = false
            host.onActivity { activity -> ready = controller(activity).isPlaying && alignment(activity) != null }
            ready
        }
        // Alignment may be calculated after playback starts. Restart explicitly to observe a
        // complete first pass with the now-available metadata; never fabricate playback events.
        host.onActivity { controller(it).restart() }
        await("Actual playback produces read and pending words inside this same paragraph", 35_000) {
            var ready = false
            host.onActivity { activity ->
                val unread = pending(activity).size
                val player = field<MediaPlayer?>(controller(activity), "player")
                ready = controller(activity).isPlaying && field<Boolean>(controller(activity), "playerReady") &&
                    player != null && player.currentPosition >= 1_500 && unread >= 8 && allWords().size - unread >= 3
            }
            ready
        }
        var beforePause = 0L
        var paused = 0L
        var segmentIndex = 0
        lateinit var pendingAtPause: List<ReadingTextRange>
        lateinit var actualAlignment: NarrationWordAlignment
        lateinit var audio: File
        lateinit var hash: String
        var modified = 0L
        lateinit var initialAudioIdentity: List<Long>
        lateinit var cacheKey: String
        var initialGenerationEvents = 0
        var jumpSeconds = 0
        lateinit var skipped: ReadingTextRange
        host.onActivity { activity ->
            val reader = controller(activity)
            val player = requireNotNull(field<MediaPlayer?>(reader, "player"))
            actualAlignment = requireNotNull(alignment(activity))
            segmentIndex = field(reader, "index")
            audio = requireNotNull(field<File?>(field<List<Any>>(reader, "segments")[segmentIndex], "file"))
            hash = sha256(audio)
            modified = audio.lastModified()
            assertEquals("Word timings describe the exact audio file currently decoded", hash, actualAlignment.audioSha256)
            assertEquals("Lossless 24 kHz samples are counted exactly", audio.length(), actualAlignment.sampleCount * 2L + 44L)
            beforePause = player.currentPosition.toLong()
            reader.pauseKeepingPosition()
            paused = field(reader, "offset")
            assertTrue("Pause captures the actual source-audio position", paused in beforePause..(beforePause + 120))
            assertNull("Pause releases MediaPlayer and its file/decoder resources", field<MediaPlayer?>(reader, "player"))
            assertFalse(field(reader, "playerReady"))
            pendingAtPause = pending(activity).toList()
            val expectedPending = actualAlignment.words.mapIndexedNotNull { ordinal, word ->
                allWords()[ordinal].takeIf { word.startSample > paused * 24L }
            }
            assertEquals("Word colors follow the real source-audio position and exact model word starts",
                expectedPending, pendingAtPause)
            assertEquals(pendingAtPause, field<List<ReadingTextRange>>(paragraph, "unreadRanges"))
            val key = requireNotNull(field<String?>(reader, "resumeKey"))
            assertEquals(paused, requireNotNull(NarrationResumeStore(context)
                .restore(key, field<List<Any>>(reader, "segments").size)).positionMs)
            val source = field<List<Any>>(reader, "segments")[segmentIndex]
            val spoken = field<String>(source, "text")
            initialAudioIdentity = audioIdentity(audio)
            cacheKey = KokoroNarrationCache.key("es", "ef_dora", spoken)
            initialGenerationEvents = generationEvents(cacheKey)
            assertEquals("Spoken and displayed words remain the original curriculum text", allWords().size, actualAlignment.words.size)
            assertTrue("The spoken segment retains the original ordinary Spanish wording", spoken.contains("Esta lectura"))
            val remaining = actualAlignment.sampleCount / 24L - paused
            jumpSeconds = (remaining / 3_000L).toInt().coerceIn(2, 6)
            val candidate = actualAlignment.words.first { it.startSample / 24L > paused + 350 &&
                it.endSample / 24L < paused + jumpSeconds * 1_000L - 350 }
            val ordinal = actualAlignment.words.indexOf(candidate)
            skipped = allWords()[ordinal]
            assertTrue("The upcoming skipped word has never been heard", skipped in pendingAtPause)
            assertFacts(originalFacts)
        }
        SystemClock.sleep(250)
        host.onActivity { activity ->
            assertEquals("A released paused reader does not advance words", pendingAtPause, pending(activity))
            assertEquals(paused, field<Long>(controller(activity), "offset"))
            assertMixedPixels(paragraph)
            capture("same-paragraph-read-and-pending")
            saveParagraph(paragraph, "same-paragraph-native-canvas")
            assertTrue(activity.findViewById<View>(R.id.buttonLessonListen).performClick())
        }
        await("The original reader resumes the exact saved source-audio position", 30_000) {
            var ready = false
            host.onActivity { activity ->
                val reader = controller(activity)
                val player = field<MediaPlayer?>(reader, "player")
                ready = reader.isPlaying && field<Boolean>(reader, "playerReady") && player != null &&
                    player.currentPosition >= paused && player.currentPosition <= paused + 1_500
            }
            ready
        }
        host.onActivity { activity ->
            val reader = controller(activity)
            assertEquals(segmentIndex, field<Int>(reader, "index"))
            reader.pauseKeepingPosition()
            assertSameCachedAudio(audio, hash, initialAudioIdentity, cacheKey, initialGenerationEvents)
            reader.seekBy(jumpSeconds)
            assertNull(field<MediaPlayer?>(reader, "player"))
            assertTrue("A forward seek does not mark skipped words as heard", skipped in pending(activity))
            assertTrue(activity.findViewById<View>(R.id.buttonLessonListen).performClick())
        }
        var seekOffset = 0L
        host.onActivity { seekOffset = field(controller(it), "offset") }
        await("The original MediaPlayer resumes after the explicit seek", 30_000) {
            var ready = false
            host.onActivity { activity ->
                val reader = controller(activity)
                ready = reader.isPlaying && field<Boolean>(reader, "playerReady") &&
                    requireNotNull(field<MediaPlayer?>(reader, "player")).currentPosition >= seekOffset + 250
            }
            ready
        }
        host.onActivity { activity ->
            val reader = controller(activity)
            reader.pauseKeepingPosition()
            assertTrue("Even after real playback beyond the jump, the skipped word stays gray", skipped in pending(activity))
            val beforeRewind = field<Long>(reader, "offset")
            reader.seekBy(-jumpSeconds)
            assertTrue(field<Long>(reader, "offset") < beforeRewind)
            assertTrue("Rewind is a position change, not proof that a skipped word was heard", skipped in pending(activity))
            assertSameCachedAudio(audio, hash, initialAudioIdentity, cacheKey, initialGenerationEvents)
            assertFacts(originalFacts)
            reader.restart()
            reader.pauseKeepingPosition()
            assertEquals("An explicit restart restores every word to pending", allWords().toSet(), pending(activity).toSet())
            assertNull(field<MediaPlayer?>(reader, "player"))
            assertTrue("Restart begins from the source audio origin", field<Long>(reader, "offset") <= 120)
            assertSameCachedAudio(audio, hash, initialAudioIdentity, cacheKey, initialGenerationEvents)
            assertEquals("The authored JSON is byte-identical", authored, field<JSONObject>(activity, "lessonJson").toString())
            assertEquals("Reading never solves an exercise", 0, field<Int>(activity, "completedExercises"))
            File(evidence(), "actual-word-playback.json").writeText(JSONObject()
                .put("package", activity.packageName).put("engine", "KOKORO").put("voice", "ef_dora")
                .put("audioSha256", hash).put("sampleCount", actualAlignment.sampleCount)
                .put("beforePauseMillis", beforePause).put("savedPauseMillis", paused)
                .put("unreadWordsAtPause", pendingAtPause.size).put("totalWords", allWords().size)
                .put("wordTimes", JSONArray(actualAlignment.words.map { word -> JSONArray().put(word.start)
                    .put(word.endExclusive).put(word.startSample).put(word.endSample) }))
                .put("skippedWord", PROSE.substring(skipped.start, skipped.endExclusive))
                .put("forwardSeekSeconds", jumpSeconds).put("seekOffsetMillis", seekOffset)
                .put("pauseReleasesPlayer", true).put("sourceJsonUnchanged", true)
                .put("cachedAudioModifiedMillisAtPause", modified)
                .put("cachedAudioAccessModifiedMillisAfterResume", audio.lastModified())
                .put("existingCacheAccessTimestampPolicyPreserved", true)
                .put("sourceAudioInode", initialAudioIdentity[1])
                .put("generationEventsAtPause", initialGenerationEvents)
                .put("generationEventsAfterResume", generationEvents(cacheKey))
                .put("sameCacheReused", true).toString(2))
            NarrationSettingsRepository.saveEnabled(activity, false)
            reader.settingsChanged()
            assertNull("Disabling narration releases the optional word-alignment service", field<Any?>(reader, "wordAlignmentService"))
            assertNull("Disabling narration releases MediaPlayer", field<MediaPlayer?>(reader, "player"))
            assertTrue("Disabled narration clears every visual word mask", field<List<ReadingTextRange>>(paragraph, "unreadRanges").isEmpty())
            val disabled = paragraphBitmap(paragraph)
            try {
                paragraph.setUnreadWordRanges(null, 0)
                val ordinary = paragraphBitmap(paragraph)
                try { assertTrue("Disabled narration draws the ordinary unchanged paragraph", disabled.sameAs(ordinary)) }
                finally { ordinary.recycle() }
            } finally { disabled.recycle() }
        }
        assertEquals(authored, fixture.toString())
    }

    private fun fixture(): JSONObject {
        val published = LessonRepository.loadLessonJson(context, ROUTE,
            "math_geometria_plana__fundamentos_geometricos__del_trazo_a_la_figura_geometrica")
            .getJSONArray("pages").getJSONObject(0).getJSONArray("blocks").getJSONObject(4)
        return JSONObject().put("id", "math_course_01__estructura_y_divisibilidad__words_qa_${UUID.randomUUID().toString().replace("-", "")}")
            .put("title", "").put("pages", JSONArray()
                .put(JSONObject().put("blocks", JSONArray()
                    .put(JSONObject().put("type", "paragraph").put("text", PROSE))
                    .put(JSONObject().put("type", "paragraph").put("text", "La igualdad \\(1/2 = 2/4\\) conserva sus fracciones y sus símbolos."))
                    .put(JSONObject().put("type", "code").put("language", "cpp").put("code", "#include <iostream>\nint main() {\n    std::cout << 2;\n}\n"))
                    .put(JSONObject(published.toString()))))
                .put(JSONObject().put("blocks", JSONArray().put(JSONObject().put("type", "paragraph")
                    .put("text", "Esta segunda página conserva su posición independiente.")))))
    }

    private data class TextFact(val view: TextView, val text: CharSequence, val spans: List<List<Any>>,
                                val width: Int, val height: Int, val colors: ColorStateList,
                                val clickable: Boolean, val enabled: Boolean)
    private fun facts(root: View): List<TextFact> = descendants(root).filterIsInstance<TextView>().map {
        TextFact(it, it.text, spans(it.text), it.width, it.height, it.textColors, it.isClickable, it.isEnabled)
    }
    private fun assertFacts(values: List<TextFact>) = values.forEach {
        assertSame(it.text, it.view.text)
        assertEquals(it.spans, spans(it.view.text))
        assertEquals(it.width, it.view.width)
        assertEquals(it.height, it.view.height)
        assertEquals(it.clickable, it.view.isClickable)
        assertEquals(it.enabled, it.view.isEnabled)
        if (it.view is Button || it.view is EditText || it.clickable)
            assertEquals("Control colors remain unchanged", it.colors, it.view.textColors)
    }
    private fun spans(value: CharSequence): List<List<Any>> = (value as? Spanned)?.let { text ->
        text.getSpans(0, text.length, Any::class.java).map {
            listOf(it, text.getSpanStart(it), text.getSpanEnd(it), text.getSpanFlags(it))
        }
    }.orEmpty()
    private fun assertMixedPixels(view: ReadingTextView) {
        val bitmap = Bitmap.createBitmap(view.width, view.height, Bitmap.Config.ARGB_8888)
        try {
            view.draw(Canvas(bitmap))
            val gray = field<Int>(view, "unreadColor")
            var normalPixels = 0
            var pendingPixels = 0
            for (y in 0 until bitmap.height) for (x in 0 until bitmap.width) {
                when (bitmap.getPixel(x, y)) {
                    view.currentTextColor -> normalPixels++
                    gray -> pendingPixels++
                }
            }
            assertTrue("The same native paragraph contains normal read glyphs", normalPixels > 20)
            assertTrue("The same native paragraph also contains gray pending glyphs", pendingPixels > 20)
        } finally { bitmap.recycle() }
    }
    private fun alignment(activity: LessonActivity): NarrationWordAlignment? {
        val model = field<LessonReadingProgressModel>(activity, "readingProgress")
        val words = field<Any>(model, "words")
        val page = field<Map<String, Any>>(words, "pages")[identity(activity)] ?: return null
        return field<Map<Int, NarrationWordAlignment>>(page, "alignments")[field<Int>(controller(activity), "index")]
    }
    private fun pending(activity: LessonActivity): List<ReadingTextRange> =
        field<LessonReadingProgressModel>(activity, "readingProgress").unreadWords(identity(activity))[0].orEmpty()
    private fun identity(activity: LessonActivity) = "$ROUTE/${field<String>(activity, "currentLessonId")}/${field<Int>(activity, "currentPage")}"
    private fun allWords() = Regex("\\p{L}[\\p{L}\\p{M}]*").findAll(PROSE)
        .map { ReadingTextRange(it.range.first, it.range.last + 1) }.toList()
    private fun controller(activity: LessonActivity): LessonNarrationController = field(activity, "narrationController")
    private fun content(activity: LessonActivity): ViewGroup = activity.findViewById(R.id.lessonContent)
    private fun texts(activity: LessonActivity) = descendants(content(activity)).filterIsInstance<TextView>()
    private fun descendants(view: View): List<View> = listOf(view) + if (view is ViewGroup)
        (0 until view.childCount).flatMap { descendants(view.getChildAt(it)) } else emptyList()
    @Suppress("UNCHECKED_CAST") private fun <T> field(instance: Any, name: String): T =
        instance.javaClass.getDeclaredField(name).apply { isAccessible = true }.get(instance) as T
    private fun setField(instance: Any, name: String, value: Any) = instance.javaClass.getDeclaredField(name)
        .apply { isAccessible = true }.set(instance, value)
    private fun invoke(instance: Any, name: String) = instance.javaClass.getDeclaredMethod(name).apply { isAccessible = true }.invoke(instance)
    private fun await(message: String, timeout: Long, ready: () -> Boolean) {
        val deadline = SystemClock.uptimeMillis() + timeout
        while (SystemClock.uptimeMillis() < deadline) {
            instrumentation.waitForIdleSync()
            if (ready()) return
            SystemClock.sleep(40)
        }
        fail(message)
    }
    private fun awaitLayout(host: ActivityScenario<LessonActivity>, id: String) = await("The native fixture finishes layout", 8_000) {
        var ready = false
        host.onActivity { activity ->
            assertEquals("A replaced fixture must not be reinjected", id, field<String>(activity, "currentLessonId"))
            ready = content(activity).width > 0 && !content(activity).isLayoutRequested
        }
        ready
    }
    private fun capture(name: String) {
        val bitmap = requireNotNull(instrumentation.uiAutomation.takeScreenshot())
        try { File(evidence(), "$name.png").outputStream().use { assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) } }
        finally { bitmap.recycle() }
    }
    private fun saveParagraph(view: View, name: String) {
        val bitmap = Bitmap.createBitmap(view.width, view.height, Bitmap.Config.ARGB_8888)
        try {
            view.draw(Canvas(bitmap))
            File(evidence(), "$name.png").outputStream().use { assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) }
        } finally { bitmap.recycle() }
    }
    private fun evidence() = File(requireNotNull(context.getExternalFilesDir(null)), "word-reading-qa").apply { mkdirs() }
    private fun sha256(file: File) = MessageDigest.getInstance("SHA-256").digest(file.readBytes()).joinToString("") { "%02x".format(it) }
    private fun audioIdentity(file: File): List<Long> = Os.stat(file.absolutePath).let { listOf(it.st_dev, it.st_ino, it.st_size) }
    private fun generationEvents(hash: String): Int {
        val file = File(context.filesDir, KokoroNarrationDiagnostics.EVENTS_FILE)
        if (!file.isFile) return 0
        return file.readLines(Charsets.UTF_8).count { line ->
            runCatching { JSONObject(line) }.getOrNull()?.let {
                it.optString("event") == "Page sentence generated" && it.optString("hash") == hash
            } == true
        }
    }
    private fun assertSameCachedAudio(audio: File, hash: String, identity: List<Long>, key: String, generations: Int) {
        assertEquals("Playback reuses exactly the same audio bytes", hash, sha256(audio))
        assertEquals("Playback retains the same cached inode and file size", identity, audioIdentity(audio))
        assertEquals("Pause, resume and seek generate no additional sentence audio", generations, generationEvents(key))
        // Cache.lookup deliberately touches lastModified to implement its existing retention
        // policy; a changed access timestamp is not evidence of new synthesis or new bytes.
    }
    private fun paragraphBitmap(view: View) = Bitmap.createBitmap(view.width, view.height, Bitmap.Config.ARGB_8888)
        .also { view.draw(Canvas(it)) }
    private fun preferenceFiles() = File(context.applicationInfo.dataDir, "shared_prefs").listFiles().orEmpty()
        .filter { it.name.endsWith(".xml") }.map { it.name.removeSuffix(".xml") }.toSet()

    companion object {
        private const val QA_PACKAGE = "com.learngraphicsprogramming.app.curricularqa"
        private const val ROUTE = "math_physics"
        private const val SOURCE_LESSON = "math_course_01__estructura_y_divisibilidad__divisibilidad_y_primos"
        private const val PROSE = "Esta lectura conserva cada palabra original mientras la voz avanza con calma por el mismo párrafo y permite observar cómo unas palabras recuperan su color habitual y otras siguen pendientes sin alterar las fórmulas los controles ni el contenido de la página."
        private val KNOWN_PREFERENCES = setOf("learning_progress", "navigation_state", "lesson_narration_settings",
            "app_settings", "appearance_settings", "ui_language_settings", "premium_state", "lesson_text_size_settings")
    }
}
