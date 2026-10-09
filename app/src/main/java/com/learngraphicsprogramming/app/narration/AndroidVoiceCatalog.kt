package com.learngraphicsprogramming.app.narration

import android.content.Context
import android.os.Handler
import android.os.HandlerThread
import android.os.Looper
import android.os.Process
import android.provider.Settings
import android.speech.tts.TextToSpeech
import android.speech.tts.Voice
import com.learngraphicsprogramming.app.R
import com.learngraphicsprogramming.app.UiLanguageSettings

data class SpanishVoice(
    val name: String,
    val label: String,
    val requiresNetwork: Boolean,
    val quality: Int
)

internal object SpanishVoices {
    fun available(voices: Set<Voice>?, language: NarrationLanguage = NarrationLanguage.SPANISH): List<Voice> = voices.orEmpty()
        .filter {
            it.locale.language == language.locale.language &&
                TextToSpeech.Engine.KEY_FEATURE_NOT_INSTALLED !in it.features.orEmpty()
        }
        .sortedWith(
            compareBy<Voice> { it.isNetworkConnectionRequired }
                .thenBy { if (language.locale.country.isEmpty() || it.locale.country == language.locale.country) 0 else 1 }
                .thenByDescending { it.quality }
                .thenBy { it.latency }
                .thenBy { it.name }
        )

    fun describe(context: Context, voice: Voice): SpanishVoice {
        val localeName = voice.locale.getDisplayName(context.resources.configuration.locales[0])
        val connection = if (voice.isNetworkConnectionRequired) context.getString(R.string.narration_requires_network) else context.getString(R.string.narration_offline)
        val quality = when {
            voice.quality >= Voice.QUALITY_VERY_HIGH -> context.getString(R.string.narration_quality_very_high)
            voice.quality >= Voice.QUALITY_HIGH -> context.getString(R.string.narration_quality_high)
            voice.quality >= Voice.QUALITY_NORMAL -> context.getString(R.string.narration_quality_normal)
            else -> context.getString(R.string.narration_quality_basic)
        }
        return SpanishVoice(
            name = voice.name,
            label = context.getString(R.string.narration_android_voice_label, localeName, quality, connection, voice.name),
            requiresNetwork = voice.isNetworkConnectionRequired,
            quality = voice.quality
        )
    }
}

@Suppress("DEPRECATION")
internal fun defaultAndroidTtsEngine(context: Context): String? = runCatching {
    Settings.Secure.getString(context.contentResolver, Settings.Secure.TTS_DEFAULT_SYNTH)
}.getOrNull()

/** Consulta el motor predeterminado del sistema sin bloquear la pantalla de opciones. */
class AndroidVoiceCatalog(context: Context) {
    private val applicationContext = context.applicationContext
    private val appContext: Context get() = UiLanguageSettings.localizedContext(applicationContext)
    private val main = Handler(Looper.getMainLooper())
    private val workerThread = HandlerThread("NarrationVoiceCatalog", Process.THREAD_PRIORITY_BACKGROUND)
        .apply { start() }
    private val worker = Handler(workerThread.looper)

    @Volatile
    private var closed = false

    private var requestId = 0L
    private var pendingRequestId: Long? = null

    // Los siguientes campos pertenecen al hilo del catálogo.
    private var tts: TextToSpeech? = null
    private var initialized = false
    private var initializing = false
    private var engineDefault: String? = null
    private var initializationGeneration = 0L
    private var request: Request? = null

    private data class Request(
        val id: Long,
        val onResult: (List<SpanishVoice>) -> Unit,
        val onError: (String) -> Unit,
        val language: NarrationLanguage
    )

    fun load(
        onResult: (List<SpanishVoice>) -> Unit,
        onError: (String) -> Unit,
        language: NarrationLanguage = NarrationLanguage.SPANISH
    ) = onMain {
        if (closed) return@onMain
        val next = Request(++requestId, onResult, onError, language)
        pendingRequestId = next.id
        main.postDelayed({
            deliverError(next, appContext.getString(R.string.narration_android_timeout))
        }, 15_000L)
        worker.post {
            if (closed) return@post
            request = next
            if (tts != null && engineDefault != defaultAndroidTtsEngine(applicationContext)) {
                initializationGeneration++
                runCatching { tts?.shutdown() }
                tts = null
                initialized = false
                initializing = false
            }
            if (initialized) {
                publishVoices()
            } else if (!initializing) {
                initialize()
            }
        }
    }

    private fun initialize() {
        initializing = true
        engineDefault = defaultAndroidTtsEngine(applicationContext)
        val generation = ++initializationGeneration
        try {
            tts = TextToSpeech(applicationContext) { status ->
                worker.post {
                    if (closed || generation != initializationGeneration) return@post
                    initializing = false
                    initialized = status == TextToSpeech.SUCCESS
                    if (initialized) {
                        publishVoices()
                    } else {
                        request?.let {
                            deliverError(it, appContext.getString(R.string.narration_android_missing))
                        }
                        tts?.shutdown()
                        tts = null
                    }
                }
            }
        } catch (_: RuntimeException) {
            initializing = false
            request?.let {
                deliverError(it, appContext.getString(R.string.narration_android_open_error))
            }
        }
    }

    private fun publishVoices() {
        val current = request ?: return
        try {
            val voices = SpanishVoices.available(tts?.voices, current.language).map { SpanishVoices.describe(appContext, it) }
            main.post {
                if (!closed && pendingRequestId == current.id) {
                    pendingRequestId = null
                    current.onResult(voices)
                }
            }
        } catch (_: RuntimeException) {
            deliverError(current, appContext.getString(R.string.narration_android_query_error))
        }
    }

    private fun deliverError(current: Request, message: String) = onMain {
        if (!closed && pendingRequestId == current.id) {
            pendingRequestId = null
            current.onError(message)
        }
    }

    fun close() = onMain {
        if (closed) return@onMain
        closed = true
        pendingRequestId = null
        main.removeCallbacksAndMessages(null)
        worker.post {
            request = null
            runCatching { tts?.shutdown() }
            tts = null
            workerThread.quitSafely()
        }
    }

    private fun onMain(action: () -> Unit) {
        if (Looper.myLooper() == Looper.getMainLooper()) action() else main.post { action() }
    }
}
