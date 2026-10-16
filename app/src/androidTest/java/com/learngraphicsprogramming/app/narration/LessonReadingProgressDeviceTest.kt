package com.learngraphicsprogramming.app.narration

import android.app.Activity
import android.app.Application
import android.app.KeyguardManager
import android.content.Context
import android.content.Intent
import android.content.res.ColorStateList
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.drawable.GradientDrawable
import android.media.MediaPlayer
import android.os.Bundle
import android.os.Build
import android.os.SystemClock
import android.text.SpannableString
import android.text.Spanned
import android.text.style.ForegroundColorSpan
import android.view.ContextThemeWrapper
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
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
import com.learngraphicsprogramming.app.math.MathTextView
import java.io.File
import java.security.MessageDigest
import java.util.UUID
import java.util.concurrent.atomic.AtomicReference
import org.json.JSONArray
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/** Isolated native rendering plus actual Kokoro/MediaPlayer callbacks. No curriculum fixture is saved. */
@RunWith(AndroidJUnit4::class)
class LessonReadingProgressDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val preferences = linkedMapOf<String, Map<String, *>>()
    private var originalFiles = emptySet<String>()
    private var captured = false
    private var scenario: ActivityScenario<LessonActivity>? = null
    private var extraController: LessonNarrationController? = null
    private val postResumed = java.util.WeakHashMap<Activity, Long>()
    private val screenGuard = object : Application.ActivityLifecycleCallbacks {
        override fun onActivityCreated(activity: Activity, state: Bundle?) {
            if (activity.packageName == QA_PACKAGE)
                activity.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        }
        override fun onActivityPostResumed(activity: Activity) { postResumed[activity] = SystemClock.uptimeMillis() }
        override fun onActivityStarted(activity: Activity) = Unit
        override fun onActivityResumed(activity: Activity) {
            if (Build.VERSION.SDK_INT < 29) postResumed[activity] = SystemClock.uptimeMillis()
        }
        override fun onActivityPaused(activity: Activity) = Unit
        override fun onActivityStopped(activity: Activity) = Unit
        override fun onActivitySaveInstanceState(activity: Activity, state: Bundle) = Unit
        override fun onActivityDestroyed(activity: Activity) = Unit
    }

    @Before fun preserveOnlyTheIsolatedQaApplication() {
        assertEquals("Never run against the ordinary application", QA_PACKAGE, context.packageName)
        assertEquals(QA_PACKAGE, BuildConfig.APPLICATION_ID)
        val lock = context.getSystemService(KeyguardManager::class.java)
        assertFalse("Unlock LDPlayer before visual playback checks", lock.isDeviceLocked)
        assertFalse("Dismiss the lock screen before checking playback focus", lock.isKeyguardLocked)
        originalFiles = preferenceFiles()
        (originalFiles + KNOWN_PREFERENCES).forEach { name ->
            preferences[name] = context.getSharedPreferences(name, Context.MODE_PRIVATE).all.mapValues { (_, value) ->
                if (value is Set<*>) value.toSet() else value
            }
        }
        captured = true
        (context.applicationContext as Application).registerActivityLifecycleCallbacks(screenGuard)
        assertTrue("Start an isolated bookmark history, restored after the test", context
            .getSharedPreferences("lesson_narration_resume_bounded_v1", Context.MODE_PRIVATE).edit().clear().commit())
        NarrationSettingsRepository.saveEnabled(context, false)
        NarrationSettingsRepository.saveEngine(context, NarrationEngine.KOKORO)
        NarrationSettingsRepository.saveLanguage(context, NarrationLanguage.SPANISH)
        NarrationSettingsRepository.saveAndroidVoice(context, null)
        NarrationSettingsRepository.saveKokoroVoice(context, "ef_dora")
        NarrationSettingsRepository.saveRate(context, 1f)
        NarrationSettingsRepository.saveControlsVisible(context, true)
        PremiumRepository.setVerifiedPremium(context)
    }

    @After fun restoreEveryOriginalPreferenceAndClosePlayback() {
        if (!captured) return
        var closingController: LessonNarrationController? = null
        try {
            extraController?.let { current ->
                onMain { current.close() }
                await("The additional playback worker closes", 15_000) { !field<Thread>(current, "workerThread").isAlive }
            }
            scenario?.onActivity {
                closingController = field<LessonNarrationController?>(it, "narrationController")
                closingController?.stop()
            }
            scenario?.close()
            instrumentation.waitForIdleSync()
            closingController?.let { closed ->
                await("The narration worker closes before restoring QA preferences", 15_000) {
                    !field<Thread>(closed, "workerThread").isAlive
                }
            }
        } finally {
            (context.applicationContext as Application).unregisterActivityLifecycleCallbacks(screenGuard)
            (preferenceFiles() + preferences.keys).forEach { name ->
                if (name !in originalFiles) {
                    context.deleteSharedPreferences(name)
                } else {
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
                    assertTrue("Restore $name", editor.commit())
                    assertEquals("Original progress, answers, settings and drafts are preserved", preferences[name], target.all)
                }
            }
            assertEquals("No test preference file remains", originalFiles, preferenceFiles())
        }
    }

    @Test fun nativeColorsPreserveFormulaSpansAndControlsOnLightAndDarkSurfaces() {
        onMain {
            for ((name, background, theme) in listOf(
                Triple("light", Color.WHITE, android.R.style.Theme_Material_Light_NoActionBar),
                Triple("dark", Color.rgb(18, 18, 18), android.R.style.Theme_Material_NoActionBar))) {
                val themed = ContextThemeWrapper(context, theme)
                val panel = LinearLayout(themed).apply {
                    orientation = LinearLayout.VERTICAL
                    setPadding(24, 24, 24, 24)
                    setBackgroundColor(background)
                }
                val original = ColorStateList(arrayOf(intArrayOf(android.R.attr.state_enabled), intArrayOf()),
                    intArrayOf(if (name == "light") Color.BLACK else Color.WHITE, Color.GRAY))
                val prose = TextView(themed).apply { text = "Lectura original: áéíóú y Ω"; textSize = 18f; setTextColor(original) }
                val formula = MathTextView(themed).apply { setMathContent("\\frac{1}{2} = \\frac{2}{4}", "block"); textSize = 22f; setTextColor(original) }
                val code = TextView(themed).apply {
                    text = SpannableString("int valor = 2;").apply {
                        setSpan(ForegroundColorSpan(Color.rgb(83, 170, 223)), 0, 3, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                    }
                    setTextColor(original)
                }
                val input = EditText(themed).apply { setText("2"); setTextColor(original) }
                val button = Button(themed).apply { text = "Comprobar"; setTextColor(original) }
                var clicks = 0
                val clickable = TextView(themed).apply { text = "Control existente"; setTextColor(original); setOnClickListener { clicks++ } }
                listOf(prose, formula, code, input, button, clickable).forEach { panel.addView(it) }
                panel.measure(View.MeasureSpec.makeMeasureSpec(900, View.MeasureSpec.EXACTLY),
                    View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED))
                panel.layout(0, 0, panel.measuredWidth, panel.measuredHeight)
                val before = facts(panel)
                val colors = LessonReadingTextColors()
                descendants(panel).filterIsInstance<TextView>().forEach { colors.register(0, it) }
                colors.show(mapOf(0 to ReadingBlockState.UNREAD), true)
                assertNotEquals("Unread prose is distinct on $name surfaces", original.defaultColor, prose.currentTextColor)
                for (view in listOf(prose, formula)) {
                    assertEquals("Unread gray is opaque", 255, Color.alpha(view.currentTextColor))
                    assertEquals(Color.red(view.currentTextColor), Color.green(view.currentTextColor))
                    assertEquals(Color.green(view.currentTextColor), Color.blue(view.currentTextColor))
                    assertTrue("Ordinary text contrast on $name", LessonReadingColor.contrast(view.currentTextColor, background) >= 4.5)
                }
                listOf(code, input, button, clickable).forEach { assertSame("Code and interactive colors remain exact", original, it.textColors) }
                assertFacts(before)
                assertTrue(clickable.performClick())
                assertEquals(1, clicks)
                saveCanvas(panel, "native-colors-$name-unread")
                for (state in listOf(ReadingBlockState.CURRENT, ReadingBlockState.READ)) {
                    colors.show(mapOf(0 to state), true)
                    assertSame("Original state list is restored for $state", original, prose.textColors)
                    assertSame(original, formula.textColors)
                    assertFacts(before)
                }
                colors.show(mapOf(0 to ReadingBlockState.UNREAD), true)
                colors.show(emptyMap(), true)
                assertSame("An unmapped fragment stays ordinary", original, prose.textColors)
                colors.show(mapOf(0 to ReadingBlockState.UNREAD), true)
                colors.show(mapOf(0 to ReadingBlockState.UNREAD), false)
                assertSame("Disabled narration restores ordinary presentation", original, prose.textColors)
                panel.background = GradientDrawable(GradientDrawable.Orientation.LEFT_RIGHT,
                    intArrayOf(background, Color.GRAY))
                colors.show(mapOf(0 to ReadingBlockState.UNREAD), true)
                assertSame("An unresolvable gradient keeps the legible original style", original, prose.textColors)
                colors.clear()
                assertFacts(before)
            }
        }
    }

    @Test fun realPlaybackUpdatesAnUnpreparedLessonAndRetainsCachedPagesWithoutEditingItsContent() {
        val fixture = fixture()
        val authored = fixture.toString()
        val id = fixture.getString("id")
        ProgressRepository.saveCurrentPage(context, ROUTE, SOURCE_LESSON, 0)
        val host = ActivityScenario.launch<LessonActivity>(Intent(context, LessonActivity::class.java)
            .putExtra("routeId", ROUTE).putExtra("courseId", "math_course_01")
            .putExtra("unitId", "estructura_y_divisibilidad").putExtra("lessonId", SOURCE_LESSON)
            .putExtra("lessonNumber", 1).putExtra("lessonTotal", 1)).also { scenario = it }
        await("The ordinary activity is post-resumed, focused and stable before a single fixture injection", 15_000) {
            var ready = false
            host.onActivity { activity ->
                ready = postResumed[activity]?.let { SystemClock.uptimeMillis() - it >= 700 } == true &&
                    activity.hasWindowFocus() && content(activity).isAttachedToWindow &&
                    content(activity).width > 0 && !content(activity).isLayoutRequested
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
        lateinit var before: List<TextFact>
        lateinit var firstPageChildren: List<View>
        lateinit var intro: TextView
        lateinit var formula: TextView
        lateinit var originalIntro: ColorStateList
        lateinit var originalFormula: ColorStateList
        host.onActivity { activity ->
            before = facts(content(activity))
            firstPageChildren = (0 until content(activity).childCount).map { content(activity).getChildAt(it) }
            intro = texts(activity).single { it.text.toString() == INTRO }
            formula = texts(activity).single { it.contentDescription?.toString() == FORMULA.replace("\\(", "").replace("\\)", "") }
            originalIntro = intro.textColors
            originalFormula = formula.textColors
            assertTrue("The original formula renderer remains in use", formula.text is Spanned)
            assertTrue("The published puzzle was rendered", field<Int>(activity, "requiredExercises") >= 2)
            NarrationSettingsRepository.saveEnabled(activity, true)
            invoke(activity, "renderCurrentPage")
        }
        awaitLayout(host, id)
        host.onActivity { activity ->
            assertEquals(ReadingBlockState.UNREAD, states(activity)[0])
            assertSame("Word drawing preserves the original text colors", originalIntro, intro.textColors)
            assertTrue("Preparation leaves the paragraph's word glyphs pending",
                field<List<ReadingTextRange>>(intro, "unreadRanges").isNotEmpty())
            assertNotEquals(originalFormula.defaultColor, formula.currentTextColor)
            assertFacts(before)
            assertTrue(activity.findViewById<View>(R.id.buttonLessonListen).performClick())
        }
        await("A real Kokoro WAV is playing the first paragraph in LDPlayer", 120_000) {
            var ready = false
            host.onActivity { activity ->
                val current = controller(activity)
                ready = current.isPlaying && field<Boolean>(current, "playerReady") &&
                    states(activity)[0] == ReadingBlockState.CURRENT &&
                    field<MediaPlayer>(current, "player").currentPosition > 150
            }
            ready
        }
        var pausedPosition = 0L
        lateinit var pausedStates: Map<Int, ReadingBlockState>
        lateinit var audio: File
        lateinit var audioHash: String
        lateinit var audioIdentity: List<Long>
        lateinit var audioCacheKey: String
        lateinit var initialGenerationEvidence: Set<String>
        fun generationEvidence(hash: String): Set<String> {
            val diagnostics = File(context.filesDir, KokoroNarrationDiagnostics.EVENTS_FILE)
            if (!diagnostics.isFile) return emptySet()
            val generated = linkedSetOf<String>()
            val previousBackground = mutableMapOf<String, Pair<Int, Int>>()
            diagnostics.readLines(Charsets.UTF_8).forEach { line ->
                val event = runCatching { JSONObject(line) }.getOrNull() ?: return@forEach
                val kind = event.optString("event")
                if (kind == "Page sentence generated" && event.optString("hash") == hash) generated += line
                if (kind == "Cached lesson speech") {
                    val counters = event.optInt("generated") to event.optInt("reused")
                    val previous = previousBackground.put(event.optString("thread"), counters)
                    val newSlice = previous == null || counters.first + counters.second <= previous.first + previous.second
                    val newlyGenerated = if (newSlice) counters.first else counters.first - requireNotNull(previous).first
                    // Background diagnostics use cumulative slice counters. A cache-race reuse
                    // can log the same hash without generating audio and must remain permitted.
                    if (newlyGenerated > 0 && event.optString("hash") == hash) generated += line
                }
            }
            return generated
        }
        fun cacheIdentity(file: File): List<Long> = android.system.Os.stat(file.absolutePath).let {
            listOf(it.st_dev, it.st_ino, it.st_size)
        }
        fun assertOriginalAudioIsReused() {
            assertTrue("The actual cached audio file remains available", audio.isFile)
            assertEquals("Playback reuses the exact original audio bytes", audioHash, sha256(audio))
            assertEquals("Cache access retains the same file identity and size", audioIdentity, cacheIdentity(audio))
            assertTrue("Resuming generates no additional audio for this sentence hash",
                (generationEvidence(audioCacheKey) - initialGenerationEvidence).isEmpty())
            // Existing cache lookup deliberately updates lastModified for retention. That
            // access timestamp is not evidence of rewritten bytes or another synthesis.
        }
        lateinit var staleCompletion: NarrationPlaybackProgress
        host.onActivity { activity ->
            val current = controller(activity)
            val segment = field<List<Any>>(current, "segments")[field<Int>(current, "index")]
            audio = field(segment, "file")
            assertTrue("Playback uses actual synthesized audio", audio.length() > 44)
            audioHash = sha256(audio)
            audioIdentity = cacheIdentity(audio)
            audioCacheKey = KokoroNarrationCache.key("es", "ef_dora", field(segment, "text"))
            initialGenerationEvidence = generationEvidence(audioCacheKey)
            assertSame("The current paragraph uses its original style", originalIntro, intro.textColors)
            assertEquals(ReadingBlockState.UNREAD, states(activity)[1])
            assertTrue(activity.findViewById<View>(R.id.buttonLessonListen).performClick())
            assertFalse(current.isPlaying)
            pausedPosition = field(current, "offset")
            pausedStates = states(activity).toMap()
            assertFacts(before)
        }
        SystemClock.sleep(450)
        host.onActivity { activity ->
            assertEquals("Pausing never credits more reading", pausedStates, states(activity))
            assertEquals(pausedPosition, field<Long>(controller(activity), "offset"))
            assertOriginalAudioIsReused()
            assertFacts(before)
            capture(activity, "actual-playback-paused-first-paragraph")
            assertTrue(activity.findViewById<View>(R.id.buttonLessonListen).performClick())
        }
        await("MediaPlayer completion credits the paragraph and starts the original formula block", 35_000) {
            var ready = false
            host.onActivity { activity ->
                ready = controller(activity).isPlaying && states(activity)[0] == ReadingBlockState.READ &&
                    states(activity)[1] == ReadingBlockState.CURRENT && field<Boolean>(controller(activity), "playerReady")
            }
            ready
        }
        host.onActivity { activity ->
            controller(activity).pauseKeepingPosition()
            assertSame(originalIntro, intro.textColors)
            assertSame("Formula color changes without replacing its spans", originalFormula, formula.textColors)
            assertEquals(ReadingBlockState.UNREAD, states(activity)[3])
            assertOriginalAudioIsReused()
            assertFacts(before)
            activity.findViewById<ScrollView>(R.id.lessonScroll).scrollTo(0, formula.top)
            capture(activity, "actual-playback-read-prose-current-formula")
            val oldController = controller(activity)
            staleCompletion = NarrationPlaybackProgress("$ROUTE/$id/0", field(oldController, "generation"),
                field(oldController, "index"), field<List<Any>>(oldController, "segments").size,
                NarrationPlaybackPhase.FINISHED, field(oldController, "resumeKey"))
            setField(activity, "currentPage", 1)
            invoke(activity, "renderCurrentPage")
        }
        awaitLayout(host, id)
        host.onActivity { activity ->
            assertTrue(states(activity).isNotEmpty())
            assertTrue("The next page has its own unread state", states(activity).values.all { it == ReadingBlockState.UNREAD })
            assertFalse(controller(activity).isPlaying)
            val beforeLateEvent = states(activity).toMap()
            activity.javaClass.getDeclaredMethod("readingPlaybackChanged", NarrationPlaybackProgress::class.java)
                .apply { isAccessible = true }.invoke(activity, staleCompletion)
            assertEquals("A deliberately late old-page callback cannot paint the new page", beforeLateEvent, states(activity))
            capture(activity, "page-change-new-unread-page")
            setField(activity, "currentPage", 0)
            invoke(activity, "renderCurrentPage")
        }
        awaitLayout(host, id)
        host.onActivity { activity ->
            assertEquals("The exact original cached views return", firstPageChildren,
                (0 until content(activity).childCount).map { content(activity).getChildAt(it) })
            assertEquals("Returning does not forget actually completed audio", ReadingBlockState.READ, states(activity)[0])
            assertEquals("A stale completed callback cannot credit unheard old-page content", ReadingBlockState.UNREAD, states(activity)[3])
            assertFacts(before)
            assertTrue(activity.findViewById<View>(R.id.buttonLessonRestartPage).performClick())
            assertEquals("Restart clears credited reading before playback advances", ReadingBlockState.UNREAD, states(activity)[0])
            controller(activity).pauseKeepingPosition()
            capture(activity, "explicit-restart-clears-reading")
            NarrationSettingsRepository.saveEnabled(activity, false)
            controller(activity).settingsChanged()
            assertSame("A real unavailable/disabled controller event restores prose immediately", originalIntro, intro.textColors)
            assertSame(originalFormula, formula.textColors)
            invoke(activity, "renderCurrentPage")
        }
        awaitLayout(host, id)
        host.onActivity { activity ->
            before.forEach { assertColorBehavior(it.colors, it.view.textColors, "Disabled narration restores ${it.source}") }
            assertFacts(before)
            assertEquals("The displayed curriculum JSON is never rewritten", authored, field<JSONObject>(activity, "lessonJson").toString())
            assertEquals("No puzzle is silently solved by reading", 0, field<Int>(activity, "completedExercises"))
            capture(activity, "disabled-narration-original-presentation")
        }
        assertEquals("No narration metadata was added to an unprepared future lesson", authored, fixture.toString())
    }

    @Test fun threeExistingBookmarksIncludeFinishedPagesAndAnEvictedPageStartsSafely() {
        val host = ActivityScenario.launch<LessonActivity>(Intent(context, LessonActivity::class.java)
            .putExtra("routeId", ROUTE).putExtra("courseId", "math_course_01")
            .putExtra("unitId", "estructura_y_divisibilidad").putExtra("lessonId", SOURCE_LESSON)
            .putExtra("lessonNumber", 1).putExtra("lessonTotal", 1)).also { scenario = it }
        await("The ordinary lesson keeps a native foreground window for real playback", 15_000) {
            var focused = false
            host.onActivity { focused = it.hasWindowFocus() && postResumed[it] != null }
            focused
        }
        val id = "reading-history-${UUID.randomUUID()}"
        val lesson = JSONObject().put("title", "").put("pages", JSONArray().apply {
            repeat(4) { index -> put(JSONObject().put("blocks", JSONArray().put(JSONObject()
                .put("type", "paragraph").put("text", "La página ${index + 1} conserva su lectura sin modificar el historial.")))) }
        })
        val source = lesson.toString()
        val scripts = (0..3).map { LessonNarrationScript.page(lesson, it, NarrationLanguage.SPANISH) }
        assertTrue(scripts.all { it.texts.size == 1 })
        val model = LessonReadingProgressModel()
        val keys = scripts.mapIndexed { index, script ->
            NarrationResumePolicy.key("$id/$index", "es", NarrationEngine.KOKORO.name, "ef_dora",
                NarrationSegments.sentences(script.texts)).also { key ->
                model.bind("$id/$index", requireNotNull(script.readingPlan(NarrationEngine.KOKORO)), key)
            }
        }
        val errors = java.util.concurrent.CopyOnWriteArrayList<String>()
        val events = java.util.concurrent.CopyOnWriteArrayList<NarrationPlaybackProgress>()
        val current = onMain {
            NarrationSettingsRepository.saveEnabled(context, true)
            LessonNarrationController(context, {}, { errors.add(it) }, { event ->
                events.add(event)
                model.update(event)
            }, trackWords = true).also { extraController = it }
        }
        val store = NarrationResumeStore(context)
        val paused = linkedMapOf<Int, Long>()
        fun start(page: Int, restart: Boolean = false) = onMain {
            current.toggle(scripts[page].texts, NarrationLanguage.SPANISH,
                pageIdentity = "$id/$page", restartFromBeginning = restart)
        }
        fun readyAtLeast(position: Long) = onMain {
            assertTrue("Real Kokoro playback in LDPlayer must not report an error: $errors", errors.isEmpty())
            current.isPlaying && field<Boolean>(current, "playerReady") &&
                field<MediaPlayer>(current, "player").currentPosition >= position
        }
        fun savedKeys(): List<String> {
            val values = context.getSharedPreferences("lesson_narration_resume_bounded_v1", Context.MODE_PRIVATE).all
            assertTrue("The existing store never writes a fourth bookmark slot",
                values.keys.filter { it.startsWith("key_") }.all { it.removePrefix("key_").toInt() < 3 })
            return (0 until NarrationResumePolicy.MAX_BOOKMARKS).mapNotNull { values["key_$it"] as? String }
        }
        fun pause(page: Int) {
            start(page)
            await("Page $page produces and plays an actual Kokoro WAV before pausing", 120_000) { readyAtLeast(250) }
            onMain {
                current.pauseKeepingPosition()
                paused[page] = field(current, "offset")
                val point = requireNotNull(store.restore(keys[page], 1))
                assertFalse(point.finished)
                assertEquals(paused[page], point.positionMs)
                assertEquals(ReadingBlockState.CURRENT, model.states("$id/$page")[0])
            }
        }
        val finishedOffsets = linkedMapOf<Int, Long>()
        fun finish(page: Int) {
            start(page)
            await("Page $page plays a real Kokoro WAV before reaching its natural end", 120_000) { readyAtLeast(150) }
            await("Page $page reaches its natural end without seeking or a fabricated completion", 60_000) {
                onMain {
                    assertTrue(errors.toString(), errors.isEmpty())
                    field<Boolean>(current, "finished") && !current.isPlaying
                }
            }
            onMain {
                val completed = requireNotNull(store.restore(keys[page], 1))
                assertTrue(completed.finished)
                assertTrue(completed.positionMs > 0)
                finishedOffsets[page] = completed.positionMs
                assertEquals("Only naturally completed audio is credited", ReadingBlockState.READ, model.states("$id/$page")[0])
            }
        }

        // Follow the user's exact example: finished A, paused B, finished C, then D.
        finish(0)
        assertEquals("A finished page occupies an ordinary bounded bookmark", listOf(keys[0]), savedKeys())
        repeat(3) {
            start(0)
            onMain {
                assertFalse("Play on a finished page must not restart automatically", current.isPlaying)
                assertTrue(field(current, "finished"))
                assertNull(field<MediaPlayer?>(current, "player"))
                assertEquals(finishedOffsets.getValue(0), requireNotNull(store.restore(keys[0], 1)).positionMs)
                assertEquals(ReadingBlockState.READ, model.states("$id/0")[0])
            }
        }
        pause(1)
        assertEquals(listOf(keys[1], keys[0]), savedKeys())
        repeat(3) {
            start(1)
            onMain {
                assertEquals("Repeated pause-button presses preserve the same page position",
                    paused.getValue(1), field<Long>(current, "offset"))
            }
            await("A retained paused page resumes its actual MediaPlayer position", 60_000) { readyAtLeast(paused.getValue(1)) }
            onMain {
                current.pauseKeepingPosition()
                paused[1] = field(current, "offset")
                assertEquals(paused.getValue(1), requireNotNull(store.restore(keys[1], 1)).positionMs)
            }
            assertEquals("Repeated pauses never consume additional page slots", listOf(keys[1], keys[0]), savedKeys())
        }
        finish(2)
        assertEquals("A finished page still counts among the three existing bookmarks", listOf(keys[2], keys[1], keys[0]), savedKeys())
        repeat(3) {
            start(2)
            onMain {
                assertFalse("Play on an exhausted page must not restart automatically", current.isPlaying)
                assertTrue(field(current, "finished"))
                assertNull(field<MediaPlayer?>(current, "player"))
                assertEquals(ReadingBlockState.READ, model.states("$id/2")[0])
            }
        }
        assertEquals(listOf(keys[2], keys[1], keys[0]), savedKeys())

        start(3)
        await("A fourth explicit page starts actual playback", 120_000) { readyAtLeast(250) }
        onMain { current.pauseKeepingPosition() }
        assertEquals("The fourth page evicts the oldest finished page using the existing policy", listOf(keys[3], keys[2], keys[1]), savedKeys())
        assertNull("A finished bookmark never survives outside the existing three-page limit", store.restore(keys[0], 1))
        onMain {
            current.toggle(scripts[0].texts, NarrationLanguage.SPANISH, pageIdentity = "$id/0")
            assertEquals("An evicted page starts at zero, with no invented resume position", 0L, field<Long>(current, "offset"))
            assertEquals("An absent bookmark does not invent already-read text", ReadingBlockState.UNREAD, model.states("$id/0")[0])
        }
        await("The evicted page remains fully playable", 60_000) { readyAtLeast(150) }
        onMain {
            assertEquals(ReadingBlockState.CURRENT, model.states("$id/0")[0])
            current.pauseKeepingPosition()
        }
        assertEquals(listOf(keys[0], keys[3], keys[2]), savedKeys())
        assertEquals("The original unprepared lesson is unchanged", source, lesson.toString())
        File(evidence(), "real-three-bookmark-history.json").writeText(JSONObject()
            .put("engine", NarrationEngine.KOKORO.name).put("voice", "ef_dora").put("store", "lesson_narration_resume_bounded_v1")
            .put("existingMaximum", NarrationResumePolicy.MAX_BOOKMARKS)
            .put("testedSequence", "A finished; B paused and resumed three times; C finished; D paused; oldest finished A evicted; A starts at zero")
            .put("naturallyFinishedSourceOffsetsMillis", JSONObject(finishedOffsets.mapKeys { it.key.toString() }))
            .put("pausedSourceOffsetsMillis", JSONObject(paused.mapKeys { it.key.toString() }))
            .put("finalKeys", JSONArray(savedKeys())).put("eventCount", events.size)
            .put("phases", JSONArray(events.map { it.phase.name }.distinct()))
            .put("naturalCompletions", events.count { it.phase == NarrationPlaybackPhase.SEGMENT_COMPLETED })
            .put("sourceJsonUnchanged", source == lesson.toString()).toString(2))
    }

    @Test fun realPausedDecoderReleaseSeekRateAndRestartPreserveTheCachedAudio() {
        val host = ActivityScenario.launch<LessonActivity>(Intent(context, LessonActivity::class.java)
            .putExtra("routeId", ROUTE).putExtra("courseId", "math_course_01")
            .putExtra("unitId", "estructura_y_divisibilidad").putExtra("lessonId", SOURCE_LESSON)
            .putExtra("lessonNumber", 1).putExtra("lessonTotal", 1)).also { scenario = it }
        await("The LDPlayer lesson owns a focused foreground window", 15_000) {
            var focused = false
            host.onActivity { focused = it.hasWindowFocus() && postResumed[it] != null }
            focused
        }
        val text = "Esta lectura extensa permite comprobar que una pausa conserva la posición original y libera el reproductor de sonido mientras los cambios de velocidad y los desplazamientos reutilizan exactamente el mismo archivo de audio."
        val page = "reading-decoder-${UUID.randomUUID()}"
        val script = LessonNarrationScript(listOf(text), emptyList(), listOf(NarrationTextOrigin(0)))
        assertEquals("This regression exercises exactly one real audio segment", 1, NarrationSegments.sentences(script.texts).size)
        val key = NarrationResumePolicy.key(page, "es", NarrationEngine.KOKORO.name, "ef_dora",
            NarrationSegments.sentences(script.texts))
        val model = LessonReadingProgressModel().apply {
            bind(page, requireNotNull(script.readingPlan(NarrationEngine.KOKORO)), key)
        }
        val errors = java.util.concurrent.CopyOnWriteArrayList<String>()
        val events = java.util.concurrent.CopyOnWriteArrayList<NarrationPlaybackProgress>()
        val current = onMain {
            NarrationSettingsRepository.saveEnabled(context, true)
            LessonNarrationController(context, {}, { errors.add(it) }, { event ->
                events.add(event)
                model.update(event)
            }).also { extraController = it; it.toggle(script.texts, NarrationLanguage.SPANISH, pageIdentity = page) }
        }
        await("Actual Kokoro audio starts before testing decoder release", 120_000) {
            onMain {
                assertTrue("Real Kokoro playback reports no errors: $errors", errors.isEmpty())
                current.isPlaying && field<Boolean>(current, "playerReady") &&
                    field<MediaPlayer>(current, "player").currentPosition >= 350
            }
        }
        val audio = onMain { field<File>(field<List<Any>>(current, "segments").single(), "file") }
        assertTrue("The test uses a real synthesized WAV", audio.length() > 44)
        val hash = sha256(audio)
        val modified = audio.lastModified()
        val store = NarrationResumeStore(context)
        val pausedOffset = onMain {
            assertTrue("The source segment is long enough to measure seeking and playback rate",
                field<MediaPlayer>(current, "player").duration >= 8_000)
            current.pauseKeepingPosition()
            assertFalse(current.isPlaying)
            assertNull("A paused page retains no native decoder or file handle", field<MediaPlayer?>(current, "player"))
            assertFalse(field(current, "playerReady"))
            val point = field<Long>(current, "offset")
            assertTrue(point >= 350)
            assertEquals(point, requireNotNull(store.restore(key, 1)).positionMs)
            assertEquals(ReadingBlockState.CURRENT, model.states(page)[0])
            point
        }
        val soughtOffset = pausedOffset + 2_000L
        onMain {
            current.seekBy(2)
            assertEquals("Paused seeking uses source audio seconds", soughtOffset, field<Long>(current, "offset"))
            assertFalse(current.isPlaying)
            assertNull("Seeking while paused must not allocate a MediaPlayer", field<MediaPlayer?>(current, "player"))
            assertEquals(soughtOffset, requireNotNull(store.restore(key, 1)).positionMs)
            assertEquals("A seek never credits skipped narration", ReadingBlockState.UNREAD, model.states(page)[0])
            NarrationSettingsRepository.saveRate(context, 1.5f)
            current.settingsChanged()
            assertEquals(1.5f, NarrationSettingsRepository.get(context).rate, 0f)
            assertEquals("Changing rate while paused preserves the exact source offset", soughtOffset, field<Long>(current, "offset"))
            assertNull("Rate changes keep the paused decoder released", field<MediaPlayer?>(current, "player"))
            assertFalse(current.isPlaying)
            assertSame("Seeking and speed changes reuse the same cached WAV", audio,
                field<File>(field<List<Any>>(current, "segments").single(), "file"))
        }
        SystemClock.sleep(300)
        assertEquals(hash, sha256(audio))
        assertEquals(modified, audio.lastModified())
        onMain {
            assertEquals(soughtOffset, field<Long>(current, "offset"))
            assertNull(field<MediaPlayer?>(current, "player"))
            current.toggle(script.texts, NarrationLanguage.SPANISH, pageIdentity = page)
            assertEquals("Resume starts from the sought source position", soughtOffset, field<Long>(current, "offset"))
        }
        await("Resume creates a real MediaPlayer from the retained WAV", 15_000) {
            onMain {
                assertTrue(errors.toString(), errors.isEmpty())
                current.isPlaying && field<Boolean>(current, "playerReady") &&
                    field<MediaPlayer>(current, "player").currentPosition >= soughtOffset
            }
        }
        val measurementStart = onMain {
            val media = field<MediaPlayer>(current, "player")
            assertEquals("The resumed native player applies the stored speed", 1.5f, media.playbackParams.speed, 0.001f)
            assertEquals("Speed changes preserve the voice pitch", 1f, media.playbackParams.pitch, 0.001f)
            assertEquals(ReadingBlockState.CURRENT, model.states(page)[0])
            media.currentPosition.toLong() to SystemClock.elapsedRealtime()
        }
        SystemClock.sleep(1_000)
        val measurementEnd = onMain {
            assertTrue("The original source segment remains actively playing", current.isPlaying)
            field<MediaPlayer>(current, "player").currentPosition.toLong() to SystemClock.elapsedRealtime()
        }
        val sourceAdvance = measurementEnd.first - measurementStart.first
        val wallAdvance = measurementEnd.second - measurementStart.second
        val observedRate = sourceAdvance.toDouble() / wallAdvance
        assertTrue("Real source audio advances at the requested speed: $observedRate", observedRate in 1.15..1.9)
        assertEquals("Resume never regenerates the WAV bytes", hash, sha256(audio))
        assertEquals(modified, audio.lastModified())
        onMain {
            current.restart()
            assertEquals("Explicit restart restores the source beginning", 0L, field<Long>(current, "offset"))
            assertEquals("Explicit restart clears visual reading evidence", ReadingBlockState.UNREAD, model.states(page)[0])
            current.pauseKeepingPosition()
            assertFalse(current.isPlaying)
            assertNull(field<MediaPlayer?>(current, "player"))
            assertEquals(0L, requireNotNull(store.restore(key, 1)).positionMs)
            current.close()
            assertNull("Closing leaves no native player allocated", field<MediaPlayer?>(current, "player"))
        }
        await("The real playback worker closes after the regression", 15_000) { !field<Thread>(current, "workerThread").isAlive }
        extraController = null
        assertTrue("Closing preserves persistent cached audio", audio.isFile)
        assertEquals(hash, sha256(audio))
        assertEquals(modified, audio.lastModified())
        assertTrue(events.any { it.phase == NarrationPlaybackPhase.POSITION_CHANGED })
        assertTrue(events.any { it.phase == NarrationPlaybackPhase.RESTARTED })
        File(evidence(), "real-paused-decoder-seek-rate.json").writeText(JSONObject()
            .put("engine", NarrationEngine.KOKORO.name).put("voice", "ef_dora")
            .put("pausedSourceOffsetMillis", pausedOffset).put("soughtSourceOffsetMillis", soughtOffset)
            .put("requestedSpeed", 1.5).put("sourceAdvanceMillis", sourceAdvance).put("wallAdvanceMillis", wallAdvance)
            .put("observedSourceRate", observedRate).put("audioSha256", hash).put("audioModifiedMillis", modified)
            .put("cachedAudioUnchangedAfterClose", true).put("pausedDecoderReleased", true)
            .put("seekKeptPlaybackPaused", true).put("restartSourceOffsetMillis", 0)
            .put("phases", JSONArray(events.map { it.phase.name }.distinct())).toString(2))
    }

    @Test fun aColdVisualModelRestoresTheRealBookmarkWithoutInventingReadOrUnreadHistory() {
        val host = ActivityScenario.launch<LessonActivity>(Intent(context, LessonActivity::class.java)
            .putExtra("routeId", ROUTE).putExtra("courseId", "math_course_01")
            .putExtra("unitId", "estructura_y_divisibilidad").putExtra("lessonId", SOURCE_LESSON)
            .putExtra("lessonNumber", 1).putExtra("lessonTotal", 1)).also { scenario = it }
        await("LDPlayer provides the real foreground playback window", 15_000) {
            var focused = false
            host.onActivity { focused = it.hasWindowFocus() && postResumed[it] != null }
            focused
        }
        val script = LessonNarrationScript(listOf(
            "Esta primera idea conserva el orden original.",
            "La segunda idea permite conservar una posición intermedia real mientras el lector inteligente mantiene el punto de lectura y las vistas futuras esperan su turno sin cambiar el contenido.",
            "La última idea todavía espera su turno de lectura."), emptyList(),
            (0..2).map { NarrationTextOrigin(it) })
        assertEquals(3, NarrationSegments.sentences(script.texts).size)
        val page = "reading-cold-${UUID.randomUUID()}"
        val key = NarrationResumePolicy.key(page, "es", NarrationEngine.KOKORO.name, "ef_dora",
            NarrationSegments.sentences(script.texts))
        val errors = java.util.concurrent.CopyOnWriteArrayList<String>()
        val oldModel = LessonReadingProgressModel().apply {
            bind(page, requireNotNull(script.readingPlan(NarrationEngine.KOKORO)), key)
        }
        val original = onMain {
            NarrationSettingsRepository.saveEnabled(context, true)
            LessonNarrationController(context, {}, { errors.add(it) }, oldModel::update).also {
                extraController = it
                it.toggle(script.texts, NarrationLanguage.SPANISH, pageIdentity = page)
            }
        }
        await("The first fragment completes naturally and the second real WAV starts", 120_000) {
            onMain {
                assertTrue(errors.toString(), errors.isEmpty())
                original.isPlaying && field<Int>(original, "index") == 1 &&
                    field<Boolean>(original, "playerReady") && field<MediaPlayer>(original, "player").currentPosition >= 300 &&
                    oldModel.states(page)[0] == ReadingBlockState.READ
            }
        }
        val store = NarrationResumeStore(context)
        val audio = onMain { field<File>(field<List<Any>>(original, "segments")[1], "file") }
        val hash = sha256(audio)
        val bytes = audio.length()
        val originalModified = audio.lastModified()
        val pausedOffset = onMain {
            original.pauseKeepingPosition()
            assertNull(field<MediaPlayer?>(original, "player"))
            val offset = field<Long>(original, "offset")
            val saved = requireNotNull(store.restore(key, 3))
            assertEquals(1, saved.segment)
            assertEquals(offset, saved.positionMs)
            assertTrue(offset >= 300)
            assertEquals(ReadingBlockState.READ, oldModel.states(page)[0])
            assertEquals(ReadingBlockState.CURRENT, oldModel.states(page)[1])
            assertEquals(ReadingBlockState.UNREAD, oldModel.states(page)[2])
            original.close()
            offset
        }
        await("The original worker is fully closed before recreation", 15_000) {
            !field<Thread>(original, "workerThread").isAlive
        }
        extraController = null
        val restoredModel = LessonReadingProgressModel().apply {
            bind(page, requireNotNull(script.readingPlan(NarrationEngine.KOKORO)), key)
        }
        val restoredEvents = java.util.concurrent.CopyOnWriteArrayList<NarrationPlaybackProgress>()
        lateinit var restoredPreparation: Map<Int, ReadingBlockState>
        val restored = onMain {
            LessonNarrationController(context, {}, { errors.add(it) }, { event ->
                restoredEvents.add(event)
                restoredModel.update(event)
            }).also {
                extraController = it
                it.toggle(script.texts, NarrationLanguage.SPANISH, pageIdentity = page)
                assertEquals("A cold reader restores the saved source segment", 1, field<Int>(it, "index"))
                assertEquals("A cold reader restores the exact real paused offset", pausedOffset, field<Long>(it, "offset"))
                restoredPreparation = restoredModel.states(page).toMap()
                assertFalse("Unknown past speech keeps normal presentation instead of a false unread state", restoredPreparation.containsKey(0))
                assertFalse("The partially heard source block is also uncertain before actual playback", restoredPreparation.containsKey(1))
                assertEquals("Future speech remains reliably unread", ReadingBlockState.UNREAD, restoredPreparation[2])
            }
        }
        await("The recreated controller actually plays the cached WAV at the saved position", 30_000) {
            onMain {
                assertTrue(errors.toString(), errors.isEmpty())
                restored.isPlaying && field<Boolean>(restored, "playerReady") &&
                    field<MediaPlayer>(restored, "player").currentPosition >= pausedOffset &&
                    restoredModel.states(page)[1] == ReadingBlockState.CURRENT
            }
        }
        val restoredModified = audio.lastModified()
        onMain {
            assertFalse("Cold restoration does not invent already-read or unread evidence for a past block", restoredModel.states(page).containsKey(0))
            assertEquals(ReadingBlockState.CURRENT, restoredModel.states(page)[1])
            assertEquals(ReadingBlockState.UNREAD, restoredModel.states(page)[2])
            assertEquals("The recreated player uses the same persistent WAV", audio.canonicalPath,
                field<File>(field<List<Any>>(restored, "segments")[1], "file").canonicalPath)
            restored.pauseKeepingPosition()
            assertNull(field<MediaPlayer?>(restored, "player"))
            assertTrue("The restored reader progresses from its saved source position",
                requireNotNull(store.restore(key, 3)).positionMs >= pausedOffset)
        }
        assertEquals(bytes, audio.length())
        assertEquals("Cold playback reuses identical audio bytes", hash, sha256(audio))
        // Existing cache lookup refreshes last-used mtime; that must remain intact.
        assertTrue("Reusing a cached file preserves the existing access-time policy", restoredModified >= originalModified)
        onMain {
            restored.restart()
            assertEquals(0, field<Int>(restored, "index"))
            assertEquals(0L, field<Long>(restored, "offset"))
            assertEquals("Explicit restart clears both uncertainty and reading evidence",
                mapOf(0 to ReadingBlockState.UNREAD, 1 to ReadingBlockState.UNREAD, 2 to ReadingBlockState.UNREAD),
                restoredModel.states(page))
            restored.pauseKeepingPosition()
            assertNull(field<MediaPlayer?>(restored, "player"))
            restored.close()
        }
        await("The recreated worker also closes without leaking playback", 15_000) {
            !field<Thread>(restored, "workerThread").isAlive
        }
        extraController = null
        assertEquals(hash, sha256(audio))
        assertEquals("Pause and restart do not rewrite the restored source WAV", restoredModified, audio.lastModified())
        assertTrue(restoredEvents.any { it.phase == NarrationPlaybackPhase.PREPARED && it.segmentIndex == 1 && it.positionMs == pausedOffset })
        File(evidence(), "real-cold-bookmark-restoration.json").writeText(JSONObject()
            .put("engine", NarrationEngine.KOKORO.name).put("restoredSegment", 1)
            .put("restoredSourceOffsetMillis", pausedOffset)
            .put("preparationStates", JSONObject(restoredPreparation.mapKeys { it.key.toString() }.mapValues { it.value.name }))
            .put("pastUnknownBlockUsesOriginalPresentation", true).put("futureBlockUnread", true)
            .put("actualCurrentBlock", 1).put("sameAudioSha256", hash).put("sameAudioBytes", bytes)
            .put("originalAudioModifiedMillis", originalModified).put("cacheAccessModifiedMillis", restoredModified)
            .put("existingCacheAccessTimePolicyPreserved", true).put("restartSourceOffsetMillis", 0)
            .put("phases", JSONArray(restoredEvents.map { it.phase.name }.distinct())).toString(2))
    }

    private data class TextFact(val view: TextView, val text: CharSequence, val source: String,
        val description: CharSequence?, val width: Int, val height: Int, val left: Int, val top: Int,
        val colors: ColorStateList, val alpha: Float, val enabled: Boolean, val clickable: Boolean,
        val spans: List<String>)

    private fun facts(root: View): List<TextFact> = descendants(root).filterIsInstance<TextView>().map {
        TextFact(it, it.text, it.text.toString(), it.contentDescription, it.width, it.height, it.left, it.top,
            it.textColors, it.alpha, it.isEnabled, it.isClickable, spans(it.text))
    }
    private fun assertFacts(facts: List<TextFact>) {
        facts.forEach { original ->
            val view = original.view
            val label = "${view.javaClass.simpleName} id=${view.id} text=${original.source}"
            assertSame("Original displayed CharSequence remains the same object: $label", original.text, view.text)
            assertEquals("Original displayed text: $label", original.source, view.text.toString())
            assertEquals("Original accessible description: $label", original.description, view.contentDescription)
            assertEquals("Width is unchanged: $label", original.width, view.width)
            assertEquals("Height is unchanged: $label", original.height, view.height)
            assertEquals("Left is unchanged: $label", original.left, view.left)
            assertEquals("Top is unchanged: $label", original.top, view.top)
            assertEquals("Opacity is unchanged: $label", original.alpha, view.alpha, 0f)
            assertEquals("Enabled state is unchanged: $label", original.enabled, view.isEnabled)
            assertEquals("Clickability is unchanged: $label", original.clickable, view.isClickable)
            assertEquals("Span identity, range and flags are retained: $label", original.spans, spans(view.text))
            if (view is Button || view is EditText || view.isClickable ||
                (view.text as? Spanned)?.getSpans(0, view.text.length, ForegroundColorSpan::class.java)?.isNotEmpty() == true)
                assertColorBehavior(original.colors, view.textColors,
                    "Controls and syntax coloring are untouched for ${view.javaClass.simpleName} id=${view.id}: ${original.source}")
        }
    }
    private fun assertColorBehavior(expected: ColorStateList, actual: ColorStateList, message: String) {
        // Cached-page rendering reapplies AppearanceUi, which legitimately recreates button
        // state lists. Compare their actual presentation in every combination of UI states.
        assertEquals("$message (default color)", expected.defaultColor, actual.defaultColor)
        for (mask in 0 until (1 shl COLOR_STATES.size)) {
            val states = COLOR_STATES.filterIndexed { index, _ -> mask and (1 shl index) != 0 }.toIntArray()
            assertEquals("$message (state=$mask)",
                expected.getColorForState(states, expected.defaultColor), actual.getColorForState(states, actual.defaultColor))
        }
    }
    private fun spans(text: CharSequence): List<String> = (text as? Spanned)?.let { value ->
        value.getSpans(0, value.length, Any::class.java).map {
            "${it.javaClass.name}/${System.identityHashCode(it)}/${value.getSpanStart(it)}/${value.getSpanEnd(it)}/${value.getSpanFlags(it)}"
        }
    }.orEmpty()

    private fun fixture(): JSONObject {
        val published = LessonRepository.loadLessonJson(context, ROUTE,
            "math_geometria_plana__fundamentos_geometricos__del_trazo_a_la_figura_geometrica")
            .getJSONArray("pages").getJSONObject(0).getJSONArray("blocks").getJSONObject(4)
        return JSONObject().put("id", "math_course_01__estructura_y_divisibilidad__reading_qa_${UUID.randomUUID().toString().replace("-", "")}")
            .put("title", "Seguimiento")
            .put("pages", JSONArray().put(JSONObject().put("blocks", JSONArray()
                .put(JSONObject().put("type", "paragraph").put("text", INTRO))
                .put(JSONObject().put("type", "paragraph").put("text", FORMULA))
                .put(JSONObject().put("type", "code").put("language", "cpp").put("code", CODE))
                .put(JSONObject().put("type", "paragraph").put("text", "Este texto sigue sin narrar y conserva el orden, el tamaño y la distribución de la página."))
                .put(JSONObject(published.toString()))
                .put(JSONObject().put("type", "output_prediction").put("question", "Escribe la salida literal del código.")
                    .put("code", "std::cout << 2;").put("answers", JSONArray().put("2")))))
                .put(JSONObject().put("blocks", JSONArray()
                    .put(JSONObject().put("type", "heading").put("text", "Otra página"))
                    .put(JSONObject().put("type", "paragraph").put("text", "La segunda página mantiene su propio progreso visual sin preparación curricular adicional.")))))
    }

    private fun states(activity: LessonActivity): Map<Int, ReadingBlockState> {
        val model = field<Any>(activity, "readingProgress")
        @Suppress("UNCHECKED_CAST")
        return model.javaClass.getMethod("states", String::class.java)
            .invoke(model, "$ROUTE/${field<String>(activity, "currentLessonId")}/${field<Int>(activity, "currentPage")}") as Map<Int, ReadingBlockState>
    }
    private fun controller(activity: LessonActivity): LessonNarrationController = field(activity, "narrationController")
    private fun content(activity: LessonActivity): ViewGroup = activity.findViewById(R.id.lessonContent)
    private fun texts(activity: LessonActivity) = descendants(content(activity)).filterIsInstance<TextView>()
    private fun descendants(view: View): List<View> = listOf(view) + if (view is ViewGroup)
        (0 until view.childCount).flatMap { descendants(view.getChildAt(it)) } else emptyList()
    @Suppress("UNCHECKED_CAST") private fun <T> field(instance: Any, name: String): T =
        (if (name == "unreadRanges" && instance is ReadingTextHost) instance.readingInk else instance).let { owner ->
            owner.javaClass.getDeclaredField(name).apply { isAccessible = true }.get(owner) as T
        }
    private fun setField(instance: Any, name: String, value: Any) = instance.javaClass.getDeclaredField(name)
        .apply { isAccessible = true }.set(instance, value)
    private fun invoke(instance: Any, name: String) = instance.javaClass.getDeclaredMethod(name)
        .apply { isAccessible = true }.invoke(instance)
    private fun <T> onMain(block: () -> T): T {
        val value = AtomicReference<T>()
        instrumentation.runOnMainSync { value.set(block()) }
        return value.get()
    }
    private fun await(message: String, timeout: Long, ready: () -> Boolean) {
        val deadline = SystemClock.uptimeMillis() + timeout
        while (SystemClock.uptimeMillis() < deadline) {
            instrumentation.waitForIdleSync()
            if (ready()) return
            SystemClock.sleep(40)
        }
        fail(message)
    }
    private fun awaitLayout(host: ActivityScenario<LessonActivity>, id: String) = await("The retained fixture finishes native layout", 8_000) {
        var ready = false
        host.onActivity { activity ->
            assertEquals("Never reinject or conceal a replaced fixture", id, field<String>(activity, "currentLessonId"))
            ready = content(activity).width > 0 && !content(activity).isLayoutRequested
        }
        ready
    }
    private fun capture(activity: LessonActivity, name: String) {
        val bitmap = requireNotNull(instrumentation.uiAutomation.takeScreenshot())
        try {
            File(evidence(), "$name.png").outputStream().use { assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) }
            File(evidence(), "$name.json").writeText(JSONObject().put("source", "real native display, unmodified")
                .put("package", activity.packageName).put("lesson", field<String>(activity, "currentLessonId"))
                .put("page", field<Int>(activity, "currentPage"))
                .put("playing", controller(activity).isPlaying)
                .put("states", JSONObject(states(activity).mapKeys { it.key.toString() }.mapValues { it.value.name }))
                .put("texts", JSONArray(texts(activity).map { JSONObject().put("text", it.text.toString())
                    .put("color", it.currentTextColor).put("width", it.width).put("height", it.height)
                    .put("spans", JSONArray(spans(it.text))) })).toString(2))
        } finally { bitmap.recycle() }
    }
    private fun saveCanvas(view: View, name: String) {
        val bitmap = Bitmap.createBitmap(view.width, view.height, Bitmap.Config.ARGB_8888)
        try {
            view.draw(Canvas(bitmap))
            File(evidence(), "$name.png").outputStream().use { assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) }
        } finally { bitmap.recycle() }
    }
    private fun evidence() = File(requireNotNull(context.getExternalFilesDir(null)), "reading-progress-qa").apply { mkdirs() }
    private fun sha256(file: File) = MessageDigest.getInstance("SHA-256").digest(file.readBytes())
        .joinToString("") { "%02x".format(it.toInt() and 255) }
    private fun preferenceFiles() = File(context.applicationInfo.dataDir, "shared_prefs").listFiles().orEmpty()
        .filter { it.name.endsWith(".xml") }.map { it.name.removeSuffix(".xml") }.toSet()

    companion object {
        private val COLOR_STATES = intArrayOf(android.R.attr.state_enabled, android.R.attr.state_pressed,
            android.R.attr.state_focused, android.R.attr.state_selected, android.R.attr.state_activated,
            android.R.attr.state_checked, android.R.attr.state_checkable, android.R.attr.state_window_focused,
            android.R.attr.state_hovered)
        private const val QA_PACKAGE = "com.learngraphicsprogramming.app.curricularqa"
        private const val ROUTE = "math_physics"
        private const val SOURCE_LESSON = "math_course_01__estructura_y_divisibilidad__divisibilidad_y_primos"
        private const val INTRO = "El seguimiento conserva las palabras originales y acompaña solamente el avance real de la narración, sin inventar tiempos."
        private const val FORMULA = "La igualdad \\(1/2 = 2/4\\) conserva sus fracciones y su presentación habitual mientras el narrador explica esta equivalencia."
        private const val CODE = "#include <iostream>\nint main() {\n    std::cout << 2;\n}\n"
        private val KNOWN_PREFERENCES = setOf("learning_progress", "navigation_state", "lesson_narration_settings",
            "app_settings", "appearance_settings", "ui_language_settings", "premium_state", "lesson_text_size_settings")
    }
}
