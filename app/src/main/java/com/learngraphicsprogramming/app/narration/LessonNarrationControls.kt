package com.learngraphicsprogramming.app.narration

import android.app.Activity
import android.content.res.ColorStateList
import android.graphics.Color
import android.graphics.drawable.GradientDrawable
import android.view.Gravity
import android.view.InputDevice
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
import com.learngraphicsprogramming.app.FloatingPoint
import com.learngraphicsprogramming.app.FloatingReaderPlacement
import com.learngraphicsprogramming.app.FloatingReaderDragGate
import com.learngraphicsprogramming.app.R
import kotlin.math.abs

/** One draggable control strip. Off-screen placement is intentional until explicitly recovered. */
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
    private var recoverPosition = false
    private val availability = NarrationControlAvailability()
    private val row = DraggableRow().apply {
        id = R.id.lessonNarrationControls
        orientation = LinearLayout.HORIZONTAL
        gravity = Gravity.CENTER
        isClickable = true
        elevation = dp(8).toFloat()
        setPadding(dp(4),dp(4),dp(4),dp(4))
        background = GradientDrawable().apply { setColor(activity.getColor(R.color.lesson_card)); cornerRadius = dp(30).toFloat() }
    }
    private val listen = ImageButton(activity).apply {
        id = R.id.buttonLessonListen
        setImageResource(R.drawable.ic_person_speaking)
        setPadding(dp(11),dp(11),dp(11),dp(11))
        setBackgroundResource(android.R.drawable.list_selector_background)
        setOnClickListener { if (narrationEnabled()) onListen() }
    }
    private val rate = control(activity.getString(R.string.narration_rate_normal),
        activity.getString(R.string.narration_change_rate)) {
        val settings = NarrationSettingsRepository.get(activity)
        NarrationSettingsRepository.saveRate(activity,NarrationSettingsRepository.nextRate(settings.rate))
        refresh(listen.isActivated)
        onRateChanged()
    }.apply { id = R.id.buttonLessonSpeechRate }
    private val restart = ImageButton(activity).apply {
        id = R.id.buttonLessonRestartPage
        setImageResource(R.drawable.ic_code_reset)
        imageTintList = ColorStateList.valueOf(Color.rgb(170,174,183))
        setPadding(dp(12),dp(12),dp(12),dp(12))
        contentDescription = activity.getString(R.string.narration_restart_current_page)
        setBackgroundResource(android.R.drawable.list_selector_background)
        setOnClickListener { if (narrationEnabled()) onRestart() }
    }
    private val layoutListener = View.OnLayoutChangeListener { _,_,_,_,_,_,_,_,_ -> positionRow() }

    init {
        row.addView(control(activity.getString(R.string.narration_control_back_three),
            activity.getString(R.string.narration_back_three)) { onSeek(-3) }
            .apply { id = R.id.buttonLessonBackThree },itemParams())
        row.addView(control(activity.getString(R.string.narration_control_back_five),
            activity.getString(R.string.narration_back_five)) { onSeek(-5) }.apply {
                id = R.id.buttonLessonBackFive
                setOnLongClickListener {
                    if (narrationEnabled()) {
                        onRestart()
                        Toast.makeText(activity,activity.getString(R.string.narration_restarted),Toast.LENGTH_SHORT).show()
                    }
                    true
                }
            },itemParams())
        row.addView(control(activity.getString(R.string.narration_control_forward_five),
            activity.getString(R.string.narration_forward_five)) { onSeek(5) }
            .apply { id = R.id.buttonLessonForwardFive },itemParams())
        row.addView(control(activity.getString(R.string.narration_control_forward_three),
            activity.getString(R.string.narration_forward_three)) { onSeek(3) }
            .apply { id = R.id.buttonLessonForwardThree },itemParams())
        row.addView(listen,itemParams())
        row.addView(rate,itemParams())
        row.addView(restart,itemParams())
        root.addView(row,FrameLayout.LayoutParams(dp(344),dp(56),Gravity.TOP or Gravity.LEFT))
        root.addOnLayoutChangeListener(layoutListener)
        row.addOnLayoutChangeListener(layoutListener)
        row.post { positionRow() }
        refresh(false)
    }

    fun refresh(playing: Boolean) {
        val settings = NarrationSettingsRepository.get(activity)
        if (settings.enabled) availability.apply(row, true)
        val route = activity.intent.getStringExtra("routeId") ?: AppearanceSettingsRepository.ROUTE_PROGRAMMING
        val accent = AppearanceSettingsRepository.getRouteAccentColor(activity,route)
        listen.isActivated = playing
        listen.imageTintList = ColorStateList.valueOf(if (playing) accent else Color.WHITE)
        listen.contentDescription = activity.getString(if (playing) R.string.narration_pause else R.string.narration_start_resume)
        rate.text = NarrationSettingsRepository.rateLabel(activity,settings.rate)
        rate.setTextColor(accent)
        rate.contentDescription = activity.getString(R.string.narration_rate_description,rate.text)
        if (!settings.enabled) availability.apply(row, false)
    }

    fun setVisible(visible: Boolean) {
        row.visibility = if (visible) View.VISIBLE else View.GONE
        if (visible) row.post { positionRow() }
    }

    /** Called only by the head shortcut, not by passive layout or focus refresh. */
    fun restoreVisiblePosition() {
        if (!narrationEnabled()) return
        recoverPosition = true
        positionRow()
        row.post { positionRow() }
    }
    fun isOutsideViewport(): Boolean {
        if (closed || row.visibility != View.VISIBLE) return true
        val b = bounds()
        return FloatingReaderPlacement.outside(FloatingPoint(row.x,row.y),b[0],b[1],b[2],b[3])
    }
    fun close() {
        if (closed) return
        closed = true
        root.removeOnLayoutChangeListener(layoutListener)
        row.removeOnLayoutChangeListener(layoutListener)
        root.removeView(row)
    }

    private fun bounds(): FloatArray {
        val insets = ViewCompat.getRootWindowInsets(root)?.getInsets(
            WindowInsetsCompat.Type.systemBars() or WindowInsetsCompat.Type.displayCutout())
        val left = maxOf(dp(4),insets?.left ?: 0).toFloat()
        val top = maxOf(dp(4),insets?.top ?: 0).toFloat()
        val right = maxOf(left,root.width-row.width-maxOf(dp(4),insets?.right ?: 0).toFloat())
        val bottom = maxOf(top,root.height-row.height-maxOf(dp(4),insets?.bottom ?: 0).toFloat())
        return floatArrayOf(left,top,right,bottom)
    }
    private fun positionRow() {
        if (closed || row.dragging || root.width <= 0 || root.height <= 0) return
        val insets = ViewCompat.getRootWindowInsets(root)?.getInsets(
            WindowInsetsCompat.Type.systemBars() or WindowInsetsCompat.Type.displayCutout())
        val available = (root.width-maxOf(dp(4),insets?.left ?: 0)-maxOf(dp(4),insets?.right ?: 0)).coerceAtLeast(1)
        val width = minOf(dp(344),available)
        if (row.layoutParams.width != width) {
            row.layoutParams = (row.layoutParams as FrameLayout.LayoutParams).apply { this.width = width }
            return
        }
        val b = bounds()
        var point = FloatingPoint(b[0]+position.first*maxOf(1f,b[2]-b[0]),
            b[1]+position.second*maxOf(1f,b[3]-b[1]))
        if (recoverPosition) {
            point = FloatingReaderPlacement.visible(point,b[0],b[1],b[2],b[3])
            recoverPosition = false
        }
        row.x = point.x; row.y = point.y
        savePosition(b)
    }
    private fun savePosition(b: FloatArray = bounds()) {
        position = (row.x-b[0])/maxOf(1f,b[2]-b[0]) to (row.y-b[1])/maxOf(1f,b[3]-b[1])
        NarrationSettingsRepository.savePosition(activity,position.first,position.second)
    }
    private fun control(label: String, description: String, action: () -> Unit) = TextView(activity).apply {
        text = label; contentDescription = description; gravity = Gravity.CENTER; textSize = 16f
        setTypeface(typeface,android.graphics.Typeface.BOLD); setTextColor(Color.WHITE)
        isClickable = true; isFocusable = true
        setBackgroundResource(android.R.drawable.list_selector_background)
        setOnClickListener { if (narrationEnabled()) action() }
    }
    private fun narrationEnabled() = !closed && NarrationSettingsRepository.get(activity).enabled
    private fun itemParams() = LinearLayout.LayoutParams(0,dp(48),1f)
    private fun dp(value: Int) = (value*activity.resources.displayMetrics.density).toInt()

    private inner class DraggableRow : LinearLayout(activity) {
        var dragging = false
        private val dragGate = FloatingReaderDragGate()
        private var downX = 0f; private var downY = 0f
        private var startX = 0f; private var startY = 0f
        private val slop = ViewConfiguration.get(activity).scaledTouchSlop

        private fun fromMouse(event: MotionEvent): Boolean =
            event.isFromSource(InputDevice.SOURCE_MOUSE) ||
                (event.pointerCount > 0 && event.getToolType(0) == MotionEvent.TOOL_TYPE_MOUSE)

        private fun primaryPressed(event: MotionEvent): Boolean =
            (event.buttonState and MotionEvent.BUTTON_PRIMARY) != 0

        private fun movedBeyondSlop(event: MotionEvent): Boolean =
            abs(event.rawX-downX) > slop || abs(event.rawY-downY) > slop

        private fun begin(event: MotionEvent) {
            dragging = false
            downX = event.rawX; downY = event.rawY; startX = x; startY = y
            dragGate.begin(event.eventTime, fromMouse(event), primaryPressed(event))
        }

        private fun endGesture() {
            dragging = false
            dragGate.end()
            parent?.requestDisallowInterceptTouchEvent(false)
        }

        override fun dispatchGenericMotionEvent(event: MotionEvent): Boolean {
            if (!narrationEnabled()) return true
            if (event.actionMasked == MotionEvent.ACTION_SCROLL) return true
            return super.dispatchGenericMotionEvent(event)
        }

        override fun onInterceptTouchEvent(event: MotionEvent): Boolean {
            if (!narrationEnabled()) { endGesture(); return true }
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN -> begin(event)
                MotionEvent.ACTION_POINTER_DOWN -> dragGate.reject()
                MotionEvent.ACTION_MOVE -> {
                    if (event.pointerCount != 1) dragGate.reject()
                    if (dragGate.mayDrag(event.eventTime, movedBeyondSlop(event),
                            fromMouse(event), primaryPressed(event))) {
                        dragging = true
                        parent?.requestDisallowInterceptTouchEvent(true)
                    }
                }
            }
            // Consume fast synthetic swipes so they cannot click a child button.
            return dragging || dragGate.isRejected
        }

        override fun onTouchEvent(event: MotionEvent): Boolean {
            if (!narrationEnabled()) { endGesture(); return true }
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN -> {
                    begin(event)
                    return true
                }
                MotionEvent.ACTION_POINTER_DOWN -> {
                    dragGate.reject()
                    return true
                }
                MotionEvent.ACTION_MOVE -> {
                    if (event.pointerCount != 1) dragGate.reject()
                    if (dragGate.mayDrag(event.eventTime, movedBeyondSlop(event),
                            fromMouse(event), primaryPressed(event))) {
                        dragging = true
                        parent?.requestDisallowInterceptTouchEvent(true)
                        // Screen clamping is deliberately handled by the header shortcut.
                        x = startX + event.rawX-downX
                        y = startY + event.rawY-downY
                    }
                    return true
                }
                MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                    val accepted = dragGate.canClick
                    if (accepted && dragging) savePosition()
                    else if (accepted && event.actionMasked == MotionEvent.ACTION_UP &&
                        !movedBeyondSlop(event)) performClick()
                    endGesture()
                    return true
                }
                else -> return super.onTouchEvent(event)
            }
        }

        override fun performClick(): Boolean { super.performClick(); return true }
    }
}
