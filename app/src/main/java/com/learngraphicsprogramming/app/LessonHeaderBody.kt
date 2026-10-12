package com.learngraphicsprogramming.app

import android.content.Context
import android.util.AttributeSet
import android.view.View
import android.view.ViewGroup
import android.widget.FrameLayout
import android.widget.LinearLayout
import kotlin.math.roundToInt

/** Two rows on compact windows; vertically aligned labels and actions on wide windows. */
class LessonHeaderBody @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0
) : FrameLayout(context, attrs, defStyleAttr) {
    private lateinit var actions: LinearLayout
    private lateinit var labels: LinearLayout
    private var inline = false
    private var labelStart = 0

    override fun onFinishInflate() {
        super.onFinishInflate()
        actions = findViewById(R.id.lessonHeaderActions)
        labels = findViewById(R.id.lessonHeaderLabels)
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val outerWidth = View.MeasureSpec.getSize(widthMeasureSpec)
        val innerWidth = (outerWidth - paddingLeft - paddingRight).coerceAtLeast(0)
        val rowHeight = if (actions.layoutParams.height >= 0) actions.layoutParams.height else dp(48)
        actions.measure(exact(innerWidth), exact(rowHeight))

        val back = actions.findViewById<View>(R.id.buttonBack)
        labelStart = actions.paddingStart + back.measuredWidth + dp(4)
        val actionIds = intArrayOf(R.id.buttonNarrationToggle, R.id.buttonLessonLanguage,
            R.id.attemptIndicatorSlot, R.id.buttonLessonSettings)
        val endWidth = actions.paddingEnd + actionIds.sumOf { id ->
            val child = actions.findViewById<View>(id)
            val margins = child.layoutParams as? ViewGroup.MarginLayoutParams
            child.measuredWidth + (margins?.leftMargin ?: 0) + (margins?.rightMargin ?: 0)
        }
        val labelRoom = (innerWidth - labelStart - endWidth - dp(8)).coerceAtLeast(0)
        inline = LessonReadingLayoutPolicy.inlineHeader(innerWidth, labelRoom,
            resources.displayMetrics.density, resources.configuration.fontScale)
        labels.measure(exact(if (inline) labelRoom else innerWidth),
            View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED))

        val contentHeight = if (inline) maxOf(actions.measuredHeight, labels.measuredHeight)
            else actions.measuredHeight + labels.measuredHeight
        val height = maxOf(suggestedMinimumHeight, contentHeight + paddingTop + paddingBottom)
        setMeasuredDimension(View.resolveSize(outerWidth, widthMeasureSpec),
            View.resolveSize(height, heightMeasureSpec))
    }

    override fun onLayout(changed: Boolean, left: Int, top: Int, right: Int, bottom: Int) {
        val height = (bottom - top - paddingTop - paddingBottom).coerceAtLeast(0)
        if (inline) {
            val rowTop = paddingTop + LessonReadingLayoutPolicy.centeredTop(height, actions.measuredHeight)
            actions.layout(paddingLeft, rowTop, paddingLeft + actions.measuredWidth,
                rowTop + actions.measuredHeight)
            val labelsLeft = if (layoutDirection == View.LAYOUT_DIRECTION_RTL)
                right - left - paddingRight - labelStart - labels.measuredWidth
            else paddingLeft + labelStart
            val labelsTop = paddingTop + LessonReadingLayoutPolicy.centeredTop(height, labels.measuredHeight)
            labels.layout(labelsLeft, labelsTop, labelsLeft + labels.measuredWidth,
                labelsTop + labels.measuredHeight)
        } else {
            val contentHeight = actions.measuredHeight + labels.measuredHeight
            val rowTop = paddingTop + LessonReadingLayoutPolicy.centeredTop(height, contentHeight)
            actions.layout(paddingLeft, rowTop, paddingLeft + actions.measuredWidth,
                rowTop + actions.measuredHeight)
            val labelsTop = rowTop + actions.measuredHeight
            labels.layout(paddingLeft, labelsTop, paddingLeft + labels.measuredWidth,
                labelsTop + labels.measuredHeight)
        }
    }

    private fun dp(value: Int): Int = (value * resources.displayMetrics.density).roundToInt()
    private fun exact(value: Int): Int = View.MeasureSpec.makeMeasureSpec(value, View.MeasureSpec.EXACTLY)
}
