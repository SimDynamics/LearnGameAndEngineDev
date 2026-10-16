package com.learngraphicsprogramming.app.narration

import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Path
import android.graphics.PorterDuff
import android.graphics.PorterDuffColorFilter
import android.graphics.Region
import android.text.Editable
import android.text.Spanned
import android.text.style.BackgroundColorSpan
import android.text.style.LineBackgroundSpan
import android.text.style.ReplacementSpan
import android.widget.TextView
import android.widget.RadioButton

/** Display offsets into the TextView's existing CharSequence; no curricular text is retained. */
data class ReadingTextRange(val start: Int, val endExclusive: Int)

/** A trusted inline-code delimiter keeps its source offset without drawing a glyph. */
internal class TechnicalDelimiterSpan : ReplacementSpan() {
    override fun getSize(paint: Paint, text: CharSequence, start: Int, end: Int,
                         fm: Paint.FontMetricsInt?): Int = 0

    override fun draw(canvas: Canvas, text: CharSequence, start: Int, end: Int, x: Float,
                      top: Int, y: Int, bottom: Int, paint: Paint) = Unit
}

/**
 * Recolors only the drawing of reliably aligned prose. Its existing CharSequence, spans,
 * Layout, colors and measurements remain owned by TextView and are never replaced.
 */
interface ReadingTextHost {
    val readingInk: ReadingTextInk
    val safeSupportsWordReading: Boolean get() = readingInk.safeSupportsWordReading
    fun setSemanticBackgroundRanges(ranges: List<ReadingTextRange>, color: Int) = readingInk.setSemanticBackgroundRanges(ranges, color)
    fun setUnreadWordRanges(ranges: List<ReadingTextRange>?, color: Int, semanticColor: Int = color) = readingInk.setUnreadWordRanges(ranges, color, semanticColor)
}

open class ReadingTextView(context: Context) : TextView(context), ReadingTextHost {
    override val readingInk = ReadingTextInk(this)
    internal fun effectiveReadingSurface(background: Int): Int = readingInk.effectiveReadingSurface(background)
    override fun onDraw(canvas: Canvas) = readingInk.draw(canvas) { super.onDraw(canvas) }
}

/** Native option control: only label glyphs are shaded; its indicator and click behavior stay native. */
class ReadingRadioButton(context: Context) : RadioButton(context), ReadingTextHost {
    override val readingInk = ReadingTextInk(this, interactiveLabel = true)
    override fun onDraw(canvas: Canvas) = readingInk.draw(canvas) { super.onDraw(canvas) }
}

class ReadingTextInk(private val view: TextView, private val interactiveLabel: Boolean = false) {
    private var unreadRanges: List<ReadingTextRange> = emptyList()
    private var rangeText: CharSequence? = null
    private var rangeLength = 0
    private var unreadColor = 0
    private var semanticUnreadColor = 0
    private val unreadPaint = Paint()
    private val semanticUnreadPaint = Paint()
    private val unreadPath = Path()
    private val rangePath = Path()
    private var semanticBackgroundRanges: List<ReadingTextRange> = emptyList()
    private var semanticBackgroundText: CharSequence? = null
    private var semanticBackgroundLength = 0
    private val semanticBackgroundPaint = Paint()
    private val semanticBackgroundPath = Path()

    /** Ambiguous display offsets or non-text drawing retain their ordinary presentation. */
    val safeSupportsWordReading: Boolean
        get() {
            val content = view.text ?: return false
            if (content.isEmpty() || content is Editable || (!interactiveLabel && view.isClickable) || view.isLongClickable ||
                view.isTextSelectable || view.selectionStart >= 0 || view.selectionEnd >= 0 ||
                view.movementMethod != null || view.transformationMethod != null || view.ellipsize != null ||
                view.scrollX != 0 || view.scrollY != 0 ||
                view.compoundDrawables.any { it != null } || view.compoundDrawablesRelative.any { it != null }) return false
            if (content is Spanned && (
                    content.getSpans(0, content.length, ReplacementSpan::class.java).any { it !is TechnicalDelimiterSpan } ||
                    content.getSpans(0, content.length, BackgroundColorSpan::class.java).isNotEmpty() ||
                    content.getSpans(0, content.length, LineBackgroundSpan::class.java).isNotEmpty())) return false
            return true
        }

    /** Decorative ink under the existing native layout; never part of the pending glyph layer. */
    fun setSemanticBackgroundRanges(ranges: List<ReadingTextRange>, color: Int) {
        val content = view.text
        val valid = if (color ushr 24 == 0 || ranges.any {
                it.start < 0 || it.endExclusive <= it.start || it.endExclusive > content.length ||
                    !isUtf16Boundary(content, it.start) || !isUtf16Boundary(content, it.endExclusive)
            }) emptyList() else ranges.toList()
        val nextText = content.takeIf { valid.isNotEmpty() }
        if (semanticBackgroundRanges == valid && semanticBackgroundText === nextText &&
            semanticBackgroundLength == content.length && semanticBackgroundPaint.color == color) return
        semanticBackgroundRanges = valid
        semanticBackgroundText = nextText
        semanticBackgroundLength = content.length
        semanticBackgroundPaint.color = color
        view.invalidate()
    }

    /** Pending ink must also contrast with the subtle mathematical underlay. */
    internal fun effectiveReadingSurface(background: Int): Int =
        if (semanticBackgroundText === view.text && semanticBackgroundLength == view.text.length && semanticBackgroundRanges.isNotEmpty())
            LessonReadingColor.composite(semanticBackgroundPaint.color, background) else background

    /** Null, empty or untrustworthy ranges restore ordinary TextView drawing immediately. */
    fun setUnreadWordRanges(ranges: List<ReadingTextRange>?, color: Int, semanticColor: Int = color) {
        val content = view.text
        val normalized = if (ranges.isNullOrEmpty() || !safeSupportsWordReading ||
            color ushr 24 != 255 || semanticColor ushr 24 != 255) {
            emptyList()
        } else if (ranges.any { range ->
                range.start < 0 || range.endExclusive <= range.start || range.endExclusive > content.length ||
                    !isUtf16Boundary(content, range.start) || !isUtf16Boundary(content, range.endExclusive)
            }) {
            emptyList()
        } else {
            val result = mutableListOf<ReadingTextRange>()
            ranges.sortedBy { it.start }.forEach { range ->
                val previous = result.lastOrNull()
                if (previous != null && range.start <= previous.endExclusive) {
                    result[result.lastIndex] = ReadingTextRange(previous.start, maxOf(previous.endExclusive, range.endExclusive))
                } else result += range
            }
            result.toList()
        }
        val nextText = content.takeIf { normalized.isNotEmpty() }
        if (unreadRanges == normalized && rangeText === nextText &&
            rangeLength == content.length && unreadColor == color && semanticUnreadColor == semanticColor) return
        unreadRanges = normalized
        rangeText = nextText
        rangeLength = content.length
        if (unreadColor != color) {
            unreadColor = color
            unreadPaint.colorFilter = PorterDuffColorFilter(color, PorterDuff.Mode.SRC_IN)
        }
        if (semanticUnreadColor != semanticColor) {
            semanticUnreadColor = semanticColor
            semanticUnreadPaint.colorFilter = PorterDuffColorFilter(semanticColor, PorterDuff.Mode.SRC_IN)
        }
        view.invalidate()
    }

    @Suppress("DEPRECATION") // DIFFERENCE supports the application's API 24 and 25 devices.
    fun draw(canvas: Canvas, drawOriginal: () -> Unit) {
        val existingLayout = view.layout
        if (semanticBackgroundText !== view.text || semanticBackgroundLength != view.text.length) {
            semanticBackgroundRanges = emptyList()
            semanticBackgroundText = null
            semanticBackgroundLength = 0
        }
        if (existingLayout != null && semanticBackgroundRanges.isNotEmpty()) {
            semanticBackgroundPath.reset()
            semanticBackgroundRanges.forEach { range ->
                rangePath.reset()
                existingLayout.getSelectionPath(range.start, range.endExclusive, rangePath)
                semanticBackgroundPath.addPath(rangePath)
            }
            semanticBackgroundPath.offset(view.compoundPaddingLeft.toFloat(), view.totalPaddingTop.toFloat())
            canvas.drawPath(semanticBackgroundPath, semanticBackgroundPaint)
        }
        if (unreadRanges.isEmpty() || rangeText !== view.text || rangeLength != view.text.length ||
            existingLayout == null || !safeSupportsWordReading) {
            drawOriginal()
            return
        }
        unreadPath.reset()
        unreadRanges.forEach { range ->
            rangePath.reset()
            existingLayout.getSelectionPath(range.start, range.endExclusive, rangePath)
            unreadPath.addPath(rangePath)
        }
        unreadPath.offset(view.compoundPaddingLeft.toFloat(), view.totalPaddingTop.toFloat())

        // Complementary clips avoid painting gray under normal glyphs or changing their edges.
        val originalSave = canvas.save()
        try {
            canvas.clipPath(unreadPath, Region.Op.DIFFERENCE)
            drawOriginal()
        } finally {
            canvas.restoreToCount(originalSave)
        }
        val separateSemanticInk = semanticBackgroundRanges.isNotEmpty() && semanticUnreadColor != unreadColor
        drawUnreadInk(canvas, unreadPaint, if (separateSemanticInk) false else null, drawOriginal)
        if (separateSemanticInk) drawUnreadInk(canvas, semanticUnreadPaint, true, drawOriginal)
    }

    @Suppress("DEPRECATION")
    private fun drawUnreadInk(canvas: Canvas, paint: Paint, insideSemanticBackground: Boolean?, drawOriginal: () -> Unit) {
        val unreadSave = canvas.save()
        try {
            canvas.clipPath(unreadPath)
            if (insideSemanticBackground == true) canvas.clipPath(semanticBackgroundPath)
            else if (insideSemanticBackground == false) canvas.clipPath(semanticBackgroundPath, Region.Op.DIFFERENCE)
            val layerSave = canvas.saveLayer(0f, 0f, view.width.toFloat(), view.height.toFloat(), paint)
            try {
                drawOriginal()
            } finally {
                canvas.restoreToCount(layerSave)
            }
        } finally {
            canvas.restoreToCount(unreadSave)
        }
    }

    private fun isUtf16Boundary(content: CharSequence, offset: Int): Boolean =
        offset == 0 || offset == content.length ||
            !(content[offset - 1].isHighSurrogate() && content[offset].isLowSurrogate())
}
