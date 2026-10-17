package com.learngraphicsprogramming.app.math

import com.learngraphicsprogramming.app.R
import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF
import android.view.View
import android.view.ViewGroup
import android.widget.LinearLayout
import org.json.JSONObject
import kotlin.math.max

/** Native Euclidean reading surface. It intentionally has no axes, grid, ticks or coordinates. */
class MathEuclideanDiagramView(context: Context, config: JSONObject, private val accent: Int) :
    View(context), MathTextScaleAware, MathFullscreenGestureCapability {
    override val allowEdgeSwipeToDismiss: Boolean = true
    val diagram: EuclideanDiagram = EuclideanDiagram.parse(config)
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val density = resources.displayMetrics.density.toDouble()
    private val fontDensity = resources.displayMetrics.scaledDensity.toDouble()
    private var textScale = 1.0
    private val background = context.getColor(R.color.lesson_canvas)

    init {
        setBackgroundColor(background)
        layoutParams = LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, MathVisualControls.dp(context, 260))
        importantForAccessibility = IMPORTANT_FOR_ACCESSIBILITY_YES
        contentDescription = config.optString("description").takeIf { it.isNotBlank() }
            ?: (diagram.points.flatMap { it.labels } + diagram.objects.mapNotNull { it.label }).joinToString(", ")
    }

    override fun scaleMathText(factor: Float) {
        if (factor.isFinite() && factor > 0) {
            textScale *= factor
            invalidate()
        }
    }

    /** The exact same geometry, arrowheads and wrapped labels consumed by onDraw. */
    fun drawingSnapshot(): EuclideanDrawingSnapshot {
        paint.textSize = (15 * fontDensity * textScale).toFloat()
        return EuclideanDiagramLayout.layout(diagram, max(1, width).toDouble(), max(1, height).toDouble(), density, paint.textSize.toDouble(), ::measureLabel)
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val drawing = drawingSnapshot()
        paint.color = accent
        paint.style = Paint.Style.STROKE
        paint.strokeWidth = (2.2 * density).toFloat()
        paint.strokeCap = Paint.Cap.ROUND
        drawing.paths.forEach { path -> line(canvas, path.start, path.end) }
        drawing.arrows.forEach { arrow ->
            line(canvas, arrow.tip, arrow.wingA)
            line(canvas, arrow.tip, arrow.wingB)
        }
        paint.style = Paint.Style.FILL
        drawing.points.forEach { point ->
            paint.color = background
            canvas.drawCircle(point.center.x.toFloat(), point.center.y.toFloat(), (5.6 * density).toFloat(), paint)
            paint.color = Color.WHITE
            canvas.drawCircle(point.center.x.toFloat(), point.center.y.toFloat(), (4.0 * density).toFloat(), paint)
        }
        // A background protects a full label from any residual line crossing in a dense scene.
        // Labels never replace or cover a point, and are not clipped to the inner plot rectangle.
        paint.textAlign = Paint.Align.LEFT
        drawing.labels.forEach { label ->
            val bounds = label.bounds
            paint.color = background
            canvas.drawRoundRect(RectF(bounds.left.toFloat(), bounds.top.toFloat(), bounds.right.toFloat(), bounds.bottom.toFloat()), (2 * density).toFloat(), (2 * density).toFloat(), paint)
            paint.color = Color.WHITE
            label.lines.forEachIndexed { index, text ->
                canvas.drawText(text, (bounds.left + 2 * density).toFloat(), (bounds.top + density + label.ascent + index * label.lineHeight).toFloat(), paint)
            }
        }
    }

    private fun line(canvas: Canvas, first: EuclideanPosition, second: EuclideanPosition) {
        canvas.drawLine(first.x.toFloat(), first.y.toFloat(), second.x.toFloat(), second.y.toFloat(), paint)
    }

    private fun measureLabel(text: String, maximum: Double): EuclideanLabelMetrics {
        // Character-safe wrapping also handles an uninterrupted label; no ellipsis or omitted text.
        val lines = mutableListOf<String>()
        var offset = 0
        while (offset < text.length) {
            var end = offset
            var lastSpace = -1
            while (end < text.length) {
                val next = end + Character.charCount(Character.codePointAt(text, end))
                if (next > text.length || paint.measureText(text, offset, next) > maximum && end > offset) break
                if (text[end].isWhitespace()) lastSpace = next
                end = next
            }
            if (end < text.length && lastSpace > offset) end = lastSpace
            if (end <= offset) end = offset + Character.charCount(Character.codePointAt(text, offset))
            lines.add(text.substring(offset, end))
            offset = end
        }
        val metrics = paint.fontMetrics
        return EuclideanLabelMetrics(lines, lines.maxOf { paint.measureText(it).toDouble() }, (metrics.descent - metrics.ascent + 2 * density).toDouble(), -metrics.ascent.toDouble())
    }
}
