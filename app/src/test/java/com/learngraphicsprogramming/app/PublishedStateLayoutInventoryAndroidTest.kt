package com.learngraphicsprogramming.app

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
        val failures = mutableListOf<String>()
        var inspected = 0
        var mapped = 0
        val languages = setOf("es", "en", "fr")
        for (file in root.walkTopDown().filter { it.isFile && it.name.startsWith("lesson_") &&
                it.extension.equals("json", ignoreCase = true) }) {
            val relative = file.relativeTo(root).invariantSeparatorsPath
            val language = relative.substringBefore('/')
            if (language !in languages) continue
            inspected++
            try {
                val lesson = JSONObject(file.readText(Charsets.UTF_8))
                if (!lesson.has("stateLayout")) continue
                mapped++
                val metadata = lesson.getJSONObject("stateLayout")
                val version = metadata.getInt("version")
                val layout = requireNotNull(LessonStateLayoutJson.read(lesson))
                assertEquals(version, layout.version)
                assertEquals(lesson.getJSONArray("pages").length, layout.current.size)
                if (version == 2) assertEquals(language, layout.locale)
            } catch (error: Exception) {
                val rootCause = generateSequence(error as Throwable?) { it.cause }.lastOrNull()
                failures += "$relative: ${error.message ?: error::class.java.simpleName}" +
                    (rootCause?.let { "; cause=${it.message}" } ?: "")
            }
        }
        assertTrue("Published lesson search returned no curricular files", inspected > 0)
        assertTrue("No authenticated stateLayout was inspected", mapped > 0)
        assertTrue("Android failed to load $mapped mapped lessons out of $inspected: \n" +
            failures.joinToString("\n").take(24000), failures.isEmpty())
        println("PASS Android published curricular inventory: $inspected lessons, $mapped authenticated layouts")
    }
}
