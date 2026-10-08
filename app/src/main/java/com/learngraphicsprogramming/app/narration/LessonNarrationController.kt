package com.learngraphicsprogramming.app.narration

import android.content.Context
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
    private val appContext = context.applicationContext
    private val main = Handler(Looper.getMainLooper())
    private val workerThread = HandlerThread("LessonNarration", Process.THREAD_PRIORITY_BACKGROUND).apply { start() }
    private val worker = Handler(workerThread.looper)
    private val models = KokoroModelStore(appContext)
    private val audioManager = appContext.getSystemService(AudioManager::class.java)
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
        if (!closed) stopInternal()
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
        if (this.segments.isNotEmpty() && this.language == language) {
            if (playing) pause() else resume()
            return@onMain
        }
        stopInternal()
        settings = NarrationSettingsRepository.forLanguage(appContext, language)
        if (!settings.enabled) return@onMain
        val prepared = NarrationSegments.prepare(segments, 250)
        if (prepared.isEmpty()) {
            onError("Esta página no contiene texto que se pueda leer.")
            return@onMain
        }
        this.language = language
        this.segments = prepared.map { Segment(it) }.toMutableList()
        directory = File(appContext.cacheDir, "lesson-narration-" + UUID.randomUUID())
        resume()
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
            onError("No se pudo iniciar la lectura mientras otra aplicación utiliza el audio.")
            return
        }
        playing = true
        onState(true)
        if (finished) {
            finished = false
            selectSegment(0, 0)
        } else if (playerReady) {
            applyRate()
            runCatching { player?.start() }.onFailure { fail("No se pudo reanudar la lectura.") }
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
            stopInternal()
            return@onMain
        }
        if (settings.engine != previous.engine || settings.kokoroVoice != previous.kokoroVoice ||
            settings.androidVoice != previous.androidVoice
        ) {
            configuration++
            kokoro?.cancelCurrentRun()
            english?.cancelCurrentRun()
            pending.clear()
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
                failTtsPending("El proveedor de voz cambió; inicia la lectura de nuevo.")
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
        val session = directory ?: return
        val request = Synthesis(generation, configuration, next, segment.text,
            File(session, configuration.toString() + "_" + next + ".wav"),
            NarrationSettingsRepository.forLanguage(appContext, language))
        val timeout = Runnable {
            if (isCurrent(request) && pending.contains(next)) fail("El motor de voz no respondió a tiempo. Puedes volver a intentarlo.")
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
                    if (!File(root, "model.onnx").isFile) error("El modelo Kokoro no está instalado")
                    val voice = KokoroVoiceCatalog.find(request.settings.kokoroVoice)
                        ?: error("La voz Kokoro seleccionada no es válida")
                    if (!File(root, voice.artifact.fileName).isFile) error("La voz Kokoro seleccionada no está instalada")
                    if (!Build.SUPPORTED_ABIS.contains("arm64-v8a")) error("Kokoro requiere ARM64")
                    val phonemes = phonemize(request.text, request.settings.language, root) { isCurrent(request) }
                    if (!isCurrent(request)) return
                    val engine = kokoro ?: KokoroOnnxEngine(root).also { kokoro = it }
                    if (!isCurrent(request)) return
                    val pcm = engine.synthesizePhonemes(phonemes, request.settings.kokoroVoice, 1f) { !isCurrent(request) }
                    if (!isCurrent(request)) return
                    NarrationWavWriter.write(request.file, pcm)
                    generated(request)
                    return
                } catch (_: InterruptedException) {
                    // Cancellation invalidated the page/configuration before aborting its G2P.
                    return
                } catch (error: Exception) {
                    if (!isCurrent(request)) return
                    reportFallback(request, error.message ?: "La síntesis neuronal no está disponible")
                } catch (_: UnsatisfiedLinkError) {
                    if (!isCurrent(request)) return
                    reportFallback(request, "ONNX Runtime no está disponible en este dispositivo")
                }
            }
            if (!isCurrent(request)) return
            ttsPending[request.id] = request
            if (ttsReady) synthesizeAndroid(request) else initializeTts()
        } catch (_: Exception) {
            synthesisFailed(request, "No se pudo preparar el audio temporal de esta página.")
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
                        failTtsPending("No hay un motor de voz Android disponible. Configura uno en las opciones.")
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
                        failTtsPending("No se pudo configurar el motor de voz Android.")
                    }
                }
            }
        } catch (_: RuntimeException) {
            ttsInitializing = false
            failTtsPending("No se pudo abrir el motor de voz Android.")
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
                ?: throw IllegalStateException("No hay una voz sin Internet instalada para " + request.settings.language.label + ". Elige una voz en las opciones.")
            if (selected.isNetworkConnectionRequired) notice(request, "La voz Android seleccionada necesita Internet.")
            require(engine.setSpeechRate(1f) == TextToSpeech.SUCCESS)
            val result = engine.synthesizeToFile(request.text, Bundle(), request.file, request.id)
            if (result != TextToSpeech.SUCCESS) {
                ttsPending.remove(request.id)
                synthesisFailed(request, "Android no pudo generar el audio de este segmento.")
            }
        } catch (error: RuntimeException) {
            ttsPending.remove(request.id)
            synthesisFailed(request, error.message ?: "No se pudo generar la lectura con Android TTS.")
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
                    TextToSpeech.ERROR_NETWORK, TextToSpeech.ERROR_NETWORK_TIMEOUT -> "La voz Android no pudo conectarse. Elige una voz sin Internet."
                    TextToSpeech.ERROR_NOT_INSTALLED_YET -> "Faltan datos de la voz Android. Instálalos desde las opciones del sistema."
                    else -> "Android TTS no pudo generar el audio. Revisa su configuración."
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
                segments[request.index].file = request.file
                segments[request.index].duration = duration
                if (request.index == index && player == null) preparePlayer(segments[index])
            }
        } catch (_: Exception) {
            synthesisFailed(request, "El motor generó un audio que no se puede reproducir.")
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
                    if (index + 1 < segments.size) ensureSegment(index + 1)
                } catch (_: RuntimeException) { fail("No se pudo iniciar el audio de la página.") }
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
                if (generation == currentGeneration && player === media) fail("No se pudo reproducir el audio de la página.")
                true
            }
            media.prepareAsync()
        } catch (_: Exception) { fail("No se pudo abrir el audio de la página.") }
    }

    private fun applyRate() {
        val media = player ?: return
        // A nonzero PlaybackParams speed starts a prepared MediaPlayer. Keep the
        // preference while paused and apply it on resume, before normal playback.
        if (!playerReady || !playing) return
        try {
            media.playbackParams = PlaybackParams().setSpeed(NarrationSettingsRepository.get(appContext).rate).setPitch(1f)
        } catch (_: RuntimeException) { fail("Este dispositivo no pudo aplicar la velocidad de lectura.") }
    }

    private fun reportFallback(request: Synthesis, reason: String) {
        val installed = runCatching { File(models.directory(), "model.onnx").isFile }.getOrDefault(false)
        main.post {
            if (isCurrent(request) && !reportedFallback) {
                reportedFallback = true
                if (request.settings.engine == NarrationEngine.KOKORO || installed) onError("Se utilizará Android TTS: " + reason)
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

    private fun stopInternal() {
        val previous = directory
        generation++
        kokoro?.cancelCurrentRun()
        english?.cancelCurrentRun()
        val wasPlaying = playing
        playing = false
        directory = null; segments.clear(); pending.clear(); index = 0; offset = 0; finished = false
        reportedFallback = false; warnedNetwork = false
        watchdogs.values.forEach(main::removeCallbacks); watchdogs.clear()
        releasePlayer(); releaseFocus()
        if (!closed) worker.post {
            runCatching { tts?.stop() }
            ttsPending.clear()
            releaseKokoro()
            releasePhonemizers()
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
        stopInternal(); closed = true; removeResourceListener()
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
