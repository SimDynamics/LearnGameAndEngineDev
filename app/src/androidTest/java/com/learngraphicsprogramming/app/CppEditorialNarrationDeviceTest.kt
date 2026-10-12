package com.learngraphicsprogramming.app

import android.app.Activity
import android.app.Application
import android.app.KeyguardManager
import android.content.Context
import android.content.Intent
import android.content.pm.ActivityInfo
import android.os.Bundle
import android.os.ParcelFileDescriptor
import android.os.PowerManager
import android.os.SystemClock
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.TextView
import androidx.lifecycle.Lifecycle
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.narration.KokoroNarrationCache
import com.learngraphicsprogramming.app.narration.KokoroPhonemeChunks
import com.learngraphicsprogramming.app.narration.LessonNarrationText
import com.learngraphicsprogramming.app.narration.NarrationLanguage
import com.learngraphicsprogramming.app.narration.NarrationSegments
import com.learngraphicsprogramming.app.narration.NarrationSettingsRepository
import com.learngraphicsprogramming.app.narration.NarrationUiInstructions
import com.learngraphicsprogramming.app.narration.phonemes.SpanishFluidG2P
import org.json.JSONArray
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/**
 * Current editorial fixtures through the real repository, narration extractor and renderer.
 * This verifies text/G2P/cache identities, not synthesis or the quality of new recordings.
 * All preference writes and activity launches require the isolated curricularqa application.
 */
@RunWith(AndroidJUnit4::class)
class CppEditorialNarrationDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private var screenGuardRegistered = false
    private val activityObservations = ArrayDeque<String>()
    private data class QaActivityRecord(
        val activity: LessonActivity, val restoredFromState: Boolean,
        var savedPage: Int? = null, var savedLesson: String? = null, var savedJson: String? = null,
        var destroyedForConfiguration: Boolean = false, var configurationChanges: Int = 0
    )
    private val activityRecords = mutableListOf<QaActivityRecord>()
    private val screenGuard = object : Application.ActivityLifecycleCallbacks {
        override fun onActivityCreated(activity: Activity, state: Bundle?) {
            if (activity is LessonActivity && activity.packageName == QA_PACKAGE) {
                activity.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                activityRecords.add(QaActivityRecord(activity, state != null))
                recordActivityObservation("created", activity)
            }
        }
        override fun onActivityStarted(activity: Activity) = Unit
        override fun onActivityResumed(activity: Activity) = Unit
        override fun onActivityPaused(activity: Activity) = Unit
        override fun onActivityStopped(activity: Activity) = Unit
        override fun onActivitySaveInstanceState(activity: Activity, state: Bundle) {
            if (activity is LessonActivity && activity.packageName == QA_PACKAGE) {
                activityRecords.single { it.activity === activity }.apply {
                    savedPage = field(activity, "currentPage") as Int
                    savedLesson = field(activity, "currentLessonId") as String
                    savedJson = (field(activity, "lessonJson") as JSONObject).toString()
                }
            }
        }
        override fun onActivityDestroyed(activity: Activity) {
            if (activity is LessonActivity && activity.packageName == QA_PACKAGE) {
                activityRecords.single { it.activity === activity }.apply {
                    destroyedForConfiguration = activity.isChangingConfigurations
                    configurationChanges = activity.changingConfigurations
                }
                recordActivityObservation("destroyed", activity)
            }
        }
    }

    @Before fun requireIsolatedAwakeApplicationBeforePreferences() {
        requireQaPackage()
        requireAwakeUnlockedQaDisplay()
        (context.applicationContext as Application).registerActivityLifecycleCallbacks(screenGuard)
        screenGuardRegistered = true
        // These settings belong exclusively to curricularqa. No synthesis is requested.
        NarrationSettingsRepository.saveEnabled(context, false)
        NarrationSettingsRepository.saveLanguage(context, NarrationLanguage.SPANISH)
        // Select the QA UI language before acceptance, not during it: requesting a locale
        // can asynchronously recreate activities. Never change the normal application's locale.
        instrumentation.runOnMainSync {
            assertEquals("The isolated QA application must already select Spanish", "es",
                UiLanguageSettings.selectedTag(context))
            assertEquals("The selected locale must have reached the QA resources", "es",
                context.resources.configuration.locales[0].language)
            assertEquals("The QA application configuration must also have settled", "es",
                context.applicationContext.resources.configuration.locales[0].language)
        }
        instrumentation.waitForIdleSync()
    }

    @After fun releaseOnlyTheQaScreenGuard() {
        // JUnit calls After even when Before rejects a normal-package invocation.
        if (context.packageName != QA_PACKAGE || BuildConfig.APPLICATION_ID != QA_PACKAGE ||
            context.applicationContext.packageName != QA_PACKAGE) return
        if (screenGuardRegistered) {
            (context.applicationContext as Application).unregisterActivityLifecycleCallbacks(screenGuard)
            screenGuardRegistered = false
        }
    }

    @Test fun editorialU1PagesRenderAndNarrateCurrentText() =
        verifyFixture("cpp_editorial_u1.json", setOf("programas_y_ejecucion"))

    @Test fun editorialU2ToU4PagesRenderAndNarrateCurrentText() =
        verifyFixture("cpp_editorial_u2_u4.json", setOf(
            "objetos_y_estado", "expresiones_y_decisiones", "repeticion_y_terminacion"))

    @Test fun editorialU5ToU8PagesRenderAndNarrateCurrentText() =
        verifyFixture("cpp_editorial_u5_u8.json", setOf(
            "colecciones_y_recorridos", "funciones_y_contratos",
            "errores_y_construccion_gradual", "excepciones_y_recuperacion"))

    @Test fun editorialU9ToU12PagesRenderAndNarrateCurrentText() =
        verifyFixture("cpp_editorial_u9_u12.json", setOf(
            "datos_propios_y_estado_valido", "aleatoriedad_y_reproducibilidad",
            "texto_interpretacion_y_flujos", "archivos_y_programa_completo"))

    @Test fun masterApa7AttributionsAndNewlineBridgeRenderAndNarrateCurrentText() =
        verifyFixture("cpp_master_apa7_revision.json", setOf(
            "programas_y_ejecucion", "objetos_y_estado", "expresiones_y_decisiones",
            "repeticion_y_terminacion", "colecciones_y_recorridos", "funciones_y_contratos",
            "errores_y_construccion_gradual", "excepciones_y_recuperacion",
            "datos_propios_y_estado_valido", "aleatoriedad_y_reproducibilidad",
            "texto_interpretacion_y_flujos", "archivos_y_programa_completo"))

    private data class Revision(
        val file: String, val lessonId: String, val page: Int, val block: Int,
        val field: String, val before: String, val after: String
    ) {
        val location: String get() = "$lessonId p${page}b${block}.${this.field}"
    }

    private fun verifyFixture(asset: String, allowedUnits: Set<String>) {
        requireQaPackage()
        val array = JSONArray(instrumentation.context.assets.open(asset)
            .bufferedReader(Charsets.UTF_8).use { it.readText() })
        assertTrue("The packaged editorial fixture must be nonempty: $asset", array.length() > 0)
        val revisions = (0 until array.length()).map { index ->
            val item = array.getJSONObject(index)
            Revision(item.getString("file"), item.getString("lessonId"), item.getInt("page"),
                item.getInt("block"), item.getString("field"), item.getString("before"), item.getString("after"))
        }
        assertEquals("One before/after entry per exact scalar coordinate", revisions.size,
            revisions.map { it.location }.toSet().size)
        revisions.forEach { change ->
            val parts = change.lessonId.split("__", limit = 3)
            assertEquals(3, parts.size)
            assertEquals(COURSE, parts[0])
            assertTrue("Fixture stays inside its assigned unit group", parts[1] in allowedUnits)
            assertEquals("app/src/main/assets/content/es/$ROUTE/$COURSE/${parts[1]}/lesson_${parts[2]}.json",
                change.file)
            assertTrue(change.page > 0 && change.block > 0)
            assertTrue("Only authorized editorial strings are reconstructed",
                change.field in setOf("text", "question", "explanation", "option:1"))
            if (change.field == "option:1") {
                assertEquals("The sole authorized option clarification stays at its exact coordinate",
                    "cpp_fundamentals__programas_y_ejecucion__primer_programa_y_salida", change.lessonId)
                assertEquals(5, change.page)
                assertEquals(5, change.block)
            }
            assertNotEquals("A recorded revision must change its scalar", change.before, change.after)
        }

        for ((lessonId, changes) in revisions.groupBy { it.lessonId }) {
            val lesson = LessonRepository.loadLessonJson(context, ROUTE, lessonId)
            assertEquals(lessonId, lesson.getString("id"))
            val assetLesson = JSONObject(context.assets.open(changes.first().file.removePrefix("app/src/main/assets/"))
                .bufferedReader(Charsets.UTF_8).use { it.readText() })
            assertEquals("LessonRepository loads the exact fixture asset", assetLesson.toString(), lesson.toString())
            val previous = JSONObject(lesson.toString())
            changes.forEach { change ->
                assertEquals("The APK includes the recorded current text: ${change.location}", change.after,
                    scalar(blockAt(lesson, change.page, change.block), change.field))
                val oldBlock = blockAt(previous, change.page, change.block)
                if (change.field == "option:1") oldBlock.getJSONArray("options").put(1, change.before)
                else oldBlock.put(change.field, change.before)
            }
            val pageGroups = changes.groupBy { it.page }.toSortedMap()
            pageGroups.forEach { (page, pageChanges) ->
                verifyNarrationRevision(lesson, previous, page, pageChanges)
            }
            renderAffectedPages(lesson, pageGroups)
        }
    }

    private fun pageText(lesson: JSONObject, page: Int): List<String> =
        LessonNarrationText.page(lesson, page - 1, NarrationLanguage.SPANISH) {
            NarrationUiInstructions.forBlock(context, it)
        }

    private fun verifyNarrationRevision(
        lesson: JSONObject, previous: JSONObject, page: Int, changes: List<Revision>
    ) {
        val location = "${lesson.getString("id")} p$page"
        val currentText = pageText(lesson, page)
        val oldText = pageText(previous, page)
        assertTrue("An affected page has authored narration: $location", currentText.isNotEmpty())
        val currentSentences = NarrationSegments.sentences(currentText)
        val oldSentences = NarrationSegments.sentences(oldText)
        if (changes.all { it.field == "explanation" }) {
            assertEquals("Unanswered feedback edits cannot change page narration: $location", oldText, currentText)
        } else {
            assertNotEquals("Visible editorial revisions reach narration: $location", oldText, currentText)
        }

        // Exercise feedback must be absent even if its content is unique and impossible to confuse
        // with an authored paragraph or an option. Neither the original nor current feedback is read.
        for (version in listOf(lesson, previous)) {
            val poisoned = JSONObject(version.toString())
            val blocks = poisoned.getJSONArray("pages").getJSONObject(page - 1).getJSONArray("blocks")
            for (index in 0 until blocks.length()) {
                blocks.getJSONObject(index).put("explanation", "Retroalimentacion privada no mostrada $index")
                    .put("feedback", JSONObject().put("text", "Respuesta privada no mostrada $index"))
            }
            assertEquals("Hidden feedback is excluded before answering: $location",
                pageText(version, page), pageText(poisoned, page))
        }

        // The production controller first phonemizes an entire sentence, then sends these exact
        // bounded chunks to Kokoro. Preserve the complete phoneme string, including long sentences.
        currentSentences.forEachIndexed { index, sentence ->
            val phonemes = SpanishFluidG2P.phonemize(sentence)
            val parts = KokoroPhonemeChunks.split(phonemes)
            assertTrue("Strict Spanish G2P produces nonempty engine segments: $location sentence ${index + 1}",
                parts.isNotEmpty() && parts.all { it.length in 1..510 && it.isNotBlank() })
            assertEquals("No phonemes are skipped to satisfy the engine limit", phonemes, parts.joinToString(""))
        }
        val oldKeys = oldSentences.map { KokoroNarrationCache.key("es", VOICE, it) }.toSet()
        val currentKeys = currentSentences.map { KokoroNarrationCache.key("es", VOICE, it) }.toSet()
        (currentSentences.toSet() - oldSentences.toSet()).forEach { sentence ->
            assertFalse("A revised sentence requires its own cache key: $location",
                KokoroNarrationCache.key("es", VOICE, sentence) in oldKeys)
        }
        (oldSentences.toSet() - currentSentences.toSet()).forEach { sentence ->
            assertFalse("A removed sentence is absent from the current cache playlist: $location",
                KokoroNarrationCache.key("es", VOICE, sentence) in currentKeys)
        }
    }

    private fun renderAffectedPages(lesson: JSONObject, pageGroups: Map<Int, List<Revision>>) {
        requireAwakeUnlockedQaDisplay()
        val lessonId = lesson.getString("id")
        val firstPage = pageGroups.keys.first()
        // Reset only this lesson in the isolated copy; never answer or run its exercises.
        ProgressRepository.resetLesson(context, ROUTE, lessonId)
        ProgressRepository.saveCurrentPage(context, ROUTE, lessonId, firstPage - 1)
        val parts = lessonId.split("__", limit = 3)
        val intent = Intent(context, LessonActivity::class.java)
            .putExtra("routeId", ROUTE).putExtra("courseId", COURSE).putExtra("unitId", parts[1])
            .putExtra("lessonId", lessonId).putExtra("lessonNumber", 1).putExtra("lessonTotal", 1)
        ActivityScenario.launch<LessonActivity>(intent).use { scenario ->
            instrumentation.waitForIdleSync()
            assertEquals("An unlocked foreground QA activity must be resumed", Lifecycle.State.RESUMED, scenario.state)
            lateinit var observedActivity: LessonActivity
            scenario.onActivity { observedActivity = it }
            var observedPage = firstPage - 1
            observedActivity = awaitFocusedQaPage(scenario, observedActivity, lesson, observedPage)
            for ((page, changes) in pageGroups) {
                scenario.onActivity { activity ->
                    // A configuration recreation must restore the previous page on its own.
                    // Validate before navigating; never use showPage to repair lost state.
                    observedActivity = requireQaPageContinuity(observedActivity, activity, lesson, observedPage)
                    if (observedPage != page - 1) showPage(activity, page - 1)
                }
                observedPage = page - 1
                instrumentation.waitForIdleSync()
                observedActivity = awaitFocusedQaPage(scenario, observedActivity, lesson, observedPage) { activity ->
                    observedActivity = requireQaPageContinuity(observedActivity, activity, lesson, observedPage)
                    assertEquals(page - 1, field(activity, "currentPage"))
                    assertTrue("The current QA renderer has a foreground window: $lessonId p$page",
                        activity.hasWindowFocus())
                    val loaded = field(activity, "lessonJson") as JSONObject
                    assertEquals("The renderer uses the published current lesson", lesson.toString(), loaded.toString())
                    @Suppress("UNCHECKED_CAST")
                    val actualNarration = LessonActivity::class.java.getDeclaredMethod("pageNarrationText", NarrationLanguage::class.java)
                        .apply { isAccessible = true }.invoke(activity, NarrationLanguage.SPANISH) as List<String>
                    assertEquals("Play/prefetch use the same current page text", pageText(lesson, page), actualNarration)
                    val texts = descendants(activity.findViewById<ViewGroup>(R.id.lessonContent))
                        .filterIsInstance<TextView>().filter { it.visibility == View.VISIBLE && it.isShown }
                        .map { it.text.toString() }
                    changes.forEach { change ->
                        val block = blockAt(loaded, page, change.block)
                        if (change.field == "explanation") {
                            assertTrue("The associated unanswered exercise is rendered: ${change.location}",
                                block.getString("question") in texts)
                        } else {
                            val expected = if (block.getString("type") == "note")
                                activity.getString(R.string.lesson_note, change.after) else change.after
                            assertTrue("The real renderer displays the revised scalar: ${change.location}", expected in texts)
                        }
                    }
                    val blocks = loaded.getJSONArray("pages").getJSONObject(page - 1).getJSONArray("blocks")
                    for (index in 0 until blocks.length()) {
                        val block = blocks.getJSONObject(index)
                        if (block.getString("type") !in EXERCISE_TYPES) continue
                        assertNull("No answer is restored or submitted during editorial acceptance",
                            ProgressRepository.getExerciseState(activity, lessonId, page - 1, index))
                        val explanation = block.optString("explanation", "")
                        if (explanation.isNotBlank()) assertFalse("Unanswered feedback is hidden by the actual renderer",
                            activity.getString(R.string.lesson_correct_explanation, explanation) in texts)
                    }
                    assertEquals("Reading pages does not solve evaluated exercises", 0, field(activity, "completedExercises"))
                }
            }
        }
    }

    private fun awaitFocusedQaPage(
        scenario: ActivityScenario<LessonActivity>, expectedActivity: LessonActivity,
        lesson: JSONObject, page: Int, verifyFocusedPage: (LessonActivity) -> Unit = {}
    ): LessonActivity {
        requireQaPackage()
        val lessonId = lesson.getString("id")
        var observedActivity = expectedActivity
        val power = context.getSystemService(Context.POWER_SERVICE) as PowerManager
        val keyguard = context.getSystemService(Context.KEYGUARD_SERVICE) as KeyguardManager
        val deadline = SystemClock.uptimeMillis() + 10_000L
        var observed = "No QA window observation"
        while (true) {
            assertTrue("The display must remain awake while waiting for QA focus", power.isInteractive)
            assertFalse("Do not dismiss a visual lock screen during acceptance", keyguard.isKeyguardLocked)
            assertFalse("Do not unlock device security during acceptance", keyguard.isDeviceLocked)
            assertEquals("Focus must arrive without moving the QA activity out of RESUMED",
                Lifecycle.State.RESUMED, scenario.state)
            var focused = false
            scenario.onActivity { activity ->
                observedActivity = requireQaPageContinuity(observedActivity, activity, lesson, page)
                val decor = activity.window.decorView
                val content = activity.findViewById<ViewGroup>(R.id.lessonContent)
                focused = activity.hasWindowFocus() && decor.isAttachedToWindow && decor.windowToken != null &&
                    content.isShown && content.width > 0 && content.height > 0
                observed = "activity=${System.identityHashCode(activity)}, lesson=$lessonId, page=${page + 1}, " +
                    "focus=${activity.hasWindowFocus()}, attached=${decor.isAttachedToWindow}, " +
                    "windowVisibility=${decor.windowVisibility}, contentShown=${content.isShown}, " +
                    "content=${content.width}x${content.height}"
                // Observe focus and verify the page in the same main-thread callback.
                // A later callback could receive a window whose focus has since changed.
                if (focused) verifyFocusedPage(activity)
            }
            if (focused) return observedActivity
            if (SystemClock.uptimeMillis() >= deadline) break
            // This sleep is on the instrumentation thread; the main thread can receive window focus.
            // Poll observations only. Never relaunch, recreate, rerender or repeat a completed case.
            SystemClock.sleep(50L)
        }
        fail("QA window focus did not arrive within 10 seconds: $observed; state=${scenario.state}, " +
            "interactive=${power.isInteractive}, keyguardLocked=${keyguard.isKeyguardLocked}, " +
            "deviceLocked=${keyguard.isDeviceLocked}")
        error("Unreachable after assertion failure")
    }

    private fun requireQaPageContinuity(
        previous: LessonActivity, current: LessonActivity, lesson: JSONObject, page: Int
    ): LessonActivity {
        val lessonId = lesson.getString("id")
        val diagnostic = "$lessonId p${page + 1}; ${activityObservations.joinToString("; ")}"
        assertEquals(QA_PACKAGE, current.packageName)
        assertEquals(ROUTE, current.intent.getStringExtra("routeId"))
        assertEquals(COURSE, current.intent.getStringExtra("courseId"))
        assertEquals(lessonId.split("__", limit = 3)[1], current.intent.getStringExtra("unitId"))
        assertEquals(lessonId, current.intent.getStringExtra("lessonId"))
        assertEquals("No lesson is replaced: $diagnostic", lessonId, field(current, "currentLessonId"))
        assertEquals("The page is restored before any test navigation: $diagnostic", page, field(current, "currentPage"))
        assertEquals("The full current lesson survives configuration: $diagnostic", lesson.toString(),
            (field(current, "lessonJson") as JSONObject).toString())
        assertEquals("Recreation preserves the saved page: $diagnostic", page,
            ProgressRepository.getCurrentPage(context, lessonId))
        assertEquals("Recreation cannot complete evaluated exercises: $diagnostic", 0, field(current, "completedExercises"))
        if (previous !== current) {
            val from = activityRecords.indexOfFirst { it.activity === previous }
            val to = activityRecords.indexOfFirst { it.activity === current }
            assertTrue("Only a recorded lifecycle successor is accepted: $diagnostic", from >= 0 && to > from)
            val chain = activityRecords.subList(from, to + 1)
            chain.forEach { record ->
                assertEquals("The lifecycle chain stays in this lesson: $diagnostic", lessonId,
                    record.activity.intent.getStringExtra("lessonId"))
            }
            chain.dropLast(1).forEach { record ->
                assertTrue("The prior instance was destroyed for configuration: $diagnostic", record.destroyedForConfiguration)
                // The Honor reports CONFIG_SCREEN_LAYOUT (0x100). Accept only that observed
                // platform recreation, not an arbitrary relaunch, reset or retry of the case.
                assertEquals("Only the observed screen-layout recreation is accepted: $diagnostic",
                    ActivityInfo.CONFIG_SCREEN_LAYOUT, record.configurationChanges)
                assertEquals("Android saved the page being observed: $diagnostic", page, record.savedPage)
                assertEquals(lessonId, record.savedLesson)
                assertEquals(lesson.toString(), record.savedJson)
            }
            chain.drop(1).forEach { record ->
                assertTrue("Every successor was created with saved instance state: $diagnostic", record.restoredFromState)
            }
        }
        return current
    }

    private fun requireQaPackage() {
        assertEquals("Build/install the curricularqa copy; never use the normal application", QA_PACKAGE, context.packageName)
        assertEquals("The compiled application ID must also be isolated", QA_PACKAGE, BuildConfig.APPLICATION_ID)
        assertEquals(QA_PACKAGE, context.applicationContext.packageName)
    }

    private fun recordActivityObservation(event: String, activity: LessonActivity) {
        if (activityObservations.size == 8) activityObservations.removeFirst()
        activityObservations.addLast("$event instance=${System.identityHashCode(activity)} " +
            "lesson=${activity.intent.getStringExtra("lessonId")}, " +
            "changing=${activity.isChangingConfigurations}, " +
            "changes=0x${activity.changingConfigurations.toString(16)}, " +
            "configuration=${activity.resources.configuration}")
    }

    private fun requireAwakeUnlockedQaDisplay() {
        requireQaPackage()
        val power = context.getSystemService(Context.POWER_SERVICE) as PowerManager
        if (!power.isInteractive) ParcelFileDescriptor.AutoCloseInputStream(
            instrumentation.uiAutomation.executeShellCommand("input keyevent KEYCODE_WAKEUP")
        ).use { it.readBytes() }
        val deadline = SystemClock.uptimeMillis() + 5_000L
        while (!power.isInteractive && SystemClock.uptimeMillis() < deadline) SystemClock.sleep(50L)
        assertTrue("The QA renderer requires an awake display", power.isInteractive)
        val keyguard = context.getSystemService(Context.KEYGUARD_SERVICE) as KeyguardManager
        assertFalse("Dismiss the visual lock screen manually; this test does not dismiss it", keyguard.isKeyguardLocked)
        assertFalse("Unlock device security manually; this test does not unlock it", keyguard.isDeviceLocked)
    }

    private fun showPage(activity: LessonActivity, page: Int) {
        invoke(activity, "saveCurrentPageState")
        LessonActivity::class.java.getDeclaredField("currentPage").apply { isAccessible = true }.set(activity, page)
        ProgressRepository.saveCurrentPage(context, ROUTE, field(activity, "currentLessonId") as String, page)
        invoke(activity, "renderCurrentPage")
    }

    private fun blockAt(lesson: JSONObject, page: Int, block: Int): JSONObject =
        lesson.getJSONArray("pages").getJSONObject(page - 1).getJSONArray("blocks").getJSONObject(block - 1)

    private fun scalar(block: JSONObject, field: String): String =
        if (field == "option:1") block.getJSONArray("options").getString(1) else block.getString(field)

    private fun field(activity: LessonActivity, name: String): Any? =
        LessonActivity::class.java.getDeclaredField(name).apply { isAccessible = true }.get(activity)

    private fun invoke(activity: LessonActivity, name: String) =
        LessonActivity::class.java.getDeclaredMethod(name).apply { isAccessible = true }.invoke(activity)

    private fun descendants(view: View): List<View> = buildList {
        add(view)
        if (view is ViewGroup) for (index in 0 until view.childCount) addAll(descendants(view.getChildAt(index)))
    }

    companion object {
        private const val QA_PACKAGE = "com.learngraphicsprogramming.app.curricularqa"
        private const val ROUTE = "programming"
        private const val COURSE = "cpp_fundamentals"
        private const val VOICE = "ef_dora"
        private val EXERCISE_TYPES = setOf("multiple_choice", "fill_code", "inline_fill_code",
            "output_prediction", "order_code", "drag_fill_code")
    }
}
