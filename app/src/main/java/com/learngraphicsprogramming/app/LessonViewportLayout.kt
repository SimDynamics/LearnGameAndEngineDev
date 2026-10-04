package com.learngraphicsprogramming.app

import android.content.Context
import android.util.AttributeSet
import android.view.View
import android.widget.EditText
import android.widget.LinearLayout
import com.learngraphicsprogramming.app.math.MathExpressionEditor
import com.learngraphicsprogramming.app.math.MathInputFocusOrder

/** Prevents a pending window resize from measuring the focused lesson scroll to zero. */
class LessonViewportLayout @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0
) : LinearLayout(context, attrs, defStyleAttr) {

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val focused = findFocus()
        val isLessonEditor = focused is MathExpressionEditor ||
            (focused is EditText && (focused.tag == MathInputFocusOrder.GESTURE_OWNER_TAG ||
                focused.tag == "cpp_code_input"))
        if (orientation == VERTICAL && MeasureSpec.getMode(heightMeasureSpec) != MeasureSpec.UNSPECIFIED &&
            isLessonEditor && focused?.hasFocus() == true) {
            val header = findViewById<View>(R.id.lessonHeader)
            val scroll = findViewById<View>(R.id.lessonScroll)
            if (header?.parent === this && scroll?.parent === this && header.visibility == View.VISIBLE &&
                scroll.visibility != View.GONE && generateSequence(focused) { it.parent as? View }.any { it === scroll }) {
                val naturalHeightSpec = MeasureSpec.makeMeasureSpec(0, MeasureSpec.UNSPECIFIED)
                measureChildWithMargins(header, widthMeasureSpec, 0, naturalHeightSpec, 0)
                val headerMargins = header.layoutParams as LayoutParams
                val scrollMargins = scroll.layoutParams as LayoutParams
                var reservedHeight = paddingTop + paddingBottom + header.measuredHeight +
                    headerMargins.topMargin + headerMargins.bottomMargin +
                    scrollMargins.topMargin + scrollMargins.bottomMargin
                for (index in 0 until childCount) {
                    val child = getChildAt(index)
                    if (child !== header && child !== scroll && child.visibility != View.GONE) {
                        measureChildWithMargins(child, widthMeasureSpec, 0, naturalHeightSpec, 0)
                        val margins = child.layoutParams as LayoutParams
                        reservedHeight += child.measuredHeight + margins.topMargin + margins.bottomMargin
                    }
                }
                if (reservedHeight >= MeasureSpec.getSize(heightMeasureSpec)) {
                    // Insets/configuration observers can still see the previous large viewport.
                    // This is the actual incoming size, before Android can clear descendant focus.
                    header.visibility = View.GONE
                }
            }
        }
        super.onMeasure(widthMeasureSpec, heightMeasureSpec)
    }
}
