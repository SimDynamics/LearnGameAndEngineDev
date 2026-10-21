package com.learngraphicsprogramming.app

import android.app.Activity
import android.content.Context
import android.content.ContextWrapper
import android.content.SharedPreferences
import android.graphics.Rect
import android.graphics.Typeface
import android.view.View
import android.view.ViewTreeObserver
import android.view.WindowManager
import android.view.accessibility.AccessibilityEvent
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import androidx.appcompat.app.AlertDialog

/** Resolves only a legacy ambiguity, with an explicit choice by the owner of the input. */
internal object CppInputDraftReview {
    data class TailPreview(val text: String, val truncated: Boolean)

    internal fun preview(input: String, maxCodePoints: Int = 80): TailPreview {
        require(maxCodePoints > 0)
        val length = input.codePointCount(0, input.length)
        val truncated = length > maxCodePoints
        val start = if (truncated) input.offsetByCodePoints(0, length - maxCodePoints) else 0
        val escaped = buildString {
            for (character in input.substring(start)) {
                append(when (character) {
                    ' ' -> "·"
                    '\t' -> "\\t"
                    '\n' -> "\\n"
                    '\r' -> "\\r"
                    '\\' -> "\\\\"
                    else -> character.toString()
                })
            }
        }
        return TailPreview(escaped, truncated)
    }

    fun attach(input: EditText, preferences: SharedPreferences, draftKey: String,
               onResolved: (String) -> Unit) {
        Controller(input, preferences, draftKey, onResolved).attach()
    }

    private class Controller(
        private val input: EditText,
        private val preferences: SharedPreferences,
        private val draftKey: String,
        private val onResolved: (String) -> Unit
    ) : View.OnAttachStateChangeListener {
        private var dialog: AlertDialog? = null
        private var observedTree: ViewTreeObserver? = null
        private var deferred = false
        private var finished = false
        private val layoutListener = ViewTreeObserver.OnGlobalLayoutListener { tryShow() }
        private val scrollListener = ViewTreeObserver.OnScrollChangedListener { tryShow() }
        private val focusListener = ViewTreeObserver.OnWindowFocusChangeListener {
            if (it) tryShow()
        }

        fun attach() {
            input.addOnAttachStateChangeListener(this)
            if (input.isAttachedToWindow) onViewAttachedToWindow(input)
        }

        override fun onViewAttachedToWindow(view: View) {
            if (finished) return
            deferred = false
            observedTree = input.viewTreeObserver.also {
                it.addOnGlobalLayoutListener(layoutListener)
                it.addOnScrollChangedListener(scrollListener)
                it.addOnWindowFocusChangeListener(focusListener)
            }
            input.post { tryShow() }
        }

        override fun onViewDetachedFromWindow(view: View) {
            stopObserving()
            dismiss()
            deferred = false
        }

        private fun activity(): Activity? {
            var context: Context = input.context
            while (context !is Activity) {
                val base = (context as? ContextWrapper)?.baseContext ?: return null
                if (base === context) return null
                context = base
            }
            return context
        }

        private fun available(requireWindowFocus: Boolean): Boolean {
            val owner = activity() ?: return false
            if (owner.isFinishing || owner.isDestroyed || !input.isAttachedToWindow || !input.isShown)
                return false
            if (requireWindowFocus && !input.hasWindowFocus()) return false
            return input.getGlobalVisibleRect(Rect())
        }

        private fun tryShow() {
            if (finished || deferred || dialog != null || !available(requireWindowFocus = true)) return
            val review = CppLessonInputDraft.review(preferences, draftKey)
            if (review == null) {
                finish()
                return
            }
            show(review)
        }

        private fun show(review: CppLessonInputDraft.Review) {
            val builder = AlertDialog.Builder(input.context)
            val context = builder.context
            val colors = context.obtainStyledAttributes(intArrayOf(android.R.attr.textColorPrimary))
            val reviewInk = try { colors.getColorStateList(0) } finally { colors.recycle() }
            val body = LinearLayout(context).apply {
                orientation = LinearLayout.VERTICAL
                setPadding(dp(24), dp(8), dp(24), dp(8))
            }
            fun text(resource: Int) = TextView(context).apply {
                setText(resource)
                if (reviewInk != null) setTextColor(reviewInk)
                setPadding(0, dp(6), 0, dp(6))
                body.addView(this)
            }
            fun tail(value: String, previewTag: String) {
                val preview = preview(value)
                if (preview.truncated) text(R.string.cpp_input_review_truncated)
                body.addView(TextView(context).apply {
                    tag = previewTag
                    text = preview.text
                    if (reviewInk != null) setTextColor(reviewInk)
                    typeface = Typeface.MONOSPACE
                    textSize = 15f
                    textDirection = View.TEXT_DIRECTION_LTR
                    setTextIsSelectable(true)
                    setPadding(0, dp(4), 0, dp(10))
                })
            }
            text(R.string.cpp_input_review_explanation)
            text(R.string.cpp_input_review_legend)
            text(R.string.cpp_input_review_original)
            tail(review.input, "cpp-input-draft-review-original")
            text(R.string.cpp_input_review_proposed)
            tail(review.proposedInput, "cpp-input-draft-review-proposed")
            val error = text(R.string.cpp_input_review_error).apply {
                tag = "cpp-input-draft-review-error"
                visibility = View.GONE
                accessibilityLiveRegion = View.ACCESSIBILITY_LIVE_REGION_POLITE
            }
            val created = builder
                .setTitle(R.string.cpp_input_review_title)
                .setView(ScrollView(context).apply { addView(body) })
                .setPositiveButton(R.string.cpp_input_review_keep, null)
                .setNegativeButton(R.string.cpp_input_review_remove, null)
                .setNeutralButton(R.string.cpp_input_review_later, null)
                .create()
            dialog = created
            created.setOnDismissListener {
                if (dialog === created) dialog = null
                // Back, tapping outside, and "later" leave the same value pending for reopening.
                deferred = true
            }
            created.setOnShowListener {
                created.window?.decorView?.tag = "cpp-input-draft-review"
                // All choices use the dialog's legible text ink, with equal emphasis.
                for (which in listOf(AlertDialog.BUTTON_POSITIVE, AlertDialog.BUTTON_NEGATIVE, AlertDialog.BUTTON_NEUTRAL)) {
                    if (reviewInk != null) created.getButton(which).setTextColor(reviewInk)
                }
                fun choose(removeFourSpaces: Boolean) {
                    if (!available(requireWindowFocus = false)) {
                        created.dismiss()
                        return
                    }
                    val chosen = CppLessonInputDraft.resolve(preferences, draftKey, review, removeFourSpaces)
                    if (chosen == null) {
                        error.visibility = View.VISIBLE
                        error.sendAccessibilityEvent(AccessibilityEvent.TYPE_ANNOUNCEMENT)
                        return
                    }
                    finish()
                    onResolved(chosen)
                }
                created.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener { choose(false) }
                created.getButton(AlertDialog.BUTTON_NEGATIVE).setOnClickListener { choose(true) }
                created.getButton(AlertDialog.BUTTON_NEUTRAL).setOnClickListener { created.dismiss() }
            }
            try {
                created.show()
            } catch (_: WindowManager.BadTokenException) {
                dialog = null
                deferred = true
            }
        }

        private fun dp(value: Int): Int = (value * input.resources.displayMetrics.density + 0.5f).toInt()

        private fun stopObserving() {
            observedTree?.takeIf { it.isAlive }?.let {
                it.removeOnGlobalLayoutListener(layoutListener)
                it.removeOnScrollChangedListener(scrollListener)
                it.removeOnWindowFocusChangeListener(focusListener)
            }
            observedTree = null
        }

        private fun dismiss() {
            val previous = dialog
            dialog = null
            previous?.setOnDismissListener(null)
            previous?.dismiss()
        }

        private fun finish() {
            finished = true
            stopObserving()
            input.removeOnAttachStateChangeListener(this)
            dismiss()
        }
    }
}
