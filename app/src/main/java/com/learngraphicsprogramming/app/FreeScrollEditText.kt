package com.learngraphicsprogramming.app

import android.content.Context
import android.text.Editable
import android.text.TextWatcher
import android.util.AttributeSet
import android.view.KeyEvent
import androidx.appcompat.widget.AppCompatEditText

class FreeScrollEditText @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = androidx.appcompat.R.attr.editTextStyle
) : AppCompatEditText(context, attrs, defStyleAttr) {

    private val cppTabStops = CppEditorTabStops()

    internal var cppHardwareKeyHandler: ((Int, KeyEvent) -> Boolean)? = null
    internal var cppEditorActionHandler: ((Int) -> Boolean)? = null
    internal var cppEditorViewport: CppEditorViewportController? = null

    init {
        addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(text: CharSequence?, start: Int, count: Int, after: Int) = Unit
            override fun onTextChanged(text: CharSequence?, start: Int, before: Int, count: Int) = Unit
            override fun afterTextChanged(text: Editable?) {
                if (text != null) cppTabStops.update(text, paint, edited = true)
            }
        })
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        text?.let { cppTabStops.update(it, paint) }
        super.onMeasure(widthMeasureSpec, heightMeasureSpec)
    }

    override fun onKeyPreIme(keyCode: Int, event: KeyEvent): Boolean {
        if (cppHardwareKeyHandler?.invoke(keyCode, event) == true) return true
        return super.onKeyPreIme(keyCode, event)
    }

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (cppHardwareKeyHandler?.invoke(event.keyCode, event) == true) return true
        return super.dispatchKeyEvent(event)
    }

    override fun onEditorAction(actionCode: Int) {
        // Some IMEs follow a handled physical TAB with a separate NEXT action.
        // C++ inputs consume that navigation without inserting a second TAB.
        if (cppEditorActionHandler?.invoke(actionCode) == true) return
        super.onEditorAction(actionCode)
    }

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
