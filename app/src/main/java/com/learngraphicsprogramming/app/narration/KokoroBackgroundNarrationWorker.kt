package com.learngraphicsprogramming.app.narration

import android.content.Context
import android.os.SystemClock
import android.util.Log
import com.learngraphicsprogramming.app.narration.phonemes.EnglishMisakiG2P
import com.learngraphicsprogramming.app.narration.phonemes.FrenchFluidG2P
import com.learngraphicsprogramming.app.narration.phonemes.SpanishFluidG2P
import java.io.File

/**
 * Resumable offline synthesis controlled by a session-scoped foreground
 * service. One bounded pass resumes from the first missing curriculum sentence.
 * Bundled Opus/WAV audio and previously generated WAVs are always reused.
 * Playback, compiler and graphics have priority; no audio focus is taken.
 */
internal class KokoroBackgroundNarrationWorker(private val app: Context) {
    enum class Outcome { COMPLETE, MORE, BUSY, PAUSED, UNAVAILABLE, RETRY }
    @Volatile private var engine: KokoroOnnxEngine? = null

    /** Called on the service's private thread, with a bounded CPU wake lock. */
    fun runSlice(activeSession: () -> Boolean): Outcome {
        val settings = NarrationSettingsRepository.get(app)
        if (!settings.enabled || settings.engine == NarrationEngine.ANDROID) return Outcome.PAUSED
        if (!activeSession() || NarrationWorkPriority.isBusy) return Outcome.BUSY

        // Respect the user's chosen storage budget and ALWAYS leave 1 GiB
        // physically free. Existing audio is never evicted.
        if (!NarrationStorageBudget.fitsNext(
                NarrationStorageBudget.snapshot(app), MIN_SENTENCE_HEADROOM_BYTES
            )
        ) {
            Log.i(TAG, "Offline narration paused: storage limit or reserve reached")
            return Outcome.PAUSED
        }

        val modelStore = KokoroModelStore(app)
        var english: EnglishMisakiG2P? = null
        var french: FrenchFluidG2P? = null
        var spanish: Map<String, String>? = null
        var root: File? = null
        var hadMissing = false
        var skippedUninstalledVoices = false
        val started = SystemClock.elapsedRealtime()
        val interruption = NarrationWorkPriority.onPreempt {
            engine?.cancelCurrentRun()
        }

        try {
            for (item in NarrationCurriculum.sentences(app)) {
                if (!activeSession() || NarrationWorkPriority.isBusy) return Outcome.BUSY

                if (KokoroNarrationCache.lookup(
                        app, item.language.tag, item.voice, item.text
                    ) != null
                ) continue

                hadMissing = true
                if (!NarrationStorageBudget.fitsNext(
                        NarrationStorageBudget.snapshot(app), MIN_SENTENCE_HEADROOM_BYTES
                    )
                ) {
                    Log.i(TAG, "Offline narration paused: user-selected storage budget reached")
                    return Outcome.PAUSED
                }

                if (root == null) {
                    root = modelStore.directory()
                    if (!File(root, "model.onnx").isFile) return Outcome.UNAVAILABLE
                }
                val modelRoot = requireNotNull(root)
                val voice = KokoroVoiceCatalog.find(item.voice)
                if (voice == null || !File(modelRoot, voice.artifact.fileName).isFile) {
                    // Missing voices do not prevent generating other installed languages.
                    skippedUninstalledVoices = true
                    continue
                }

                // Two ORT/XNNPACK threads, no NNAPI GPU access in background.
                // Foreground playback still selects its fastest available provider.
                if (engine == null) {
                    engine = KokoroOnnxEngine(
                        modelRoot, preferAccelerator = false, cpuThreads = 2
                    )
                }

                val active = {
                    activeSession() && !NarrationWorkPriority.isBusy
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
                if (!active()) return Outcome.BUSY
                val parts = mutableListOf<FloatArray>()
                for (part in KokoroPhonemeChunks.split(phonemes)) {
                    if (!active()) return Outcome.BUSY
                    parts += engine!!.synthesizePhonemes(part, item.voice, 1f) { !active() }
                }
                if (!active()) return Outcome.BUSY

                val temporary = File(app.cacheDir, "kokoro-offline-${System.nanoTime()}.wav")
                try {
                    NarrationWavWriter.writeChunks(temporary, parts)
                    // Budget may have changed during a long ONNX inference.
                    // Do not add a new persistent file above the selected limit.
                    if (!NarrationStorageBudget.fitsNext(
                            NarrationStorageBudget.snapshot(app), temporary.length()
                        )
                    ) {
                        Log.i(TAG, "Offline narration suspended before committing the new WAV")
                        return Outcome.PAUSED
                    }
                    checkNotNull(KokoroNarrationCache.store(
                        app, item.language.tag, item.voice, item.text, temporary
                    )) { "No se pudo guardar el audio generado" }
                } finally { temporary.delete() }

                Log.i(TAG, "Cached lesson speech: ${item.location}")
                // Release the synthesis engine periodically while keeping all finished WAVs.
                if (SystemClock.elapsedRealtime() - started >= SLICE_MS) {
                    return Outcome.MORE
                }
            }
            if (skippedUninstalledVoices) {
                Log.i(TAG, "Offline narration pending: some voices are not installed")
                return Outcome.UNAVAILABLE
            }
            if (!hadMissing) Log.i(TAG, "All available lesson sentences are cached")
            return Outcome.COMPLETE
        } catch (_: InterruptedException) {
            return Outcome.BUSY
        } catch (error: Exception) {
            Log.w(TAG, "Offline narration interrupted or resources unavailable", error)
            return Outcome.RETRY
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
        private const val SLICE_MS = 4L * 60 * 1000
        private const val MIN_SENTENCE_HEADROOM_BYTES = 4L * NarrationStorageBudget.MIB

        /** Retains call sites while the lifecycle manager owns scheduling. */
        fun start(context: Context) {
            KokoroNarrationSessionMonitor.refresh(context)
        }
    }
}
