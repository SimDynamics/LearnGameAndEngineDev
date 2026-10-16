package com.learngraphicsprogramming.app

import android.graphics.Color
import android.widget.TextView
import com.learngraphicsprogramming.app.narration.ReadingTextHost
import android.text.SpannableString
import android.text.Spanned
import android.text.style.ForegroundColorSpan
import com.learngraphicsprogramming.app.math.MathTextRenderer
import com.learngraphicsprogramming.app.math.MathTextSemantics
import com.learngraphicsprogramming.app.narration.NarrationLanguage
import com.learngraphicsprogramming.app.narration.LessonReadingColor
import com.learngraphicsprogramming.app.narration.ProgrammingTechnicalRanges
import com.learngraphicsprogramming.app.narration.ReadingTextRange
import com.learngraphicsprogramming.app.narration.ReadingTextView
import com.learngraphicsprogramming.app.narration.TechnicalDelimiterSpan

/** Presentation over the existing source offsets, shared with the narration frontends. */
internal object LessonTechnicalText {
    private class TechnicalInkSpan(color: Int) : ForegroundColorSpan(color)

    fun apply(view: TextView, mathematics: Boolean, language: NarrationLanguage, mode: String = "auto") {
        val original = view.text
        if (view !is ReadingTextHost || original.isNullOrEmpty() || view.isLongClickable || view.isTextSelectable) return
        if (mathematics) {
            val formulas = MathTextRenderer.formulaRanges(original)
            val ranges = MathTextSemantics.ranges(original, mode).map { ReadingTextRange(it.start, it.endExclusive) } +
                formulas.map { ReadingTextRange(it.first, it.last + 1) }
            val wrappers = MathTextSemantics.wrappers(original).filter { wrapper ->
                formulas.none { formula -> wrapper.start <= formula.last && wrapper.endExclusive > formula.first }
            }
            if (wrappers.isNotEmpty()) {
                val styled = SpannableString(original)
                styled.getSpans(0, styled.length, TechnicalDelimiterSpan::class.java).forEach(styled::removeSpan)
                wrappers.forEach { wrapper -> styled.setSpan(TechnicalDelimiterSpan(), wrapper.start,
                    wrapper.endExclusive, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE) }
                view.text = styled
            }
            view.setSemanticBackgroundRanges(ranges.distinct(), underlay(view.currentTextColor, 16))
            return
        }
        val analysis = ProgrammingTechnicalRanges.analyze(original, language)
        if (analysis.fragments.isEmpty()) return
        val styled = SpannableString(original)
        styled.getSpans(0, styled.length, TechnicalInkSpan::class.java).forEach(styled::removeSpan)
        styled.getSpans(0, styled.length, TechnicalDelimiterSpan::class.java).forEach(styled::removeSpan)
        for (range in analysis.fragments) {
            // The same lexer and exact palette used by the existing C++ consoles.
            // No monospace, scaling, padding, text or measurement changes are introduced.
            styled.setSpan(TechnicalInkSpan(Color.rgb(216, 216, 216)), range.start, range.endExclusive,
                Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            val syntax = CppSyntaxHighlighter.highlight(original.subSequence(range.start, range.endExclusive).toString())
            for (span in syntax.getSpans(0, syntax.length, ForegroundColorSpan::class.java)) {
                styled.setSpan(TechnicalInkSpan(span.foregroundColor), range.start + syntax.getSpanStart(span), range.start + syntax.getSpanEnd(span),
                    syntax.getSpanFlags(span))
            }
        }
        analysis.wrappers.forEach { range ->
            styled.setSpan(TechnicalDelimiterSpan(), range.start, range.endExclusive, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        }
        view.text = styled
        view.setSemanticBackgroundRanges(analysis.fragments.map { ReadingTextRange(it.start, it.endExclusive) },
            // Retain the console's exact palette. A light surface needs the console's
            // dark backing; the usual dark lesson surface only needs a discreet tint.
            if (LessonReadingColor.contrast(view.currentTextColor, Color.WHITE) >= LessonReadingColor.MIN_CONTRAST)
                Color.rgb(31, 35, 48) else underlay(view.currentTextColor, 8))
    }

    private fun underlay(foreground: Int, alpha: Int): Int = (foreground and 0x00ffffff) or (alpha shl 24)
}
