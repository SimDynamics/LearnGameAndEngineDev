package com.learngraphicsprogramming.app.math

import android.app.Activity
import android.graphics.Color
import android.graphics.Rect
import android.graphics.drawable.ColorDrawable
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.EditText
import android.widget.PopupWindow
import android.widget.ScrollView
import com.learngraphicsprogramming.app.AppearanceSettingsRepository
import com.learngraphicsprogramming.app.R
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsAnimationCompat
import java.util.WeakHashMap

/** A single non-focusable toolbar follows the resized window immediately above the real IME.
 * Measures its own height and asks the existing ScrollView to reveal the input plus that space.
 */
class MathKeyboardHost private constructor(private val activity: Activity) {
    private val root = activity.findViewById<View>(android.R.id.content)
    private var keyboard: MathKeyboardView? = null
    private var activeInput: EditText? = null
    private var popup: PopupWindow? = null
    private var originalParent: ViewGroup? = null
    private var originalIndex = 0
    private var originalParams: ViewGroup.LayoutParams? = null
    private var previousGeometry = ""
    private var compactHeader: View? = null
    private var originalHeaderVisibility = View.VISIBLE

    init {
        root.viewTreeObserver.addOnGlobalLayoutListener { update() }
        // Insets may arrive after the resize/layout event on real IMEs. Listen to both sources.
        ViewCompat.setOnApplyWindowInsetsListener(root) { _, insets ->
            root.post { update() }
            insets
        }
        ViewCompat.setWindowInsetsAnimationCallback(root, object : WindowInsetsAnimationCompat.Callback(DISPATCH_MODE_CONTINUE_ON_SUBTREE) {
            override fun onProgress(insets: WindowInsetsCompat, runningAnimations: MutableList<WindowInsetsAnimationCompat>): WindowInsetsCompat {
                root.post { update() }
                return insets
            }
        })
        ViewCompat.requestApplyInsets(root)
        root.viewTreeObserver.addOnGlobalFocusChangeListener { _, _ ->
            root.post {
                // Focus can move again before this callback runs; never dismiss the latest binding
                // because an earlier input has already lost focus.
                val input = (root.findFocus() as? EditText)?.takeIf {
                    it.isEnabled && it.tag == MathInputFocusOrder.GESTURE_OWNER_TAG && it.hasFocus()
                }
                if (input == null) dismissCurrent()
                else if (activeInput !== input) {
                    // Graph and number-line coordinate editors use the same toolbar infrastructure.
                    MathKeyboardView(activity, AppearanceSettingsRepository.getRouteAccentColor(activity,
                        AppearanceSettingsRepository.ROUTE_MATH_PHYSICS)).bind(input, "NUMERIC")
                } else update()
            }
        }
    }

    internal fun show(bar: MathKeyboardView, input: EditText) {
        if (keyboard !== bar) {
            dismissCurrent()
            keyboard = bar
            originalParent = bar.parent as? ViewGroup
            originalIndex = originalParent?.indexOfChild(bar) ?: 0
            originalParams = bar.layoutParams
            originalParent?.removeView(bar)
            popup = PopupWindow(bar, 1, 1, false).apply {
                isOutsideTouchable = false
                inputMethodMode = PopupWindow.INPUT_METHOD_NOT_NEEDED
                softInputMode = WindowManager.LayoutParams.SOFT_INPUT_ADJUST_NOTHING
                setBackgroundDrawable(ColorDrawable(Color.rgb(24, 26, 31)))
                elevation = MathWidgetUi.dp(activity, 8).toFloat()
            }
        }
        activeInput = input
        previousGeometry = ""
        root.post { update() }
    }

    internal fun hide(bar: MathKeyboardView) {
        if (keyboard === bar) dismissCurrent()
    }

    private fun update() {
        val bar = keyboard ?: return
        val input = activeInput ?: return
        if (activity.isFinishing || activity.isDestroyed || !input.isAttachedToWindow || !input.isEnabled || !input.hasFocus()) {
            dismissCurrent()
            return
        }
        val insets = ViewCompat.getRootWindowInsets(root)
        val imeVisible = insets?.isVisible(WindowInsetsCompat.Type.ime()) == true
        val frame = Rect()
        root.getWindowVisibleDisplayFrame(frame)
        if (!imeVisible || !activity.hasWindowFocus() || frame.width() <= 0) {
            popup?.dismiss()
            restoreHeader()
            previousGeometry = ""
            return
        }
        bar.visibility = View.VISIBLE
        bar.measure(View.MeasureSpec.makeMeasureSpec(frame.width(), View.MeasureSpec.EXACTLY),
            View.MeasureSpec.makeMeasureSpec(frame.height(), View.MeasureSpec.AT_MOST))
        val height = bar.measuredHeight.coerceAtLeast(MathWidgetUi.dp(activity, 48))
        val margin = MathWidgetUi.dp(activity, 8)
        val scroll = generateSequence(input.parent) { it.parent }.filterIsInstance<ScrollView>().firstOrNull()
        // In landscape a tall IME can leave less room than the field and toolbar together.
        // Temporarily release the lesson header's space, restoring it when editing ends.
        if (scroll != null && compactHeader == null && scroll.height < input.height + height + margin * 2) {
            (activity.findViewById<View>(R.id.textTopLesson)?.parent?.parent as? View)?.let { header ->
                if (header.visibility == View.VISIBLE) {
                    compactHeader = header
                    originalHeaderVisibility = header.visibility
                    header.visibility = View.GONE
                    root.post { update() }
                    return
                }
            }
        }
        val location = IntArray(2)
        input.getLocationOnScreen(location)
        val geometry = "${frame.flattenToString()}:$height:${location[1]}:${input.height}"
        if (geometry == previousGeometry) return
        previousGeometry = geometry
        val y = (frame.bottom - height).coerceAtLeast(frame.top)
        popup?.let { window ->
            if (window.isShowing) window.update(frame.left, y, frame.width(), height)
            else {
                window.width = frame.width()
                window.height = height
                window.showAtLocation(root, Gravity.TOP or Gravity.START, frame.left, y)
            }
        }
        if (scroll != null) {
            val scrollBounds = Rect()
            if (scroll.getGlobalVisibleRect(scrollBounds)) {
                val visibleBottom = y - margin
                val visibleTop = scrollBounds.top + margin
                val inputBottom = location[1] + input.height
                val delta = when {
                    // A very short/split window cannot fit the entire field. Use one stable
                    // alignment instead of alternating top/bottom on successive layout frames.
                    input.height > visibleBottom - visibleTop -> location[1] - visibleTop
                    inputBottom > visibleBottom -> inputBottom - visibleBottom
                    location[1] < visibleTop -> location[1] - visibleTop
                    else -> 0
                }
                if (delta != 0) { scroll.scrollBy(0, delta); root.post { update() } }
            }
        } else input.requestRectangleOnScreen(Rect(0, -margin, input.width, input.height + height + margin), true)
    }

    private fun dismissCurrent() {
        restoreHeader()
        popup?.dismiss()
        val bar = keyboard
        if (bar != null) {
            (bar.parent as? ViewGroup)?.removeView(bar)
            bar.visibility = View.GONE
            originalParent?.let { parent ->
                parent.addView(bar, originalIndex.coerceAtMost(parent.childCount), originalParams)
            }
        }
        keyboard = null
        activeInput = null
        popup = null
        originalParent = null
        originalParams = null
        previousGeometry = ""
    }

    private fun restoreHeader() {
        compactHeader?.visibility = originalHeaderVisibility
        compactHeader = null
    }

    fun close() {
        dismissCurrent()
        hosts.remove(activity)
    }

    companion object {
        private val hosts = WeakHashMap<Activity, MathKeyboardHost>()
        fun install(activity: Activity): MathKeyboardHost = hosts.getOrPut(activity) { MathKeyboardHost(activity) }
        internal fun current(activity: Activity): MathKeyboardHost? = hosts[activity]
    }
}
