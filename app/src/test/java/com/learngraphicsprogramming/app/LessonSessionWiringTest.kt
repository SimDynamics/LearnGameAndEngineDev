package com.learngraphicsprogramming.app

import java.io.File
import javax.xml.parsers.DocumentBuilderFactory
import org.junit.Assert.*
import org.junit.Test

/** Source-contract checks supplement, not replace, real Android playback validation. */
class LessonSessionWiringTest {
    private val resources = File(System.getProperty("translation.resources", "src/main/res"))
    private val sources = resources.absoluteFile.parentFile.resolve("java/com/learngraphicsprogramming/app")
    private fun source(name: String) = sources.resolve(name).readText()
    private fun body(text: String, signature: String): String {
        val begin = text.indexOf(signature)
        assertTrue("Missing $signature", begin >= 0)
        val open = text.indexOf('{', begin)
        var depth = 1
        var end = open + 1
        while (depth > 0 && end < text.length) {
            if (text[end] == '{') depth++
            if (text[end] == '}') depth--
            end++
        }
        assertEquals(0, depth)
        return text.substring(open + 1, end - 1)
    }

    @Test fun livesUseOneHourEvenForTheDebugTestApk() {
        assertEquals(3_600_000L, AttemptRepository.RECOVERY_MILLIS)
        val repository = source("AttemptRepository.kt")
        assertTrue(repository.contains("RECOVERY_MILLIS.toFloat()"))
        assertTrue(repository.contains("attempt_reserve_seconds_test"))
        assertFalse(repository.contains("BuildConfig.DEBUG"))
    }

    @Test fun settingsWarnOnlyForTheChangesTheirSaveButtonWouldCommit() {
        for ((file, button) in listOf("OptionsActivity.kt" to "buttonSave",
                "CourseOptionsActivity.kt" to "buttonSave",
                "LessonOptionsActivity.kt" to "buttonSaveCourseSettings")) {
            assertTrue(source(file).contains("UnsavedSettingsExit.install(this, buttonBack) { $button.isEnabled }"))
        }
        val exit = source("UnsavedSettingsExit.kt")
        assertTrue(exit.contains("Toast.LENGTH_SHORT"))
        assertTrue(exit.contains("activity.finish()"))
        assertTrue(exit.contains("onBackPressedDispatcher.addCallback"))
        assertFalse(exit.contains("AlertDialog"))
    }

    @Test fun noticesAreAvailableInAllUiLanguages() {
        for (locale in listOf("values", "values-en", "values-fr")) {
            val doc = DocumentBuilderFactory.newInstance().newDocumentBuilder()
                .parse(File(resources, "$locale/settings_exit_strings.xml"))
            assertEquals(1, doc.getElementsByTagName("string").length)
        }
    }

    @Test fun allLessonNarrationPathsUseTheSameRealPageIdentity() {
        val activity = source("LessonActivity.kt")
        assertEquals(3, Regex("pageIdentity = narrationPageIdentity\\(\\)").findAll(activity).count())
        assertTrue(activity.contains("\$currentRouteId/\$currentLessonId/\$currentPage"))
    }

    @Test fun prefetchDoesNotCreateNativePlayersOrPromoteBookmarks() {
        val controller = source("narration/LessonNarrationController.kt")
        val prefetch = body(controller, "fun prefetchPage(")
        val start = body(controller, "private fun startPage(")
        assertFalse(prefetch.contains("resumeStore.begin("))
        assertFalse(start.contains("resumeStore.begin("))
        assertTrue(start.contains("resumeStore.restore("))
        assertFalse(prefetch.contains("MediaPlayer("))
        assertFalse(start.contains("MediaPlayer("))
        val ensure = body(controller, "private fun ensureSegment(")
        assertTrue(ensure.contains("if (playing && next == (feedback?.index ?: index) && player == null &&"))
        assertTrue(ensure.contains("((feedback != null) == (incorrectFeedback != null))) preparePlayer(segment, feedback)"))
        val generated = body(controller, "private fun generated(")
        assertTrue(generated.contains("if (incorrectFeedback == null && playing && request.index == index && player == null) preparePlayer(segment)"))
    }

    @Test fun pauseAndDepartureReleaseResourcesAfterRememberingThePosition() {
        val controller = source("narration/LessonNarrationController.kt")
        val pause = body(controller, "private fun pause()")
        assertTrue(pause.indexOf("rememberPlaybackPosition()") < pause.indexOf("releasePlayer()"))
        assertTrue(pause.contains("offset = playbackPoint().positionMs"))
        val stop = body(source("LessonActivity.kt"), "override fun onStop()")
        assertTrue(stop.contains("isInteractive"))
        assertTrue(stop.contains("isKeyguardLocked"))
        assertTrue(stop.contains("narrationController?.close()"))
        assertTrue(stop.contains("narrationController = null"))
    }

    @Test fun bookmarkEvictionDoesNotTouchAudioFilesOrSynthesisQueues() {
        val store = source("narration/NarrationResumeStore.kt")
        for (forbidden in listOf("File(", "MediaPlayer", "KokoroNarrationCache", "Thread(", "WorkManager"))
            assertFalse(store.contains(forbidden))
        assertTrue(store.contains("0 until NarrationResumePolicy.MAX_BOOKMARKS"))
    }
}
