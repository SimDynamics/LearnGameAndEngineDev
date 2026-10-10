package com.learngraphicsprogramming.app

import java.io.File
import org.junit.Assert.assertTrue
import org.junit.Test

/** The global options button must never borrow a platform password-toggle label. */
class GlobalOptionsLabelTest {
    private val res = File(System.getProperty("translation.resources", "src/main/res"))

    @Test fun homeOptionsButtonUsesItsOwnLocalizedLabel() {
        val layout = File(res, "layout/activity_main.xml").readText()
        val button = layout.substringAfter("android:id=\"@+id/buttonOptions\"")
            .substringBefore("</Button>")
        assertTrue(button.contains("android:text=\"@string/main_global_options_button\""))
        assertTrue(button.contains("android:contentDescription=\"@string/main_global_options_button\""))
        listOf("values", "values-en", "values-fr").forEach { directory ->
            val strings = File(res, "$directory/strings.xml").readText()
            assertTrue("$directory must define the global-options title",
                strings.contains("name=\"main_global_options_button\""))
        }
    }
}
