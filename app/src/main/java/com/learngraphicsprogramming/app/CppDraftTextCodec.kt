package com.learngraphicsprogramming.app

import org.json.JSONObject

/** Keep newline-ending draft text escaped inside its existing preference value.
 * Legacy Android XML writers can otherwise add closing-tag indentation to it.
 * The separate format marker prevents interpreting ordinary legacy JSON as storage.
 */
internal object CppDraftTextCodec {
    const val ENCODING = "json-v1"

    fun encode(text: String, field: String): String =
        JSONObject().put("version", 1).put(field, text).toString()

    fun decode(stored: String, field: String): String? = runCatching {
        val value = JSONObject(stored)
        require(value.opt("version") == 1)
        value.get(field) as String
    }.getOrNull()
}
