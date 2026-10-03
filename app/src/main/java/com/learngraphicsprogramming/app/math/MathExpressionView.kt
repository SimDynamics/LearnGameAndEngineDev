package com.learngraphicsprogramming.app.math

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import android.graphics.Typeface
import android.util.AttributeSet
import android.util.TypedValue
import android.view.MotionEvent
import android.view.View
import kotlin.math.abs
import kotlin.math.ceil
import kotlin.math.max
import kotlin.math.min

/** The same layout is used by editable expressions and formula spans in lesson prose. */
class MathExpressionLayout private constructor(private val box: ExpressionBox) {
    val width: Float get() = box.width
    val ascent: Float get() = box.ascent
    val descent: Float get() = box.descent
    val height: Float get() = descent - ascent

    fun draw(canvas: Canvas, paint: Paint, x: Float, baseline: Float) = box.draw(canvas, paint, x, baseline)

    /** Coordinates are relative to the expression's origin and baseline. */
    fun caretBounds(cursor: MathCursor): RectF? = box.carets.firstOrNull { it.cursor == cursor }?.let {
        RectF(it.x, it.top, it.x, it.bottom)
    }

    fun hitTest(x: Float, y: Float): MathCursor = box.carets.minByOrNull {
        val dx = abs(x - it.x)
        val dy = when { y < it.top -> it.top - y; y > it.bottom -> y - it.bottom; else -> 0f }
        dx * dx + dy * dy * 4f - it.cursor.path.size * .01f
    }?.cursor ?: MathCursor()

    companion object {
        fun create(expression: MathExpression.Sequence, paint: Paint, size: Float, showPlaceholders: Boolean = false): MathExpressionLayout =
            MathExpressionLayout(ExpressionLayoutBuilder(Paint(paint), showPlaceholders).sequence(expression, size, emptyList()))
    }
}

/** Native two-dimensional mathematical surface. Editors add input/state behavior to this view. */
open class MathExpressionView @JvmOverloads constructor(context: Context, attrs: AttributeSet? = null) :
    View(context, attrs), MathTextScaleAware, MathFullscreenGestureCapability {
    override val allowEdgeSwipeToDismiss: Boolean get() = onCursorRequested == null
    protected val expressionPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.WHITE }
    private var cachedLayout: MathExpressionLayout? = null

    var expression: MathExpression.Sequence = MathExpression.Sequence(emptyList())
        set(value) {
            field = value
            contentDescription = MathLatexSerializer.serialize(value)
            resetExpressionLayout()
        }
    var cursor: MathCursor? = null
        set(value) { field = value; invalidate() }
    var onCursorRequested: ((MathCursor) -> Unit)? = null
    var showPlaceholders: Boolean = false
        set(value) { field = value; resetExpressionLayout() }
    var textSizePx: Float = TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_SP, 18f, resources.displayMetrics)
        set(value) {
            if (!value.isFinite() || value <= 0f) return
            field = value
            resetExpressionLayout()
        }
    var textColor: Int = Color.WHITE
        set(value) { field = value; expressionPaint.color = value; invalidate() }
    var caretColor: Int = Color.rgb(133, 193, 255)
        set(value) { field = value; invalidate() }

    val expressionLayout: MathExpressionLayout
        get() = cachedLayout ?: MathExpressionLayout.create(expression, expressionPaint, textSizePx, showPlaceholders).also { cachedLayout = it }
    val expressionOriginX: Float get() = paddingLeft.toFloat()
    val expressionBaseline: Float get() = paddingTop - expressionLayout.ascent

    init { setWillNotDraw(false) }

    protected fun resetExpressionLayout() {
        cachedLayout = null
        requestLayout()
        invalidate()
    }

    override fun scaleMathText(factor: Float) {
        if (factor.isFinite() && factor > 0f) textSizePx *= factor
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val layout = expressionLayout
        val desiredWidth = max(suggestedMinimumWidth, ceil(layout.width).toInt() + paddingLeft + paddingRight)
        val desiredHeight = max(suggestedMinimumHeight, ceil(layout.height).toInt() + paddingTop + paddingBottom)
        setMeasuredDimension(resolveSize(desiredWidth, widthMeasureSpec), resolveSize(desiredHeight, heightMeasureSpec))
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        expressionPaint.color = textColor
        expressionLayout.draw(canvas, expressionPaint, expressionOriginX, expressionBaseline)
        cursor?.let { selected ->
            expressionLayout.caretBounds(selected)?.let { bounds ->
                expressionPaint.color = caretColor
                expressionPaint.style = Paint.Style.STROKE
                expressionPaint.strokeWidth = max(resources.displayMetrics.density, textSizePx * .045f)
                canvas.drawLine(expressionOriginX + bounds.left, expressionBaseline + bounds.top,
                    expressionOriginX + bounds.left, expressionBaseline + bounds.bottom, expressionPaint)
                expressionPaint.style = Paint.Style.FILL
            }
        }
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        val callback = onCursorRequested ?: return super.onTouchEvent(event)
        if (!isEnabled) return false
        if (event.actionMasked == MotionEvent.ACTION_DOWN) return true
        if (event.actionMasked == MotionEvent.ACTION_UP) {
            callback(expressionLayout.hitTest(event.x - expressionOriginX, event.y - expressionBaseline))
            performClick()
            return true
        }
        return event.actionMasked == MotionEvent.ACTION_MOVE
    }

    override fun performClick(): Boolean { super.performClick(); return true }
}

private data class ExpressionCaret(val cursor: MathCursor, val x: Float, val top: Float, val bottom: Float) {
    fun translated(dx: Float, dy: Float) = copy(x = x + dx, top = top + dy, bottom = bottom + dy)
}

private interface ExpressionInk { fun draw(canvas: Canvas, paint: Paint, x: Float, baseline: Float) }

private class ExpressionText(private val value: String, private val size: Float, private val italic: Boolean = false) : ExpressionInk {
    override fun draw(canvas: Canvas, paint: Paint, x: Float, baseline: Float) {
        paint.textSize = size
        paint.typeface = Typeface.create("serif", if (italic) Typeface.ITALIC else Typeface.NORMAL)
        paint.style = Paint.Style.FILL
        canvas.drawText(value, x, baseline, paint)
    }
}

private class ExpressionStroke(private val path: Path, private val size: Float, private val muted: Boolean = false) : ExpressionInk {
    override fun draw(canvas: Canvas, paint: Paint, x: Float, baseline: Float) {
        val save = canvas.save()
        val oldAlpha = paint.alpha
        canvas.translate(x, baseline)
        paint.style = Paint.Style.STROKE
        paint.strokeWidth = max(1f, size * .035f)
        paint.strokeCap = Paint.Cap.ROUND
        if (muted) paint.alpha = min(oldAlpha, 120)
        canvas.drawPath(path, paint)
        paint.alpha = oldAlpha
        paint.style = Paint.Style.FILL
        canvas.restoreToCount(save)
    }
}

private class ExpressionPlaced(private val child: ExpressionBox, private val dx: Float, private val dy: Float) : ExpressionInk {
    override fun draw(canvas: Canvas, paint: Paint, x: Float, baseline: Float) = child.draw(canvas, paint, x + dx, baseline + dy)
}

private data class ExpressionBox(val width: Float, val ascent: Float, val descent: Float,
    val ink: List<ExpressionInk>, val carets: List<ExpressionCaret> = emptyList()) {
    fun draw(canvas: Canvas, paint: Paint, x: Float, baseline: Float) { ink.forEach { it.draw(canvas, paint, x, baseline) } }
}

private class ExpressionLayoutBuilder(private val paint: Paint, private val placeholders: Boolean) {
    fun sequence(sequence: MathExpression.Sequence, size: Float, path: List<MathCursorStep>): ExpressionBox {
        if (sequence.children.isEmpty()) {
            val width = if (placeholders) size * .65f else 0f
            val ink = if (placeholders) listOf(ExpressionStroke(Path().apply {
                addRoundRect(RectF(size * .08f, -size * .73f, width - size * .08f, size * .12f), size * .08f, size * .08f, Path.Direction.CW)
            }, size, true)) else emptyList()
            return ExpressionBox(width, -size * .8f, size * .2f, ink,
                listOf(ExpressionCaret(MathCursor(path, 0), width / 2f, -size * .8f, size * .2f)))
        }
        val boxes = sequence.children.mapIndexed { index, child -> node(child, size, path, index) }
        val ascent = boxes.minOf { it.ascent }
        val descent = boxes.maxOf { it.descent }
        val ink = mutableListOf<ExpressionInk>()
        val carets = mutableListOf<ExpressionCaret>()
        var x = 0f
        boxes.forEachIndexed { index, child ->
            carets += ExpressionCaret(MathCursor(path, index), x, ascent, descent)
            ink += ExpressionPlaced(child, x, 0f)
            carets += child.carets.map { it.translated(x, 0f) }
            x += child.width
        }
        carets += ExpressionCaret(MathCursor(path, boxes.size), x, ascent, descent)
        return ExpressionBox(x, ascent, descent, ink, carets)
    }

    private fun text(value: String, size: Float, italic: Boolean = false, gap: Float = 0f): ExpressionBox {
        paint.textSize = size
        paint.typeface = Typeface.create("serif", if (italic) Typeface.ITALIC else Typeface.NORMAL)
        val fm = paint.fontMetrics
        val literal = ExpressionBox(paint.measureText(value), fm.ascent, fm.descent, listOf(ExpressionText(value, size, italic)))
        return if (gap == 0f) literal else compose(listOf(Triple(literal, gap, 0f)), literal.width + 2f * gap)
    }

    private fun node(node: MathExpression, size: Float, path: List<MathCursorStep>, index: Int): ExpressionBox {
        fun child(value: MathExpression.Sequence, slot: String, scale: Float = 1f) = sequence(value, size * scale, path + MathCursorStep(index, slot))
        return when (node) {
            is MathExpression.Sequence -> sequence(node, size, path + MathCursorStep(index, "content"))
            is MathExpression.Number -> text(node.value.replace('-', '−'), size)
            is MathExpression.Variable -> text(node.name, size, true)
            is MathExpression.Symbol -> text(node.value, size)
            is MathExpression.Operator -> text(node.value.replace('-', '−'), size, gap = size * .1f)
            is MathExpression.Relation -> text(node.value, size, gap = size * .14f)
            is MathExpression.Fraction -> fraction(child(node.numerator, "numerator", .82f), child(node.denominator, "denominator", .82f), size)
            is MathExpression.Scripts -> scripts(child(node.base, "base"), node.upper?.let { child(it, "upper", .68f) }, node.lower?.let { child(it, "lower", .68f) }, size)
            is MathExpression.Root -> root(child(node.radicand, "radicand"), node.index?.let { child(it, "index", .56f) }, size)
            is MathExpression.Group -> delimit(child(node.content, "content"), node.delimiter, size)
            is MathExpression.Function -> {
                val label = text(node.name, size)
                val function = if (node.base == null) label else scripts(label, null, child(node.base!!, "base", .68f), size)
                val argument = delimit(child(node.argument, "argument"), MathDelimiter.PARENTHESES, size)
                compose(listOf(Triple(function, 0f, 0f), Triple(argument, function.width + size * .08f, 0f)))
            }
            is MathExpression.LargeOperator -> largeOperator(node.kind, child(node.body, "body"), node.lower?.let { child(it, "lower", .62f) }, node.upper?.let { child(it, "upper", .62f) }, size)
            is MathExpression.Limit -> {
                val label = text("lim", size)
                val condition = child(node.condition, "condition", .62f)
                val lower = label.descent + size * .1f - condition.ascent
                val width = max(label.width, condition.width)
                val head = compose(listOf(Triple(label, (width - label.width) / 2f, 0f), Triple(condition, (width - condition.width) / 2f, lower)), width)
                val body = child(node.body, "body")
                compose(listOf(Triple(head, 0f, 0f), Triple(body, width + size * .2f, 0f)))
            }
            is MathExpression.Derivative -> {
                val d = text("d", size * .82f, true)
                val orderTop = node.order?.let { child(it, "order", .56f) }
                val topD = if (orderTop == null) d else scripts(d, orderTop, null, size * .82f)
                val body = child(node.body, "body", .82f)
                val numerator = compose(listOf(Triple(topD, 0f, 0f), Triple(body, topD.width + size * .04f, 0f)))
                val variable = child(node.variable, "variable", .82f)
                val variablePower = if (orderTop == null) variable else scripts(variable, orderTop.copy(carets = emptyList()), null, size * .82f)
                val denominator = compose(listOf(Triple(d, 0f, 0f), Triple(variablePower, d.width, 0f)))
                fraction(numerator, denominator, size)
            }
            is MathExpression.Matrix -> grid(node.rows.mapIndexed { row, cells -> cells.mapIndexed { column, value -> child(value, "r${row}c$column") } }, size, false)
            is MathExpression.Cases -> grid(node.rows.mapIndexed { row, value -> listOf(child(value.expression, "expression$row"), child(value.condition, "condition$row")) }, size, true)
        }
    }

    private fun compose(children: List<Triple<ExpressionBox, Float, Float>>, width: Float? = null, ink: List<ExpressionInk> = emptyList()): ExpressionBox =
        ExpressionBox(width ?: (children.maxOfOrNull { it.second + it.first.width } ?: 0f),
            children.minOfOrNull { it.third + it.first.ascent } ?: 0f,
            children.maxOfOrNull { it.third + it.first.descent } ?: 0f,
            children.map { ExpressionPlaced(it.first, it.second, it.third) } + ink,
            children.flatMap { item -> item.first.carets.map { it.translated(item.second, item.third) } })

    private fun fraction(top: ExpressionBox, bottom: ExpressionBox, size: Float): ExpressionBox {
        val axis = -size * .29f
        val width = max(top.width, bottom.width) + size * .25f
        val topY = axis - size * .11f - top.descent
        val bottomY = axis + size * .11f - bottom.ascent
        val line = ExpressionStroke(Path().apply { moveTo(size * .025f, axis); lineTo(width - size * .025f, axis) }, size)
        return compose(listOf(Triple(top, (width - top.width) / 2f, topY), Triple(bottom, (width - bottom.width) / 2f, bottomY)), width, listOf(line))
    }

    private fun scripts(base: ExpressionBox, upper: ExpressionBox?, lower: ExpressionBox?, size: Float): ExpressionBox {
        val items = mutableListOf(Triple(base, 0f, 0f))
        // Scripts share the base anchor, irrespective of either script's own width.
        val scriptX = base.width + size * .025f
        upper?.let { items += Triple(it, scriptX, min(-size * .48f, base.ascent - it.descent + size * .55f)) }
        lower?.let { items += Triple(it, scriptX, max(size * .28f, base.descent - it.ascent - size * .55f)) }
        return compose(items)
    }

    private fun root(body: ExpressionBox, index: ExpressionBox?, size: Float): ExpressionBox {
        val indexWidth = index?.width ?: 0f
        val lead = max(size * .68f, indexWidth + size * .22f)
        val top = min(body.ascent - size * .1f, -size * .9f)
        val bottom = max(body.descent, size * .1f)
        val rootStroke = Path().apply {
            moveTo(lead - size * .65f, -size * .15f)
            lineTo(lead - size * .51f, -size * .24f)
            lineTo(lead - size * .31f, bottom)
            lineTo(lead - size * .11f, top)
            lineTo(lead + body.width + size * .08f, top)
        }
        val children = mutableListOf(Triple(body, lead, 0f))
        index?.let { children += Triple(it, 0f, top + size * .24f - it.descent) }
        val box = compose(children, lead + body.width + size * .1f, listOf(ExpressionStroke(rootStroke, size)))
        return box.copy(ascent = min(box.ascent, top), descent = max(box.descent, bottom))
    }

    private fun delimit(body: ExpressionBox, delimiter: MathDelimiter, size: Float): ExpressionBox {
        val top = min(body.ascent, -size * .8f) - size * .07f
        val bottom = max(body.descent, size * .2f) + size * .07f
        val side = size * (if (delimiter == MathDelimiter.ABSOLUTE) .18f else .34f)
        val width = body.width + 2f * side + size * .08f
        val left = delimiterPath(delimiter, side, top, bottom, false)
        val right = delimiterPath(delimiter, side, top, bottom, true).apply { offset(width - side, 0f) }
        return compose(listOf(Triple(body, side + size * .04f, 0f)), width,
            listOf(ExpressionStroke(left, size), ExpressionStroke(right, size))).copy(ascent = top, descent = bottom)
    }

    private fun delimiterPath(delimiter: MathDelimiter, width: Float, top: Float, bottom: Float, right: Boolean): Path {
        fun x(value: Float) = if (right) width - value else value
        val middle = (top + bottom) / 2f
        return Path().apply {
            when (delimiter) {
                MathDelimiter.ABSOLUTE -> { moveTo(width / 2f, top); lineTo(width / 2f, bottom) }
                MathDelimiter.BRACKETS -> { moveTo(x(width * .88f), top); lineTo(x(width * .2f), top); lineTo(x(width * .2f), bottom); lineTo(x(width * .88f), bottom) }
                MathDelimiter.BRACES -> {
                    moveTo(x(width * .95f), top)
                    cubicTo(x(width * .25f), top, x(width * .5f), middle - (bottom - top) * .08f, x(width * .05f), middle)
                    cubicTo(x(width * .5f), middle + (bottom - top) * .08f, x(width * .25f), bottom, x(width * .95f), bottom)
                }
                MathDelimiter.PARENTHESES -> {
                    moveTo(x(width * .9f), top)
                    cubicTo(x(-width * .1f), top + (bottom - top) * .2f, x(-width * .1f), bottom - (bottom - top) * .2f, x(width * .9f), bottom)
                }
            }
        }
    }

    private fun largeOperator(kind: MathLargeOperator, body: ExpressionBox, lower: ExpressionBox?, upper: ExpressionBox?, size: Float): ExpressionBox {
        val symbol = text(when (kind) { MathLargeOperator.INTEGRAL -> "∫"; MathLargeOperator.SUM -> "∑"; MathLargeOperator.PRODUCT -> "∏" }, size * 1.5f)
        val symbolY = -size * .29f - (symbol.ascent + symbol.descent) / 2f
        val items = mutableListOf<Triple<ExpressionBox, Float, Float>>()
        val headWidth: Float
        if (kind == MathLargeOperator.INTEGRAL) {
            headWidth = symbol.width + max(lower?.width ?: 0f, upper?.width ?: 0f) + size * .06f
            items += Triple(symbol, 0f, symbolY)
            upper?.let { items += Triple(it, symbol.width * .9f, symbolY + symbol.ascent + it.descent) }
            lower?.let { items += Triple(it, symbol.width * .65f, symbolY + symbol.descent - it.ascent * .6f) }
        } else {
            headWidth = max(symbol.width, max(lower?.width ?: 0f, upper?.width ?: 0f))
            items += Triple(symbol, (headWidth - symbol.width) / 2f, symbolY)
            upper?.let { items += Triple(it, (headWidth - it.width) / 2f, symbolY + symbol.ascent - size * .08f - it.descent) }
            lower?.let { items += Triple(it, (headWidth - it.width) / 2f, symbolY + symbol.descent + size * .08f - it.ascent) }
        }
        items += Triple(body, headWidth + size * .2f, 0f)
        return compose(items)
    }

    private fun grid(rows: List<List<ExpressionBox>>, size: Float, cases: Boolean): ExpressionBox {
        if (rows.isEmpty()) return delimit(text("", size), if (cases) MathDelimiter.BRACES else MathDelimiter.BRACKETS, size)
        val columns = rows.maxOf { it.size }
        val widths = (0 until columns).map { column -> max(size * .5f, rows.maxOf { it.getOrNull(column)?.width ?: 0f }) }
        val rowAscent = rows.map { row -> min(-size * .8f, row.minOfOrNull { it.ascent } ?: 0f) }
        val rowDescent = rows.map { row -> max(size * .2f, row.maxOfOrNull { it.descent } ?: 0f) }
        val gap = size * .5f
        val height = rowAscent.indices.sumOf { (rowDescent[it] - rowAscent[it]).toDouble() }.toFloat() + size * .3f * (rows.size - 1)
        val top = -height / 2f - size * .29f
        val side = size * .45f
        val contentWidth = widths.sum() + gap * max(0, columns - 1)
        var y = top
        val children = mutableListOf<Triple<ExpressionBox, Float, Float>>()
        rows.forEachIndexed { row, cells ->
            val baseline = y - rowAscent[row]
            var x = side
            cells.forEachIndexed { column, cell ->
                children += Triple(cell, x + if (cases) 0f else (widths[column] - cell.width) / 2f, baseline)
                x += widths[column] + gap
            }
            y = baseline + rowDescent[row] + size * .3f
        }
        val width = contentWidth + side * if (cases) 1.2f else 2f
        val bottom = top + height
        val left = delimiterPath(if (cases) MathDelimiter.BRACES else MathDelimiter.BRACKETS, side * .8f, top - size * .07f, bottom + size * .07f, false)
        val ink = mutableListOf<ExpressionInk>(ExpressionStroke(left, size))
        if (!cases) ink += ExpressionStroke(delimiterPath(MathDelimiter.BRACKETS, side * .8f, top - size * .07f, bottom + size * .07f, true).apply { offset(width - side * .8f, 0f) }, size)
        return compose(children, width, ink).copy(ascent = top - size * .07f, descent = bottom + size * .07f)
    }
}
