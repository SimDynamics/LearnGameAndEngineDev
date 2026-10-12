package com.learngraphicsprogramming.app

import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class LessonReaderControlsOrderTest {
    private val resources = File(System.getProperty("translation.resources", "src/main/res"))
    @Test fun backThreeBackFiveForwardFiveForwardThreeAndOtherControls() {
        val source = resources.absoluteFile.parentFile
            .resolve("java/com/learngraphicsprogramming/app/narration/LessonNarrationControls.kt").readText()
        val names = listOf(
            "buttonLessonBackThree", "buttonLessonBackFive",
            "buttonLessonForwardFive", "buttonLessonForwardThree",
            "row.addView(listen", "row.addView(rate", "row.addView(restart"
        )
        val positions = names.map { source.indexOf(it) }
        assertTrue(positions.all { it >= 0 })
        assertEquals(positions.sorted(), positions)
        assertTrue(source.contains("onSeek(-3)"))
        assertTrue(source.contains("onSeek(-5)"))
        assertTrue(source.contains("onSeek(5)"))
        assertTrue(source.contains("onSeek(3)"))
    }

    @Test fun forwardThreeSymbolMatchesAllThreeLocalesWithoutSecondsSuffix() {
        for (locale in listOf("values", "values-en", "values-fr")) {
            val xml = resources.resolve("$locale/reader_interaction_strings.xml").readText()
            assertTrue(xml.contains("<string name=\"narration_control_forward_three\">3↷</string>"))
            assertTrue(!xml.contains("+3s"))
        }
    }
}
