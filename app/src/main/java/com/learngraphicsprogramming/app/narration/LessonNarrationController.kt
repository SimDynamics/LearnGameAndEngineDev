package com.learngraphicsprogramming.app.narration

import android.content.Context
import com.learngraphicsprogramming.app.R
import com.learngraphicsprogramming.app.UiLanguageSettings
import android.media.AudioAttributes
import android.media.AudioFocusRequest
import android.media.AudioManager
import android.media.MediaMetadataRetriever
import android.media.MediaPlayer
import android.media.PlaybackParams
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.HandlerThread
import android.os.Looper
import android.os.Process
import android.os.PowerManager
import android.speech.tts.TextToSpeech
import android.speech.tts.UtteranceProgressListener
import android.speech.tts.Voice
import com.learngraphicsprogramming.app.narration.phonemes.EnglishMisakiG2P
import com.learngraphicsprogramming.app.narration.phonemes.FrenchFluidG2P
import com.learngraphicsprogramming.app.narration.phonemes.SpanishFluidG2P
import java.io.File
import java.util.UUID

/** TTS and Kokoro synthesize temporary files; one player supplies pause, seek and speed. */
class LessonNarrationController(
    context: Context,
    private val onState: (Boolean) -> Unit,
    private val onError: (String) -> Unit
) {
    private val uiContext: Context get() = UiLanguageSettings.localizedContext(appContext)
    private fun s(id: Int, vararg args: Any): String = uiContext.getString(id, *args)
    private class UiFailure(val messageRes: Int, vararg val arguments: Any) : IllegalStateException()
    private fun errorMessage(error: Exception, fallback: Int) =
        if (error is UiFailure) s(error.messageRes, *error.arguments) else s(fallback)
    private val appContext = context.applicationContext
    private val main = Handler(Looper.getMainLooper())
    private val workerThread = HandlerThread(
        "LessonNarration",
        Process.THREAD_PRIORITY_FOREGROUND
    ).apply { start() }
    private val worker = Handler(workerThread.looper)
    private val models = KokoroModelStore(appContext)
    private val audioManager = appContext.getSystemService(AudioManager::class.java)
    private val synthesisWakeLock = appContext.getSystemService(PowerManager::class.java)
        ?.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "LearnGraphicsProgramming:KokoroSynthesis")
        ?.apply { setReferenceCounted(false) }

    // MediaPlayer holds its own partial lock while playing. The synthesis lock
    // keeps the CPU awake only while a neural/audio file is being generated.
    private fun holdSynthesisCpu() {
        if (synthesisWakeLock?.isHeld != true) {
            synthesisWakeLock?.acquire(210_000L)
        }
    }
    private fun releaseSynthesisCpu() {
        if (synthesisWakeLock?.isHeld == true) synthesisWakeLock.release()
    }
    private val attributes = AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA)
        .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH).build()
    private val focusListener = AudioManager.OnAudioFocusChangeListener { change ->
        if (change < 0) onMain { pause() }
    }
    private val focusRequest = if (Build.VERSION.SDK_INT >= 26) {
        AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN_TRANSIENT_MAY_DUCK)
            .setAudioAttributes(attributes).setOnAudioFocusChangeListener(focusListener, main)
            .setAcceptsDelayedFocusGain(false).build()
    } else null

    @Volatile private var closed = false
    @Volatile private var generation = 0L
    @Volatile private var configuration = 0L
    @Volatile private var playing = false
    private var language = NarrationSettingsRepository.get(appContext).language
    private var settings = NarrationSettingsRepository.forLanguage(appContext, language)
    private var directory: File? = null
    private var segments = mutableListOf<Segment>()
    private var index = 0
    private var offset = 0L
    private var finished = false
    private var player: MediaPlayer? = null
    private var playerReady = false
    private var focusHeld = false
    private val pending = mutableSetOf<Int>()
    private val watchdogs = mutableMapOf<String, Runnable>()
    private var reportedFallback = false
    private var warnedNetwork = false

    // Construction, inference and close belong to the worker; main may request cancellation.
    @Volatile private var kokoro: KokoroOnnxEngine? = null
    @Volatile private var english: EnglishMisakiG2P? = null
    private var french: FrenchFluidG2P? = null
    private var spanishLexicon: Map<String, String>? = null
    private var tts: TextToSpeech? = null
    private var ttsReady = false
    private var ttsInitializing = false
    private var ttsGeneration = 0L
    private var systemEngine: String? = null
    private var voices: List<Voice> = emptyList()
    private val ttsPending = linkedMapOf<String, Synthesis>()
    private val removeResourceListener = NarrationResourceEvents.addListener {
        if (!closed) stopInternal(releaseNeuralResources = true)
    }

    private data class Segment(val text: String, var file: File? = null, var duration: Int? = null)
    private data class Synthesis(
        val generation: Long, val configuration: Long, val index: Int, val text: String,
        val file: File, val settings: NarrationSettings
    ) {
        val id = "narration_" + generation + "_" + configuration + "_" + index
    }

    val isPlaying: Boolean get() = playing
    val hasNarration: Boolean get() = segments.isNotEmpty()

    fun toggle(
        segments: List<String>,
        language: NarrationLanguage = NarrationSettingsRepository.get(appContext).language
    ) = onMain {
        if (closed) return@onMain
        val selectedSettings = NarrationSettingsRepository.forLanguage(appContext, language)
        val prepared = narrationParts(segments, selectedSettings)
        if (this.segments.isNotEmpty() && this.language == language &&
            this.segments.map { it.text } == prepared
        ) {
            if (playing) pause() else resume()
            return@onMain
        }
        if (!startPage(prepared, selectedSettings)) return@onMain
        resume()
    }

    private fun narrationParts(texts: List<String>, selected: NarrationSettings): List<String> =
        if (selected.engine == NarrationEngine.ANDROID) NarrationSegments.prepare(texts, 250)
        else NarrationSegments.sentences(texts)

    private fun startPage(prepared: List<String>, selected: NarrationSettings): Boolean {
        stopInternal()
        settings = selected
        if (!settings.enabled) return false
        if (prepared.isEmpty()) {
            onError(s(R.string.narration_no_text))
            return false
        }
        language = selected.language
        segments = prepared.map { Segment(it) }.toMutableList()
        directory = File(appContext.cacheDir, "lesson-narration-" + UUID.randomUUID())
        return true
    }

    /**
     * Start synthesis when a page is OPENED, without playing audio or taking
     * audio focus. Replay uses the same cached WAV files; no new inference.
     */
    fun prefetchPage(
        texts: List<String>,
        pageLanguage: NarrationLanguage
    ) = onMain {
        if (closed) return@onMain
        val selected = NarrationSettingsRepository.forLanguage(appContext, pageLanguage)
        if (!selected.enabled || selected.engine == NarrationEngine.ANDROID) return@onMain
        // AUTO should not silently launch Android TTS just because a page
        // appeared: passive preparation is only for installed Kokoro files.
        val installed = runCatching {
            val root = models.directory()
            File(root, "model.onnx").isFile &&
                (KokoroVoiceCatalog.find(selected.kokoroVoice)?.let { voice ->
                    File(root, voice.artifact.fileName).isFile
                } == true)
        }.getOrDefault(false)
        if (!installed) return@onMain
        val prepared = narrationParts(texts, selected)
        if (prepared.isEmpty()) return@onMain
        if (language == pageLanguage && segments.map { it.text } == prepared) return@onMain
        if (!startPage(prepared, selected)) return@onMain
        ensureSegment(0)
        ensureSegment(1)
    }

    /**
     * Opens an already-installed neural model in the background while the lesson
     * is visible. Never downloads files and never blocks the lesson UI thread.
     * Sessions are reused across page changes and released on activity destroy.
     */
    fun warmUp() {
        if (closed || !NarrationSettingsRepository.get(appContext).enabled) return
        worker.post {
            if (closed || kokoro != null) return@post
            val preference = NarrationSettingsRepository.get(appContext)
            if (!preference.enabled || preference.engine == NarrationEngine.ANDROID) return@post
            runCatching {
                val root = models.directory()
                val voice = KokoroVoiceCatalog.find(preference.kokoroVoice) ?: return@post
                if (!File(root, "model.onnx").isFile ||
                    !File(root, voice.artifact.fileName).isFile
                ) return@post
                val engine = KokoroOnnxEngine(root)
                if (closed) engine.close() else kokoro = engine
            }
            // Errors are reported through the existing fallback when Listen
            // is pressed, not during passive lesson loading.
        }
    }

    private fun pause() {
        if (!playing) return
        playing = false
        if (playerReady) runCatching { player?.pause() }
        releaseFocus()
        onState(false)
    }

    private fun resume() {
        if (closed || segments.isEmpty() || !NarrationSettingsRepository.get(appContext).enabled) return
        if (!acquireFocus()) {
            onError(s(R.string.narration_focus_error))
            return
        }
        playing = true
        onState(true)
        if (finished) {
            finished = false
            selectSegment(0, 0)
        } else if (playerReady) {
            applyRate()
            runCatching { player?.start() }.onFailure { fail(s(R.string.narration_resume_error)) }
        } else ensureSegment(index)
    }

    fun stop() = onMain { stopInternal() }

    fun restart() = onMain {
        if (closed || segments.isEmpty()) return@onMain
        finished = false
        selectSegment(0, 0)
        if (!playing) resume()
    }

    /** Offsets are measured in source audio seconds, independent of playback speed. */
    fun seekBy(seconds: Int) = onMain {
        if (closed || segments.isEmpty()) return@onMain
        val position = if (playerReady) runCatching { player?.currentPosition?.toLong() ?: offset }.getOrDefault(offset) else offset
        var target = position + seconds.toLong() * 1_000L
        var targetIndex = index
        var previousDurationUnknown = false
        while (target < 0L && targetIndex > 0) {
            targetIndex--
            val duration = segments[targetIndex].duration
            if (duration == null) { previousDurationUnknown = true; break }
            target += duration
        }
        finished = false
        // A negative offset means "from the previous segment's end" only if that
        // duration is still unknown. At the beginning of the page it must clamp to zero.
        selectSegment(targetIndex, if (previousDurationUnknown) target else target.coerceAtLeast(0L))
    }

    fun settingsChanged() = onMain {
        if (closed) return@onMain
        val previous = settings
        settings = NarrationSettingsRepository.forLanguage(appContext, language)
        if (!settings.enabled) {
            stopInternal(releaseNeuralResources = true)
            return@onMain
        }
        if (settings.engine != previous.engine || settings.kokoroVoice != previous.kokoroVoice ||
            settings.androidVoice != previous.androidVoice
        ) {
            configuration++
            kokoro?.cancelCurrentRun()
            english?.cancelCurrentRun()
            pending.clear()
            releaseSynthesisCpu()
            watchdogs.values.forEach(main::removeCallbacks)
            watchdogs.clear()
            segments.forEachIndexed { current, segment ->
                if (current != index || player == null) {
                    val previousFile = segment.file
                    segment.file = null
                    segment.duration = null
                    if (previousFile != null) worker.post { previousFile.delete() }
                }
            }
            if (segments.isNotEmpty() && player == null) ensureSegment(index)
        }
        applyRate()
        worker.post {
            if (tts != null && systemEngine != defaultAndroidTtsEngine(appContext)) {
                ttsGeneration++
                runCatching { tts?.shutdown() }
                tts = null; ttsReady = false; ttsInitializing = false; voices = emptyList()
                failTtsPending(s(R.string.narration_provider_changed))
            }
            if (settings.engine == NarrationEngine.ANDROID) releaseKokoro()
        }
    }

    private fun selectSegment(next: Int, position: Long) {
        releasePlayer()
        index = next.coerceIn(0, segments.lastIndex)
        offset = position
        ensureSegment(index)
    }

    private fun ensureSegment(next: Int) {
        if (closed || next !in segments.indices) return
        val segment = segments[next]
        if (segment.file?.isFile == true) {
            if (next == index && player == null) preparePlayer(segment)
            return
        }
        if (!pending.add(next)) return
        holdSynthesisCpu()
        val session = directory ?: return
        val request = Synthesis(generation, configuration, next, segment.text,
            File(session, configuration.toString() + "_" + next + ".wav"),
            NarrationSettingsRepository.forLanguage(appContext, language))
        val timeout = Runnable {
            if (isCurrent(request) && pending.contains(next)) fail(s(R.string.narration_engine_timeout))
        }
        watchdogs[request.id] = timeout
        main.postDelayed(timeout, 180_000L)
        worker.post { synthesize(request) }
    }

    private fun synthesize(request: Synthesis) {
        if (!isCurrent(request)) return
        try {
            val parent = request.file.parentFile ?: error("Directorio temporal no disponible")
            if (!parent.isDirectory && !parent.mkdirs()) error("No se pudo crear el audio temporal")
            if (request.settings.engine != NarrationEngine.ANDROID) {
                try {
                    val root = models.directory()
                    if (!File(root, "model.onnx").isFile) throw UiFailure(R.string.narration_model_missing)
                    val voice = KokoroVoiceCatalog.find(request.settings.kokoroVoice)
                        ?: throw UiFailure(R.string.narration_voice_invalid)
                    if (!File(root, voice.artifact.fileName).isFile) throw UiFailure(R.string.narration_voice_missing)
                    if (!Build.SUPPORTED_ABIS.contains("arm64-v8a")) throw UiFailure(R.string.narration_requires_arm64)
                    val synthesisStarted = android.os.SystemClock.elapsedRealtime()
                    val phonemes = phonemize(request.text, request.settings.language, root) { isCurrent(request) }
                    if (!isCurrent(request)) return
                    var engine = kokoro ?: KokoroOnnxEngine(root).also { kokoro = it }
                    if (!isCurrent(request)) return
                    // All pieces become ONE seamless WAV for the current
                    // sentence. The next sentence is generated in advance.
                    val phonemeParts = KokoroPhonemeChunks.split(phonemes)
                    fun renderWith(active: KokoroOnnxEngine): List<FloatArray> =
                        phonemeParts.map { part ->
                            if (!isCurrent(request)) throw InterruptedException("Se canceló la narración")
                            active.synthesizePhonemes(part, request.settings.kokoroVoice, 1f) {
                                !isCurrent(request)
                            }
                        }
                    val audioParts = try {
                        renderWith(engine)
                    } catch (error: Exception) {
                        if (error is InterruptedException || !engine.usesNnapi || !isCurrent(request)) throw error
                        // Some devices accept NNAPI session creation but reject
                        // a Kokoro operator at inference time. Retry the whole
                        // sentence with multithreaded CPU, never losing words.
                        android.util.Log.w("KokoroInference", "NNAPI run failed; reverting to CPU", error)
                        releaseKokoro()
                        engine = KokoroOnnxEngine(root, preferAccelerator = false).also { kokoro = it }
                        renderWith(engine)
                    }
                    if (!isCurrent(request)) return
                    NarrationWavWriter.writeChunks(request.file, audioParts)
                    android.util.Log.i(
                        "KokoroInference",
                        "Sentence ${request.index}: ${request.text.length} chars, " +
                            "${phonemes.length} phonemes, ${audioParts.size} runs, " +
                            "${android.os.SystemClock.elapsedRealtime() - synthesisStarted}ms"
                    )
                    generated(request)
                    return
                } catch (_: InterruptedException) {
                    // Cancellation invalidated the page/configuration before aborting its G2P.
                    return
                } catch (error: Exception) {
                    if (!isCurrent(request)) return
                    reportFallback(request, errorMessage(error, R.string.narration_neural_unavailable))
                } catch (_: UnsatisfiedLinkError) {
                    if (!isCurrent(request)) return
                    reportFallback(request, s(R.string.narration_onnx_unavailable))
                }
            }
            if (!isCurrent(request)) return
            ttsPending[request.id] = request
            if (ttsReady) synthesizeAndroid(request) else initializeTts()
        } catch (_: Exception) {
            synthesisFailed(request, s(R.string.narration_temporary_audio_error))
        }
    }

    private fun phonemize(text: String, language: NarrationLanguage, root: File, isCurrent: () -> Boolean): String = when (language) {
        NarrationLanguage.SPANISH -> {
            val lexicon = spanishLexicon ?: run {
                val artifact = KokoroModelManifest.resources(language).single()
                val file = File(root, artifact.fileName)
                require(KokoroModelManifest.verify(file, artifact)) { "Falta el diccionario español verificado" }
                SpanishFluidG2P.loadLexicon(file).also { spanishLexicon = it }
            }
            SpanishFluidG2P.phonemize(text, lexicon)
        }
        NarrationLanguage.AMERICAN_ENGLISH, NarrationLanguage.BRITISH_ENGLISH ->
            (english ?: EnglishMisakiG2P(root).also { english = it })
                .phonemize(text, language == NarrationLanguage.BRITISH_ENGLISH, isCurrent)
        NarrationLanguage.FRENCH ->
            (french ?: FrenchFluidG2P(root, File(root, "tokenizer.json")).also { french = it }).phonemize(text)
    }

    private fun initializeTts() {
        if (ttsInitializing || closed) return
        ttsInitializing = true
        systemEngine = defaultAndroidTtsEngine(appContext)
        val initialization = ++ttsGeneration
        try {
            tts = TextToSpeech(appContext) { status ->
                worker.post {
                    if (closed || initialization != ttsGeneration) return@post
                    ttsInitializing = false
                    if (status != TextToSpeech.SUCCESS) {
                        failTtsPending(s(R.string.narration_android_configure))
                        runCatching { tts?.shutdown() }; tts = null
                        return@post
                    }
                    try {
                        val engine = tts ?: error("Motor de voz no disponible")
                        engine.setOnUtteranceProgressListener(ttsListener())
                        voices = engine.voices.orEmpty().toList()
                        ttsReady = true
                        ttsPending.values.toList().forEach(::synthesizeAndroid)
                    } catch (_: RuntimeException) {
                        failTtsPending(s(R.string.narration_android_configuration_error))
                    }
                }
            }
        } catch (_: RuntimeException) {
            ttsInitializing = false
            failTtsPending(s(R.string.narration_android_open_error))
        }
    }

    private fun synthesizeAndroid(request: Synthesis) {
        if (!isCurrent(request)) { ttsPending.remove(request.id); return }
        try {
            val engine = tts ?: error("Motor de voz no disponible")
            val available = SpanishVoices.available(voices.toSet(), request.settings.language)
            val preferred = available.firstOrNull { it.name == request.settings.androidVoice }
            val automatic = available.filter { it != preferred && !it.isNetworkConnectionRequired }
            val selected = (listOfNotNull(preferred) + automatic)
                .firstOrNull { engine.setVoice(it) == TextToSpeech.SUCCESS }
                ?: throw UiFailure(R.string.narration_offline_voice_missing, request.settings.language.label(uiContext))
            if (selected.isNetworkConnectionRequired) notice(request, s(R.string.narration_network_notice))
            require(engine.setSpeechRate(1f) == TextToSpeech.SUCCESS)
            val result = engine.synthesizeToFile(request.text, Bundle(), request.file, request.id)
            if (result != TextToSpeech.SUCCESS) {
                ttsPending.remove(request.id)
                synthesisFailed(request, s(R.string.narration_android_segment_error))
            }
        } catch (error: RuntimeException) {
            ttsPending.remove(request.id)
            synthesisFailed(request, errorMessage(error, R.string.narration_android_synthesis_error))
        }
    }

    private fun ttsListener() = object : UtteranceProgressListener() {
        override fun onStart(utteranceId: String?) = Unit
        override fun onDone(utteranceId: String?) {
            val id = utteranceId ?: return
            worker.post { ttsPending.remove(id)?.let(::generated) }
        }
        @Deprecated("Required abstract compatibility callback")
        override fun onError(utteranceId: String?) = onError(utteranceId, TextToSpeech.ERROR)
        override fun onError(utteranceId: String?, errorCode: Int) {
            val id = utteranceId ?: return
            worker.post {
                val request = ttsPending.remove(id) ?: return@post
                synthesisFailed(request, when (errorCode) {
                    TextToSpeech.ERROR_NETWORK, TextToSpeech.ERROR_NETWORK_TIMEOUT -> s(R.string.narration_android_network_error)
                    TextToSpeech.ERROR_NOT_INSTALLED_YET -> s(R.string.narration_android_data_missing)
                    else -> s(R.string.narration_android_failed)
                })
            }
        }
    }

    private fun generated(request: Synthesis) {
        if (!isCurrent(request)) { request.file.delete(); return }
        try {
            require(request.file.isFile && request.file.length() > 44) { "El motor produjo un archivo vacío" }
            val retriever = MediaMetadataRetriever()
            val duration = try {
                retriever.setDataSource(request.file.absolutePath)
                retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION)?.toIntOrNull()
            } finally { retriever.release() }
            require(duration != null && duration > 0) { "El audio no tiene una duración válida" }
            val cachedBytes = request.file.parentFile?.listFiles()?.sumOf { it.length() } ?: 0L
            require(cachedBytes <= 96L * 1024L * 1024L) { "La página excede el límite de audio temporal" }
            main.post {
                if (!isCurrent(request)) { worker.post { request.file.delete() }; return@post }
                watchdogs.remove(request.id)?.let(main::removeCallbacks)
                pending.remove(request.index)
                if (pending.isEmpty()) releaseSynthesisCpu()
                segments[request.index].file = request.file
                segments[request.index].duration = duration
                if (request.index == index && player == null) preparePlayer(segments[index])
                if (request.settings.engine != NarrationEngine.ANDROID) {
                    // Continue rendering the whole page in the background,
                    // even before Play is pressed. A two-sentence window
                    // bounds queued inference and keeps the first words ready.
                    ensureSegment(request.index + 2)
                }
            }
        } catch (_: Exception) {
            synthesisFailed(request, s(R.string.narration_invalid_audio))
        }
    }

    private fun preparePlayer(segment: Segment) {
        val file = segment.file ?: return
        val duration = segment.duration ?: return
        if (offset < 0L) {
            offset += duration
            if (offset < 0L && index > 0) {
                selectSegment(index - 1, offset)
                return
            }
        }
        if (offset >= duration && index < segments.lastIndex) {
            selectSegment(index + 1, offset - duration)
            return
        }
        offset = offset.coerceIn(0L, (duration - 1).coerceAtLeast(0).toLong())
        val currentGeneration = generation
        val currentIndex = index
        try {
            val media = MediaPlayer()
            media.setWakeMode(appContext, PowerManager.PARTIAL_WAKE_LOCK)
            player = media
            playerReady = false
            media.setAudioAttributes(attributes)
            media.setDataSource(file.absolutePath)
            media.setOnPreparedListener {
                if (closed || generation != currentGeneration || index != currentIndex || player !== media) return@setOnPreparedListener
                try {
                    playerReady = true
                    media.seekTo(offset.toInt())
                    applyRate()
                    if (playing && player === media) media.start()
                    // Build the following sentences while this one plays.
                    if (index + 1 < segments.size) ensureSegment(index + 1)
                    if (index + 2 < segments.size) ensureSegment(index + 2)
                } catch (_: RuntimeException) { fail(s(R.string.narration_audio_start_error)) }
            }
            media.setOnCompletionListener {
                if (closed || generation != currentGeneration || player !== media) return@setOnCompletionListener
                if (index < segments.lastIndex) selectSegment(index + 1, 0)
                else {
                    finished = true
                    playing = false
                    releaseFocus()
                    onState(false)
                }
            }
            media.setOnErrorListener { _, _, _ ->
                if (generation == currentGeneration && player === media) fail(s(R.string.narration_audio_play_error))
                true
            }
            media.prepareAsync()
        } catch (_: Exception) { fail(s(R.string.narration_audio_open_error)) }
    }

    private fun applyRate() {
        val media = player ?: return
        // A nonzero PlaybackParams speed starts a prepared MediaPlayer. Keep the
        // preference while paused and apply it on resume, before normal playback.
        if (!playerReady || !playing) return
        try {
            media.playbackParams = PlaybackParams().setSpeed(NarrationSettingsRepository.get(appContext).rate).setPitch(1f)
        } catch (_: RuntimeException) { fail(s(R.string.narration_rate_error)) }
    }

    private fun reportFallback(request: Synthesis, reason: String) {
        val installed = runCatching { File(models.directory(), "model.onnx").isFile }.getOrDefault(false)
        main.post {
            if (isCurrent(request) && !reportedFallback) {
                reportedFallback = true
                if (request.settings.engine == NarrationEngine.KOKORO || installed) onError(s(R.string.narration_fallback, reason))
            }
        }
    }
    private fun notice(request: Synthesis, message: String) {
        main.post {
            if (isCurrent(request) && request.index == index && !warnedNetwork) {
                warnedNetwork = true
                onError(message)
            }
        }
    }
    private fun synthesisFailed(request: Synthesis, message: String) {
        main.post { if (isCurrent(request)) fail(message) }
    }
    private fun failTtsPending(message: String) {
        ttsReady = false
        ttsPending.values.toList().forEach { synthesisFailed(it, message) }
        ttsPending.clear()
    }
    private fun isCurrent(request: Synthesis): Boolean = !closed &&
        request.generation == generation && request.configuration == configuration
    private fun fail(message: String) { stopInternal(); if (!closed) onError(message) }

    private fun releasePlayer() {
        playerReady = false
        runCatching { player?.release() }
        player = null
    }

    private fun stopInternal(releaseNeuralResources: Boolean = false) {
        val previous = directory
        generation++
        kokoro?.cancelCurrentRun()
        english?.cancelCurrentRun()
        val wasPlaying = playing
        playing = false
        directory = null; segments.clear(); pending.clear(); releaseSynthesisCpu()
        index = 0; offset = 0; finished = false
        reportedFallback = false; warnedNetwork = false
        watchdogs.values.forEach(main::removeCallbacks); watchdogs.clear()
        releasePlayer(); releaseFocus()
        if (!closed) worker.post {
            runCatching { tts?.stop() }
            ttsPending.clear()
            // Keep the verified ONNX session and language frontends warm between
            // pages of the SAME lesson. The audio files are still discarded.
            // Remove them if narration is disabled, assets change or we close.
            if (releaseNeuralResources) {
                releaseKokoro()
                releasePhonemizers()
            }
            if (previous != null) cleanup(previous)
        }
        if (wasPlaying) onState(false)
    }

    private fun cleanup(root: File) {
        runCatching {
            val directory = root.canonicalFile
            if (directory.parentFile != appContext.cacheDir.canonicalFile || !directory.name.startsWith("lesson-narration-")) return
            directory.listFiles()?.forEach { file -> if (file.canonicalFile.parentFile == directory && file.isFile) file.delete() }
            directory.delete()
        }
    }

    @Suppress("DEPRECATION") private fun acquireFocus(): Boolean {
        if (focusHeld) return true
        val manager = audioManager ?: return false
        val result = runCatching {
            if (Build.VERSION.SDK_INT >= 26 && focusRequest != null) manager.requestAudioFocus(focusRequest)
            else manager.requestAudioFocus(focusListener, AudioManager.STREAM_MUSIC, AudioManager.AUDIOFOCUS_GAIN_TRANSIENT_MAY_DUCK)
        }.getOrDefault(AudioManager.AUDIOFOCUS_REQUEST_FAILED)
        focusHeld = result == AudioManager.AUDIOFOCUS_REQUEST_GRANTED
        return focusHeld
    }
    @Suppress("DEPRECATION") private fun releaseFocus() {
        if (!focusHeld) return
        focusHeld = false
        val manager = audioManager ?: return
        runCatching {
            if (Build.VERSION.SDK_INT >= 26 && focusRequest != null) manager.abandonAudioFocusRequest(focusRequest)
            else manager.abandonAudioFocus(focusListener)
        }
    }

    private fun releaseKokoro() { runCatching { kokoro?.close() }; kokoro = null }
    private fun releasePhonemizers() {
        fun release(resource: Any?) { if (resource is AutoCloseable) runCatching { resource.close() } }
        release(english); release(french)
        english = null; french = null; spanishLexicon = null
    }
    fun close() = onMain {
        if (closed) return@onMain
        stopInternal(releaseNeuralResources = true); closed = true; removeResourceListener()
        main.removeCallbacksAndMessages(null); models.close()
        worker.post {
            runCatching { tts?.shutdown() }; tts = null; releaseKokoro()
            releasePhonemizers()
            workerThread.quitSafely()
        }
    }
    private fun onMain(action: () -> Unit) {
        if (Looper.myLooper() == Looper.getMainLooper()) action() else main.post { action() }
    }
}
