package com.learngraphicsprogramming.app

import android.content.Context
import android.util.AttributeSet
import androidx.appcompat.widget.AppCompatEditText

class FreeScrollEditText @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = androidx.appcompat.R.attr.editTextStyle
) : AppCompatEditText(context, attrs, defStyleAttr) {

    /*
     * false:
     * Android puede llevar automáticamente
     * la vista hacia el cursor.
     *
     * true:
     * permitimos navegar libremente por el
     * código sin que el cursor nos regrese.
     */
    var suppressCursorAutoScroll =
        false

    override fun bringPointIntoView(
        offset: Int
    ): Boolean {

        if (
            suppressCursorAutoScroll
        ) {

            return false
        }

        return super.bringPointIntoView(
            offset
        )
    }
}
