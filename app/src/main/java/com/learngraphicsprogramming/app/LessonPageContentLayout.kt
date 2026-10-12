package com.learngraphicsprogramming.app

import android.content.Context
import android.util.AttributeSet
import android.view.View
import android.widget.LinearLayout

/** Small horizontal inset for page content only, not the toolbar or fullscreen overlays. */
class LessonPageContentLayout @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0
) : LinearLayout(context, attrs, defStyleAttr) {
    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val availableWidth = if (View.MeasureSpec.getMode(widthMeasureSpec) == View.MeasureSpec.UNSPECIFIED)
            width else View.MeasureSpec.getSize(widthMeasureSpec)
        val inset = LessonReadingLayoutPolicy.horizontalInset(
            availableWidth, resources.displayMetrics.density
        )
        // Do not add to the old padding: repeated measures must not accumulate margins.
        // Route setup may reset the horizontal padding; reapply it before measuring children.
        if (paddingLeft != inset || paddingRight != inset) {
            setPaddingRelative(inset, paddingTop, inset, paddingBottom)
        }
        super.onMeasure(widthMeasureSpec, heightMeasureSpec)
    }
}
