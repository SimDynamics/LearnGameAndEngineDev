package com.learngraphicsprogramming.app

import android.content.res.Configuration
import android.text.Editable
import android.text.TextWatcher
import android.view.MotionEvent
import android.view.View
import android.view.ViewConfiguration
import android.view.ViewGroup
import android.view.ViewTreeObserver
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.TextView
import kotlin.math.abs

/** The lesson compiler's scrolling and gutter, shared with the standalone editor. */
internal class CppEditorViewportController(
    private val source: FreeScrollEditText,
    private val lineNumbers: TextView,
    private val lineNumberScroll: ScrollView,
    private val editorBody: ViewGroup,
    private val verticalBar: SeekBar,
    private val horizontalBar: SeekBar,
    private val isFullscreen: () -> Boolean,
    private val isKeyboardVisible: () -> Boolean,
    private val compactNormalContent: Boolean = false,
    private val onEditorGesture: () -> Unit = {}
) {
    private var disposed = false
    private var horizontalRange = 0
    private var wrapEnabled = false
    private var verticalRange = 0
    private var gestureAxis = 0
    private var nativeSelectionGesture = false
    private var startedInEmptySpace = false
    private var pendingEmptyDown: MotionEvent? = null
    private var delegatesGesture = false
    private var touchX = 0f
    private var touchY = 0f
    private var initialScrollX = 0
    private var initialScrollY = 0
    private var initialSelectionStart = 0
    private var initialSelectionEnd = 0
    private var lastTapTime = Long.MIN_VALUE
    private var lastTapX = 0f
    private var lastTapY = 0f
    private val configuration = ViewConfiguration.get(source.context)
    private val dragThreshold = maxOf(dp(2), configuration.scaledTouchSlop / 2).toFloat()
    private var observedTree: ViewTreeObserver? = null
    private val refreshTask = Runnable { if (!disposed && source.isAttachedToWindow) refreshNow() }
    private val revealTask = Runnable { if (!disposed) ensureCursorVisible() }
    private val globalLayout = ViewTreeObserver.OnGlobalLayoutListener {
        if (!disposed && source.isAttachedToWindow) refreshNow()
    }
    private val attachListener = object : View.OnAttachStateChangeListener {
        override fun onViewAttachedToWindow(view: View) {
            observeLayout()
            refresh()
        }
        override fun onViewDetachedFromWindow(view: View) {
            stopObservingLayout()
            source.removeCallbacks(refreshTask)
            source.removeCallbacks(revealTask)
            pendingEmptyDown?.recycle()
            pendingEmptyDown = null
        }
    }
    private val textWatcher = object : TextWatcher {
        override fun beforeTextChanged(text: CharSequence?, start: Int, count: Int, after: Int) = Unit
        override fun afterTextChanged(text: Editable?) = Unit
        override fun onTextChanged(text: CharSequence?, start: Int, before: Int, count: Int) {
            if (source.suppressCursorAutoScroll) {
                source.suppressCursorAutoScroll = false
                source.removeCallbacks(revealTask)
                source.post(revealTask)
            }
            refresh()
        }
    }

    init {
        source.cppEditorViewport = this
        // The custom bars avoid device-specific native EditText scrollbar drawables.
        source.isHorizontalScrollBarEnabled = false
        source.isVerticalScrollBarEnabled = false
        horizontalBar.contentDescription = source.context.getString(R.string.code_scroll_horizontal)
        verticalBar.contentDescription = source.context.getString(R.string.code_scroll_vertical)
        horizontalBar.max = 0
        verticalBar.max = 0
        source.addTextChangedListener(textWatcher)
        source.addOnAttachStateChangeListener(attachListener)
        if (source.isAttachedToWindow) observeLayout()
        source.setOnScrollChangeListener { _, _, _, _, _ -> synchronizeScroll() }
        source.setOnLongClickListener {
            if (startedInEmptySpace) true else {
                nativeSelectionGesture = true
                false // Let TextView start its native selection/action mode.
            }
        }
        source.setOnTouchListener { _, event -> handleSourceTouch(event) }
        horizontalBar.setOnSeekBarChangeListener(barListener(horizontal = true))
        verticalBar.setOnSeekBarChangeListener(barListener(horizontal = false))
        horizontalBar.setOnTouchListener { view, event ->
            ownBarTouch(view, event)
            // With the IME open, this remains the lesson's visual reference bar.
            isKeyboardVisible()
        }
        verticalBar.setOnTouchListener { view, event ->
            ownBarTouch(view, event)
            false
        }
        refresh()
    }

    fun refresh() {
        if (disposed || !source.isAttachedToWindow) return
        source.removeCallbacks(refreshTask)
        source.post(refreshTask)
    }

    private fun observeLayout() {
        if (disposed) return
        val tree = source.viewTreeObserver
        if (observedTree === tree) return
        stopObservingLayout()
        tree.addOnGlobalLayoutListener(globalLayout)
        observedTree = tree
    }

    private fun stopObservingLayout() {
        observedTree?.takeIf { it.isAlive }?.removeOnGlobalLayoutListener(globalLayout)
        observedTree = null
    }

    private fun refreshNow() {
        updateNormalHeight()
        updateLineNumbersNow()
        updateHorizontalBar()
        updateVerticalBar()
        synchronizeScroll()
    }

    fun updateLineNumbers() = refresh()

    private fun updateLineNumbersNow() {
        val layout = source.layout ?: return
        val text = source.text?.toString().orEmpty()
        var logicalLine = 1
        val numbers = buildString {
            for (visualLine in 0 until layout.lineCount) {
                val start = layout.getLineStart(visualLine)
                if (visualLine == 0) append(logicalLine)
                else if (start > 0 && text.getOrNull(start - 1) == '\n') append(++logicalLine)
                if (visualLine < layout.lineCount - 1) append('\n')
            }
        }
        if (lineNumbers.text.toString() != numbers) lineNumbers.text = numbers
        val width = CodeLineNumberMetrics.requiredWidth(
            numbers, lineNumbers.paint::measureText,
            maxOf(dp(8), lineNumbers.compoundPaddingLeft + lineNumbers.compoundPaddingRight), dp(18)
        )
        val params = lineNumberScroll.layoutParams
        if (params != null && params.width != width) {
            params.width = width
            lineNumberScroll.layoutParams = params
        }
    }

    fun updateBottomSafety() {
        if (disposed) return
        val landscapeIme = source.resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE &&
            isKeyboardVisible() && source.hasFocus()
        val bottom = when {
            landscapeIme -> 0
            isFullscreen() -> maxOf(source.lineHeight, lineNumbers.lineHeight) + dp(12)
            else -> dp(16)
        }
        for (view in listOf(source, lineNumbers)) {
            if (view.paddingBottom != bottom) view.setPadding(view.paddingLeft, view.paddingTop, view.paddingRight, bottom)
        }
    }

    fun updateNormalHeight() {
        if (disposed || isFullscreen()) return
        updateBottomSafety()
        val rows = if (compactNormalContent) {
            // Layout owns visual rows after wrapping. A final authored newline already
            // provides the editable row; reserve one only when it does not exist yet.
            val visualRows = source.layout?.lineCount ?: ((source.text?.count { it == '\n' } ?: 0) + 1)
            val editingRow = if (source.text?.lastOrNull() == '\n') 0 else 1
            (visualRows + editingRow).coerceIn(4, 10)
        } else 10
        val height = source.compoundPaddingTop + source.lineHeight * rows + source.compoundPaddingBottom
        val params = editorBody.layoutParams ?: return
        if (params.height != height) {
            params.height = height
            if (params is android.widget.LinearLayout.LayoutParams) params.weight = 0f
            editorBody.layoutParams = params
        }
    }

    fun updateHorizontalBar() {
        if (disposed) return
        horizontalRange = actualHorizontalRange()
        horizontalBar.max = horizontalRange
        horizontalBar.progress = source.scrollX.coerceIn(0, horizontalRange)
        val compact = isFullscreen() && isKeyboardVisible() &&
            source.resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE
        horizontalBar.visibility = if (horizontalRange > 0 && !compact) View.VISIBLE else View.GONE
        val clamped = source.scrollX.coerceIn(0, horizontalRange)
        if (clamped != source.scrollX) source.scrollTo(clamped, source.scrollY)
    }

    private fun actualHorizontalRange(): Int {
        val layout = source.layout
        val width = (source.width - source.totalPaddingLeft - source.totalPaddingRight).coerceAtLeast(1)
        // Android includes trailing whitespace in getLineWidth even when the
        // wrapped text fits. Wrapping must never retain a horizontal pan range.
        return if (wrapEnabled || layout == null || source.width <= 0) 0 else {
            var widest = 0f
            for (line in 0 until layout.lineCount) widest = maxOf(widest, layout.getLineWidth(line))
            (widest - width).toInt().coerceAtLeast(0)
        }
    }

    fun updateVerticalBar() {
        if (disposed) return
        updateBottomSafety()
        verticalRange = actualVerticalRange()
        verticalBar.max = verticalRange
        verticalBar.progress = source.scrollY.coerceIn(0, verticalRange)
        verticalBar.visibility = if (verticalRange > 0) View.VISIBLE else View.GONE
        val clamped = source.scrollY.coerceIn(0, verticalRange)
        if (clamped != source.scrollY) source.scrollTo(source.scrollX, clamped)
    }

    private fun actualVerticalRange(): Int {
        val layout = source.layout ?: return 0
        if (source.height <= 0) return 0
        val height = (source.height - source.totalPaddingTop - source.totalPaddingBottom).coerceAtLeast(1)
        return (layout.height - height).coerceAtLeast(0).let { if (it <= dp(1)) 0 else it }
    }

    /** Same Layout coordinates as TextView selection, without snapping blank space to a word. */
    private fun isTouchOnText(x: Float, y: Float): Boolean {
        val layout = source.layout ?: return true // Unknown geometry keeps native ownership.
        val textX = x + source.scrollX - source.totalPaddingLeft
        val textY = y + source.scrollY - source.totalPaddingTop
        if (textY < 0f || textY >= layout.height) return false
        val line = layout.getLineForVertical(textY.toInt())
        val text = source.text ?: return false
        if (layout.getLineEnd(line) > text.length) return true // A pending edit has not been laid out yet.
        if ((layout.getLineStart(line) until layout.getLineEnd(line)).all { text[it].isWhitespace() }) return false
        val left = minOf(layout.getLineLeft(line), layout.getLineRight(line))
        val right = maxOf(layout.getLineLeft(line), layout.getLineRight(line))
        return textX >= left && textX <= right
    }

    fun startsInEmptySpace(rawX: Float, rawY: Float): Boolean {
        val location = IntArray(2)
        source.getLocationOnScreen(location)
        return !isTouchOnText(rawX - location[0], rawY - location[1])
    }

    fun ownsHorizontalGestureAt(rawX: Float, rawY: Float): Boolean =
        !startsInEmptySpace(rawX, rawY) || actualHorizontalRange() > 0

    fun setWrapEnabled(enabled: Boolean) {
        if (disposed) return
        wrapEnabled = enabled
        source.setHorizontallyScrolling(!enabled)
        if (enabled) source.scrollTo(0, source.scrollY)
        refresh()
    }

    fun ensureCursorVisible() {
        if (disposed) return
        source.suppressCursorAutoScroll = false
        val cursor = source.selectionEnd
        if (cursor >= 0) source.bringPointIntoView(cursor)
        refresh()
    }

    private fun synchronizeScroll() {
        lineNumberScroll.scrollTo(0, 0)
        lineNumbers.translationY = -source.scrollY.toFloat()
        horizontalBar.progress = source.scrollX.coerceIn(0, horizontalRange)
        verticalBar.progress = source.scrollY.coerceIn(0, verticalRange)
    }

    private fun barListener(horizontal: Boolean) = object : SeekBar.OnSeekBarChangeListener {
        override fun onProgressChanged(bar: SeekBar?, progress: Int, fromUser: Boolean) {
            if (!fromUser || disposed) return
            source.suppressCursorAutoScroll = true
            if (horizontal) source.scrollTo(progress.coerceIn(0, horizontalRange), source.scrollY)
            else source.scrollTo(source.scrollX, progress.coerceIn(0, verticalRange))
        }
        override fun onStartTrackingTouch(bar: SeekBar?) = Unit
        override fun onStopTrackingTouch(bar: SeekBar?) = Unit
    }

    private fun ownBarTouch(view: View, event: MotionEvent) {
        onEditorGesture()
        view.parent?.requestDisallowInterceptTouchEvent(event.actionMasked != MotionEvent.ACTION_UP &&
            event.actionMasked != MotionEvent.ACTION_CANCEL)
    }

    private fun handleSourceTouch(event: MotionEvent): Boolean {
        if (disposed) return false
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                pendingEmptyDown?.recycle()
                pendingEmptyDown = null
                startedInEmptySpace = !isTouchOnText(event.x, event.y)
                delegatesGesture = false
                updateHorizontalBar()
                updateVerticalBar()
                source.parent?.requestDisallowInterceptTouchEvent(true)
                touchX = event.x
                touchY = event.y
                initialScrollX = source.scrollX
                initialScrollY = source.scrollY
                initialSelectionStart = source.selectionStart
                initialSelectionEnd = source.selectionEnd
                gestureAxis = 0
                val elapsed = event.eventTime - lastTapTime
                nativeSelectionGesture = !startedInEmptySpace && elapsed in 0..ViewConfiguration.getDoubleTapTimeout().toLong() &&
                    abs(event.x - lastTapX) <= configuration.scaledDoubleTapSlop &&
                    abs(event.y - lastTapY) <= configuration.scaledDoubleTapSlop
                if (startedInEmptySpace) {
                    // Do not let a long hold in blank space select the nearest word.
                    // A stationary tap is delivered to TextView on UP instead.
                    pendingEmptyDown = MotionEvent.obtain(event)
                    return true
                }
                onEditorGesture()
                source.suppressCursorAutoScroll = false
                return false // Native taps, double taps and long presses retain selection handles.
            }
            MotionEvent.ACTION_POINTER_DOWN -> {
                onEditorGesture()
                source.parent?.requestDisallowInterceptTouchEvent(true)
                pendingEmptyDown?.let { down ->
                    try { source.onTouchEvent(down) } finally { down.recycle() }
                    pendingEmptyDown = null
                }
                startedInEmptySpace = false
                delegatesGesture = false
                nativeSelectionGesture = true
                return gestureAxis != 0
            }
            MotionEvent.ACTION_MOVE -> {
                if (delegatesGesture) return true
                // A committed pan has already cancelled (or never entered) TextView.
                // A second pointer must not resume it with an orphan native MOVE.
                if (nativeSelectionGesture) return gestureAxis != 0
                if (gestureAxis == 0 && source.selectionStart != source.selectionEnd &&
                    (source.selectionStart != initialSelectionStart || source.selectionEnd != initialSelectionEnd)) {
                    nativeSelectionGesture = true
                    return false
                }
                val dx = event.x - touchX
                val dy = event.y - touchY
                if (gestureAxis == 0 && maxOf(abs(dx), abs(dy)) >= dragThreshold) {
                    gestureAxis = if (abs(dx) > abs(dy)) 1 else 2
                    if (startedInEmptySpace &&
                        (if (gestureAxis == 1) horizontalRange else verticalRange) == 0 && !isFullscreen()) {
                        delegatesGesture = true
                        pendingEmptyDown?.recycle()
                        pendingEmptyDown = null
                        // ScrollView saw the original DOWN. It can intercept the next
                        // vertical MOVE normally; horizontal navigation remains in Activity.
                        source.parent?.requestDisallowInterceptTouchEvent(false)
                        return true
                    }
                    onEditorGesture()
                    source.suppressCursorAutoScroll = true
                    if (!startedInEmptySpace) {
                        val cancel = MotionEvent.obtain(event).apply { action = MotionEvent.ACTION_CANCEL }
                        try { source.onTouchEvent(cancel) } finally { cancel.recycle() }
                    }
                    pendingEmptyDown?.recycle()
                    pendingEmptyDown = null
                    val length = source.text?.length ?: 0
                    if (initialSelectionStart >= 0 && initialSelectionEnd >= 0) {
                        source.setSelection(initialSelectionStart.coerceIn(0, length), initialSelectionEnd.coerceIn(0, length))
                    }
                }
                if (gestureAxis == 1) source.scrollTo((initialScrollX - dx).toInt().coerceIn(0, horizontalRange), initialScrollY)
                else if (gestureAxis == 2) source.scrollTo(initialScrollX, (initialScrollY - dy).toInt().coerceIn(0, verticalRange))
                return startedInEmptySpace || gestureAxis != 0
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                val consumed = startedInEmptySpace || gestureAxis != 0
                pendingEmptyDown?.let { down ->
                    try {
                        if (event.actionMasked == MotionEvent.ACTION_UP && gestureAxis == 0) {
                            source.suppressCursorAutoScroll = false
                            source.onTouchEvent(down)
                            source.onTouchEvent(event)
                        }
                    } finally { down.recycle() }
                    pendingEmptyDown = null
                }
                source.parent?.requestDisallowInterceptTouchEvent(false)
                if (event.actionMasked == MotionEvent.ACTION_UP && !consumed) {
                    lastTapTime = event.eventTime
                    lastTapX = event.x
                    lastTapY = event.y
                } else lastTapTime = Long.MIN_VALUE
                gestureAxis = 0
                nativeSelectionGesture = false
                startedInEmptySpace = false
                delegatesGesture = false
                return consumed
            }
        }
        return gestureAxis != 0
    }

    fun dispose() {
        if (disposed) return
        disposed = true
        pendingEmptyDown?.recycle()
        pendingEmptyDown = null
        if (source.cppEditorViewport === this) source.cppEditorViewport = null
        source.removeCallbacks(refreshTask)
        source.removeCallbacks(revealTask)
        source.removeTextChangedListener(textWatcher)
        source.removeOnAttachStateChangeListener(attachListener)
        stopObservingLayout()
        source.setOnTouchListener(null)
        source.setOnLongClickListener(null)
        source.setOnScrollChangeListener(null)
        horizontalBar.setOnTouchListener(null)
        verticalBar.setOnTouchListener(null)
        horizontalBar.setOnSeekBarChangeListener(null)
        verticalBar.setOnSeekBarChangeListener(null)
    }

    private fun dp(value: Int) = (value * source.resources.displayMetrics.density).toInt()
}
