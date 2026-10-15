package com.learngraphicsprogramming.app

import android.content.Context
import android.content.res.Resources
import java.io.FileNotFoundException
import java.io.InputStream
import java.util.Locale

/**
 * Translated educational JSON lives under assets/content/{es,en,fr}, not res/values-*.
 * The global UI language selects the content; narrator voice settings remain independent.
 * Individual missing translations fall back to the published Spanish asset.
 */
internal object CurriculumAssetLocale {
    private val available = setOf("es", "en", "fr")

    fun languageFor(selection: String, systemLanguage: String): String {
        val primary = selection.ifBlank { systemLanguage }.substringBefore(',')
            .trim().replace('_', '-')
        val language = Locale.forLanguageTag(primary).language.lowercase(Locale.ROOT)
        return language.takeIf { it in available } ?: "es"
    }

    fun current(context: Context): String {
        val systemLanguage = Resources.getSystem().configuration.locales.get(0)
            ?.toLanguageTag().orEmpty()
        return languageFor(UiLanguageSettings.selectedTag(context), systemLanguage)
    }

    fun open(context: Context, relativePath: String): InputStream {
        val language = current(context)
        if (language != "es") {
            try {
                return context.assets.open("content/$language/$relativePath")
            } catch (_: FileNotFoundException) {
                // Not every course has been translated. Do not hide other I/O failures.
            }
        }
        return context.assets.open("content/es/$relativePath")
    }

    fun read(context: Context, relativePath: String): String =
        open(context, relativePath).bufferedReader(Charsets.UTF_8).use { it.readText() }
}
