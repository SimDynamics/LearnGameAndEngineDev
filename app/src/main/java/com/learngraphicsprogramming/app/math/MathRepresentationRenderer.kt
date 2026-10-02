package com.learngraphicsprogramming.app.math

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import android.view.View
import android.view.ViewGroup
import android.widget.HorizontalScrollView
import android.widget.LinearLayout
import android.widget.TableLayout
import android.widget.TableRow
import android.widget.TextView
import org.json.JSONArray
import org.json.JSONObject
import kotlin.math.min

/** Rendering depends only on representation data, so the same visual can appear in prompts,
 * choices or auxiliary tools. Accepts config nested in the spec or fields directly on it.
 * Geometry: shape + labels:[String]. Venn: leftLabel/rightLabel/leftOnly/intersection/rightOnly/outside.
 */
object MathRepresentationRenderer {
    fun render(context: Context, spec: JSONObject, accentColor: Int): View {
        val config = spec.optJSONObject("config") ?: spec
        val type = spec.optString("type", "text")
        return when (type) {
            "text" -> text(context, config.optString("text", config.optString("value", "")))
            "math_formula" -> text(context, config.optString("text", config.optString("formula", config.optString("value", ""))), true)
            "math_table" -> table(context, config)
            "math_coordinate_graph", "coordinate_graph" -> MathCartesianView(context, config, accentColor).apply {
                layoutParams = LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, MathVisualControls.dp(context, 260))
            }
            "math_number_line" -> MathNumberLineView(context, config, accentColor).apply {
                layoutParams = LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, MathVisualControls.dp(context, 136))
            }
            "geometry_2d_diagram" -> if (config.has("points")) {
                MathCartesianView(context, JSONObject(config.toString()).put("mode", config.optString("mode", "polygon")), accentColor).apply {
                    layoutParams = LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, MathVisualControls.dp(context, 260))
                }
            } else GeometryDiagramView(context, config, accentColor, false)
            "geometry_3d_diagram" -> GeometryDiagramView(context, config, accentColor, true)
            "venn_diagram" -> VennDiagramView(context, config, accentColor)
            "rich_answer_option" -> content(context, config.optJSONArray("content") ?: JSONArray(), accentColor)
            else -> text(context, "Representación no compatible: $type").apply { setTextColor(Color.rgb(255, 194, 132)) }
        }
    }

    fun content(context: Context, blocks: JSONArray, accentColor: Int): View = LinearLayout(context).apply {
        orientation = LinearLayout.VERTICAL
        for (index in 0 until blocks.length()) {
            val block = blocks.optJSONObject(index)
            addView(if (block != null) render(context, block, accentColor) else text(context, blocks.optString(index)))
        }
    }

    private fun text(context: Context, value: String, formula: Boolean = false): TextView = TextView(context).apply {
        text = value
        textSize = if (formula) 22f else 16f
        setTextColor(Color.WHITE)
        setPadding(MathVisualControls.dp(context, 10), MathVisualControls.dp(context, 10), MathVisualControls.dp(context, 10), MathVisualControls.dp(context, 10))
        setTextIsSelectable(true)
    }

    private fun table(context: Context, config: JSONObject): View {
        val table = TableLayout(context)
        val headers = config.optJSONArray("headers")
        val rows = config.optJSONArray("rows") ?: JSONArray()
        fun addRow(values: JSONArray, header: Boolean) {
            val row = TableRow(context)
            for (index in 0 until values.length()) {
                val cell = text(context, values.optString(index))
                cell.setBackgroundColor(if (header) Color.rgb(49, 56, 66) else Color.rgb(28, 31, 36))
                if (header) cell.setTypeface(cell.typeface, android.graphics.Typeface.BOLD)
                val params = TableRow.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT)
                params.setMargins(1, 1, 1, 1)
                cell.minWidth = MathVisualControls.dp(context, 70)
                row.addView(cell, params)
            }
            table.addView(row)
        }
        if (headers != null) addRow(headers, true)
        for (index in 0 until rows.length()) rows.optJSONArray(index)?.let { addRow(it, false) }
        return HorizontalScrollView(context).apply { addView(table) }
    }
}

private class GeometryDiagramView(
    context: Context,
    private val config: JSONObject,
    private val accent: Int,
    private val threeDimensional: Boolean
) : View(context), MathTextScaleAware {
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val density = resources.displayMetrics.density
    private val shape = config.optString("shape", if (threeDimensional) "cube" else "rectangle")
    private val labels = config.optJSONArray("labels") ?: JSONArray()
    private var textScale = 1f

    override fun scaleMathText(factor: Float) {
        if (factor.isFinite() && factor > 0) { textScale *= factor; invalidate() }
    }

    init {
        setBackgroundColor(Color.rgb(16, 17, 20))
        layoutParams = LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, MathVisualControls.dp(context, 240))
        contentDescription = "Diagrama ${if (threeDimensional) "tridimensional" else "geométrico"}: $shape. " + (0 until labels.length()).joinToString { labels.optString(it) }
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val box = RectF(width * .22f, height * .18f, width * .75f, height * .72f)
        paint.color = accent
        paint.strokeWidth = 2.5f * density
        paint.style = Paint.Style.STROKE
        if (threeDimensional) draw3d(canvas, box) else draw2d(canvas, box)
        paint.color = Color.WHITE
        paint.style = Paint.Style.FILL
        paint.textSize = 15 * density * textScale
        paint.textAlign = Paint.Align.CENTER
        if (labels.length() > 0) canvas.drawText(labels.optString(0), width / 2f, height - 15 * density, paint)
        if (labels.length() > 1) {
            paint.textAlign = Paint.Align.LEFT
            canvas.drawText(labels.optString(1), 10 * density, height * .5f, paint)
        }
        if (labels.length() > 2) {
            paint.textAlign = Paint.Align.RIGHT
            canvas.drawText(labels.optString(2), width - 10 * density, height * .22f, paint)
        }
    }

    private fun draw2d(canvas: Canvas, box: RectF) {
        when (shape) {
            "circle" -> canvas.drawCircle(box.centerX(), box.centerY(), min(box.width(), box.height()) / 2, paint)
            "triangle" -> {
                val path = Path().apply { moveTo(box.left, box.bottom); lineTo(box.right, box.bottom); lineTo(box.centerX(), box.top); close() }
                canvas.drawPath(path, paint)
            }
            else -> canvas.drawRect(box, paint)
        }
    }

    private fun draw3d(canvas: Canvas, box: RectF) {
        val ellipseHeight = box.height() * .22f
        when (shape) {
            "cylinder" -> {
                canvas.drawOval(RectF(box.left, box.top, box.right, box.top + ellipseHeight), paint)
                canvas.drawOval(RectF(box.left, box.bottom - ellipseHeight, box.right, box.bottom), paint)
                canvas.drawLine(box.left, box.top + ellipseHeight / 2, box.left, box.bottom - ellipseHeight / 2, paint)
                canvas.drawLine(box.right, box.top + ellipseHeight / 2, box.right, box.bottom - ellipseHeight / 2, paint)
            }
            "cone" -> {
                canvas.drawOval(RectF(box.left, box.bottom - ellipseHeight, box.right, box.bottom), paint)
                canvas.drawLine(box.centerX(), box.top, box.left, box.bottom - ellipseHeight / 2, paint)
                canvas.drawLine(box.centerX(), box.top, box.right, box.bottom - ellipseHeight / 2, paint)
            }
            "sphere" -> {
                val radius = min(box.width(), box.height()) / 2
                canvas.drawCircle(box.centerX(), box.centerY(), radius, paint)
                canvas.drawOval(RectF(box.centerX() - radius, box.centerY() - radius / 3, box.centerX() + radius, box.centerY() + radius / 3), paint)
                canvas.drawOval(RectF(box.centerX() - radius / 3, box.centerY() - radius, box.centerX() + radius / 3, box.centerY() + radius), paint)
            }
            else -> {
                val offset = min(box.width(), box.height()) * .23f
                val front = RectF(box.left, box.top + offset, box.right - offset, box.bottom)
                val back = RectF(front.left + offset, front.top - offset, front.right + offset, front.bottom - offset)
                canvas.drawRect(front, paint)
                canvas.drawRect(back, paint)
                canvas.drawLine(front.left, front.top, back.left, back.top, paint)
                canvas.drawLine(front.right, front.top, back.right, back.top, paint)
                canvas.drawLine(front.left, front.bottom, back.left, back.bottom, paint)
                canvas.drawLine(front.right, front.bottom, back.right, back.bottom, paint)
            }
        }
    }
}

private class VennDiagramView(context: Context, private val config: JSONObject, private val accent: Int) : View(context), MathTextScaleAware {
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val density = resources.displayMetrics.density
    private var textScale = 1f

    override fun scaleMathText(factor: Float) {
        if (factor.isFinite() && factor > 0) { textScale *= factor; invalidate() }
    }

    init {
        setBackgroundColor(Color.rgb(16, 17, 20))
        layoutParams = LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, MathVisualControls.dp(context, 240))
        contentDescription = "Diagrama de Venn. ${config.optString("leftLabel", "A")}: ${config.optString("leftOnly")}. " +
            "Intersección: ${config.optString("intersection")}. ${config.optString("rightLabel", "B")}: ${config.optString("rightOnly")}. Fuera: ${config.optString("outside")}."
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val radius = min(width * .25f, height * .32f)
        val centerY = height * .49f
        val leftX = width * .40f
        val rightX = width * .60f
        paint.style = Paint.Style.STROKE
        paint.strokeWidth = 2 * density
        paint.color = Color.rgb(98, 109, 121)
        canvas.drawRect(10 * density, 12 * density, width - 10 * density, height - 12 * density, paint)
        paint.color = accent
        canvas.drawCircle(leftX, centerY, radius, paint)
        canvas.drawCircle(rightX, centerY, radius, paint)
        paint.style = Paint.Style.FILL
        paint.color = Color.WHITE
        paint.textSize = 14 * density * textScale
        paint.textAlign = Paint.Align.CENTER
        canvas.drawText(config.optString("leftLabel", "A"), leftX - radius * .25f, centerY - radius - 8 * density, paint)
        canvas.drawText(config.optString("rightLabel", "B"), rightX + radius * .25f, centerY - radius - 8 * density, paint)
        drawLines(canvas, config.optString("leftOnly"), leftX - radius * .35f, centerY)
        drawLines(canvas, config.optString("intersection"), width / 2f, centerY)
        drawLines(canvas, config.optString("rightOnly"), rightX + radius * .35f, centerY)
        canvas.drawText(config.optString("outside"), width / 2f, height - 25 * density, paint)
        paint.textAlign = Paint.Align.LEFT
        canvas.drawText(config.optString("universeLabel", "U"), 18 * density, 30 * density, paint)
    }

    private fun drawLines(canvas: Canvas, text: String, x: Float, y: Float) {
        text.split('\n').take(4).forEachIndexed { index, line -> canvas.drawText(line, x, y + index * 18 * density * textScale, paint) }
    }
}
