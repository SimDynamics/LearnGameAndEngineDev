package com.learngraphicsprogramming.app

import android.content.Context
import androidx.appcompat.widget.AppCompatEditText

class FreeScrollEditText(
    context: Context
) : AppCompatEditText(context) {

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