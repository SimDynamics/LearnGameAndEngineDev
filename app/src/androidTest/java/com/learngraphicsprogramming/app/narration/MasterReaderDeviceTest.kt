package com.learngraphicsprogramming.app.narration

import android.app.Activity
import android.app.Application
import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Rect
import android.media.MediaPlayer
import android.os.Build
import android.os.SystemClock
import android.util.Base64
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.Spinner
import android.widget.Switch
import android.widget.TextView
import androidx.test.core.app.ActivityScenario
import androidx.test.espresso.Espresso.onData
import androidx.test.espresso.Espresso.onView
import androidx.test.espresso.action.ViewActions.click
import androidx.test.espresso.action.ViewActions.scrollTo
import androidx.test.espresso.matcher.RootMatchers.isDialog
import androidx.test.espresso.matcher.ViewMatchers.isAssignableFrom
import androidx.test.espresso.matcher.ViewMatchers.withText
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.AppearanceSettingsRepository
import com.learngraphicsprogramming.app.BuildConfig
import com.learngraphicsprogramming.app.LessonActivity
import com.learngraphicsprogramming.app.LessonOptionsActivity
import com.learngraphicsprogramming.app.OptionsActivity
import com.learngraphicsprogramming.app.PremiumRepository
import com.learngraphicsprogramming.app.ProgressRepository
import com.learngraphicsprogramming.app.R
import com.learngraphicsprogramming.app.UiLanguageSettings
import java.io.File
import java.security.MessageDigest
import java.util.concurrent.atomic.AtomicReference
import org.hamcrest.Matchers.anything
import org.hamcrest.Matchers.allOf
import org.json.JSONArray
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.FixMethodOrder
import org.junit.Test
import org.junit.runner.RunWith
import org.junit.runners.MethodSorters

/** Runs only in the isolated APK on LDPlayer, sharing its genuine installed model files. */
@RunWith(AndroidJUnit4::class)
@FixMethodOrder(MethodSorters.NAME_ASCENDING)
class MasterReaderDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val app = instrumentation.targetContext
    private var names = emptySet<String>()
    private var before = emptyMap<String, Map<String, *>>()
    private var locale = ""
    private var modelFiles = emptyMap<String, Pair<Long, Long>>()
    private val scenarios = mutableListOf<ActivityScenario<out Activity>>()
    private val readers = mutableListOf<LessonNarrationController>()
    private val durableFiles = linkedMapOf<String, ByteArray?>()
    private var captured = false
    private val evidence: File get() = File(requireNotNull(app.getExternalFilesDir(null)), "master-reader-qa")
        .apply { check(isDirectory || mkdir()) }
    private val recoveryFile: File get() = File(evidence, "pending-restoration.json")

    @Before fun preserveRealInstallation() {
        assertEquals("Never run in the user's normal application", QA_PACKAGE, app.packageName)
        assertEquals(QA_PACKAGE, BuildConfig.APPLICATION_ID)
        assertTrue("LDPlayer only", Build.SUPPORTED_ABIS.any { it == "x86" || it == "x86_64" })
        recoverInterruptedRun()
        names = preferenceNames()
        before = (names + KNOWN_PREFERENCES).associateWith(::preference)
        locale = onMain { UiLanguageSettings.selectedTag(app) }
        modelFiles = modelRoot().listFiles().orEmpty().associate { it.name to (it.length() to it.lastModified()) }
        listOf(KokoroPagePreparationQueue.FILE_NAME, NarrationAudioPlanner.FILE_NAME).forEach { name ->
            durableFiles[name] = File(app.filesDir, name).takeIf(File::isFile)?.readBytes()
        }
        captured = true
        preserveRecoverySnapshot()
        onMain {
            NarrationSettingsRepository.saveEnabled(app, false)
            UiLanguageSettings.select(app, "es")
            NarrationSettingsRepository.saveEngine(app, NarrationEngine.KOKORO)
            NarrationSettingsRepository.saveLanguage(app, NarrationLanguage.SPANISH)
            NarrationSettingsRepository.saveKokoroVoice(app, "ef_dora")
            NarrationSettingsRepository.saveRate(app, 1f)
            NarrationSettingsRepository.saveControlsVisible(app, true)
            PremiumRepository.setVerifiedPremium(app)
            ProgressRepository.saveCurrentPage(app, AppearanceSettingsRepository.ROUTE_PROGRAMMING, LESSON_ID, 0)
        }
    }

    @After fun preservePreferencesProgressModelsAndPositions() {
        if (!captured) return
        onMain { NarrationSettingsRepository.saveEnabled(app, false) }
        readers.forEach { reader -> onMain { reader.close() } }
        scenarios.asReversed().forEach { it.close() }
        await("Test-owned reader threads close", 30_000) { readers.all { !field<Thread>(it, "workerThread").isAlive } }
        app.stopService(Intent(app, KokoroContinuousNarrationService::class.java))
        await("The QA service stops before restoring its durable state", 30_000) {
            Thread.getAllStackTraces().keys.none { it.isAlive && it.name == "KokoroOfflineNarration" }
        }
        instrumentation.waitForIdleSync()
        restoreOwnedState(names, before, locale, durableFiles)
        assertEquals("All genuine downloaded models and voices remain unchanged", modelFiles,
            modelRoot().listFiles().orEmpty().associate { it.name to (it.length() to it.lastModified()) })
        assertEquals("User preferences, curriculum progress and three saved positions are restored", before,
            (names + KNOWN_PREFERENCES).associateWith(::preference))
        assertTrue("A completed cleanup removes only its QA recovery marker", recoveryFile.delete())
    }

    @Test fun a_lessonHeadNeverEnablesMaster() {
        onMain { NarrationSettingsRepository.savePosition(app, 0.5f, 0.75f) }
        val lesson = launchLesson()
        lesson.onActivity { activity ->
            val head = activity.findViewById<View>(R.id.buttonNarrationToggle)
            val row = activity.findViewById<ViewGroup>(R.id.lessonNarrationControls)
            val controls = (0 until row.childCount).map(row::getChildAt)
            val settingsBeforeClicks = NarrationSettingsRepository.get(activity)
            val positionsBeforeClicks = preference("lesson_narration_resume_bounded_v1")
            assertEquals("A previously shown floating bar remains visible while master is off", View.VISIBLE, row.visibility)
            assertTrue("The disabled bar remains on screen for the visual evidence", row.getGlobalVisibleRect(Rect()))
            assertEquals("All existing floating actions remain present", 7, controls.size)
            controls.forEach { control ->
                assertFalse("Master off disables ${viewLabel(control)}", control.isEnabled)
                assertTrue("Master off dims ${viewLabel(control)}", effectiveAlpha(control) < 0.8f)
                val color = when (control) {
                    is TextView -> control.currentTextColor
                    is android.widget.ImageView -> requireNotNull(control.imageTintList).defaultColor
                    else -> error("Unexpected floating control ${control.javaClass.simpleName}")
                }
                assertTrue("Disabled floating controls render gray", kotlin.math.abs(android.graphics.Color.red(color) -
                    android.graphics.Color.blue(color)) <= 20)
            }
            screenshot(activity, "a-disabled-lesson-head.png")
            assertFalse("The head shortcut must not enable the reader when master is off", head.isEnabled)
            assertTrue("The disabled head must be visibly dimmed", effectiveAlpha(head) < 0.8f)
            head.performClick()
            controls.forEach { it.performClick() }
            activity.findViewById<View>(R.id.buttonLessonBackFive).performLongClick()
            assertFalse("The head shortcut cannot change the master preference", NarrationSettingsRepository.get(app).enabled)
            assertEquals("Disabled floating buttons cannot change narration preferences", settingsBeforeClicks,
                NarrationSettingsRepository.get(activity))
            assertTrue("A disabled bar click cannot hide or re-enable the controls", NarrationSettingsRepository.controlsVisible(activity))
            assertEquals("Disabled actions cannot create or move a saved reading position", positionsBeforeClicks,
                preference("lesson_narration_resume_bounded_v1"))
            assertFalse("No disabled head click may start a decoder", reader(activity).isPlaying)
            assertNull("Disabled actions cannot allocate a decoder", field<MediaPlayer?>(reader(activity), "player"))
            assertTrue("Disabled actions cannot queue synthesis", field<Set<*>>(reader(activity), "pending").isEmpty())
        }
    }

    @Test fun b_disabledGlobalAndLessonControlsAreDimmed() {
        val options = launchOptions()
        options.onActivity { activity ->
            val host = activity.findViewById<ViewGroup>(R.id.narrationSettingsSection)
            val master = descendants(host).filterIsInstance<Switch>().single()
            assertTrue("The master remains interactive", master.isEnabled)
            assertFalse(master.isChecked)
            val controls = descendants(host).filter { it !== master && (it.isClickable || it is android.widget.SeekBar) }
            assertTrue("Exercise installed voices, language, engine, resources and storage controls", controls.size >= 10)
            screenshot(activity, "b-global-master-disabled.png")
            controls.forEach { view ->
                assertFalse("Disabled master must gate ${viewLabel(view)}", view.isEnabled)
                assertTrue("Disabled master must dim ${viewLabel(view)}", effectiveAlpha(view) < 0.8f)
                if (view is TextView) {
                    val color = view.currentTextColor
                    assertTrue("Disabled narration controls must actually render gray", kotlin.math.abs(android.graphics.Color.red(color) -
                        android.graphics.Color.blue(color)) <= 20)
                }
            }
        }
        toggleMasterWithRealTouch()
        options.onActivity { activity ->
            assertTrue("The master explicitly restores normal availability", NarrationSettingsRepository.get(activity).enabled)
            val section = requireNotNull(field<NarrationOptionsSection?>(activity, "narrationOptions"))
            listOf("engineButton", "languageButton", "voiceButton", "voicesButton", "storageSlider").forEach { name ->
                val view = field<View>(section, name)
                assertTrue("Manual enable restores $name", view.isEnabled)
                assertTrue("Manual enable restores normal presentation for $name", effectiveAlpha(view) >= 0.99f)
            }
            screenshot(activity, "b-global-master-reactivated.png")
        }
        toggleMasterWithRealTouch()
        options.onActivity { activity ->
            assertFalse(NarrationSettingsRepository.get(activity).enabled)
        }
        launchLessonOptions().onActivity { activity ->
            listOf(R.id.buttonLessonNarrationLanguage, R.id.buttonLessonNarrationVoice).forEach { id ->
                val view = activity.findViewById<View>(id)
                assertFalse("Lesson narration selector is disabled", view.isEnabled)
                assertTrue("Lesson narration selector is visibly dimmed", effectiveAlpha(view) < 0.8f)
            }
            screenshot(activity, "b-lesson-options-master-disabled.png")
        }
    }

    @Test fun c_lessonHeaderSynchronizesLanguageOnlyOnExplicitAppSelection() {
        onMain {
            NarrationSettingsRepository.saveEngine(app, NarrationEngine.ANDROID)
            NarrationSettingsRepository.saveLanguage(app, NarrationLanguage.BRITISH_ENGLISH)
            NarrationSettingsRepository.saveEnabled(app, true)
            ProgressRepository.saveCurrentPage(app, AppearanceSettingsRepository.ROUTE_PROGRAMMING, LESSON_ID, 1)
        }
        val lesson = launchLesson()
        val progress = preference("learning_progress")
        val originalPage = AtomicReference<Int>()
        lesson.onActivity { originalPage.set(field(it, "currentPage")) }
        fun choose(tag: String, expected: NarrationLanguage) {
            var index = 0
            var previousActivity: Activity? = null
            lesson.onActivity { activity ->
                previousActivity = activity
                index = (listOf("") + UiLanguageSettings.supportedTags(activity)).indexOf(tag)
                assertTrue(index > 0)
                activity.findViewById<View>(R.id.buttonLessonLanguage).performClick()
            }
            onData(anything()).inRoot(isDialog()).atPosition(index).perform(click())
            await("The header selection updates UI and narration immediately", 20_000) {
                UiLanguageSettings.selectedTag(app) == tag && NarrationSettingsRepository.get(app).language == expected
            }
            awaitLocale(lesson, tag, previousActivity)
            lesson.onActivity { activity ->
                assertEquals("Changing language keeps the current page", originalPage.get(), field<Int>(activity, "currentPage"))
                screenshot(activity, "c-header-language-$tag.png")
            }
            assertEquals("Language changes never clear curriculum progress", progress, preference("learning_progress"))
        }
        choose("fr", NarrationLanguage.FRENCH)
        choose("es", NarrationLanguage.SPANISH)
        choose("en", NarrationLanguage.BRITISH_ENGLISH)
        lesson.onActivity { activity ->
            repeat(4) {
                activity.findViewById<View>(R.id.buttonNarrationToggle).performClick()
                assertTrue("Showing/hiding the bar cannot toggle the master", NarrationSettingsRepository.get(activity).enabled)
            }
        }
        onMain { NarrationSettingsRepository.saveLanguage(app, NarrationLanguage.FRENCH) }
        lesson.recreate()
        lesson.onActivity {
            assertEquals("Activity recreation cannot force narration back to the UI language", NarrationLanguage.FRENCH,
                NarrationSettingsRepository.get(it).language)
            assertEquals("en", UiLanguageSettings.selectedTag(it))
            assertEquals(originalPage.get(), field<Int>(it, "currentPage"))
        }
    }

    @Test fun d_globalAppLanguageAndIndependentReaderSelectionPersistWithoutReset() {
        onMain { NarrationSettingsRepository.saveEngine(app, NarrationEngine.ANDROID) }
        val options = launchOptions()
        options.onActivity { activity -> master(activity).performClick() }
        assertTrue("Only the explicit master action enables narration", NarrationSettingsRepository.get(app).enabled)
        fun chooseReader(language: NarrationLanguage) {
            options.onActivity { activity ->
                val section = requireNotNull(field<NarrationOptionsSection?>(activity, "narrationOptions"))
                field<View>(section, "languageButton").performClick()
            }
            onData(anything()).inRoot(isDialog()).atPosition(NarrationLanguage.entries.indexOf(language)).perform(click())
            assertEquals(language, NarrationSettingsRepository.get(app).language)
            options.onActivity { activity ->
                val section = requireNotNull(field<NarrationOptionsSection?>(activity, "narrationOptions"))
                assertEquals("The visible selector reflects the new preference immediately",
                    activity.getString(R.string.narration_selected_language, language.label(activity)),
                    field<TextView>(section, "languageButton").text.toString())
            }
        }
        chooseReader(NarrationLanguage.BRITISH_ENGLISH)
        val progress = preference("learning_progress")
        fun chooseApp(tag: String, expected: NarrationLanguage) {
            var previousActivity: Activity? = null
            options.onActivity { activity ->
                previousActivity = activity
                val spinner = activity.findViewById<Spinner>(R.id.spinnerAppLanguage)
                spinner.setSelection((listOf("") + UiLanguageSettings.supportedTags(activity)).indexOf(tag))
            }
            await("Global language changes must synchronize the reader", 20_000) {
                UiLanguageSettings.selectedTag(app) == tag && NarrationSettingsRepository.get(app).language == expected
            }
            awaitLocale(options, tag, previousActivity)
            options.onActivity { activity ->
                val section = requireNotNull(field<NarrationOptionsSection?>(activity, "narrationOptions"))
                assertEquals(activity.getString(R.string.narration_selected_language, expected.label(activity)),
                    field<TextView>(section, "languageButton").text.toString())
            }
        }
        chooseApp("fr", NarrationLanguage.FRENCH)
        chooseReader(NarrationLanguage.AMERICAN_ENGLISH)
        assertEquals("Choosing a reader language never changes the UI language", "fr", UiLanguageSettings.selectedTag(app))
        options.recreate()
        options.onActivity { activity ->
            assertEquals("Reopening options preserves the user's independent reader selection", NarrationLanguage.AMERICAN_ENGLISH,
                NarrationSettingsRepository.get(activity).language)
            assertEquals("fr", UiLanguageSettings.selectedTag(activity))
        }
        chooseApp("es", NarrationLanguage.SPANISH)
        chooseApp("en", NarrationLanguage.AMERICAN_ENGLISH)
        chooseReader(NarrationLanguage.BRITISH_ENGLISH)
        chooseApp("fr", NarrationLanguage.FRENCH)
        chooseApp("en", NarrationLanguage.BRITISH_ENGLISH)
        assertEquals(progress, preference("learning_progress"))
        options.onActivity { screenshot(it, "d-global-language-independent-reader.png") }
    }

    @Test fun e_masterOffInterruptsActualPlaybackAndManualEnableRestoresExactBookmark() {
        val options = launchOptions()
        options.onActivity { master(it).performClick() }
        val errors = java.util.concurrent.CopyOnWriteArrayList<String>()
        val reader = onMain { LessonNarrationController(app, {}, { errors += it }, trackWords = true) }.also(readers::add)
        val lesson = JSONObject(app.assets.open(PROGRAMMING_ASSET).bufferedReader().use { it.readText() })
        val authored = lesson.toString()
        val texts = LessonNarrationText.page(lesson, 0, NarrationLanguage.SPANISH, forKokoro = true)
        val identity = "qa-master-reader/playback/${System.nanoTime()}"
        onMain { reader.toggle(texts, NarrationLanguage.SPANISH, identity) }
        await("Genuine Kokoro starts the authored lesson without Android TTS", 120_000) {
            assertTrue("No neural failures may be hidden: $errors", errors.isEmpty())
            onMain { ready(reader) && requireNotNull(field<MediaPlayer?>(reader, "player")).currentPosition >= 250 }
        }
        val key = onMain { requireNotNull(field<String?>(reader, "resumeKey")) }
        var beforePause = 0L
        var saved: NarrationResumePoint? = null
        options.onActivity { activity ->
            beforePause = requireNotNull(field<MediaPlayer?>(reader, "player")).currentPosition.toLong()
            master(activity).performClick()
            screenshot(activity, "e-master-disabled-during-kokoro.png")
        }
        await("Master off releases actual playback and its pending synthesis", 30_000) {
            onMain { !reader.isPlaying && field<MediaPlayer?>(reader, "player") == null &&
                field<Set<*>>(reader, "pending").isEmpty() && !field<Boolean>(reader, "playerReady") }
        }
        saved = requireNotNull(NarrationResumeStore(app).restore(key, NarrationSegments.sentences(texts).size))
        assertTrue("Disabling saves the exact real decoder position", requireNotNull(saved).positionMs >= beforePause)
        val retained = preference("lesson_narration_resume_bounded_v1")
        val recordings = File(app.filesDir, "kokoro-narration-cache").listFiles().orEmpty().filter { it.isFile }
            .associate { it.name to sha256(it) }
        val cacheCount = recordings.size
        val stoppedAt = System.currentTimeMillis()
        SystemClock.sleep(1_000)
        onMain {
            assertFalse(reader.isPlaying)
            assertNull("Master off leaves no decoder", field<MediaPlayer?>(reader, "player"))
            assertNull("Master off shuts down Android TTS when present", field<Any?>(reader, "tts"))
        }
        assertTrue("No disabled synthesis may complete and start another recording", synthesisEvents().none { it.optLong("at") > stoppedAt })
        assertEquals("A disabled reader never modifies the saved page position", retained,
            preference("lesson_narration_resume_bounded_v1"))
        options.onActivity { activity ->
            assertFalse(NarrationSettingsRepository.get(activity).enabled)
            master(activity).performClick()
        }
        assertTrue(NarrationSettingsRepository.get(app).enabled)
        onMain { reader.toggle(texts, NarrationLanguage.SPANISH, identity) }
        await("Manual activation resumes the existing cached sentence", 120_000) {
            assertTrue("No Kokoro failures: $errors", errors.isEmpty())
            onMain { ready(reader) && field<Int>(reader, "index") == requireNotNull(saved).segment &&
                requireNotNull(field<MediaPlayer?>(reader, "player")).currentPosition >= requireNotNull(saved).positionMs }
        }
        onMain { reader.pauseKeepingPosition() }
        recordings.forEach { (name, hash) -> assertEquals("Installed valid audio remains byte-identical", hash,
            sha256(File(app.filesDir, "kokoro-narration-cache/$name"))) }
        assertEquals(authored, lesson.toString())
        writeReport("e-playback-master.json", JSONObject().put("actualKokoro", true).put("androidTtsUsed", false)
            .put("decoderPositionBeforeDisableMs", beforePause).put("savedPositionMs", requireNotNull(saved).positionMs)
            .put("masterDisableReleasedMediaPlayer", true).put("resumeAtExistingPosition", true)
            .put("previousCacheFilesUntouched", cacheCount).put("errors", JSONArray(errors.toList())))
    }

    @Test fun f_masterOffCancelsRealBackgroundPreparationAndBlocksNavigationRestarts() {
        val options = launchOptions()
        options.onActivity { master(it).performClick() }
        val sentence = "La prueba número ${SystemClock.elapsedRealtime() % 100_000L} necesita conservar exactamente el resultado y todos sus símbolos. " +
            "El programa comprueba una condición verdadera antes de mostrar la salida solicitada."
        val texts = listOf(sentence, "La variable resultado conserva una cantidad entera.")
        val keys = NarrationSegments.sentences(texts).map { KokoroNarrationCache.key("es", "ef_dora", it) }
        assertTrue("Background cancellation needs genuine uncached audio, not an injected recording",
            keys.any { !File(app.filesDir, "kokoro-narration-cache/$it.wav").isFile })
        onMain {
            val queued = KokoroPagePreparationQueue.submit(app, texts, NarrationLanguage.SPANISH, "ef_dora")
            NarrationAudioPlanner.enter(app, AppearanceSettingsRepository.ROUTE_PROGRAMMING, "cpp_fundamentals",
                LESSON_ID, 0, queued.revision)
            KokoroBackgroundNarrationWorker.start(app)
        }
        await("Observe actual ONNX preparation of the requested audio", 120_000) {
            val status = KokoroNarrationDiagnostics.snapshot(app)
            status.optString("workerState") == "synthesizing" && status.optString("currentHash") in keys
        }
        options.onActivity { activity -> master(activity).performClick(); screenshot(activity, "f-background-generation-disabled.png") }
        await("Master off cancels the real foreground preparation service and releases its engine", 30_000) {
            !KokoroNarrationDiagnostics.snapshot(app).optBoolean("serviceRunning", false) &&
                Thread.getAllStackTraces().keys.none { it.isAlive && it.name == "KokoroOfflineNarration" } &&
                field<Any?>(KokoroEngineCoordinator, "engine") == null
        }
        val stoppedAt = System.currentTimeMillis()
        val queueBefore = KokoroPagePreparationQueue.snapshot(app)?.encode()
        val lesson = launchLesson()
        lesson.onActivity { activity ->
            val head = activity.findViewById<View>(R.id.buttonNarrationToggle)
            assertFalse(head.isEnabled)
            repeat(3) { head.performClick() }
            assertFalse(NarrationSettingsRepository.get(activity).enabled)
        }
        onMain { UiLanguageSettings.select(app, "fr") }
        lesson.recreate()
        lesson.onActivity { activity ->
            assertFalse(NarrationSettingsRepository.get(activity).enabled)
            reader(activity).prefetchPage(texts, NarrationLanguage.SPANISH, "qa-disabled-prefetch")
            reader(activity).warmUp()
        }
        onMain { KokoroBackgroundNarrationWorker.start(app) }
        SystemClock.sleep(1_500)
        assertTrue("A disabled UI, language change, warmup or prefetch cannot generate narration",
            synthesisEvents().none { it.optLong("at") > stoppedAt })
        assertEquals("Disabled navigation does not replace the durable pending queue", queueBefore,
            KokoroPagePreparationQueue.snapshot(app)?.encode())
        assertNull("A disabled reader cannot recreate the ONNX session", field<Any?>(KokoroEngineCoordinator, "engine"))
        assertFalse(NarrationSettingsRepository.get(app).enabled)
        writeReport("f-background-master.json", JSONObject().put("actualOnnxSynthesisObserved", true)
            .put("masterCancelledServiceAndEngine", true).put("disabledNavigationLanguageWarmupPrefetchStarts", 0)
            .put("durableQueuePreserved", true).put("newDownloadRequests", 0))
    }

    @Test fun g_fourKokoroLanguagesKeepFinishedAndPausedPagesInTheSameThreeSlots() {
        val options = launchOptions()
        options.onActivity { master(it).performClick() }
        onMain { NarrationSettingsRepository.saveRate(app, 2f) }
        val errors = java.util.concurrent.CopyOnWriteArrayList<String>()
        val reader = onMain { LessonNarrationController(app, {}, { errors += it }, trackWords = true) }.also(readers::add)
        val samples = listOf(
            Triple(NarrationLanguage.SPANISH, "ef_dora", "es"),
            Triple(NarrationLanguage.FRENCH, "ff_siwis", "fr"),
            Triple(NarrationLanguage.AMERICAN_ENGLISH, "af_heart", "en"),
            Triple(NarrationLanguage.BRITISH_ENGLISH, "bf_emma", "en")
        )
        val keys = mutableListOf<String>()
        val report = JSONArray()
        var lastTexts = emptyList<String>()
        var lastIdentity = ""
        samples.forEachIndexed { index, (language, voice, directory) ->
            val lesson = JSONObject(app.assets.open(PROGRAMMING_ASSET.replace("content/es/", "content/$directory/"))
                .bufferedReader().use { it.readText() })
            val text = NarrationSegments.sentences(LessonNarrationText.page(lesson, 0, language, forKokoro = true)).first()
            val identity = "qa-master-language-${language.tag}/${System.nanoTime()}"
            onMain {
                NarrationSettingsRepository.saveLanguage(app, language)
                NarrationSettingsRepository.saveKokoroVoice(app, voice)
                reader.toggle(listOf(text), language, identity)
            }
            await("$voice must genuinely synthesize/play without Android TTS", 120_000) {
                assertTrue("Actual Kokoro regressions: $errors", errors.isEmpty())
                onMain { ready(reader) && requireNotNull(field<MediaPlayer?>(reader, "player")).currentPosition >= 120 }
            }
            val key = onMain { requireNotNull(field<String?>(reader, "resumeKey")) }
            keys += key
            if (index < samples.lastIndex) {
                await("The original $directory sentence completes naturally", 60_000) { onMain { field<Boolean>(reader, "finished") } }
                assertTrue(requireNotNull(NarrationResumeStore(app).restore(key, 1)).finished)
                onMain {
                    reader.toggle(listOf(text), language, identity)
                    assertFalse("A completed sentence never restarts automatically", reader.isPlaying)
                    assertTrue(field(reader, "finished"))
                }
            } else onMain { reader.pauseKeepingPosition() }
            onMain { assertNull("All four languages use Kokoro rather than fallback TTS", field<Any?>(reader, "tts")) }
            report.put(JSONObject().put("language", language.tag).put("voice", voice).put("actualPlayback", true)
                .put("finished", index < samples.lastIndex).put("resumeKey", key))
            lastTexts = listOf(text)
            lastIdentity = identity
        }
        val store = NarrationResumeStore(app)
        assertNull("The fourth actual page evicts the oldest finished page", store.restore(keys[0], 1))
        assertTrue(requireNotNull(store.restore(keys[1], 1)).finished)
        assertTrue(requireNotNull(store.restore(keys[2], 1)).finished)
        val paused = requireNotNull(store.restore(keys[3], 1))
        assertFalse(paused.finished)
        val history = preference("lesson_narration_resume_bounded_v1")
        assertEquals("Exactly three ordinary bookmark slots remain", 3, history.keys.count { it.startsWith("key_") })
        options.onActivity { master(it).performClick() }
        assertEquals("Master disable preserves finished and paused pages", history, preference("lesson_narration_resume_bounded_v1"))
        options.onActivity { master(it).performClick() }
        onMain { reader.toggle(lastTexts, NarrationLanguage.BRITISH_ENGLISH, lastIdentity) }
        await("Manual reactivation resumes the saved British English page", 60_000) {
            onMain { ready(reader) && requireNotNull(field<MediaPlayer?>(reader, "player")).currentPosition >= paused.positionMs }
        }
        onMain { reader.pauseKeepingPosition() }
        writeReport("g-four-languages-and-three-pages.json", JSONObject().put("samples", report)
            .put("finishedPagesUseOrdinarySlots", true).put("fourthPageEvictsOldest", true)
            .put("manualReactivationResumesPausedPage", true).put("androidTtsUsed", false)
            .put("errors", JSONArray(errors.toList())))
    }

    @Test fun h_interruptedRealCurriculumIteratorStopsBeforeItsFirstLazyScanAndKeepsInterruptFlag() {
        val preferencesBefore = (preferenceNames() + KNOWN_PREFERENCES).associateWith(::preference)
        val filesBefore = listOf(KokoroPagePreparationQueue.FILE_NAME, NarrationAudioPlanner.FILE_NAME).associateWith { name ->
            File(app.filesDir, name).takeIf(File::isFile)?.readBytes()
        }
        val authoredBefore = app.assets.open(PROGRAMMING_ASSET).use { it.readBytes() }
        val failure = AtomicReference<Throwable?>(null)
        val interruptRetained = AtomicReference(false)
        val yielded = AtomicReference(false)
        val thread = Thread({
            Thread.currentThread().interrupt()
            try {
                yielded.set(NarrationCurriculum.pages(app).iterator().hasNext())
            } catch (error: Throwable) {
                failure.set(error)
            } finally {
                interruptRetained.set(Thread.currentThread().isInterrupted)
            }
        }, "QaCancelledCurriculumIterator")
        thread.start()
        thread.join(5_000)
        assertFalse("The already-cancelled real catalog must finish rather than scan lesson assets", thread.isAlive)
        assertTrue("The lazy iterator must propagate the existing cancellation contract, not yield a page: ${failure.get()}",
            failure.get() is InterruptedException)
        assertTrue("Cancellation must preserve the worker interrupt flag for its owner", interruptRetained.get())
        assertFalse("An interrupted iterator cannot return a page ready for synthesis", yielded.get())
        assertEquals("Inspecting a cancelled catalog cannot change preferences, progress or three page positions",
            preferencesBefore, (preferenceNames() + KNOWN_PREFERENCES).associateWith(::preference))
        filesBefore.forEach { (name, bytes) ->
            val file = File(app.filesDir, name)
            if (bytes == null) assertFalse("A cancelled catalog cannot create $name", file.exists())
            else assertArrayEquals("A cancelled catalog cannot rewrite $name", bytes, file.readBytes())
        }
        assertArrayEquals("The actual packaged lesson remains byte-for-byte intact", authoredBefore,
            app.assets.open(PROGRAMMING_ASSET).use { it.readBytes() })
        writeReport("h-interrupted-real-curriculum.json", JSONObject()
            .put("alreadyInterruptedBeforeIteratorHasNext", true)
            .put("exception", requireNotNull(failure.get()).javaClass.name)
            .put("interruptFlagRetained", interruptRetained.get()).put("pageYielded", yielded.get())
            .put("preferencesQueuePlannerAndAuthoredJsonUnchanged", true))
    }

    private fun launchLesson(): ActivityScenario<LessonActivity> = ActivityScenario.launch<LessonActivity>(
        Intent(app, LessonActivity::class.java).putExtra("routeId", AppearanceSettingsRepository.ROUTE_PROGRAMMING)
            .putExtra("courseId", "cpp_fundamentals").putExtra("unitId", "programas_y_ejecucion")
            .putExtra("lessonId", LESSON_ID).putExtra("lessonNumber", 1).putExtra("lessonTotal", 6)
    ).also { scenario ->
        scenarios += scenario
        await("The authored lesson is visible and owns focus", 25_000) {
            var ready = false
            scenario.onActivity { activity ->
                activity.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                ready = activity.hasWindowFocus() && activity.findViewById<View>(R.id.lessonContent).width > 0
                field<LessonNarrationController?>(activity, "narrationController")?.let { if (it !in readers) readers += it }
            }
            ready
        }
    }
    private fun launchOptions(): ActivityScenario<OptionsActivity> = ActivityScenario.launch<OptionsActivity>(
        Intent(app, OptionsActivity::class.java)).also { scenarios += it; keepAwake(it) }
    private fun launchLessonOptions(): ActivityScenario<LessonOptionsActivity> = ActivityScenario.launch<LessonOptionsActivity>(
        Intent(app, LessonOptionsActivity::class.java).putExtra("routeId", AppearanceSettingsRepository.ROUTE_PROGRAMMING)
            .putExtra("courseId", "cpp_fundamentals").putExtra("lessonId", LESSON_ID).putExtra("lessonNumber", 1)
    ).also { scenarios += it; keepAwake(it) }
    private fun <A : Activity> keepAwake(scenario: ActivityScenario<A>) = scenario.onActivity {
        it.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
    }
    private fun reader(activity: LessonActivity): LessonNarrationController =
        requireNotNull(field<LessonNarrationController?>(activity, "narrationController")).also { if (it !in readers) readers += it }
    private fun master(activity: OptionsActivity) = descendants(activity.findViewById<ViewGroup>(R.id.narrationSettingsSection))
        .filterIsInstance<Switch>().single()
    private fun ready(reader: LessonNarrationController) = reader.isPlaying && field<Boolean>(reader, "playerReady") &&
        field<MediaPlayer?>(reader, "player") != null
    private fun synthesisEvents(): List<JSONObject> = File(app.filesDir, KokoroNarrationDiagnostics.EVENTS_FILE)
        .takeIf(File::isFile)?.readLines().orEmpty().mapNotNull { runCatching { JSONObject(it) }.getOrNull() }
        .filter { it.optString("event") in setOf("Page sentence generated", "Cached lesson speech") }
    private fun writeReport(name: String, value: JSONObject) = File(evidence, name).writeText(value.toString(2))
    private fun sha256(file: File): String {
        val digest = MessageDigest.getInstance("SHA-256")
        file.inputStream().buffered().use { stream ->
            val buffer = ByteArray(64 * 1024)
            while (true) { val count = stream.read(buffer); if (count < 0) break; digest.update(buffer, 0, count) }
        }
        return digest.digest().joinToString("") { "%02x".format(it.toInt() and 255) }
    }
    private fun descendants(root: View): List<View> = listOf(root) + if (root is ViewGroup)
        (0 until root.childCount).flatMap { descendants(root.getChildAt(it)) } else emptyList()
    private fun effectiveAlpha(view: View): Float = view.alpha * ((view.parent as? View)?.let(::effectiveAlpha) ?: 1f)
    private fun viewLabel(view: View) = (view as? TextView)?.text?.toString() ?: "${view.javaClass.simpleName}/${view.id}"
    private fun screenshot(activity: Activity, name: String) {
        val root = activity.window.decorView
        if (root.width <= 0 || root.height <= 0) return
        if (activity is OptionsActivity) {
            val host = activity.findViewById<View>(R.id.narrationSettingsSection)
            val scroll = descendants(root).filterIsInstance<android.widget.ScrollView>().first()
            val rectangle = Rect(0, 0, host.width, minOf(host.height, scroll.height))
            scroll.offsetDescendantRectToMyCoords(host, rectangle)
            // Draw the master, engine, language and voice controls at the top of the
            // actual options viewport rather than recording the unrelated page header.
            scroll.scrollTo(0, rectangle.top)
        }
        val bitmap = Bitmap.createBitmap(root.width, root.height, Bitmap.Config.ARGB_8888)
        root.draw(Canvas(bitmap))
        File(evidence, name).outputStream().use { bitmap.compress(Bitmap.CompressFormat.PNG, 100, it) }
        bitmap.recycle()
    }
    private fun toggleMasterWithRealTouch() = onView(allOf(isAssignableFrom(Switch::class.java),
        withText(R.string.narration_show_controls))).perform(scrollTo(), click())
    private fun modelRoot() = File(requireNotNull(app.getExternalFilesDir(null)), "kokoro/${KokoroModelManifest.revision}")
    private fun preserveRecoverySnapshot() {
        val preferences = JSONArray(before.map { (name, values) ->
            JSONObject().put("name", name).put("existed", name in names).put("values", JSONArray(values.map { (key, value) ->
                val kind = when (value) {
                    is String -> "string"
                    is Int -> "int"
                    is Long -> "long"
                    is Float -> "float"
                    is Boolean -> "boolean"
                    is Set<*> -> "set"
                    else -> error("Unsupported recovery preference $name/$key")
                }
                JSONObject().put("key", key).put("type", kind)
                    .put("value", if (value is Set<*>) JSONArray(value.toList()) else value)
            }))
        })
        val files = JSONArray(durableFiles.map { (name, bytes) -> JSONObject().put("name", name)
            .put("data", bytes?.let { Base64.encodeToString(it, Base64.NO_WRAP) } ?: JSONObject.NULL) })
        val models = JSONArray(modelFiles.map { (name, metadata) -> JSONObject().put("name", name)
            .put("bytes", metadata.first).put("modified", metadata.second) })
        KokoroPagePreparationQueue.writeAtomic(recoveryFile, JSONObject().put("format", 1).put("package", app.packageName)
            .put("locale", locale).put("preferences", preferences).put("durableFiles", files).put("models", models).toString())
    }
    private fun recoverInterruptedRun() {
        if (!recoveryFile.isFile) return
        val saved = JSONObject(recoveryFile.readText())
        assertEquals("Only this isolated APK's interrupted test may be recovered", app.packageName, saved.getString("package"))
        assertEquals(1, saved.getInt("format"))
        onMain { NarrationSettingsRepository.saveEnabled(app, false) }
        app.stopService(Intent(app, KokoroContinuousNarrationService::class.java))
        await("Interrupted QA preparation stops before original state is recovered", 30_000) {
            Thread.getAllStackTraces().keys.none { it.isAlive && it.name == "KokoroOfflineNarration" }
        }
        val originalNames = linkedSetOf<String>()
        val originalPreferences = linkedMapOf<String, Map<String, *>>()
        val preferences = saved.getJSONArray("preferences")
        repeat(preferences.length()) { index ->
            val row = preferences.getJSONObject(index)
            val name = row.getString("name")
            if (row.getBoolean("existed")) originalNames += name
            val values = linkedMapOf<String, Any>()
            val entries = row.getJSONArray("values")
            repeat(entries.length()) { valueIndex ->
                val entry = entries.getJSONObject(valueIndex)
                values[entry.getString("key")] = when (entry.getString("type")) {
                    "string" -> entry.getString("value")
                    "int" -> entry.getInt("value")
                    "long" -> entry.getLong("value")
                    "float" -> entry.getDouble("value").toFloat()
                    "boolean" -> entry.getBoolean("value")
                    "set" -> entry.getJSONArray("value").let { array -> (0 until array.length()).map { array.getString(it) }.toSet() }
                    else -> error("Unsupported recovery preference type")
                }
            }
            originalPreferences[name] = values
        }
        val originalFiles = linkedMapOf<String, ByteArray?>()
        val files = saved.getJSONArray("durableFiles")
        repeat(files.length()) { index ->
            val row = files.getJSONObject(index)
            originalFiles[row.getString("name")] = if (row.isNull("data")) null else Base64.decode(row.getString("data"), Base64.NO_WRAP)
        }
        restoreOwnedState(originalNames, originalPreferences, saved.getString("locale"), originalFiles)
        assertEquals("Interrupted QA restores each original preference without inventing values", originalPreferences,
            originalPreferences.keys.associateWith(::preference))
        val models = saved.getJSONArray("models")
        repeat(models.length()) { index ->
            val row = models.getJSONObject(index)
            val file = File(modelRoot(), row.getString("name"))
            assertEquals("Interrupted QA never modifies downloaded model bytes", row.getLong("bytes"), file.length())
            assertEquals("Interrupted QA never rewrites downloaded models", row.getLong("modified"), file.lastModified())
        }
        writeReport("interrupted-run-recovered.json", JSONObject().put("originalPreferencesRestored", true)
            .put("originalLocaleRestored", true).put("durableQueuesRestored", true).put("downloadedModelsUntouched", true))
        assertTrue(recoveryFile.delete())
    }
    private fun restoreOwnedState(originalNames: Set<String>, originalPreferences: Map<String, Map<String, *>>,
                                  originalLocale: String, originalFiles: Map<String, ByteArray?>) {
        onMain { UiLanguageSettings.select(app, originalLocale) }
        (preferenceNames() - originalNames).forEach { app.deleteSharedPreferences(it) }
        originalPreferences.filterKeys { it in originalNames }.forEach { (name, values) -> restorePreference(name, values) }
        originalFiles.forEach { (name, bytes) ->
            val file = File(app.filesDir, name)
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
                .remove(File(app.filesDir, NarrationAudioPlanner.FILE_NAME).canonicalFile)
        }
    }
    private fun preferenceNames() = File(app.applicationInfo.dataDir, "shared_prefs").listFiles().orEmpty()
        .filter { it.name.endsWith(".xml") }.map { it.name.removeSuffix(".xml") }.toSet()
    private fun preference(name: String): Map<String, *> = app.getSharedPreferences(name, Context.MODE_PRIVATE).all
        .mapValues { (_, value) -> if (value is Set<*>) value.toSet() else value }
    private fun restorePreference(name: String, values: Map<String, *>) {
        val editor = app.getSharedPreferences(name, Context.MODE_PRIVATE).edit().clear()
        values.forEach { (key, value) -> when (value) {
            is String -> editor.putString(key, value)
            is Int -> editor.putInt(key, value)
            is Long -> editor.putLong(key, value)
            is Float -> editor.putFloat(key, value)
            is Boolean -> editor.putBoolean(key, value)
            is Set<*> -> editor.putStringSet(key, value.filterIsInstance<String>().toSet())
            else -> error("Unsupported QA preference $name/$key")
        } }
        assertTrue(editor.commit())
    }
    private fun await(message: String, timeout: Long, condition: () -> Boolean) {
        val deadline = SystemClock.uptimeMillis() + timeout
        while (SystemClock.uptimeMillis() < deadline) {
            if (condition()) return
            SystemClock.sleep(50)
        }
        val stacks = Thread.getAllStackTraces().entries.sortedBy { it.key.name }.map { (thread, frames) ->
            JSONObject().put("name", thread.name).put("id", thread.id).put("alive", thread.isAlive)
                .put("state", thread.state.name).put("interrupted", thread.isInterrupted)
                .put("priority", thread.priority).put("frames", JSONArray(frames.map { it.toString() }))
        }
        val diagnostic = JSONObject().put("condition", message).put("timeoutMs", timeout)
            .put("uptimeMs", SystemClock.uptimeMillis()).put("threads", JSONArray(stacks))
            .put("narrationSettings", JSONObject(preference("lesson_narration_settings")))
        listOf(KokoroNarrationDiagnostics.STATUS_FILE, KokoroNarrationDiagnostics.EVENTS_FILE).forEach { name ->
            diagnostic.put(name, runCatching { File(app.filesDir, name).readLines().takeLast(80).joinToString("\n") }
                .getOrElse { it.toString() })
        }
        writeReport("timeout-${System.currentTimeMillis()}.json", diagnostic)
        fail(message)
    }
    private fun <A : Activity> awaitLocale(scenario: ActivityScenario<A>, tag: String, previous: Activity?) {
        await("LocaleManager recreates a focused screen using the selected language $tag", 25_000) {
            var ready = false
            scenario.onActivity { activity ->
                ready = activity !== previous && !activity.isChangingConfigurations && !activity.isDestroyed &&
                    activity.hasWindowFocus() && activity.resources.configuration.locales[0].language == tag.substringBefore('-')
            }
            ready
        }
    }
    private fun <T> onMain(action: () -> T): T {
        val result = AtomicReference<T>()
        instrumentation.runOnMainSync { result.set(action()) }
        return result.get()
    }
    @Suppress("UNCHECKED_CAST") private fun <T> field(instance: Any, name: String): T =
        instance.javaClass.getDeclaredField(name).apply { isAccessible = true }.get(instance) as T
    private companion object {
        const val QA_PACKAGE = "com.learngraphicsprogramming.app.realdownloadsqa"
        const val LESSON_ID = "cpp_fundamentals__programas_y_ejecucion__problemas_instrucciones_y_resultados"
        const val PROGRAMMING_ASSET = "content/es/programming/cpp_fundamentals/programas_y_ejecucion/lesson_problemas_instrucciones_y_resultados.json"
        val KNOWN_PREFERENCES = setOf("lesson_narration_settings", "lesson_narration_resume_bounded_v1", "learning_progress",
            "premium", "app_settings", "ui_language_settings")
    }
}
