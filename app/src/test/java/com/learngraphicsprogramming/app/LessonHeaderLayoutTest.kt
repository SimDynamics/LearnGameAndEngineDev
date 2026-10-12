package com.learngraphicsprogramming.app

import java.io.File
import javax.xml.parsers.DocumentBuilderFactory
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.w3c.dom.Element

/** Regression coverage for action order, neutral styling and unmodified lives geometry. */
class LessonHeaderLayoutTest {
    private val resources = File(System.getProperty("translation.resources", "src/main/res"))
    private val android = "http://schemas.android.com/apk/res/android"

    private fun document(path: String) = DocumentBuilderFactory.newInstance().apply {
        isNamespaceAware = true
    }.newDocumentBuilder().parse(File(resources, path))

    private fun element(id: String): Element {
        val nodes = document("layout/activity_lesson.xml").getElementsByTagName("*")
        return (0 until nodes.length).map { nodes.item(it) as Element }.single {
            it.getAttributeNS(android, "id") == "@+id/$id"
        }
    }

    private fun attr(id: String, name: String) = element(id).getAttributeNS(android, name)

    @Test fun narrationThenLanguageThenLivesThenSettings() {
        val row = element("lessonHeaderActions")
        val expected = listOf("buttonNarrationToggle", "buttonLessonLanguage", "attemptIndicatorSlot", "buttonLessonSettings")
        val actual = (0 until row.childNodes.length).mapNotNull { row.childNodes.item(it) as? Element }
            .map { it.getAttributeNS(android, "id").removePrefix("@+id/") }.filter { it in expected }
        assertEquals(expected, actual)
        assertEquals("horizontal", row.getAttributeNS(android, "orientation"))
    }

    @Test fun narrationIsEightyPercentWithoutShrinkingItsTouchTarget() {
        val size = attr("buttonNarrationToggle", "layout_width").removeSuffix("dp").toDouble()
        val padding = attr("buttonNarrationToggle", "padding").removeSuffix("dp").toDouble()
        assertEquals(48.0, size, 0.001)
        assertEquals(38.0 * 0.8, size - 2 * padding, 0.001)
        assertEquals("48dp", attr("buttonNarrationToggle", "layout_height"))
    }

    @Test fun livesScaleTogetherWithoutBreakingRuntimeFillCalculation() {
        assertEquals("0.8", attr("attemptIndicator", "scaleX"))
        assertEquals("0.8", attr("attemptIndicator", "scaleY"))
        assertEquals("86dp", attr("attemptIndicator", "layout_width"))
        assertEquals("58dp", attr("attemptIndicator", "layout_height"))
        assertEquals("68.8dp", attr("attemptIndicatorSlot", "layout_width"))
        assertEquals("64dp", attr("attemptIndicatorSlot", "layout_height"))
        for (id in listOf("imageAttemptRoute", "imageAttemptFill", "attemptFillClip")) {
            assertEquals("43dp", attr(id, "layout_width"))
            assertEquals("43dp", attr(id, "layout_height"))
        }
        // Do not replace the route indicator, its percentage or Premium state with a heart.
        element("textAttemptCount")
        element("textAttemptPercent")
        element("imageAttemptInfinity")
    }

    @Test fun languageHasTheInactiveNarrationTintAndNoColoredFrame() {
        assertEquals("#AAAEB7", attr("buttonLessonLanguage", "tint"))
        assertEquals("@android:color/transparent", attr("buttonLessonLanguage", "background"))
        assertEquals("48dp", attr("buttonLessonLanguage", "layout_width"))
        assertEquals("5dp", attr("buttonLessonLanguage", "padding"))
        val paths = document("drawable/ic_lesson_language.xml").getElementsByTagName("path")
        assertTrue(paths.length > 0)
        for (index in 0 until paths.length) {
            val path = paths.item(index) as Element
            assertEquals("#AAAEB7", path.getAttributeNS(android, "fillColor"))
            assertFalse(path.hasAttributeNS(android, "strokeColor"))
        }
    }

    @Test fun labelsCannotPushHeaderActionsOutsideANarrowPhone() {
        val row = element("lessonHeaderActions")
        val fixedWidth = (0 until row.childNodes.length)
            .mapNotNull { row.childNodes.item(it) as? Element }
            .sumOf { it.getAttributeNS(android, "layout_width").removeSuffix("dp").toDouble() }
        val padding = listOf("paddingStart", "paddingEnd")
            .sumOf { row.getAttributeNS(android, it).removeSuffix("dp").toDouble() }
        assertTrue("Action row must fit at 320dp", fixedWidth + padding <= 320.0)
        assertEquals("@+id/lessonHeaderLabels", (element("textProgress").parentNode as Element).getAttributeNS(android, "id"))
        assertEquals("24dp", attr("lessonHeaderLabels", "minHeight"))
        assertEquals("64dp", attr("lessonHeaderActions", "layout_height"))
    }

    @Test fun shortcutLabelsExistInAllThreeUiLanguages() {
        val expected = setOf("lesson_header_language", "lesson_header_language_title")
        for (locale in listOf("values", "values-en", "values-fr")) {
            val nodes = document("$locale/lesson_header_strings.xml").getElementsByTagName("string")
            val names = (0 until nodes.length).map { (nodes.item(it) as Element).getAttribute("name") }.toSet()
            assertEquals(expected, names)
            for (index in 0 until nodes.length) assertTrue(nodes.item(index).textContent.isNotBlank())
        }
    }
}
