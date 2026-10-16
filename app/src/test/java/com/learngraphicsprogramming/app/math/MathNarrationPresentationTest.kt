package com.learngraphicsprogramming.app.math

import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class MathNarrationPresentationTest {
    private fun source() = JSONObject("""{"type":"math_puzzle","prompt":"Ordena.","responses":[
        {"id":"cards","type":"math_ordering_drag","items":[{"id":"a","label":"408"},{"id":"b","label":"480"},{"id":"c","label":"804"},{"id":"d","label":"840"}],
         "initialOrder":["d","c","b","a"],"validator":{"expected":["a","b","c","d"]}}]}""")

    @Test fun onlyTheCurrentNativePresentationOrderChangesInTheTemporaryProjection() {
        val original = source()
        val snapshot = original.toString()
        val result = MathNarrationPresentation.block(original, mapOf("cards" to listOf("b", "d", "c", "a")))
        val response = result.getJSONArray("responses").getJSONObject(0)
        assertEquals("[\"b\",\"d\",\"c\",\"a\"]", response.getJSONArray("initialOrder").toString())
        assertEquals(original.getJSONArray("responses").getJSONObject(0).getJSONArray("items").toString(), response.getJSONArray("items").toString())
        assertEquals("[\"a\",\"b\",\"c\",\"d\"]", response.getJSONObject("validator").getJSONArray("expected").toString())
        assertEquals(snapshot, original.toString())
    }

    @Test fun absentOrInvalidNativeOrdersNeverInventCardsOrChangeTheSource() {
        val original = source()
        assertSame(original, MathNarrationPresentation.block(original, emptyMap()))
        for (order in listOf(listOf("d", "c"), listOf("d", "d", "b", "a"), listOf("d", "c", "b", "other"))) {
            assertEquals(original.toString(), MathNarrationPresentation.block(original, mapOf("cards" to order)).toString())
        }
    }
}
