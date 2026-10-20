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

    enum class Style(val typefaceStyle: Int) {
        BOLD(Typeface.BOLD), ITALIC(Typeface.ITALIC), BOLD_ITALIC(Typeface.BOLD_ITALIC)
    }

    data class StyledRange(val start: Int, val endExclusive: Int, val style: Style)

    private class EmphasisSpan(style: Style) : StyleSpan(style.typefaceStyle)

    /**
     * A fragment must occur exactly once unless an explicit one-based occurrence selects it.
     * Malformed/stale decoration is ignored, leaving all curricular characters available.
     */
    fun ranges(text: String, specification: Any?, sourceRange: Range = Range(0, text.length)): List<Range> {
        return styledRanges(text, specification, sourceRange).map { Range(it.start, it.endExclusive) }.distinct()
    }

    /** Missing style preserves the original bold contract; unsupported styles are decoration only. */
    fun styledRanges(text: String, specification: Any?, sourceRange: Range = Range(0, text.length)): List<StyledRange> {
        if (sourceRange.start < 0 || sourceRange.endExclusive < sourceRange.start || sourceRange.endExclusive > text.length ||
            !isBoundary(text, sourceRange.start) || !isBoundary(text, sourceRange.endExclusive)) return emptyList()
        val source = text.substring(sourceRange.start, sourceRange.endExclusive)
        val entries = specification as? JSONArray ?: return emptyList()
        val result = linkedSetOf<StyledRange>()
        for (index in 0 until entries.length()) {
            val entry = entries.opt(index) as? JSONObject ?: continue
            val style = if (!entry.has("style")) Style.BOLD else when (entry.opt("style")) {
                "bold" -> Style.BOLD
                "italic" -> Style.ITALIC
                "bold_italic" -> Style.BOLD_ITALIC
                else -> continue
            }
            val fragment = entry.opt("text") as? String ?: continue
            if (fragment.isBlank()) continue
            val occurrence = if (entry.has("occurrence")) {
                val number = entry.opt("occurrence") as? Number ?: continue
                val value = number.toDouble()
                if (!value.isFinite() || value < 1 || value > Int.MAX_VALUE || value != value.toInt().toDouble()) continue
                value.toInt()
            } else null
            val matches = mutableListOf<Int>()
            var offset = source.indexOf(fragment)
            while (offset >= 0) {
                val end = offset + fragment.length
                if (isBoundary(source, offset) && isBoundary(source, end)) matches += offset
                offset = source.indexOf(fragment, offset + 1)
            }
            val start = if (occurrence != null) matches.getOrNull(occurrence - 1)
                else matches.singleOrNull()
            if (start != null) result += StyledRange(sourceRange.start + start, sourceRange.start + start + fragment.length, style)
        }
        return result.sortedWith(compareBy(StyledRange::start, StyledRange::endExclusive, { it.style.ordinal }))
    }

    /** Copies existing spans and only replaces this decorator's own style spans. */
    fun apply(view: TextView, specification: Any?, sourceRange: Range? = null) {
        val original = view.text ?: return
        val selected = styledRanges(original.toString(), specification, sourceRange ?: Range(0, original.length))
        val previous = (original as? Spanned)?.getSpans(0, original.length, EmphasisSpan::class.java).orEmpty()
        if (selected.isEmpty() && previous.isEmpty()) return
        val styled = SpannableString(original)
        previous.forEach(styled::removeSpan)
        selected.forEach { range ->
            styled.setSpan(EmphasisSpan(range.style), range.start, range.endExclusive, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        }
        view.text = styled
    }

    private fun isBoundary(text: String, index: Int): Boolean = index == 0 || index == text.length ||
        !(text[index - 1].isHighSurrogate() && text[index].isLowSurrogate())
}
