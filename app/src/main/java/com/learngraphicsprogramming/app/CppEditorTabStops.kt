package com.learngraphicsprogramming.app

import android.graphics.Canvas
import android.graphics.Paint
import android.text.Editable
import android.text.NoCopySpan
import android.text.Spanned
import android.text.TextPaint
import android.text.style.ReplacementSpan

/** Native tab/caret/selection layout; never expands a TAB into editable spaces. */
internal class CppEditorTabStops {
    private class Stop(val pixels: Int) : ReplacementSpan(), NoCopySpan {
        override fun getSize(paint: Paint, text: CharSequence, start: Int, end: Int,
                             metrics: Paint.FontMetricsInt?): Int {
            if (metrics != null) paint.getFontMetricsInt(metrics)
            return pixels
        }

        override fun draw(canvas: Canvas, text: CharSequence, start: Int, end: Int,
                          x: Float, top: Int, y: Int, bottom: Int, paint: Paint) = Unit
    }

    private var buffer: Editable? = null
    private var cellWidth = Float.NaN
    private var changing = false

    fun update(source: Editable, paint: TextPaint, edited: Boolean = false) {
        if (changing) return
        val width = paint.measureText(" ")
        if (!edited && buffer === source && width == cellWidth) return
        changing = true
        try {
            buffer = source
            cellWidth = width
            val old = source.getSpans(0, source.length, Stop::class.java).associateBy(source::getSpanStart).toMutableMap()
            if (width > 0f && width.isFinite()) {
                val tabs = CppTabColumns.tabs(source)
                val pixels = CppTabColumns.pixelWidths(tabs, width)
                tabs.forEachIndexed { index, tab ->
                    val existing = old.remove(tab.offset)
                    if (existing?.pixels != pixels[index] || source.getSpanEnd(existing) != tab.offset + 1) {
                        if (existing != null) source.removeSpan(existing)
                        // AOSP replaces the measured character of a ReplacementSpan
                        // internally, so native TAB handling cannot add a second jump.
                        // The actual Editable still contains exactly one original TAB.
                        source.setSpan(Stop(pixels[index]), tab.offset, tab.offset + 1, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                    }
                }
            }
            old.values.forEach(source::removeSpan)
        } finally {
            changing = false
        }
    }
}
