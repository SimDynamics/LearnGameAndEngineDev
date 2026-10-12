package com.learngraphicsprogramming.app

import java.io.File
import javax.xml.parsers.DocumentBuilderFactory
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.w3c.dom.Element

/** Source contract checks supplement, rather than replace, Android playback validation. */
class LessonReadingWiringTest {
    private val resources = File(System.getProperty("translation.resources", "src/main/res"))
    private val sources = resources.absoluteFile.parentFile.resolve("java/com/learngraphicsprogramming/app")
    private fun source(name: String) = sources.resolve(name).readText()
    private fun body(text: String, start: String): String {
        val begin = text.indexOf(start)
        assertTrue("Missing source boundary: $start", begin >= 0)
        val open = text.indexOf('{', begin)
        var depth = 1
        var index = open + 1
        while (depth > 0 && index < text.length) {
            if (text[index] == '{') depth++
            if (text[index] == '}') depth--
            index++
        }
        assertEquals("Unbalanced source boundary: $start", 0, depth)
        return text.substring(open + 1, index - 1)
    }

    @Test fun hidingControlsPausesRatherThanDisablingOrDestroyingNarration() {
        val click = body(source("LessonActivity.kt"), "buttonNarrationToggle.setOnClickListener")
        assertTrue(click.contains("saveControlsVisible(this, showControls)"))
        assertTrue(click.contains("narrationController?.pauseKeepingPosition()"))
        assertFalse(click.contains("saveEnabled(this, false)"))
        assertFalse(click.contains("saveEnabled(this, enabled)"))
        assertFalse(click.contains("narrationController?.stop()"))
        assertFalse(click.contains("narrationController?.restart()"))
        assertFalse(click.contains("narrationController?.toggle("))
    }

    @Test fun presentationPreferenceNeverMutatesTheEngineSettings() {
        val save = body(source("narration/NarrationSettingsRepository.kt"), "fun saveControlsVisible(")
        assertTrue(save.contains("putBoolean(KEY_CONTROLS_VISIBLE, visible)"))
        for (setting in listOf("KEY_ENABLED", "KEY_ENGINE", "KEY_RATE", "KEY_KOKORO_VOICE"))
            assertFalse(save.contains(setting))
    }

    @Test fun pauseEntryPointDoesNotResetLogicalPlayback() {
        val pause = body(source("narration/LessonNarrationController.kt"), "fun pauseKeepingPosition()")
        assertTrue(pause.contains("pause()"))
        for (reset in listOf("stopInternal", "releasePlayer", "selectSegment", "settingsChanged", "configuration++", "generation++"))
            assertFalse(pause.contains(reset))
        val visibility = body(source("LessonActivity.kt"), "private fun updateNarrationVisibility()")
        assertTrue(visibility.contains("controlsVisible"))
    }

    @Test fun adaptiveContainersAreWiredOnlyToTheHeaderBodyAndPageContent() {
        val android = "http://schemas.android.com/apk/res/android"
        val document = DocumentBuilderFactory.newInstance().apply { isNamespaceAware = true }
            .newDocumentBuilder().parse(File(resources, "layout/activity_lesson.xml"))
        val elements = document.getElementsByTagName("*")
        fun element(id: String) = (0 until elements.length).map { elements.item(it) as Element }
            .single { it.getAttributeNS(android, "id") == "@+id/$id" }
        assertEquals("com.learngraphicsprogramming.app.LessonPageContentLayout", element("lessonContent").tagName)
        assertEquals("24dp", element("lessonContent").getAttributeNS(android, "paddingTop"))
        assertEquals("16dp", element("lessonContent").getAttributeNS(android, "paddingBottom"))
        assertEquals("com.learngraphicsprogramming.app.LessonHeaderBody", element("lessonHeaderBody").tagName)
        assertEquals(element("lessonHeaderBody"), element("lessonHeaderActions").parentNode)
        assertEquals(element("lessonHeaderBody"), element("lessonHeaderLabels").parentNode)
        // Progress still spans the full header, outside the padded/adaptive body.
        assertEquals(element("lessonHeader"), element("lessonProgress").parentNode)
    }
}
