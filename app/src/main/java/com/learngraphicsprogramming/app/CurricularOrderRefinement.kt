package com.learngraphicsprogramming.app

import org.json.JSONArray
import org.json.JSONObject

/**
 * Exact, source-derived order refinement for the published stateLayout v2.
 * A grouped historical card becomes consecutive physical cards. Persisted
 * answer indices are *not* migrated or rewritten: the presentation alone
 * expands an old order when the historical fragment count still matches.
 */
internal data class CurricularOrderRefinement(
    val oldFragmentCount: Int,
    val expandedFragmentCount: Int,
    val groups: List<List<Int>>
) {
    fun present(saved: List<Int>): List<Int> {
        if (saved.size != oldFragmentCount || saved.toSet() != (0 until oldFragmentCount).toSet())
            return saved
        return saved.flatMap { groups[it] }
    }

    companion object {
        private fun values(array: JSONArray): List<String> =
            (0 until array.length()).map { array.get(it) as? String
                ?: throw IllegalArgumentException("Order fragment must be a string") }

        private fun permutation(array: JSONArray, size: Int): List<Int> {
            val values = (0 until array.length()).map {
                array.get(it) as? Int ?: throw IllegalArgumentException("Order index must be integer")
            }
            require(values.size == size && values.sorted() == (0 until size).toList())
            return values
        }

        private fun noPhysicalMacroOrRawLiteral(source: String) {
            require(!source.contains("\\\n") && !source.contains("\\\r\n"))
            var at = source.indexOf("R\"")
            while (at >= 0) {
                val opening = source.indexOf('(', at + 2)
                if (opening >= 0 && opening - at <= 19) {
                    val delimiter = source.substring(at + 2, opening)
                    if (delimiter.length <= 16 &&
                        delimiter.none { it.code < 33 || it.code > 126 || it in "()\\" }) {
                        val ending = source.indexOf(")$delimiter\"", opening + 1)
                        require(ending >= 0 && '\n' !in source.substring(at, ending))
                        at = ending + delimiter.length + 2
                        at = source.indexOf("R\"", at)
                        continue
                    }
                }
                at = source.indexOf("R\"", at + 2)
            }
        }

        private fun splitOriginal(fragment: String): List<String> {
            val pieces = if (fragment == "if (validar(candidata)) { tabla = candidata; }")
                listOf("if (validar(candidata)) {", " tabla = candidata;", " }")
            else fragment.replace("\r\n", "\n").split("\n")
            require(pieces.none { '\r' in it })
            val nonBlank = pieces.filter { it.trim(' ', '\t').isNotEmpty() }
            require(nonBlank.isNotEmpty())
            return nonBlank
        }

        fun project(active: JSONObject, original: JSONObject): Pair<JSONObject, CurricularOrderRefinement> {
            require(original.getString("type") == "order_code" &&
                active.getString("type") == "order_code")
            val originalFragments = values(original.getJSONArray("fragments"))
            val originalOrder = permutation(original.getJSONArray("correctOrder"), originalFragments.size)
            require(originalFragments.isNotEmpty() && originalFragments.size <= 100_000 &&
                originalFragments.all { it.isNotEmpty() })
            noPhysicalMacroOrRawLiteral(originalOrder.joinToString("\n") { originalFragments[it] })
            val expanded = mutableListOf<String>()
            val groups = mutableListOf<List<Int>>()
            for (fragment in originalFragments) {
                val lines = splitOriginal(fragment)
                groups.add((expanded.size until expanded.size + lines.size).toList())
                expanded.addAll(lines)
            }
            val canonical = originalOrder.flatMap { groups[it] }
            require(expanded.size <= 100_000 &&
                (expanded != originalFragments || canonical != originalOrder))
            require(values(active.getJSONArray("fragments")) == expanded)
            require(permutation(active.getJSONArray("correctOrder"), expanded.size) == canonical)
            val restored = JSONObject(active.toString())
            restored.put("fragments", JSONArray(original.getJSONArray("fragments").toString()))
            restored.put("correctOrder", JSONArray(original.getJSONArray("correctOrder").toString()))
            return restored to CurricularOrderRefinement(originalFragments.size, expanded.size, groups)
        }
    }
}
