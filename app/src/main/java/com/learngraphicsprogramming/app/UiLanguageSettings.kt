package com.learngraphicsprogramming.app

import android.app.Application
import android.app.LocaleManager
import android.content.Context
import com.google.android.play.core.splitcompat.SplitCompat
import android.content.res.Configuration
import android.os.Build
import android.os.LocaleList
import androidx.appcompat.app.AppCompatDelegate
import androidx.core.os.LocaleListCompat

/** UI locales only: educational assets and narration have their own selection policies. */
object UiLanguageSettings {
    private const val PREFERENCES = "ui_language_settings"
    private const val SELECTION = "selection"
    private const val INITIALIZED = "initialized"
    const val DEFAULT_LANGUAGE = "es"

    fun supportedTags(context: Context): List<String> =
        context.resources.getStringArray(R.array.ui_language_tags).toList()

    fun initialize(context: Context) {
        val preferences = context.getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE)
        if (Build.VERSION.SDK_INT >= 33) {
            val manager = context.getSystemService(LocaleManager::class.java)
            // Android owns persistence on 13+. Respect choices made in Android Settings too.
            if (!preferences.getBoolean(INITIALIZED, false)) {
                val existing = manager.applicationLocales.toLanguageTags()
                val initial = preferences.getString(SELECTION, null) ?: existing.ifEmpty { DEFAULT_LANGUAGE }
                manager.applicationLocales = LocaleList.forLanguageTags(initial)
                preferences.edit().putBoolean(INITIALIZED, true).apply()
            }
        } else {
            AppCompatDelegate.setApplicationLocales(LocaleListCompat.forLanguageTags(
                preferences.getString(SELECTION, DEFAULT_LANGUAGE).orEmpty()))
        }
    }

    fun selectedTag(context: Context): String = if (Build.VERSION.SDK_INT >= 33) {
        context.getSystemService(LocaleManager::class.java).applicationLocales.toLanguageTags()
    } else {
        context.getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE)
            .getString(SELECTION, DEFAULT_LANGUAGE).orEmpty()
    }

    fun select(context: Context, tag: String) {
        require(tag.isEmpty() || tag in supportedTags(context))
        context.getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE).edit()
            .putString(SELECTION, tag).putBoolean(INITIALIZED, true).apply()
        if (Build.VERSION.SDK_INT >= 33) {
            context.getSystemService(LocaleManager::class.java).applicationLocales = LocaleList.forLanguageTags(tag)
        } else {
            AppCompatDelegate.setApplicationLocales(LocaleListCompat.forLanguageTags(tag))
        }
    }

    /** Resolve asynchronous application-context notices using the currently selected UI language. */
    fun localizedContext(context: Context): Context {
        val tags = selectedTag(context)
        val locales = if (tags.isEmpty()) android.content.res.Resources.getSystem().configuration.locales
            else LocaleList.forLanguageTags(tags)
        return context.createConfigurationContext(Configuration(context.resources.configuration).apply {
            setLocales(locales)
        })
    }
}

/**
 * Main application entry point.
 *
 * Keep the existing language initialization and enable Play SplitCompat only
 * for builds that actually package the optional Clang 21 dynamic feature.
 */
class LearnGraphicsApplication : Application() {
    override fun attachBaseContext(base: Context) {
        super.attachBaseContext(base)
        if (BuildConfig.LGP_PLAY_CLANG21_FEATURE_ENABLED) {
            SplitCompat.install(this)
        }
    }

    override fun onCreate() {
        super.onCreate()
        UiLanguageSettings.initialize(this)
    }
}
