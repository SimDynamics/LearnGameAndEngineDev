package com.learngraphicsprogramming.app

import android.content.Context
import android.graphics.Color
import android.graphics.Typeface
import android.text.Editable
import android.text.TextWatcher
import android.view.Gravity
import android.view.View
import android.widget.LinearLayout
import android.widget.PopupMenu
import android.widget.TextView
import com.learngraphicsprogramming.app.CppLabPresentationPolicy.Channel

/** One displayed console; the three original result channels retain their identity and bytes. */
internal class CppConsoleView(context: Context) : LinearLayout(context) {
    val diagnostics = channel("cpp_compilation_output")
    val stdout = channel("cpp_stdout")
    val stderr = channel("cpp_stderr")
    val display = TextView(context).apply {
        tag = "cpp_console_display"
        typeface = Typeface.MONOSPACE
        textSize = 18f
        setTextColor(Color.rgb(121, 226, 157))
        setPadding(dp(16), dp(12), dp(16), dp(16))
        minHeight = dp(72)
        setTextIsSelectable(true)
    }
    private val title = TextView(context).apply {
        tag = "cpp_console_title"
        textSize = 14f
        setTextColor(Color.rgb(174, 180, 190))
        setPadding(dp(16), dp(8), dp(8), dp(8))
    }
    private val selector = TextView(context).apply {
        tag = "cpp_console_channels"
        textSize = 14f
        setTextColor(Color.rgb(213, 219, 230))
        gravity = Gravity.CENTER
        minHeight = dp(48)
        setPadding(dp(12), 0, dp(12), 0)
        text = "▾"
        contentDescription = context.getString(R.string.cpp_console_channel_selector)
        isFocusable = true
    }
    private var selected: Channel? = null
    private var userSelected = false
    private var refreshing = false
    private var menu: PopupMenu? = null

    init {
        tag = "cpp_console"
        orientation = VERTICAL
        setBackgroundColor(Color.rgb(5, 6, 7))
        val header = LinearLayout(context).apply {
            orientation = HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            addView(title, LayoutParams(0, LayoutParams.WRAP_CONTENT, 1f))
            addView(selector, LayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT))
        }
        addView(header, LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT))
        addView(display, LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT))
        listOf(diagnostics, stdout, stderr).forEach { source ->
            // Existing compilation, CMake and QA consumers keep these raw channels.
            // GONE providers occupy no console space and expose no accessibility node.
            addView(source)
            source.addTextChangedListener(object : TextWatcher {
                override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) = Unit
                override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) = refresh()
                override fun afterTextChanged(s: Editable?) = Unit
            })
        }
        selector.setOnClickListener { openChannels() }
        refresh()
    }

    /** Result handlers may emphasize a failing phase without modifying any result channel. */
    fun present(preferred: Channel? = null) {
        userSelected = false
        selected = preferred
        refresh(preferred)
    }

    fun selectChannel(channel: Channel) {
        val state = state()
        if (channel !in state.channels) return
        userSelected = true
        selected = channel
        refresh(channel)
    }

    private fun channel(viewTag: String) = TextView(context).apply {
        tag = viewTag
        visibility = View.GONE
        importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
    }

    private fun state(preferred: Channel? = if (userSelected) selected else null) =
        CppLabPresentationPolicy.console(diagnostics.text.toString(), stdout.text.toString(), stderr.text.toString(), preferred)

    private fun refresh(preferred: Channel? = if (userSelected) selected else null) {
        if (refreshing) return
        refreshing = true
        try {
            menu?.dismiss()
            menu = null
            val state = state(preferred)
            selected = state.selected
            val source = when (state.selected) {
                Channel.DIAGNOSTICS -> diagnostics
                Channel.STDOUT -> stdout
                Channel.STDERR -> stderr
            }
            if (display.text.toString() != source.text.toString()) display.text = source.text
            title.setText(label(state.selected))
            selector.visibility = if (state.channels.size > 1) View.VISIBLE else View.GONE
        } finally { refreshing = false }
    }

    private fun openChannels() {
        val available = state(selected).channels
        if (available.size <= 1) return
        menu?.dismiss()
        menu = PopupMenu(context, selector).apply {
            available.forEach { channel ->
                menu.add(0, channel.ordinal, channel.ordinal, label(channel)).apply {
                    isCheckable = true
                    isChecked = channel == selected
                }
            }
            menu.setGroupCheckable(0, true, true)
            setOnMenuItemClickListener { item ->
                selectChannel(Channel.entries[item.itemId])
                true
            }
            show()
        }
    }

    private fun label(channel: Channel): Int = when (channel) {
        Channel.DIAGNOSTICS -> R.string.cpp_build_diagnostics
        Channel.STDOUT -> R.string.cpp_program_stdout
        Channel.STDERR -> R.string.cpp_program_stderr
    }

    override fun onDetachedFromWindow() {
        menu?.dismiss()
        menu = null
        super.onDetachedFromWindow()
    }

    private fun dp(value: Int) = (value * resources.displayMetrics.density + .5f).toInt()
}
