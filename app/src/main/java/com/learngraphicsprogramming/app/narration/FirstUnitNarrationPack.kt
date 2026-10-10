package com.learngraphicsprogramming.app.narration

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

/**
 * The exact keys for the first course's first unit are calculated with the
 * SAME Kotlin text extraction and sentence splitting used during playback.
 * A desktop tool exports only matching verified WAVs, then makes Opus assets.
 */
internal object FirstUnitNarrationPack {
    const val MANIFEST_FILE = "kokoro-first-unit-manifest.json"
    private const val UNIT_PREFIX =
        "programming/cpp_fundamentals/cpp_fundamentals__intro_programming/"
    private val lessons = setOf(
        "cpp_fundamentals__intro_programming__what_is_programming",
        "cpp_fundamentals__intro_programming__first_contact_cpp",
        "cpp_fundamentals__intro_programming__program_structure"
    )

    fun writeManifest(context: Context) {
        val entries = JSONArray()
        val seen = mutableSetOf<String>()
        val presentLessons = mutableSetOf<String>()
        for (item in NarrationCurriculum.sentences(context.applicationContext)) {
            if (!item.location.startsWith(UNIT_PREFIX)) {
                if (presentLessons.size == lessons.size) break
                continue
            }
            val lesson = item.location.removePrefix(UNIT_PREFIX).substringBefore("/")
            if (lesson !in lessons) continue
            presentLessons += lesson
            val hash = KokoroNarrationCache.key(item.language.tag, item.voice, item.text)
            if (seen.add(hash)) {
                entries.put(JSONObject()
                    .put("hash", hash)
                    .put("language", item.language.tag)
                    .put("voice", item.voice)
                    .put("lesson", lesson)
                    .put("location", item.location))
            }
        }
        check(presentLessons == lessons && entries.length() > 0) {
            "Primera unidad incompleta: ${lessons - presentLessons}"
        }
        val manifest = JSONObject()
            .put("format", 1)
            .put("course", "cpp_fundamentals")
            .put("unit", "intro_programming")
            .put("lessons", JSONArray(lessons.toList()))
            .put("uniqueSentences", entries.length())
            .put("entries", entries)
        val output = File(context.filesDir, MANIFEST_FILE)
        val partial = File(context.filesDir, "$MANIFEST_FILE.part")
        try {
            partial.writeText(manifest.toString(), Charsets.UTF_8)
            check(partial.renameTo(output)) { "No se pudo publicar el manifiesto de voz" }
        } finally { partial.delete() }
    }
}
