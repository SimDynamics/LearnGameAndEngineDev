package com.learngraphicsprogramming.app.narration

import android.content.Context
import com.learngraphicsprogramming.app.R

enum class NarrationEngine {
    AUTO,
    KOKORO,
    ANDROID
}

data class NarrationSettings(
    val enabled: Boolean = true,
    val engine: NarrationEngine = NarrationEngine.AUTO,
    val kokoroVoice: String = "ef_dora",
    val androidVoice: String? = null,
    val rate: Float = 1f,
    val language: NarrationLanguage = NarrationLanguage.SPANISH
)

object NarrationSettingsRepository {

    private const val PREFERENCES_NAME = "lesson_narration_settings"
    private const val KEY_ENABLED = "enabled"
    private const val KEY_CONTROLS_VISIBLE = "controls_visible"
    private const val KEY_ENGINE = "engine"
    private const val KEY_KOKORO_VOICE = "kokoro_voice"
    private const val KEY_ANDROID_VOICE = "android_voice"
    private const val KEY_RATE = "rate"

    val KOKORO_VOICES get() = KokoroVoiceCatalog.voices.map { it.id }
    val RATES = listOf(0.5f, 1f, 1.5f, 2f)

    private fun preferences(context: Context) =
        context.applicationContext.getSharedPreferences(
            PREFERENCES_NAME,
            Context.MODE_PRIVATE
        )

    fun get(context: Context): NarrationSettings {
        val preferences = preferences(context)
        val engineName = preferences.getString(KEY_ENGINE, NarrationEngine.AUTO.name)
        val language = NarrationLanguage.fromTag(preferences.getString("language", "es")) ?: NarrationLanguage.SPANISH
        val voice = preferences.getString("kokoro_voice:${language.tag}", null)
            ?: preferences.getString(KEY_KOKORO_VOICE, language.defaultVoice)

        return NarrationSettings(
            enabled = preferences.getBoolean(KEY_ENABLED, true),
            engine = NarrationEngine.entries.firstOrNull { it.name == engineName }
                ?: NarrationEngine.AUTO,
            kokoroVoice = voice?.takeIf { KokoroVoiceCatalog.find(it)?.language == language } ?: language.defaultVoice,
            androidVoice = preferences.getString("android_voice:${language.tag}", null)
                ?: if (language == NarrationLanguage.SPANISH) preferences.getString(KEY_ANDROID_VOICE, null) else null,
            rate = normalizedRate(preferences.getFloat(KEY_RATE, 1f)),
            language = language
        )
    }

    fun saveEnabled(context: Context, enabled: Boolean) {
        preferences(context).edit().putBoolean(KEY_ENABLED, enabled).apply()
    }

    /** Presentation only: hiding the controls does not disable background narration. */
    fun controlsVisible(context: Context): Boolean =
        preferences(context).getBoolean(KEY_CONTROLS_VISIBLE, true)

    fun saveControlsVisible(context: Context, visible: Boolean) {
        preferences(context).edit().putBoolean(KEY_CONTROLS_VISIBLE, visible).apply()
    }

    fun saveEngine(context: Context, engine: NarrationEngine) {
        preferences(context).edit().putString(KEY_ENGINE, engine.name).apply()
    }

    fun saveKokoroVoice(context: Context, voice: String) {
        val selected = KokoroVoiceCatalog.find(voice) ?: return
        preferences(context).edit()
            .putString(KEY_KOKORO_VOICE, voice)
            .putString("kokoro_voice:${selected.language.tag}", voice)
            .putString("language", selected.language.tag)
            .apply()
    }

    fun saveAndroidVoice(context: Context, voice: String?) {
        val language = get(context).language
        val editor = preferences(context).edit()
            .putString("android_voice:${language.tag}", voice?.trim()?.takeIf { it.isNotEmpty() })
        if (language == NarrationLanguage.SPANISH) editor.remove(KEY_ANDROID_VOICE)
        editor.apply()
    }

    fun saveLanguage(context: Context, language: NarrationLanguage) {
        val current = get(context)
        val editor = preferences(context).edit()
            .putString("kokoro_voice:${current.language.tag}", current.kokoroVoice)
            .putString("android_voice:${current.language.tag}", current.androidVoice)
            .putString("language", language.tag)
        if (current.language == NarrationLanguage.SPANISH) editor.remove(KEY_ANDROID_VOICE)
        editor.apply()
    }

    fun forLanguage(context: Context, language: NarrationLanguage): NarrationSettings {
        val current = get(context)
        if (current.language == language) return current
        val prefs = preferences(context)
        return current.copy(language = language,
            kokoroVoice = prefs.getString("kokoro_voice:${language.tag}", null)
                ?.takeIf { KokoroVoiceCatalog.find(it)?.language == language }
                ?: prefs.getString(KEY_KOKORO_VOICE, null)?.takeIf { KokoroVoiceCatalog.find(it)?.language == language }
                ?: language.defaultVoice,
            androidVoice = prefs.getString("android_voice:${language.tag}", null)
                ?: if (language == NarrationLanguage.SPANISH) prefs.getString(KEY_ANDROID_VOICE, null) else null)
    }

    fun savePosition(context: Context, x: Float, y: Float) {
        if (!x.isFinite() || !y.isFinite()) return
        val prefs = preferences(context)
        if (prefs.getFloat("bar_x",1f) == x && prefs.getFloat("bar_y",0.85f) == y) return
        prefs.edit().putFloat("bar_x",x).putFloat("bar_y",y).apply()
    }

    fun position(context: Context): Pair<Float, Float> = preferences(context).let {
        (it.getFloat("bar_x",1f).takeIf { value -> value.isFinite() } ?: 1f) to
            (it.getFloat("bar_y",0.85f).takeIf { value -> value.isFinite() } ?: 0.85f)
    }

    fun saveRate(context: Context, rate: Float) {
        preferences(context).edit().putFloat(KEY_RATE, normalizedRate(rate)).apply()
    }

    fun nextRate(rate: Float): Float {
        val index = RATES.indexOf(normalizedRate(rate))
        return RATES[(index + 1) % RATES.size]
    }

    fun rateLabel(context: Context, rate: Float): String = context.getString(when (normalizedRate(rate)) {
        0.5f -> R.string.narration_rate_half
        1.5f -> R.string.narration_rate_one_half
        2f -> R.string.narration_rate_double
        else -> R.string.narration_rate_normal
    })

    // Stable diagnostic labels remain available to non-UI callers.
    fun rateLabel(rate: Float): String = when (normalizedRate(rate)) {
        0.5f -> "0.5X"
        1.5f -> "1.5X"
        2f -> "2X"
        else -> "1X"
    }

    private fun normalizedRate(rate: Float): Float =
        RATES.firstOrNull { it == rate } ?: 1f
}
