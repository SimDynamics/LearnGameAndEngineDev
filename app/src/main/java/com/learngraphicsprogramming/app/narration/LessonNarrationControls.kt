package com.learngraphicsprogramming.app.narration

import android.app.Activity
import android.content.res.ColorStateList
import android.graphics.Color
import android.graphics.drawable.GradientDrawable
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.view.ViewConfiguration
import android.widget.FrameLayout
import android.widget.ImageButton
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import com.learngraphicsprogramming.app.AppearanceSettingsRepository
import com.learngraphicsprogramming.app.R
import kotlin.math.abs

/** One draggable widget for either engine; coordinates persist as viewport fractions. */
class LessonNarrationControls(
    private val activity: Activity,
    private val onListen: () -> Unit,
    private val onRateChanged: () -> Unit,
    private val onSeek: (Int) -> Unit = {},
    private val onRestart: () -> Unit = {}
) {
    private val root = activity.findViewById<FrameLayout>(android.R.id.content)
    private var position = NarrationSettingsRepository.position(activity)
    private var closed = false
    private val row = DraggableRow().apply {
        id = R.id.lessonNarrationControls
        orientation = LinearLayout.HORIZONTAL
        gravity = Gravity.CENTER
        elevation = dp(8).toFloat()
        setPadding(dp(4), dp(4), dp(4), dp(4))
        background = GradientDrawable().apply { setColor(Color.rgb(32, 33, 38)); cornerRadius = dp(30).toFloat() }
    }
    private val listen = ImageButton(activity).apply {
        id = R.id.buttonLessonListen
        setImageResource(R.drawable.ic_person_speaking)
        setPadding(dp(11), dp(11), dp(11), dp(11))
        setBackgroundResource(android.R.drawable.list_selector_background)
        setOnClickListener { onListen() }
    }
    private val rate = control(activity.getString(R.string.narration_rate_normal), activity.getString(R.string.narration_change_rate)) {
        val settings = NarrationSettingsRepository.get(activity)
        NarrationSettingsRepository.saveRate(activity, NarrationSettingsRepository.nextRate(settings.rate))
        refresh(listen.isActivated)
        onRateChanged()
    }.apply { id = R.id.buttonLessonSpeechRate }
    private val layoutListener = View.OnLayoutChangeListener { _, _, _, _, _, _, _, _, _ -> positionRow() }

    init {
        row.addView(control(activity.getString(R.string.narration_control_back_three), activity.getString(R.string.narration_back_three)) { onSeek(-3) }.apply { id = R.id.buttonLessonBackThree }, itemParams())
        row.addView(control(activity.getString(R.string.narration_control_back_five), activity.getString(R.string.narration_back_five)) { onSeek(-5) }.apply {
            id = R.id.buttonLessonBackFive
            setOnLongClickListener { onRestart(); Toast.makeText(activity, activity.getString(R.string.narration_restarted), Toast.LENGTH_SHORT).show(); true }
        }, itemParams())
        row.addView(control(activity.getString(R.string.narration_control_forward_five), activity.getString(R.string.narration_forward_five)) { onSeek(5) }.apply { id = R.id.buttonLessonForwardFive }, itemParams())
        row.addView(listen, itemParams())
        row.addView(rate, itemParams())
        root.addView(row, FrameLayout.LayoutParams(dp(256), dp(56), Gravity.TOP or Gravity.START))
        root.addOnLayoutChangeListener(layoutListener)
        row.addOnLayoutChangeListener(layoutListener)
        row.post { positionRow() }
        refresh(false)
    }

    fun refresh(playing: Boolean) {
        val settings = NarrationSettingsRepository.get(activity)
        val route = activity.intent.getStringExtra("routeId") ?: AppearanceSettingsRepository.ROUTE_PROGRAMMING
        val accent = AppearanceSettingsRepository.getRouteAccentColor(activity, route)
        listen.isActivated = playing
        listen.imageTintList = ColorStateList.valueOf(if (playing) accent else Color.WHITE)
        listen.contentDescription = if (playing) activity.getString(R.string.narration_pause) else activity.getString(R.string.narration_start_resume)
        rate.text = NarrationSettingsRepository.rateLabel(activity, settings.rate)
        rate.setTextColor(accent)
        rate.contentDescription = activity.getString(R.string.narration_rate_description, rate.text)
    }

    fun setVisible(visible: Boolean) {
        row.visibility = if (visible) View.VISIBLE else View.GONE
        if (visible) row.post { positionRow() }
    }

    fun close() {
        closed = true
        root.removeOnLayoutChangeListener(layoutListener)
        row.removeOnLayoutChangeListener(layoutListener)
        root.removeView(row)
    }

    private fun bounds(): FloatArray {
        val insets = ViewCompat.getRootWindowInsets(root)?.getInsets(WindowInsetsCompat.Type.systemBars() or WindowInsetsCompat.Type.displayCutout())
        val left = maxOf(dp(4), insets?.left ?: 0).toFloat()
        val top = maxOf(dp(4), insets?.top ?: 0).toFloat()
        val right = maxOf(left, root.width - row.width - maxOf(dp(4), insets?.right ?: 0).toFloat())
        val bottom = maxOf(top, root.height - row.height - maxOf(dp(4), insets?.bottom ?: 0).toFloat())
        return floatArrayOf(left, top, right, bottom)
    }

    private fun positionRow() {
        if (closed || row.dragging || root.width <= 0 || root.height <= 0) return
        val insets = ViewCompat.getRootWindowInsets(root)?.getInsets(WindowInsetsCompat.Type.systemBars() or WindowInsetsCompat.Type.displayCutout())
        val available = (root.width - maxOf(dp(4), insets?.left ?: 0) - maxOf(dp(4), insets?.right ?: 0)).coerceAtLeast(1)
        val width = minOf(dp(256), available)
        if (row.layoutParams.width != width) { row.layoutParams = (row.layoutParams as FrameLayout.LayoutParams).apply { this.width = width }; return }
        val b = bounds()
        row.x = b[0] + position.first * (b[2] - b[0])
        row.y = b[1] + position.second * (b[3] - b[1])
    }

    private fun control(label: String, description: String, action: () -> Unit) = TextView(activity).apply {
        text = label; contentDescription = description; gravity = Gravity.CENTER; textSize = 16f
        setTypeface(typeface, android.graphics.Typeface.BOLD); setTextColor(Color.WHITE)
        isClickable = true; isFocusable = true; setBackgroundResource(android.R.drawable.list_selector_background)
        setOnClickListener { action() }
    }
    private fun itemParams() = LinearLayout.LayoutParams(0, dp(48), 1f)
    private fun dp(value: Int) = (value * activity.resources.displayMetrics.density).toInt()

    private inner class DraggableRow : LinearLayout(activity) {
        var dragging = false
        private var downX = 0f
        private var downY = 0f
        private var startX = 0f
        private var startY = 0f
        private val slop = ViewConfiguration.get(activity).scaledTouchSlop

        override fun onInterceptTouchEvent(event: MotionEvent): Boolean {
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN -> { dragging = false; downX = event.rawX; downY = event.rawY; startX = x; startY = y }
                MotionEvent.ACTION_MOVE -> if (abs(event.rawX - downX) > slop || abs(event.rawY - downY) > slop) {
                    dragging = true; parent.requestDisallowInterceptTouchEvent(true); return true
                }
            }
            return dragging
        }

        override fun onTouchEvent(event: MotionEvent): Boolean {
            if (!dragging) return super.onTouchEvent(event)
            val b = bounds()
            if (event.actionMasked == MotionEvent.ACTION_MOVE) {
                x = (startX + event.rawX - downX).coerceIn(b[0], b[2])
                y = (startY + event.rawY - downY).coerceIn(b[1], b[3])
            } else if (event.actionMasked == MotionEvent.ACTION_UP || event.actionMasked == MotionEvent.ACTION_CANCEL) {
                position = (if (b[2] == b[0]) 0f else (x - b[0]) / (b[2] - b[0])).coerceIn(0f, 1f) to
                    (if (b[3] == b[1]) 0f else (y - b[1]) / (b[3] - b[1])).coerceIn(0f, 1f)
                NarrationSettingsRepository.savePosition(activity, position.first, position.second)
                dragging = false
                parent.requestDisallowInterceptTouchEvent(false)
                positionRow()
            }
            return true
        }
    }
}
