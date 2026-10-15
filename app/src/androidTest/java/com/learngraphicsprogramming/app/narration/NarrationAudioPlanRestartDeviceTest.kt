package com.learngraphicsprogramming.app.narration

import android.app.Application
import android.content.Context
import android.content.SharedPreferences
import android.os.Process
import android.system.Os
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.AppearanceSettingsRepository
import com.learngraphicsprogramming.app.BuildConfig
import java.io.File
import java.security.MessageDigest
import java.util.UUID
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Run seed, stop the QA process externally, then resume with the same UUID in a new process. */
@RunWith(AndroidJUnit4::class)
class NarrationAudioPlanRestartDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val actual = instrumentation.targetContext

    @Test fun realProcessRestartPreservesPlannerAndCachedAudio() {
        assertEquals("Use only the isolated QA application on LDPlayer", QA_PACKAGE, actual.packageName)
        assertEquals(QA_PACKAGE, BuildConfig.APPLICATION_ID)
        val arguments = InstrumentationRegistry.getArguments()
        val phase = arguments.getString("restartPhase")
        assumeTrue("Requiere protocolo seed/resume en dos procesos", arguments.containsKey("restartPhase"))
        assertTrue("Pass -e restartPhase seed or resume", phase == "seed" || phase == "resume")
        val rawId = requireNotNull(arguments.getString("restartId")) { "Pass the same restartId UUID to both phases" }
        val id = UUID.fromString(rawId).toString()
        assertEquals("Use a canonical UUID so the sandbox path cannot escape the QA cache", id, rawId)
        val root = File(actual.cacheDir, "audio-plan-restart-$id").canonicalFile
        assertEquals(actual.cacheDir.canonicalFile, root.parentFile)
        if (phase == "seed") assertTrue("A seed must use a fresh, test-owned restart UUID", root.mkdirs())
        else assertTrue("The process restart must preserve the original private test files", root.isDirectory)
        val isolated = RestartApplication(actual.applicationContext as Application, root, id)
        val evidenceDirectory = requireNotNull(actual.getExternalFilesDir("cpu-planning-qa")).apply { mkdirs() }
        val evidenceFile = File(evidenceDirectory, "restart-$id.json")
        val pages = listOf(
            page(AppearanceSettingsRepository.ROUTE_PROGRAMMING, "La libreta violeta permanece abierta."),
            page(AppearanceSettingsRepository.ROUTE_MATH_PHYSICS, "La ventana naranja guarda silencio.")
        )
        val catalog: (String, String?) -> Sequence<NarrationCurriculum.Page> = { route, course ->
            pages.asSequence().filter { it.routeId == route && (course == null || it.courseId == course) }
        }
        val worker = KokoroBackgroundNarrationWorker(isolated, catalog)
        try {
            assertFalse("A new instrumentation process must have no interactive narration lease", NarrationWorkPriority.isBusy)
            if (phase == "seed") {
                NarrationSettingsRepository.saveEnabled(isolated, true)
                NarrationSettingsRepository.saveEngine(isolated, NarrationEngine.KOKORO)
                NarrationSettingsRepository.saveLanguage(isolated, NarrationLanguage.SPANISH)
                NarrationSettingsRepository.saveKokoroVoice(isolated, VOICE)
                NarrationSettingsRepository.saveRate(isolated, 1f)
                NarrationStorageBudget.saveLimit(isolated, 64L * NarrationStorageBudget.MIB)
                val modelStore = KokoroModelStore(isolated)
                val models = try { modelStore.directory() } finally { modelStore.close() }
                assertTrue("Existing model resources are required; never download test resources", File(models, "model.onnx").isFile)
                assertTrue(File(models, "$VOICE.bin").isFile)
                assertEquals(KokoroBackgroundNarrationWorker.Outcome.COMPLETE, worker.runSlice { true })
                val expectedHashes = pages.map { NarrationPageQueue.hash(it.sentences.single()) }
                assertEquals(expectedHashes, generationHashes(isolated))
                val snapshot = NarrationAudioPlanner.store(isolated).snapshot()
                assertEquals(2, snapshot.cursors.size)
                assertNull(snapshot.context.routeId)
                val storeFile = File(isolated.filesDir, NarrationAudioPlanner.FILE_NAME)
                assertTrue(storeFile.isFile)
                val recordings = JSONArray()
                pages.forEach { page -> recordings.put(recording(isolated, page)) }
                // Flush only our prefixed settings, so resume genuinely obtains them from disk.
                isolated.preferenceNames.toList().forEach { name ->
                    assertTrue(actual.getSharedPreferences(name, Context.MODE_PRIVATE).edit().commit())
                }
                val data = JSONObject().put("testOwnedFixtures", true).put("realInstalledKokoro", true)
                    .put("restartId", id).put("seedPid", Process.myPid()).put("seedPlan", JSONObject(snapshot.encode()))
                    .put("seedPlanSha256", sha256(storeFile)).put("seedGenerationHashes", JSONArray(expectedHashes))
                    .put("seedGenerationCount", expectedHashes.size).put("recordings", recordings)
                    .put("seedPreferences", JSONArray(isolated.preferenceNames.toList()))
                KokoroPagePreparationQueue.writeAtomic(evidenceFile, data.toString(2))
            } else {
                assertTrue("The seed phase must have saved external evidence", evidenceFile.isFile)
                val data = JSONObject(evidenceFile.readText())
                assertEquals(id, data.getString("restartId"))
                assertNotEquals("This phase must run after an actual process termination and new instrumentation launch",
                    data.getInt("seedPid"), Process.myPid())
                val settings = NarrationSettingsRepository.get(isolated)
                assertTrue("Settings must be read from the previous process, without reseeding them", settings.enabled)
                assertEquals(NarrationEngine.KOKORO, settings.engine)
                assertEquals(VOICE, settings.kokoroVoice)
                val plan = NarrationAudioPlanner.store(isolated)
                val savedPlan = NarrationAudioPlan(data.getJSONObject("seedPlan").toString()).snapshot()
                assertEquals("A new process must restore the AtomicFile metadata", savedPlan, plan.snapshot())
                val storeFile = File(isolated.filesDir, NarrationAudioPlanner.FILE_NAME)
                assertEquals(data.getString("seedPlanSha256"), sha256(storeFile))
                assertRecordingsUnchanged(isolated, pages, data.getJSONArray("recordings"))
                val originalEvents = generationHashes(isolated)
                assertEquals(data.getInt("seedGenerationCount"), originalEvents.size)
                assertEquals(KokoroBackgroundNarrationWorker.Outcome.COMPLETE, worker.runSlice { true })
                assertEquals("Opening the app again must not synthesize cached audio", originalEvents, generationHashes(isolated))
                assertEquals("A cache-only scan must not rewrite the scheduler", data.getString("seedPlanSha256"), sha256(storeFile))
                assertEquals(savedPlan, plan.snapshot())
                assertRecordingsUnchanged(isolated, pages, data.getJSONArray("recordings"))

                val math = pages.last()
                val selected = plan.enter(math.routeId, math.courseId)
                assertTrue(selected.epoch > savedPlan.epoch)
                val preferred = plan.candidates(null, catalog).first()
                assertEquals(NarrationAudioPriority.COURSE, preferred.priority)
                assertEquals(math.identity, preferred.page.identity)
                assertEquals(KokoroBackgroundNarrationWorker.Outcome.COMPLETE, worker.runSlice { true })
                assertEquals("Entering a previously prepared course also reuses the original files", originalEvents, generationHashes(isolated))
                assertRecordingsUnchanged(isolated, pages, data.getJSONArray("recordings"))
                data.put("resumePid", Process.myPid()).put("actualProcessRestartVerified", true)
                    .put("restoredPlanMatches", true).put("cacheOnlySchedulerWrites", 0)
                    .put("regeneratedAfterRestart", 0).put("audioBytesAndInodesUnchanged", true)
                    .put("courseAfterRestart", math.courseId).put("finalPlan", JSONObject(plan.snapshot().encode()))
                KokoroPagePreparationQueue.writeAtomic(evidenceFile, data.toString(2))
            }
        } finally {
            worker.cancel()
            KokoroEngineCoordinator.releaseIfIdle()
            if (phase == "resume") {
                forgetStore(isolated)
                val prefix = isolated.preferencePrefix
                val names = File(actual.applicationInfo.dataDir, "shared_prefs").listFiles().orEmpty()
                    .filter { it.name.startsWith(prefix) && (it.name.endsWith(".xml") || it.name.endsWith(".xml.bak")) }
                    .map { it.name.removeSuffix(".bak").removeSuffix(".xml") }.toSet() + isolated.preferenceNames
                names.forEach { name ->
                    assertTrue("Only this restart test's preferences may be removed", name.startsWith(prefix))
                    actual.deleteSharedPreferences(name)
                }
                assertEquals(actual.cacheDir.canonicalFile, root.parentFile)
                assertTrue("Remove only the resumed test's private sandbox after all native handles close", root.deleteRecursively())
            }
        }
    }

    private fun page(route: String, text: String): NarrationCurriculum.Page {
        val course = "qa_${route}_course"
        val unit = "${course}__unit1"
        val lesson = "${unit}__lesson1"
        return NarrationCurriculum.Page(route, course, unit, lesson, 0,
            listOf(NarrationCurriculum.Sentence(text, NarrationLanguage.SPANISH, VOICE, "$route/$course/$unit/$lesson/1")))
    }
    private fun recording(context: Context, page: NarrationCurriculum.Page): JSONObject {
        val sentence = page.sentences.single()
        val audio = requireNotNull(KokoroNarrationCache.lookup(context, sentence.language.tag, sentence.voice, sentence.text))
        assertTrue("This recording must contain the real generated waveform", audio.length() > 44L)
        val stat = Os.stat(audio.absolutePath)
        return JSONObject().put("page", page.identity).put("audioFile", audio.name).put("sha256", sha256(audio))
            .put("length", audio.length()).put("device", stat.st_dev).put("inode", stat.st_ino)
    }
    private fun assertRecordingsUnchanged(context: Context, pages: List<NarrationCurriculum.Page>, before: JSONArray) {
        assertEquals(pages.size, before.length())
        pages.forEachIndexed { index, page ->
            val original = before.getJSONObject(index)
            val current = recording(context, page)
            listOf("page", "audioFile", "sha256").forEach { field ->
                assertEquals("The exact original cached recording survives the process restart: $field",
                    original.getString(field), current.getString(field))
            }
            listOf("length", "device", "inode").forEach { field ->
                assertEquals("The original recording identity survives JSON integer representation changes: $field",
                    original.getLong(field), current.getLong(field))
            }
        }
    }
    private fun generationHashes(context: Context): List<String> = File(context.filesDir, KokoroNarrationDiagnostics.EVENTS_FILE)
        .takeIf { it.isFile }?.readLines()?.mapNotNull { runCatching { JSONObject(it) }.getOrNull() }
        ?.filter { it.optString("event") == "Cached lesson speech" }?.map { it.getString("hash") }.orEmpty()
    private fun sha256(file: File): String = MessageDigest.getInstance("SHA-256").digest(file.readBytes())
        .joinToString("") { "%02x".format(it.toInt() and 255) }
    @Suppress("UNCHECKED_CAST") private fun forgetStore(context: Context) {
        val field = NarrationAudioPlanner.javaClass.getDeclaredField("stores").apply { isAccessible = true }
        val stores = field.get(NarrationAudioPlanner) as MutableMap<File, NarrationAudioPlan>
        synchronized(NarrationAudioPlanner) { stores.remove(File(context.filesDir, NarrationAudioPlanner.FILE_NAME).canonicalFile) }
    }
    private class RestartApplication(private val actual: Application, root: File, id: String) : Application() {
        private val files = File(root, "files").apply { check(isDirectory || mkdirs()) }
        private val cache = File(root, "cache").apply { check(isDirectory || mkdirs()) }
        val preferencePrefix = "audio_plan_restart_${id}_"
        val preferenceNames = java.util.Collections.synchronizedSet(mutableSetOf<String>())
        init { attachBaseContext(actual) }
        override fun getApplicationContext(): Context = this
        override fun getFilesDir(): File = files
        override fun getCacheDir(): File = cache
        override fun getSharedPreferences(name: String, mode: Int): SharedPreferences {
            val scoped = preferencePrefix + name
            preferenceNames += scoped
            return actual.getSharedPreferences(scoped, mode)
        }
    }
    companion object {
        private const val QA_PACKAGE = "com.learngraphicsprogramming.app.curricularqa"
        private const val VOICE = "ef_dora"
    }
}
