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
import android.widget.TextView
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

/** Published lesson/native views and real Kokoro. No replacement curriculum or fabricated timing. */
@RunWith(AndroidJUnit4::class)
class PuzzleVisualNarrationDeviceTest {
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

    @Test fun publishedOrderingNarratesItsVisibleInstructionAndCurrentTopToBottomCards() {
        val source = LessonRepository.loadLessonJson(context, ROUTE, LESSON)
        val sourceIdentity = source.toString()
        val host = openMathPage()
        host.onActivity { activity ->
            val card = orderingCard(activity)
            reset(card, activity)
        }
        instrumentation.waitForIdleSync()
        host.onActivity { activity ->
            val card = orderingCard(activity)
            val actualCards = numberCards(card)
            assertEquals(listOf("840", "804", "480", "408"), actualCards.map { it.text.toString() })
            card.requestRectangleOnScreen(Rect(0, 0, card.width, card.height), true)
            capture(card, "published-ordering-before-reading")
            val script = currentScript(activity)
            val spoken = blockSentences(script)
            writeEvidence("published-ordering-before-reading", JSONObject()
                .put("actualVisualOrder", JSONArray(actualCards.map { it.text.toString() }))
                .put("actualNarration", JSONArray(spoken)))
            assertTrue("The same instruction visible below the heading must be narrated", spoken.any {
                it.contains("Arrastra una tarjeta directamente") && it.contains("orden deseado")
            })
            assertNumberOrder(spoken, listOf("840", "804", "480", "408"))
            // Ordinary taps reorder actual native cards, without checking or changing solutions.
            val selected = generateSequence(actualCards.single { it.text.toString() == "480" } as View) {
                it.parent as? View
            }.first { it.isClickable }
            assertTrue(selected.performClick())
        }
        instrumentation.waitForIdleSync()
        host.onActivity { activity ->
            val actual = numberCards(orderingCard(activity)).map { it.text.toString() }
            assertEquals(listOf("480", "840", "804", "408"), actual)
            assertNumberOrder(blockSentences(currentScript(activity)), actual)
            assertEquals("Narration and reordering preserve validator answers and all original source text", sourceIdentity,
                field<JSONObject>(activity, "lessonJson").toString())
            assertEquals("A draft is not a solved exercise", false, ProgressRepository.getExerciseState(context, LESSON, PAGE, BLOCK)?.optBoolean("correct", false) ?: false)
            capture(orderingCard(activity), "published-ordering-after-ordinary-tap")
        }
        assertEquals(sourceIdentity, LessonRepository.loadLessonJson(context, ROUTE, LESSON).toString())
    }

    @Test fun nativeKokoroReadsSecondaryHelpAndRevealsEachVisibleCardIndependently() {
        val sourceIdentity = LessonRepository.loadLessonJson(context, ROUTE, LESSON).toString()
        val host = openMathPage()
        host.onActivity { activity ->
            reset(orderingCard(activity), activity)
            // Reach the ordering through the existing successful-feedback gate. The
            // previous published puzzle is solved normally and its explanation is
            // actually played, rather than faking curriculum listening credit.
            val previous = descendants(content(activity)).filterIsInstance<ViewGroup>().single { card ->
                card.tag == MathPuzzleRenderer.INTERACTION_TAG && descendants(card).filterIsInstance<TextView>()
                    .any { it.text.toString() == "Escribe el número representado por 6000 + 300 + 8." }
            }
            reset(previous, activity)
            descendants(previous).filterIsInstance<EditText>().single().setText("6308")
            assertTrue(descendants(previous).single { it.tag == MathPuzzleRenderer.CHECK_ACTION_TAG }.performClick())
        }
        instrumentation.waitForIdleSync()
        lateinit var help: ReadingTextView
        lateinit var cards: List<ReadingTextView>
        lateinit var spoken: List<String>
        lateinit var indices: List<Int>
        var instructionIndex = -1
        host.onActivity { activity ->
            val card = orderingCard(activity)
            help = descendants(card).filterIsInstance<ReadingTextView>().single {
                it.text.toString().startsWith("Arrastra una tarjeta directamente")
            }
            cards = numberCards(card)
            assertEquals(listOf("840", "804", "480", "408"), cards.map { it.text.toString() })
            assertTrue("Secondary instructions use the shared word-reading view", help.safeSupportsWordReading)
            assertTrue("Every original card supports the existing word mask", cards.all { it.safeSupportsWordReading })
            val script = currentScript(activity)
            val plan = requireNotNull(script.readingPlan(NarrationEngine.KOKORO))
            spoken = NarrationSegments.sentences(script.texts)
            val body = plan.origins.indices.filter { plan.origins[it].blockIndex == BLOCK && !plan.origins[it].feedback }
            instructionIndex = body.single { spoken[it].contains("Arrastra una tarjeta directamente") }
            indices = cards.map { view -> body.single { numberIn(spoken[it], view.text.toString()) } }
            assertEquals(indices.sorted(), indices)
            val key = NarrationResumePolicy.key(identity(activity), NarrationLanguage.SPANISH.tag,
                NarrationEngine.KOKORO.name, "ef_dora", spoken)
            val previousGate = NarrationScriptSegments.gates(script.texts, NarrationEngine.KOKORO, script.gates).first()
            assertEquals("The real prior puzzle uses its unchanged success gate", 2, previousGate.blockIndex)
            NarrationResumeStore(activity).begin(key, NarrationResumePoint(previousGate.start, 0L))
            help.requestRectangleOnScreen(Rect(0, 0, help.width, help.height), true)
            capture(card, "published-ordering-native-before")
            assertTrue(activity.findViewById<View>(R.id.buttonLessonListen).performClick())
        }
        val observed = linkedSetOf<Int>()
        val samples = JSONArray()
        var mixedHelp = false
        var mixedCards = false
        await("Model-timed Kokoro progressively reads the real secondary instruction and each displayed card", 180_000) {
            var done = false
            host.onActivity { activity ->
                val reader = controller(activity)
                val index = field<Int>(reader, "index")
                if (reader.isPlaying && field<Boolean>(reader, "playerReady")) {
                    val position = requireNotNull(field<MediaPlayer?>(reader, "player")).currentPosition
                    if (index == instructionIndex) {
                        val pending = unread(help).sumOf { it.endExclusive - it.start }
                        if (!mixedHelp && pending in 1 until help.text.count { !it.isWhitespace() } - 3) {
                            mixedHelp = true
                            capture(help, "published-ordering-secondary-help-mixed-native")
                        }
                    }
                    cards.forEachIndexed { ordinal, view ->
                        if (index == indices[ordinal] && unread(view).isEmpty()) observed += ordinal
                    }
                    if (!mixedCards && index in indices && cards.any { unread(it).isEmpty() } && cards.any { unread(it).isNotEmpty() }) {
                        mixedCards = true
                        capture(orderingCard(activity), "published-ordering-cards-mixed-native")
                    }
                    samples.put(JSONObject().put("segment", index).put("positionMs", position)
                        .put("pendingCards", JSONArray(cards.map { unread(it).isNotEmpty() })))
                }
                done = index > indices.last()
            }
            done
        }
        host.onActivity { activity ->
            val reader = controller(activity)
            reader.pauseKeepingPosition()
            assertTrue("Words within the real secondary instruction coexist as read and gray", mixedHelp)
            assertTrue("Earlier read cards and later gray cards coexist during actual playback", mixedCards)
            assertEquals("Every card reveals during its own audio, before the next card", cards.indices.toSet(), observed)
            assertTrue(cards.all { unread(it).isEmpty() })
            assertNull("Pause retains a bookmark while releasing MediaPlayer", field<MediaPlayer?>(reader, "player"))
            val savedSegment = field<Int>(reader, "index")
            val savedOffset = field<Long>(reader, "offset")
            assertEquals(NarrationResumePoint(savedSegment, savedOffset),
                NarrationResumeStore(activity).restore(field(reader, "resumeKey"), spoken.size)?.let {
                    NarrationResumePoint(it.segment, it.positionMs)
                })
            assertFalse("Reading options cannot solve the exercise",
                ProgressRepository.getExerciseState(context, LESSON, PAGE, BLOCK)?.optBoolean("correct", false) ?: false)
            writeEvidence("published-ordering-native", JSONObject().put("visualOrder", JSONArray(cards.map { it.text.toString() }))
                .put("spokenSegments", JSONArray(spoken)).put("nativePlaybackSamples", samples)
                .put("eachCardReadDuringItsOwnAudio", JSONArray(observed.toList())).put("secondaryWordMixed", mixedHelp)
                .put("pauseReleasedMediaPlayer", true))
            assertEquals(sourceIdentity, field<JSONObject>(activity, "lessonJson").toString())
        }
    }

    @Test fun publishedProgrammingOptionsKeepVisualOrderAndProgressWithinEachNativeOption() {
        val lesson = "cpp_fundamentals__programas_y_ejecucion__problemas_instrucciones_y_resultados"
        val source = LessonRepository.loadLessonJson(context, "programming", lesson)
        val sourceIdentity = source.toString()
        val block = source.getJSONArray("pages").getJSONObject(3).getJSONArray("blocks").getJSONObject(3)
        val options = block.getJSONArray("options").let { values -> List(values.length()) { values.getString(it) } }
        ProgressRepository.saveCurrentPage(context, "programming", lesson, 3)
        val host = ActivityScenario.launch<LessonActivity>(Intent(context, LessonActivity::class.java)
            .putExtra("routeId", "programming").putExtra("courseId", "cpp_fundamentals")
            .putExtra("unitId", "programas_y_ejecucion").putExtra("lessonId", lesson)
            .putExtra("lessonNumber", 1).putExtra("lessonTotal", 1)).also { scenario = it }
        await("The unchanged published Programming page and its native options open", 15_000) {
            var ready = false
            host.onActivity { activity -> ready = activity.hasWindowFocus() && field<Int>(activity, "currentPage") == 3 &&
                descendants(content(activity)).filterIsInstance<TextView>().any { it.text.toString() == options.last() } &&
                !content(activity).isLayoutRequested }
            ready
        }
        host.onActivity { activity ->
            NarrationSettingsRepository.saveEnabled(activity, true)
            invoke(activity, "renderCurrentPage")
        }
        await("Native Programming controls settle before playback", 10_000) {
            var ready = false
            host.onActivity { ready = !content(it).isLayoutRequested }
            ready
        }
        lateinit var views: List<TextView>
        lateinit var indices: List<Int>
        lateinit var sentences: List<String>
        host.onActivity { activity ->
            views = descendants(content(activity)).filterIsInstance<TextView>().filter { it.text.toString() in options }
            assertEquals("Existing C++ option order is unchanged", options, views.map { it.text.toString() })
            assertTrue(views.all { it is ReadingTextHost && it.safeSupportsWordReading })
            val script = currentScript(activity)
            sentences = NarrationSegments.sentences(script.texts)
            val plan = requireNotNull(script.readingPlan(NarrationEngine.KOKORO))
            indices = options.map { text ->
                val matches = sentences.indices.filter { index ->
                    plan.origins[index].blockIndex == 3 && !plan.origins[index].feedback &&
                        ManualEnglishSpeech.strip(sentences[index]) == text
                }
                assertEquals("The original option owns exactly one audio sentence after removing existing pronunciation metadata: $text; actual=$sentences",
                    1, matches.size)
                matches.single()
            }
            assertEquals(indices.sorted(), indices)
            assertEquals("The original correct index is untouched", 1, block.getInt("correctIndex"))
            val key = NarrationResumePolicy.key("programming/$lesson/3", NarrationLanguage.SPANISH.tag,
                NarrationEngine.KOKORO.name, "ef_dora", sentences)
            NarrationResumeStore(activity).begin(key, NarrationResumePoint(indices.first(), 0L))
            views.first().requestRectangleOnScreen(Rect(0, 0, views.first().width, views.first().height), true)
            assertTrue(activity.findViewById<View>(R.id.buttonLessonListen).performClick())
        }
        var mixedWords = false
        var mixedOptions = false
        val samples = JSONArray()
        await("Actual Kokoro retains word-level progression inside ordinary C++ puzzle options", 150_000) {
            var done = false
            host.onActivity { activity ->
                val reader = controller(activity)
                val index = field<Int>(reader, "index")
                if (reader.isPlaying && field<Boolean>(reader, "playerReady") && index in indices) {
                    val ordinal = indices.indexOf(index)
                    val view = views[ordinal]
                    val mask = unread(view)
                    val pending = mask.sumOf { it.endExclusive - it.start }
                    if (pending in 1 until view.text.count { !it.isWhitespace() } - 4) {
                        if (!mixedWords) capture(view, "published-programming-option-mixed-native")
                        mixedWords = true
                    }
                    if (ordinal > 0 && unread(views.first()).isEmpty() && unread(views.last()).isNotEmpty()) mixedOptions = true
                    samples.put(JSONObject().put("segment", index)
                        .put("positionMs", requireNotNull(field<MediaPlayer?>(reader, "player")).currentPosition)
                        .put("pendingCounts", JSONArray(views.map { unread(it).sumOf { range -> range.endExclusive - range.start } })))
                }
                done = index > indices.last()
            }
            done
        }
        host.onActivity { activity ->
            val reader = controller(activity)
            reader.pauseKeepingPosition()
            assertTrue("Words within one real option coexist as read and pending", mixedWords)
            assertTrue("The first option is read while the later visual option is pending", mixedOptions)
            assertTrue(views.all { unread(it).isEmpty() })
            assertNull(field<MediaPlayer?>(reader, "player"))
            assertEquals("Narration does not select or answer a puzzle", sourceIdentity, field<JSONObject>(activity, "lessonJson").toString())
            writeEvidence("published-programming-option-native", JSONObject().put("sourceLesson", lesson)
                .put("visualOptions", JSONArray(options)).put("spokenSegments", JSONArray(sentences))
                .put("mixedWords", mixedWords).put("mixedOptions", mixedOptions).put("nativeSamples", samples))
        }
        assertEquals(sourceIdentity, LessonRepository.loadLessonJson(context, "programming", lesson).toString())
    }

    @Test fun reorderingPausedNativeCardsKeepsTheirExactAudioOffsetAndTheSameThreeRecentPages() {
        val host = openMathPage()
        host.onActivity { activity ->
            reset(orderingCard(activity), activity)
            val previous = previousPuzzle(activity)
            reset(previous, activity)
            descendants(previous).filterIsInstance<EditText>().single().setText("6308")
            assertTrue(descendants(previous).single { it.tag == MathPuzzleRenderer.CHECK_ACTION_TAG }.performClick())
        }
        instrumentation.waitForIdleSync()
        val otherA = "puzzle-qa/$session/finished-page"
        val otherB = "puzzle-qa/$session/paused-page"
        val pointA = NarrationResumePoint(1, 234L, finished = true)
        val pointB = NarrationResumePoint(2, 567L)
        var firstCard = -1
        host.onActivity { activity ->
            val script = currentScript(activity)
            val sentences = NarrationSegments.sentences(script.texts)
            val plan = requireNotNull(script.readingPlan(NarrationEngine.KOKORO))
            firstCard = sentences.indices.single {
                plan.origins[it].blockIndex == BLOCK && !plan.origins[it].feedback && numberIn(sentences[it], "840")
            }
            val store = NarrationResumeStore(activity)
            val finishedOwner = store.begin(otherA, pointA.copy(finished = false))
            store.save(otherA, finishedOwner, pointA)
            store.begin(otherB, pointB)
            val previousGate = NarrationScriptSegments.gates(script.texts, NarrationEngine.KOKORO, script.gates).first()
            store.begin(NarrationResumePolicy.key(identity(activity), NarrationLanguage.SPANISH.tag,
                NarrationEngine.KOKORO.name, "ef_dora", sentences), NarrationResumePoint(previousGate.start))
            assertTrue(activity.findViewById<View>(R.id.buttonLessonListen).performClick())
        }
        await("The real first visible number is playing before a deliberate pause", 180_000) {
            var ready = false
            host.onActivity { activity ->
                val reader = controller(activity)
                ready = reader.isPlaying && field<Int>(reader, "index") == firstCard && field<Boolean>(reader, "playerReady") &&
                    requireNotNull(field<MediaPlayer?>(reader, "player")).currentPosition >= 300
            }
            ready
        }
        var offset = 0L
        var oldKey = ""
        var retainedText = ""
        host.onActivity { activity ->
            val reader = controller(activity)
            reader.pauseKeepingPosition()
            assertNull(field<MediaPlayer?>(reader, "player"))
            offset = field(reader, "offset")
            oldKey = requireNotNull(field<String?>(reader, "resumeKey"))
            retainedText = field<String>(field<List<Any>>(reader, "segments")[field(reader, "index")], "text")
            assertTrue(numberIn(retainedText, "840"))
            val target = numberCards(orderingCard(activity)).single { it.text.toString() == "480" }
            val clickable = generateSequence(target as View) { it.parent as? View }.first { it.isClickable }
            assertTrue(clickable.performClick())
        }
        instrumentation.waitForIdleSync()
        host.onActivity { activity ->
            assertEquals(listOf("480", "840", "804", "408"), numberCards(orderingCard(activity)).map { it.text.toString() })
            assertNumberOrder(blockSentences(currentScript(activity)), listOf("480", "840", "804", "408"))
            assertTrue(activity.findViewById<View>(R.id.buttonLessonListen).performClick())
        }
        await("The same original number resumes after moving to its new visual index", 25_000) {
            var ready = false
            host.onActivity { activity ->
                val reader = controller(activity)
                if (reader.isPlaying && field<Boolean>(reader, "playerReady")) {
                    val current = field<String>(field<List<Any>>(reader, "segments")[field(reader, "index")], "text")
                    ready = current == retainedText && requireNotNull(field<MediaPlayer?>(reader, "player")).currentPosition >= offset
                }
            }
            ready
        }
        host.onActivity { activity ->
            val reader = controller(activity)
            val player = requireNotNull(field<MediaPlayer?>(reader, "player"))
            val resumedPosition = player.currentPosition
            assertTrue("Resuming does not replay the numerical audio from zero", resumedPosition >= offset)
            assertTrue("Reordering preserves the exact checkpoint instead of jumping to another spoken word", resumedPosition <= offset + 800L)
            val currentIndex = field<Int>(reader, "index")
            assertTrue("The source utterance is remapped to its new displayed position", currentIndex > firstCard)
            val key = requireNotNull(field<String?>(reader, "resumeKey"))
            assertNotEquals("A changed visual sentence order has its own actual script identity", oldKey, key)
            val store = NarrationResumeStore(activity)
            assertEquals("A finished page still occupies one existing recent slot", pointA, store.restore(otherA, 2))
            assertEquals("The other paused page is not evicted by reordering one page", pointB, store.restore(otherB, 10))
            val keys = (0 until NarrationResumePolicy.MAX_BOOKMARKS).mapNotNull { resumePreferences().getString("key_$it", null) }
            assertEquals("Reordering relabels the same page; it cannot consume a fourth slot", setOf(otherA, otherB, key), keys.toSet())
            assertFalse("The previous script must not remain as a fourth/duplicate bookmark", oldKey in keys)
            reader.pauseKeepingPosition()
            assertNull(field<MediaPlayer?>(reader, "player"))
            writeEvidence("published-ordering-paused-reorder", JSONObject().put("retainedUtterance", retainedText)
                .put("offsetBeforeReorderMs", offset).put("resumedPositionMs", resumedPosition)
                .put("indexBeforeReorder", firstCard).put("indexAfterReorder", currentIndex)
                .put("sameThreePageSlots", JSONArray(keys)))
        }
    }

    @Test fun anActualLegacyRecordingKeepsItsPausedOffsetAndThreeSlotsAcrossActivityUpgrade() {
        val source = LessonRepository.loadLessonJson(context, ROUTE, LESSON)
        val sourceIdentity = source.toString()
        val oldHost = openMathPage()
        oldHost.onActivity { activity ->
            reset(orderingCard(activity), activity)
            val previous = previousPuzzle(activity)
            reset(previous, activity)
            descendants(previous).filterIsInstance<EditText>().single().setText("6308")
            assertTrue(descendants(previous).single { it.tag == MathPuzzleRenderer.CHECK_ACTION_TAG }.performClick())
        }
        instrumentation.waitForIdleSync()
        val otherA = "puzzle-legacy-qa/$session/finished-page"
        val otherB = "puzzle-legacy-qa/$session/paused-page"
        val pointA = NarrationResumePoint(1, 234L, finished = true)
        val pointB = NarrationResumePoint(2, 567L)
        lateinit var legacy: LessonNarrationScript
        lateinit var oldSentences: List<String>
        var oldNumber = -1
        var oldKey = ""
        oldHost.onActivity { activity ->
            legacy = LessonNarrationScript.presentedPage(source, PAGE, NarrationLanguage.SPANISH, true,
                instruction = { type -> if (type in setOf("order_code", "drag_fill_code"))
                    NarrationUiInstructions.forBlock(activity, type) else "" },
                presentation = { _, block -> com.learngraphicsprogramming.app.math.MathNarrationPresentation.legacyBlock(block) })
            val body = blockSentences(legacy)
            assertNumberOrder(body, listOf("408", "480", "804", "840"))
            assertFalse("This is the genuinely preceding script with its missing secondary help",
                body.any { it.contains("Arrastra una tarjeta directamente") })
            oldSentences = NarrationSegments.sentences(legacy.texts)
            val plan = requireNotNull(legacy.readingPlan(NarrationEngine.KOKORO))
            oldNumber = oldSentences.indices.single { plan.origins[it].blockIndex == BLOCK &&
                !plan.origins[it].feedback && numberIn(oldSentences[it], "840") }
            oldKey = NarrationResumePolicy.key(identity(activity), NarrationLanguage.SPANISH.tag,
                NarrationEngine.KOKORO.name, "ef_dora", oldSentences)
            val store = NarrationResumeStore(activity)
            val owner = store.begin(otherA, pointA.copy(finished = false))
            store.save(otherA, owner, pointA)
            store.begin(otherB, pointB)
            val previousGate = NarrationScriptSegments.gates(legacy.texts, NarrationEngine.KOKORO, legacy.gates).first()
            store.begin(oldKey, NarrationResumePoint(previousGate.start))
            // Use the existing real controller and its one normal player. The prior
            // successful feedback must still be naturally heard before these cards.
            controller(activity).toggle(legacy.texts, NarrationLanguage.SPANISH, identity(activity), legacy.gates, setOf(2))
        }
        await("The genuine preceding script actually plays its last ascending card", 180_000) {
            var ready = false
            oldHost.onActivity { activity ->
                val reader = controller(activity)
                ready = reader.isPlaying && field<Int>(reader, "index") == oldNumber && field<Boolean>(reader, "playerReady") &&
                    requireNotNull(field<MediaPlayer?>(reader, "player")).currentPosition >= 300
            }
            ready
        }
        var oldOffset = 0L
        var audioHash = ""
        lateinit var oldController: LessonNarrationController
        oldHost.onActivity { activity ->
            oldController = controller(activity)
            assertEquals(oldKey, field<String?>(oldController, "resumeKey"))
            oldController.pauseKeepingPosition()
            oldOffset = field(oldController, "offset")
            assertNull(field<MediaPlayer?>(oldController, "player"))
            val audio = requireNotNull(field<File?>(field<List<Any>>(oldController, "segments")[oldNumber], "file"))
            audioHash = sha256(audio)
            writeEvidence("published-ordering-legacy-real-pause", JSONObject().put("legacySpokenSegments", JSONArray(oldSentences))
                .put("segment", oldNumber).put("offsetMs", oldOffset).put("audioSha256", audioHash))
        }
        oldHost.close()
        scenario = null
        await("The old Activity releases its existing worker before reopening", 20_000) { !field<Thread>(oldController, "workerThread").isAlive }
        NarrationSettingsRepository.saveEnabled(context, false)
        val host = openMathPage()
        var currentNumber = -1
        var currentKey = ""
        host.onActivity { activity ->
            val script = currentScript(activity)
            val currentSentences = NarrationSegments.sentences(script.texts)
            assertNumberOrder(blockSentences(script), listOf("840", "804", "480", "408"))
            val plan = requireNotNull(script.readingPlan(NarrationEngine.KOKORO))
            currentNumber = currentSentences.indices.single { plan.origins[it].blockIndex == BLOCK &&
                !plan.origins[it].feedback && numberIn(currentSentences[it], "840") }
            currentKey = NarrationResumePolicy.key(identity(activity), NarrationLanguage.SPANISH.tag,
                NarrationEngine.KOKORO.name, "ef_dora", currentSentences)
            val migrated = requireNotNull(NarrationResumeStore(activity).restore(currentKey, currentSentences.size))
            assertEquals("The same real recording is remapped to the new visible position", currentNumber, migrated.segment)
            assertEquals("An app-side presentation update retains the exact saved milliseconds", oldOffset, migrated.positionMs)
            assertTrue(activity.findViewById<View>(R.id.buttonLessonListen).performClick())
        }
        await("The updated Activity resumes the identical Kokoro recording at its existing offset", 30_000) {
            var ready = false
            host.onActivity { activity ->
                val reader = controller(activity)
                ready = reader.isPlaying && field<Int>(reader, "index") == currentNumber && field<Boolean>(reader, "playerReady") &&
                    requireNotNull(field<MediaPlayer?>(reader, "player")).currentPosition >= oldOffset
            }
            ready
        }
        host.onActivity { activity ->
            val reader = controller(activity)
            val actualPosition = requireNotNull(field<MediaPlayer?>(reader, "player")).currentPosition
            assertTrue(actualPosition <= oldOffset + 800L)
            val audio = requireNotNull(field<File?>(field<List<Any>>(reader, "segments")[currentNumber], "file"))
            assertEquals("Presentation corrections reuse the previously verified recording without regeneration", audioHash, sha256(audio))
            val store = NarrationResumeStore(activity)
            assertEquals(pointA, store.restore(otherA, 2))
            assertEquals(pointB, store.restore(otherB, 10))
            val keys = (0 until NarrationResumePolicy.MAX_BOOKMARKS).mapNotNull { resumePreferences().getString("key_$it", null) }
            assertEquals(setOf(otherA, otherB, currentKey), keys.toSet())
            assertFalse(oldKey in keys)
            reader.pauseKeepingPosition()
            assertNull(field<MediaPlayer?>(reader, "player"))
            assertEquals(sourceIdentity, field<JSONObject>(activity, "lessonJson").toString())
            writeEvidence("published-ordering-legacy-upgrade", JSONObject().put("oldSegment", oldNumber)
                .put("newSegment", currentNumber).put("savedOffsetMs", oldOffset).put("actualResumedPositionMs", actualPosition)
                .put("sameAudioSha256", audioHash).put("sameThreePageSlots", JSONArray(keys)))
        }
        assertEquals(sourceIdentity, LessonRepository.loadLessonJson(context, ROUTE, LESSON).toString())
    }

    @Test fun pausedAndroidPresentationRebindNeedsNoTtsEngine() {
        val page = "puzzle-android-metadata/$session/page"
        val otherA = "puzzle-android-metadata/$session/finished"
        val otherB = "puzzle-android-metadata/$session/paused"
        val pointA = NarrationResumePoint(1, 234L, finished = true)
        val pointB = NarrationResumePoint(2, 567L)
        val old = LessonNarrationScript(listOf("Primera tarjeta.", "Segunda tarjeta."), emptyList(),
            List(2) { NarrationTextOrigin(0) })
        val current = LessonNarrationScript(listOf("Arrastra las tarjetas.", "Segunda tarjeta.", "Primera tarjeta."),
            emptyList(), List(3) { NarrationTextOrigin(0) })
        val failures = mutableListOf<String>()
        lateinit var reader: LessonNarrationController
        instrumentation.runOnMainSync {
            NarrationSettingsRepository.saveEngine(context, NarrationEngine.ANDROID)
            NarrationSettingsRepository.saveEnabled(context, true)
            reader = LessonNarrationController(context, {}, { failures += it })
        }
        try {
            instrumentation.runOnMainSync {
                val settings = NarrationSettingsRepository.forLanguage(context, NarrationLanguage.SPANISH)
                val oldKey = NarrationResumePolicy.key(page, "es", NarrationEngine.ANDROID.name,
                    settings.androidVoice.orEmpty(), NarrationSegments.prepare(old.texts, 250))
                val newKey = NarrationResumePolicy.key(page, "es", NarrationEngine.ANDROID.name,
                    settings.androidVoice.orEmpty(), NarrationSegments.prepare(current.texts, 250))
                val store = NarrationResumeStore(context)
                val owner = store.begin(otherA, pointA.copy(finished = false))
                store.save(otherA, owner, pointA)
                store.begin(otherB, pointB)
                store.begin(oldKey, NarrationResumePoint(0, 423L))
                reader.bindPausedPresentation(old.texts, NarrationLanguage.SPANISH, page)
                assertEquals(0, field<Int>(reader, "index"))
                assertEquals(423L, field<Long>(reader, "offset"))
                val update = reader.relocatePresentation(old, current, NarrationLanguage.SPANISH, page)
                assertTrue(update.rebind)
                assertFalse(update.resume)
                reader.bindPausedPresentation(current.texts, NarrationLanguage.SPANISH, page)
                assertEquals("A paused Android checkpoint follows the unchanged utterance", 2, field<Int>(reader, "index"))
                assertEquals(423L, field<Long>(reader, "offset"))
                assertFalse(reader.isPlaying)
                assertNull("Metadata rebinding cannot retain or create a MediaPlayer", field<MediaPlayer?>(reader, "player"))
                assertNull("No installed Android TTS engine is necessary to preserve a paused position", field<Any?>(reader, "tts"))
                assertFalse(field<Boolean>(reader, "ttsInitializing"))
                assertEquals(pointA, store.restore(otherA, 2))
                assertEquals(pointB, store.restore(otherB, 10))
                assertEquals(NarrationResumePoint(2, 423L), store.restore(newKey, 3))
                val keys = (0 until NarrationResumePolicy.MAX_BOOKMARKS).mapNotNull { resumePreferences().getString("key_$it", null) }
                assertEquals(setOf(otherA, otherB, newKey), keys.toSet())
                assertTrue("Binding metadata must not request unsupported synthesis", failures.isEmpty())
                writeEvidence("paused-android-presentation-metadata", JSONObject().put("voicePlaybackTest", false)
                    .put("offsetMs", 423L).put("oldIndex", 0).put("newIndex", 2)
                    .put("sameThreePageSlots", JSONArray(keys)).put("ttsInitialized", false).put("mediaPlayerRetained", false))
            }
        } finally {
            instrumentation.runOnMainSync { reader.close() }
            await("The metadata-only controller closes its existing worker", 20_000) { !field<Thread>(reader, "workerThread").isAlive }
        }
    }

    private fun openMathPage(): ActivityScenario<LessonActivity> {
        ProgressRepository.saveCurrentPage(context, ROUTE, LESSON, PAGE)
        val host = ActivityScenario.launch<LessonActivity>(Intent(context, LessonActivity::class.java)
            .putExtra("routeId", ROUTE).putExtra("courseId", "math_course_01")
            .putExtra("unitId", "numeros_y_operaciones").putExtra("lessonId", LESSON)
            .putExtra("lessonNumber", 1).putExtra("lessonTotal", 1)).also { scenario = it }
        await("The exact published M1 U1 L1 P3 page opens", 15_000) {
            var ready = false
            host.onActivity { activity -> ready = activity.hasWindowFocus() && field<Int>(activity, "currentPage") == PAGE &&
                descendants(content(activity)).filterIsInstance<TextView>().any { it.text.toString() == PROMPT } &&
                !content(activity).isLayoutRequested }
            ready
        }
        host.onActivity { activity ->
            NarrationSettingsRepository.saveEnabled(activity, true)
            invoke(activity, "renderCurrentPage")
        }
        await("Published puzzle native layout settles", 10_000) {
            var ready = false
            host.onActivity { ready = !content(it).isLayoutRequested && orderingCard(it).width > 0 }
            ready
        }
        return host
    }

    private fun orderingCard(activity: LessonActivity): ViewGroup = descendants(content(activity)).filterIsInstance<ViewGroup>()
        .single { card -> card.tag == MathPuzzleRenderer.INTERACTION_TAG && descendants(card).filterIsInstance<TextView>()
            .any { it.text.toString() == PROMPT } }
    private fun numberCards(card: ViewGroup) = descendants(card).filterIsInstance<ReadingTextView>()
        .filter { it.text.toString() in setOf("840", "804", "480", "408") }
    private fun reset(card: ViewGroup, activity: LessonActivity) {
        assertTrue(descendants(card).single { it.contentDescription == activity.getString(R.string.math_ui_restart) }.performClick())
    }
    private fun previousPuzzle(activity: LessonActivity): ViewGroup = descendants(content(activity)).filterIsInstance<ViewGroup>()
        .single { card -> card.tag == MathPuzzleRenderer.INTERACTION_TAG && descendants(card).filterIsInstance<TextView>()
            .any { it.text.toString() == "Escribe el número representado por 6000 + 300 + 8." } }
    private fun blockSentences(script: LessonNarrationScript): List<String> {
        val plan = requireNotNull(script.readingPlan(NarrationEngine.KOKORO))
        val all = NarrationSegments.sentences(script.texts)
        return all.filterIndexed { index, _ -> plan.origins[index].blockIndex == BLOCK && !plan.origins[index].feedback }
    }
    private fun assertNumberOrder(spoken: List<String>, visual: List<String>) {
        val indices = visual.map { value -> spoken.indexOfFirst { numberIn(it, value) }.also {
            assertTrue("Visible card $value must have its own spoken item: $spoken", it >= 0)
        } }
        assertEquals("Speech follows actual top-to-bottom native cards, never validator order", indices.sorted(), indices)
        assertEquals("Distinct cards cannot share one delayed read boundary", indices.size, indices.toSet().size)
    }
    private fun numberIn(spoken: String, value: String): Boolean {
        // Math speech legitimately expands a visible numeral (840) to its actual
        // Spanish words (ochocientos cuarenta). Match that existing frontend,
        // never an answer key or a test-only pronunciation substitution.
        val authoredSpeech = MultilingualMathSpeech.text(value, "auto", NarrationLanguage.SPANISH)
        return listOf(value, authoredSpeech).distinct().any { candidate ->
            Regex("(?<![\\p{L}\\p{N}])${Regex.escape(candidate)}(?![\\p{L}\\p{N}])").containsMatchIn(spoken)
        }
    }
    private fun currentScript(activity: LessonActivity): LessonNarrationScript = LessonActivity::class.java
        .getDeclaredMethod("narrationScript", NarrationLanguage::class.java).apply { isAccessible = true }
        .invoke(activity, NarrationLanguage.SPANISH) as LessonNarrationScript
    private fun unread(view: TextView): List<ReadingTextRange> = field((view as ReadingTextHost).readingInk, "unreadRanges")
    private fun controller(activity: LessonActivity): LessonNarrationController = field(activity, "narrationController")
    private fun content(activity: LessonActivity): ViewGroup = activity.findViewById(R.id.lessonContent)
    private fun identity(activity: LessonActivity) = "$ROUTE/$LESSON/${field<Int>(activity, "currentPage")}"
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
            view.draw(Canvas(bitmap))
            File(evidence(), "$name.png").outputStream().use { assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) }
        } finally { bitmap.recycle() }
    }
    private fun writeEvidence(name: String, value: JSONObject) = File(evidence(), "$name.json").writeText(value.toString(2))
    private fun evidence() = File(requireNotNull(context.getExternalFilesDir(null)), "puzzle-visual-narration-qa/$session").apply { mkdirs() }
    private fun sha256(file: File) = MessageDigest.getInstance("SHA-256").digest(file.readBytes()).joinToString("") { "%02x".format(it) }
    private fun resumePreferences() = context.getSharedPreferences("lesson_narration_resume_bounded_v1", Context.MODE_PRIVATE)
    private fun preferenceFiles() = File(context.applicationInfo.dataDir, "shared_prefs").listFiles().orEmpty()
        .filter { it.name.endsWith(".xml") }.map { it.name.removeSuffix(".xml") }.toSet()

    companion object {
        private val QA_PACKAGES = setOf("com.learngraphicsprogramming.app.curricularqa", "com.learngraphicsprogramming.app.realdownloadsqa")
        private val KNOWN_PREFERENCES = setOf("learning_progress", "navigation_state", "lesson_narration_settings", "app_settings",
            "appearance_settings", "ui_language_settings", "premium_state", "lesson_text_size_settings", "lesson_narration_resume_bounded_v1")
        private const val ROUTE = "math_physics"
        private const val LESSON = "math_course_01__numeros_y_operaciones__magnitudes_y_numeracion"
        private const val PAGE = 2
        private const val BLOCK = 3
        private const val PROMPT = "Ordena de menor a mayor los números."
    }
}
