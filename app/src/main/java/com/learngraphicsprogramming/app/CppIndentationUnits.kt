package com.learngraphicsprogramming.app

import android.text.Editable
import android.text.SpannableString
import android.text.Spanned
import org.json.JSONArray
import org.json.JSONObject
import java.security.MessageDigest

/** Editing provenance only. Source characters, formatting and compiler input stay unchanged. */
internal object CppIndentationUnits {
    private class UnitMark
    private data class Prefix(val start: Int, val end: Int)

    fun loadedText(code: String, saved: String? = null, seedPredefined: Boolean = false): CharSequence {
        val result = SpannableString(code)
        val prefixes = prefixes(code)
        val starts = if (saved != null) restoredStarts(code, saved, prefixes) else if (seedPredefined) {
            buildList {
                for (prefix in prefixes) {
                    var cursor = prefix.start
                    while (cursor < prefix.end) {
                        if (code[cursor] == '\t') { cursor++; continue }
                        val start = cursor
                        while (cursor < prefix.end && code[cursor] == ' ') cursor++
                        for (offset in start until cursor - 3 step 4) add(offset)
                    }
                }
            }
        } else emptyList()
        starts.forEach { result.setSpan(UnitMark(), it, it + 4, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE) }
        return result
    }

    /** The existing draft owns the code; this optional sidecar stores only validated offsets. */
    fun snapshot(text: CharSequence): String = JSONObject().put("version", 1)
        .put("sha256", hash(text.toString())).put("starts", JSONArray(validStarts(text))).toString()

    /** Copy only the existing units of an automatically inherited indentation. Fresh marker
     * identities let the child and closing-brace lines each retain their own independent unit. */
    fun inheritedIndentation(text: CharSequence, start: Int, endExclusive: Int): CharSequence {
        val result = SpannableString(text.subSequence(start, endExclusive).toString())
        validStarts(text).filter { it >= start && it + 4 <= endExclusive }.forEach { original ->
            result.setSpan(UnitMark(), original - start, original - start + 4, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        }
        return result
    }

    /** Called before the actual edit. Only a collapsed backward deletion at an intact unit's
     * end can ask the existing input rules to remove that unit's remaining three spaces. */
    fun beforeEdit(text: Editable, start: Int, count: Int, after: Int,
                   selectionStart: Int, selectionEnd: Int): Int? {
        val prefixes = prefixes(text)
        var backwardUnit: Int? = null
        for (mark in text.getSpans(0, text.length, UnitMark::class.java)) {
            val first = text.getSpanStart(mark)
            val end = text.getSpanEnd(mark)
            if (end != first + 4 || !validStart(text, first, prefixes)) {
                text.removeSpan(mark)
                continue
            }
            val overlaps = count > 0 && start < end && start + count > first
            val insertsInside = count == 0 && after > 0 && start > first && start < end
            if (overlaps || insertsInside) {
                if (count == 1 && after == 0 && start + 1 == end &&
                    selectionStart == selectionEnd && selectionStart == end) backwardUnit = first
                text.removeSpan(mark)
            }
        }
        return backwardUnit
    }

    private fun restoredStarts(code: String, saved: String, prefixes: List<Prefix>): List<Int> = runCatching {
        val value = JSONObject(saved)
        require(value.opt("version") == 1 && value.opt("sha256") == hash(code))
        val starts = value.getJSONArray("starts")
        require(starts.length() <= code.length / 4)
        var previous = -4
        List(starts.length()) { index ->
            val number = starts.get(index)
            require(number is Int || number is Long)
            val offset = (number as Number).toLong()
            require(offset in 0..Int.MAX_VALUE.toLong())
            val start = offset.toInt()
            require(start >= previous + 4 && validStart(code, start, prefixes))
            previous = start
            start
        }
    }.getOrElse { emptyList() }

    private fun validStarts(text: CharSequence): List<Int> {
        val spanned = text as? Spanned ?: return emptyList()
        val prefixes = prefixes(text)
        var previous = -4
        return spanned.getSpans(0, text.length, UnitMark::class.java).mapNotNull { mark ->
            val start = spanned.getSpanStart(mark)
            start.takeIf { spanned.getSpanEnd(mark) == start + 4 && validStart(text, start, prefixes) }
        }.sorted().filter { start ->
            (start >= previous + 4).also { if (it) previous = start }
        }
    }

    private fun validStart(text: CharSequence, start: Int, prefixes: List<Prefix>): Boolean {
        if (start < 0 || start > text.length - 4 || (start until start + 4).any { text[it] != ' ' }) return false
        val found = prefixes.binarySearchBy(start) { it.start }
        val index = if (found >= 0) found else -found - 2
        return index >= 0 && start + 4 <= prefixes[index].end
    }

    private fun prefixes(text: CharSequence): List<Prefix> = buildList {
        val literals = CppCodeAnswerNormalizer.literalRanges(text.toString())
        var literalIndex = 0
        var cursor = 0
        while (cursor < text.length) {
            val start = cursor
            while (cursor < text.length && (text[cursor] == ' ' || text[cursor] == '\t')) cursor++
            while (literalIndex < literals.size && literals[literalIndex].last < start) literalIndex++
            // Leading whitespace within a raw/continued/incomplete literal is content,
            // even when it visually resembles an authored indentation level.
            val inLiteral = literalIndex < literals.size && literals[literalIndex].first < cursor
            if (cursor > start && !inLiteral) add(Prefix(start, cursor))
            while (cursor < text.length && text[cursor] != '\n') cursor++
            if (cursor < text.length) cursor++
        }
    }

    private fun hash(code: String): String = MessageDigest.getInstance("SHA-256")
        .digest(code.toByteArray(Charsets.UTF_8)).joinToString("") { "%02x".format(it.toInt() and 0xff) }
}
