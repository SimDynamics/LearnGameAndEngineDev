package com.learngraphicsprogramming.app

import android.content.Context
import android.util.AttributeSet
import android.view.View
import androidx.appcompat.widget.AppCompatTextView

/** A gutter owns its natural width; resizing code or its font cannot split a numeric label. */
class CodeLineNumbersView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = android.R.attr.textViewStyle
) : AppCompatTextView(context, attrs, defStyleAttr) {
    init {
        ellipsize = null
        // Explicit newlines remain intact. Each complete number fits in the measured column.
        setHorizontallyScrolling(false)
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val width = CodeLineNumberMetrics.requiredWidth(
            text.toString(), paint::measureText, compoundPaddingLeft + compoundPaddingRight,
            (18 * resources.displayMetrics.density).toInt()
        )
        // Update the parent's accounting as well as this TextView's measure. In particular,
        // cached lesson cards may be rescaled without constructing a new gutter.
        layoutParams?.let { if (it.width != width) it.width = width }
        super.onMeasure(View.MeasureSpec.makeMeasureSpec(width, View.MeasureSpec.EXACTLY), heightMeasureSpec)
    }
}
