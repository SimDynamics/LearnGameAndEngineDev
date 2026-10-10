package com.learngraphicsprogramming.app

import java.io.File
import javax.xml.parsers.DocumentBuilderFactory
import org.junit.Assert.assertEquals
import org.junit.Test
import org.w3c.dom.Element

/** Lesson text uses the full screen width in both programming and mathematics. */
class LessonFullWidthLayoutTest {
    private val resources = File(System.getProperty("translation.resources", "src/main/res"))
    private val androidNamespace = "http://schemas.android.com/apk/res/android"

    @Test fun lessonContentHasNoHorizontalInsets() {
        val layout = File(resources, "layout/activity_lesson.xml")
        val document = DocumentBuilderFactory.newInstance()
            .apply { isNamespaceAware = true }
            .newDocumentBuilder()
            .parse(layout)
        val nodes = document.getElementsByTagName("*")
        val elements = (0 until nodes.length).map { nodes.item(it) as Element }
        val scroll = elements.single {
            it.getAttributeNS(androidNamespace, "id") == "@+id/lessonScroll"
        }
        val content = elements.single {
            it.getAttributeNS(androidNamespace, "id") == "@+id/lessonContent"
        }

        assertEquals("match_parent", scroll.getAttributeNS(androidNamespace, "layout_width"))
        assertEquals("match_parent", content.getAttributeNS(androidNamespace, "layout_width"))
        assertEquals("0dp", content.getAttributeNS(androidNamespace, "paddingStart"))
        assertEquals("0dp", content.getAttributeNS(androidNamespace, "paddingEnd"))
        // Keep the original vertical rhythm; only horizontal text insets change.
        assertEquals("24dp", content.getAttributeNS(androidNamespace, "paddingTop"))
        assertEquals("16dp", content.getAttributeNS(androidNamespace, "paddingBottom"))
    }
}
