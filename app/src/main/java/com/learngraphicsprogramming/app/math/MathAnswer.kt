package com.learngraphicsprogramming.app.math

import org.json.JSONArray
import org.json.JSONObject

data class MathPoint(val x: Double, val y: Double)

/** Typed responses are independent of Views and can be saved alongside lesson progress. */
sealed class MathAnswer {
    data class Text(val value: String) : MathAnswer()
    data class Choice(val selected: Set<String>) : MathAnswer()
    data class Fields(val values: Map<String, String>) : MathAnswer()
    data class Order(val ids: List<String>) : MathAnswer()
    data class Matching(val pairs: Map<String, String>) : MathAnswer()
    data class NumberLine(val value: Double, val closed: Boolean, val direction: String) : MathAnswer()
    data class Sliders(val values: Map<String, Double>) : MathAnswer()
    data class Graph(val points: List<MathPoint>) : MathAnswer()
    data class RigidAngle(val placement: RigidAnglePlacement) : MathAnswer()
}

object MathAnswerJson {
    fun encode(answer: MathAnswer): JSONObject = JSONObject().apply {
        when (answer) {
            is MathAnswer.RigidAngle -> put("type", "rigid_angle").put("x", answer.placement.x)
                .put("y", answer.placement.y).put("rotation", answer.placement.rotation)
            is MathAnswer.Text -> put("type", "text").put("value", answer.value)
            is MathAnswer.Choice -> put("type", "choice").put("selected", JSONArray(answer.selected.sorted()))
            is MathAnswer.Fields -> put("type", "fields").put("values", JSONObject(answer.values))
            is MathAnswer.Order -> put("type", "order").put("ids", JSONArray(answer.ids))
            is MathAnswer.Matching -> put("type", "matching").put("pairs", JSONObject(answer.pairs))
            is MathAnswer.NumberLine -> {
                require(answer.value.isFinite()) { "La posición debe ser finita." }
                put("type", "number_line").put("value", answer.value)
                    .put("closed", answer.closed).put("direction", answer.direction)
            }
            is MathAnswer.Sliders -> {
                require(answer.values.values.all { it.isFinite() }) { "Los sliders deben ser finitos." }
                put("type", "sliders").put("values", JSONObject(answer.values))
            }
            is MathAnswer.Graph -> {
                require(answer.points.all { it.x.isFinite() && it.y.isFinite() }) { "Los puntos deben ser finitos." }
                put("type", "graph").put("points", JSONArray().apply {
                    answer.points.forEach { put(JSONObject().put("x", it.x).put("y", it.y)) }
                })
            }
        }
    }

    /** Corrupted or old state returns null so the renderer can use its normal initial answer. */
    fun decode(json: JSONObject): MathAnswer? = try {
        when (json.getString("type")) {
            "rigid_angle" -> MathAnswer.RigidAngle(RigidAnglePlacement(json.getDouble("x"), json.getDouble("y"), json.getDouble("rotation")))
            "text" -> MathAnswer.Text(json.getString("value"))
            "choice" -> MathAnswer.Choice(strings(json.getJSONArray("selected")).toSet())
            "fields" -> MathAnswer.Fields(stringMap(json.getJSONObject("values")))
            "order" -> MathAnswer.Order(strings(json.getJSONArray("ids")))
            "matching" -> MathAnswer.Matching(stringMap(json.getJSONObject("pairs")))
            "number_line" -> MathAnswer.NumberLine(
                finite(json.getDouble("value")), json.getBoolean("closed"), json.getString("direction")
            )
            "sliders" -> {
                val values = json.getJSONObject("values")
                MathAnswer.Sliders(values.keys().asSequence().associateWith { finite(values.getDouble(it)) })
            }
            "graph" -> {
                val values = json.getJSONArray("points")
                MathAnswer.Graph((0 until values.length()).map {
                    val point = values.getJSONObject(it)
                    MathPoint(finite(point.getDouble("x")), finite(point.getDouble("y")))
                })
            }
            else -> null
        }
    } catch (_: Exception) { null }

    private fun finite(value: Double): Double {
        require(value.isFinite())
        return value
    }

    private fun strings(array: JSONArray): List<String> = (0 until array.length()).map {
        val value = array.get(it)
        require(value is String)
        value
    }

    private fun stringMap(obj: JSONObject): Map<String, String> = obj.keys().asSequence().associateWith {
        val value = obj.get(it)
        require(value is String)
        value
    }
}
