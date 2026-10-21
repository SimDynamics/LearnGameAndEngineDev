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

    data class ResolvedText(val text: String, val language: String)
    private data class ResolvedStream(val input: InputStream, val language: String)

    private fun openResolved(context: Context, relativePath: String): ResolvedStream {
        val language = current(context)
        if (language != "es") {
            try {
                return ResolvedStream(context.assets.open("content/$language/$relativePath"), language)
            } catch (_: FileNotFoundException) {
                // Not every course has been translated. Do not hide other I/O failures.
            }
        }
        return ResolvedStream(context.assets.open("content/es/$relativePath"), "es")
    }

    fun open(context: Context, relativePath: String): InputStream = openResolved(context, relativePath).input

    fun readResolved(context: Context, relativePath: String): ResolvedText {
        val asset = openResolved(context, relativePath)
        return ResolvedText(asset.input.bufferedReader(Charsets.UTF_8).use { it.readText() }, asset.language)
    }

    fun read(context: Context, relativePath: String): String = readResolved(context, relativePath).text
}
