package com.learngraphicsprogramming.app.math

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.view.MotionEvent
import android.view.View
import org.json.JSONObject
import kotlin.math.ceil
import kotlin.math.max
import kotlin.math.round

/** Single reusable number line. Only build_inequality is an editable answer;
 * the other modes use this representation with a separate response mechanism.
 */
class MathNumberLineView(
    context: Context,
    config: JSONObject,
    private val accentColor: Int,
    interactive: Boolean = false
) : View(context), MathTextScaleAware {
    private val density = resources.displayMetrics.density
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val min = MathCartesianView.finite(config, "min", -10.0)
    private val max = MathCartesianView.finite(config, "max", 10.0).let { if (it > min) it else min + 20 }
    val step = MathCartesianView.finite(config, "step", 1.0).let { if (it > 0) it else 1.0 }
    private var markerValue = MathCartesianView.finite(config, "value", 0.0).let { if (interactive) normalize(it) else it.coerceIn(min, max) }
    private var markerClosed = config.optBoolean("closed", true)
    private var markerDirection = normalizeDirection(config.optString("direction", "none"))
    private val showValueLabel = config.optBoolean("showValueLabel", config.optString("mode") !in listOf("read_value", "missing_value_hint"))
    private var dragging = false
    private var textScale = 1f
    var editable = interactive
        set(value) { field = value; tag = if (value) "math_gesture_owner" else null; dragging = false; invalidate() }
    var onValueChanged: (() -> Unit)? = null

    override fun scaleMathText(factor: Float) {
        if (factor.isFinite() && factor > 0) { textScale *= factor; invalidate() }
    }

    init {
        setBackgroundColor(Color.rgb(16, 17, 20))
        tag = if (interactive) "math_gesture_owner" else null
        isFocusable = true
        updateDescription()
    }

    fun answer(): MathAnswer.NumberLine = MathAnswer.NumberLine(markerValue, markerClosed, markerDirection)

    fun restore(value: Double, closed: Boolean, direction: String) {
        markerValue = normalize(value)
        markerClosed = closed
        markerDirection = normalizeDirection(direction)
        updateDescription()
        invalidate()
    }

    fun setMarker(value: Double = markerValue, closed: Boolean = markerClosed, direction: String = markerDirection) {
        if (!editable || !value.isFinite()) return
        val normalized = normalize(value)
        val normalizedDirection = normalizeDirection(direction)
        if (normalized == markerValue && closed == markerClosed && normalizedDirection == markerDirection) return
        restore(normalized, closed, normalizedDirection)
        onValueChanged?.invoke()
    }

    private fun normalize(value: Double): Double {
        val safe = if (value.isFinite()) value else min
        if (safe <= min) return min
        if (safe >= max) return max
        return (min + round((safe - min) / step) * step).coerceIn(min, max)
    }
    private fun normalizeDirection(value: String): String = when (value) { "left", "right" -> value; else -> "none" }
    private fun left(): Float = 28 * density
    private fun right(): Float = width - 28 * density
    private fun axisY(): Float = height * .5f
    private fun px(value: Double): Float = (left() + (value - min) / (max - min) * (right() - left())).toFloat()

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        if (right() <= left()) return
        val axis = axisY()
        paint.color = Color.LTGRAY
        paint.style = Paint.Style.STROKE
        paint.strokeWidth = 2 * density
        canvas.drawLine(left(), axis, right(), axis, paint)
        drawArrow(canvas, left(), axis, true)
        drawArrow(canvas, right(), axis, false)
        paint.style = Paint.Style.FILL
        paint.textAlign = Paint.Align.CENTER
        paint.textSize = 12 * density * textScale
        val tickStep = step * max(1.0, ceil((max - min) / step / 16))
        var tick = min
        var count = 0
        while (tick <= max + 1e-10 && count++ <= 18) {
            canvas.drawLine(px(tick), axis - 5 * density, px(tick), axis + 5 * density, paint)
            canvas.drawText(MathCartesianView.formatCoordinate(tick), px(tick), axis + 24 * density, paint)
            tick += tickStep
        }
        val markerX = px(markerValue)
        paint.color = accentColor
        paint.strokeWidth = 5 * density
        if (markerDirection != "none") {
            val end = if (markerDirection == "left") left() else right()
            canvas.drawLine(markerX, axis, end, axis, paint)
            drawArrow(canvas, end, axis, markerDirection == "left")
        }
        paint.style = Paint.Style.FILL
        paint.color = Color.rgb(16, 17, 20)
        canvas.drawCircle(markerX, axis, 8 * density, paint)
        paint.color = accentColor
        paint.style = if (markerClosed) Paint.Style.FILL else Paint.Style.STROKE
        paint.strokeWidth = 3 * density
        canvas.drawCircle(markerX, axis, 7 * density, paint)
        paint.style = Paint.Style.FILL
        paint.textSize = 15 * density * textScale
        paint.color = Color.WHITE
        if (showValueLabel) canvas.drawText(MathCartesianView.formatCoordinate(markerValue), markerX, axis - 21 * density, paint)
    }

    private fun drawArrow(canvas: Canvas, x: Float, y: Float, pointsLeft: Boolean) {
        val delta = if (pointsLeft) 8 * density else -8 * density
        canvas.drawLine(x, y, x + delta, y - 6 * density, paint)
        canvas.drawLine(x, y, x + delta, y + 6 * density, paint)
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        if (!editable || right() <= left()) return false
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                dragging = true
                parent?.requestDisallowInterceptTouchEvent(true)
            }
            MotionEvent.ACTION_MOVE -> if (!dragging) return false
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                if (!dragging) return false
                dragging = false
                parent?.requestDisallowInterceptTouchEvent(false)
                if (event.actionMasked == MotionEvent.ACTION_UP) performClick()
                return true
            }
            else -> return false
        }
        setMarker(min + (event.x - left()) / (right() - left()) * (max - min))
        return true
    }

    override fun performClick(): Boolean { super.performClick(); return true }
    private fun updateDescription() {
        contentDescription = "Recta numérica. Valor ${MathCartesianView.formatCoordinate(markerValue)}, punto ${if (markerClosed) "cerrado" else "abierto"}, dirección ${when (markerDirection) { "left" -> "izquierda"; "right" -> "derecha"; else -> "sin rayo" }}."
    }
}
