package com.learngraphicsprogramming.app.math

import org.json.JSONArray
import org.json.JSONObject

/** Temporary presentation of the original cards in their current native control order.
 * No solution, validator, source asset or student answer is changed. */
internal object MathNarrationPresentation {
    /** Reproduce the preceding extractor solely to relocate its existing bounded bookmark. */
    fun legacyBlock(source: JSONObject): JSONObject {
        val type = source.optString("type")
        if (type != "math_ordering_drag") {
            if (type != "math_puzzle") return source
            val responses = source.optJSONArray("responses") ?: return source
            if ((0 until responses.length()).none { responses.optJSONObject(it)?.optString("type") == "math_ordering_drag" }) return source
        }
        val result = JSONObject(source.toString())
        val responses = if (result.optString("type") == "math_puzzle") {
            result.optJSONArray("responses")?.let { array ->
                (0 until array.length()).mapNotNull(array::optJSONObject)
            }.orEmpty()
        } else listOf(result)
        responses.filter { it.optString("type") == "math_ordering_drag" }.forEach {
            (it.optJSONObject("config") ?: it).remove("initialOrder")
        }
        return result
    }

    fun block(source: JSONObject, orders: Map<String, List<String>>): JSONObject {
        if (orders.isEmpty()) return source
        val result = JSONObject(source.toString())
        val responses = if (result.optString("type") == "math_puzzle") {
            result.optJSONArray("responses")?.let { array ->
                (0 until array.length()).mapNotNull(array::optJSONObject)
            }.orEmpty()
        } else listOf(result)
        responses.forEach { response ->
            if (response.optString("type") != "math_ordering_drag") return@forEach
            val order = orders[response.optString("id")] ?: return@forEach
            val config = response.optJSONObject("config") ?: response
            val items = config.optJSONArray("items") ?: return@forEach
            val ids = (0 until items.length()).map { items.getJSONObject(it).getString("id") }
            if (order.size == ids.size && order.toSet() == ids.toSet()) {
                config.put("initialOrder", JSONArray(order))
            }
        }
        return result
    }
}
