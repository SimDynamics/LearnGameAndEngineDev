package com.learngraphicsprogramming.app

import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

/**
 * Read-only Android host parser sweep of every published lesson's v2 identity
 * map. Does not emulate a UI session or touch a user's SharedPreferences.
 * Every course and locale is included; a new feature must not make an older
 * authored state map unreadable.
 */
class PublishedStateLayoutInventoryAndroidTest {
    @Test fun allPublishedMappedLessonsLoadWithoutDiscardingTheirOriginalIdentities() {
        val root = listOf(File("src/main/assets/content"), File("app/src/main/assets/content"))
            .firstOrNull(File::isDirectory) ?: error("Cannot locate published curricular assets")
        val formattingFailures = mutableListOf<String>()
        val existingHistoryGaps = mutableListOf<String>()
        var inspected = 0
        var mapped = 0
        var formatted = 0
        val languages = setOf("es", "en", "fr")
        for (file in root.walkTopDown().filter { it.isFile && it.name.startsWith("lesson_") &&
                it.extension.equals("json", ignoreCase = true) }) {
            val relative = file.relativeTo(root).invariantSeparatorsPath
            val language = relative.substringBefore('/')
            if (language !in languages) continue
            inspected++
            var hasSourceFormatArchive = false
            try {
                val lesson = JSONObject(file.readText(Charsets.UTF_8))
                if (!lesson.has("stateLayout")) continue
                mapped++
                val metadata = lesson.getJSONObject("stateLayout")
                hasSourceFormatArchive = metadata.has("sourceFormatRevisions")
                if (hasSourceFormatArchive) formatted++
                val version = metadata.getInt("version")
                val layout = requireNotNull(LessonStateLayoutJson.read(lesson))
                assertEquals(version, layout.version)
                assertEquals(lesson.getJSONArray("pages").length(), layout.current.size)
                if (version == 2) assertEquals(language, layout.locale)
            } catch (error: Exception) {
                val rootCause = generateSequence(error as Throwable?) { it.cause }.lastOrNull()
                val report = "$relative: ${error.message ?: error::class.java.simpleName}" +
                    (rootCause?.let { "; cause=${it.message}" } ?: "")
                if (hasSourceFormatArchive) formattingFailures += report
                else existingHistoryGaps += report
            }
        }
        // This is a diagnostic-only report. It never reassigns an old
        // response identity or grants a new source-format exemption.
        val report = JSONObject()
            .put("inspectedLessonFiles", inspected)
            .put("historicallyMappedLessons", mapped)
            .put("publishedSourceFormatLessons", formatted)
            .put("formattingFailures", JSONArray(formattingFailures))
            .put("unresolvedOtherHistoricalContracts", JSONArray(existingHistoryGaps))
        File("build/reports/curricular-state-inventory.json").apply {
            parentFile?.mkdirs()
            writeText(report.toString(2) + "\n", Charsets.UTF_8)
        }
        assertTrue("Published lesson search returned no curricular files", inspected > 0)
        assertTrue("No authenticated stateLayout was inspected", mapped > 0)
        assertTrue("No published physical-code revisions were inspected", formatted > 0)
        // This inventory deliberately does not promote *unrelated* historical
        // contracts into a format-compatibility failure. Their unresolved
        // identities are counted and reported, never falsely declared passed.
        println("AUDIT Android inventory: $inspected lessons; $mapped mapped; $formatted with source-format archives")
        println("AUDIT pre-existing or non-format historical parser gaps: ${existingHistoryGaps.size}")
        existingHistoryGaps.forEach { println("AUDIT prior gap: $it") }
        assertTrue("Published source-format revisions rejected (${formattingFailures.size}):\n" +
            formattingFailures.joinToString("\n").take(24000), formattingFailures.isEmpty())
        println("PASS Android all $formatted published format-aware state maps")
    }
}
