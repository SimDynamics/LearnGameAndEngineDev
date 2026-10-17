package com.learngraphicsprogramming.app.math

import com.learngraphicsprogramming.app.R
import android.content.Context
import android.content.res.ColorStateList
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.RippleDrawable
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import org.json.JSONArray
import org.json.JSONObject

internal object MathWidgetUi {
    val foreground: Int = Color.rgb(230, 230, 230)
    val muted: Int = Color.rgb(184, 184, 184)
    fun dp(context: Context, value: Int): Int =
        (value * context.resources.displayMetrics.density).toInt()

    fun column(context: Context): LinearLayout = LinearLayout(context).apply {
        orientation = LinearLayout.VERTICAL
        layoutParams = LinearLayout.LayoutParams(-1, -2)
    }

    fun row(context: Context): LinearLayout = LinearLayout(context).apply {
        orientation = LinearLayout.HORIZONTAL
        gravity = Gravity.CENTER_VERTICAL
        layoutParams = LinearLayout.LayoutParams(-1, -2)
    }

    fun background(context: Context, selected: Boolean = false, accent: Int = foreground) =
        GradientDrawable().apply {
            setColor(context.getColor(R.color.lesson_card))
            cornerRadius = dp(context, 10).toFloat()
            setStroke(dp(context, 1), if (selected) accent else context.getColor(R.color.lesson_outline))
        }

    fun text(context: Context, value: String, bold: Boolean = false, mathMode: String = "auto"): TextView = MathTextView(context).apply {
        setMathContent(value, mathMode)
        textSize = 18f
        setTextColor(MathWidgetUi.foreground)
        if (bold) setTypeface(typeface, Typeface.BOLD)
        setPadding(0, dp(context, 6), 0, dp(context, 6))
        layoutParams = LinearLayout.LayoutParams(-1, -2)
    }

    /** The icon is visually compact; its touch/accessibility target remains 48 dp. */
    fun icon(context: Context, drawableId: Int, description: String, enabledColor: Int = foreground): TextView = TextView(context).apply {
        text = ""
        contentDescription = description
        gravity = Gravity.CENTER
        minWidth = dp(context, 48)
        minHeight = dp(context, 48)
        isClickable = true
        isFocusable = true
        setPadding(dp(context, 12), dp(context, 12), dp(context, 12), dp(context, 12))
        val drawable = context.getDrawable(drawableId)?.mutate()?.apply { setBounds(0, 0, dp(context, 24), dp(context, 24)) }
        setCompoundDrawables(drawable, null, null, null)
        compoundDrawableTintList = ColorStateList(
            arrayOf(intArrayOf(android.R.attr.state_enabled), intArrayOf()),
            intArrayOf(enabledColor, Color.rgb(120, 120, 120))
        )
        background = RippleDrawable(ColorStateList.valueOf(Color.argb(40, 230, 230, 230)), null, GradientDrawable().apply {
            shape = GradientDrawable.OVAL
            setColor(Color.WHITE)
        })
        layoutParams = LinearLayout.LayoutParams(dp(context, 48), dp(context, 48))
    }

    fun button(context: Context, label: String, accent: Int): Button = Button(context).apply {
        text = label
        isAllCaps = false
        textSize = 18f
        minHeight = dp(context, 48)
        val brightness = (Color.red(accent) * 299 + Color.green(accent) * 587 + Color.blue(accent) * 114) / 1000
        setTextColor(if (brightness >= 160) Color.rgb(24, 24, 24) else Color.WHITE)
        backgroundTintList = ColorStateList.valueOf(accent)
        layoutParams = LinearLayout.LayoutParams(-2, -2)
    }

    fun rich(context: Context, item: JSONObject, accent: Int): View {
        val result = column(context)
        val content = item.optJSONArray("content")
        if (content == null || content.length() == 0) {
            result.addView(text(context, item.optString("label", item.optString("text", item.optString("id")))))
        } else {
            for (index in 0 until content.length()) {
                val representation = content.optJSONObject(index)
                result.addView(if (representation != null) {
                    MathRepresentationRenderer.render(context, representation, accent)
                } else text(context, content.optString(index)))
            }
        }
        val explanation = item.optString("explanation")
        if (explanation.isNotBlank()) result.addView(text(context, explanation))
        fun allowCardTap(view: View) {
            if (view is TextView) view.setTextIsSelectable(false)
            if (view is ViewGroup) for (index in 0 until view.childCount) allowCardTap(view.getChildAt(index))
        }
        allowCardTap(result)
        return result
    }

    fun objects(array: JSONArray?): List<JSONObject> =
        if (array == null) emptyList() else (0 until array.length()).map { array.getJSONObject(it) }
}
