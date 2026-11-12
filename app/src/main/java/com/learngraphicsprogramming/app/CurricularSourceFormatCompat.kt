package com.learngraphicsprogramming.app

import org.json.JSONObject

/**
 * Conservative physical-source formatting verifier for Android's original
 * state mapping. It never edits a student's source, answers or preferences.
 *
 * A changed source is accepted only with the exact pre-format block archived
 * by stateLayout, and only when C++/CMake tokens and protected syntax match.
 */
internal object CurricularSourceFormatCompat {
    private enum class Language { CPP, CMAKE, IMMUTABLE }
    private val spaces = " \t\r\n\u000b\u000c"
    private val rawPrefixes = listOf("u8R\"", "uR\"", "UR\"", "LR\"", "R\"")
    private val quotePrefixes = listOf("u8\"", "u\"", "U\"", "L\"", "\"",
        "u8'", "u'", "U'", "L'", "'")
    private val operators = listOf("%:%:", "<<=", ">>=", "<=>", "->*", "...", "##", "::",
        "->", ".*", "++", "--", "==", "!=", "<=", ">=", "&&", "||", "<<", ">>", "+=", "-=",
        "*=", "/=", "%=", "&=", "|=", "^=", "<:", ":>", "<%", "%>", "%:")
    private val codeExtensions = setOf("cpp", "cc", "cxx", "c", "h", "hpp", "hh", "hxx", "inl", "ixx")

    private fun space(c: Char): Boolean = c in spaces
    private fun identifier(c: Char): Boolean =
        c == '_' || c.isLetterOrDigit() || c.code >= 128

    private fun suffixEnd(text: String, start: Int): Int {
        var end = start
        if (end < text.length && text[end] == '_') {
            end++
            while (end < text.length && identifier(text[end])) end++
        }
        return end
    }

    private fun quoteEnd(text: String, opening: Int): Int? {
        val quote = text.getOrNull(opening) ?: return null
        var pos = opening + 1
        while (pos < text.length) {
            when (text[pos]) {
                '\\' -> {
                    pos += 2
                    if (pos > text.length) return null
                }
                quote -> return pos + 1
                else -> pos++
            }
        }
        return null
    }

    private fun cppTokens(code: String): List<String>? {
        val tokens = mutableListOf<String>()
        var i = 0
        while (i < code.length) {
            if (space(code[i])) { i++; continue }
            val line = code.lastIndexOf('\n', i - 1) + 1
            if (code[i] == '#' && code.substring(line, i).all(::space)) {
                var end = i
                while (true) {
                    val lf = code.indexOf('\n', end)
                    if (lf < 0) { end = code.length; break }
                    val physical = code.substring(end, lf).removeSuffix("\r")
                    end = lf + 1
                    if (!physical.endsWith("\\")) break
                }
                tokens += "directive:" + code.substring(i, end).trimEnd('\r', '\n')
                i = end
                continue
            }
            if (code.startsWith("//", i)) {
                val lf = code.indexOf('\n', i + 2).let { if (it < 0) code.length else it }
                tokens += "comment:" + code.substring(i, lf).removeSuffix("\r")
                i = lf
                continue
            }
            if (code.startsWith("/*", i)) {
                val end = code.indexOf("*/", i + 2)
                if (end < 0) return null
                tokens += "comment:" + code.substring(i, end + 2)
                i = end + 2
                continue
            }
            val raw = rawPrefixes.firstOrNull { code.startsWith(it, i) }
            if (raw != null) {
                val begin = i + raw.length
                val paren = code.indexOf('(', begin)
                if (paren < 0 || paren - begin > 16) return null
                val marker = code.substring(begin, paren)
                if (marker.any { space(it) || it == '\\' || it == '(' || it == ')' || it.code !in 0x21..0x7e }) return null
                val closing = ")" + marker + "\""
                val at = code.indexOf(closing, paren + 1)
                if (at < 0) return null
                val end = suffixEnd(code, at + closing.length)
                tokens += "literal:" + code.substring(i, end)
                i = end
                continue
            }
            val quoted = quotePrefixes.firstOrNull { code.startsWith(it, i) }
            if (quoted != null) {
                val end = quoteEnd(code, i + quoted.length - 1) ?: return null
                val suffix = suffixEnd(code, end)
                tokens += "literal:" + code.substring(i, suffix)
                i = suffix
                continue
            }
            if (identifier(code[i]) && !code[i].isDigit()) {
                var end = i + 1
                while (end < code.length && identifier(code[end])) end++
                tokens += "id:" + code.substring(i, end)
                i = end
                continue
            }
            if (code[i] in '0'..'9' || (code[i] == '.' &&
                code.getOrNull(i + 1)?.let { it in '0'..'9' } == true)) {
                var end = i + 1
                while (end < code.length) {
                    val next = code[end]
                    when {
                        identifier(next) || next == '.' || next == '\'' -> end++
                        (next == '+' || next == '-') &&
                            code[end - 1] in "eEpP" -> end++
                        else -> break
                    }
                }
                tokens += "number:" + code.substring(i, end)
                i = end
                continue
            }
            val op = operators.firstOrNull { code.startsWith(it, i) }
            if (op != null) {
                tokens += "op:" + op
                i += op.length
            } else {
                tokens += "op:" + code[i]
                i++
            }
        }
        return tokens
    }

    private fun cmakeTokens(source: String): List<String>? {
        val tokens = mutableListOf<String>()
        var i = 0
        while (i < source.length) {
            if (space(source[i])) { i++; continue }
            val comment = source[i] == '#'
            val start = i + if (comment) 1 else 0
            if (start < source.length && source[start] == '[') {
                var equal = start + 1
                while (equal < source.length && source[equal] == '=') equal++
                if (equal < source.length && source[equal] == '[') {
                    val endMarker = "]" + source.substring(start + 1, equal) + "]"
                    val endStart = source.indexOf(endMarker, equal + 1)
                    if (endStart < 0) return null
                    val end = endStart + endMarker.length
                    tokens += "bracket:" + source.substring(i, end)
                    i = end
                    continue
                }
            }
            if (comment) {
                val lf = source.indexOf('\n', i).let { if (it < 0) source.length else it }
                tokens += "comment:" + source.substring(i, lf).removeSuffix("\r")
                i = lf
                continue
            }
            if (source[i] == '"') {
                val end = quoteEnd(source, i) ?: return null
                tokens += "quoted:" + source.substring(i, end)
                i = end
                continue
            }
            if (source[i] == '(' || source[i] == ')') {
                tokens += "paren:" + source[i]
                i++
                continue
            }
            var end = i
            while (end < source.length && !space(source[end]) &&
                source[end] !in "()#\"") {
                if (source[end] == '\\') {
                    if (end + 1 >= source.length) return null
                    end += 2
                } else end++
            }
            if (end == i) return null
            tokens += "word:" + source.substring(i, end)
            i = end
        }
        return tokens
    }

    private fun sourceLanguage(block: JSONObject): Language {
        return when (block.optString("language", "cpp").trim().lowercase()) {
            "cpp", "c++", "cxx", "c", "h", "hpp" -> Language.CPP
            "cmake" -> Language.CMAKE
            else -> Language.IMMUTABLE
        }
    }

    private fun fileLanguage(path: String): Language {
        if (path.substringAfterLast('/') == "CMakeLists.txt" || path.endsWith(".cmake")) return Language.CMAKE
        if (path.substringAfterLast('.', "") in codeExtensions) return Language.CPP
        return Language.IMMUTABLE
    }

    private fun equivalent(old: String, current: String, language: Language): Boolean {
        if (old == current) return true
        if (language == Language.IMMUTABLE || old.contains("\\\n") || current.contains("\\\n") ||
            old.contains("\\\r\n") || current.contains("\\\r\n")) return false
        if (language == Language.CPP &&
            listOf("__LINE__", "source_location").any { old.contains(it) || current.contains(it) }) return false
        if (language == Language.CMAKE &&
            (old.contains("CMAKE_CURRENT_LIST_LINE") || current.contains("CMAKE_CURRENT_LIST_LINE"))) return false
        val original = if (language == Language.CMAKE) cmakeTokens(old) else cppTokens(old)
        val revised = if (language == Language.CMAKE) cmakeTokens(current) else cppTokens(current)
        return original != null && original == revised
    }

    fun project(actual: JSONObject, original: JSONObject): JSONObject {
        val type = actual.getString("type")
        require(type == original.getString("type"))
        val projected = JSONObject(actual.toString())
        fun stringField(field: String, language: Language) {
            require(actual.has(field) == original.has(field))
            if (!original.has(field)) return
            val before = original.get(field) as? String ?: error("Non-string original source")
            val after = actual.get(field) as? String ?: error("Non-string formatted source")
            require(equivalent(before, after, language))
            projected.put(field, before)
        }
        val language = sourceLanguage(actual)
        stringField("code", language)
        if (type == "inline_fill_code" || type == "drag_fill_code") stringField("template", language)
        if (type == "cpp_compiler") {
            stringField("initialCode", Language.CPP)
            require(actual.has("projectFiles") == original.has("projectFiles"))
            if (original.has("projectFiles")) {
                val old = original.getJSONArray("projectFiles")
                val now = actual.getJSONArray("projectFiles")
                val output = projected.getJSONArray("projectFiles")
                require(old.length() == now.length())
                for (i in 0 until old.length()) {
                    val first = old.getJSONObject(i)
                    val second = now.getJSONObject(i)
                    val path = first.getString("path")
                    require(path == second.getString("path"))
                    val before = first.getString("initialCode")
                    val after = second.getString("initialCode")
                    require(equivalent(before, after, fileLanguage(path)))
                    output.getJSONObject(i).put("initialCode", before)
                }
            }
        }
        for (field in listOf("fragments", "choices")) {
            val expectedType = if (field == "fragments") "order_code" else "drag_fill_code"
            if (type != expectedType) continue
            require(actual.has(field) == original.has(field))
            if (!original.has(field)) continue
            val before = original.getJSONArray(field)
            val after = actual.getJSONArray(field)
            require(before.length() == after.length())
            for (i in 0 until before.length()) {
                require(equivalent(before.getString(i), after.getString(i), language))
            }
            projected.put(field, org.json.JSONArray(before.toString()))
        }
        return projected
    }
}
