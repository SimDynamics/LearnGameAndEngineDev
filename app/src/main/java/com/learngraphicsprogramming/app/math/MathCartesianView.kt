package com.learngraphicsprogramming.app.math

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import android.view.MotionEvent
import android.view.View
import org.json.JSONObject
import java.util.Locale
import kotlin.math.abs
import kotlin.math.ceil
import kotlin.math.floor
import kotlin.math.hypot
import kotlin.math.max
import kotlin.math.round

/** Shared Cartesian surface for representations, auxiliary tools and graph/geometry answers.
 * config: mode, bounds{xMin,xMax,yMin,yMax}, gridStep, snap (0 disables),
 * points:[{x,y,draggable:true}], showLabels. Parabola points are vertex then another point.
 */
class MathCartesianView(
    context: Context,
    config: JSONObject,
    private val accentColor: Int,
    interactive: Boolean = false
) : View(context), MathTextScaleAware, MathFullscreenGestureCapability {
    // An interactive graph/geometry surface stays conservative even after being completed.
    override val allowEdgeSwipeToDismiss: Boolean = !interactive
    private val density = resources.displayMetrics.density
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val plot = RectF()
    val mode: String = config.optString("mode", "point")
    private val bounds = config.optJSONObject("bounds") ?: JSONObject()
    private val xMin = finite(bounds, "xMin", -5.0)
    private val xMax = finite(bounds, "xMax", 5.0).let { if (it > xMin) it else xMin + 10 }
    private val yMin = finite(bounds, "yMin", -5.0)
    private val yMax = finite(bounds, "yMax", 5.0).let { if (it > yMin) it else yMin + 10 }
    val coordinateAspectRatio: Float = ((xMax - xMin) / (yMax - yMin)).toFloat().let {
        if (it.isFinite() && it > 0) it else 1f
    }
    val snap: Double = finite(config, "snap", 1.0).coerceAtLeast(0.0)
    private val gridStep = finite(config, "gridStep", 1.0).let {
        max(if (it > 0) it else 1.0, max(xMax - xMin, yMax - yMin) / 100.0)
    }
    private val showLabels = config.optBoolean("showLabels", true)
    private val mutablePoints = mutableListOf<MathPoint>()
    private val draggable = mutableListOf<Boolean>()
    private var movingIndex = -1
    private var textScale = 1f
    var editable: Boolean = interactive
        set(value) { field = value; tag = if (value) "math_gesture_owner" else null; movingIndex = -1; invalidate() }
    var onPointsChanged: (() -> Unit)? = null

    override fun scaleMathText(factor: Float) {
        if (factor.isFinite() && factor > 0) { textScale *= factor; invalidate() }
    }

    init {
        val initial = config.optJSONArray("points")
        if (initial != null) {
            for (index in 0 until initial.length()) {
                val point = initial.optJSONObject(index) ?: continue
                val coordinate = MathPoint(finite(point, "x", 0.0), finite(point, "y", 0.0))
                mutablePoints += if (interactive) bounded(coordinate) else coordinate
                draggable += point.optBoolean("draggable", true)
            }
        }
        if (mutablePoints.isEmpty() && interactive) {
            val defaults = when (mode) {
                "line", "parabola" -> listOf(MathPoint(0.0, 0.0), MathPoint(1.0, 1.0))
                "polygon" -> listOf(MathPoint(-2.0, -1.0), MathPoint(2.0, -1.0), MathPoint(0.0, 2.0))
                "scatter" -> listOf(MathPoint(-2.0, 0.0), MathPoint(0.0, 1.0), MathPoint(2.0, 2.0))
                else -> listOf(MathPoint(0.0, 0.0))
            }
            defaults.forEach { mutablePoints += bounded(it); draggable += true }
        }
        setBackgroundColor(Color.rgb(16, 17, 20))
        tag = if (interactive) "math_gesture_owner" else null
        isFocusable = true
        updateDescription()
    }

    fun points(): List<MathPoint> = mutablePoints.toList()
    fun canMovePoint(index: Int): Boolean = draggable.getOrElse(index) { true }

    fun setPoints(points: List<MathPoint>) {
        mutablePoints.clear()
        mutablePoints.addAll(if (editable) points.map(::bounded) else points)
        updateDescription()
        invalidate()
    }

    fun movePoint(index: Int, x: Double, y: Double) {
        if (!editable || index !in mutablePoints.indices || !canMovePoint(index) || !x.isFinite() || !y.isFinite()) return
        val point = bounded(MathPoint(x, y))
        if (mutablePoints[index] == point) return
        mutablePoints[index] = point
        updateDescription()
        invalidate()
        onPointsChanged?.invoke()
    }

    private fun bounded(point: MathPoint): MathPoint {
        fun coordinate(value: Double, min: Double, max: Double): Double {
            val safe = if (value.isFinite()) value else 0.0
            if (safe <= min) return min
            if (safe >= max) return max
            return (if (snap > 0) round(safe / snap) * snap else safe).coerceIn(min, max)
        }
        return MathPoint(coordinate(point.x, xMin, xMax), coordinate(point.y, yMin, yMax))
    }

    private fun px(x: Double): Float = (plot.left + (x - xMin) / (xMax - xMin) * plot.width()).toFloat()
    private fun py(y: Double): Float = (plot.bottom - (y - yMin) / (yMax - yMin) * plot.height()).toFloat()

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        paint.textSize = 11 * density * textScale
        val fontHeight = paint.fontMetrics.bottom - paint.fontMetrics.top
        val widestY = listOf(yMin, yMax, 0.0).maxOf { paint.measureText(formatCoordinate(it)) }
        val edgeX = max(paint.measureText(formatCoordinate(xMin)), paint.measureText(formatCoordinate(xMax))) / 2f
        plot.set(max(widestY + 8 * density, edgeX + 4 * density), max(fontHeight, 16 * density),
            width - max(edgeX + 4 * density, 18 * density), height - max(fontHeight + 12 * density, 30 * density))
        if (plot.width() <= 0 || plot.height() <= 0) return
        // Equal coordinate units keep geometric figures and graphs proportional in either orientation.
        val usableWidth = kotlin.math.min(plot.width(), plot.height() * coordinateAspectRatio)
        val usableHeight = usableWidth / coordinateAspectRatio
        val centerX = plot.centerX()
        val centerY = plot.centerY()
        plot.set(centerX - usableWidth / 2, centerY - usableHeight / 2, centerX + usableWidth / 2, centerY + usableHeight / 2)
        paint.style = Paint.Style.STROKE
        paint.strokeWidth = density
        paint.color = Color.rgb(51, 56, 65)
        var x = ceil(xMin / gridStep) * gridStep
        var gridCount = 0
        while (x <= xMax && gridCount++ <= 101) { canvas.drawLine(px(x), plot.top, px(x), plot.bottom, paint); x += gridStep }
        var y = ceil(yMin / gridStep) * gridStep
        gridCount = 0
        while (y <= yMax && gridCount++ <= 101) { canvas.drawLine(plot.left, py(y), plot.right, py(y), paint); y += gridStep }
        canvas.drawRect(plot, paint)
        paint.color = Color.rgb(192, 201, 211)
        paint.strokeWidth = 1.5f * density
        if (0.0 in xMin..xMax) canvas.drawLine(px(0.0), plot.top, px(0.0), plot.bottom, paint)
        if (0.0 in yMin..yMax) canvas.drawLine(plot.left, py(0.0), plot.right, py(0.0), paint)
        paint.style = Paint.Style.FILL
        paint.textSize = 11 * density * textScale
        val labelStep = gridStep * max(1.0, ceil(max(xMax - xMin, yMax - yMin) / gridStep / 12.0))
        x = ceil(xMin / labelStep) * labelStep
        var labelCount = 0
        while (x <= xMax && labelCount++ <= 15) {
            paint.textAlign = Paint.Align.CENTER
            canvas.drawText(formatCoordinate(x), px(x), plot.bottom - paint.fontMetrics.top + 4 * density, paint)
            x += labelStep
        }
        y = ceil(yMin / labelStep) * labelStep
        labelCount = 0
        while (y <= yMax && labelCount++ <= 15) {
            paint.textAlign = Paint.Align.RIGHT
            canvas.drawText(formatCoordinate(y), plot.left - 6 * density, py(y) - (paint.fontMetrics.top + paint.fontMetrics.bottom) / 2, paint)
            y += labelStep
        }
        paint.textAlign = Paint.Align.LEFT
        canvas.drawText("x", plot.right + 5 * density, plot.bottom - paint.fontMetrics.top + 4 * density, paint)
        canvas.drawText("y", plot.left - 4 * density, max(-paint.fontMetrics.top, plot.top - 6 * density), paint)
        canvas.save()
        canvas.clipRect(plot)
        paint.color = accentColor
        paint.strokeWidth = 2.5f * density
        paint.style = Paint.Style.STROKE
        when (mode) {
            "line" -> if (mutablePoints.size >= 2) drawLine(canvas, mutablePoints[0], mutablePoints[1])
            "polygon" -> if (mutablePoints.size >= 2) {
                val path = Path()
                mutablePoints.forEachIndexed { index, point ->
                    if (index == 0) path.moveTo(px(point.x), py(point.y)) else path.lineTo(px(point.x), py(point.y))
                }
                path.close()
                paint.style = Paint.Style.FILL
                paint.color = (accentColor and 0x00ffffff) or 0x33000000
                canvas.drawPath(path, paint)
                paint.style = Paint.Style.STROKE
                paint.color = accentColor
                canvas.drawPath(path, paint)
            }
            "parabola" -> if (mutablePoints.size >= 2) drawParabola(canvas)
        }
        paint.style = Paint.Style.FILL
        paint.textSize = 11 * density * textScale
        mutablePoints.forEachIndexed { index, point ->
            paint.color = if (canMovePoint(index)) accentColor else Color.LTGRAY
            canvas.drawCircle(px(point.x), py(point.y), 6 * density, paint)
            if (showLabels) {
                paint.color = Color.WHITE
                val prefix = if (mutablePoints.size > 1) "${pointLabel(index)}: " else ""
                val label = "$prefix(${formatCoordinate(point.x)}, ${formatCoordinate(point.y)})"
                val labelWidth = paint.measureText(label)
                val preferredX = if (px(point.x) + 9 * density + labelWidth <= plot.right) px(point.x) + 9 * density else px(point.x) - 9 * density - labelWidth
                val labelX = preferredX.coerceIn(plot.left + 2 * density, max(plot.left + 2 * density, plot.right - labelWidth - 2 * density))
                val labelY = (py(point.y) - 9 * density).coerceIn(plot.top - paint.fontMetrics.top, plot.bottom - paint.fontMetrics.bottom)
                canvas.drawText(label, labelX, labelY, paint)
            }
        }
        canvas.restore()
    }

    private fun drawLine(canvas: Canvas, first: MathPoint, second: MathPoint) {
        if (abs(second.x - first.x) < 1e-10) {
            canvas.drawLine(px(first.x), plot.top, px(first.x), plot.bottom, paint)
        } else {
            val slope = (second.y - first.y) / (second.x - first.x)
            canvas.drawLine(px(xMin), py(first.y + slope * (xMin - first.x)), px(xMax), py(first.y + slope * (xMax - first.x)), paint)
        }
    }

    private fun drawParabola(canvas: Canvas) {
        val vertex = mutablePoints[0]
        val second = mutablePoints[1]
        val delta = second.x - vertex.x
        if (abs(delta) < 1e-10) return
        val a = (second.y - vertex.y) / (delta * delta)
        val path = Path()
        for (index in 0..200) {
            val x = xMin + (xMax - xMin) * index / 200
            val y = a * (x - vertex.x) * (x - vertex.x) + vertex.y
            if (index == 0) path.moveTo(px(x), py(y)) else path.lineTo(px(x), py(y))
        }
        canvas.drawPath(path, paint)
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        if (!editable) return false
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                movingIndex = mutablePoints.indices.filter(::canMovePoint).minByOrNull { index ->
                    hypot((px(mutablePoints[index].x) - event.x).toDouble(), (py(mutablePoints[index].y) - event.y).toDouble())
                } ?: -1
                if (movingIndex < 0 || hypot((px(mutablePoints[movingIndex].x) - event.x).toDouble(), (py(mutablePoints[movingIndex].y) - event.y).toDouble()) > 32 * density) {
                    movingIndex = -1
                    return false
                }
                parent?.requestDisallowInterceptTouchEvent(true)
                return true
            }
            MotionEvent.ACTION_MOVE -> if (movingIndex >= 0 && plot.width() > 0 && plot.height() > 0) {
                movePoint(movingIndex, xMin + (event.x - plot.left) / plot.width() * (xMax - xMin), yMin + (plot.bottom - event.y) / plot.height() * (yMax - yMin))
                return true
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                val handled = movingIndex >= 0
                movingIndex = -1
                parent?.requestDisallowInterceptTouchEvent(false)
                if (event.actionMasked == MotionEvent.ACTION_UP && handled) performClick()
                return handled
            }
        }
        return false
    }

    override fun performClick(): Boolean { super.performClick(); return true }

    private fun updateDescription() {
        contentDescription = "Gráfica $mode. " + mutablePoints.mapIndexed { index, point ->
            "Punto ${index + 1}: x ${formatCoordinate(point.x)}, y ${formatCoordinate(point.y)}"
        }.joinToString(". ")
    }

    companion object {
        internal fun pointLabel(index: Int): String = if (index < 26) ('A'.code + index).toChar().toString() else "P${index + 1}"
        internal fun finite(json: JSONObject, key: String, fallback: Double): Double = json.optDouble(key, fallback).let { if (it.isFinite()) it else fallback }
        internal fun formatCoordinate(value: Double): String {
            val clean = if (abs(value) < 1e-9) 0.0 else value
            return if (clean == floor(clean) && abs(clean) < 1e12) clean.toLong().toString() else String.format(Locale.US, "%.4f", clean).trimEnd('0').trimEnd('.')
        }
    }
}
