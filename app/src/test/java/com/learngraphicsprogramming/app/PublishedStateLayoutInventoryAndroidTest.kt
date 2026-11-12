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
        // No historical incompatibility is advisory-only: the entire asset
        // inventory must load with preserved original slots and credentials.
        val historicalErrors = formattingFailures + existingHistoryGaps
        val report = JSONObject()
            .put("inspectedLessonFiles", inspected)
            .put("historicallyMappedLessons", mapped)
            .put("publishedSourceFormatLessons", formatted)
            .put("formattingFailures", JSONArray(formattingFailures))
            .put("unresolvedOtherHistoricalContracts", JSONArray(existingHistoryGaps))
            .put("historicalErrorCount", historicalErrors.size)
            .put("historicalErrors", JSONArray(historicalErrors))
        File("build/reports/curricular-state-inventory.json").apply {
            parentFile?.mkdirs()
            writeText(report.toString(2) + "\n", Charsets.UTF_8)
        }
        assertTrue("Published lesson search returned no curricular files", inspected > 0)
        assertTrue("No authenticated stateLayout was inspected", mapped > 0)
        assertTrue("No published physical-code revisions were inspected", formatted > 0)
        // A historical contract failure is a failure even when no formatting
        // archive was involved. Do not present a partial inventory as passed.
        println("AUDIT Android inventory: $inspected lessons; $mapped mapped; $formatted with source-format archives")
        println("AUDIT pre-existing or non-format historical parser gaps: ${existingHistoryGaps.size}")
        existingHistoryGaps.forEach { println("AUDIT prior gap: $it") }
        assertTrue("Published source-format revisions rejected (${formattingFailures.size}):\n" +
            formattingFailures.joinToString("\n").take(24000), formattingFailures.isEmpty())
        assertTrue("Unresolved Android historical contract errors (${historicalErrors.size}):\n" +
            historicalErrors.joinToString("\n").take(24000), historicalErrors.isEmpty())
        println("PASS Android all $mapped mapped histories among $inspected published lessons")
    }
}
