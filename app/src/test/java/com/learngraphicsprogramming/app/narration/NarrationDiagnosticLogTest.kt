package com.learngraphicsprogramming.app.narration

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class NarrationDiagnosticLogTest {
    @get:Rule val temporary = TemporaryFolder()

    @Test fun logRetainsRecentCompleteUtf8EventsWithinItsLimit() {
        val log = temporary.newFile()
        repeat(50) { index ->
            val event = JSONObject().put("event", index).put("state", "Síntesis française").toString()
            BoundedNarrationDiagnosticLog.append(log, event, 256)
        }
        assertTrue(log.length() <= 256)
        val events = log.readLines().map(::JSONObject)
        assertEquals(49, events.last().getInt("event"))
        assertFalse(events.any { it.getInt("event") == 0 })
        assertTrue(events.all { it.getString("state") == "Síntesis française" })
    }

    @Test fun exceptionChainsRemainOneCompleteParsableEvent() {
        val log = temporary.newFile()
        val stack = "outer\nCaused by: inner\n\tat inference(File.kt:123)"
        val event = JSONObject().put("exception", stack).toString()
        BoundedNarrationDiagnosticLog.append(log, event, 1024)
        assertEquals(1, log.readLines().size)
        assertEquals(stack, JSONObject(log.readText()).getString("exception"))
    }
}
