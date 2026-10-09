package com.learngraphicsprogramming.app.math

import com.learngraphicsprogramming.app.R
import android.content.Context
import android.content.res.ColorStateList
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Rect
import android.util.TypedValue
import androidx.appcompat.widget.AppCompatButton
import kotlin.math.ceil
import kotlin.math.max

/** A native Button whose icon uses exactly the lesson/editor mathematical layout. */
class MathKeyboardActionPreviewView(
    context: Context,
    private val action: MathKeyboardAction,
    expression: MathExpression.Sequence,
    accentColor: Int,
    textSizePx: Float
) : AppCompatButton(context) {
    private val previewPaint = Paint(Paint.ANTI_ALIAS_FLAG)
    private var cachedPreviewLayout: MathExpressionLayout? = null
    private var compactHeight: Int? = null
    private var normalVerticalPadding: Pair<Int, Int>? = null

    var previewExpression: MathExpression.Sequence = expression
        set(value) {
            if (field == value) return
            field = value
            contentDescription = accessibilityDescription()
            invalidatePreviewLayout()
        }

    /** The source editor's size; the compact key uses the same 14/18 ratio as text keys. */
    var previewTextSizePx: Float = textSizePx.takeIf { it.isFinite() && it > 0f }
        ?: TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_SP, 18f, resources.displayMetrics)
        set(value) {
            if (!value.isFinite() || value <= 0f || field == value) return
            field = value
            setTextSize(TypedValue.COMPLEX_UNIT_PX, value * COMPACT_TEXT_RATIO)
            invalidatePreviewLayout()
        }

    val previewLayout: MathExpressionLayout
        get() = cachedPreviewLayout ?: run {
            val size = previewTextSizePx * COMPACT_TEXT_RATIO
            val normal = MathExpressionLayout.create(previewExpression, previewPaint, size, showPlaceholders = true)
            val inkHeight = compactHeight?.let { (it - paddingTop - paddingBottom).coerceAtLeast(1) }
            if (inkHeight != null && normal.height > inkHeight) {
                MathExpressionLayout.create(previewExpression, previewPaint,
                    size * inkHeight / normal.height, showPlaceholders = true)
            } else normal
        }.also { cachedPreviewLayout = it }

    internal fun setCompactHeight(height: Int?) {
        if (compactHeight == height) return
        if (normalVerticalPadding == null) normalVerticalPadding = paddingTop to paddingBottom
        compactHeight = height
        val normal = requireNotNull(normalVerticalPadding)
        val compactPadding =
            0
        setPadding(paddingLeft, if (height == null) normal.first else compactPadding,
            paddingRight, if (height == null) normal.second else compactPadding)
        invalidatePreviewLayout()
    }

    init {
        text = ""
        isAllCaps = false
        isFocusable = false
        contentDescription = accessibilityDescription()
        backgroundTintList = ColorStateList.valueOf(accentColor)
        val brightness = (Color.red(accentColor) * 299 + Color.green(accentColor) * 587 + Color.blue(accentColor) * 114) / 1000
        setTextColor(if (brightness >= 160) Color.rgb(20, 23, 29) else Color.WHITE)
        minWidth = 0
        minHeight = 0
        minimumWidth = MathWidgetUi.dp(context, 48)
        minimumHeight = MathWidgetUi.dp(context, 48)
        val backgroundPadding = Rect()
        background?.getPadding(backgroundPadding)
        val breathingRoom = MathWidgetUi.dp(context, 4)
        val minimumHorizontalPadding = MathWidgetUi.dp(context, 6)
        val minimumVerticalPadding = MathWidgetUi.dp(context, 4)
        // Native Button backgrounds may inset their filled area. Keep every math stroke inside it.
        setPadding(max(minimumHorizontalPadding, backgroundPadding.left + breathingRoom),
            max(minimumVerticalPadding, backgroundPadding.top + breathingRoom),
            max(minimumHorizontalPadding, backgroundPadding.right + breathingRoom),
            max(minimumVerticalPadding, backgroundPadding.bottom + breathingRoom))
        // Save portrait padding before the toolbar replaces the background for landscape.
        // Assigning that drawable may change padding before setCompactHeight is called.
        normalVerticalPadding = paddingTop to paddingBottom
        setTextSize(TypedValue.COMPLEX_UNIT_PX, previewTextSizePx * COMPACT_TEXT_RATIO)
    }

    private fun invalidatePreviewLayout() {
        cachedPreviewLayout = null
        requestLayout()
        invalidate()
    }

    private fun accessibilityDescription(): String {
        if (action == MathKeyboardAction.Matrix) {
            val matrix = previewExpression.children.singleOrNull() as? MathExpression.Matrix
            if (matrix != null) return context.getString(R.string.math_ui_matrix_size, matrix.rows.size, matrix.rows.first().size)
        }
        return action.localizedDescription(context)
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        // Retain TextView/Button's native layout and drawable state even though its text is empty.
        super.onMeasure(widthMeasureSpec, heightMeasureSpec)
        val layout = previewLayout
        val width = max(suggestedMinimumWidth, ceil(layout.width).toInt() + paddingLeft + paddingRight)
        val height = max(suggestedMinimumHeight, ceil(layout.height).toInt() + paddingTop + paddingBottom)
        setMeasuredDimension(resolveSize(width, widthMeasureSpec), resolveSize(height, heightMeasureSpec))
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val layout = previewLayout
        val x = paddingLeft + (width - paddingLeft - paddingRight - layout.width) / 2f
        val baseline = paddingTop + (height - paddingTop - paddingBottom - layout.height) / 2f - layout.ascent
        previewPaint.color = currentTextColor
        layout.draw(canvas, previewPaint, x, baseline)
    }

    private companion object {
        const val COMPACT_TEXT_RATIO = 14f / 18f
    }
}
