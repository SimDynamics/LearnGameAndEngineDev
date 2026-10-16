package com.learngraphicsprogramming.app

import java.io.File
import javax.xml.parsers.DocumentBuilderFactory
import org.junit.Assert.*
import org.junit.Test
import org.w3c.dom.Element

/** Static wiring complements logic tests; real MediaPlayer/touch behavior must be checked on Android. */
class LessonInteractionWiringTest {
    private val resources = File(System.getProperty("translation.resources","src/main/res"))
    private val sources = resources.absoluteFile.parentFile.resolve("java/com/learngraphicsprogramming/app")
    private fun source(name: String) = sources.resolve(name).readText()
    @Test fun completionReleasesPlayerAndDoesNotInvokeResumeOrRestart() {
        val text = source("narration/LessonNarrationController.kt")
        val body = text.substringAfter("private fun finishPage() {").substringBefore("\n    }")
        assertTrue(body.contains("finished = true")); assertTrue(body.contains("playing = false"))
        assertTrue(body.contains("onState(false)")); assertTrue(body.contains("releasePlayer()"))
        for(call in listOf("resume()","restart()","selectSegment(0","ensureSegment(0")) assertFalse(body.contains(call))
        assertTrue(text.contains("media.isLooping = false"))
        val rate = text.substringAfter("private fun applyRate() {").substringBefore("\n    }")
        assertTrue(rate.contains("!playerReady || !playing || (finished && incorrectFeedback == null)"))
        assertEquals("The guide and page must share the sole native player factory", 1,
            Regex("\\bMediaPlayer\\(\\)").findAll(text).count())
        assertTrue(text.contains("playbackGate?.completedNaturally(currentIndex)"))
    }
    @Test fun plusThreeAndRestartAreRealActionsWithRestartAfterRate() {
        val text = source("narration/LessonNarrationControls.kt")
        assertTrue(text.contains("onSeek(3)"))
        assertTrue(text.contains("R.id.buttonLessonRestartPage"))
        assertTrue(text.indexOf("row.addView(rate,") < text.indexOf("row.addView(restart,"))
        assertTrue(source("LessonActivity.kt").contains("restartFromBeginning = true"))
    }
    @Test fun readerPointerStreamBypassesLessonSwipeAndPopupSecondTapIsConsumed() {
        val text = source("LessonFloatingInteractions.kt")
        assertTrue(text.contains("Window.Callback by original"))
        assertTrue(text.contains("if (ownership.consumeAnchorGesture) return true"))
        assertTrue(text.contains("if (ownership.readerOwnsGesture)"))
        assertTrue(text.contains("target.dispatchTouchEvent(local)"))
        assertTrue(text.contains("ownership.end()"))
        val activity = source("LessonActivity.kt")
        assertTrue(activity.contains("floatingInteractions.shown(popup)"))
        assertTrue(activity.contains("floatingInteractions.dismissed(popup)"))
    }
    @Test fun playbackGatesDoNotLimitTheBackgroundSynthesisList() {
        val source = source("narration/NarrationCurriculum.kt")
        assertTrue(source.contains("LessonNarrationScript.page("))
        assertTrue(source.contains("NarrationSegments.sentences(script.texts)"))
        assertFalse(source.contains("ProgressRepository"))
        assertFalse(source.contains("canPlay("))
    }
    @Test fun countdownIsOnlyAThirdLabelInsideTheExistingLivesSlot() {
        val android = "http://schemas.android.com/apk/res/android"
        val doc = DocumentBuilderFactory.newInstance().apply { isNamespaceAware = true }
            .newDocumentBuilder().parse(File(resources,"layout/activity_lesson.xml"))
        val nodes = doc.getElementsByTagName("*")
        fun element(id: String) = (0 until nodes.length).map { nodes.item(it) as Element }
            .single { it.getAttributeNS(android,"id") == "@+id/$id" }
        val label = element("textAttemptRemaining")
        assertEquals(element("attemptIndicatorSlot"),label.parentNode)
        assertEquals("bottom|center_horizontal",label.getAttributeNS(android,"layout_gravity"))
        assertFalse(label.hasAttributeNS(android,"drawableLeft"))
        element("textAttemptCount"); element("textAttemptPercent")
    }
}
