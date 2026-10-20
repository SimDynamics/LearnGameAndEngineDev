package com.learngraphicsprogramming.app

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import java.io.File

/** Cross-language fixture check on the JVM; this does not execute Android repository APIs. */
class StableLessonRepositoryFixtureTest {
    @Test fun pythonGeneratedFixtureHasRuntimeCompatibleFingerprintsAndOriginalProgressGroups() {
        val assets = listOf(File("src/androidTest/assets"), File("app/src/androidTest/assets"))
            .first { it.isDirectory }
        val directory = File(assets, "content/es/programming/state_order_fixture/order")
        val original = JSONObject(File(directory, "legacy_original.json").readText())
        val reordered = JSONObject(File(directory, "lesson_relocated.json").readText())
        assertNull(LessonStateLayoutJson.read(original))
        // This parser recomputes every digest from org.json values. The checked-in metadata was
        // produced by stable_state_layout.reorder(prepare(original), plan), not this JVM parser.
        val layout = requireNotNull(LessonStateLayoutJson.read(reordered))
        assertEquals(LessonStatePosition(3, 1), layout.storagePosition(1, 2))
        assertEquals(LessonStatePosition(1, 2), layout.storagePosition(3, 1))
        assertEquals(LessonStatePosition(4, 1), layout.storagePosition(1, 3))
        assertEquals(4, layout.currentPage(2))
        assertEquals(listOf(LessonStatePosition(1, 0), LessonStatePosition(1, 1),
            LessonStatePosition(3, 1)), layout.progressGroups[1])
        assertEquals(listOf(LessonStatePosition(0, 0), LessonStatePosition(1, 3)),
            layout.progressGroups[4])
    }
}
