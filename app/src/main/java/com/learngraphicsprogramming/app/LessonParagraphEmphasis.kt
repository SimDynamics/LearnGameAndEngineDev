package com.learngraphicsprogramming.app

import android.graphics.Typeface
import android.text.SpannableString
import android.text.Spanned
import android.text.style.StyleSpan
import android.widget.TextView
import org.json.JSONArray
import org.json.JSONObject

/** Optional presentation over exact source fragments; never a second text or narration source. */
internal object LessonParagraphEmphasis {
    data class Range(val start: Int, val endExclusive: Int)

    private class EmphasisSpan : StyleSpan(Typeface.BOLD)

    /**
     * A fragment must occur exactly once unless an explicit one-based occurrence selects it.
     * Malformed/stale decoration is ignored, leaving all curricular characters available.
     */
    fun ranges(text: String, specification: Any?): List<Range> {
        val entries = specification as? JSONArray ?: return emptyList()
        val result = linkedSetOf<Range>()
        for (index in 0 until entries.length()) {
            val entry = entries.opt(index) as? JSONObject ?: continue
            val fragment = entry.opt("text") as? String ?: continue
            if (fragment.isBlank()) continue
            val occurrence = if (entry.has("occurrence")) {
                val number = entry.opt("occurrence") as? Number ?: continue
                val value = number.toDouble()
                if (!value.isFinite() || value < 1 || value > Int.MAX_VALUE || value != value.toInt().toDouble()) continue
                value.toInt()
            } else null
            val matches = mutableListOf<Int>()
            var offset = text.indexOf(fragment)
            while (offset >= 0) {
                val end = offset + fragment.length
                if (isBoundary(text, offset) && isBoundary(text, end)) matches += offset
                offset = text.indexOf(fragment, offset + 1)
            }
            val start = if (occurrence != null) matches.getOrNull(occurrence - 1)
                else matches.singleOrNull()
            if (start != null) result += Range(start, start + fragment.length)
        }
        return result.sortedWith(compareBy(Range::start, Range::endExclusive))
    }

    /** Copies existing spans and only replaces this decorator's own bold spans. */
    fun apply(view: TextView, specification: Any?) {
        val original = view.text ?: return
        val selected = ranges(original.toString(), specification)
        val previous = (original as? Spanned)?.getSpans(0, original.length, EmphasisSpan::class.java).orEmpty()
        if (selected.isEmpty() && previous.isEmpty()) return
        val styled = SpannableString(original)
        previous.forEach(styled::removeSpan)
        selected.forEach { range ->
            styled.setSpan(EmphasisSpan(), range.start, range.endExclusive, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        }
        view.text = styled
    }

    private fun isBoundary(text: String, index: Int): Boolean = index == 0 || index == text.length ||
        !(text[index - 1].isHighSurrogate() && text[index].isLowSurrogate())
}
