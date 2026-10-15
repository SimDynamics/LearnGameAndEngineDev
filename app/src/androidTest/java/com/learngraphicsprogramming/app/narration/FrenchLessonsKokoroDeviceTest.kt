package com.learngraphicsprogramming.app.narration

import android.app.Activity
import android.app.Application
import android.content.Context
import android.content.Intent
import android.content.SharedPreferences
import android.graphics.Bitmap
import android.media.MediaPlayer
import android.os.Build
import android.os.SystemClock
import android.system.Os
import android.view.View
import android.view.WindowManager
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.AppearanceSettingsRepository
import com.learngraphicsprogramming.app.BuildConfig
import com.learngraphicsprogramming.app.CoursesActivity
import com.learngraphicsprogramming.app.LessonActivity
import com.learngraphicsprogramming.app.PremiumRepository
import com.learngraphicsprogramming.app.ProgressRepository
import com.learngraphicsprogramming.app.R
import com.learngraphicsprogramming.app.UiLanguageSettings
import com.learngraphicsprogramming.app.narration.phonemes.FrenchFluidG2P
import com.learngraphicsprogramming.app.narration.phonemes.FrenchRuleBasedG2P
import com.learngraphicsprogramming.app.narration.phonemes.FrenchWordG2P
import java.io.File
import java.security.MessageDigest
import java.util.UUID
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.atomic.AtomicReference
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.FixMethodOrder
import org.junit.Test
import org.junit.runner.RunWith
import org.junit.runners.MethodSorters

/** Fresh production synthesis, using only the resources genuinely installed in the isolated APK. */
@RunWith(AndroidJUnit4::class)
@FixMethodOrder(MethodSorters.NAME_ASCENDING)
class FrenchLessonsKokoroDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val app = instrumentation.targetContext
    private val evidence: File get() = File(requireNotNull(app.getExternalFilesDir(null)), "french-lessons-kokoro")
        .apply { check(isDirectory || mkdir()) }

    @Test fun a_verifyInstalledFrenchResources() {
        requireQa()
        val root = modelRoot()
        val rows = JSONArray()
        frenchArtifacts().forEach { artifact ->
            val file = File(root, artifact.fileName)
            val actual = if (file.isFile) sha256(file) else "absent"
            rows.put(JSONObject().put("file", artifact.fileName).put("bytes", file.length())
                .put("expectedBytes", artifact.bytes).put("actualSha256", actual).put("expectedSha256", artifact.sha256)
                .put("verified", actual == artifact.sha256 && file.length() == artifact.bytes))
        }
        File(evidence, "installed-french-resources.json").writeText(JSONObject().put("package", app.packageName)
            .put("device", Build.MODEL).put("resources", rows).put("downloadedDuringTest", false).toString(2))
        frenchArtifacts().forEach { artifact ->
            assertTrue("The actual installed bytes must match the pinned SHA-256: ${artifact.fileName}",
                KokoroModelManifest.verify(File(root, artifact.fileName), artifact))
        }
    }

    @Test fun b_actualLessonUsesProductionKokoroWithoutAndroidTts() {
        // This original page includes the absent lexicon words "suffit" and "reçoivent".
        // No recording is supplied or prepared by the test: Play starts the real worker.
        exerciseOriginalPage(PROGRAMMING_LESSON, 0, "programming-page-1")
    }

    @Test fun c_everyFrenchLessonPhonemizesItsOriginalNarrationWithoutMissingWordsOrSymbols() {
        requireQa()
        val root = modelRoot()
        val unknownWords = linkedMapOf<String, Int>()
        val frontend = FrenchFluidG2P(root, File(root, "tokenizer.json"), fallback = FrenchWordG2P { word ->
            unknownWords[word] = (unknownWords[word] ?: 0) + 1
            FrenchRuleBasedG2P.phonemizeWord(word)
        })
        val vocabulary = KokoroOnnxInputs.loadVocabulary(File(root, "tokenizer.json"))
        val failures = JSONArray()
        var lessons = 0
        var pages = 0
        var sentences = 0
        fun paths(directory: String): List<String> = app.assets.list(directory).orEmpty().flatMap { name ->
            val path = "$directory/$name"
            if (name.endsWith(".json")) listOf(path) else paths(path)
        }
        val lessonPaths = paths("content/fr").filter { it.substringAfterLast('/').startsWith("lesson_") }
        assertTrue("Exercise every existing French lesson", lessonPaths.isNotEmpty())
        try {
            lessonPaths.forEach { path ->
                val lesson = JSONObject(app.assets.open(path).bufferedReader(Charsets.UTF_8).use { it.readText() })
                val before = lesson.toString()
                repeat(lesson.getJSONArray("pages").length()) { page ->
                    NarrationSegments.sentences(LessonNarrationText.page(lesson, page, NarrationLanguage.FRENCH,
                        forKokoro = true)).forEachIndexed { sentence, text ->
                        try {
                            val phones = MixedFrenchG2P.phonemizePrepared(text, frontend)
                            assertTrue("Narratable text cannot silently disappear", phones.isNotBlank())
                            KokoroPhonemeChunks.split(phones).forEach { KokoroOnnxInputs.tokenize(it, vocabulary) }
                        } catch (failure: Throwable) {
                            failures.put(JSONObject().put("asset", path).put("page", page).put("sentence", sentence)
                                .put("text", ManualEnglishSpeech.strip(text)).put("failure", failure.toString()))
                        }
                        sentences++
                    }
                    pages++
                }
                assertEquals("The authored lesson remains intact: $path", before, lesson.toString())
                lessons++
            }
        } finally {
            (frontend as? AutoCloseable)?.close()
            File(evidence, "all-french-lessons-phonemes.json").writeText(JSONObject().put("lessons", lessons)
                .put("pages", pages).put("sentences", sentences).put("failures", failures)
                .put("uniqueWordsAbsentFromLexicon", unknownWords.size).put("fallbackCalls", unknownWords.values.sum())
                .put("unknownWords", JSONObject(unknownWords as Map<*, *>))
                .put("audioCacheUsed", false).put("curriculumModified", false).toString(2))
        }
        assertEquals("Every French curriculum sentence must produce valid Kokoro phonemes; see corpus evidence", 0, failures.length())
    }

    @Test fun d_completeFirstProgramLessonSpeaksTechnicalTermsAndPuzzles() {
        val lesson = JSONObject(app.assets.open(FIRST_PROGRAM).bufferedReader(Charsets.UTF_8).use { it.readText() })
        repeat(lesson.getJSONArray("pages").length()) { page ->
            exerciseOriginalPage(FIRST_PROGRAM, page, "first-program-page-${page + 1}")
        }
    }

    @Test fun e_completeMathematicsLessonSpeaksFormulasAndExercises() {
        val lesson = JSONObject(app.assets.open(MATHEMATICS).bufferedReader(Charsets.UTF_8).use { it.readText() })
        repeat(lesson.getJSONArray("pages").length()) { page ->
            exerciseOriginalPage(MATHEMATICS, page, "mathematics-page-${page + 1}")
        }
    }

    @Test fun f_frenchPauseResumeSeekingRestartAndThreeBoundedPositionsPreserveAudio() {
        exerciseOriginalPage(FIRST_PROGRAM, 5, "french-reader-controls") { reader, isolated, page, identity, report ->
            val cache = File(isolated.filesDir, "kokoro-narration-cache")
            fun cacheFiles() = cache.listFiles().orEmpty().filter { it.extension in setOf("wav", "ogg") }
            fun fileIdentity(file: File) = Os.stat(file.absolutePath).let { listOf(it.st_dev, it.st_ino, it.st_size) }
            val recordings = cacheFiles().associate { it.name to (sha256(it) to fileIdentity(it)) }
            fun verifyOriginalAudio() = recordings.forEach { (name, before) ->
                val file = File(cache, name)
                assertEquals("Existing French audio is never regenerated by playback controls", before,
                    sha256(file) to fileIdentity(file))
            }
            val key = onMain { requireNotNull(field<String?>(reader, "resumeKey")) }
            val count = NarrationSegments.sentences(page).size
            val store = NarrationResumeStore(isolated)
            assertTrue(requireNotNull(store.restore(key, count)).finished)
            onMain {
                reader.toggle(page, NarrationLanguage.FRENCH, identity)
                assertTrue("Play does not implicitly restart a finished page", field(reader, "finished"))
                assertFalse(reader.isPlaying)
                assertNull(field<MediaPlayer?>(reader, "player"))
                reader.seekBy(-3)
                assertFalse(field(reader, "finished"))
                assertNull("Seeking a finished page must not retain a paused decoder", field<MediaPlayer?>(reader, "player"))
                reader.toggle(page, NarrationLanguage.FRENCH, identity)
            }
            await("The completed French page resumes after rewind", 45_000) {
                onMain { reader.isPlaying && field<Boolean>(reader, "playerReady") }
            }
            var positionBeforePause = 0L
            val paused = onMain {
                positionBeforePause = requireNotNull(field<MediaPlayer?>(reader, "player")).currentPosition.toLong()
                reader.pauseKeepingPosition()
                field<Long>(reader, "offset").also { saved ->
                    assertTrue("Pause saves the actual decoder position", saved in positionBeforePause..positionBeforePause + 120)
                    assertNull("French pause releases MediaPlayer", field<MediaPlayer?>(reader, "player"))
                    assertFalse(field(reader, "playerReady"))
                    assertEquals(saved, requireNotNull(store.restore(key, count)).positionMs)
                }
            }
            SystemClock.sleep(150)
            onMain {
                assertEquals("A released player cannot advance while paused", paused, field<Long>(reader, "offset"))
                reader.toggle(page, NarrationLanguage.FRENCH, identity)
                assertEquals("Resume chooses the exact existing offset", paused, field<Long>(reader, "offset"))
            }
            await("The released French decoder resumes its saved audio position", 45_000) {
                onMain { reader.isPlaying && field<Boolean>(reader, "playerReady") &&
                    requireNotNull(field<MediaPlayer?>(reader, "player")).currentPosition >= paused }
            }
            onMain { reader.restart() }
            await("Explicit restart returns to the beginning of the French page", 45_000) {
                onMain { reader.isPlaying && field<Int>(reader, "index") == 0 && field<Boolean>(reader, "playerReady") &&
                    requireNotNull(field<MediaPlayer?>(reader, "player")).currentPosition < 1_500 }
            }
            onMain { reader.pauseKeepingPosition() }
            verifyOriginalAudio()
            val texts = listOf("Cette implémentation reçoit une valeur.", "Une solution correcte suffit.",
                "La borne supérieure vaut quatre.", "Une expression concrète est utile.").map(::listOf)
            val pageIds = (0..3).map { "qa-french-history-${UUID.randomUUID()}/$it" }
            val keys = pageIds.mapIndexed { index, pageId -> NarrationResumePolicy.key(pageId, "fr",
                NarrationEngine.KOKORO.name, VOICE, NarrationSegments.sentences(texts[index])) }
            fun savedKeys(): List<String> = isolated.getSharedPreferences("lesson_narration_resume_bounded_v1", Context.MODE_PRIVATE)
                .all.let { values ->
                    assertTrue("No fourth bookmark slot may be written", values.keys.filter { it.startsWith("key_") }
                        .all { it.removePrefix("key_").toInt() < 3 })
                    (0 until NarrationResumePolicy.MAX_BOOKMARKS).mapNotNull { values["key_$it"] as? String }
                }
            fun start(index: Int) = onMain { reader.toggle(texts[index], NarrationLanguage.FRENCH, pageIds[index]) }
            fun ready() = onMain { reader.isPlaying && field<Boolean>(reader, "playerReady") &&
                requireNotNull(field<MediaPlayer?>(reader, "player")).currentPosition >= 150 }
            fun finish(index: Int) {
                start(index)
                await("French page $index starts real synthesis and playback", 120_000) { ready() }
                await("French page $index finishes naturally", 60_000) { onMain { field<Boolean>(reader, "finished") } }
                assertTrue(requireNotNull(store.restore(keys[index], 1)).finished)
                onMain {
                    assertFalse(reader.isPlaying)
                    assertNull(field<MediaPlayer?>(reader, "player"))
                    reader.toggle(texts[index], NarrationLanguage.FRENCH, pageIds[index])
                    assertFalse("Finished French page cannot restart implicitly", reader.isPlaying)
                    assertTrue(field(reader, "finished"))
                }
            }
            finish(0)
            start(1)
            await("French page B starts", 120_000) { ready() }
            onMain { reader.pauseKeepingPosition() }
            repeat(3) {
                val before = requireNotNull(store.restore(keys[1], 1))
                start(1)
                onMain { assertEquals(before.positionMs, field<Long>(reader, "offset")) }
                await("Repeated pauses use the same French page bookmark", 45_000) { ready() }
                onMain { reader.pauseKeepingPosition(); assertNull(field<MediaPlayer?>(reader, "player")) }
                assertEquals(1, savedKeys().count { it == keys[1] })
            }
            finish(2)
            assertEquals("Finished pages use the same three ordinary bookmark slots", listOf(keys[2], keys[1], keys[0]), savedKeys())
            start(3)
            await("French page D starts", 120_000) { ready() }
            onMain { reader.pauseKeepingPosition() }
            assertEquals("The existing policy evicts the oldest finished French page", listOf(keys[3], keys[2], keys[1]), savedKeys())
            assertNull(store.restore(keys[0], 1))
            start(0)
            onMain { assertEquals("An evicted French bookmark starts from zero", 0L, field<Long>(reader, "offset")) }
            await("An evicted French page remains playable from its existing audio", 45_000) { ready() }
            onMain { reader.pauseKeepingPosition(); assertNull(field<MediaPlayer?>(reader, "player")); assertNull(field<Any?>(reader, "tts")) }
            verifyOriginalAudio()
            report.put("controls", JSONObject().put("pauseReleasesPlayer", true).put("exactSavedPositionMs", paused)
                .put("positionBeforePauseMs", positionBeforePause).put("rewindResumeRestart", true)
                .put("threeBookmarksIncludeFinishedPages", true).put("fourthPageEvictsOldestFinished", true)
                .put("existingAudioBytesAndInodesPreserved", true).put("finalBookmarkCount", savedKeys().size))
        }
    }

    @Test fun g_spanishAmericanAndBritishEnglishKeepRealProductionNarration() {
        listOf(
            Triple(NarrationLanguage.SPANISH, "ef_dora", "es"),
            Triple(NarrationLanguage.AMERICAN_ENGLISH, "af_heart", "en"),
            Triple(NarrationLanguage.BRITISH_ENGLISH, "bf_emma", "en")
        ).forEach { (language, voice, directory) ->
            exerciseOriginalPage(FIRST_PROGRAM.replace("content/fr/", "content/$directory/"), 0,
                "regression-${language.tag}", language, voice)
        }
    }

    @Test fun h_originalFrenchLessonUiPlaysKokoroAndItsUnknownWordWithoutAndroidTts() {
        requireQa()
        val before = preferenceNames().associateWith { preferenceSnapshot(it) }
        val localeBefore = onMain { UiLanguageSettings.selectedTag(app) }
        val root = modelRoot()
        val modelsBefore = root.listFiles().orEmpty().associate { it.name to (it.length() to it.lastModified()) }
        val cache = File(app.filesDir, "kokoro-narration-cache")
        val previousAudio = cache.listFiles().orEmpty().filter { it.isFile }.associate { it.name to sha256(it) }
        val lessonId = "cpp_fundamentals__programas_y_ejecucion__problemas_instrucciones_y_resultados"
        val report = JSONObject().put("package", app.packageName).put("asset", PROGRAMMING_LESSON)
            .put("fixtureInjected", false).put("voice", VOICE)
        var scenario: ActivityScenario<LessonActivity>? = null
        var controller: LessonNarrationController? = null
        try {
            onMain {
                UiLanguageSettings.select(app, "fr")
                NarrationSettingsRepository.saveEnabled(app, true)
                NarrationSettingsRepository.saveEngine(app, NarrationEngine.KOKORO)
                NarrationSettingsRepository.saveLanguage(app, NarrationLanguage.FRENCH)
                NarrationSettingsRepository.saveKokoroVoice(app, VOICE)
                NarrationSettingsRepository.saveControlsVisible(app, true)
                NarrationSettingsRepository.saveRate(app, 2f)
                PremiumRepository.setVerifiedPremium(app)
                ProgressRepository.saveCurrentPage(app, AppearanceSettingsRepository.ROUTE_PROGRAMMING, lessonId, 0)
            }
            val host = ActivityScenario.launch<LessonActivity>(Intent(app, LessonActivity::class.java)
                .putExtra("routeId", AppearanceSettingsRepository.ROUTE_PROGRAMMING)
                .putExtra("courseId", "cpp_fundamentals").putExtra("unitId", "programas_y_ejecucion")
                .putExtra("lessonId", lessonId).putExtra("lessonNumber", 1).putExtra("lessonTotal", 6)).also { scenario = it }
            await("The original French lesson must be loaded, visible and focused", 25_000) {
                var ready = false
                host.onActivity { activity ->
                    activity.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                    ready = activity.hasWindowFocus() && activity.findViewById<View>(R.id.lessonContent).width > 0 &&
                        field<JSONObject>(activity, "lessonJson").optString("title") == "Problèmes, instructions et résultats"
                }
                ready
            }
            val original = JSONObject(app.assets.open(PROGRAMMING_LESSON).bufferedReader(Charsets.UTF_8).use { it.readText() })
            val expected = LessonNarrationScript.page(original, 0, NarrationLanguage.FRENCH, forKokoro = true)
            val segments = NarrationSegments.sentences(expected.texts)
            val unknownSegment = segments.indexOfFirst { "ne suffit pas" in ManualEnglishSpeech.strip(it) }
            assertTrue("The real UI test must reach an original unknown word", unknownSegment >= 0)
            host.onActivity { activity ->
                val reader = requireNotNull(field<LessonNarrationController?>(activity, "narrationController")).also { controller = it }
                val settings = NarrationSettingsRepository.get(activity)
                assertEquals(NarrationLanguage.FRENCH, settings.language)
                assertEquals(NarrationEngine.KOKORO, settings.engine)
                assertEquals(VOICE, settings.kokoroVoice)
                assertEquals("The original French JSON is rendered without injected content", original.toString(),
                    field<JSONObject>(activity, "lessonJson").toString())
                assertTrue("Play uses the existing visible reader control", activity.findViewById<View>(R.id.buttonLessonListen).performClick())
                if (field<Boolean>(reader, "finished") || field<Int>(reader, "index") > unknownSegment) {
                    // Respect an already-finished bookmark. Only the explicit, existing Restart control
                    // may begin that page again; the test never clears any retained position.
                    assertTrue(activity.findViewById<View>(R.id.buttonLessonRestartPage).performClick())
                    report.put("existingBookmarkExplicitlyRestartedForObservation", true)
                }
            }
            await("The UI must play the real original French OOV sentence with Kokoro", 6 * 60_000L) {
                var ready = false
                host.onActivity { activity ->
                    val reader = requireNotNull(controller)
                    assertFalse("No neural-unavailable message may precede a hidden fallback", field(reader, "reportedFallback"))
                    assertNull("The actual lesson never initializes Android TTS", field<Any?>(reader, "tts"))
                    ready = reader.isPlaying && field<Boolean>(reader, "playerReady") &&
                        field<Int>(reader, "index") == unknownSegment &&
                        requireNotNull(field<MediaPlayer?>(reader, "player")).currentPosition >= 150 && activity.hasWindowFocus()
                }
                ready
            }
            val bitmap = requireNotNull(instrumentation.uiAutomation.takeScreenshot())
            try { File(evidence, "actual-french-lesson-playing.png").outputStream().use {
                assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it))
            } } finally { bitmap.recycle() }
            host.onActivity {
                val reader = requireNotNull(controller)
                report.put("actualSegmentIndex", field<Int>(reader, "index"))
                    .put("spokenSource", ManualEnglishSpeech.strip(segments[unknownSegment]))
                    .put("actualPlayerPositionMs", requireNotNull(field<MediaPlayer?>(reader, "player")).currentPosition)
                    .put("androidTtsUsed", false).put("reportedFallback", false).put("actualUiPlayClicked", true)
            }
            await("The actual UI must finish pronouncing the entire original unknown-word sentence", 60_000) {
                var completed = false
                host.onActivity {
                    val reader = requireNotNull(controller)
                    assertFalse(field(reader, "reportedFallback"))
                    assertNull(field<Any?>(reader, "tts"))
                    completed = field<Int>(reader, "index") > unknownSegment
                }
                completed
            }
            host.onActivity {
                val reader = requireNotNull(controller)
                reader.pauseKeepingPosition()
                assertNull("The actual UI pause releases its player", field<MediaPlayer?>(reader, "player"))
            }
            report.put("status", "passed").put("originalUnknownWordSentencePlayedCompletely", true)
        } catch (failure: Throwable) {
            report.put("status", "failed").put("failure", failure.toString())
            throw failure
        } finally {
            try {
                scenario?.close()
                instrumentation.waitForIdleSync()
                controller?.let { reader -> await("The actual lesson closes its worker", 30_000) {
                    !field<Thread>(reader, "workerThread").isAlive
                } }
                onMain { UiLanguageSettings.select(app, localeBefore) }
                restorePreferences(before)
                assertEquals("The original Android app locale is restored", localeBefore, onMain { UiLanguageSettings.selectedTag(app) })
                assertEquals("Every preference, progress entry and retained position is restored", before,
                    preferenceNames().associateWith { preferenceSnapshot(it) })
                assertEquals(modelsBefore, root.listFiles().orEmpty().associate { it.name to (it.length() to it.lastModified()) })
                previousAudio.forEach { (name, hash) -> assertEquals("Compatible recordings remain intact", hash, sha256(File(cache, name))) }
                report.put("existingPreferencesProgressPositionsAndAudioPreserved", true)
            } finally {
                File(evidence, "actual-french-lesson-ui.json").writeText(report.toString(2))
            }
        }
    }

    private fun exerciseOriginalPage(asset: String, pageIndex: Int, name: String,
        language: NarrationLanguage = NarrationLanguage.FRENCH, voice: String = VOICE,
        afterPlayback: ((LessonNarrationController, Application, List<String>, String, JSONObject) -> Unit)? = null) {
        requireQa()
        val root = modelRoot()
        val auxiliary = if (language in setOf(NarrationLanguage.AMERICAN_ENGLISH, NarrationLanguage.BRITISH_ENGLISH))
            EnglishAuxResources.artifacts else emptyList()
        val required = KokoroModelManifest.sharedArtifacts + KokoroModelManifest.resources(language) +
            requireNotNull(KokoroVoiceCatalog.find(voice)).artifact + auxiliary
        required.forEach { assertTrue(it.fileName, KokoroModelManifest.verify(File(root, it.fileName), it)) }
        val installedBefore = root.listFiles().orEmpty().associate { it.name to (it.length() to it.lastModified()) }
        val originalPreferences = preferenceNames().associateWith { preferenceSnapshot(it) }
        val actual = app.applicationContext as Application
        val sandbox = File(app.cacheDir, "french-lessons-${UUID.randomUUID()}").canonicalFile
        assertEquals(app.cacheDir.canonicalFile, sandbox.parentFile)
        assertTrue(sandbox.mkdir())
        val isolated = IsolatedApplication(actual, sandbox)
        val output = File(evidence, "$name-${UUID.randomUUID()}").apply { check(mkdir()) }
        val errors = CopyOnWriteArrayList<String>()
        val events = CopyOnWriteArrayList<JSONObject>()
        val report = JSONObject().put("package", app.packageName).put("device", Build.MODEL)
            .put("asset", asset).put("pageIndex", pageIndex).put("voice", voice).put("language", language.tag)
            .put("freshAudioSandbox", sandbox.absolutePath).put("preGeneratedAudio", false)
        fun persist() = File(output, "production.json").writeText(report.put("errors", JSONArray(errors.toList()))
            .put("playbackEvents", JSONArray(events.toList())).toString(2))
        var controller: LessonNarrationController? = null
        var scenario: ActivityScenario<CoursesActivity>? = null
        try {
            assertTrue(app.getSharedPreferences("lesson_narration_settings", Context.MODE_PRIVATE).edit()
                .putBoolean("enabled", false).putString("engine", NarrationEngine.ANDROID.name).commit())
            NarrationSettingsRepository.saveEnabled(isolated, true)
            NarrationSettingsRepository.saveEngine(isolated, NarrationEngine.KOKORO)
            NarrationSettingsRepository.saveLanguage(isolated, language)
            NarrationSettingsRepository.saveKokoroVoice(isolated, voice)
            NarrationSettingsRepository.saveRate(isolated, 2f)
            NarrationStorageBudget.saveLimit(isolated, 128L * NarrationStorageBudget.MIB)
            var activity: Activity? = null
            scenario = ActivityScenario.launch<CoursesActivity>(Intent(app, CoursesActivity::class.java)
                .putExtra("routeId", AppearanceSettingsRepository.ROUTE_PROGRAMMING))
            requireNotNull(scenario).onActivity { activity = it; it.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON) }
            await("The QA host must own audio focus", 15_000) { activity?.hasWindowFocus() == true }
            val authored = app.assets.open(asset).bufferedReader(Charsets.UTF_8).use { it.readText() }
            val lesson = JSONObject(authored)
            val jsonBefore = lesson.toString()
            val page = LessonNarrationText.page(lesson, pageIndex, language, forKokoro = true)
            assertTrue("A real curriculum page is required", page.isNotEmpty())
            report.put("segments", JSONArray(NarrationSegments.sentences(page).map(ManualEnglishSpeech::strip)))
            persist()
            val reader = onMain {
                LessonNarrationController(isolated, {}, { errors.add(it) }, { event ->
                    events.add(JSONObject().put("phase", event.phase.name).put("segment", event.segmentIndex)
                        .put("positionMs", event.positionMs))
                }).also { controller = it }
            }
            assertTrue("Fresh sandbox must contain no prepared narration",
                File(isolated.filesDir, "kokoro-narration-cache").listFiles().orEmpty().isEmpty())
            val pageIdentity = "qa-french-$name-${UUID.randomUUID()}"
            onMain { reader.toggle(page, language, pageIdentity) }
            awaitReader("The original French page must finish through the production worker", reader, errors, 12 * 60_000L) {
                field<Boolean>(reader, "finished") && !reader.isPlaying
            }
            onMain {
                assertNull("No Android TTS fallback may mask a French synthesis failure", field<Any?>(reader, "tts"))
                assertNull("Completion releases the actual player", field<MediaPlayer?>(reader, "player"))
            }
            val playedSegments = onMain { field<List<Any>>(reader, "segments").map { segment ->
                field<String>(segment, "text") to requireNotNull(field<File?>(segment, "file"))
            } }
            assertEquals("The controller plays the exact original engine-specific sentence identities",
                NarrationSegments.sentences(page), playedSegments.map { it.first })
            assertEquals("Every original sentence must finish through real playback",
                playedSegments.indices.toSet(), events.filter { it.optString("phase") == "SEGMENT_COMPLETED" }
                    .map { it.getInt("segment") }.toSet())
            afterPlayback?.invoke(reader, isolated, page, pageIdentity, report)
            assertTrue("No synthesis or playback error may be hidden by subsequent controls: $errors", errors.isEmpty())
            assertTrue("Actual playback, not only pre-generation, must occur", events.any { it.getString("phase") == "PLAYING" })
            assertEquals("The educational JSON remains intact", jsonBefore, lesson.toString())
            val recordings = File(isolated.filesDir, "kokoro-narration-cache").listFiles().orEmpty()
                .filter { it.extension == "wav" }
            assertTrue("The real worker creates original French recordings", recordings.isNotEmpty())
            val diagnosticFile = File(isolated.filesDir, KokoroNarrationDiagnostics.EVENTS_FILE)
            val diagnostics = diagnosticFile.takeIf { it.isFile }?.readLines().orEmpty().map(::JSONObject)
            val generatedKeys = diagnostics.filter { it.optString("event") == "Page sentence generated" }
                .map { it.getString("hash") }.toSet()
            val originalKeys = playedSegments.map { KokoroNarrationCache.key(language.tag, voice, it.first) }.toSet()
            val packagedKeys = diagnostics.filter { it.optString("event") == "packaged-audio-loaded" }
                .associate { it.getString("cacheKey") to it.getString("format") }
            val origins = JSONArray()
            val verifiedPackagedKeys = linkedSetOf<String>()
            playedSegments.forEach { (text, playedFile) ->
                val key = KokoroNarrationCache.key(language.tag, voice, text)
                val origin = JSONObject().put("cacheKey", key).put("actualPlayedFile", playedFile.name)
                    .put("actualPlayedSha256", sha256(playedFile))
                if (key in generatedKeys) {
                    assertEquals("Native recordings use their actual sentence identity", "$key.wav", playedFile.name)
                    origin.put("source", "fresh-native-synthesis")
                } else {
                    // The Spanish release already bundles verified Opus for reusable sentences.
                    // Preserve that optimization and prove its exact provenance instead of
                    // requiring unnecessary regeneration or accepting an arbitrary cache file.
                    val format = requireNotNull(packagedKeys[key]) { "No native or packaged provenance for $key" }
                    assertTrue("Only formats validated by the production cache are accepted", format in setOf("wav", "ogg"))
                    val assetPath = "narration/prerendered/$key.$format"
                    assertEquals("The decoder used the exact packaged sentence", "$key.$format", playedFile.name)
                    val assetDigest = MessageDigest.getInstance("SHA-256")
                    isolated.assets.open(assetPath).use { input ->
                        val buffer = ByteArray(64 * 1024)
                        while (true) { val count = input.read(buffer); if (count < 0) break; assetDigest.update(buffer, 0, count) }
                    }
                    val assetHash = assetDigest.digest().joinToString("") { "%02x".format(it.toInt() and 255) }
                    assertEquals("Packaged playback retains the APK's exact original audio bytes", assetHash, sha256(playedFile))
                    assertEquals("The existing production cache validates this same file", playedFile.canonicalFile,
                        requireNotNull(KokoroNarrationCache.lookup(isolated, language.tag, voice, text)).canonicalFile)
                    verifiedPackagedKeys += key
                    origin.put("source", "verified-apk-asset").put("asset", assetPath).put("assetSha256", assetHash)
                }
                origins.put(origin)
            }
            assertTrue("Every original recording requires proven native or packaged provenance",
                (generatedKeys + verifiedPackagedKeys).containsAll(originalKeys))
            if (language == NarrationLanguage.FRENCH) {
                assertTrue("French acceptance requires fresh synthesis for every original sentence",
                    generatedKeys.containsAll(originalKeys))
                assertTrue("French acceptance cannot be masked by pre-generated packaged samples", verifiedPackagedKeys.isEmpty())
            }
            recordings.forEachIndexed { index, file -> file.copyTo(File(output, "sentence-${index + 1}.wav")) }
            report.put("status", "passed").put("actualPlaybackCompleted", true).put("androidTtsUsed", false)
                .put("nativeGenerationKeys", JSONArray(generatedKeys.toList())).put("originalUniqueSentences", originalKeys.size)
                .put("testProvidedAudio", false).put("preGeneratedAudio", verifiedPackagedKeys.isNotEmpty())
                .put("verifiedPackagedKeys", JSONArray(verifiedPackagedKeys.toList())).put("actualPlaybackOrigins", origins)
                .put("recordings", JSONArray(recordings.map { JSONObject().put("name", it.name).put("sha256", sha256(it)) }))
            diagnosticFile.takeIf { it.isFile }?.copyTo(File(output, "kokoro-diagnostics.jsonl"))
            persist()
        } catch (failure: Throwable) {
            report.put("status", "failed").put("failure", failure.toString())
            File(isolated.filesDir, KokoroNarrationDiagnostics.EVENTS_FILE).takeIf { it.isFile }
                ?.copyTo(File(output, "kokoro-diagnostics.jsonl"), overwrite = true)
            persist()
            throw failure
        } finally {
            try {
                controller?.let { reader ->
                    onMain { reader.close() }
                    await("The test-owned synthesis worker closes", 30_000) { !field<Thread>(reader, "workerThread").isAlive }
                }
                scenario?.close()
                instrumentation.waitForIdleSync()
                KokoroPagePreparationQueue.snapshot(app)
            } finally {
                restorePreferences(originalPreferences)
                assertEquals("All installed models and voices remain untouched", installedBefore,
                    root.listFiles().orEmpty().associate { it.name to (it.length() to it.lastModified()) })
                assertEquals("All existing preferences, progress and positions remain unchanged", originalPreferences,
                    preferenceNames().associateWith { preferenceSnapshot(it) })
                report.put("installedModelsUntouched", true).put("preferencesProgressAndPositionsRestored", true)
                persist()
            }
        }
    }

    private fun requireQa() {
        assertEquals("Never run against the regular app or Work", QA_PACKAGE, app.packageName)
        assertEquals(QA_PACKAGE, BuildConfig.APPLICATION_ID)
        assertTrue("Target LDPlayer explicitly", Build.SUPPORTED_ABIS.any { it == "x86" || it == "x86_64" })
    }
    private fun modelRoot() = File(requireNotNull(app.getExternalFilesDir(null)), "kokoro/${KokoroModelManifest.revision}")
    private fun frenchArtifacts() = KokoroModelManifest.sharedArtifacts + KokoroModelManifest.resources(NarrationLanguage.FRENCH) +
        requireNotNull(KokoroVoiceCatalog.find(VOICE)).artifact
    private fun preferenceNames() = File(app.applicationInfo.dataDir, "shared_prefs").listFiles().orEmpty()
        .filter { it.name.endsWith(".xml") }.map { it.name.removeSuffix(".xml") }.toSet()
    private fun preferenceSnapshot(name: String): Map<String, *> = app.getSharedPreferences(name, Context.MODE_PRIVATE).all
        .mapValues { (_, value) -> if (value is Set<*>) value.toSet() else value }
    private fun restorePreferences(before: Map<String, Map<String, *>>) {
        (preferenceNames() - before.keys).forEach { assertTrue(app.deleteSharedPreferences(it)) }
        before.forEach { (name, values) ->
            val edit = app.getSharedPreferences(name, Context.MODE_PRIVATE).edit().clear()
            values.forEach { (key, value) -> when (value) {
                is String -> edit.putString(key, value)
                is Int -> edit.putInt(key, value)
                is Long -> edit.putLong(key, value)
                is Float -> edit.putFloat(key, value)
                is Boolean -> edit.putBoolean(key, value)
                is Set<*> -> edit.putStringSet(key, value.filterIsInstance<String>().toSet())
                else -> error("Unsupported QA preference $name/$key")
            } }
            assertTrue(edit.commit())
        }
    }
    private class IsolatedApplication(private val actual: Application, root: File) : Application() {
        private val files = File(root, "files").apply { check(mkdir()) }
        private val cache = File(root, "cache").apply { check(mkdir()) }
        private val prefix = "french_lessons_${UUID.randomUUID()}_"
        init { attachBaseContext(actual) }
        override fun getApplicationContext(): Context = this
        override fun getFilesDir(): File = files
        override fun getCacheDir(): File = cache
        override fun getExternalFilesDir(type: String?): File? = actual.getExternalFilesDir(type)
        override fun getSharedPreferences(name: String, mode: Int): SharedPreferences = actual.getSharedPreferences(prefix + name, mode)
    }
    private fun awaitReader(message: String, reader: LessonNarrationController, errors: List<String>, timeout: Long,
                            condition: () -> Boolean) = await(message, timeout) {
        assertTrue("The actual production worker must not report a native failure: $errors", errors.isEmpty())
        onMain { assertNull("Android TTS must never hide a neural failure", field<Any?>(reader, "tts")); condition() }
    }
    private fun await(message: String, timeout: Long, condition: () -> Boolean) {
        val deadline = SystemClock.uptimeMillis() + timeout
        while (SystemClock.uptimeMillis() < deadline) {
            if (condition()) return
            SystemClock.sleep(40)
        }
        fail(message)
    }
    private fun <T> onMain(action: () -> T): T {
        val result = AtomicReference<T>()
        instrumentation.runOnMainSync { result.set(action()) }
        return result.get()
    }
    @Suppress("UNCHECKED_CAST") private fun <T> field(instance: Any, name: String): T =
        instance.javaClass.getDeclaredField(name).apply { isAccessible = true }.get(instance) as T
    private fun sha256(file: File): String {
        val digest = MessageDigest.getInstance("SHA-256")
        file.inputStream().buffered().use { stream ->
            val bytes = ByteArray(64 * 1024)
            while (true) { val count = stream.read(bytes); if (count < 0) break; digest.update(bytes, 0, count) }
        }
        return digest.digest().joinToString("") { "%02x".format(it.toInt() and 255) }
    }
    private companion object {
        const val QA_PACKAGE = "com.learngraphicsprogramming.app.realdownloadsqa"
        const val VOICE = "ff_siwis"
        const val PROGRAMMING_LESSON = "content/fr/programming/cpp_fundamentals/programas_y_ejecucion/lesson_problemas_instrucciones_y_resultados.json"
        const val FIRST_PROGRAM = "content/fr/programming/cpp_fundamentals/programas_y_ejecucion/lesson_primer_programa_y_salida.json"
        const val MATHEMATICS = "content/fr/math_physics/math_course_01/estructura_y_divisibilidad/lesson_expresiones_y_prioridad.json"
    }
}
