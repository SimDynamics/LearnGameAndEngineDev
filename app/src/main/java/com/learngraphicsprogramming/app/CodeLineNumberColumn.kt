package com.learngraphicsprogramming.app

import android.content.Context
import android.util.AttributeSet
import android.view.View
import android.widget.LinearLayout
import android.widget.TextView

/** Shared full-label width for numbered interactive code rows. */
class CodeLineNumberColumn @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0
) : LinearLayout(context, attrs, defStyleAttr) {
    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        var contentWidth = (18 * resources.displayMetrics.density).toInt()
        for (index in 0 until childCount) {
            val label = getChildAt(index) as? TextView ?: continue
            contentWidth = maxOf(contentWidth, CodeLineNumberMetrics.requiredWidth(
                label.text.toString(), label.paint::measureText,
                label.compoundPaddingLeft + label.compoundPaddingRight, 0
            ))
        }
        val width = contentWidth + paddingLeft + paddingRight
        layoutParams?.let { if (it.width != width) it.width = width }
        super.onMeasure(View.MeasureSpec.makeMeasureSpec(width, View.MeasureSpec.EXACTLY), heightMeasureSpec)
    }
}
