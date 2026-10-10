package com.learngraphicsprogramming.app.narration

import android.content.Context
import android.os.StatFs
import android.os.SystemClock
import android.util.Log
import androidx.work.BackoffPolicy
import androidx.work.Constraints
import androidx.work.ExistingWorkPolicy
import androidx.work.OneTimeWorkRequest
import androidx.work.WorkManager
import androidx.work.Worker
import androidx.work.WorkerParameters
import com.learngraphicsprogramming.app.narration.phonemes.EnglishMisakiG2P
import com.learngraphicsprogramming.app.narration.phonemes.FrenchFluidG2P
import com.learngraphicsprogramming.app.narration.phonemes.SpanishFluidG2P
import java.io.File
import java.util.concurrent.TimeUnit

/**
 * Resumable best-effort background pre-rendering. Never takes audio focus.
 *
 * Android limits ordinary WorkManager jobs. Render for up to four minutes
 * per job, then chain the next job; complete WAVs are persisted independently.
 * On interruption the next run scans the syllabus and resumes from its first
 * missing sentence. This also detects any selectively deleted audio.
 */
internal class KokoroBackgroundNarrationWorker(
    context: Context, parameters: WorkerParameters
) : Worker(context, parameters) {

    private val app = applicationContext
    @Volatile private var engine: KokoroOnnxEngine? = null

    override fun doWork(): Result {
        val settings = NarrationSettingsRepository.get(app)
        if (!settings.enabled || settings.engine == NarrationEngine.ANDROID) return Result.success()
        if (NarrationWorkPriority.isBusy) return Result.retry()

        // Never exhaust storage to fill a nonessential educational audio cache.
        val storage = StatFs(app.filesDir.absolutePath)
        if (storage.availableBytes < MIN_FREE_BYTES) {
            Log.w(TAG, "Offline narration paused: low storage")
            return Result.success()
        }

        val modelStore = KokoroModelStore(app)
        var english: EnglishMisakiG2P? = null
        var french: FrenchFluidG2P? = null
        var spanish: Map<String, String>? = null
        var root: File? = null
        var hadMissing = false
        val started = SystemClock.elapsedRealtime()
        val interruption = NarrationWorkPriority.onPreempt {
            engine?.cancelCurrentRun()
        }

        try {
            for (item in NarrationCurriculum.sentences(app)) {
                if (isStopped || NarrationWorkPriority.isBusy) return Result.retry()

                if (KokoroNarrationCache.lookup(
                        app, item.language.tag, item.voice, item.text
                    ) != null
                ) continue

                hadMissing = true
                if (StatFs(app.filesDir.absolutePath).availableBytes < MIN_FREE_BYTES) {
                    Log.w(TAG, "Offline narration paused before storage becomes exhausted")
                    return Result.success()
                }

                if (root == null) {
                    root = modelStore.directory()
                    if (!File(root, "model.onnx").isFile) return Result.success()
                }
                val modelRoot = requireNotNull(root)
                val voice = KokoroVoiceCatalog.find(item.voice)
                if (voice == null || !File(modelRoot, voice.artifact.fileName).isFile) {
                    // Another voice may be installed later; retry on the next app launch.
                    return Result.success()
                }

                // Two ORT/XNNPACK threads, no NNAPI GPU access in background.
                // Foreground playback still selects its fastest available provider.
                if (engine == null) {
                    engine = KokoroOnnxEngine(
                        modelRoot, preferAccelerator = false, cpuThreads = 2
                    )
                }

                val active = {
                    !isStopped && !NarrationWorkPriority.isBusy
                }
                val phonemes = when (item.language) {
                    NarrationLanguage.SPANISH -> {
                        if (spanish == null) {
                            val artifact = KokoroModelManifest.resources(item.language).single()
                            val file = File(modelRoot, artifact.fileName)
                            require(KokoroModelManifest.verify(file, artifact)) {
                                "No está instalado el diccionario español verificado"
                            }
                            spanish = SpanishFluidG2P.loadLexicon(file)
                        }
                        SpanishFluidG2P.phonemize(item.text, spanish!!)
                    }
                    NarrationLanguage.FRENCH ->
                        (french ?: FrenchFluidG2P(modelRoot, File(modelRoot, "tokenizer.json"))
                            .also { french = it }).phonemize(item.text)
                    else -> (english ?: EnglishMisakiG2P(modelRoot).also { english = it })
                        .phonemize(item.text, item.language == NarrationLanguage.BRITISH_ENGLISH, active)
                }
                if (!active()) return Result.retry()
                val parts = mutableListOf<FloatArray>()
                for (part in KokoroPhonemeChunks.split(phonemes)) {
                    if (!active()) return Result.retry()
                    parts += engine!!.synthesizePhonemes(part, item.voice, 1f) { !active() }
                }
                if (!active()) return Result.retry()

                val temporary = File(app.cacheDir, "kokoro-offline-${System.nanoTime()}.wav")
                try {
                    NarrationWavWriter.writeChunks(temporary, parts)
                    checkNotNull(KokoroNarrationCache.store(
                        app, item.language.tag, item.voice, item.text, temporary
                    )) { "No se pudo guardar el audio generado" }
                } finally { temporary.delete() }

                Log.i(TAG, "Cached lesson speech: ${item.location}")
                // Close work in a bounded slice so JobScheduler can
                // stop/restart cleanly without losing already rendered audio.
                if (SystemClock.elapsedRealtime() - started >= SLICE_MS) {
                    scheduleNext(app)
                    return Result.success()
                }
            }
            if (!hadMissing) Log.i(TAG, "All available lesson sentences are cached")
            return Result.success()
        } catch (error: Exception) {
            Log.w(TAG, "Offline narration interrupted or resources unavailable", error)
            return Result.retry()
        } finally {
            interruption.close()
            runCatching { engine?.close() }
            engine = null
            runCatching { english?.close() }
            runCatching { (french as? AutoCloseable)?.close() }
            modelStore.close()
        }
    }

    companion object {
        private const val TAG = "KokoroBackground"
        private const val WORK_NAME = "kokoro-offline-curriculum-v1"
        private const val SLICE_MS = 4L * 60 * 1000
        private const val MIN_FREE_BYTES = 1024L * 1024 * 1024

        private fun request() =
            OneTimeWorkRequest.Builder(KokoroBackgroundNarrationWorker::class.java)
                .setConstraints(
                    Constraints.Builder().setRequiresBatteryNotLow(true).build()
                )
                .setBackoffCriteria(BackoffPolicy.LINEAR, 20, TimeUnit.SECONDS)
                .build()

        fun start(context: Context) {
            WorkManager.getInstance(context.applicationContext).enqueueUniqueWork(
                WORK_NAME, ExistingWorkPolicy.KEEP, request()
            )
        }

        private fun scheduleNext(context: Context) {
            WorkManager.getInstance(context).beginUniqueWork(
                WORK_NAME, ExistingWorkPolicy.APPEND_OR_REPLACE, request()
            ).enqueue()
        }
    }
}
