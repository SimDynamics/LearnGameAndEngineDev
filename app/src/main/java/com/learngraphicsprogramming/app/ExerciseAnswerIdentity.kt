package com.learngraphicsprogramming.app

import org.json.JSONArray
import org.json.JSONObject
import java.math.BigDecimal
import java.security.MessageDigest
import java.util.IdentityHashMap

/** Stable response identity, independent of JSON/map insertion order and bounded to 64 characters. */
object ExerciseAnswerIdentity {
    fun of(value: Any?): String {
        val canonical = canonical(value, IdentityHashMap(), 0)
        return MessageDigest.getInstance("SHA-256").digest(canonical.toByteArray(Charsets.UTF_8))
            .joinToString("") { byte -> (byte.toInt() and 0xff).toString(16).padStart(2, '0') }
    }

    private fun canonical(value: Any?, ancestors: IdentityHashMap<Any, Boolean>, depth: Int): String {
        require(depth <= 128) { "La respuesta tiene demasiados niveles." }
        if (value == null || value === JSONObject.NULL) return "null"
        return when (value) {
            is String -> JSONObject.quote(value)
            is Boolean -> value.toString()
            is Number -> BigDecimal(value.toString()).stripTrailingZeros().toString()
            else -> {
                require(ancestors.put(value, true) == null) { "La respuesta contiene una referencia circular." }
                try {
                    when (value) {
                        is JSONObject -> value.keys().asSequence().toList().sorted().joinToString(",", "{", "}") {
                            JSONObject.quote(it) + ":" + canonical(value.get(it), ancestors, depth + 1)
                        }
                        is JSONArray -> (0 until value.length()).joinToString(",", "[", "]") {
                            canonical(value.get(it), ancestors, depth + 1)
                        }
                        is Map<*, *> -> {
                            require(value.keys.all { it is String }) { "Las claves de la respuesta deben ser texto." }
                            value.keys.map { it as String }.sorted().joinToString(",", "{", "}") {
                                JSONObject.quote(it) + ":" + canonical(value[it], ancestors, depth + 1)
                            }
                        }
                        is Set<*> -> value.map { canonical(it, ancestors, depth + 1) }.sorted().joinToString(",", "[", "]")
                        is Iterable<*> -> value.joinToString(",", "[", "]") { canonical(it, ancestors, depth + 1) }
                        else -> {
                            require(value.javaClass.isArray) { "Tipo de respuesta no soportado: ${value.javaClass.simpleName}" }
                            (0 until java.lang.reflect.Array.getLength(value)).joinToString(",", "[", "]") {
                                canonical(java.lang.reflect.Array.get(value, it), ancestors, depth + 1)
                            }
                        }
                    }
                } finally {
                    ancestors.remove(value)
                }
            }
        }
    }
}
