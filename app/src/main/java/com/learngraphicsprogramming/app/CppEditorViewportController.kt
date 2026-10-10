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
import kotlin.math.ceil

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
    private val onEditorGesture: () -> Unit = {}
) {
    private var disposed = false
    private var horizontalRange = 0
    private var wrapEnabled = false
    private var verticalRange = 0
    private var gestureAxis = 0
    private var nativeSelectionGesture = false
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
            nativeSelectionGesture = true
            false // Let TextView start its native selection/action mode.
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
        val logicalCount = text.count { it == '\n' } + 1
        val width = maxOf(dp(18), ceil(lineNumbers.paint.measureText(logicalCount.toString()).toDouble()).toInt() + dp(8))
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
        val height = source.compoundPaddingTop + source.lineHeight * 10 + source.compoundPaddingBottom
        val params = editorBody.layoutParams ?: return
        if (params.height != height) {
            params.height = height
            if (params is android.widget.LinearLayout.LayoutParams) params.weight = 0f
            editorBody.layoutParams = params
        }
    }

    fun updateHorizontalBar() {
        if (disposed) return
        val layout = source.layout
        val width = (source.width - source.totalPaddingLeft - source.totalPaddingRight).coerceAtLeast(1)
        // Android includes trailing whitespace in getLineWidth even when the
        // wrapped text fits. Wrapping must never retain a horizontal pan range.
        horizontalRange = if (wrapEnabled || layout == null || source.width <= 0) 0 else {
            var widest = 0f
            for (line in 0 until layout.lineCount) widest = maxOf(widest, layout.getLineWidth(line))
            (widest - width).toInt().coerceAtLeast(0)
        }
        horizontalBar.max = horizontalRange
        horizontalBar.progress = source.scrollX.coerceIn(0, horizontalRange)
        val compact = isFullscreen() && isKeyboardVisible() &&
            source.resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE
        horizontalBar.visibility = if (horizontalRange > 0 && !compact) View.VISIBLE else View.GONE
        val clamped = source.scrollX.coerceIn(0, horizontalRange)
        if (clamped != source.scrollX) source.scrollTo(clamped, source.scrollY)
    }

    fun updateVerticalBar() {
        if (disposed) return
        updateBottomSafety()
        val layout = source.layout
        val height = (source.height - source.totalPaddingTop - source.totalPaddingBottom).coerceAtLeast(1)
        verticalRange = if (layout == null || source.height <= 0) 0 else (layout.height - height).coerceAtLeast(0)
        if (verticalRange <= dp(1)) verticalRange = 0
        verticalBar.max = verticalRange
        verticalBar.progress = source.scrollY.coerceIn(0, verticalRange)
        verticalBar.visibility = if (verticalRange > 0) View.VISIBLE else View.GONE
        val clamped = source.scrollY.coerceIn(0, verticalRange)
        if (clamped != source.scrollY) source.scrollTo(source.scrollX, clamped)
    }

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
        onEditorGesture()
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                source.parent?.requestDisallowInterceptTouchEvent(true)
                touchX = event.x
                touchY = event.y
                initialScrollX = source.scrollX
                initialScrollY = source.scrollY
                initialSelectionStart = source.selectionStart
                initialSelectionEnd = source.selectionEnd
                gestureAxis = 0
                val elapsed = event.eventTime - lastTapTime
                nativeSelectionGesture = elapsed in 0..ViewConfiguration.getDoubleTapTimeout().toLong() &&
                    abs(event.x - lastTapX) <= configuration.scaledDoubleTapSlop &&
                    abs(event.y - lastTapY) <= configuration.scaledDoubleTapSlop
                source.suppressCursorAutoScroll = false
                return false // Native taps, double taps and long presses retain selection handles.
            }
            MotionEvent.ACTION_POINTER_DOWN -> {
                nativeSelectionGesture = true
                return gestureAxis != 0
            }
            MotionEvent.ACTION_MOVE -> {
                if (nativeSelectionGesture) return false
                if (gestureAxis == 0 && source.selectionStart != source.selectionEnd &&
                    (source.selectionStart != initialSelectionStart || source.selectionEnd != initialSelectionEnd)) {
                    nativeSelectionGesture = true
                    return false
                }
                val dx = event.x - touchX
                val dy = event.y - touchY
                if (gestureAxis == 0 && maxOf(abs(dx), abs(dy)) >= dragThreshold) {
                    gestureAxis = if (abs(dx) > abs(dy)) 1 else 2
                    source.suppressCursorAutoScroll = true
                    val cancel = MotionEvent.obtain(event).apply { action = MotionEvent.ACTION_CANCEL }
                    try { source.onTouchEvent(cancel) } finally { cancel.recycle() }
                    val length = source.text?.length ?: 0
                    if (initialSelectionStart >= 0 && initialSelectionEnd >= 0) {
                        source.setSelection(initialSelectionStart.coerceIn(0, length), initialSelectionEnd.coerceIn(0, length))
                    }
                }
                if (gestureAxis == 1) source.scrollTo((initialScrollX - dx).toInt().coerceIn(0, horizontalRange), initialScrollY)
                else if (gestureAxis == 2) source.scrollTo(initialScrollX, (initialScrollY - dy).toInt().coerceIn(0, verticalRange))
                return gestureAxis != 0
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                source.parent?.requestDisallowInterceptTouchEvent(false)
                val consumed = gestureAxis != 0
                if (event.actionMasked == MotionEvent.ACTION_UP && !consumed) {
                    lastTapTime = event.eventTime
                    lastTapX = event.x
                    lastTapY = event.y
                } else lastTapTime = Long.MIN_VALUE
                gestureAxis = 0
                nativeSelectionGesture = false
                return consumed
            }
        }
        return gestureAxis != 0
    }

    fun dispose() {
        if (disposed) return
        disposed = true
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
